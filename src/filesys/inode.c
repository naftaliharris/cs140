#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "filesys/cache.h"
#include "threads/thread.h"

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
#define NUM_BLOCK_IDS_IN_INODE 125
#define NUM_DIRECT_BLOCKS 123
#define NUM_INDIRECT_BLOCKS 1
#define NUM_DOUBLE_INDIRECT_BLOCKS 1
#define MAX_FILE_LENGTH_IN_BYTES 8387976
#define NUM_BLOCK_IDS_PER_BLOCK 128
#define INODE_INDIRECT_BLOCK_INDEX 123
#define INDOE_DOUBLE_INDIRECT_BLOCK_INDEX 124

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
    off_t length;                                  /* File size in bytes. */
    bool is_directory;                             /* true if inode represents a directory, false for files */
    unsigned magic;                                /* Magic number. */
    block_sector_t blocks[NUM_BLOCK_IDS_IN_INODE]; /* block id's of file data */
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
static off_t extend_inode(struct inode* inode, const void* buffer, off_t size, off_t offset);
static void add_blocks_to_inode(struct inode* inode, off_t num_blocks_needed, off_t index_of_next_block_to_add);
static void add_direct_blocks(struct inode* inode, off_t* num_blocks_needed, off_t* index_of_next_block_to_add);
static void add_indirect_blocks(struct inode* inode, off_t* num_blocks_needed, off_t* index_of_next_block_to_add);
static void add_double_indirect_blocks(struct inode* inode, off_t* num_blocks_needed, off_t* index_of_next_block_to_add);
static off_t get_indirect_block_index_in_double_indirect_block(off_t index);
static void add_to_existing_indirect_block(struct cache_entry* double_indirect_block_cache_entry, off_t* num_blocks_needed, off_t* index_of_next_block_to_add);



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
    off_t offset = sizeof(off_t) + sizeof(bool) + sizeof(unsigned) + (index * sizeof(block_sector_t));
    read_from_cache(disk_inode_cache_entry, &block_number, offset, sizeof(block_sector_t));
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
    index = index - NUM_DIRECT_BLOCKS - (NUM_BLOCK_IDS_PER_BLOCK * NUM_INDIRECT_BLOCKS); //This gives us an index relative to double indirect.
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
 ----------------------------------------------------------------------------
 Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. 
 returns -2 if pos exceeds max_file_length_in_bytes
 NOTE: we need to be able to call this function without 
    having to check the inode for length, as this is 
    required by the extending functionality. 
    thus, we take two extra parameters, to allow for this 
    case. 
 NOTE: could have written a new function, but
    that would have been repeated code. 
 ----------------------------------------------------------------------------
 */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos, bool use_param_length, off_t length_p)
{
    ASSERT (inode != NULL);
    off_t length;
    if (use_param_length) {
        length = length_p;
    } else {
        length = inode_length(inode);
    }
    if (pos > MAX_FILE_LENGTH_IN_BYTES) {
        return (block_sector_t)-2;
    }
    if (length < pos) {
        return (block_sector_t)-1;
    }
    
    struct cache_entry* disk_inode_cache_entry = get_cache_entry_for_sector(inode->sector, false); //false because we are reading here.
    unsigned index_for_pos = (unsigned)pos / (unsigned)BLOCK_SECTOR_SIZE;
    if (index_for_pos < NUM_DIRECT_BLOCKS) {
        block_sector_t retval = get_direct_block_sector_number(disk_inode_cache_entry, index_for_pos);
        release_cache_lock_for_read(&disk_inode_cache_entry->lock);
        return retval;
    }
    if (index_for_pos < (NUM_DIRECT_BLOCKS + (NUM_BLOCK_IDS_PER_BLOCK - 1))) {
        block_sector_t retval = get_indirect_block_sector_number(disk_inode_cache_entry, index_for_pos);
        release_cache_lock_for_read(&disk_inode_cache_entry->lock);
        return retval;
    }
    block_sector_t retval = get_double_indirect_block_sector_number(disk_inode_cache_entry, index_for_pos);
    release_cache_lock_for_read(&disk_inode_cache_entry->lock);
    return retval;
}


