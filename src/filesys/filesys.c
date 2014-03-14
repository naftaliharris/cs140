#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "filesys/cache.h"
#include "threads/thread.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format)
{
    fs_device = block_get_role (BLOCK_FILESYS);
    if (fs_device == NULL)
        PANIC ("No file system device found, can't initialize file system.");
    
    inode_init ();
    free_map_init ();
    
    if (format)
        do_format ();
    
    free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  free_map_close ();
    flush_cache();
    cache_free();
    
}

/* 
 -----------------------------------------------------------
 Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. 
 NOTE: we now take care to call free_map_release
    for the inode block in inode_create if failure arises.
 -----------------------------------------------------------
 */
bool
filesys_create (const char *name, struct dir* parent, bool isDir, off_t initial_size) 
{
  block_sector_t inode_sector = 0;
  bool success;
  if(isDir) {
    success = (parent != NULL
                  && free_map_allocate (1, &inode_sector)
                  && dir_create (inode_sector, parent->inode->sector, (size_t)initial_size)
                  && dir_add (parent, name, inode_sector));
  } else {
    success = (parent != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, false)
                  && dir_add (parent, name, inode_sector));
  }
    
  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  struct inode *inode = NULL;
  
  int fileNameOffset;
  struct inode* dirInode = dir_resolve_path(name, thread_current()->curr_dir, &fileNameOffset, true);
  struct dir* parentDir = NULL;
  // failed to open directory
  if(!(dirInode->is_directory && (parentDir = dir_open(dirInode)))) {
    dir_close(parentDir);
    return NULL;
  }

  dir_lookup (parentDir, name + fileNameOffset, &inode);
  dir_close(parentDir);

  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  bool success = false;

  int fileNameOffset;
  struct inode* dirInode = dir_resolve_path(name, thread_current()->curr_dir, &fileNameOffset, true);
  struct dir* parentDir = NULL;
  // failed to open directory
  if(!(dirInode->is_directory && (parentDir = dir_open(dirInode)))) {
    dir_close(parentDir);
    return NULL;
  }

  success = dir_remove (parentDir, name + fileNameOffset);
  dir_close(parentDir);

  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}
