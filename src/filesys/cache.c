#include "filesys/cache.h"
#include "threads/malloc.h"
#include "filesys/filesys.h"

void
init_fs_cache(void)
{
    /* TODO: Figure out if can remove the lock acquire and release here:
     * Probably.
     */
    lock_init(&fs_cache_lock);
    lock_acquire(&fs_cache_lock);
    fs_cache = (struct cached_block*)malloc(CACHE_BLOCKS * sizeof(struct cached_block));
    if (fs_cache == NULL) {
        PANIC("Couldn't allocated file system cache!");
    }

    int i;
    for (i = 0; i < CACHE_BLOCKS; i++)
    {
        lock_init(&fs_cache[i].lock);
        lock_acquire(&fs_cache[i].lock);
        fs_cache[i].occupied = false;
        fs_cache[i].data = malloc(BLOCK_SECTOR_SIZE);
        if (fs_cache[i].data == NULL) {
            PANIC("Couldn't allocate file system cache!");
        }
        lock_release(&fs_cache[i].lock);
    }

    fs_cache_arm = 0;
    lock_release(&fs_cache_lock);
}

void
write_back(struct cached_block *cb)
{
    ASSERT(lock_held_by_current_thread(&cb->lock));
    if (cb->occupied && cb->dirty) {
       block_write(fs_device, cb->sector, cb->data);
        cb->dirty = false;
    }
}

/* Returns an unoccupied block and acquires its lock. */
struct cached_block *
get_free_block(void)
{
    lock_acquire(&fs_cache_lock);
    while(true)
    {
        struct cached_block *cb = &fs_cache[fs_cache_arm];
        fs_cache_arm = (fs_cache_arm + 1) % CACHE_BLOCKS;
        if (lock_try_acquire(&cb->lock))
        {
            if (cb->occupied == false) {
                lock_release(&fs_cache_lock);
                return cb;
            }
            if (cb->accessed == false) {
                lock_release(&fs_cache_lock);
                write_back(cb);
                cb->occupied = false;
                return cb;
            }
            cb->accessed = false;
        }
    }
}
