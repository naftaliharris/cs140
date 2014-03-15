#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"

  /* Directory Constants */
const char* PARENT_DIRECTORY_STRING = "..";
const char* SELF_DIRECTORY_STRING = ".";

/* A single directory entry. */
struct dir_entry 
  {
    block_sector_t inode_sector;        /* Sector number of header. */
    char name[NAME_MAX + 1];            /* Null terminated file name. */
    bool in_use;                        /* In use or free? */
  };

/* 
 ----------------------------------------------------------------------------
 Creates a directory with space for ENTRY_CNT entries in the
   given SECTOR.  Returns true if successful, false on failure. 
 NOTE: pass in the parent sector block so that we can wire up the 
    ".." to link to the parent directories inode block.
 NOTE: By convention, every directory must have access to its parent
    directory and to itself. 
 NOTE: In the event of root directory creation, the value of 
    parent_sector must equal sector. 
 NOTE: no need to acquire the directory lock here, as we are creating a 
    directory. Race conditions occur in created directories.
 ThreadSafe
 ----------------------------------------------------------------------------
 */
bool dir_create (block_sector_t sector, block_sector_t parent_sector, size_t entry_cnt) {
    bool creation_success = inode_create (sector, entry_cnt * sizeof (struct dir_entry), true);
    if (creation_success == true) {
        struct inode* directory_inode = inode_open(sector);
        if(directory_inode == NULL) {
          return false;
        }
        struct dir* newly_created_directory = dir_open(directory_inode);
        if(newly_created_directory == NULL) {
          return false;
        }
        lock_acquire(&newly_created_directory->inode->directory_lock);
        bool success1 = dir_add(newly_created_directory, SELF_DIRECTORY_STRING, sector);
        if (success1 == false) {
            lock_release(&newly_created_directory->inode->directory_lock);
            dir_close(newly_created_directory);
            return false;
        }
        bool success2 = dir_add(newly_created_directory, PARENT_DIRECTORY_STRING, parent_sector);
        if (success2 == false) {
            lock_release(&newly_created_directory->inode->directory_lock);
            dir_close(newly_created_directory);
            return false;
        }
        lock_release(&newly_created_directory->inode->directory_lock);
        dir_close(newly_created_directory);
        return true;
    }
  return false;
}

/* 
 ----------------------------------------------------------------------------
 Opens and returns the directory for the given INODE, of which
   it takes ownership.  Returns a null pointer on failure. 
 ----------------------------------------------------------------------------
 */
struct dir *
dir_open (struct inode *inode) 
{
  struct dir *dir = calloc (1, sizeof *dir);
  if (inode != NULL && dir != NULL)
    {
      dir->inode = inode;
      dir->pos = 0;
      return dir;
    }
  else
    {
      inode_close (inode);
      free (dir);
      return NULL; 
    }
}

/* 
 ----------------------------------------------------------------------------
 Opens the root directory and returns a directory for it.
   Return true if successful, false on failure. 
 ----------------------------------------------------------------------------
 */
struct dir *
dir_open_root (void)
{
  return dir_open (inode_open (ROOT_DIR_SECTOR));
}

/* 
 ----------------------------------------------------------------------------
 Opens and returns a new directory for the same inode as DIR.
   Returns a null pointer on failure. 
 ----------------------------------------------------------------------------
 */
struct dir *
dir_reopen (struct dir *dir) 
{
  return dir_open (inode_reopen (dir->inode));
}

/* 
 ----------------------------------------------------------------------------
 Destroys DIR and frees associated resources. 
 NOTE: Important that we close the inode here!
 ----------------------------------------------------------------------------
 */
void
dir_close (struct dir *dir) 
{
  if (dir != NULL)
    {
      inode_close (dir->inode);
      free (dir);
    }
}

/* 
 ----------------------------------------------------------------------------
 Returns the inode encapsulated by DIR. 
 ----------------------------------------------------------------------------
 */
struct inode *
dir_get_inode (struct dir *dir) 
{
  return dir->inode;
}

/* 
 ----------------------------------------------------------------------------
 Searches DIR for a file with the given NAME.
   If successful, returns true, sets *EP to the directory entry
   if EP is non-null, and sets *OFSP to the byte offset of the
   directory entry if OFSP is non-null.
   otherwise, returns false and ignores EP and OFSP. 
 NOTE: Per our design, this function will EXPECT!! that caller
    has acquired the directory lock in the dir->inode, and will
    release it when lookup returns. 
 ----------------------------------------------------------------------------
 */
