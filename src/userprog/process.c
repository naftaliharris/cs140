#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/interrupt.h"

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);

/* LP Defined functions project 2 */
struct vital_info* get_child_by_tid(tid_t child_tid);
void notify_children_parent_is_finished(void);
void close_open_files(struct thread* t);
void release_resources(struct thread* t);
void release_all_locks(struct thread* t);

/*
 ----------------------------------------------------------------
 Starts a new thread running a user program loaded from
 FILENAME.  The new thread may be scheduled (and may even exit)
 before process_execute() returns.  Returns the new process's
 thread id, or TID_ERROR if the thread cannot be created.
 ----------------------------------------------------------------
 */
tid_t
process_execute (const char *arguments)
{
    char *fn_copy;
    tid_t tid;
    
    // strlen(arguments) should be less than PGSIZE - 8 bytes
    int maxlen = PGSIZE - sizeof(int) * 2;
    int arglen = strnlen(arguments, maxlen);
    if(arglen == 0 || arglen == maxlen)
    {
        return TID_ERROR;
    }
    
    /* Allocate a temp page of data that gets passed to our new thread */
    fn_copy = palloc_get_page (0);
    if (fn_copy == NULL)
        return TID_ERROR;
    
    // Copy argument string onto our temporary page
    // After the first two bytes
    // First byte: pointer to end of string
    // Second byte:  number of arguments
    int numargs = 0;
    const char* argument_copy = fn_copy + sizeof(int) + sizeof(int);
    strlcpy ((char*)argument_copy, (char*)arguments, PGSIZE);
    
    char* itr = (char*)argument_copy;
    
    // Run through rest of arguments; replace spaces with \0
    // We do this manually instead of using strtok_r
    // Because that function does not set adjacent spaces to \0
    // But only the first space
    // in_delim is initially true so that numargs counts the first
    bool in_delim = true;
    while(*itr != '\0')
    {
        if(*itr == ' ')
        {
            *itr = '\0';
            in_delim = true;
        }
        else if(in_delim)
        {
            in_delim = false;
            numargs++;
        }
        itr++;
    }
    
    // Store number of arguments and pointer to end of written data
    *((char**) fn_copy) = (char*)(itr - fn_copy);
    *(((int*) fn_copy) + 1) = numargs;
    
    const char* file_name = argument_copy;
    
    /* Create a new thread to execute FILE_NAME. */
    tid = thread_create (file_name, PRI_DEFAULT, start_process, fn_copy);
    // Free our page if the thread wasn't properly created
    // Otherwise start_process will free the page
    if (tid == TID_ERROR)
    {
        palloc_free_page (fn_copy);
    }
    return tid;
}

/*
 ----------------------------------------------------------------
 A thread function that loads a user process and starts it
 running.
 NOTE: This function will run in child process. Thus, this is
 where we want to signal to the parent that the child
 has finished loading.
 NOTE: We need to add a pointer to the parent thread in the
 child thread. We do this in the init_thread function.
 NOTE: this is the child process. So a call to thread_current
 will return a pointer to the child's struct thread.
 NOTE: the load function accesses the file system, so we have to
 lock it down.
 NOTE: if the child did not load its program successfully, we
 have to exit the child. We must set its exit status to -1
 as it
 QUESTION: Can we call exit(-1) in the case that success is false?
 ----------------------------------------------------------------
 */
