#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

static void syscall_handler (struct intr_frame *);
// Begin LP Defined functions //
void check_usr_ptr(void* u_ptr);
uint32_t read_frame(struct intr_frame* f, int byteOffset);
// END LP Defined functions   //

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/*
 --------------------------------------------------------------------
 Description: reads the system call number from f->eip and dispatches
    to the correct system call handler.
 --------------------------------------------------------------------
 */
static void
syscall_handler (struct intr_frame *f UNUSED) 
{
    int systemCall_num = (int)read_frame(f, 0);
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

