//
//  cache.h
//  
//
//  Created by Luke Pappas on 3/9/14.
//
//

#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include "off_t.h"
#include "threads/synch.h"

//note for read ahead, just call get_cache_entry and then
//release the lock when it is returned. 

/*
 -----------------------------------------------------------
 DESCRIPTION: a lock that allows multiple readers, but
    only one writer. 
 NOTE: this implimentation taken from class notes, lecture
    on synchronization. 
 -----------------------------------------------------------
 */
struct cache_lock {
    int i; /* number of shared lockers, or -1 if exclusively locked */
    struct lock internal_lock; /* mutual exclusion in updating cache_lock
                                data */
    struct condition cond; /* used to notify waiters when lock releases */
};

/*
 -----------------------------------------------------------
 DESCRIPTION: This struct tracks the information that
    pertains to a given cache entry. 
 NOTE: Per the spec, there are 64 of these entries. Thus,   
    we track these entries in an array that is global.
 NOTE: the bytes pointer will point to a page of physical 
    memory from the kernel pool that is returned after a call
    to palloc. 
 -----------------------------------------------------------
 */
struct cache_entry {
    int sector_id; /*used to identify the sector from disk that is in this
                            entry currently*/
    bool accessed;      /*true if the data has been accessed recently */
    bool dirty;         /*true if the data has been written to */
    void* bytes;        /*pointer to the bytes that this cache_entry manages */
    struct cache_lock lock; /*lock that allows multpiple reads, but only one 
                              write */
};

/*
 -----------------------------------------------------------
 DESCRIPTION: global boolean indicates to the flush thread
    in thread.c if the thread can flush or not. Thread
    cannot begin flushing until the cache is initialized.
 NOTE: can_flush set to true in cache_init
 NOTE: we need this field because cache_init gets called
    from filesys_init, which is called after the thread
    init functions. Thus, it is possible that the flushing
    thread begins flushing before the cache is initialized.
    Thus, by using this boolean, we guard against that case.
 NOTE: although this gobal is read and written in two different
    places, we allow for the benign race, as the write of true
    happens after the initialization is complete. Thus, we allow
    the race, as there is no need to protect against it, and the
    overhead of synchronization would be useless. 
 -----------------------------------------------------------
 */
bool can_flush;

/*
 -----------------------------------------------------------
 DESCRITPION: performs all necessary initialization of the 
    cache system.
 -----------------------------------------------------------
 */
void init_cache(void);

/*
 -----------------------------------------------------------
 DESCRIPTION: frees the kernel pages that the cache uses. 
 NOTE: Must be called after cache_flush.
 -----------------------------------------------------------
 */
void cache_free(void);

/*
 -----------------------------------------------------------
 DESCRIPTION: looks up the cache_entry that corresponds to 
    the given sector number. 
 NOTE: returns the cache entry in the locked state, according
    to value of exclusive
 NOTE: if there is no entry in the cache for the given 
    sector_id, then we do one of two things:
    1. Check if there are any unsed cach entries. If so
    take one of these and use it to store the requested
    block data.
    2. If no unused cache entries, then we have to evict. 
    Find a cache entry to evict, and then return it. 
 NOTE: the exclusive field indicates whether or not the
    cache_entry should be returned exclusively locked for a 
    write, or shared lock for a read
 -----------------------------------------------------------
 */
struct cache_entry* get_cache_entry_for_sector(int sector_id, bool exclusive);

/*
 -----------------------------------------------------------
 DESCRIPTION: reads num_bytes starting from entry->bytes + offset
    into buffer. 
 NOTE: updates the accessed bit for the cache_entry.
 NOTE: this function must be called with the cache_entry 
    having been locked in shared context. This amounts
    to calling get_cache_entry_for_sector with a value of   
    false passed in. 
 NOTE: offset must be a byte offset.
 NOTE: does not release cache_entry lock. Requires caller to 
    do so. 
 -----------------------------------------------------------
 */
void read_from_cache(struct cache_entry* entry, void* buffer, off_t offset, unsigned num_bytes);

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
 -----------------------------------------------------------
 */
void write_to_cache(struct cache_entry* entry, const void* buffer, off_t offset, unsigned num_bytes);

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
void flush_cache(void);

/*
 -----------------------------------------------------------
 DESCRIPTION: Clears the cache entry that pertains to the 
    given sector_id if it is contained in cache.
 -----------------------------------------------------------
 */
void clear_cache_entry_if_present(int sector_id);



/*
 -----------------------------------------------------------
 DESCRIPTION: Aquires a cache_lock for writing purposes. In 
    this case, because we are writing, we need mutual 
    exclusion.
 NOTE: code taken from lecture slides.
 -----------------------------------------------------------
 */
void acquire_cache_lock_for_write(struct cache_lock* lock);

/*
 -----------------------------------------------------------
 DESCRIPTION: releases a cache_lock that had previously
    been exclusively acquired for a write. 
 NOTE: code taken from lecture slides.
 -----------------------------------------------------------
 */
void release_cache_lock_for_write(struct cache_lock* lock);

/*
 -----------------------------------------------------------
 DESCRIPTION: Aquires a cache_lock to read from the cache. 
    This is not a mutually exclusive acquire, thus we use 
    the shared lock acquire from lecture notes
 -----------------------------------------------------------
 */
void acquire_cache_lock_for_read(struct cache_lock* lock);

/*
 -----------------------------------------------------------
 DESCRIPTION: releases the cache_lock that had previously
    been acquired for a read. 
 NOTE: code taken from lecture slides
 -----------------------------------------------------------
 */
void release_cache_lock_for_read(struct cache_lock* lock);



#endif
