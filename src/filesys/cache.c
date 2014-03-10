//
//  cache.c
//  
//
//  Created by Luke Pappas on 3/9/14.
//
//

#include <stdio.h>
#include <string.h>
#include "filesys/cache.h"
#include "threads/palloc.h"
#include "filesys/filesys.h"
#include "devices/block.h"


/*
 -----------------------------------------------------------
 DESCRIPTTION: lock used when accessing the cache. Because
    the cache is global, when modifing the cache, we 
    have to lock it. The only time we use this lock is during
    eviction however, as each cache entry has its own lock, 
    and the cache itself is a static array.
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

/* ============================================================== */
//LP defined helper functions
void init_cache_lock(struct cache_lock* lock);
struct cache_entry* search_for_existing_entry(int sector_id, bool exclusive);
struct cache_entry* check_for_unused_entry(void);
struct cache_entry* evict(void);
void advance_clock_hand(void);
void clear_cache_entry(struct cache_entry* entry);

/*
 -----------------------------------------------------------
 DESCRIPTION: Note, only be called by a thread currently 
    holding the eviction_lock
 -----------------------------------------------------------
 */
void advance_clock_hand(void) {
    clock_hand++;
    if (clock_hand >= NUM_CACHE_ENTRIES) {
        clock_hand = 0;
    }
}

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
void init_cache(void) {
    int i;
    int j;
    void* page;
    for (i = 0; i < NUM_KERNEL_PAGES; i++) {
        page = palloc_get_page(PAL_ZERO);
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
    can_flush = true;
}

/*
 -----------------------------------------------------------
 DESCRIPTION: frees the kernel pages that the cache uses.
 NOTE: Must be called after cache_flush.
 NOTE: after calling this function, all of the byte
    fields within the cache_entry's will be invalid.
 -----------------------------------------------------------
 */
void cache_free(void) {
    int i;
    for (i = 0; i < NUM_CACHE_ENTRIES; i++) {
        int index = i * NUM_CACHE_ENTRIES_PER_PAGE;
        struct cache_entry* entry = &cache[index];
        void* page_to_free = entry->bytes;
        palloc_free_page(page_to_free);
    }
}


/*
 -----------------------------------------------------------
 DESCRIPTION: searches the list of cache entries for one
    that matches sector_id. Returns the cache entry, with  
    the lock aquired according to exclusive if found,
    returns NULL if non found.
 NOTE: we aquire the lock initially in the shared sense as 
    all we want to do is read the sector_id field. If 
    we find a match, and want the lock in exclusive mode
    we have to release shared and reaquire the lock in 
    write context. However, there is a chance that we get
    swapped out in between these two calls. If that happens
    if the sector_id still matches, we are fine, otherwise we 
    have to restart the process. 
 -----------------------------------------------------------
 */
struct cache_entry* search_for_existing_entry(int sector_id, bool exclusive) {
    int i;
    for (i = 0; i < NUM_CACHE_ENTRIES; i++) {
        acquire_cache_lock_for_read(&cache[i].lock);
        if (cache[i].sector_id == sector_id) {
            if (exclusive) {
                release_cache_lock_for_read(&cache[i].lock);
                acquire_cache_lock_for_write(&cache[i].lock);
                if (cache[i].sector_id == sector_id) {
                    return &cache[i];
                } else {
                    release_cache_lock_for_write(&cache[i].lock);
                    i = 0;
                }
            } else {
                return &cache[i];
            }
        }
        release_cache_lock_for_read(&cache[i].lock);
    }
    return NULL;
}

/*
 -----------------------------------------------------------
 DESCRIPTION: checks the cache for an unused entry.
 NOTE: initialy checks a cache entry by acquiring the lock
    in shared mode. If the entry is not used, then 
    aquires the releases lock from shared mode, and tries
    to reaquire in exclusive mode. If the cache_entry is 
    no longer unused after this, then moves on, else, 
    returns the cache entry that is currently unused with the 
    cache_lock acquired in the exclusive context. If no
    unused cache entries exist, returns null.
 NOTE: this is exactly the same code as the search for existing entry
    with sector_id of unused and exclusive = true passed in. Thus
    we wrap this function here.
 -----------------------------------------------------------
 */
struct cache_entry* check_for_unused_entry() {
    struct cache_entry* entry = search_for_existing_entry(UNUSED_ENTRY_INDICATOR, true);
    return entry;
}

/*
 -----------------------------------------------------------
 DESCRIPTION: clears the fields of a cache_entry. This is 
    called after a cache_entry has been evicted.
 NOTE: This must be called with the current process having
    allready acquired the cache_lock exclusively. 
 -----------------------------------------------------------
 */
void clear_cache_entry(struct cache_entry* entry) {
    entry->sector_id = UNUSED_ENTRY_INDICATOR;
    entry->accessed = false;
    entry->dirty = false;
    memset(entry->bytes, 0, BLOCK_SECTOR_SIZE);
}

/*
 -----------------------------------------------------------
 DESCRIPTION: finds an entry to evict by checking the 
    accessed bits.
 NOTE: this is the clock algorithm described in lecture
 -----------------------------------------------------------
 */
struct cache_entry* evict(void) {
    while (true) {
        lock_acquire(&eviction_lock);
        struct cache_entry* curr = &cache[clock_hand];
        acquire_cache_lock_for_write(&curr->lock);
        if (curr->accessed == false) {
            if (curr->dirty) {
                block_write(fs_device, (block_sector_t)curr->sector_id, curr->bytes);
                clear_cache_entry(curr);
            }
            advance_clock_hand();
            lock_release(&eviction_lock);
            return curr;
        }
        curr->accessed = false;
        release_cache_lock_for_write(&curr->lock);
        advance_clock_hand();
        lock_release(&eviction_lock);
    }
}

/*
 -----------------------------------------------------------
 DESCRIPTION: returns the cache_entry for a given
    sector_id with the data of that sector loaded
    into the cache entry.
 NOTE: in the case where we do not find an existing entry
    we will be responsible for loading data into the 
    the cache. To do this, we have to acquire the cache_lock
    in the exclusive context. Thus, both check_for_unused_entry
    and evict return a non_null cache entry with the cache_lock
    aquired in the exclusive context.
 NOTE: the use of the while true loop is to handle the case
    where we want the cache_entry locked in the shared context, 
    but when switching to shared lock, get swapped out. Thus, 
    we have to check for state consistency. 
 NOTE: one other alternative is to disable interrupts.
 -----------------------------------------------------------
 */
struct cache_entry* get_cache_entry_for_sector(int sector_id, bool exclusive) {
    while (true) {
        struct cache_entry* entry = search_for_existing_entry(sector_id, exclusive);
        if (entry != NULL) {
            return entry;
        }
        entry = check_for_unused_entry();
        if (entry == NULL) {
            entry = evict();
        }
        block_read(fs_device, sector_id, entry->bytes);
        entry->sector_id = sector_id;
        if (exclusive == false) {
            release_cache_lock_for_write(&entry->lock);
            acquire_cache_lock_for_read(&entry->lock);
            if (entry->sector_id == sector_id) {
                return entry;
            }
        } else {
            return entry;
        }
    }
}

/*
 -----------------------------------------------------------
 DESCRIPTION: None
 NOTE: the updating of the accessed field allows for a benign
    race, as multiple readers will set this field to true. 
    This is ok, because when checking for accessed in eviction, 
    we lock the cache_entry exclusively beforehand. 
 NOTE: does not release cache_entry lock. Requires caller to
 do so.
 -----------------------------------------------------------
 */
void read_from_cache(struct cache_entry* entry, void* buffer, off_t offset, unsigned num_bytes) {
    void* copy_from_address = entry->bytes + offset;
    memcpy(buffer, copy_from_address, (size_t)num_bytes);
    entry->accessed = true;
}

/*
 -----------------------------------------------------------
 DESCRIPTION: writes the buffer to the cache, writing
    num_bytes and offset in the cache.
 NOTE: Updates accessed and dirty bits to true.
 NOTE: Caller must have already acquired the cache_entry
    lock in the exclusive context, prior to calling
    this function.
 NOTE: does not release cache_entry lock. That is the
    responsibility of the caller.
 NOTE: Offset must be in bytes.
 -----------------------------------------------------------
 */
void write_to_cache(struct cache_entry* entry, const void* buffer, off_t offset, unsigned num_bytes) {
    void* copy_to_address = entry->bytes + offset;
    memcpy(copy_to_address, buffer, num_bytes);
    entry->accessed = true;
    entry->dirty = true;
}

/*
 -----------------------------------------------------------
 DESCRIPTION: Flushes the cache to disk. We do this
    periodically to account for power failures.
 NOTE: Launch a thread in to do this for us. Thus
    the thread will periodically call this function.
 NOTE: Only flush if the cache_entry is in use
 NOTE: Because we are not writing to the cache_entry
    we can aquire the cache_entry lock in the shared context
 -----------------------------------------------------------
 */
void flush_cache(void) {
    int i;
    for (i = 0; i < NUM_CACHE_ENTRIES; i++) {
        struct cache_entry* entry = &cache[i];
        acquire_cache_lock_for_read(&entry->lock);
        if (entry->sector_id != UNUSED_ENTRY_INDICATOR) {
            block_write(fs_device, (block_sector_t)entry->sector_id, entry->bytes);
            entry->dirty = false;
        }
        release_cache_lock_for_read(&entry->lock);
    }
}


/* ================SHARED LOCK CODE============================ */

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
    lock->i--;
    if (!(lock->i)) {
        cond_signal(&lock->cond, &lock->internal_lock);
    }
    lock_release(&lock->internal_lock);
}








