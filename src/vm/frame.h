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
 DESCRIPTION: the frame table. An array of frame structs. Because the
    size of physical memory is variable, we cannot declare the array
    statically, has to be dynamically created. 
 NOTE: num_frames is the number of frames in the array.
 --------------------------------------------------------------------
 */
static struct list list_of_frames;
static lock frame_list_lock;



/*
 --------------------------------------------------------------------
 DESCRIPTION: initializes the frame table.
 --------------------------------------------------------------------
 */
void init_frame_table();

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
void* allocate_user_page(bool zeros, struct spte* spte);

/*
 DESCRIPTION: this function loads a page of memory into a frame. 
 NOTE: this is different than allocate, as the page allready exists.
    all we need to do is the following:
    1. call allocate frame to get a region of physical memory
    2. Then, copy the page from its current location to
    to the region of physical memory we just aquired. 
 */
void load_page_into_frame();

#endif /* __VM_FRAME_H */