/* 
 ----------------------------------------------------------------------------
 List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. 
 ----------------------------------------------------------------------------
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
 ----------------------------------------------------------------------------
 DESCRIPTION: clears direct blocks
 ----------------------------------------------------------------------------
 */
static void clear_direct_blocks(struct cache_entry* disk_inode_cache_entry, size_t* num_blocks_to_clear) {
    int direct_index;
    for (direct_index = 0; direct_index < NUM_DIRECT_BLOCKS; direct_index++) {
        block_sector_t curr_block_to_clear = 0;
        off_t offset_to_read_from = sizeof(off_t) + sizeof(bool) + sizeof(unsigned) + (direct_index * sizeof(block_sector_t));
        read_from_cache(disk_inode_cache_entry, &curr_block_to_clear, offset_to_read_from, sizeof(block_sector_t));
        clear_cache_entry_if_present(curr_block_to_clear);
        free_map_release(curr_block_to_clear, 1);
        (*num_blocks_to_clear)--;
        if ((*num_blocks_to_clear) == 0) return;
    }
}

/*
 ----------------------------------------------------------------------------
 DESCRIPTION: clears blocks that are held indirectly, 
    Also, clears the indirect block in the 
    disk_inode_cache_entry.
 ----------------------------------------------------------------------------
 */
