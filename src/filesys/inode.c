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
struct inode_disk {
    //block_sector_t start;             /* First data sector. */
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    block_sector_t blocks[NUM_BLOCK_IDS_IN_INODE]; /*array containing the sector
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
    struct lock data_lock;              /* lock protecting internal inode data */
    struct lock extend_inode_lock;      /* ensures writing past EOF and subsequent inode_extension is atomic */
    //struct inode_disk data;             /* Inode content. */
};

static bool settup_direct_blocks(struct cache_entry* disk_inode_cache_entry, size_t* num_sectors_needed, size_t* num_sectors_allocated);
static bool settup_indirect_blocks(struct cache_entry* disk_inode_cache_entry, size_t* num_sectors_needed, size_t* num_sectors_allocated);
static bool settup_double_indirect_blocks(struct cache_entry* disk_inode_cache_entry, size_t* num_sectors_needed, size_t* num_sectors_allocated);
static void release_blocks(struct cache_entry* disk_inode_cache_entry, off_t length);
static void clear_direct_blocks(struct cache_entry* disk_inode_cache_entry, size_t* num_blocks_to_clear);
static void clear_indirect_blocks(struct cache_entry* disk_inode_cache_entry, size_t* num_blocks_to_clear, block_sector_t indirect_block_number);
static void clear_double_indirect_blocks(struct cache_entry* disk_inode_cache_entry, size_t* num_blocks_to_clear);
static block_sector_t get_direct_block_sector_number(struct cache_entry* disk_inode_cache_entry, unsigned index);
static block_sector_t get_indirect_block_sector_number(struct cache_entry* disk_inode_cache_entry, unsigned index);
static block_sector_t get_double_indirect_block_sector_number(struct cache_entry* disk_inode_cache_entry, unsigned index);
static off_t extend_inode(struct inode* inode, void* buffer, off_t size, off_t offset);

/*
 -----------------------------------------------------------
 DESCRIPTION: returns the sector number of the block that
    is at "index" position in the array
    of blocks contained in the inode.
 NOTE: in this case, we are an direct block.
 NOTE: Releases the cache lock for the inode!!
 -----------------------------------------------------------
 */
static block_sector_t get_direct_block_sector_number(struct cache_entry* disk_inode_cache_entry, unsigned index) {
    block_sector_t block_number = 0;
    off_t offset = sizeof(off_t) + sizeof(unsigned) + (index * sizeof(block_sector_t));
    read_from_cache(disk_inode_cache_entry, &block_number, offset, sizeof(block_sector_t));
    release_cache_lock_for_read(&disk_inode_cache_entry->lock);
    return block_number;
}

/*
 -----------------------------------------------------------
 DESCRIPTION: returns the sector number of the block that
    is at "index" position in the array
    of blocks contained in the inode.
 NOTE: in this case, we are an indirect block.
 NOTE: Releases the cache lock for the inode!!
 -----------------------------------------------------------
 */
static block_sector_t get_indirect_block_sector_number(struct cache_entry* disk_inode_cache_entry, unsigned index) {
    block_sector_t indirect_block_number = get_direct_block_sector_number(disk_inode_cache_entry, NUM_DIRECT_BLOCKS);
    struct cache_entry* indirect_block_cache_entry = get_cache_entry_for_sector(indirect_block_number, false);
    index = index - NUM_DIRECT_BLOCKS; //this gives us an index relative to indirect block
    block_sector_t block_number = 0;
    off_t offset = index * sizeof(block_sector_t);
    read_from_cache(indirect_block_cache_entry, &block_number, offset, sizeof(block_sector_t));
    release_cache_lock_for_read(&indirect_block_cache_entry->lock);
    return block_number;
}

/*
 -----------------------------------------------------------
 DESCRIPTION: returns the sector number of the block that
    is at "index" position in the array
    of blocks contained in the inode.
 NOTE: in this case, we are a doubly indirect block.
 NOTE: Releases the cache lock for the inode!!
 -----------------------------------------------------------
 */
