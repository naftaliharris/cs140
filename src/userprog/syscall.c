#include "devices/shutdown.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

//static void *utok (const void *uaddr);
static void syscall_handler (struct intr_frame *);

void halt (void);
void exit (int status);
pid_t exec (const char *cmd_line);
int wait (pid_t pid);
bool create (const char *file, unsigned initial_size);
bool remove (const char *file);
int open (const char *file);
int filesize (int fd);
int read (int fd, void *buffer, unsigned size);
int write (int fd, const void *buffer, unsigned size);
void seek (int fd, unsigned position);
unsigned tell (int fd);
void close (int fd);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  int syscall;
  int fd;

  syscall = *(int *)f->esp;  /* XXX Are we allowed to do this dereferencing? */

  switch (syscall)
    {
        case SYS_HALT:                   /* Halt the operating system. */
            shutdown ();
            thread_exit ();
            break;
        case SYS_EXIT:                   /* Terminate this process. */
            thread_exit ();
            break;
        case SYS_EXEC:                   /* Start another process. */
            printf ("system call %d!\n", syscall);
            thread_exit ();
            break;
        case SYS_WAIT:                   /* Wait for a child process to die. */
            printf ("system call %d!\n", syscall);
            thread_exit ();
            break;
        case SYS_CREATE:                 /* Create a file. */
            printf ("system call %d!\n", syscall);
            thread_exit ();
            break;
        case SYS_REMOVE:                 /* Delete a file. */
            printf ("system call %d!\n", syscall);
            thread_exit ();
            break;
        case SYS_OPEN:                   /* Open a file. */
            printf ("system call %d!\n", syscall);
            thread_exit ();
            break;
        case SYS_FILESIZE:               /* Obtain a file's size. */
            printf ("system call %d!\n", syscall);
            thread_exit ();
            break;
        case SYS_READ:                   /* Read from a file. */
            printf ("system call %d!\n", syscall);
            thread_exit ();
            break;
        case SYS_WRITE:                  /* Write to a file. */
            /* XXX Can we do this de-referencing? */
            fd = *(int *)(f->esp + 4);
            const void *buffer = *(const void **)(f->esp + 8);
            unsigned size = *(unsigned *)(f->esp + 12);
            printf("size: %d\n", size);
            printf("buffer: \"%s\"\n", buffer);
            write(fd, buffer, size);
            break;

        case SYS_SEEK:                   /* Change position in a file. */
        case SYS_TELL:                   /* Report current position in a file. */
        case SYS_CLOSE:                  /* Close a file. */

        default:
            printf ("system call %d!\n", syscall);
            printf ("Thread id %d\n", thread_current()->tid);
            printf ("Thread name: %s\n", thread_current()->name);
            thread_exit ();
            break;
    }
}

int write
(int fd, const void *buffer, unsigned size)
{
  if (fd == 1)
    {
      putbuf (buffer, size);
      return size;
    }
  else
    {
      printf ("No writes to files allowed yet\n");
      thread_exit ();
    }
}
