#ifndef VM_MMAP_H
#define VM_MMAP_H

#include <list.h>
#include "filesys/file.h"

typedef int32_t mapid_t;

struct mmap_state
{
    struct list_elem elem;
    struct file *fp;
    void *vaddr;            /* The page the file is first mapped to */
    mapid_t mapping;
};

void munmap_state(struct mmap_state *mmap_s);

#endif