static block_sector_t get_double_indirect_block_sector_number(struct cache_entry* disk_inode_cache_entry, unsigned index) {
    block_sector_t double_indirect_block_number = get_direct_block_sector_number(disk_inode_cache_entry, NUM_DIRECT_BLOCKS + NUM_INDIRECT_BLOCKS);
    struct cache_entry* double_indirect_block_cache_entry = get_cache_entry_for_sector(double_indirect_block_number, false);
    int index_in_double_indirect_block = 0;
    index = index - NUM_DIRECT_BLOCKS - NUM_BLOCK_IDS_PER_BLOCK; //This gives us an index relative to double indirect.
    while (true) {
        if (index < NUM_BLOCK_IDS_PER_BLOCK) break;
        index = index - NUM_BLOCK_IDS_PER_BLOCK;
        index_in_double_indirect_block++;
    }
    block_sector_t indirect_block_number = 0;
    off_t offset = index_in_double_indirect_block * sizeof(block_sector_t);
    read_from_cache(double_indirect_block_cache_entry, &indirect_block_number, offset, sizeof(block_sector_t));
    release_cache_lock_for_read(&double_indirect_block_cache_entry->lock);
    
    struct cache_entry* indirect_block_cache_entry = get_cache_entry_for_sector(indirect_block_number, false);
    block_sector_t block_number = 0;
    offset = index * sizeof(block_sector_t);
    read_from_cache(indirect_block_cache_entry, &block_number, offset, sizeof(block_sector_t));
    release_cache_lock_for_read(&indirect_block_cache_entry->lock);
    
    return block_number;
}



/*
 -----------------------------------------------------------
 Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. 
 returns -2 if pos exceeds max_file_length_in_bytes
 -----------------------------------------------------------
 */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos)
{
    ASSERT (inode != NULL);
    off_t length = inode_length(inode);
    if (pos > MAX_FILE_LENGTH_IN_BYTES) {
        return (block_sector_t)-2;
    }
    if (length < pos) {
        return (block_sector_t)-1;
    }
    
    struct cache_entry* disk_inode_cache_entry = get_cache_entry_for_sector(inode->sector, false); //false because we are reading here.
    unsigned index_for_pos = (unsigned)pos / (unsigned)BLOCK_SECTOR_SIZE;
    if (index_for_pos < NUM_DIRECT_BLOCKS) {
        return get_direct_block_sector_number(disk_inode_cache_entry, index_for_pos);
    }
    if (index_for_pos < (NUM_DIRECT_BLOCKS + (NUM_BLOCK_IDS_PER_BLOCK - 1))) {
        return get_indirect_block_sector_number(disk_inode_cache_entry, index_for_pos);
    }
    return get_double_indirect_block_sector_number(disk_inode_cache_entry, index_for_pos);
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
    list_init(&open_inodes);
    lock_init(&open_inodes_lock);
}

/*
 -----------------------------------------------------------
 DESCRIPTION: clears direct blocks
 -----------------------------------------------------------
 */
static void clear_direct_blocks(struct cache_entry* disk_inode_cache_entry, size_t* num_blocks_to_clear) {
    int direct_index;
    for (direct_index = 0; direct_index < NUM_DIRECT_BLOCKS; direct_index++) {
        block_sector_t curr_block_to_clear = 0;
        off_t offset_to_read_from = sizeof(off_t) + sizeof(unsigned) + (direct_index * sizeof(block_sector_t));
        read_from_cache(disk_inode_cache_entry, &curr_block_to_clear, offset_to_read_from, sizeof(block_sector_t));
        clear_cache_entry_if_present(curr_block_to_clear);
        free_map_release(curr_block_to_clear, 1);
        (*num_blocks_to_clear)--;
        if ((*num_blocks_to_clear) == 0) return;
    }
}

/*
 -----------------------------------------------------------
 DESCRIPTION: clears blocks that are held indirectly, 
    Also, clears the indirect block in the 
    disk_inode_cache_entry.
 -----------------------------------------------------------
 */
