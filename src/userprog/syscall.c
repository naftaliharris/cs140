#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"




//TO DO LIST:
//1. CHECK ALL STRINGS FOR POINTER VALIDITY
//2. CHECK ALL BUFFERS FOR POINTER VALIDITY
//3. SYNCHRONIZE ALL FILESYSTEM CALLS WITH A SINGLE LOCK
//4. THE DEFAULT CASE IN SYSTEM_HANDLER SWITCH STATEMENT
//5. HOW TO RESPOND IF THE CHECK_FILENAME_LENGTH RETURNS FALSE

struct file_package {
    int fd; //file descriptor
    off_t position; //this file_package's position
    struct file* fp; //file pointer
    struct list_elem elem; //so it can be placed in a list
}

static lock file_system_lock;

static void syscall_handler (struct intr_frame *);

// BEGIN LP DEFINED HELPER FUNCTIONS//
void check_usr_ptr(void* u_ptr);
void check_usr_string(char* str);
void check_usr_buffer(void* buffer, unsigned length);
bool check_file_name_length(const char* filename);
uint32_t read_frame(struct intr_frame* f, int byteOffset);
int add_to_open_file_list(struct file* fp);
struct file* get_file_from_open_list(int fd);
struct file_package* get_file_package_from_open_list(int fd);
// END LP DEFINED HELPER FUNCTIONS  //

