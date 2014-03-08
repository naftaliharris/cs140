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

typedef enum cached_block_state
{
    UNOCCUPIED,
    IN_IO,
    CLEAN,
    DIRTY
} cb_state_t;

struct cached_block
{
    struct rw_lock rw_lock;  /* Protects the state below */

    block_sector_t sector;    /* Protected by both global sector lock and the per-cached_block rw_lock */
    cb_state_t state;
    bool accessed;
    void *data;
};

/* The actual file-system cache */
struct cached_block *fs_cache;

/* Eviction data */
struct lock fs_evict_lock; /* Only one cached_block may be evicted at a time */
int fs_cache_arm;

/* Block sector data */
struct rw_lock sector_lock; /* Lock for all sectors */

/* Prototypes */
void init_fs_cache(void);
void write_back(struct cached_block *cb);
struct cached_block *get_free_block(void);
void write_back_all(void);
#endif
