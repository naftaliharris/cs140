#include "filesys/cache.h"

void
init_fs_cache(void)
{
    /* TODO: Figure out if can remove the lock aquire and release here:
     * Probably.
     */
    lock_init(&fs_cache_lock);
    lock_acquire(&fs_cache_lock);
    fs_cache = (struct cached_block*)malloc(64 * sizeof(struct cached_block));
    if (fs_cache == NULL) {
        PANIC("Couldn't allocated file system cache!");
    }

    int i;
    for (i = 0; i < 64; i++)
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
