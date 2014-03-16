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
struct cache_entry* search_for_existing_entry(int sector_id);
struct cache_entry* check_for_unused_entry(void);
struct cache_entry* evict(void);
void advance_clock_hand(void);
void clear_cache_entry(struct cache_entry* entry);
int check_existing_mappings(int sector_id);
void clear_mapping(int sector_id);
void install_mapping(int sector_id, int index);

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
 DESCRIPTION: Allows us to mantain a mapping of 
    sector_ids to cache entry containing said 
    sector_id for efficient lookup in cache. 
 -----------------------------------------------------------
 */
struct mapping_package {
    int sector_id;
    int index_in_cache;
};

/*
 -----------------------------------------------------------
 DESCRIPTION: array of mappings
 -----------------------------------------------------------
 */
struct mapping_package mappings[NUM_CACHE_ENTRIES];

/*
 -----------------------------------------------------------
 DESCRIPTION: used to ensure atomic access of mappings. 
 -----------------------------------------------------------
 */
struct lock mappings_lock;

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
            cache[curr_index].index = curr_index;
            init_cache_lock(&cache[curr_index].lock);
        }
    }
    for (i = 0; i < NUM_CACHE_ENTRIES; i++) {
        mappings[i].sector_id = -1;
        mappings[i].index_in_cache = -1;
    }
    lock_init(&mappings_lock);
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
 NOTE: allows benign race by setting can flush without lock
    but errors on side of over safe, by seting it before
    freeing cache. 
 -----------------------------------------------------------
 */