static void clear_indirect_blocks(struct cache_entry* disk_inode_cache_entry, size_t* num_blocks_to_clear, block_sector_t indirect_block_number) {
    if (indirect_block_number == 0) {
        off_t offset = sizeof(off_t) + sizeof(unsigned) + (NUM_DIRECT_BLOCKS * sizeof(block_sector_t));
        read_from_cache(disk_inode_cache_entry, &indirect_block_number, offset, sizeof(block_sector_t));
    }
    struct cache_entry* indirect_block_cache_entry = get_cache_entry_for_sector(indirect_block_number, true);
    int indirect_index;
    for (indirect_index = 0; indirect_index < NUM_BLOCK_IDS_PER_BLOCK; indirect_index++) {
        block_sector_t curr_block_to_clear = 0;
        off_t offset_to_read_from = indirect_index * sizeof(block_sector_t);
        read_from_cache(indirect_block_cache_entry, &curr_block_to_clear, offset_to_read_from, sizeof(block_sector_t));
        clear_cache_entry_if_present(curr_block_to_clear);
        free_map_release(curr_block_to_clear, 1);
        (*num_blocks_to_clear)--;
        if ((*num_blocks_to_clear) == 0) break;
    }
    clear_cache_entry_if_present(indirect_block_number);
    free_map_release(indirect_block_number, 1);
    release_cache_lock_for_write(&indirect_block_cache_entry->lock);
}

/*
 -----------------------------------------------------------
 DESCRIPTION: clears doubly indirect blocks.
 -----------------------------------------------------------
 */
static void clear_double_indirect_blocks(struct cache_entry* disk_inode_cache_entry, size_t* num_blocks_to_clear) {
    block_sector_t double_indirect_block_number = 0;
    off_t offset = sizeof(off_t) + sizeof(unsigned) + ((NUM_DIRECT_BLOCKS + NUM_INDIRECT_BLOCKS) * sizeof(block_sector_t));
    read_from_cache(disk_inode_cache_entry, &double_indirect_block_number, offset, sizeof(block_sector_t));
    struct cache_entry* double_indirect_block_cache_entry = get_cache_entry_for_sector(double_indirect_block_number, true);
    int double_indirect_index;
    for (double_indirect_index = 0; double_indirect_index < NUM_BLOCK_IDS_PER_BLOCK; double_indirect_index++) {
        block_sector_t curr_indirect_block_number = 0;
        off_t offset_to_read_from = double_indirect_index * sizeof(block_sector_t);
        read_from_cache(double_indirect_block_cache_entry, &curr_indirect_block_number, offset_to_read_from, sizeof(block_sector_t));
        clear_indirect_blocks(disk_inode_cache_entry, num_blocks_to_clear, curr_indirect_block_number);
        if ((*num_blocks_to_clear) == 0) break;
    }
    clear_cache_entry_if_present(double_indirect_block_number);
    free_map_release(double_indirect_block_number, 1);
    release_cache_lock_for_write(&double_indirect_block_cache_entry->lock);
}

/*
 -----------------------------------------------------------
 DESCRIPTION: Releases the blocks that the inode currently
    has. 
 NOTE: length tells us how many blocks the inode has
 NOTE: Assumes that the caller has already aquired the 
    cache_lock for the disk_inode_cache_entry in exclusive
    mode. Also, relies on caller to release this lock.
 NOTE: assumes that no other cache entries for other blocks
    have been aquired. Thus, in this function, when clearing
    indirect and double indirect blocks, we aquire the cache
    locks for those blocks.
 NOTE: also releases th block for the inode on disk, but does   
    not release the cache entry lock for that block
 -----------------------------------------------------------
 */
static void release_blocks(struct cache_entry* disk_inode_cache_entry, off_t length) {
    size_t num_blocks_to_clear = bytes_to_sectors(length);
    
    if (num_blocks_to_clear > 0) clear_direct_blocks(disk_inode_cache_entry, &num_blocks_to_clear);
    
    if (num_blocks_to_clear > 0) clear_indirect_blocks(disk_inode_cache_entry, &num_blocks_to_clear, 0);
    
    if (num_blocks_to_clear > 0) clear_double_indirect_blocks(disk_inode_cache_entry, &num_blocks_to_clear);
    
    ASSERT(num_blocks_to_clear == 0);
    
    block_sector_t inode_sector_number = disk_inode_cache_entry->sector_id;
    clear_cache_entry_if_present(inode_sector_number);
    free_map_release(inode_sector_number, 1);
    
}

