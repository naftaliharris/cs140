#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include "devices/block.h"
#include "threads/synch.h"

#define CACHE_BLOCKS 64

struct cached_block
{
    struct lock lock;
    block_sector_t sector;
    bool occupied;
    bool dirty;
    bool accessed;
    void *data;
};

struct lock fs_cache_lock;
struct cached_block *fs_cache;
int fs_cache_arm;

/* Prototypes */
void init_fs_cache(void);
void write_back(struct cached_block *cb);
struct cached_block *get_free_block(void);
#endif
