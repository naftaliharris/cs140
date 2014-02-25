#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <list.h>
#include "threads/thread.h"


//TO DO
//1. SWITCH TO A HASHMAP

/* Possible locations of pages */
typedef enum
{
    LOC_MEMORY,
    LOC_SWAPPED,
    LOC_DISK
} page_loc;

/* The different types of pages */
typedef enum {
    STACK_PAGE;
    FILE_PAGE;
    MMAPED_PAGE;
} page_type;

/* 
 --------------------------------------------------------------------
 DESCRIPTION: tracks the additional info we need to keep tabs on 
    for a page.
 NOTE: this is a per-process data structure. 
 NOTE: paddr is a versatile field. It is used to track the physical 
    location of the page, wherever it may be. This field may be
    an offset into a file on disk, a frame in physical memory, or
    an index into the swap partition. 
 NOTE: vaddr is the identifier for this spte. It is our method of 
    associating pages with supplemental page entries. Our method of
    identification is to use the base page number, (ie the rounded
    down virtual address.)
 TO DO: Chnage to hashmap, not a list
 --------------------------------------------------------------------
 */
struct spte
{
    struct list_elem   elem; /* For the per-process list                   */
    uint32_t paddr;          /* The frame address or swap slot             */
    uint32_t vaddr;
    page_loc loc;            /* Whether the frame is swapped or on disk    */
    page_type type;        
    struct file* file_ptr;   /* Allows us to locate the file on disk for a */
                             /* page that resides in a file */
};

//--------------------LP ADDED FUNCTIONS------------------------//
/*
 --------------------------------------------------------------------
 DESCRIPTION: This function creates a new spte struct, populates
    the fields with the supplied data, and then returns the struct.
 NOTE: Need to add the spte to thread specific data structure. 
 --------------------------------------------------------------------
 */
struct spte* create_spte(uint32_t paddr, uint32_t vadd, page_loc loc, page_type type, struct file* file);

/*
 --------------------------------------------------------------------
 DESCRIPTION: this function frees the resources that an spte uses
    to track a page. 
 NOTE: Here we release all resources attained in the process of 
    managing a page with an spte. This involces the following:
    1. Remove the spte from the data structure it is a part of
    2. Freeing memory
    3. Releasing locks
 --------------------------------------------------------------------
 */
void free_spte(struct spte* spte);

/*
 --------------------------------------------------------------------
 DESCRIPTION: Loads a page into a physical memory frame.
 NOTE: This function gets called AFTER!! the create_spte has been 
    called. 
 NOTE: A page is loaded into memory when the user calls palloc
    or from the page fault handler. The process of loading a page
    of memory is different for the different types of pages.
    Thus, we use a switch statement based on page type to 
    load the page
 NOTE: Need to finish implimentation of the specific load page
    functions in page.c
 --------------------------------------------------------------------
 */
void load_page_into_physical_memory(struct spte* spte);

/*
 --------------------------------------------------------------------
 DESCRIPTION: Removes a page that is currently residing in a physcal
    frame, and moves it to another location. 
 NOTE: this function can only be called if the given page is currently
    in physical memory!
 NOTE: The process of moving a page is dependent on the page type.
    Thus, we use a switch statement, just as in load page. 
 --------------------------------------------------------------------
 */
void evict_page_from_physical_memory(struct spte* spte);

/*
 --------------------------------------------------------------------
 DESCRIPTION: looks up an spte entry for a given virtual address.
 NOTE: Our method of identifiying pages to spte's is by rounded 
    down virtual address. Thus, to get the spte entry for a given 
    virtual address, we pass the address to this function, we
    round the address down, and then we look up the spte entry in the
    per thread data structure based on the rounded down virtual 
    address.
 NOTE: The rounded down vitual address is the page number, given
    the settup of the virtual address. 
 --------------------------------------------------------------------
 */
struct spte* find_spte(void* virtual_address);


bool map_page (struct thread*, void *, void *, bool);
void evict_page (struct thread*, void *);
struct spte *find_spte(struct thread*, void *);
void free_page (struct thread*, struct spte *);
void free_spt (struct list *);

#endif /* vm/page.h */
