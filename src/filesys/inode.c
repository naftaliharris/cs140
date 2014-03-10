#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "filesys/cache.h"

//TO-DO:
//1.Add a lock to the free_map so that it is synchronized

/*
 -----------------------------------------------------------
 DESCRIPTION: Max number of blocks in a file given that
    max disk size is 8mb and we subtract metadata.
    STEP 1: If we have a file, assume extreme case of 1 directory
        and 1 file. That means we have 1 inode for the directory, 
        and 1 inode for the file, which means our metadata
        is 2*(BLOCK_SECTOR_SIZE) = 2*512 = 1024
    STEP 2: Subtract 8 mb from 1024 bytes, to get max size of 
        file in bytes. 8mb = 8389000 bytes - 1024 bytes = 
        8387976 bytes which is max file size.
    STEP 3: determine the max number of blocks that a file
        can have given max file size in bytes is 8387976.
        8387976 / BLOCK_SECTOR_SIZE = 8387976 / 512 = 
        16382.7, round up to 16383 blocks. 
    STEP 4: We are given that the inode can only by 512 bytes in 
        size. Thusm we have to support indirect and doubly 
        indirect blocks to track 16383 blocks.
    NOTE: another edge case one could consider is the case
        where the root directory is the only directory,
        and it is populated with a bunch of 0 size files. 
        In this case, the max size of the directory would be
        (8Mb - 512 bytes)/512, rounded up. However, in this
        case, the 0 sized files are unusable, and thus
        this case is trivial, and we do not account for it.
 -----------------------------------------------------------
 */

#define INODE_MAGIC 0x494e4f44 /* Identifies an inode. */
#define NUM_BLOCK_IDS_IN_INODE 126
#define NUM_DIRECT_BLOCKS 124
#define NUM_INDIRECT_BLOCKS 1
#define NUM_DOUBLE_INDIRECT_BLOCKS 1
#define MAX_FILE_LENGTH_IN_BYTES 8387976
#define NUM_BLOCK_IDS_PER_BLOCK 128
#define INODE_INDIRECT_BLOCK_INDEX 124
#define INDOE_DOUBLE_INDIRECT_BLOCK_INDEX 125

/* 
 -----------------------------------------------------------
 On-disk inode.
 Must be exactly BLOCK_SECTOR_SIZE bytes long.
 NOTE: from the length of the file, we can determine the 
    number of blocks in use, by taking length/512 and rounding
    up.
 NOTE: because we have 8 bytes between the length and magic
    fields, that leaves 504 bytes, or an array of size 126
    to contain block sector numbers for the blocks that 
    contain file data. We will use 124 direct references, 
    1 indirect reference, and 1 doubly indirect reference. This
    will allow us to support files of size 8Mb, and likewise 
    minimize the number of disk accesses, as we only go to 
    indirect and doubly indirect for the last blocks of the
    file.
 NOTE: blocks index 0-123 contain block sector numbers of blocks
    that contain actual file data.
 NOTE: blocks index 124 contains the block sector number of 
    the block that supports indirection. This block will
    contain 128 block sector numbers of blocks that contain
    actual file data.
 NOTE: blocks index 125 contains the block sector number of 
    the block that supports double indirection. This block
    will contain 128 block sector numbers, each of which
    will contain 128 block sector numbers, which correspond
    to file data.
 -----------------------------------------------------------
 */
struct inode_disk
{
    //block_sector_t start;               /* First data sector. */
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    block_sector_t blocks[NUM_BLOCK_IDS_IN_INODE] /*array containing the sector 
                                                   numbers of blocks that contain
                                                   file data. Please see comment above
                                                   for indication as to indexing and
                                                   indirection, and double indirection. */
    
    //uint32_t unused[125];               /* Not used. */
};



/*
 -----------------------------------------------------------
 Returns the number of sectors to allocate for an inode SIZE
   bytes long. 
 -----------------------------------------------------------
 */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}



/* In-memory inode. */
/* This is in kernel memory I think */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */
  };




/* 
 -----------------------------------------------------------
 Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. 
 -----------------------------------------------------------
 */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);
  if (pos < inode->data.length)
    return inode->data.start + pos / BLOCK_SECTOR_SIZE;
  else
    return -1;
}





/* 
 -----------------------------------------------------------
 List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. 
 -----------------------------------------------------------
 */
static struct list open_inodes;
static struct lock open_inodes_lock;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}




/* 
 -----------------------------------------------------------
 Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. 
 NOTE: alocates blocks needed to track file data
    of size. If num_sectors_needed is less
 NOTE: if we encounter an error in the middle of allocating
    blocks for the file data, we have to undo what we have 
    previously done, and return fase to indicate failure.
    This amounts to releaseing the previously aquired blocks
    and freeing the disk_inode we allocated.
 NOTE: we do not allocate the inode's sector until all 
    of the inode data has been updated.
 NOTE: using a local copy of a disk_sector, and then writing
    that local copy to a cache entry at the very end allows
    us to minimize the amount of time we hold the cache
    lock. 
 -----------------------------------------------------------
 */
