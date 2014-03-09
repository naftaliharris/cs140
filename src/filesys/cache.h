#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include "devices/block.h"
#include "threads/synch.h"

#define CACHE_BLOCKS 64

#define CACHE_OCCUPIED 0x1
#define CACHE_ACCESSED 0x2
#define CACHE_DIRTY 0x4
#define CACHE_WRITING 0x8
#define CACHE_PINNED 0x10

/* States a cached block may be in */
typedef enum cached_block_state
{
    UNOCCUPIED,
    IN_IO,
    CLEAN,
    DIRTY
} cb_state_t;

/* The cached block data structure */
struct cached_block
{
    struct rw_lock rw_lock; /* Protects the cached_block data and metadata */

    block_sector_t sector;  /* The referenced sector on disk. To modify this,
                             * you must have a write-lock for both this cached
                             * block and also the global sector_lock. */

    cb_state_t state;       /* The state of the cached block */
    bool accessed;          /* Whether the cached block is accessed */
    void *data;             /* The actual cached data, (512 bytes) */
};

/* The actual file-system block cache */
struct cached_block *fs_cache;

/* Lock for all sectors */
struct rw_lock sector_lock;

/* Globals for eviction */
struct lock fs_evict_lock;  /* Only one cached_block may be evicted at a time */
int fs_cache_arm;           /* The clock arm for the eviction procedure */


/* Prototypes */
void init_fs_cache(void);
void write_back(struct cached_block *cb);
struct cached_block *get_free_block(void);
struct cached_block *find_cached_sector(block_sector_t sector);
void cached_read(block_sector_t sector, uint32_t from, uint32_t to, void *buffer);
void cached_write(block_sector_t sector, uint32_t from, uint32_t to, void *buffer);
void write_back_all(void);
#endif