static void clear_indirect_blocks(struct cache_entry* disk_inode_cache_entry, size_t* num_blocks_to_clear, block_sector_t indirect_block_number) {
    if (indirect_block_number == 0) {
        off_t offset = sizeof(off_t) + sizeof(bool) + sizeof(unsigned) + (NUM_DIRECT_BLOCKS * sizeof(block_sector_t));
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
 ----------------------------------------------------------------------------
 DESCRIPTION: clears doubly indirect blocks.
 ----------------------------------------------------------------------------
 */
static void clear_double_indirect_blocks(struct cache_entry* disk_inode_cache_entry, size_t* num_blocks_to_clear) {
    block_sector_t double_indirect_block_number = 0;
    off_t offset = sizeof(off_t) + sizeof(bool) + sizeof(unsigned) + ((NUM_DIRECT_BLOCKS + NUM_INDIRECT_BLOCKS) * sizeof(block_sector_t));
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
 ----------------------------------------------------------------------------
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
 ----------------------------------------------------------------------------
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
 ----------------------------------------------------------------------------
 DESCRIPTION: code that sets up the direct blocks
    within the inode.
 NOTE: return true on success, false otherwise
 ----------------------------------------------------------------------------
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
        off_t offset = sizeof(off_t) + sizeof(bool) + sizeof(unsigned) + (direct_index * sizeof(block_sector_t));
        write_to_cache(disk_inode_cache_entry, &curr_sector, offset, sizeof(block_sector_t));
        (*num_sectors_allocated)++;
        (*num_sectors_needed)--;
        if ((*num_sectors_needed) == 0) break;
    }
    return true;
}

/*
 ----------------------------------------------------------------------------
 DESCRIPTION: ode that sets up the indirect blocks for 
    an inode. 
 ----------------------------------------------------------------------------
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
    off_t offset_in_inode_cache_entry = sizeof(off_t) + sizeof(bool) + sizeof(unsigned) + (NUM_DIRECT_BLOCKS * sizeof(block_sector_t));
    write_to_cache(disk_inode_cache_entry, &indirect_block, offset_in_inode_cache_entry, sizeof(block_sector_t));
    
    return true;
}

/*
 ----------------------------------------------------------------------------
 DESCRIPTION: Code that sets up doubly indirect blocks
    in inode
 ----------------------------------------------------------------------------
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
    off_t offset_in_inode = sizeof(off_t) + sizeof(bool) + sizeof(unsigned) + ((NUM_DIRECT_BLOCKS + NUM_DOUBLE_INDIRECT_BLOCKS) * sizeof(block_sector_t));
    write_to_cache(disk_inode_cache_entry, &double_indirect_block, offset_in_inode, sizeof(block_sector_t));
    
    return true;
}


/*
 ----------------------------------------------------------------------------
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
 NOTE: Modified inode_create function to take is_directory. Now
    need to modify existing code to reflect this change
 ----------------------------------------------------------------------------
 */
bool
inode_create (block_sector_t sector, off_t length, bool is_directory)
{
    if (length > MAX_FILE_LENGTH_IN_BYTES) return false;
    bool success;
    
    ASSERT (length >= 0);
    
    /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
    ASSERT (sizeof(struct inode_disk) == BLOCK_SECTOR_SIZE);
    
    struct cache_entry* disk_inode_cache_entry = get_cache_entry_for_sector(sector, true);
    write_to_cache(disk_inode_cache_entry, &length, 0, sizeof(off_t));
    write_to_cache(disk_inode_cache_entry, &is_directory, sizeof(off_t), sizeof(bool));
    unsigned magic = INODE_MAGIC;
    write_to_cache(disk_inode_cache_entry, &magic, sizeof(off_t) + sizeof(bool), sizeof(unsigned));
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
 ----------------------------------------------------------------------------
 //ORIGINAL COMMENT BELOW
 Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. 
 //END ORIGINAL COMMENT
 
 NOTE: Now the code no longer reads the inode information
    into the inode. The reason for this is that on_disk
    inode information may change, it would be a pain to 
    have to update the inode data when this happens. Thus, 
    we store all inode disk data on disk, and keep a sector 
    number as access to disk when needed. 
 NOTE: We do not release the open_inodes_lock if we do not
    find an inode in the list, because we want inode creation
    to be atomic. Otherwise, there could be multiple inodes
    in the list for the same file. 
 ----------------------------------------------------------------------------
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
    lock_init(&inode->directory_lock);
    struct cache_entry* disk_inode_cache_entry = get_cache_entry_for_sector(sector, false);
    bool is_directory;
    read_from_cache(disk_inode_cache_entry, &is_directory, sizeof(off_t), sizeof(bool));
    release_cache_lock_for_read(&disk_inode_cache_entry->lock);
    inode->is_directory = is_directory;
    list_push_front (&open_inodes, &inode->elem);
    lock_release(&open_inodes_lock);
    return inode;
}

/* 
 ----------------------------------------------------------------------------
 Reopens and returns INODE. 
 ----------------------------------------------------------------------------
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
 ----------------------------------------------------------------------------
 Returns INODE's inode number. 
 ----------------------------------------------------------------------------
 */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* 
 ----------------------------------------------------------------------------
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
 ----------------------------------------------------------------------------
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
 ----------------------------------------------------------------------------
 Marks INODE to be deleted when it is closed by the last caller who
   has it open. 
 ----------------------------------------------------------------------------
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
 ----------------------------------------------------------------------------
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
 ----------------------------------------------------------------------------
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
        block_sector_t sector_idx = byte_to_sector (inode, offset, false, 0);
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
        //here we make the read_ahead call if necesary.
        block_sector_t read_ahead_block = byte_to_sector (inode, offset, false, 0);
        if (read_ahead_block != sector_idx && (int)read_ahead_block != -1) {
            struct read_ahead_package* request = malloc(sizeof(struct read_ahead_package));
            if (request != NULL) {
                request->sector_number = read_ahead_block;
                lock_acquire(&read_ahead_requests_list_lock);
                list_push_back(&read_ahead_requests_list, &request->elem);
                lock_release(&read_ahead_requests_list_lock);
            }
        }
        bytes_read += chunk_size;
    }
    return bytes_read;
}

