//
//  cache.c
//  
//
//  Created by Luke Pappas on 3/9/14.
//
//

#include <stdio.h>
#include "cache.h"

/*
 -----------------------------------------------------------
 DESCRIPTTION: lock used during eviction of a cache entry.
 Eviction must be mutually exclusive, thus the use of
 the lock
 -----------------------------------------------------------
 */
static struct lock eviction_lock;

/*
 -----------------------------------------------------------
 DESCRIPTION: index number of the next cache entry to check
 during eviction. Note, when incrementing this field
 must acquire the eviction lock, as this must be a
 mututally exclusive operation.
 -----------------------------------------------------------
 */
unsigned clock_hand;


#define NUM_CACHE_ENTRIES 64
#define NUM_KERNEL_PAGES 8
#define NUM_CACHE_ENTRIES_PER_PAGE 8
#define UNUSED_ENTRY_INDICATOR -1

/*
 -----------------------------------------------------------
 DESCRIPTION: global array of cache entries. 
 -----------------------------------------------------------
 */
struct cache_entry cache[NUM_CACHE_ENTRIES];

//==============================================================\\
//LP defined helper functions
void init_cache_lock(struct cache_lock* lock);

/*
 -----------------------------------------------------------
 DESCRIPTION: Initializes the global cache.
 NOTE: We allocate pages in the kernel to use as our
    cache. The pages are of size 4096 bytes. This means
    that we can fit 4096 bytes / 512 bytes = 8 cache 
    entries per page. 
 NOTE: because we have 64 cache entries, that comes to 
    8 kernel pages.
 -----------------------------------------------------------
 */
void init_cache() {
    int i;
    int j;
    void* page;
    for (i = 0; i < NUM_KERNEL_PAGES; i++) {
        page = palloc_get_page(PAL_ZEROS);
        for (j = 0; j < NUM_CACHE_ENTRIES_PER_PAGE; j++) {
            int curr_index = (i*NUM_KERNEL_PAGES) + j;
            cache[curr_index].bytes = page + (j*BLOCK_SECTOR_SIZE);
            cache[curr_index].accessed = false;
            cache[curr_index].dirty = false;
            cache[curr_index].sector_id = UNUSED_ENTRY_INDICATOR;
            init_cache_lock(&cache[curr_index].lock);
        }
    }
    clock_hand = 0;
    lock_init(&eviction_lock);
}






//================SHARED LOCK CODE============================\\

/*
 -----------------------------------------------------------
 DESCRIPTION: Initializes the cache lock fields
 -----------------------------------------------------------
 */
void init_cache_lock(struct cache_lock* lock) {
    lock->i = 0;
    lock_init(&lock->internal_lock);
    cond_init(&lock->cond);
}

/*
 -----------------------------------------------------------
 DESCRIPTION: None
 -----------------------------------------------------------
 */
void acquire_cache_lock_for_write(struct cache_lock* lock) {
    lock_acquire(&lock->internal_lock);
    while (lock->i) {
        cond_wait(&lock->cond, &lock->internal_lock);
    }
    lock->i = -1;
    lock_release(&lock->internal_lock);
}

/*
 -----------------------------------------------------------
 DESCRIPTION: None
 -----------------------------------------------------------
 */
void release_cache_lock_for_write(struct cache_lock* lock) {
    lock_acquire(&lock->internal_lock);
    lock->i = 0;
    cond_broadcast(&lock->cond, &lock->internal_lock);
    lock_release(&lock->internal_lock);
}

/*
 -----------------------------------------------------------
 DESCRIPTION: None
 -----------------------------------------------------------
 */
void acquire_cache_lock_for_read(struct cache_lock* lock) {
    lock_acquire(&lock->internal_lock);
    while (lock->i < 0) {
        cond_wait(&lock->cond, &lock->internal_lock);
    }
    lock->i++;
    lock_release(&lock->internal_lock);
}

/*
 -----------------------------------------------------------
 DESCRIPTION: None
 -----------------------------------------------------------
 */
void release_cache_lock_for_read(struct cache_lock* lock) {
    lock_acquire(&lock->internal_lock);
    if (!--lock->i) {
        cond_signal(&lock->cond, &lock->internal_lock);
    }
    release_lock(&lock->internal_lock);
}








