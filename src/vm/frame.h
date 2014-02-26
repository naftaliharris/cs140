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
    struct void* frame_base;
    struct lock frame_lock;
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
 NOTE: In this allocate frame function, we do the following:
    1. Create an spte
    2. select a frame for the page to reside in by calling palloc
    3. if palloc returns null, we evict a page from a frame. This will
    involve removing the mapping for that page
    4. Wire up the new address transaltion for the new page.
 NOTE: if there are no free pages to allocate, we have to evict one.
 NOTE: we call pallac to get a physica frame. If none exist, we
    evict a page. 
 NOTE: physcial memory frames are given back to the system when
    palloc_free is called. 
 --------------------------------------------------------------------
 */
void* allocate_frame(FLAGS, PARAMS)








//-----------------BELOW IS CONNOR'S CODE-----------------------//











struct frame {
  struct thread* owner_thread; // if NULL, no page mapped
  void* vaddr; // user virtual address, for use when referencing page table, and perhaps for when converting between kaddr/uaddr
  void* kaddr;
  struct lock lock;
};

bool frame_handler_init(size_t num_frames, uint8_t* frame_base);

typedef bool create_page_func (void* kaddr, void* aux);
bool frame_handler_create_user_page(void* virtaddr, bool writeable, bool zeroed, create_page_func* func, void* aux);

bool frame_handler_free_page(void* kaddr, void* uaddr, struct thread* owner);

#endif /* __VM_FRAME_H */