void cache_free(void) {
    lock_acquire(&mappings_lock);
    lock_acquire(&eviction_lock);
    can_flush = false;
    int i;
    for (i = 0; i < NUM_CACHE_ENTRIES; i++) {
        struct cache_entry* entry = &cache[i];
        if (entry->sector_id != UNUSED_ENTRY_INDICATOR) {
            clear_mapping(entry->sector_id);
        }
        entry->sector_id = UNUSED_ENTRY_INDICATOR;
    }
    for (i = 0; i < NUM_KERNEL_PAGES; i++) {
        int index = i * NUM_CACHE_ENTRIES_PER_PAGE;
        struct cache_entry* entry = &cache[index];
        void* page_to_free = entry->bytes;
        palloc_free_page(page_to_free);
    }
    lock_release(&eviction_lock);
    lock_release(&mappings_lock);
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
struct cache_entry* search_for_existing_entry(int sector_id) {
    int i;
    for (i = 0; i < NUM_CACHE_ENTRIES; i++) {
        bool success = try_acquire_cache_lock_for_write(&cache[i].lock);
        if (success) {
            if (cache[i].sector_id == sector_id) {
                return &cache[i];
            } else {
                release_cache_lock_for_write(&cache[i].lock);
            }
        }
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
    struct cache_entry* entry = search_for_existing_entry(UNUSED_ENTRY_INDICATOR);
    return entry;
}

/*
 -----------------------------------------------------------
 DESCRIPTION: clears the mapping for sector_id
 NOTE: expects that the caller has acquired the mappings_lock
 -----------------------------------------------------------
 */
void clear_mapping(int sector_id) {
    int i;
    int num_matches = 0;
    for (i = 0; i < NUM_CACHE_ENTRIES; i++) {
        if (mappings[i].sector_id == sector_id) {
            num_matches++;
            mappings[i].sector_id = -1;
            mappings[i].index_in_cache = -1;
        }
    }
    ASSERT(num_matches == 1);
}

/*
 -----------------------------------------------------------
 DESCRIPTION: installs a mapping from sector_id
 to index of the cache where cache_entry contains
 data of sector_id
 NOTE: expects caller has acquired the mappings_lock
 -----------------------------------------------------------
 */
void install_mapping(int sector_id, int index) {
    int i;
    for (i = 0; i < NUM_CACHE_ENTRIES; i++) {
        if (mappings[i].sector_id == -1) {
            mappings[i].sector_id = sector_id;
            mappings[i].index_in_cache = index;
            return;
        }
    }
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
    clear_mapping(entry->sector_id);
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
        bool success = try_acquire_cache_lock_for_write(&curr->lock);
        if (success) {
            if (curr->accessed == false) {
                if (curr->dirty) {
                    block_write(fs_device, (block_sector_t)curr->sector_id, curr->bytes);
                    clear_cache_entry(curr); //calls clear_mapping
                } else {
                    clear_mapping(curr->sector_id);
                }
                advance_clock_hand();
                lock_release(&eviction_lock);
                return curr;
            }
            curr->accessed = false;
            release_cache_lock_for_write(&curr->lock);
            advance_clock_hand();
            lock_release(&eviction_lock);
        } else {
            advance_clock_hand();
            lock_release(&eviction_lock);
        }
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
        while (true) {
            lock_acquire(&mappings_lock);
            int index = check_existing_mappings(sector_id);
            if (index == -1) {
                break;
            }
            struct cache_entry* entry = &cache[index];
            lock_release(&mappings_lock);
            if (exclusive) {
                acquire_cache_lock_for_write(&entry->lock);
                if (entry->sector_id == sector_id) return entry;
                release_cache_lock_for_write(&entry->lock);
            } else {
                acquire_cache_lock_for_read(&entry->lock);
                if (entry->sector_id == sector_id) return entry;
                release_cache_lock_for_read(&entry->lock);
            }
        }
        //if we fall through here, we still have the mappings_lock
        struct cache_entry* entry = check_for_unused_entry();
        if (entry == NULL) {
            entry = evict();
        }
        install_mapping(sector_id, entry->index);
        entry->sector_id = sector_id;
        lock_release(&mappings_lock);
        block_read(fs_device, sector_id, entry->bytes);
        if (exclusive == true) {
            return entry;
        } else {
            release_cache_lock_for_write(&entry->lock);
            acquire_cache_lock_for_read(&entry->lock);
            if (entry->sector_id == sector_id) {
                return entry;
            } else {
                release_cache_lock_for_read(&entry->lock);
            }
        }
    }
}

/*
 -----------------------------------------------------------
 DESCRIPTION: checks the current mappings. If there is
 one for sector_id, returns the index.
 NOTE: expects caller to acquire mappings_lock
 -----------------------------------------------------------
 */
int check_existing_mappings(int sector_id) {
    int i;
    for (i = 0; i < NUM_CACHE_ENTRIES; i++) {
        if (mappings[i].sector_id == sector_id) {
            return mappings[i].index_in_cache;
        }
    }
    return -1;
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
    if (can_flush == false) {
        return;
    }
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

/*
 -----------------------------------------------------------
 DESCRIPTION: Clears the cache entry that pertains to the
 given sector_id if it is contained in cache.
 -----------------------------------------------------------
 */
void clear_cache_entry_if_present(int sector_id) {
    lock_acquire(&mappings_lock);
    int index = check_existing_mappings(sector_id);
    if (index != -1) {
        struct cache_entry* entry = &cache[index];
        clear_cache_entry(entry);
    }
    lock_release(&mappings_lock);
}


/* ================SHARED LOCK CODE============================ */
/* The design of these shared locks comes from pseudocode at:
 * http://en.wikipedia.org/wiki/Readers%E2%80%93writers_problem
 */

/*
 -----------------------------------------------------------
 DESCRIPTION: Initializes the cache lock fields
 -----------------------------------------------------------
 */
void init_cache_lock(struct cache_lock* rw_lock) {
    sema_init(&rw_lock->no_waiting, 1);
    sema_init(&rw_lock->no_accessing, 1);
    sema_init(&rw_lock->readers_lock, 1);
    rw_lock->readers = 0;
}

/*
 -----------------------------------------------------------
 DESCRIPTION: None
 -----------------------------------------------------------
 */
void acquire_cache_lock_for_write(struct cache_lock* rw_lock) {
    sema_down(&rw_lock->no_waiting);
    sema_down(&rw_lock->no_accessing);
    sema_up(&rw_lock->no_waiting);
}

/*
 -----------------------------------------------------------
 DESCRIPTION: None
 -----------------------------------------------------------
 */
void release_cache_lock_for_write(struct cache_lock* rw_lock) {
    sema_up(&rw_lock->no_accessing);
}

/*
 -----------------------------------------------------------
 DESCRIPTION: None
 -----------------------------------------------------------
 */
void acquire_cache_lock_for_read(struct cache_lock* rw_lock) {
    sema_down(&rw_lock->no_waiting);
    sema_down(&rw_lock->readers_lock);
    int readers = rw_lock->readers++;
    sema_up(&rw_lock->readers_lock);
    if (readers == 0) {
        sema_down(&rw_lock->no_accessing);
    }
    sema_up(&rw_lock->no_waiting);
}

/*
 -----------------------------------------------------------
 DESCRIPTION: None
 -----------------------------------------------------------
 */
void release_cache_lock_for_read(struct cache_lock* rw_lock) {
    sema_down(&rw_lock->readers_lock);
    int readers = --rw_lock->readers;
    sema_up(&rw_lock->readers_lock);
    if (readers == 0) {
        sema_up(&rw_lock->no_accessing);
    }
}

/*
 -----------------------------------------------------------
 DESCRIPTION: try lock for the shared cache entry locks. 
    Used during eviction so that processes do not
    wait on cache entry's that are in I/O. 
 NOTE: in this case, we are attempting to acquire the shared
    lock for write purposes.
 -----------------------------------------------------------
 */
bool try_acquire_cache_lock_for_write(struct cache_lock* rw_lock) {
    if (sema_try_down(&rw_lock->no_waiting)) {
        if (sema_try_down(&rw_lock->no_accessing)) {
            sema_up(&rw_lock->no_waiting);
            return true;
        }
        sema_up(&rw_lock->no_waiting);
    }
    return false;
}

/*
 -----------------------------------------------------------
 DESCRIPTION: try lock for the shared cache entry locks.
    Used during eviction so that processes do not
    wait on cache entry's that are in I/O.
 NOTE: in this case, we are attempting to acquire the shared
    lock for read purposes.
 -----------------------------------------------------------
 */
bool try_acquire_cache_lock_for_read(struct cache_lock* rw_lock) {
    if (sema_try_down(&rw_lock->no_waiting)) {
        if (sema_try_down(&rw_lock->readers_lock)) {
            int readers = rw_lock->readers++;
            sema_up(&rw_lock->readers_lock);
            if (readers == 0) {
                bool success = sema_try_down(&rw_lock->no_accessing);
                sema_up(&rw_lock->no_waiting);
                return success;
            }
            sema_up(&rw_lock->no_waiting);
            return true;
        }
        sema_up(&rw_lock->no_waiting);
    }
    return false;
}
