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

/* Supplementary Page Table Entry. This is a per-process data structure.  */
struct spte
{
    struct list_elem   elem; /* For the per-process list */
    uint32_t paddr;           /* The frame address or swap slot */
    uint32_t vaddr;
    page_loc loc;            /* Whether the frame is swapped or on disk */
    /* Also need the file and offset, (if on disk ie executable) */
};


bool map_page (struct thread*, void *, void *, bool);
void evict_page (struct thread*, void *);
struct spte *find_spte(struct thread*, void *);
void free_page (struct thread*, struct spte *);
void free_spt (struct list *);

#endif /* vm/page.h */
