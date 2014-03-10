//
//  cache.h
//  
//
//  Created by Luke Pappas on 3/9/14.
//
//

#ifndef _cache_h
#define _cache_h

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
    unsigned sector_id; /*used to identify the sector from disk that is in this
                            entry currently*/
    bool accessed;      /*true if the data has been accessed recently */
    bool dirty;         /*true if the data has been written to */
    void* bytes;        /*pointer to the bytes that this cache_entry manages */
    struct cache_lock lock; /*lock that allows multpiple reads, but only one 
                              write */
};

/*
 -----------------------------------------------------------
 DESCRITPION: performs all necessary initialization of the 
    cache system.
 -----------------------------------------------------------
 */
void init_cache();

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
struct cache_entry* get_cache_entry_for_sector(unsigned sector_id, bool exclusive);



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
