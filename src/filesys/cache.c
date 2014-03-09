#include <string.h>
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
 *      => solved by daemon: TODO
 * 5) Asyncronous loading of caches
 *      => solved by daemon: TODO
 */

/* Initialize the data structures for the file system cache */
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

/* Writes the cached block back to disk if it's dirty.
 * Must be called from a write-lock context! */
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
            ASSERT (cb->state != IN_IO);
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
            writer_release(&cb->rw_lock);
        }
    }
}


/* Finds the cached_block, if present. Does no locking. */
struct cached_block *
find_cached_sector(block_sector_t sector)
{
    int i;
    for (i = 0; i < CACHE_BLOCKS; i++)
    {
        struct cached_block *cb = &fs_cache[i];
        /* UNOCCUPIED cached blocks have arbitrary sector numbers */
        if (cb->sector == sector && cb->state != UNOCCUPIED) {
            return cb;
        }
    }
    return NULL;
}

/* Reads from or writes to the given sector at bytes i such that from <= i < to,
 * reading them to or writing them from buffer. buffer must have size at least
 * from - to. 
 *
 * For internal use.
 */
static void
cached_read_write(block_sector_t sector, bool write,
                  uint32_t from, uint32_t to, void *buffer)
{
    ASSERT (from < to);
    ASSERT (to <= BLOCK_SECTOR_SIZE);

    struct cached_block *cb;
    while (true) {
        reader_acquire(&sector_lock);
        cb = find_cached_sector(sector);
        if (cb != NULL) {
            reader_release(&sector_lock);
            rw_acquire(&cb->rw_lock, write);
            if (cb->sector == sector && (cb->state == CLEAN || cb->state == DIRTY)) {
                /* Typical case: Found the sector in cache, and it didn't
                 * change before we got a chance to see it. */

                if (write) {
                    memcpy(cb->data + from, buffer, (to - from));
                    cb->state = DIRTY;
                }
                else {
                    memcpy(buffer, cb->data + from, (to - from));
                }

                cb->accessed = true;  /* XXX synchronized? */
                rw_release(&cb->rw_lock, write);
                return;
            }
            /* Atypical case: We found the sector in cache, but it changed
             * before we got a chance to see it: Try again. */
            rw_release(&cb->rw_lock, write);
        } else {
            cb = get_free_block();
            reader_release(&sector_lock);
            writer_acquire(&sector_lock);
            if (find_cached_sector(sector) == NULL) {
                /* Typical case: We didn't find the sector in cache, and it
                 * didn't pop up before we could add it */
                cb->sector = sector;
                cb->state = IN_IO;
                writer_release(&sector_lock);

                block_read(fs_device, cb->sector, cb->data);
                cb->state = CLEAN;

                if (write) {
                    memcpy(cb->data + from, buffer, (to - from));
                    cb->state = DIRTY;
                }
                else {
                    memcpy(buffer, cb->data + from, (to - from));
                }

                cb->accessed = true;
                writer_release(&cb->rw_lock);
                return;
            }
            /* Atypical case: We didn't find the sector in cache, but it
             * popped up before we could add it: Try again. */
            writer_release(&sector_lock);
            writer_release(&cb->rw_lock);
        }
    }
}

/* Reads from the file system at the given sector at bytes between from and to,
 * and stores them into buffer, which must have size at least to - from.
 *
 * First checks the file system cache to see if it can read from memory.
 * All syncronization is done internally.
 */
void
cached_write(block_sector_t sector, uint32_t from, uint32_t to, void *buffer)
{
    cached_read_write(sector, true, from, to, buffer);
}

/* Writes to the file system at the given sector at bytes between from and to,
 * using the data from buffer, which must have size at least to - from.
 *
 * First checks the file system cache to see if it can write into memory.
 * All syncronization is done internally.
 */
void
cached_read(block_sector_t sector, uint32_t from, uint32_t to, void *buffer)
{
    cached_read_write(sector, false, from, to, buffer);
}

/* Write all of the cached blocks back to disk. */
void
write_back_all(void)
{
    /* XXX: Hard-coded 64 cache blocks */
    uint64_t wrote_back = 0;
    int i;
    while (wrote_back != UINT64_MAX) {
        for (i = 0; i < 64; i++) {
            struct cached_block *cb = &fs_cache[i];
            if ((wrote_back & (1 << i)) == 0) {
                if (writer_try_acquire(&cb->rw_lock)) {
                    write_back(cb);
                    wrote_back |= (1 << i);
                    writer_release(&cb->rw_lock);
                }
            }
        }
    }
}

/* TODO: A daemon for writing all caches back to disk periodically, and a daemon
 * and shared data structures for asyncronously caching blocks */
