#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <list.h>
#include "threads/thread.h"

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
    struct list_elem   elem; /* For the per-process list */
    uint32_t paddr;          /* The frame address or swap slot */
    uint32_t vaddr;
    page_loc loc;            /* Whether the frame is swapped or on disk */
    page_type type;        
    struct file* file;       /* Allows us to locate the file on disk for a */
                             /* page that resides in a file */
};


bool map_page (struct thread*, void *, void *, bool);
void evict_page (struct thread*, void *);
struct spte *find_spte(struct thread*, void *);
void free_page (struct thread*, struct spte *);
void free_spt (struct list *);

#endif /* vm/page.h */
