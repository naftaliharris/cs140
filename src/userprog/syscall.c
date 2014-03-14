#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "threads/init.h"
#include "threads/vaddr.h"
#include "devices/shutdown.h"
#include "devices/input.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/directory.h"
#include "userprog/process.h"
#include "lib/string.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"


static void syscall_handler (struct intr_frame *);

// BEGIN LP DEFINED HELPER FUNCTIONS//
static void check_usr_ptr(const void* u_ptr);
static void check_usr_string(const char* str);
static void check_usr_buffer(const void* buffer, unsigned length);
static uint32_t read_frame(struct intr_frame* f, int byteOffset);
static int add_to_open_file_list(struct file* fp);
static struct file_package* get_file_package_from_open_list(int fd);
// END LP DEFINED HELPER FUNCTIONS  //

// BEGIN LP DEFINED SYSTEM CALL HANDLERS //
static void LP_halt (void) NO_RETURN;
static void LP_exit (int status) NO_RETURN;
static pid_t LP_exec (const char* command_line);
static int LP_wait (pid_t pid);
static bool LP_create (const char *file, unsigned initial_size);
static bool LP_remove (const char *file);
static int LP_open (const char *file);
static int LP_filesize (int fd);
static int LP_read (int fd, void *buffer, unsigned length);
static int LP_write (int fd, const void *buffer, unsigned length);
static void LP_seek (int fd, unsigned position);
static unsigned LP_tell (int fd);
static void LP_close (int fd);
// END   LP DEFINED SYSTEM CALL HANDLERS //


static bool chdir(const char* dir);
static bool mkdir(const char* dir);
static bool readdir(int fd, char* name);
static bool isdir(int fd);
static int inumber(int fd);

/*
 --------------------------------------------------------------------
 Description: we do any file system initialization here. 
 NOTE: currently, the only initialization that we add is 
    to init the global file_system lock. 
 --------------------------------------------------------------------
 */
