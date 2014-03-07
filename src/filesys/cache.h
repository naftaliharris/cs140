#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include "devices/block.h"
#include "threads/synch.h"
#include "threads/malloc.h"

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

#endif
