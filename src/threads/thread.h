#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <threads/synch.h>
#include <stdint.h>
#include "threads/fixed-point.h"

/* States in a thread's life cycle. */
enum thread_status
  {
    THREAD_RUNNING,     /* Running thread. */
    THREAD_READY,       /* Not running but ready to run. */
    THREAD_BLOCKED,     /* Waiting for an event to trigger. */
    THREAD_DYING        /* About to be destroyed. */
  };

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

typedef int pid_t;
#define PID_ERROR ((pid_t) -1)

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

/* A kernel thread or user process.

   Each thread structure is stored in its own 4 kB page.  The
   thread structure itself sits at the very bottom of the page
   (at offset 0).  The rest of the page is reserved for the
   thread's kernel stack, which grows downward from the top of
   the page (at offset 4 kB).  Here's an illustration:

        4 kB +---------------------------------+
             |          kernel stack           |
             |                |                |
             |                |                |
             |                V                |
             |         grows downward          |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             +---------------------------------+
             |              magic              |
             |                :                |
             |                :                |
             |               name              |
             |              status             |
        0 kB +---------------------------------+

   The upshot of this is twofold:

      1. First, `struct thread' must not be allowed to grow too
         big.  If it does, then there will not be enough room for
         the kernel stack.  Our base `struct thread' is only a
         few bytes in size.  It probably should stay well under 1
         kB.

      2. Second, kernel stacks must not be allowed to grow too
         large.  If a stack overflows, it will corrupt the thread
         state.  Thus, kernel functions should not allocate large
         structures or arrays as non-static local variables.  Use
         dynamic allocation with malloc() or palloc_get_page()
         instead.

   The first symptom of either of these problems will probably be
   an assertion failure in thread_current(), which checks that
   the `magic' member of the running thread's `struct thread' is
   set to THREAD_MAGIC.  Stack overflow will normally change this
   value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
   the run queue (thread.c), or it can be an element in a
   semaphore wait list (synch.c).  It can be used these two ways
   only because they are mutually exclusive: only a thread in the
   ready state is on the run queue, whereas only a thread in the
   blocked state is on a semaphore wait list. */
struct thread
{
    /* Owned by thread.c. */
    tid_t tid;                          /* Thread identifier. */
    enum thread_status status;          /* Thread state. */
    char name[16];                      /* Name (for debugging purposes). */
    uint8_t *stack;                     /* Saved stack pointer. */
    int priority;                       /* Priority. */
    struct list_elem allelem;           /* List element for all threads list. */
    
    //BEGIN PROJECT 1 ADDITIONS//
    /* List of locks this thread currently holds */
    /* Used for priority donationa */
    struct list locks_held;
    /* Dummy lock containing this threads original priority */
    struct lock original_priority_info;
    /* The lock this thread is waiting on. Null if not waiting */
    struct lock* lock_waiting_on;
    /* threads nice value. For bsd_scheduler */
    int nice;
    /* Thread's recent cpu. For the bsd_scheduler */
    fp_float recent_cpu;
    /* Allows this thread to be placed in a list of threads who's */
    /* recent cpu value has changed */
    struct list_elem cpu_list_elem;
    /* True if the threads recent_cpu has changed and the thread */
    /* has not been updated yet */
    bool cpu_has_changed;
    //END PROJECT 1 ADDITIONS//
    
    //PROJECT 2 ADDITION
    /* Distinguises between kernel threads and user threads */
    /* Initially set to false on thread creation, set to true in start_process */
    bool is_running_user_program;
    
    
    /* Shared between thread.c and synch.c. */
    struct list_elem elem;              /* List element. */
    
#ifdef USERPROG
    /* Owned by userprog/process.c. */
    uint32_t *pagedir;                  /* Page directory. */
    
    //BEGIN PROJECT 2 ADDITIONS//
    
    //FILE INFORMATION
    /* list of files this thread currently has open */
    /* note, this list does not require synchronization, as it is only*/
    /* accessed by the same thread */
    struct list open_files;
    /* file descriptor counter */
    int fd_counter;
    
    
    //CHILD INFORMATION
    /* synchronizes the process of creating a child*/
    struct semaphore sema_child_load;
    /* outcome of the child load. The child will set this field before signaling*/
    bool child_did_load_successfully;
    /* allows the child to set fields in the parent struct */
    struct thread* parent_thread;
    /* list of child threads spawned by this thread */
    /* note, this list does not require synchronization, as it is only*/
    /* accessed by the same thread */
    struct list child_threads;
    /* allows parent to wait on child process */
    struct semaphore wait_on_me;
    /* pointer to the threads vital_info */
    struct vital_info* vital_info;
    
    
    
    //END PROJECT 2 ADDITIONS//
#endif
    
    /* Owned by thread.c. */
    unsigned magic;                     /* Detects stack overflow. */
};

/*
 ----------------------------------------------------------------
 Description: vital_info is the information that has to stay 
    around even after a thread exits. Thus, we malloc this struct
    and free the pointer only when we no longer need this 
    information.
 ----------------------------------------------------------------
 */
struct vital_info {
    /* pointer to the thread who's vital info this is */
    struct thread* t;
    /* Records the processes exit status */
    int exit_status;
    /* used to indicate if this thread has been waited on by parent before */
    bool has_allready_been_waited_on;
    /* identifier so we can look up by tid */
    tid_t tid;
    /* Indicates if the parent thread is finished */
    bool parent_is_finished;
    /* Indicates if this thread has finished */
    bool child_is_finished;
    /* locks the vital info so only one thread can access it */
    struct lock vital_info_lock;
    /* allows this thread to be placed in the parent child_threads list */
    struct list_elem child_elem;
};

/*
 ----------------------------------------------------------------
 Description: packages open file information. Processes will
    manage a list of these file_packages, one for each open file.
 ----------------------------------------------------------------
 */
struct file_package {
    int fd; //file descriptor
    unsigned position; //this file_package's position
    struct file* fp; //file pointer
    struct list_elem elem; //so it can be placed in a list
};

/* global lock to be used for file_system access */
struct lock file_system_lock;

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

/* Performs some operation on thread t, given auxiliary data AUX. */
typedef void thread_action_func (struct thread *t, void *aux);
void thread_foreach (thread_action_func *, void *);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

/* LP Functions */
void donate_priority(void);
void shed_priority(void);
struct thread* get_highest_priority_thread(struct list*, bool should_remove);


#endif /* threads/thread.h */