void
syscall_init (void)
{
    intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/*
 --------------------------------------------------------------------
 Description: reads the system call number from f->eip and dispatches
    to the correct system call handler.
 NOTE: for all handlers that return a value, we place this return 
    value in the eax register of the intr_frame. 
 NOTE: because the eax register is defined to be a uiint32_t, we
    have to cast as such when placing the return value in it. 
 --------------------------------------------------------------------
 */
static void
syscall_handler (struct intr_frame *f ) 
{
    unsigned arg1, arg2, arg3;
    int systemCall_num = (int)read_frame(f, 0);
    switch (systemCall_num) {
        case SYS_HALT:
            LP_halt();
            break;
        case SYS_EXIT:
            arg1 = read_frame(f, 4);
            LP_exit((int)arg1);
            break;
        case SYS_EXEC:
            arg1 = read_frame(f, 4);
            f->eax = (uint32_t)LP_exec((char*)arg1);
            break;
        case SYS_WAIT:
            arg1 = read_frame(f, 4);
            f->eax = (uint32_t)LP_wait((pid_t)arg1);
            break;
        case SYS_CREATE:
            arg1 = read_frame(f, 4);
            arg2 = read_frame(f, 8);
            f->eax = (uint32_t)LP_create((const char*)arg1, (unsigned)arg2);
            break;
        case SYS_REMOVE:
            arg1 = read_frame(f, 4);
            f->eax = (uint32_t)LP_remove((const char*)arg1);
            break;
        case SYS_OPEN:
            arg1 = read_frame(f, 4);
            f->eax = (uint32_t)LP_open((const char*)arg1);
            break;
        case SYS_FILESIZE:
            arg1 = read_frame(f, 4);
            f->eax = (uint32_t)LP_filesize((int)arg1);
            break;
        case SYS_READ:
            arg1 = read_frame(f, 4);
            arg2 = read_frame(f, 8);
            arg3 = read_frame(f, 12);
            f->eax = (int)LP_read((int)arg1, (void*)arg2, (unsigned)arg3);
            break;
        case SYS_WRITE:
            arg1 = read_frame(f, 4);
            arg2 = read_frame(f, 8);
            arg3 = read_frame(f, 12);
            f->eax = (uint32_t)LP_write((int)arg1, (const void*)arg2, (unsigned)arg3);
            break;
        case SYS_SEEK:
            arg1 = read_frame(f, 4);
            arg2 = read_frame(f, 8);
            LP_seek((int)arg1, (unsigned)arg2);
            break;
        case SYS_TELL:
            arg1 = read_frame(f, 4);
            f->eax = (uint32_t)LP_tell((int)arg1);
            break;
        case SYS_CLOSE:
            arg1 = read_frame(f, 4);
            LP_close((int)arg1);
            break;
        case SYS_CHDIR:
          arg1 = read_frame(f, 4);
          f->eax = (uint32_t)chdir((const char*)arg1);
          break;
        case SYS_MKDIR:
          arg1 = read_frame(f, 4);
          f->eax = (uint32_t)mkdir((const char*)arg1);
          break;
        case SYS_READDIR:
          arg1 = read_frame(f, 4);
          arg2 = read_frame(f, 8);
          f->eax = (uint32_t)readdir((int)arg1, (char*)arg2);
          break;
        case SYS_ISDIR:
          arg1 = read_frame(f, 4);
          f->eax = (uint32_t)isdir((int)arg1);
          break;
        case SYS_INUMBER:
          arg1 = read_frame(f, 4);
          f->eax = (uint32_t)inumber((int)arg1);
          break;
        default:
            LP_exit(-1); //should never get here. If we do, exit with -1.
            break;
    }
}

/*
 --------------------------------------------------------------------
 Description: Terminates Pintos by calling power_off() 
    (declared in threads/init.h). This should be seldom 
    used, because you lose some information about possible 
    deadlock situations, etc.
 NOTE: the NO_RETURN included for style, and to ensure no 
    compiler warnings. 
 --------------------------------------------------------------------
 */
static void LP_halt (void) {
    shutdown_power_off ();
    NOT_REACHED();
}

/*
 --------------------------------------------------------------------
 Description: Terminates the current user program, returning 
    status to the kernel. If the process's parent waits for 
    it (see below), this is the status that will be returned. 
    Conventionally, a status of 0 indicates success and nonzero 
    values indicate errors.
 NOTE: exit is the first in a series of calls that is a process
    exit. Chain is as follows:
    1. exit -- here we set the exit status
    2. thread_exit() -- takes care of internal thread information
    3. process_exit() -- here is where we free our resources. 
 --------------------------------------------------------------------
 */
static void LP_exit (int status) {
    thread_current()->vital_info->exit_status = status;
    if (thread_current()->is_running_user_program) {
        printf("%s: exit(%d)\n", thread_name(), status);
    }
    thread_exit();
    NOT_REACHED();
}

/*
 --------------------------------------------------------------------
 Description: Runs the executable whose name is given in cmd_line, 
    passing any given arguments, and returns the new process's
    program id (pid). Must return pid -1, which otherwise should 
    not be a valid pid, if the program cannot load or run for any 
    reason. Thus, the parent process cannot return from the exec 
    until it knows whether the child process successfully loaded 
    its executable. You must use appropriate synchronization to 
    ensure this.
 NOTE: if pid is TID_ERROR after the call to process_execute, then
    the call to thread_create in process_execute failed, and the 
    child will never get a chance to load. Thus we return immediately
    in this case. 
 NOTE: if pid is not TID_ERROR, than the child thread will run the 
    start_process function. In this case, we have to wait until the 
    child process has signaled us that it has finished the load process. 
    Depending on the outcome of the load process, we return pid for 
    a successful load, or -1 if the load failed. 
 --------------------------------------------------------------------
 */
static pid_t LP_exec (const char* command_line) {
    check_usr_string(command_line);
    struct thread* curr_thread = thread_current();
    pid_t pid = process_execute(command_line);
    if (pid == TID_ERROR) {
        return -1;
    }
    sema_down(&curr_thread->sema_child_load);
    if (curr_thread->child_did_load_successfully) {
        curr_thread->child_did_load_successfully = false;
        return pid;
    }
    return -1;
}

/*
 --------------------------------------------------------------------
 Description: Waits for a child process pid and retrieves the 
    child's exit status.
 --------------------------------------------------------------------
 */
static int LP_wait (pid_t pid) {
    return process_wait(pid);
}

/*
 --------------------------------------------------------------------
 Description: Creates a new file called file initially initial_size 
    bytes in size. Returns true if successful, false otherwise. 
    Creating a new file does not open it: opening the new file 
    is a separate operation which would require a open system call.
 NOTE: we must acquire the file_system_lock to ensure that we 
    are the only process accessing the file_system. 
 --------------------------------------------------------------------
 */
static bool LP_create (const char *file, unsigned initial_size) {
    check_usr_string(file);
    lock_acquire(&file_system_lock);
    int fileNameOffset;
    struct inode* dirInode = dir_resolve_path(file, thread_current()->curr_dir, &fileNameOffset, true);
    struct dir* parentDir = NULL;
    // failed to open directory, or 'LP_create("/",...)'
    if(!(dirInode->is_directory && (parentDir = dir_open(dirInode))) || *(file + fileNameOffset) == '\0') {
      lock_release(&file_system_lock);
      dir_close(parentDir);
      return false;
    }
    
    if(strcmp(file + fileNameOffset, SELF_DIRECTORY_STRING) == 0 || strcmp(file + fileNameOffset, PARENT_DIRECTORY_STRING) == 0) {
      lock_release(&file_system_lock);
      dir_close(parentDir);
      return false;
    }
    
    bool outcome = filesys_create(file + fileNameOffset, parentDir, false, initial_size);
    dir_close(parentDir);
    lock_release(&file_system_lock);
    
    return outcome;
}

/*
 --------------------------------------------------------------------
 Description: Deletes the file called file. Returns true if 
    successful, false otherwise. A file may be removed regardless 
    of whether it is open or closed, and removing an open file 
    does not close it. See Removing an Open File, for details.
 NOTE: we must acquire the file_system_lock to ensure that we
    are the only process accessing the file_system.
 --------------------------------------------------------------------
 */
static bool LP_remove (const char *file) {
    check_usr_string(file);
    
    lock_acquire(&file_system_lock);
    bool outcome = filesys_remove(file);
    lock_release(&file_system_lock);
    
    return outcome;
}

/*
 --------------------------------------------------------------------
 Description: Opens the file called file. Returns a nonnegative 
    integer handle called a "file descriptor" (fd), or -1 if the
    file could not be opened.
 --------------------------------------------------------------------
 */
static int LP_open (const char *file) {
    check_usr_string(file);
    lock_acquire(&file_system_lock);
    
    struct file* fp = filesys_open(file);
    if (fp == NULL) {
        lock_release(&file_system_lock);
        return -1;
    }
    int fd = add_to_open_file_list(fp);
    lock_release(&file_system_lock);
    return fd;
}

/*
 --------------------------------------------------------------------
 Description: Returns the size, in bytes, of the file open as fd.
 NOTE: if no open file for fd, then returns -1;
 --------------------------------------------------------------------
 */
static int LP_filesize (int fd) {
    lock_acquire(&file_system_lock);
    struct file_package* package = get_file_package_from_open_list(fd);
    if (package == NULL || file_is_dir(package->fp)) {
        lock_release(&file_system_lock);
        return -1;
    }
    off_t size = file_length(package->fp);
    lock_release(&file_system_lock);
    return (int)size;
}

/*
 --------------------------------------------------------------------
 Description: Reads size bytes from the file open as fd into buffer. 
    Returns the number of bytes actually read (0 at end of file), 
    or -1 if the file could not be read (due to a condition other 
    than end of file). Fd 0 reads from the keyboard using input_getc().
 --------------------------------------------------------------------
 */
static int LP_read (int fd, void *buffer, unsigned length) {
    check_usr_buffer(buffer, length);
    
    if (fd == STDIN_FILENO) {
        char* char_buff = (char*)buffer;
        unsigned i;
        for (i = 0; i < length; i++) {
            char_buff[i] = input_getc();
        }
        return length;
    }
    
    lock_acquire(&file_system_lock);
    struct file_package* package = get_file_package_from_open_list(fd);
    if (package == NULL || file_is_dir(package->fp)) {
        lock_release(&file_system_lock);
        return -1;
    }
    int num_bytes_read = file_read_at(package->fp, buffer, length, package->position);
    package->position += num_bytes_read;
    lock_release(&file_system_lock);
    return num_bytes_read;
}

/*
 --------------------------------------------------------------------
 Description: Writes size bytes from buffer to the open file fd. 
    Returns the number of bytes actually written, which may be less 
    than size if some bytes could not be written.
 --------------------------------------------------------------------
 */
static int LP_write (int fd, const void *buffer, unsigned length) {
    check_usr_buffer(buffer, length);
    
    if (fd == STDOUT_FILENO) {
        putbuf(buffer, length);
        return length;
    }
    
    lock_acquire(&file_system_lock);
    struct file_package* package = get_file_package_from_open_list(fd);
    if (package == NULL) {
        lock_release(&file_system_lock);
        return -1;
    }
    int num_bytes_written = file_write_at(package->fp, buffer, length, package->position);
    package->position += num_bytes_written;
    lock_release(&file_system_lock);
    return num_bytes_written;
}

/*
 --------------------------------------------------------------------
 Description: Changes the next byte to be read or written in open 
    file fd to position, expressed in bytes from the beginning of 
    the file. (Thus, a position of 0 is the file's start.)
 NOTE: The assignment spec indicates we do not need to check the    
    validity of the new position v. filesize, as this is handled for
    us in the write and read implimentations in the filsysem. 
 --------------------------------------------------------------------
 */
static void LP_seek (int fd, unsigned position) {
    lock_acquire(&file_system_lock);
    struct file_package* package = get_file_package_from_open_list(fd);
    if (package == NULL) {
        lock_release(&file_system_lock);
        LP_exit(-1);
    }
    package->position = position;
    lock_release(&file_system_lock);
}

/*
 --------------------------------------------------------------------
 Description: Returns the position of the next byte to be read 
    or written in open file fd, expressed in bytes from the beginning 
    of the file.
 NOTE: In the case of an invalid fd, we will have to call exit with 
    an error message as the porotype of this system call having an 
    unsigned return value does not allow us to return -1 on error. 
 --------------------------------------------------------------------
 */
static unsigned LP_tell (int fd) {
    lock_acquire(&file_system_lock);
    struct file_package* package = get_file_package_from_open_list(fd);
    if (package == NULL) {
        lock_release(&file_system_lock);
        LP_exit(-1);
    }
    unsigned position = package->position;
    lock_release(&file_system_lock);
    return position;
}

/*
 --------------------------------------------------------------------
 Description: Closes file descriptor fd. Exiting or terminating a 
    process implicitly closes all its open file descriptors, as if 
    by calling this function for each one.
 NOTE: if there is no associated file_package for the given fd, 
    forces us to exit the program. 
 --------------------------------------------------------------------
 */
static void LP_close (int fd) {
    lock_acquire(&file_system_lock);
    struct file_package* package = get_file_package_from_open_list(fd);
    if (package == NULL) {
        lock_release(&file_system_lock);
        LP_exit(-1);
    }
    file_close(package->fp);
    list_remove(&package->elem);
    lock_release(&file_system_lock);
    free(package);
}





/*
 --------------------------------------------------------------------
 Description: Changes the current working directory of the process to
  dir, which may be relative or absolute. Returns true if successful,
  false on failure.
 --------------------------------------------------------------------
 */
static bool chdir(const char* dir) {
  check_usr_string(dir);
  lock_acquire(&file_system_lock);
  struct thread* t = thread_current();
  int unused = 0;
  struct inode* dirInode = dir_resolve_path(dir, t->curr_dir, &unused, false);
  if (dirInode == NULL || !dirInode->is_directory) {
      lock_release(&file_system_lock);
      return false;
  }
  struct dir* new_dir = dir_open(dirInode);
  if(new_dir == NULL)
  {
    lock_release(&file_system_lock);
    return false;
  }
  dir_close(t->curr_dir);
  t->curr_dir = new_dir;
  lock_release(&file_system_lock);
  return true;
}

/*
 --------------------------------------------------------------------
 Description: Creates the directory named dir, which may be relative
  or absolute. Returns true if successful, false on failure. Fails if
  dir already exists or if any directory name in dir, besides the
  last, does not already exist.
 --------------------------------------------------------------------
 */
static bool mkdir(const char* dir) {
  check_usr_string(dir);
  lock_acquire(&file_system_lock);
  int fileNameOffset;
  struct inode* dirInode = dir_resolve_path(dir, thread_current()->curr_dir, &fileNameOffset, true);
  struct dir* parentDir = NULL;
  // failed to open directory, or 'mkdir("/")'
  if(!(dirInode->is_directory && (parentDir = dir_open(dirInode))) || *(dir + fileNameOffset) == '\0') {
    lock_release(&file_system_lock);
    dir_close(parentDir);
    return false;
  }
  
  if(strcmp(dir + fileNameOffset, SELF_DIRECTORY_STRING) == 0 || strcmp(dir + fileNameOffset, PARENT_DIRECTORY_STRING) == 0) {
    lock_release(&file_system_lock);
    dir_close(parentDir);
    return false;
  }
  
  bool success = filesys_create(dir + fileNameOffset, parentDir, true, 16);
  dir_close(parentDir);
  lock_release(&file_system_lock);
  return success;
}

/*
 --------------------------------------------------------------------
 Description: Reads a directory entry from file descriptor fd, which
  must represent a directory. If successful, stores the
  null-terminated file name in name, which must have room for
  READDIR_MAX_LEN + 1 bytes, and returns true. If no entries are left
  in the directory, returns false.
 --------------------------------------------------------------------
 */
static bool readdir(int fd, char* name) {
  check_usr_buffer(name, NAME_MAX + 1);
  lock_acquire(&file_system_lock);
  struct file_package* package = get_file_package_from_open_list(fd);
  if (package == NULL) {
      lock_release(&file_system_lock);
      LP_exit(-1);
  }
  if(!file_is_dir(package->fp)) {
    return false;
  }
  bool result = dir_readdir(package->fp->dir, name);
  lock_release(&file_system_lock);
  return result;
}

/*
 --------------------------------------------------------------------
 Description: Returns true if fd represents a directory, false if it
  represents an ordinary file.
 --------------------------------------------------------------------
 */
static bool isdir(int fd) {
  lock_acquire(&file_system_lock);
  struct file_package* package = get_file_package_from_open_list(fd);
  if (package == NULL) {
      lock_release(&file_system_lock);
      LP_exit(-1);
  }
  bool result = file_is_dir(package->fp);
  lock_release(&file_system_lock);
  return result;
}

/*
 --------------------------------------------------------------------
 Description: Returns the inode number of the inode associated with
  fd, which may represent an ordinary file or a directory.
 --------------------------------------------------------------------
 */
static int inumber(int fd) {
  lock_acquire(&file_system_lock);
  struct file_package* package = get_file_package_from_open_list(fd);
  if (package == NULL) {
      lock_release(&file_system_lock);
      LP_exit(-1);
  }
  int result = (int)(package->fp->inode->sector);
  lock_release(&file_system_lock);
  return result;
}





/*
 --------------------------------------------------------------------
 Description: returns the file_packae struct that corresponds to a 
    given fd in the list of open_files within a process. 
 --------------------------------------------------------------------
 */
static struct file_package* get_file_package_from_open_list(int fd) {
    struct thread* curr_thread = thread_current();
    struct list_elem* curr = list_head(&curr_thread->open_files);
    struct list_elem* tail = list_tail(&curr_thread->open_files);
    while (true) {
        curr = list_next(curr);
        if (curr == tail) break;
        struct file_package* package = list_entry(curr, struct file_package, elem);
        if (package->fd == fd) {
            return package;
        }
    }
    return NULL;
}

/*
 --------------------------------------------------------------------
 Description: this funtion makes a thread package around this
    struct and then adds it to the list of files open for the
    given thread. 
 --------------------------------------------------------------------
 */
static int add_to_open_file_list(struct file* fp) {
    struct thread* curr_thread = thread_current();
    struct file_package* package = malloc(sizeof(struct file_package));
    if (package == NULL) {
        return -1;
    }
    package->position = 0;
    package->fp = fp;
    int fd = curr_thread->fd_counter;
    package->fd = fd;
    curr_thread->fd_counter++;
    list_push_back(&curr_thread->open_files, &package->elem);
    return fd;
}

/*
 --------------------------------------------------------------------
 Description: Checks to ensure that all pointers within the usr's
    supplied buffer are proper for user space. 
 NOTE: If this function completes and returns, we know that the 
    buffer is solid. 
 ADDITOIN: CHeck every 4kb for every page until length is exceeded. 
 --------------------------------------------------------------------
 */
#define BYTES_PER_PAGE PGSIZE
static void check_usr_buffer(const void* buffer, unsigned length) {
    check_usr_ptr(buffer);
    unsigned curr_offset = BYTES_PER_PAGE;
    while (true) {
        if (curr_offset >= length) break;
        check_usr_ptr((const void*)((char*)buffer + curr_offset));
        curr_offset += BYTES_PER_PAGE;
    }
    check_usr_ptr((const void*)((char*)buffer + length));
}


/*
 --------------------------------------------------------------------
 Description: checks the pointer to make sure that it is valid.
    A pointer is valid only if it is within user virtual address 
    space, it is not null, and it is mapped. 
 NOTE: We use the is_usr_vaddr in thread/vaddr.h and pagedir_get_page 
    in userprg/pagedir.c
 NOTE: If the pointer is determined to be invalid, we call
    the exit system call which will terminate the current program.
    We pass the appropriate error as well. 
 NOTE: If this function completes and returns, than we know the pointer
    is valid, and we continue operation in the kernel processing
    the system call.
 --------------------------------------------------------------------
 */
static void check_usr_ptr(const void* ptr) {
    if (ptr == NULL) {
        LP_exit(-1);
    }
    if (!is_user_vaddr(ptr)) {
        LP_exit(-1);
    } 
    if (pagedir_get_page(thread_current()->pagedir, ptr) == NULL) {
        LP_exit(-1);
    }
}

/*
 --------------------------------------------------------------------
 Description: checks each character in the string to make sure that
    the pointers are non null, are within user virtual address space,
    and are properly mapped. 
 --------------------------------------------------------------------
 */
static void check_usr_string(const char* str) {
    while (true) {
        const void* ptr = (const void*)str;
        check_usr_ptr(ptr);
        if (*str == '\0') break;
        str = (char*)str + 1;
    }
}

/*
 --------------------------------------------------------------------
 Description: This is a helper method for reading values from the 
    frame. We increment f->esp by offset, check the pointer to make
    sure it is valid, and then return the numerical value that resides
    at the address of the pointer. 
 NOTE: The return value of this function is the uinsigned int equivalent
    of the bits at said address. It is the responsibility of the caller
    to cast this return value to the appropriate type.
 --------------------------------------------------------------------
 */
static uint32_t read_frame(struct intr_frame* f, int byteOffset) {
    void* addr = f->esp + byteOffset;
    check_usr_ptr(addr);
    return *(uint32_t*)addr;
}