static bool
lookup (const struct dir *dir, const char *name,
        struct dir_entry *ep, off_t *ofsp) 
{
  struct dir_entry e;
  size_t ofs;
  
  ASSERT (dir != NULL);
  ASSERT (name != NULL);
  ASSERT(lock_held_by_current_thread(&dir->inode->directory_lock));

  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (e.in_use && !strcmp (name, e.name)) 
      {
        if (ep != NULL)
          *ep = e;
        if (ofsp != NULL)
          *ofsp = ofs;
        return true;
      }
  return false;
}

/* 
 ----------------------------------------------------------------------------
 Searches DIR for a file with the given NAME
   and returns true if one exists, false otherwise.
   On success, sets *INODE to an inode for the file, otherwise to
   a null pointer.  The caller must close *INODE. 
 ----------------------------------------------------------------------------
 */
bool
dir_lookup (const struct dir *dir, const char *name,
            struct inode **inode)
{
    struct dir_entry e;
    
    ASSERT (dir != NULL);
    ASSERT (name != NULL);
    
    if (lookup (dir, name, &e, NULL)) {
        *inode = inode_open (e.inode_sector);
    } else {
        *inode = NULL;
    }
    
    return *inode != NULL;
}

/* 
 ----------------------------------------------------------------------------
 Adds a file named NAME to DIR, which must not already contain a
   file by that name.  The file's inode is in sector
   INODE_SECTOR.
   Returns true if successful, false on failure.
   Fails if NAME is invalid (i.e. too long) or a disk or memory
   error occurs.
 NOTE: Per the assignment
 dir->inode->directory_lock must be acquired
 ----------------------------------------------------------------------------
 */
bool
dir_add (struct dir *dir, const char *name, block_sector_t inode_sector)
{
    struct dir_entry e;
    off_t ofs;
    bool success = false;
    
    ASSERT (dir != NULL);
    ASSERT (name != NULL);
    ASSERT(lock_held_by_current_thread(&dir->inode->directory_lock));
    
    /* Check NAME for validity. */
    if (*name == '\0' || (strlen (name) > NAME_MAX)) {
        return false;
    }
    
    /* Check that NAME is not in use. */
    if (lookup (dir, name, NULL, NULL))
        goto done;
    
    /* Set OFS to offset of free slot.
     If there are no free slots, then it will be set to the
     current end-of-file.
     
     inode_read_at() will only return a short read at end of file.
     Otherwise, we'd need to verify that we didn't get a short
     read due to something intermittent such as low memory. */
    for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
         ofs += sizeof e)
        if (!e.in_use)
            break;
    
    /* Write slot. */
    e.in_use = true;
    strlcpy (e.name, name, sizeof e.name);
    e.inode_sector = inode_sector;
    success = inode_write_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
    
done:
    return success;
}


/* 
 ----------------------------------------------------------------------------
 Removes any entry for NAME in DIR.
   Returns true if successful, false on failure,
   which occurs only if there is no file with the given NAME.
 Must have dir->inode->directory_lock
 ----------------------------------------------------------------------------
 */
bool
dir_remove (struct dir *dir, const char *name) 
{
  struct dir_entry e;
  struct inode *inode = NULL;
  bool success = false;
  off_t ofs;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);
  ASSERT(lock_held_by_current_thread(&dir->inode->directory_lock));

  /* Find directory entry. */
  if (!lookup (dir, name, &e, &ofs))
    goto done;

  /* Open inode. */
  inode = inode_open (e.inode_sector);
  if (inode == NULL)
    goto done;
    
  if(inode->is_directory) {
    if(inode->open_cnt > 1)
      goto done;
    char temp[NAME_MAX + 1];
    struct dir* inner_dir = dir_open(inode);
    // check if directory is empty
    if(dir_readdir(inner_dir, temp)) {
      dir_close(inner_dir);
      inode = NULL;
      goto done;
    }
    inode = inode_open(e.inode_sector);
    dir_close(inner_dir);
  }

  /* Erase directory entry. */
  e.in_use = false;
  if (inode_write_at (dir->inode, &e, sizeof e, ofs) != sizeof e) 
    goto done;

  /* Remove inode. */
  inode_remove (inode);
  success = true;

 done:
  inode_close (inode);
  return success;
}