/*
 -----------------------------------------------------------
 DESCRIPTION: code that sets up the direct blocks
    within the inode.
 NOTE: return true on success, false otherwise
 -----------------------------------------------------------
 */
static bool settup_direct_blocks(struct cache_entry* disk_inode_cache_entry, size_t* num_sectors_needed, size_t* num_sectors_allocated) {
    
    int direct_index;
    for (direct_index = 0; direct_index < NUM_DIRECT_BLOCKS; direct_index++) {
        block_sector_t curr_sector = 0;
        bool success = free_map_allocate(1, &curr_sector);
        if (success == false) {
            release_blocks(disk_inode_cache_entry, ((*num_sectors_allocated) * BLOCK_SECTOR_SIZE));
            release_cache_lock_for_write(&disk_inode_cache_entry->lock);
            return false;
        }
        off_t offset = sizeof(off_t) + sizeof(unsigned) + (direct_index * sizeof(block_sector_t));
        write_to_cache(disk_inode_cache_entry, &curr_sector, offset, sizeof(block_sector_t));
        (*num_sectors_allocated)++;
        (*num_sectors_needed)--;
        if ((*num_sectors_needed) == 0) break;
    }
    return true;
}

/*
 -----------------------------------------------------------
 DESCRIPTION: ode that sets up the indirect blocks for 
    an inode. 
 -----------------------------------------------------------
 */
static bool settup_indirect_blocks(struct cache_entry* disk_inode_cache_entry, size_t* num_sectors_needed, size_t* num_sectors_allocated) {
    
    block_sector_t indirect_block = 0;
    bool success = free_map_allocate(1, &indirect_block);
    if (success == false) {
        release_blocks(disk_inode_cache_entry, ((*num_sectors_allocated) * BLOCK_SECTOR_SIZE));
        release_cache_lock_for_write(&disk_inode_cache_entry->lock);
        return false;
    }
    struct cache_entry* indirect_block_cache_entry = get_cache_entry_for_sector(indirect_block, true);
    int indirect_index;
    for (indirect_index = 0; indirect_index < NUM_BLOCK_IDS_PER_BLOCK; indirect_index++) {
        block_sector_t curr_sector = 0;
        bool success = free_map_allocate(1, &curr_sector);
        if (success == false) {
            release_cache_lock_for_write(&indirect_block_cache_entry->lock);
            release_blocks(disk_inode_cache_entry, ((*num_sectors_allocated) * BLOCK_SECTOR_SIZE));
            release_cache_lock_for_write(&disk_inode_cache_entry->lock);
            return false;
        }
        off_t offset = (indirect_index * sizeof(block_sector_t));
        write_to_cache(indirect_block_cache_entry, &curr_sector, offset, sizeof(block_sector_t));
        (*num_sectors_allocated)++;
        (*num_sectors_needed)--;
        if ((*num_sectors_needed) == 0) break;
    }
    release_cache_lock_for_write(&indirect_block_cache_entry->lock);
    off_t offset_in_inode_cache_entry = sizeof(off_t) + sizeof(unsigned) + (NUM_DIRECT_BLOCKS * sizeof(block_sector_t));
    write_to_cache(disk_inode_cache_entry, &indirect_block, offset_in_inode_cache_entry, sizeof(block_sector_t));
    
    return true;
}

/*
 -----------------------------------------------------------
 DESCRIPTION: Code that sets up doubly indirect blocks
    in inode
 -----------------------------------------------------------
 */
