#ifndef VM_MMAP_H
#define VM_MMAP_H

#include <list.h>
#include "filesys/file.h"

/* Define the mapid_t type */
typedef int32_t mapid_t;

/* State keeping track of each mmapped file.
 * Note that multiple calls to mmap generate new mmap_states, even if the same
 * file was mmapped. */
struct mmap_state
{
    struct list_elem elem;  /* For the per-process list of mmapped files */
    struct file *fp;        /* The mmapped's owned reference to the file */
    void *vaddr;            /* Virtual address where the mmapped file begins */
    mapid_t mapping;        /* The per-process mapping id */
};

#endif