/* 
 ----------------------------------------------------------------------------
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
 ----------------------------------------------------------------------------
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
      if (offset >= inode_length(inode)) {
          lock_acquire(&inode->extend_inode_lock);
          if (offset >= inode_length(inode)) {
              bytes_written += extend_inode(inode, buffer + bytes_written, size, offset); 
              lock_release(&inode->extend_inode_lock);
              return bytes_written;
          } else {
              lock_release(&inode->extend_inode_lock);
          }
      }
      
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset, false, 0);
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
      
      struct cache_entry* entry = get_cache_entry_for_sector(sector_idx, false);
      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE) {
          write_to_cache(entry, (const void*)buffer+bytes_written, 0, BLOCK_SECTOR_SIZE);
          release_cache_lock_for_read(&entry->lock);
      } else {
          write_to_cache(entry, (const void*)buffer+bytes_written, sector_ofs, chunk_size);
          release_cache_lock_for_read(&entry->lock);
      }
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
  }
    
  return bytes_written;
}

/*
 ----------------------------------------------------------------------------
 DESCRIPTION: Extends the inode writing size bytes from
    buffer to the inode starting at offset bytes into the 
    inode.
 NOTE: Expects that the caller has acquired the lock
    on extedning the inode, namely inode->extend_inode_lock;
 NOTE: because we zero 
 ----------------------------------------------------------------------------
 */
static off_t extend_inode(struct inode* inode, const void* buffer, off_t size, off_t offset) {
    off_t size_ = size;
    off_t old_length = inode_length(inode);
    off_t new_length = offset + size;
    off_t delta_length = new_length - old_length;
    off_t remaining_space_in_last_block;
    if (old_length % BLOCK_SECTOR_SIZE == 0) {
        remaining_space_in_last_block = 0;
    } else {
        remaining_space_in_last_block = BLOCK_SECTOR_SIZE - (old_length % BLOCK_SECTOR_SIZE);
    }
    int bytes_needed_to_allocate = delta_length - remaining_space_in_last_block;
    if (bytes_needed_to_allocate > 0) {
        off_t num_blocks_needed = bytes_to_sectors((off_t)bytes_needed_to_allocate);
        off_t index_of_next_block_to_add = bytes_to_sectors(old_length); //as we 0 index, we don't subtract 1
        add_blocks_to_inode(inode, num_blocks_needed, index_of_next_block_to_add);
    }
    
    /* now we write to the added blocks. First add the 0's*/
    off_t remaining_bytes_of_zeros = offset - old_length;
    int zero_array[BLOCK_SECTOR_SIZE/4];
    memset(&zero_array, 0, BLOCK_SECTOR_SIZE);
    off_t curr_length = old_length;
    while (true) {
        if (remaining_bytes_of_zeros == 0) break;
        block_sector_t curr_block = byte_to_sector(inode, curr_length, true, curr_length);
        off_t offset_of_write = curr_length % BLOCK_SECTOR_SIZE;
        off_t write_length = BLOCK_SECTOR_SIZE - offset_of_write;
        if (write_length > remaining_bytes_of_zeros) write_length = remaining_bytes_of_zeros;
        //write zeros to current block
        struct cache_entry* block_entry = get_cache_entry_for_sector(curr_block, true);
        write_to_cache(block_entry, &zero_array, offset_of_write, write_length);
        release_cache_lock_for_write(&block_entry->lock);
        remaining_bytes_of_zeros -= write_length;
        curr_length += write_length;
    }
    
    /* Now write the contents from buffer */
    off_t bytes_written = 0;
    while (true) {
        if (size == 0) break;
        block_sector_t curr_block = byte_to_sector(inode, curr_length, true, curr_length);
        off_t offset_of_write = curr_length % BLOCK_SECTOR_SIZE;
        off_t write_length = BLOCK_SECTOR_SIZE - offset_of_write;
        if (write_length > size) write_length = size;
        struct cache_entry* block_entry = get_cache_entry_for_sector(curr_block, true);
        write_to_cache(block_entry, (buffer + bytes_written), offset_of_write, write_length);
        release_cache_lock_for_write(&block_entry->lock);
        size -= write_length;
        curr_length += write_length;
        bytes_written += write_length;
    }
    
    /* now update length field in inode */
    struct cache_entry* disk_inode_cache_entry = get_cache_entry_for_sector(inode->sector, true);
    write_to_cache(disk_inode_cache_entry, &new_length, 0, sizeof(off_t));
    release_cache_lock_for_write(&disk_inode_cache_entry->lock);
    
    return size_; //note, when handling free-map fail, might not be size. OH said not to worry.
}