static bool settup_double_indirect_blocks(struct cache_entry* disk_inode_cache_entry, size_t* num_sectors_needed, size_t* num_sectors_allocated) {
    
    
    block_sector_t double_indirect_block = 0;
    bool success = free_map_allocate(1, &double_indirect_block);
    if (success == false) {
        release_blocks(disk_inode_cache_entry, ((*num_sectors_allocated) * BLOCK_SECTOR_SIZE));
        release_cache_lock_for_write(&disk_inode_cache_entry->lock);
        return false;
    }
    struct cache_entry* double_indirect_block_cache_entry = get_cache_entry_for_sector(double_indirect_block, true);
    int double_indirect_index;
    for (double_indirect_index = 0; double_indirect_index < NUM_BLOCK_IDS_PER_BLOCK; double_indirect_index++) {
        block_sector_t curr_indirect_block = 0;
        bool success = free_map_allocate(1, &curr_indirect_block);
        if (success == false) {
            release_cache_lock_for_write(&double_indirect_block_cache_entry->lock);
            release_blocks(disk_inode_cache_entry, ((*num_sectors_allocated) * BLOCK_SECTOR_SIZE));
            release_cache_lock_for_write(&disk_inode_cache_entry->lock);
            return false;
        }
        struct cache_entry* curr_indirect_block_cache_entry = get_cache_entry_for_sector(curr_indirect_block, true);
        int indirect_index;
        for (indirect_index = 0; indirect_index < NUM_BLOCK_IDS_PER_BLOCK; indirect_index++) {
            block_sector_t curr_sector = 0;
            bool success = free_map_allocate(1, &curr_sector);
            if (success == false) {
                release_cache_lock_for_write(&curr_indirect_block_cache_entry->lock);
                release_cache_lock_for_write(&double_indirect_block_cache_entry->lock);
                release_blocks(disk_inode_cache_entry, ((*num_sectors_allocated) * BLOCK_SECTOR_SIZE));
                release_cache_lock_for_write(&disk_inode_cache_entry->lock);
                return false;
            }
            off_t offset = (indirect_index * sizeof(block_sector_t));
            write_to_cache(curr_indirect_block_cache_entry, &curr_sector, offset, sizeof(block_sector_t));
            (*num_sectors_allocated)++;
            (*num_sectors_needed)--;
            if ((*num_sectors_needed) == 0) break;
        }
        release_cache_lock_for_write(&curr_indirect_block_cache_entry->lock);
        off_t offset_in_double_indirect_block = double_indirect_index * sizeof(block_sector_t);
        write_to_cache(double_indirect_block_cache_entry, &curr_indirect_block, offset_in_double_indirect_block, sizeof(block_sector_t));
        if ((*num_sectors_needed) == 0) break;
    }
    release_cache_lock_for_write(&double_indirect_block_cache_entry->lock);
    off_t offset_in_inode = sizeof(off_t) + sizeof(unsigned) + ((NUM_DIRECT_BLOCKS + NUM_DOUBLE_INDIRECT_BLOCKS) * sizeof(block_sector_t));
    write_to_cache(disk_inode_cache_entry, &double_indirect_block, offset_in_inode, sizeof(block_sector_t));
    
    return true;
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
    bool success;
    
    ASSERT (length >= 0);
    
    /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
    ASSERT (sizeof(struct inode_disk) == BLOCK_SECTOR_SIZE);
    
    struct cache_entry* disk_inode_cache_entry = get_cache_entry_for_sector(sector, true);
    write_to_cache(disk_inode_cache_entry, &length, 0, sizeof(off_t));
    unsigned magic = INODE_MAGIC;
    write_to_cache(disk_inode_cache_entry, &magic, sizeof(off_t), sizeof(unsigned));
    size_t num_sectors_needed = bytes_to_sectors(length);
    size_t num_sectors_allocated = 0;
    if (num_sectors_needed > 0) {
        success = settup_direct_blocks(disk_inode_cache_entry, &num_sectors_needed, &num_sectors_allocated);
        if (success == false) return false;
    }
    if (num_sectors_needed > 0) {
        success = settup_indirect_blocks(disk_inode_cache_entry, &num_sectors_needed, &num_sectors_allocated);
        if (success == false) return false;
    }
    if (num_sectors_needed > 0) {
        success = settup_double_indirect_blocks(disk_inode_cache_entry, &num_sectors_needed, &num_sectors_allocated);
        if (success == false) return false;
    }
    release_cache_lock_for_write(&disk_inode_cache_entry->lock);
    
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
    inode->sector = sector;
    inode->open_cnt = 1;
    inode->deny_write_cnt = 0;
    inode->removed = false;
    lock_init(&inode->data_lock);
    lock_init(&inode->extend_inode_lock);
    list_push_front (&open_inodes, &inode->elem);
    lock_release(&open_inodes_lock);
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
    if (inode != NULL) {
        lock_acquire(&inode->data_lock);
        inode->open_cnt++;
        lock_release(&inode->data_lock);
    }
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
    lock_acquire(&inode->data_lock);
    inode->open_cnt--;
    if (inode->open_cnt == 0) {
        lock_acquire(&open_inodes_lock);
        list_remove (&inode->elem);
        lock_release(&open_inodes_lock);
        
        /* Deallocate blocks if removed. */
        if (inode->removed) {
            struct cache_entry* disk_inode_cache_entry = get_cache_entry_for_sector(inode->sector, true);
            off_t length = 0;
            read_from_cache(disk_inode_cache_entry, &length, 0, sizeof(off_t));
            release_blocks(disk_inode_cache_entry, length);
            release_cache_lock_for_write(&disk_inode_cache_entry->lock);
        }
        lock_release(&inode->data_lock);
        free (inode);
    } else {
        lock_release(&inode->data_lock);
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
    lock_acquire(&inode->data_lock);
    inode->removed = true;
    lock_release(&inode->data_lock);
}





/* 
 -----------------------------------------------------------
 Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. 
 NOTE: we only synchronize EOF in inode_write. Assume that 
    A is reading at EOF and B is writing at EOF of same file.
    If B is scehduled first, B will acquire the extend_inode
    lock, will then acquire blocks to extend inode exclusively,
    will then write necessary data to these blocks, and update 
    inode block id's array, and finally, after all of that is 
    finished, update the length of the inode accordingly, 
    and then release the extend_inode lock. When A resumes 
    execution after B, it will no longer be at EOF, and will
    read what B wrote, up to new EOF. Assume B gets swapped
    out mid execution of extension. Then A runs, will still
    see itself at EOF, as A has not updated inode_length yet
    and will thus return 0 bytes read. This is acceptable
    behavior as stated in program specification. 
 -----------------------------------------------------------
 */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset)
{
    uint8_t *buffer = buffer_;
    off_t bytes_read = 0;
    
    while (size > 0) {
        /* check for reading past EOF */
        if (offset > inode_length(inode)) {
            break;
        }
        
        /* Disk sector to read, starting byte offset within sector. */
        block_sector_t sector_idx = byte_to_sector (inode, offset);
        int sector_ofs = offset % BLOCK_SECTOR_SIZE;
        
        /* Bytes left in inode, bytes left in sector, lesser of the two. */
        off_t inode_left = inode_length (inode) - offset;
        int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
        int min_left = inode_left < sector_left ? inode_left : sector_left;
        
        /* Number of bytes to actually copy out of this sector. */
        int chunk_size = size < min_left ? size : min_left;
        if (chunk_size <= 0) {
            break;
        }
        
        struct cache_entry* entry = get_cache_entry_for_sector(sector_idx, false);
        if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE) {
            read_from_cache(entry, buffer+bytes_read, 0, BLOCK_SECTOR_SIZE);
            release_cache_lock_for_read(&entry->lock);
        } else {
            read_from_cache(entry, buffer+bytes_read, sector_ofs, chunk_size);
            release_cache_lock_for_read(&entry->lock);
        }
        
        /* Advance. */
        size -= chunk_size;
        offset += chunk_size;
        //here we make the read_ahead call. 
        bytes_read += chunk_size;
    }
    return bytes_read;
}