/* 
 ----------------------------------------------------------------------------
 Reads the next directory entry in DIR and stores the name in
   NAME.  Returns true if successful, false if the directory
   contains no more entries. 
 NOTE: Note, per the assignment spec, should never return "." and ".."
 ----------------------------------------------------------------------------
 */
bool
dir_readdir (struct dir *dir, char name[NAME_MAX + 1])
{
    struct dir_entry e;
    
    while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e)
    {
        dir->pos += sizeof e;
        if (e.in_use)
        {
            if (strcmp(e.name, SELF_DIRECTORY_STRING) == 0 || strcmp(e.name, PARENT_DIRECTORY_STRING) == 0) {
                continue;
            }
            strlcpy (name, e.name, NAME_MAX + 1);
            return true;
        }
    }
    return false;
}


/* 
 ----------------------------------------------------------------------------
 Resolves a path name and current working directory into the inode that it
 points to. The path must exist.
 Boolean parameter allows you to return the parent directory of the path,
 meaning that the full path might not exist
 Returns the final inode, or NULL if it fails.
 Through fileNameIndex returns the name of the individual file, w/o directories
 Value of fileNameIndex not guaranteed useable if function fails
 The caller must close the returned inode.
 It can fail if:
  * A file name exceeds NAME_MAX
  * A file is treated as a directory when it is not
  * Failure in dir_open (calloc returns NULL)
  * lookup fails - the file doesn't exist
 Returned inode has directory_lock acquired
 ----------------------------------------------------------------------------
 */
struct inode*
dir_resolve_path(const char* path, struct dir* cwd, char** fileName, bool parent) {
  char* offset = (char*)path;
  struct inode* lastInode;
  *fileName = offset;
  
  // decide if we use root or cwd
  if(path[0] == '/') {
    lastInode = inode_open(ROOT_DIR_SECTOR);
    offset++;
  } else if(path[0] == '\0') {
    return NULL;
  } else {
    lastInode = inode_reopen(cwd->inode);
  }
  
  lock_acquire(&lastInode->directory_lock);
  
  char curFileName[NAME_MAX + 1];
  
  while(true) {
    // skip all /'s
    while(*offset == '/')
      offset++;
    
    *fileName = offset;
    
    // Ensures that even if file name ends with /'s we have no problem
    if(*offset == '\0') {
      return lastInode;
    }
    
    // As we're looking for the next file name the last one must be a directory
    if(!lastInode->is_directory) {
      lock_release(&lastInode->directory_lock);
      inode_close(lastInode);
      return NULL;
    }
    
    // Copy file name into our own array
    int i = 0;
    while(*offset != '/' && *offset != '\0') {
      // If we reach this then we have exceeded our file name length
      if(i == NAME_MAX) {
        lock_release(&lastInode->directory_lock);
        inode_close(lastInode);
        return NULL;
      }
      ASSERT(i < NAME_MAX);
      curFileName[i] = *offset;
      i++;
      offset++;
    }
    // Make sure to null-terminate the array
    curFileName[i] = '\0';
    
    if(parent) {
      // skip all /'s
      while(*offset == '/')
        offset++;
      
      // Ensures that even if file name ends with /'s we have no problem
      if(*offset == '\0') {
        return lastInode;
      }
    }
    
    // dir_open will call inode_close for us on failure
    struct dir* lastInodeAsDir = dir_open(lastInode);
    if(lastInodeAsDir == NULL) {
      lock_release(&lastInode->directory_lock);
      return NULL;
    }
    
    // Find the next file name
    // Overwriting lastInode is fine, as dir_close will close the former value
    bool lookupResult = dir_lookup(lastInodeAsDir, curFileName, &lastInode);
      if (lastInode != lastInodeAsDir->inode) {
          lock_acquire(&lastInode->directory_lock);
          lock_release(&lastInodeAsDir->inode->directory_lock);
      }
    dir_close(lastInodeAsDir);
    if(!lookupResult) {
      lock_release(&lastInode->directory_lock);
      return NULL;
    }
  }
  NOT_REACHED();
}
