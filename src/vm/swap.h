/* Swap table datastructure */

#ifndef  VM_SWAP_H
#define  VM_SWAP_H

#include <stddef.h>
#include "devices/block.h"
#include "threads/synch.h"

struct block *swap_block; /* The swap device */

/* The global swap table has precisely one job: It keeps track of which swap
 * slots are open. It does so with a stack that contains all open swap slots.
 */

uint32_t *swap_table;  /* Array of open indices */
uint32_t swap_slots;   /* Size of the array */
int swap_top;     /* Current location in the array */
struct lock swap_lock; /* Lock for the swap table */



void init_swap_table (void);

#endif /* vm/swap.h */
