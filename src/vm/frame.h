#ifndef __VM_FRAME_H
#define __VM_FRAME_H

#include <stdbool.h>
#include <stdint.h>
#include "threads/thread.h"

//any time we palloc, we create an spte, any time we page fault, we check the spte's for the one to bring in.

/*
 --------------------------------------------------------------------
 DESCRIPTION: The struct containing information to manage
    a frame of physical memory
 NOTE: Resident_page is a pointer to the spte for the page
    that currently occupies a frame.
 NOTE: frame_base is the pointer that is returned by a call
    to palloc. It is the base address for a page of physical
    memory.
 NOTE: frame_lock locks the given frame for sycnchronization 
    purposes
 --------------------------------------------------------------------
 */
struct frame {
    struct spte* resident_page;
    struct void* physical_mem_frame_base;
    struct lock frame_lock;
    struct list_elem elem;
};


/*
 --------------------------------------------------------------------
 DESCRIPTION: initializes the frame table.
 --------------------------------------------------------------------
 */
void init_frame_table(size_t num_frames, uint8_t* frame_base);


/*
 --------------------------------------------------------------------
 DESCRIPTION: this function allocates a new page of memory to the 
    user. 
 NOTE: simply a wrapper around palloc. Calls palloc to get a 
    page of memory. If palloc returns null, we evict a page by 
    consulting our frame_table. 
 NOTE: the spte is created before the call to this function. 
    The spte contains all of the information about the page.
 NOTE: this function does not modify the data in the page. It 
    simply returns the base address of a physical page of 
    memory. 
 --------------------------------------------------------------------
 */
bool frame_handler_palloc(bool zeros, struct spte* spte);

/*
 --------------------------------------------------------------------
 DESCRIPTION: Wrapper around the call to palloc_free. In addition
    to calling palloc_free(physical_memory_address), also takes care of
    frame cleanup...
 --------------------------------------------------------------------
 */
bool frame_handler_palloc_free(void* physical_memory_address, struct spte* spte);


#endif /* __VM_FRAME_H */