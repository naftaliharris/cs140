/* Swap table implementation */

#include <debug.h>
#include <stdint.h>
#include "devices/block.h"
#include "threads/malloc.h"
#include "vm/swap.h"

void
init_swap_table (void)
{
    swap_block = block_get_role(BLOCK_SWAP);
    if (swap_block == NULL)
        PANIC("No swap partition found!");

    swap_slots = block_size(swap_block) / (SECTORS_PER_PAGE);
    swap_table = malloc(swap_slots * sizeof(uint32_t));
    if (swap_table == NULL)
        PANIC("Couldn't allocate swap table!");

    uint32_t slot;
    for (slot = 0; slot < swap_slots; slot++)
        swap_table[slot] = slot;

    swap_top = swap_slots - 1;
    lock_init(&swap_lock);
}

/* Writes the page located at kaddr to swap, returning the swap slot. */
uint32_t
write_to_swap (void *kaddr)
{
    /* Only lock the swap table to find an open swap slot, not while writing! */
    uint32_t slot;

    lock_acquire(&swap_lock);
    if (swap_top < 0)
        PANIC("Out of swap slots!");
    
    slot = swap_table[swap_top];
    swap_table[swap_top] = 0xBADC0DE5;  /* For debugging */
    swap_top--;
    lock_release(&swap_lock);

    /* Now begin writing to disk */
    int i;
    void *buffer;
    block_sector_t sector;
    for (i = 0; i < SECTORS_PER_PAGE; i++)
    {
        sector = slot * SECTORS_PER_PAGE + i;
        buffer = kaddr + i * BLOCK_SECTOR_SIZE;
        block_write (swap_block, sector, buffer);
    }

    return slot;
}

/* Reads the data located at the given swap slot into the page-sized kaddr,
 * and frees the slot in the swap table. */
void
read_from_swap (void *kaddr, uint32_t slot)
{
    /* Read the swap data into memory */
    int i;
    void *buffer;
    block_sector_t sector;
    for (i = 0; i < SECTORS_PER_PAGE; i++)
    {
        sector = slot * SECTORS_PER_PAGE + i;
        buffer = kaddr + i * BLOCK_SECTOR_SIZE;
        block_read (swap_block, sector, buffer);
    }

    /* Tell the swap table that this slot is now free */
    lock_acquire(&swap_lock);
    swap_top++;
    ASSERT(swap_top < swap_slots);
    swap_table[swap_top] = slot;
    lock_release(&swap_lock);
}