/* 
 -----------------------------------------------------------
 Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) 
 NOTE: Suppose both A and B are writing same file, and both
    are currently at EOF. In this case, both try to acquire 
    the extend_inode lock. The first will acquire, will 
    check length, see that it is still too small, so it will 
    acquire blocks for inode extension exclusively, and then write data
    into these blocks, and when finished updated the length of 
    inode, and then release the extend inode lock.
    Then, the second process trying to extend will 
    now acquire the extend lock, will re-check the inode
    length, and see that it is now extended, and thus will
    release the extend lock, and resume normal writes, as
    it is no longer wrting past EOF.
 PROCESS OF EXTENDING AN INODE:
    1. determine the number 0 bytes to write, starting
    at current EOF and extedning up to start of offset.
    2. Then, add the blocks to the inode that will
    be needed to extend the file.
    3. then write these blocks with the corresponding
    data.
    4. Then, update the inode length. 
    5. Then release the inode lock
    6. Then return the number of bytes written = SIZE
        as we do not count the zero bytes
 -----------------------------------------------------------
 */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;

  if (inode->deny_write_cnt)
    return 0;

  while (size > 0) {
      
      /* if offset is greater than current length of inode, we extend, must be atomic, so acquire inode_extend lock */
      if (offset > inode_length(inode)) {
          lock_acquire(&inode->extend_inode_lock);
          if (offset > inode_length(inode)) {
              bytes written += extend_inode(inode, buffer + bytes_written, size, offset); //note might not be size if we max out the disk space.
              lock_release(&inode->extend_inode_lock);
              return bytes_written;
          } else {
              lock_release(&inode->extend_inode_lock);
          }
      }
      
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;
      
      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;
      
      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0) {
          break;
      }
      
      struct cache_entry* entry = get_cache_entry_for_sector(sector_idx, true);
      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE) {
          write_to_cache(entry, (const void*)buffer+bytes_written, 0, BLOCK_SECTOR_SIZE);
          release_cache_lock_for_write(&entry->lock);
      } else {
          write_to_cache(entry, (const void*)buffer+bytes_written, sector_ofs, chunk_size);
          release_cache_lock_for_write(&entry->lock);
      }
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
  }
    
  return bytes_written;
}

