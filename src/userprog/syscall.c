#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <list.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "threads/init.h"
#include "threads/vaddr.h"
#include "devices/shutdown.h"
#include "devices/input.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "userprog/process.h"
#include "lib/string.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "vm/mmap.h"


static void syscall_handler (struct intr_frame *);

// BEGIN LP DEFINED HELPER FUNCTIONS//
static void check_usr_ptr(const void* u_ptr, void* esp);
static void check_usr_string(const char* str, void* esp);
static void check_usr_buffer(const void* buffer, unsigned length, void* esp, bool check_writable);
static bool check_file_name_length(const char* filename);
static uint32_t read_frame(struct intr_frame* f, int byteOffset);
static int add_to_open_file_list(struct file* fp);
static struct file* get_file_from_open_list(int fd);
static struct file_package* get_file_package_from_open_list(int fd);
// END LP DEFINED HELPER FUNCTIONS  //

// BEGIN LP DEFINED SYSTEM CALL HANDLERS //
static void LP_halt (void) NO_RETURN;
static void LP_exit (int status) NO_RETURN;
static pid_t LP_exec (const char* command_line, void* esp);
static int LP_wait (pid_t pid);
static bool LP_create (const char *file, unsigned initial_size, void* esp);
static bool LP_remove (const char *file, void* esp);
static int LP_open (const char *file, void* esp);
static int LP_filesize (int fd);
static int LP_read (int fd, void *buffer, unsigned length, void* esp);
static int LP_write (int fd, const void *buffer, unsigned length, void* esp);
static void LP_seek (int fd, unsigned position);
static unsigned LP_tell (int fd);
static void LP_close (int fd);
static mapid_t mmap (int fd, void *addr);
static void munmap (mapid_t mapping);
// END   LP DEFINED SYSTEM CALL HANDLERS //

