#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/block.h"
#include "threads/synch.h"

/* 
 -----------------------------------------------------------
 In-memory inode. 
 NOTE: The dir struct in the directory.c code
    maintains a pointer to an inode struct. If multiple
    processess have the same directory open, they
    will have unique dir structs, but each will
    have a pointer to the same inode struct. Thus
    we have to put the directory lock in this struct!
 -----------------------------------------------------------
 */
struct inode
{
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    bool is_directory;                  /* True if is direcrtory, false otherwise. Set on call to inode_open, as it is read from disk_inode */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct lock data_lock;              /* lock protecting internal inode data */
    struct lock extend_inode_lock;      /* ensures writing past EOF and subsequent inode_extension is atomic */
    struct lock directory_lock;         /* ensures atomic directory access */
};

struct bitmap;

void inode_init (void);
bool inode_create (block_sector_t, off_t, bool is_directory);
struct inode *inode_open (block_sector_t);
struct inode *inode_reopen (struct inode *);
block_sector_t inode_get_inumber (const struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (const struct inode *);

#endif /* filesys/inode.h */