bool
inode_create (block_sector_t sector, off_t length)
{
    if (length > MAX_FILE_LENGTH_IN_BYTES) return false;
    struct inode_disk *disk_inode = NULL;
    //bool success = false;
    
    ASSERT (length >= 0);
    
    /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
    ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);
    
    disk_inode = calloc (1, sizeof *disk_inode);
    if (disk_inode == NULL) return false;
    disk_inode->length = length;
    disk_inode->magic = INODE_MAGIC;
    //now allocate disk sectors for the file's data
    size_t num_sectors_needed = bytes_to_sectors(length);
    size_t num_sectors_allocated = 0;
    
    /* here we do direct allocation */
    int direct_index;
    for (direct_index = 0; direct_index < NUM_DIRECT_BLOCKS; direct_index++) {
        block_sector_t curr_sector = 0;
        bool success = free_map_allocate(1, &curr_sector);
        if (success == false) {
            //here we undo what we previously done and return false
        }
        disk_inode->blocks[direct_index] = curr_sector;
        num_sectors_allocated++;
        num_sectors_needed--;
        if (num_sectors_needed == 0) break;
    }
    
    
    /* here we do indirect allocation */
    if (num_sectors_needed > 0) {
        block_sector_t* local_indirect_block = calloc(1, BLOCK_SECTOR_SIZE);
        if (local_indirect_block == NULL) {
            //here we undo what we previously done and return false
        }
        int indirect_index;
        for (indirect_index = 0; indirect_index < NUM_BLOCK_IDS_PER_BLOCK; indirect_index++) {
            block_sector_t curr_sector = 0;
            bool success = free_map_allocate(1, &curr_sector);
            if (success == false) {
                //here we undo what we previously done and return false
            }
            block_sector_t* curr_indirect_slot = local_indirect_block + indirect_index;
            *curr_indirect_slot = curr_sector;
            num_sectors_allocated++;
            num_sectors_needed--;
            if (num_sectors_needed == 0) break;
        }
        block_sector_t indirect_block = 0;
        bool success = free_map_allocate(1, &indirect_block);
        if (success == false) {
            //here we undo what we previously done and return false
        }
        disk_inode->blocks[INODE_INDIRECT_BLOCK_INDEX] = indirect_block;
        struct cache_entry* entry = get_cache_entry_for_sector(indirect_block, true);
        write_to_cache(entry, local_indirect_block, 0, BLOCK_SECTOR_SIZE);
        release_cache_lock_for_write(&entry->lock);
        free(local_indirect_block);
        num_sectors_allocated++;
    }
    
    /* here we do double indirect allocation */
    if (num_sectors_needed > 0) {
        block_sector_t* local_double_indirect_block = calloc(1, BLOCK_SECTOR_SIZE);
        if (local_double_indirect_block == NULL) {
            //here we undo what we previously done and return false
        }
        
        int double_indirect_index;
        for (double_indirect_index = 0; double_indirect_index < NUM_BLOCK_IDS_PER_BLOCK; double_indirect_index++) {
            block_sector_t* local_indirect_block = calloc(1, BLOCK_SECTOR_SIZE);
            if (local_indirect_block == NULL) {
                //here we undo what we previously done and return false
            }
            
            int indirect_index;
            for (indirect_index = 0; indirect_index < NUM_BLOCK_IDS_PER_BLOCK; indirect_index++) {
                block_sector_t curr_sector = 0;
                bool success = free_map_allocate(1, &curr_sector);
                if (success == false) {
                    //here we undo what we previously done and return false
                }
                block_sector_t* curr_indirect_slot = local_indirect_block + indirect_index;
                *curr_indirect_slot = curr_sector;
                num_sectors_allocated++;
                num_sectors_needed--;
                if (num_sectors_needed == 0) break;
            }
            block_sector_t curr_indirect_block = 0;
            bool success = free_map_allocate(1, &curr_indirect_block);
            if (success == false) {
                //here we undo what we previously done and return false
            }
            block_sector_t* curr_double_indirect_slot = local_indirect_block + double_indirect_index;
            *curr_double_indirect_slot = curr_indirect_block;
            struct cache_entry* entry = get_cache_entry_for_sector(curr_indirect_block, true);
            write_to_cache(entry, local_indirect_block, 0, BLOCK_SECTOR_SIZE);
            release_cache_lock_for_write(&entry->lock);
            free(local_indirect_block);
            num_sectors_allocated++;
            if (num_sectors_needed == 0) break;
        }
        //here we add the double indirect block to the inode, and cache it
        block_sector_t double_indirect_block = 0;
        bool success = free_map_allocate(1, &double_indirect_block);
        if (success == false) {
            //here we undo what we previously done and return false
        }
        disk_inode->blocks[INDOE_DOUBLE_INDIRECT_BLOCK_INDEX] = double_indirect_block;
        struct cache_entry* entry = get_cache_entry_for_sector(double_indirect_block, true);
        write_to_cache(entry, local_double_indirect_block, 0, BLOCK_SECTOR_SIZE);
        release_cache_lock_for_write(&entry->lock);
        free(local_double_indirect_block);
        num_sectors_allocated++;
    }
    
    /* once we get here, we have finished allocating blocks for the file, and the inode
     is completly updated. Note, the inode sector has allready been allocated for us
     so all we have to do is write to the cache for the sector, writing the 
     data contained in the local copy of disk_inode*/
    struct cache_entry* entry = get_cache_entry_for_sector(sector, true);
    write_to_cache(entry, disk_inode, 0, BLOCK_SECTOR_SIZE);
    release_cache_lock_for_write(&entry->lock);
    free(disk_inode);
    
    return true;
}