static void
start_process (void *arg_page_)
{
    char *arg_page = arg_page_;
    char *file_name = arg_page_ + sizeof(int) + sizeof(int);
    struct intr_frame if_;
    bool success;
    
    /* Initialize interrupt frame and load executable. */
    memset (&if_, 0, sizeof if_);
    if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
    if_.cs = SEL_UCSEG;
    if_.eflags = FLAG_IF | FLAG_MBS;
    
    
    lock_acquire(&file_system_lock);
    success = load (file_name, &if_.eip, &if_.esp);
    lock_release(&file_system_lock);
    thread_current()->parent_thread->child_did_load_successfully = success;
    thread_current()->is_running_user_program = true;
    sema_up(&(thread_current()->parent_thread->sema_child_load));
    
    /* If load failed, quit. */
    if (!success)
    {
        palloc_free_page (arg_page);
        thread_current()->vital_info->exit_status = -1;
        thread_exit ();
        NOT_REACHED();
    }
    
    // With the stack created, modify it and place our arguments into it
    // From our temporary page, get the number of arguments and a pointer
    // To the end of our data
    int far_byte = *((int*) arg_page);
    int num_args = *(((int*) arg_page) + 1);
    
    /* Make sure we won't overflow the stack page */
    int max_stack_length = far_byte - 8              /* The argument string */
    + sizeof(char *)            /* NULL argv[argc] pointer */
    + num_args * sizeof(char *) /* argv[i] pointers */
    + sizeof(char **)           /* argv pointer */
    + sizeof(int)               /* argc */
    + sizeof(char **);          /* Fake return address */
    
    if (max_stack_length > PGSIZE)
    {
        palloc_free_page (arg_page);
        thread_current()->vital_info->exit_status = -1;
        thread_exit ();
        NOT_REACHED();
    }
    
    char* args[num_args];
    
    // Iterate from the end of our argument string, adding each byte to
    // The stack. We know each argument is separated by at least one \0
    // But we only want to write one \0 to the stack between arguments
    // Keep track of the stack pointers to the beginning of each argument
    int cur_arg = num_args;
    int i;
    bool skipping_nulls = true;
    for(i = far_byte; i >= (int)(sizeof(int) * 2); i--)
    {
        char* copy_byte = arg_page + i;
        if(*copy_byte == '\0')
        {
            skipping_nulls = true;
        }
        else
        {
            if(skipping_nulls)
            {
                skipping_nulls = false;
                
                if(cur_arg < num_args)
                {
                    args[cur_arg] = if_.esp;
                }
                cur_arg--;
                
                if_.esp = ((char*)if_.esp) - 1;
                *((char*)if_.esp) = '\0';
            }
            if_.esp = ((char*)if_.esp) - 1;
            *((char*)if_.esp) = *copy_byte;
        }
    }
    ASSERT(cur_arg == 0);
    args[0] = if_.esp;
    
    /* Add the NULL pointer for argv[argc] */
    if_.esp = ((char**)if_.esp) - 1;
    *((char**)if_.esp) = NULL;
    
    /* Add the pointers to other elements of argv */
    for(i = num_args - 1; i >= 0; i--)
    {
        if_.esp = ((char*)if_.esp) - sizeof(char*);
        *((char**)if_.esp) = args[i];
    }
    
    /* Add the argv pointer, the argc value, and fake return address */
    char* argv = if_.esp;
    if_.esp = ((char*)if_.esp) - sizeof(char*);
    *((char**)if_.esp) = argv;
    if_.esp = ((char*)if_.esp) - sizeof(int);
    *((int*)if_.esp) = num_args;
    if_.esp = ((char*)if_.esp) - sizeof(char*);
    *((char**)if_.esp) = NULL;
    
    palloc_free_page (arg_page);
    
    /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
    asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
    NOT_REACHED ();
}

/*
 ----------------------------------------------------------------
 Waits for thread TID to die and returns its exit status.  If
 it was terminated by the kernel (i.e. killed due to an
 exception), returns -1.  If TID is invalid or if it was not a
 child of the calling process, or if process_wait() has already
 been successfully called for the given TID, returns -1
 immediately, without waiting.
 
 NOTE: There are several courses of action for which we account
 for.
 FROM THE PARENT PERSPECTIVE
 1. The Parent process exits, and does not wait for children.
 In this case, the parent will clean up any children it
 did not wait on that have already finished, will set
 parent_exited to true in the children it has not waited on
 and who have not finished yet, and then the parent will be done
 2. The parent process waits for a child. In this case, the parent
 will wait until the child exits. The parent will then clean up
 the child once the semaphore clears.
 FROM THE CHILD PERSPECTIVE
 1. The child exits, but the parent has not exited yet. In this
 case, the child will set its field to indicate it has exited,
 and then leave itself for the parent to clean up. IMPORTANT, the
 child must signal the semaphore, as the parent might
 still wait on this child.
 2. The child exits after the parent has exited. In this case,
 the child will clean itelf up.
 NOTE: in this case, assuming pid is a valid pid for a child thread,
 the parent process will not end until pid ends. Thus, the parent
 must free the vital info.
 NOTE: first we check the initial conditions for which we return
 -1.
 ----------------------------------------------------------------
 */