/*
 ----------------------------------------------------------------------------
 DESCRIPTION: 
 ----------------------------------------------------------------------------
 */
static void add_direct_blocks(struct inode* inode, off_t* num_blocks_needed, off_t* index_of_next_block_to_add) {
    struct cache_entry* disk_inode_cache_entry = get_cache_entry_for_sector(inode->sector, true);
    while (true) {
        if ((*index_of_next_block_to_add) >= NUM_DIRECT_BLOCKS) break;
        if ((*num_blocks_needed) == 0) break;
        
        block_sector_t curr_sector = 0;
        bool success = free_map_allocate(1, &curr_sector);
        if (success == false) {
            ASSERT(0 == 1);
        }
        off_t offset = sizeof(off_t) + sizeof(bool) + sizeof(unsigned) + ((*index_of_next_block_to_add) * sizeof(block_sector_t));
        write_to_cache(disk_inode_cache_entry, &curr_sector, offset, sizeof(block_sector_t));
        (*num_blocks_needed)--;
        (*index_of_next_block_to_add)++;
    }
    release_cache_lock_for_write(&disk_inode_cache_entry->lock);
}

/*
 ----------------------------------------------------------------------------
 DESCRIPTION: 
 ----------------------------------------------------------------------------
 */
static void add_indirect_blocks(struct inode* inode, off_t* num_blocks_needed, off_t* index_of_next_block_to_add) {
    block_sector_t indirect_block_number = 0;
    off_t offset_of_indirect_block_id = sizeof(off_t) + sizeof(bool) + sizeof(unsigned) + (NUM_DIRECT_BLOCKS * sizeof(block_sector_t));
    if ((*index_of_next_block_to_add) == NUM_DIRECT_BLOCKS) {
        //alocate a block for the indrect block in the inode and store block id in inode
        bool success = free_map_allocate(1, &indirect_block_number);
        if (success == false) {
            ASSERT(0 == 1);
        }
        struct cache_entry* disk_inode_cache_entry = get_cache_entry_for_sector(inode->sector, true);
        write_to_cache(disk_inode_cache_entry, &indirect_block_number, offset_of_indirect_block_id, sizeof(block_sector_t));
        release_cache_lock_for_write(&disk_inode_cache_entry->lock);
    } else {
        //otherwise, it has allready been allocated, so read it
        struct cache_entry* disk_inode_cache_entry = get_cache_entry_for_sector(inode->sector, false);
        read_from_cache(disk_inode_cache_entry, &indirect_block_number, offset_of_indirect_block_id, sizeof(block_sector_t));
        release_cache_lock_for_read(&disk_inode_cache_entry->lock);
    }
    struct cache_entry* indirect_block_cache_entry = get_cache_entry_for_sector(indirect_block_number, true);
    while (true) {
        if ((*index_of_next_block_to_add) >= NUM_DIRECT_BLOCKS + NUM_BLOCK_IDS_PER_BLOCK) break;
        if ((*num_blocks_needed) == 0) break;
        
        block_sector_t curr_sector = 0;
        bool success = free_map_allocate(1, &curr_sector);
        if (success == false) {
            ASSERT(0 == 1);
        }
        off_t offset_in_indirect_block = (((*index_of_next_block_to_add) - NUM_DIRECT_BLOCKS) * sizeof(block_sector_t));
        write_to_cache(indirect_block_cache_entry, &curr_sector, offset_in_indirect_block, sizeof(block_sector_t));
        (*num_blocks_needed)--;
        (*index_of_next_block_to_add)++;
    }
    release_cache_lock_for_write(&indirect_block_cache_entry->lock);
}

/*
 ----------------------------------------------------------------------------
 DESCRIPTION:
 ----------------------------------------------------------------------------
 */