/*
 -----------------------------------------------------------
 DESCRIPTION: Extends the inode writing size bytes from
    buffer to the inode starting at offset bytes into the 
    inode.
 NOTE: Expects that the caller has acquired the lock
    on extedning the inode, namely inode->extend_inode_lock;
 NOTE: bytes_to_sectors(length) - 1 is the index of block
    containing length byte.
 1. get the index 
 -----------------------------------------------------------
 */
static off_t extend_inode(struct inode* inode, void* buffer, off_t size, off_t offset) {
    //here we extend the inode exclusively
    //bytes remaining in current last block of inode to write to are BLOCK_SECTOR_SIZE*blocks previous to length - inode_length(inode).
    //bytes to zero = offset - inode_length(inode);
    off_t old_length = inode_length(inode);
    off_t bytes_to_zero = offset - old_length;
    off_t number_used_bytes_in_last_block;
    if (old_length % BLOCK_SECTOR_SIZE == 0) {
        number_used_bytes_in_last_block = BLOCK_SECTOR_SIZE;
    } else {
        number_used_bytes_in_last_block = old_length % BLOCK_SECTOR_SIZE;
    }
    
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
    lock_acquire(&inode->data_lock);
    inode->deny_write_cnt++;
    ASSERT (inode->deny_write_cnt <= inode->open_cnt);
    lock_release(&inode->data_lock);
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
    lock_acquire(&inode->data_lock);
    inode->deny_write_cnt--;
    lock_release(&inode->data_lock);
}



/* 
 -----------------------------------------------------------
 Returns the length, in bytes, of INODE's data. 
 -----------------------------------------------------------
 */
off_t
inode_length (const struct inode *inode)
{
    block_sector_t disk_inode_block_number = inode->sector;
    struct cache_entry* disk_inode_cache_entry = get_cache_entry_for_sector(disk_inode_block_number, false);
    off_t length = 0;
    read_from_cache(disk_inode_cache_entry, &length, 0, sizeof(off_t));
    release_cache_lock_for_read(&disk_inode_cache_entry->lock);
    return length;
}
