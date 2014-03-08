#include <bitmap.h>
#include "filesys/cache.h"
#include "threads/malloc.h"
#include "filesys/filesys.h"

/* Design goals:
 * 1) FS cache contains all unique cache blocks, (no repetitions)
 *      => Must have lock around at least all sectors.
 * 2) Multiple reads can happen simultaneously
 * 3) Cache writes occur eventually
 *      => 2 and 3 solved by rw_lock
 * 4) Cache periodically written back to disk
 *      => solved by daemon or in thread_tick
 * 5) Asyncronous loading of caches
 *      => solved by daemon
 * 6) Threads might suddenly die?
 *      => Not while they are running OS code!
 */

void
init_fs_cache(void)
{
    rw_lock_init(&sector_lock);
    lock_init(&fs_evict_lock);

    fs_cache = (struct cached_block*)malloc(CACHE_BLOCKS * sizeof(struct cached_block));
    if (fs_cache == NULL) {
        PANIC("Couldn't allocate file system cache!");
    }

    int i;
    for (i = 0; i < CACHE_BLOCKS; i++)
    {
        struct cached_block *cb = &fs_cache[i];
        rw_lock_init(&cb->rw_lock);
        cb->state = UNOCCUPIED;
        cb->accessed = false;
        cb->data = malloc(BLOCK_SECTOR_SIZE);
        if (cb->data == NULL) {
            PANIC("Couldn't allocate a file system cache buffer!");
        }
    }

    fs_cache_arm = 0;
}

/* XXX: Must be called from a write-lock context! */
void
write_back(struct cached_block *cb)
{
    /* The thread that is doing the IO should have the write-lock */
    ASSERT (cb->state != IN_IO);

    if (cb->state == DIRTY) {
        cb->state = IN_IO; 
        block_write(fs_device, cb->sector, cb->data);
        cb->state = CLEAN;
    }
}

/* Returns an unoccupied block and acquires its lock as a writer */
struct cached_block *
get_free_block(void)
{
    lock_acquire(&fs_evict_lock);
    while(true)
    {
        struct cached_block *cb = &fs_cache[fs_cache_arm];
        fs_cache_arm = (fs_cache_arm + 1) % CACHE_BLOCKS;
        if (writer_try_acquire(&cb->rw_lock)) {
            if (cb->state == UNOCCUPIED) {
                lock_release(&fs_evict_lock);
                return cb;
            }
            if (cb->accessed == false) {
                lock_release(&fs_evict_lock);
                write_back(cb);
                cb->state = UNOCCUPIED;
                return cb;
            }
            cb->accessed = false;
        }
    }
}


/* Finds the cached_block, if present.
 * If the cached_block is present...
 *
 *
 */

//struct cached_block *
//find_cached_sector(block_sector_t sector)
//{
//    reader_lock_acquire(&sector_lock);
//    int i;
//    for (i = 0; i < CACHE_BLOCKS; i++)
//    {
//        struct cached_block *cb = &fs_cache[i];
//        if (cb->sector == sector)
//            return cb;
//        }
//    }
//    return NULL;
//}

//void
//cached_read(block_sector_t sector, uint32_t from, uint32_t to, void *buffer)
//{
//    ASSERT (0 <= from);
//    ASSERT (from < to);
//    ASSERT (to < BLOCK_SECTOR_SIZE);
//
//    /* Outline:
//     * 1) Find the cb, getting a new one if necessary, and acquire read lock
//     * 2) Read
//     * 3) Release read lock
//     */
//
//    struct cached_block *cb = find_cached_sector(sector);
//
//    if (cb == NULL) {
//        cb = get_free_block();
//        cb->occupied = true;
//        cb->accessed = true;
//        cb->dirty = false;
//        cb->being_loaded = true;
//        lock_release(&cache_state_lock);
//
//        block_read(fs_device, sector, cb->data);
//        lock_acquire(&cache_state_lock);
//        cb->being_loaded = false;
//        lock_release(&cache_state_lock);
//
//        // Lock or something
//        // Read or something
//    } else {
//        cb->accessed = true;
//        lock_release(&cache_state_lock);
//
//        // Lock or something
//        // Read or something
//    }
//}

//void
//cached_write(block_sector_t sector, uint32_t from, uint32_t to, void *buffer)
//{
//    ASSERT (0 <= from);
//    ASSERT (from < to);
//    ASSERT (to < BLOCK_SECTOR_SIZE);
//
//}
//

void
write_back_all(void)
{
    /* XXX: Hard-coded 64 cache blocks */
    uint64_t wrote_back = 0;
    int i;
    while (wrote_back != UINT64_MAX) {
        for (i = 0; i < 64; i++) {
            struct cached_block *cb = &fs_cache[i];
            if (wrote_back & (1 << i) == 0) {
                if (writer_try_acquire(&cb->rw_lock)) {
                    write_back(cb);
                    wrote_back |= (1 << i);
                    writer_release(&cb->rw_lock);
                }
            }
        }
    }
}