static off_t get_indirect_block_index_in_double_indirect_block(off_t index) {
    index = index - NUM_DIRECT_BLOCKS - (NUM_BLOCK_IDS_PER_BLOCK * NUM_INDIRECT_BLOCKS);
    off_t indirect_index = 0;
    while (true) {
        if (index < NUM_BLOCK_IDS_PER_BLOCK) break;
        index = index - NUM_BLOCK_IDS_PER_BLOCK;
        indirect_index++;
    }
    return indirect_index;
}

/*
 ----------------------------------------------------------------------------
 DESCRIPTION: 
 ----------------------------------------------------------------------------
 */
static void add_to_existing_indirect_block(struct cache_entry* double_indirect_block_cache_entry, off_t* num_blocks_needed, off_t* index_of_next_block_to_add) {
    off_t indirect_index = get_indirect_block_index_in_double_indirect_block((*index_of_next_block_to_add));
    block_sector_t indirect_block_number = 0;
    off_t offset_to_indirect_block = indirect_index * sizeof(block_sector_t);
    read_from_cache(double_indirect_block_cache_entry, &indirect_block_number, offset_to_indirect_block, sizeof(block_sector_t));
    struct cache_entry* indirect_block_cache_entry = get_cache_entry_for_sector(indirect_block_number, true);
    off_t begin_index = (*index_of_next_block_to_add) - NUM_DIRECT_BLOCKS - (NUM_BLOCK_IDS_PER_BLOCK * NUM_INDIRECT_BLOCKS) - (indirect_index * NUM_BLOCK_IDS_PER_BLOCK);
    int i;
    for (i = begin_index; i < NUM_BLOCK_IDS_PER_BLOCK; i++) {
        if ((*num_blocks_needed) == 0) break;
        block_sector_t curr_sector = 0;
        bool success = free_map_allocate(1, &curr_sector);
        if (success == false) {
            ASSERT(0 == 1);
        }
        off_t offset_in_indirect_block = (i * sizeof(block_sector_t));
        write_to_cache(indirect_block_cache_entry, &curr_sector, offset_in_indirect_block, sizeof(block_sector_t));
        (*num_blocks_needed)--;
        (*index_of_next_block_to_add)++;
    }
    release_cache_lock_for_write(&indirect_block_cache_entry->lock);
}


/*
 ----------------------------------------------------------------------------
 DESCRIPTION: 
 ----------------------------------------------------------------------------
 */