// BEGIN LP DEFINED SYSTEM CALL HANDLERS //
void LP_halt (void) NO_RETURN;
void LP_exit (int status) NO_RETURN;
pid_t LP_exec (const char* command_line);
int LP_wait (pid_t pid);
bool LP_create (const char *file, unsigned initial_size);
bool LP_remove (const char *file);
int LP_open (const char *file);
int LP_filesize (int fd);
int LP_read (int fd, void *buffer, unsigned length);
int LP_write (int fd, const void *buffer, unsigned length);
void LP_seek (int fd, unsigned position);
unsigned LP_tell (int fd);
void LP_close (int fd);
// END   LP DEFINED SYSTEM CALL HANDLERS //


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
    lock_init(&file_system_lock);
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
syscall_handler (struct intr_frame *f UNUSED) 
{
    int systemCall_num = (int)read_frame(f, 0);
    switch (systemCall_num) {
        case SYS_HALT:
            LP_halt();
            break;
        case SYS_EXIT:
            int status = (int)read_frame(f, 4);
            LP_exit(status);
            break;
        case SYS_EXEC:
            char* command_line = (char*)read_frame(f, 4);
            f->eax = (uint32_t)LP_exec(command_line);
            break;
        case SYS_WAIT:
            pid_t pid = (pid_t)read_frame(f, 4);
            f->eax = (uint32_t)LP_wait(pid);
            break;
        case SYS_CREATE:
            const char* file = (const char*)read_frame(f, 4);
            unsigned initial_size = (unsigned)read_frame(f, 8);
            f->eax = (uint32_t)LP_create(file, initial_size);
            break;
        case SYS_REMOVE:
            const char* file = (const char*)read_frame(f, 4);
            f->eax = (uint32_t)LP_remove(file);
            break;
        case SYS_OPEN:
            const char* file = (const char*)read_frame(f, 4);
            f->eax = (uint32_t)LP_open(file);
            break;
        case SYS_FILESIZE:
            int fd = (int)read_frame(f, 4);
            f->eax = (uint32_t)LP_filesize(fd);
            break;
        case SYS_READ:
            int fd = (int)read_frame(f, 4);
            void* buffer = (void*)read_frame(f, 8);
            unsigned length = (unsigned)read_frame(f, 12);
            f->eax = (uint32_t)LP_read(fd, buffer, length);
            break;
        case SYS_WRITE:
            int fd = (int)read_frame(f, 4);
            const void* buffer = (const void*)read_frame(f, 8);
            unsigned length = (unsigned)read_frame(f, 12);
            f->eax = (uint32_t)LP_write(fd, buffer, length);
            break;
        case SYS_SEEK:
            int fd = (int)read_frame(f, 4);
            unsigned position = (unsigned)read_frame(f, 8);
            LP_seek(fd, position);
            break;
        case SYS_TELL:
            int fd = (int)read_frame(f, 4);
            f->eax = (uint32_t)LP_tell(fd);
            break;
        case SYS_CLOSE:
            int fd = (int)read_frame(f, 4);
            LP_close(fd);
            break;
        default:
            //IF WE GET HERE, SHOULD WE EXIT THE PROCESS???
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
void LP_halt (void) NO_RETURN {
    shutdown_power_off ();
}

/*
 --------------------------------------------------------------------
 Description: Terminates the current user program, returning 
    status to the kernel. If the process's parent waits for 
    it (see below), this is the status that will be returned. 
    Conventionally, a status of 0 indicates success and nonzero 
    values indicate errors.
 --------------------------------------------------------------------
 */
void LP_exit (int status) NO_RETURN {
    
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
 --------------------------------------------------------------------
 */
pid_t LP_exec (const char* command_line) {
    
}

/*
 --------------------------------------------------------------------
 Description: Waits for a child process pid and retrieves the 
    child's exit status.
 --------------------------------------------------------------------
 */
int LP_wait (pid_t pid) {
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
bool LP_create (const char *file, unsigned initial_size) {
    check_usr_string(file);
    if (!check_file_name_length(file)) {
        //what to do here?
    }
    
    lock_acquire(&file_system_lock);
    bool outcome = filesys_create(file, initial_size);
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
bool LP_remove (const char *file) {
    check_usr_string(file);
    if (!check_file_name_length(file)) {
        what to do here?
    }
    
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
int LP_open (const char *file) {
    check_usr_string(file);
    if (!check_file_name_length(file)) {
        what to do here?
    }
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
int LP_filesize (int fd) {
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
int LP_read (int fd, void *buffer, unsigned length) {
    check_usr_buffer(buffer, length);
    
    if (fd == STDIN_FILENO) {
        char* char_buff = (char*)buffer;
        for (int i = 0; i < length; i++) {
            char_buff[i] = input_getc();
        }
        return length;
    }
    
    lock_acquire(&file_system_lock);
    struct file_package* package = get_file_package_from_open_list(fd);
    if (package == NULL) {
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
int LP_write (int fd, const void *buffer, unsigned length) {
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
void LP_seek (int fd, unsigned position) {
    lock_acquire(&file_system_lock);
    struct file_package* package = get_file_package_from_open_list(fd);
    if (package == NULL) {
        lock_release(&file_system_lock);
        CALL EXIT WITH AN ERROR MESSAGE
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
unsigned LP_tell (int fd) {
    lock_acquire(&file_system_lock);
    struct file_package* package = get_file_package_from_open_list(fd);
    if (package == NULL) {
        lock_release(&file_system_lock);
        INVOKE EXIT HERE WITH AN ERROR MESSAGE
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
void LP_close (int fd) {
    lock_acquire(&file_system_lock);
    struct file_package* package = get_file_package_from_open_list(fd);
    if (package == NULL) {
        lock_release(&file_system_lock);
        INVOKE ERROR HERE WITH AN ERROR MESSAGE
    }
    file_close(package->fp);
    list_remove(&package->elem);
    lock_release(&file_system_lock);
    free(package);
}

/*
 --------------------------------------------------------------------
 Description: returns the file_packae struct that corresponds to a 
    given fd in the list of open_files within a process. 
 --------------------------------------------------------------------
 */
struct file_package* get_file_package_from_open_list(int fd) {
    struct thread* curr_thread = thread_current();
    struct list_elem* curr = list_head(&curr_thread->open_files);
    struct list_elem* tail = list_tail(&curr_thread->open_files);
    while (true) {
        curr = list_next(curr);
        if (curr == tail) break;
        struct file_package* package = list_entry(curr, struct file_package, elem);
        if (package->fd == fd) return package;
    }
    return NULL;
}

/*
 --------------------------------------------------------------------
 Description: checks the list of file_packages in the thread for one 
    contains fd. If none exist, returns NULL.
 --------------------------------------------------------------------
 */
struct file* get_file_from_open_list(int fd) {
    struct thread* curr_thread = thread_current();
    struct list_elem* curr = list_head(&curr_thread->open_files);
    struct list_elem* tail = list_tail(&curr_thread->open_files);
    while (true) {
        curr = list_next(curr);
        if (curr == tail) break;
        struct file_package* package = list_entry(curr, struct file_package, elem);
        if (package->fd == fd) return package->fp;
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
int add_to_open_file_list(struct file* fp) {
    struct thread* curr_thread = thread_current();
    struct file_package* package = malloc(sizeof(struct file_package));
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
 Description: ensures that the length of the filename does not 
    exceed 14 characters. 
 --------------------------------------------------------------------
 */
#define MAX_FILENAME_LENGTH 14
bool check_file_name_length(const char* filename) {
    size_t length = strlen(filename);
    if (length > MAX_FILENAME_LENGTH) return false;
    return true;
}

/*
 --------------------------------------------------------------------
 Description: Checks to ensure that all pointers within the usr's
    supplied buffer are proper for user space. 
 NOTE: If this function completes and returns, we know that the 
    buffer is solid. 
 --------------------------------------------------------------------
 */
void check_usr_buffer(void* buffer, unsigned length) {
    char* buff_as_char_ptr = (char*)buffer;
    for (int i = 0; i < length; i++) {
        const void* curr_addr = buff_as_char_ptr;
        check_usr_ptr(curr_addr);
        buff_as_char_ptr = buff_as_char_ptr + 1;
    }
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
void check_usr_ptr(const void* ptr) {
    if (ptr == NULL) {
        //here is where we call exit
    }
    if (!is_usr_vaddr(ptr)) {
        //here is where we call exit
    } 
    if (pagedir_get_page(thread_current()->pagedir, ptr) == NULL) {
        //here is where we call exit
    }
}

/*
 --------------------------------------------------------------------
 Description: checks each character in the string to make sure that
    the pointers are non null, are within user virtual address space,
    and are properly mapped. 
 --------------------------------------------------------------------
 */
void check_usr_string(char* str) {
    while (true) {
        if (*str == '\0') break;
        const void* ptr = (const void*)str;
        check_usr_ptr(ptr);
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
uint32_t read_frame(struct intr_frame* f, int byteOffset) {
    void* addr = f->esp + byteOffset;
    check_usr_ptr(addr);
    return *(uint32_t*)addr;
}