int
process_wait (tid_t child_tid)
{
    struct vital_info* child_to_wait_on = get_child_by_tid(child_tid);
    if (child_to_wait_on == NULL) return -1;
    if (child_to_wait_on->has_allready_been_waited_on) return -1;
    child_to_wait_on->has_allready_been_waited_on = true;
    int returnVal = 0;
    lock_acquire(&child_to_wait_on->vital_info_lock);
    if (!child_to_wait_on->child_is_finished) {
        lock_release(&child_to_wait_on->vital_info_lock);
        sema_down(&child_to_wait_on->t->wait_on_me);
    } else {
        lock_release(&child_to_wait_on->vital_info_lock);
    }
    returnVal = child_to_wait_on->exit_status;
    list_remove(&child_to_wait_on->child_elem);
    free(child_to_wait_on);
    return returnVal;
}

/*
 ----------------------------------------------------------------
 Description: Returns a pointer to the child thread
 defined by tid. If no thread is in the list, returns NULL.
 NOTE: There is no race condition here, as the vital info
 structs are malloc'd, and they are only freed by the child
 if the parent has exited. Given the parent is executing
 this function, we know the parent has not exited, so these
 structs will always be there.
 ----------------------------------------------------------------
 */
struct vital_info* get_child_by_tid(tid_t child_tid) {
    struct thread* curr_thread = thread_current();
    struct list_elem* curr = list_head(&curr_thread->child_threads);
    struct list_elem* tail = list_tail(&curr_thread->child_threads);
    while (true) {
        curr = list_next(curr);
        if (curr == tail) break;
        struct vital_info* t = list_entry(curr, struct vital_info, child_elem);
        if (t->tid == child_tid) return t;
    }
    return NULL;
}

/*
 ----------------------------------------------------------------
 Description: closes all files that the current thread has open.
 ----------------------------------------------------------------
 */
void close_open_files(struct thread* t) {
    
    while (!list_empty(&t->open_files)) {
        struct list_elem* curr = list_pop_front(&t->open_files);
        struct file_package* package = list_entry(curr, struct file_package, elem);
        lock_acquire(&file_system_lock);
        file_close(package->fp);
        lock_release(&file_system_lock);
        free(package);
    }
}

/*
 ----------------------------------------------------------------
 Description: for all children in the waiting list, informs
 them that their parent is finished. If the child is finished
 then it frees that child's vital_info.
 NOTE: if this function is called, it is because the parent has
 exited. In such a case, we want to remove all vital_info
 structs from the parent's child list, and free the
 vital_info structs associated with finished children.
 If the child has not finished yet, we set the boolean
 parent_finished to true, which will prmopt the child
 to free its vital info when it fnishes.
 ----------------------------------------------------------------
 */
void notify_children_parent_is_finished() {
    struct thread* curr_thread = thread_current();
    while (!list_empty(&curr_thread->child_threads)) {
        struct list_elem* curr = list_pop_front(&curr_thread->child_threads);
        struct vital_info* child_vital_info = list_entry(curr, struct vital_info, child_elem);
        lock_acquire(&child_vital_info->vital_info_lock);
        if (child_vital_info->child_is_finished) {
            lock_release(&child_vital_info->vital_info_lock);
            free(child_vital_info);
        } else {
            child_vital_info->parent_is_finished = true;
            lock_release(&child_vital_info->vital_info_lock);
        }
    }
}

/*
 ----------------------------------------------------------------
 Description: walks the list of locks held by this thread and
 releases each one by one.
 NOTE: As described in our project 1 implimentation notes and
 design document, we use a dummy lock to store the threads
 initial priority. To designate the dummy lock from valid
 locks, we use the lock->holder. If NULL, we are holding
 the dummy lock, else, it is the valid lock. Thus, when we
 release all locks, we check for the dummy lock, as we cannot
 release it.
 ----------------------------------------------------------------
 */
void release_all_locks(struct thread* t) {
    while (!list_empty(&t->locks_held)) {
        struct list_elem* curr = list_pop_front(&t->locks_held);
        struct lock* lock = list_entry(curr, struct lock, elem);
        if (lock->holder != NULL) {
            lock_release(lock);
        }
    }
}

/*
 ----------------------------------------------------------------
 Description: disables interrupts, checks all locks are released,
 all malloc'd memory is freed, and any other resource is
 released.
 NOTE: we disable interrupts, as we have to check if this thead
 is holding any locks, and subsequently release them.
 ----------------------------------------------------------------
 */
void release_resources(struct thread* t) {
    enum intr_level old_level = intr_disable();
    
    close_open_files(t);
    release_all_locks(t);
    
    intr_set_level(old_level);
}