static void add_double_indirect_blocks(struct inode* inode, off_t* num_blocks_needed, off_t* index_of_next_block_to_add) {
    
    block_sector_t double_indirect_block_number = 0;
    off_t offset_of_double_indirect_block_id = sizeof(off_t) + sizeof(bool) + sizeof(unsigned) + ((NUM_DIRECT_BLOCKS + NUM_INDIRECT_BLOCKS) * sizeof(block_sector_t));
    if ((*index_of_next_block_to_add) == NUM_DIRECT_BLOCKS + (NUM_BLOCK_IDS_PER_BLOCK*NUM_INDIRECT_BLOCKS)) {
        //allocate the doubly indirect block for the inode
        bool success = free_map_allocate(1, &double_indirect_block_number);
        if (success == false) {
            ASSERT(0 == 1);
        }
        struct cache_entry* disk_inode_cache_entry = get_cache_entry_for_sector(inode->sector, true);
        write_to_cache(disk_inode_cache_entry, &double_indirect_block_number, offset_of_double_indirect_block_id, sizeof(block_sector_t));
        release_cache_lock_for_write(&disk_inode_cache_entry->lock);
    } else {
        //read block id of doubly indirect block from inode
        struct cache_entry* disk_inode_cache_entry = get_cache_entry_for_sector(inode->sector, false);
        read_from_cache(disk_inode_cache_entry, &double_indirect_block_number, offset_of_double_indirect_block_id, sizeof(block_sector_t));
        release_cache_lock_for_read(&disk_inode_cache_entry->lock);
    }
    struct cache_entry* double_indirect_block_cache_entry = get_cache_entry_for_sector(double_indirect_block_number, true);
    if (((*index_of_next_block_to_add) - NUM_DIRECT_BLOCKS - (NUM_BLOCK_IDS_PER_BLOCK*NUM_INDIRECT_BLOCKS)) % NUM_BLOCK_IDS_PER_BLOCK != 0) {
        add_to_existing_indirect_block(double_indirect_block_cache_entry, num_blocks_needed, index_of_next_block_to_add);
    }
    if ((*num_blocks_needed) == 0) {
        release_cache_lock_for_write(&double_indirect_block_cache_entry->lock);
        return;
    }
    ASSERT((*index_of_next_block_to_add) - NUM_DIRECT_BLOCKS - (NUM_BLOCK_IDS_PER_BLOCK * NUM_INDIRECT_BLOCKS) % NUM_BLOCK_IDS_PER_BLOCK == 0);
    off_t indirect_start_index = get_indirect_block_index_in_double_indirect_block((*index_of_next_block_to_add));
    int double_indirect_index;
    for (double_indirect_index = indirect_start_index; double_indirect_index < NUM_BLOCK_IDS_PER_BLOCK; double_indirect_index++) {
        block_sector_t curr_indirect_block = 0;
        bool success = free_map_allocate(1, &curr_indirect_block);
        if (success == false) {
            ASSERT(0 == 1);
        }
        struct cache_entry* curr_indirect_block_cache_entry = get_cache_entry_for_sector(curr_indirect_block, true);
        int indirect_index;
        for (indirect_index = 0; indirect_index < NUM_BLOCK_IDS_PER_BLOCK; indirect_index++) {
            block_sector_t curr_sector = 0;
            bool success = free_map_allocate(1, &curr_sector);
            if (success == false) {
                ASSERT(0 == 1);
            }
            off_t offset = (indirect_index * sizeof(block_sector_t));
            write_to_cache(curr_indirect_block_cache_entry, &curr_sector, offset, sizeof(block_sector_t));
            (*index_of_next_block_to_add)++;
            (*num_blocks_needed)--;
            if ((*num_blocks_needed) == 0) break;
        }
        release_cache_lock_for_write(&curr_indirect_block_cache_entry->lock);
        off_t offset_in_double_indirect_block = double_indirect_index * sizeof(block_sector_t);
        write_to_cache(double_indirect_block_cache_entry, &curr_indirect_block, offset_in_double_indirect_block, sizeof(block_sector_t));
        if ((*num_blocks_needed) == 0) break;
    }
    release_cache_lock_for_write(&double_indirect_block_cache_entry->lock);
}



/*
 ----------------------------------------------------------------------------
 DESCRIPTION: 
 ----------------------------------------------------------------------------
 */
static void add_blocks_to_inode(struct inode* inode, off_t num_blocks_needed, off_t index_of_next_block_to_add) {
    if (index_of_next_block_to_add < (NUM_DIRECT_BLOCKS)) {
        add_direct_blocks(inode, &num_blocks_needed, &index_of_next_block_to_add);
        if (num_blocks_needed > 0) {
            add_indirect_blocks(inode, &num_blocks_needed, &index_of_next_block_to_add);
        }
        if (num_blocks_needed > 0) {
            add_double_indirect_blocks(inode, &num_blocks_needed, &index_of_next_block_to_add);
        }
    } else if (index_of_next_block_to_add < NUM_DIRECT_BLOCKS + (NUM_BLOCK_IDS_PER_BLOCK)) {
        add_indirect_blocks(inode, &num_blocks_needed, &index_of_next_block_to_add);
        if (num_blocks_needed > 0) {
            add_double_indirect_blocks(inode, &num_blocks_needed, &index_of_next_block_to_add);
        }
    } else {
        add_double_indirect_blocks(inode, &num_blocks_needed, &index_of_next_block_to_add);
    }
}



/*
 ---------------------------------------------------------------------------
 Disables writes to INODE.
   May be called at most once per inode opener. 
 ---------------------------------------------------------------------------
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
 ---------------------------------------------------------------------------
 Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. 
 ---------------------------------------------------------------------------
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
 ---------------------------------------------------------------------------
 Returns the length, in bytes, of INODE's data. 
 ---------------------------------------------------------------------------
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

