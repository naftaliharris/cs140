#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <list.h>

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
    uint32_t addr;           /* The frame address or swap slot */
    page_loc loc;            /* Whether the frame is swapped or on disk */
    /* Also need the file and offset, (if on disk ie executable) */
};


bool map_page (void *, void *, bool);
void evict_page (void *);
struct spte *find_spte(void *);
void free_page (struct spte *);
void free_spt (struct list *);

#endif /* vm/page.h */