//BEGIN LP Project 3 additions
static void pinning_for_system_call(const void* begin, unsigned length, bool should_pin);
static unsigned strlen_pin(const char* string);
//END LP Project 3 additions


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
            f->eax = (uint32_t)LP_exec((char*)arg1, f->esp);
            break;
        case SYS_WAIT:
            arg1 = read_frame(f, 4);
            f->eax = (uint32_t)LP_wait((pid_t)arg1);
            break;
        case SYS_CREATE:
            arg1 = read_frame(f, 4);
            arg2 = read_frame(f, 8);
            f->eax = (uint32_t)LP_create((const char*)arg1, (unsigned)arg2, f->esp);
            break;
        case SYS_REMOVE:
            arg1 = read_frame(f, 4);
            f->eax = (uint32_t)LP_remove((const char*)arg1, f->esp);
            break;
        case SYS_OPEN:
            arg1 = read_frame(f, 4);
            f->eax = (uint32_t)LP_open((const char*)arg1, f->esp);
            break;
        case SYS_FILESIZE:
            arg1 = read_frame(f, 4);
            f->eax = (uint32_t)LP_filesize((int)arg1);
            break;
        case SYS_READ:
            arg1 = read_frame(f, 4);
            arg2 = read_frame(f, 8);
            arg3 = read_frame(f, 12);
            f->eax = (int)LP_read((int)arg1, (void*)arg2, (unsigned)arg3, f->esp);
            break;
        case SYS_WRITE:
            arg1 = read_frame(f, 4);
            arg2 = read_frame(f, 8);
            arg3 = read_frame(f, 12);
            f->eax = (uint32_t)LP_write((int)arg1, (const void*)arg2, (unsigned)arg3, f->esp);
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
        case SYS_MMAP:
            arg1 = read_frame(f, 4);
            arg2 = read_frame(f, 8);
            f->eax = mmap((int)arg1, (void*)arg2);
            break;
        case SYS_MUNMAP:
            arg1 = read_frame(f, 4);
            munmap((mapid_t) arg1);
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
static pid_t LP_exec (const char* command_line, void* esp) {
    check_usr_string(command_line, esp);
    struct thread* curr_thread = thread_current();
    
    unsigned length = strlen_pin(command_line);
    
    pid_t pid = process_execute(command_line);
    pinning_for_system_call(command_line, length, false);
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
static bool LP_create (const char *file, unsigned initial_size, void* esp) {
    check_usr_string(file, esp);
    if (!check_file_name_length(file)) {
        return false;
    }
    lock_acquire(&file_system_lock);
    unsigned length = strlen(file);
    bool outcome = filesys_create(file, initial_size);
    pinning_for_system_call(file, length, false);
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
static bool LP_remove (const char *file, void* esp) {
    check_usr_string(file, esp);
    if (!check_file_name_length(file)) {
        return -1;
    }
    
    lock_acquire(&file_system_lock);
    unsigned length = strlen(file);
    bool outcome = filesys_remove(file);
    pinning_for_system_call(file, length, false);
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
static int LP_open (const char *file, void* esp) {
    check_usr_string(file, esp);
    if (!check_file_name_length(file)) {
        return -1;
    }
    lock_acquire(&file_system_lock);
    unsigned length = strlen(file);
    struct file* fp = filesys_open(file);
    if (fp == NULL) {
        pinning_for_system_call(file, length, false);
        lock_release(&file_system_lock);
        return -1;
    }
    pinning_for_system_call(file, length, false);
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
    struct file* fp = get_file_from_open_list(fd);
    if (fp == NULL) {
        lock_release(&file_system_lock);
        return -1;
    }
    off_t size = file_length(fp);
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
static int LP_read (int fd, void *buffer, unsigned length, void* esp) {
    check_usr_buffer(buffer, length, esp, true);
    pinning_for_system_call(buffer, length, true);
    
    if (fd == STDIN_FILENO) {
        char* char_buff = (char*)buffer;
        unsigned i;
        for (i = 0; i < length; i++) {
            char_buff[i] = input_getc();
        }
        pinning_for_system_call(buffer, length, false);
        return length;
    }
    
    lock_acquire(&file_system_lock);
    struct file_package* package = get_file_package_from_open_list(fd);
    if (package == NULL) {
        lock_release(&file_system_lock);
        pinning_for_system_call(buffer, length, false);
        return -1;
    }
    int num_bytes_read = file_read_at(package->fp, buffer, length, package->position);
    pinning_for_system_call(buffer, length, false);
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
static int LP_write (int fd, const void *buffer, unsigned length, void* esp) {
    check_usr_buffer(buffer, length, esp, false);
    pinning_for_system_call(buffer, length, true);
    
    if (fd == STDOUT_FILENO) {
        putbuf(buffer, length);
        pinning_for_system_call(buffer, length, false);
        return length;
    }
    
    lock_acquire(&file_system_lock);
    struct file_package* package = get_file_package_from_open_list(fd);
    if (package == NULL) {
        lock_release(&file_system_lock);
        pinning_for_system_call(buffer, length, false);
        return -1;
    }
    int num_bytes_written = file_write_at(package->fp, buffer, length, package->position);
    pinning_for_system_call(buffer, length, false);
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

static mapid_t
mmap(int fd, void *addr)
{
    /* Validate the parameters */
    if (((uint32_t)addr) % PGSIZE != 0) {
        return -1;
    }
    if (fd == 0 || fd == 1) {
        return -1;
    }

    /* Ensure the fd has been assigned to the user */
    lock_acquire(&file_system_lock);
    struct file* fp = get_file_from_open_list(fd);
    if (fp == NULL) {
        lock_release(&file_system_lock);
        return -1;
    }
    off_t size = file_length(fp);
    lock_release(&file_system_lock);

    /* Ensure that the requested VM region wouldn't contain invalid addresses
     * or overlap other user memory */
    struct thread *t = thread_current();
    void *page;
    for (page = addr; page < addr + size; page++)
    {
        if (page == NULL) {
            return -1;
        }
        if (!is_user_vaddr(page)) {
            return -1;
        }
        if (find_spte(page, t) != NULL) {
            return -1;
        }
    }

    /* Fill in the mmap state data */
    struct mmap_state *mmap_s = malloc(sizeof(struct mmap_state));
    if (mmap_s == NULL) {
        thread_current()->vital_info->exit_status = -1;
        if (thread_current()->is_running_user_program) {
            printf("%s: exit(%d)\n", thread_name(), -1);
        }
        thread_exit();
    }

    lock_acquire(&file_system_lock);
    mmap_s->fp = file_reopen(fp);
    if (mmap_s->fp == NULL)
    {
        lock_release(&file_system_lock);
        return -1;
    }
    lock_release(&file_system_lock);

    mmap_s->vaddr = addr;
    mmap_s->mapping = t->mapid_counter;
    list_push_back(&t->mmapped_files, &mmap_s->elem);

    /* Finally, create the necessary SPTEs */
    for (page = addr; page < addr + size; page += PGSIZE)
    {
        uint32_t read_bytes = page + PGSIZE < addr + size ? PGSIZE
                                                          : addr + size - page;
        uint32_t zero_bytes = PGSIZE - read_bytes;
        create_spte_and_add_to_table(MMAPED_PAGE,             /* Location */
                                     page,                    /* Address */
                                     true,                    /* Writeable */
                                     false,                   /* Loaded */
                                     mmap_s->fp,              /* File */
                                     page - addr,             /* File offset */
                                     read_bytes,
                                     zero_bytes);
    }

    return t->mapid_counter++;
}

static void
munmap(mapid_t mapping)
{
    /* Find the mmap_state */
    struct thread *t = thread_current();
    struct list_elem *e = list_head(&t->mmapped_files);
    while ((e = list_next (e)) != list_end (&t->mmapped_files)) 
    {
        struct mmap_state *mmap_s = list_entry(e, struct mmap_state, elem);
        if (mmap_s->mapping == mapping) {
            munmap_state(mmap_s, t);
            return;
        }
    }
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
 Description: checks the list of file_packages in the thread for one 
    contains fd. If none exist, returns NULL.
 --------------------------------------------------------------------
 */
static struct file* get_file_from_open_list(int fd) {
    struct thread* curr_thread = thread_current();
    struct list_elem* curr = list_head(&curr_thread->open_files);
    struct list_elem* tail = list_tail(&curr_thread->open_files);
    while (true) {
        curr = list_next(curr);
        if (curr == tail) break;
        struct file_package* package = list_entry(curr, struct file_package, elem);
        if (package->fd == fd) {
            return package->fp;
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
 */
static unsigned strlen_pin(const char* string) {
    void* last_page = NULL;
    void* curr_page = NULL;
    unsigned str_len = 0;
    char* str = (char*)string;
    while (true) {
        const void* ptr = (const void*)str;
        curr_page = pg_round_down(ptr);
        if (curr_page != last_page) {
            pinning_for_system_call(curr_page, 0, true);
            last_page = curr_page;
        }
        if (*str == '\0') break;
        str_len++;
        str = (char*)str + 1;
    }
    return str_len;
}

/*
 --------------------------------------------------------------------
 Description: ensures that the length of the filename does not 
    exceed 14 characters. 
 --------------------------------------------------------------------
 */
#define MAX_FILENAME_LENGTH 14
static bool check_file_name_length(const char* filename) {
    
    void* last_page = NULL;
    void* curr_page = NULL;
    unsigned str_len = 0;
    char* str = (char*)filename;
    while (true) {
        const void* ptr = (const void*)str;
        curr_page = pg_round_down(ptr);
        if (curr_page != last_page) {
            pinning_for_system_call(curr_page, 0, true);
            last_page = curr_page;
        }
        if (*str == '\0') break;
        str_len++;
        str = (char*)str + 1;
    }
    if (str_len > MAX_FILENAME_LENGTH) {
        last_page = NULL;
        curr_page = NULL;
        str = (char*)filename;
        while (true) {
            const void* ptr = (const void*)str;
            curr_page = pg_round_down(ptr);
            if (curr_page != last_page) {
                pinning_for_system_call(curr_page, 0, false);
                last_page = curr_page;
            }
            if (*str == '\0') break;
            str = (char*)str + 1;
        }
        return false;
    }
    return true;
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
static void check_usr_buffer(const void* buffer, unsigned length, void* esp, bool check_writable) {
    check_usr_ptr(buffer, esp);
    struct spte* spte = find_spte((void*)buffer, thread_current());
    if (check_writable && spte->is_writeable == false) {
        LP_exit(-1);
    }
    unsigned curr_offset = BYTES_PER_PAGE;
    while (true) {
        if (curr_offset >= length) break;
        check_usr_ptr((const void*)((char*)buffer + curr_offset), esp);
        struct spte* spte = find_spte((void*)((char*)buffer + curr_offset), thread_current());
        if (spte->is_writeable == false) {
            LP_exit(-1);
        }
        curr_offset += BYTES_PER_PAGE;
    }
    check_usr_ptr((const void*)((char*)buffer + length), esp);
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
 NOTE: We need to replace the call to pagedir_get_page with a call
    to get spte, as the page we are accessing might not be mapped.
 --------------------------------------------------------------------
 */
static void check_usr_ptr(const void* ptr, void* esp) {
    if (ptr == NULL) {
        LP_exit(-1);
    }
    if (!is_user_vaddr(ptr)) {
        LP_exit(-1);
    } 
    if (find_spte((void*)ptr, thread_current()) == NULL) {
        if (is_valid_stack_access(esp, (void*)ptr)) {
            void* new_stack_page = pg_round_down(ptr);
            grow_stack(new_stack_page);
        } else {
            LP_exit(-1);
        }
    }
}

/*
 --------------------------------------------------------------------
 Description: checks each character in the string to make sure that
    the pointers are non null, are within user virtual address space,
    and are properly mapped. 
 --------------------------------------------------------------------
 */
static void check_usr_string(const char* str, void* esp) {
    while (true) {
        const void* ptr = (const void*)str;
        check_usr_ptr(ptr, esp);
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
    check_usr_ptr(addr, f->esp);
    return *(uint32_t*)addr;
}

/*
 --------------------------------------------------------------------
 DESCRIPTION: This function pins all of the pages that make up the 
    space from begin to length.
 NOTE: length is given in bytes
 NOTE: We assume that the validation of pointers has allready been
    done, so that these addresses are all valid.
 NOTE: We define pages by rounding down to the page boundary. If we 
    call pg_dir_round_up + 1, we will be in the next page. The 
    differnce between thus rounded up value + 1 and the current
    address is the offset of the address in the page.
 NOTE: if we ever encounter length being less than distance to 
    next page, we have covered all of the necessary pages.
 NOTE: if should_pin is true, we pin, otherwise, we un_pin
 NOTE: must be called first with pin, then unpin!
 --------------------------------------------------------------------
 */
static void pinning_for_system_call(const void* begin, unsigned length, bool should_pin) {
    void* curr_addr = (void*)begin;
    void* curr_page = pg_round_down(curr_addr);
    if (should_pin) {
        pin_page(curr_page);
    } else {
        un_pin_page(curr_page);
    }
    while (length > PGSIZE) {
        //add page size to curr_address to get next page
        //subtract page size from length
        curr_addr = (void*)((char*)curr_addr + PGSIZE);
        curr_page = pg_round_down(curr_addr);
        if (should_pin) {
            pin_page(curr_page);
        } else {
            un_pin_page(curr_page);
        }
        length -= PGSIZE;
    }
    curr_addr = (void*)((char*)curr_addr + length);
    void* last_page = pg_round_down(curr_addr);
    if ((uint32_t)last_page != (uint32_t)curr_page) {
        if (should_pin) {
            pin_page(last_page);
        } else {
            un_pin_page(last_page);
        }
    }
}

