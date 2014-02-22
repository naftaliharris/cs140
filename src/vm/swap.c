/* Swap table implementation */

#include <debug.h>
#include "devices/block.h"
#include "threads/vaddr.h"
#include "vm/swap.h"

void
init_swap_table (void)
{
    swap_block = block_get_role(BLOCK_SWAP);
    if (swap_block == NULL)
        PANIC("No swap partition found!");

    swap_slots = block_size(swap_block) / (PGSIZE / BLOCK_SECTOR_SIZE);
    swap_table = malloc(swap_slots * sizeof(uint32_t));
    if (swap_table == NULL)
        PANIC("Couldn't allocate swap table!");

    int slot;
    for (slot = 0; slot < swap_slots; slot++)
        swap_table[slot] = slot;

    swap_top = swap_slots - 1;
    lock_init(&swap_lock);
}