/*
 ----------------------------------------------------------------
 Free the current process's resources.
 
 NOTE: We must disable interrupts here so that synchronization
 of parent and child threads does not get interleaved.
 NOTE: This function gets called by thread_exit(), which itself
 is called by LP_exit.
 NOTE: if the parent is finished, the parent has allready removed
 the child from the child_list, so all we have to do is
 close the open files.
 ----------------------------------------------------------------
 */
void
process_exit (void)
{
    enum intr_level old_level = intr_disable();
    struct thread *cur = thread_current ();
    
    //LP Project 2 additions
    
    lock_acquire(&cur->vital_info->vital_info_lock);
    if (cur->vital_info->parent_is_finished) {
        lock_release(&cur->vital_info->vital_info_lock);
        free(cur->vital_info);
    } else {
        cur->vital_info->child_is_finished = true;
        lock_release(&cur->vital_info->vital_info_lock);
        sema_up(&cur->wait_on_me);
    }
    notify_children_parent_is_finished();
    release_resources(cur);
    
    
    uint32_t *pd;
    
    /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
    pd = cur->pagedir;
    if (pd != NULL)
    {
        /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
        cur->pagedir = NULL;
        pagedir_activate (NULL);
        pagedir_destroy (pd);
    }
    intr_set_level(old_level);
}


/*
 ----------------------------------------------------------------
 Sets up the CPU for running user code in the current
 thread.
 This function is called on every context switch.
 ----------------------------------------------------------------
 */
void
process_activate (void)
{
    struct thread *t = thread_current ();
    
    /* Activate thread's page tables. */
    pagedir_activate (t->pagedir);
    
    /* Set thread's kernel stack for use in processing
     interrupts. */
    tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
 from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/*
 ----------------------------------------------------------------
 Executable header.  See [ELF1] 1-4 to 1-8.
 This appears at the very beginning of an ELF binary.
 ----------------------------------------------------------------
 */
struct Elf32_Ehdr
{
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
};

/*
 ----------------------------------------------------------------
 Program header.  See [ELF1] 2-2 to 2-4.
 There are e_phnum of these, starting at file offset e_phoff
 (see [ELF1] 1-6).
 ----------------------------------------------------------------
 */
struct Elf32_Phdr
{
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
};

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/*
 ----------------------------------------------------------------
 Loads an ELF executable from FILE_NAME into the current thread.
 Stores the executable's entry point into *EIP
 and its initial stack pointer into *ESP.
 Returns true if successful, false otherwise.
 ----------------------------------------------------------------
 */
bool
load (const char *file_name, void (**eip) (void), void **esp)
{
    struct thread *t = thread_current ();
    struct Elf32_Ehdr ehdr;
    struct file *file = NULL;
    off_t file_ofs;
    bool success = false;
    int i;
    
    /* Allocate and activate page directory. */
    t->pagedir = pagedir_create ();
    if (t->pagedir == NULL)
        goto done;
    process_activate ();
    
    /* Open executable file. */
    file = filesys_open (file_name);
    if (file == NULL)
    {
        printf ("load: %s: open failed\n", file_name);
        goto done;
    }
    
    /* Read and verify executable header. */
    if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
        || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
        || ehdr.e_type != 2
        || ehdr.e_machine != 3
        || ehdr.e_version != 1
        || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
        || ehdr.e_phnum > 1024)
    {
        printf ("load: %s: error loading executable\n", file_name);
        goto done;
    }
    
    /* Read program headers. */
    file_ofs = ehdr.e_phoff;
    for (i = 0; i < ehdr.e_phnum; i++)
    {
        struct Elf32_Phdr phdr;
        
        if (file_ofs < 0 || file_ofs > file_length (file))
            goto done;
        file_seek (file, file_ofs);
        
        if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
            goto done;
        file_ofs += sizeof phdr;
        switch (phdr.p_type)
        {
            case PT_NULL:
            case PT_NOTE:
            case PT_PHDR:
            case PT_STACK:
            default:
                /* Ignore this segment. */
                break;
            case PT_DYNAMIC:
            case PT_INTERP:
            case PT_SHLIB:
                goto done;
            case PT_LOAD:
                if (validate_segment (&phdr, file))
                {
                    bool writable = (phdr.p_flags & PF_W) != 0;
                    uint32_t file_page = phdr.p_offset & ~PGMASK;
                    uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
                    uint32_t page_offset = phdr.p_vaddr & PGMASK;
                    uint32_t read_bytes, zero_bytes;
                    if (phdr.p_filesz > 0)
                    {
                        /* Normal segment.
                         Read initial part from disk and zero the rest. */
                        read_bytes = page_offset + phdr.p_filesz;
                        zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                      - read_bytes);
                    }
                    else
                    {
                        /* Entirely zero.
                         Don't read anything from disk. */
                        read_bytes = 0;
                        zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                    }
                    if (!load_segment (file, file_page, (void *) mem_page,
                                       read_bytes, zero_bytes, writable))
                        goto done;
                }
                else
                    goto done;
                break;
        }
    }
    
    /* Set up stack. */
    if (!setup_stack (esp))
        goto done;
    
    /* Start address. */
    *eip = (void (*) (void)) ehdr.e_entry;
    
    success = true;
    
