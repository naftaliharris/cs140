#ifndef PAGE_H
#define PAGE_H

/* Possible locations of pages */
typedef enum
{
    LOC_MEMORY,
    LOC_SWAPPED,
    LOC_DISK
} page_loc;

/* Supplementary Page Table. This is a per-process data structure.  */
struct spt
{
    uint32_t    *addr;      /* The frame address or disk address */
    page_loc    loc;        /* Whether the frame is swapped or on disk */
}

#endif