/* 
 -----------------------------------------------------------
 Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. 
 -----------------------------------------------------------
 */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
    lock_acquire(&open_inodes_lock);
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
            lock_release(&open_inodes_lock);
          return inode; 
        }
    }
    

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
    lock_release(&open_inodes_lock);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
    struct cache_entry* entry = get_cache_entry_for_sector(inode->sector, false);
    read_from_cache(entry, &inode->data, 0, BLOCK_SECTOR_SIZE);
    release_cache_lock_for_read(&entry->lock);
  //block_read (fs_device, inode->sector, &inode->data);
  return inode;
}





/* 
 -----------------------------------------------------------
 Reopens and returns INODE. 
 -----------------------------------------------------------
 */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}





/* 
 -----------------------------------------------------------
 Returns INODE's inode number. 
 -----------------------------------------------------------
 */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}




/* 
 -----------------------------------------------------------
 Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. 
 NOTE: if we are removing an inode, then we must release resources
    inode used to track file data.
 NOTE: freeing resources:
    1. Release all blocks that the inode was previously using
    To do this, first clear the cache entry that contains
    a given block, if an entry exists. Then call free_map_release
    on the block to give the block back.
 NOTE: in the case where we are just closing the inode, but
    not removing, the file data will be consistent, as if
    it is in the cache, will be written back to block 
    during eviction.
 -----------------------------------------------------------
 */
void
inode_close (struct inode *inode)
{
    /* Ignore null pointer. */
    if (inode == NULL) return;
    
    /* Release resources if this was the last opener. */
    if (--inode->open_cnt == 0) {
        /* Remove from inode list and release lock. */
        lock_acquire(&open_inodes_lock);
        list_remove (&inode->elem);
        lock_release(&open_inodes_lock);
        
        /* Deallocate blocks if removed. */
        if (inode->removed) {
            //here we need to release all blocks and clear cache entries
            
            
            
            
            free_map_release (inode->sector, 1);
            free_map_release (inode->data.start,
                              bytes_to_sectors (inode->data.length));
        }
        
        free (inode);
    }
}




/* 
 -----------------------------------------------------------
 Marks INODE to be deleted when it is closed by the last caller who
   has it open. 
 -----------------------------------------------------------
 */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}





/* 
 -----------------------------------------------------------
 Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. 
 -----------------------------------------------------------
 */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector directly into caller's buffer. */
            struct cache_entry* entry = get_cache_entry_for_sector(sector_idx, false);
            read_from_cache(entry, buffer+bytes_read, 0, BLOCK_SECTOR_SIZE);
            release_cache_lock_for_read(&entry->lock);
          //block_read (fs_device, sector_idx, buffer + bytes_read);
        }
      else 
        {
          /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
          if (bounce == NULL)
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
            struct cache_entry* entry = get_cache_entry_for_sector(sector_idx, false);
            read_from_cache(entry, buffer+bytes_read, sector_ofs, chunk_size);
            release_cache_lock_for_read(&entry->lock);
          //block_read (fs_device, sector_idx, bounce);
          //memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);

  return bytes_read;
}

/* 
 -----------------------------------------------------------
 Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) 
 -----------------------------------------------------------
 */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Write full sector directly to disk. */
            struct cache_entry* entry = get_cache_entry_for_sector(sector_idx, true);
            write_to_cache(entry, (const void*)buffer+bytes_written, 0, BLOCK_SECTOR_SIZE);
            release_cache_lock_for_write(&entry->lock);
          //block_write (fs_device, sector_idx, buffer + bytes_written);
        }
      else 
        {
          /* We need a bounce buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }

          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
         /* if (sector_ofs > 0 || chunk_size < sector_left)
            block_read (fs_device, sector_idx, bounce);
          else
            memset (bounce, 0, BLOCK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          block_write (fs_device, sector_idx, bounce);*/
            struct cache_entry* entry = get_cache_entry_for_sector(sector_idx, true);
            write_to_cache(entry, (const void*)buffer+bytes_written, sector_ofs, chunk_size);
            release_cache_lock_for_write(&entry->lock);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  free (bounce);

  return bytes_written;
}



/* 
 -----------------------------------------------------------
 Disables writes to INODE.
   May be called at most once per inode opener. 
 -----------------------------------------------------------
 */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}




/* 
 -----------------------------------------------------------
 Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. 
 -----------------------------------------------------------
 */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}



/* 
 -----------------------------------------------------------
 Returns the length, in bytes, of INODE's data. 
 -----------------------------------------------------------
 */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}