done:
    /* We arrive here whether the load is successful or not. */
    
    /* On success, mark the file as unwritable and keep track of it */
    if (success)
    {
        file_deny_write(file);
        struct file_package* package = malloc(sizeof(struct file_package));
        if(package == NULL) { //LP Added
            thread_exit();
        }
        package->position = 0;
        package->fp = file;
        package->fd = t->fd_counter;
        t->fd_counter++;
        list_push_back(&t->open_files, &package->elem);
    }
    else
        file_close (file);
    
    return success;
}

/* load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);

/*
 ----------------------------------------------------------------
 Checks whether PHDR describes a valid, loadable segment in
 FILE and returns true if so, false otherwise.
 ----------------------------------------------------------------
 */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file)
{
    /* p_offset and p_vaddr must have the same page offset. */
    if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
        return false;
    
    /* p_offset must point within FILE. */
    if (phdr->p_offset > (Elf32_Off) file_length (file))
        return false;
    
    /* p_memsz must be at least as big as p_filesz. */
    if (phdr->p_memsz < phdr->p_filesz)
        return false;
    
    /* The segment must not be empty. */
    if (phdr->p_memsz == 0)
        return false;
    
    /* The virtual memory region must both start and end within the
     user address space range. */
    if (!is_user_vaddr ((void *) phdr->p_vaddr))
        return false;
    if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
        return false;
    
    /* The region cannot "wrap around" across the kernel virtual
     address space. */
    if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
        return false;
    
    /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
    if (phdr->p_vaddr < PGSIZE)
        return false;
    
    /* It's okay. */
    return true;
}

/*
 ----------------------------------------------------------------
 Loads a segment starting at offset OFS in FILE at address
 UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 memory are initialized, as follows:
 
 - READ_BYTES bytes at UPAGE must be read from FILE
 starting at offset OFS.
 
 - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 
 The pages initialized by this function must be writable by the
 user process if WRITABLE is true, read-only otherwise.
 
 Return true if successful, false if a memory allocation error
 or disk read error occurs.
 ----------------------------------------------------------------
 */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
    ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
    ASSERT (pg_ofs (upage) == 0);
    ASSERT (ofs % PGSIZE == 0);
    
    file_seek (file, ofs);
    while (read_bytes > 0 || zero_bytes > 0)
    {
        /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
        size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
        size_t page_zero_bytes = PGSIZE - page_read_bytes;
        
        //LP Project 3 addition
        create_spte_and_add_to_table(FILE_PAGE, (void*)upage, writable, false, false, file, ofs, read_bytes, zero_bytes);
        //End LP Project 3 addition
        
        
        /* Get a page of memory. */
       /* uint8_t *kpage = palloc_get_page (PAL_USER);
        if (kpage == NULL)
            return false;*/
        
        /* Load this page. */
        /*if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
        {
            palloc_free_page (kpage);
            return false;
        }
        memset (kpage + page_read_bytes, 0, page_zero_bytes); */
        
        /* Add the page to the process's address space. */
       /* if (!install_page (upage, kpage, writable))
        {
            palloc_free_page (kpage);
            return false;
        }*/
        
        
        
        /* Advance. */
        read_bytes -= page_read_bytes;
        zero_bytes -= page_zero_bytes;
        upage += PGSIZE;
    }
    return true;
}

/*
 ----------------------------------------------------------------
 Create a minimal stack by mapping a zeroed page at the top of
 user virtual memory.
 ----------------------------------------------------------------
 */
static bool
setup_stack (void **esp)
{
    void* upage = (void*)(((uint8_t *) PHYS_BASE) - PGSIZE);
    bool success = grow_stack(upage);
    if (success) {
        *esp = PHYS_BASE;
    }
    return success;
}

