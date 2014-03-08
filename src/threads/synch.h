#ifndef THREADS_SYNCH_H
#define THREADS_SYNCH_H

#include <list.h>
#include <stdbool.h>

/* A counting semaphore. */
struct semaphore 
  {
    unsigned value;             /* Current value. */
    struct list waiters;        /* List of waiting threads. */
  };

void sema_init (struct semaphore *, unsigned value);
void sema_down (struct semaphore *);
bool sema_try_down (struct semaphore *);
void sema_up (struct semaphore *);
void sema_self_test (void);

/* Lock. */
struct lock
{
    struct thread *holder;      /* Thread holding lock (for debugging). */
    struct semaphore semaphore; /* Binary semaphore controlling access. */
    
    int priority;             /* LP for donation purposes */
    struct list_elem elem;     /* LP to allow locks to be placed in lists */
};

void lock_init (struct lock *);
void lock_acquire (struct lock *);
bool lock_try_acquire (struct lock *);
void lock_release (struct lock *);
bool lock_held_by_current_thread (const struct lock *);

/* Condition variable. */
struct condition 
  {
    struct list waiters;        /* List of waiting threads.  */
                                /* This comment in the src is deceiving */
                                /* Is really a list of semaphore_elems */
                                /* which themselves contain lists of */
                                /* waiting threads */
  };

void cond_init (struct condition *);
void cond_wait (struct condition *, struct lock *);
void cond_signal (struct condition *, struct lock *);
void cond_broadcast (struct condition *, struct lock *);

/* Read-Write Lock */
/* Uses pseudocode from:
 * http://en.wikipedia.org/wiki/Readers-writers_problem
 */
struct rw_lock
{
    struct semaphore no_waiting;
    struct semaphore no_accessing;
    struct semaphore readers_lock;
    int readers;
};

void rw_lock_init (struct rw_lock *);
void writer_acquire (struct rw_lock *);
bool writer_try_acquire (struct rw_lock *);
void writer_release (struct rw_lock *);
void reader_acquire (struct rw_lock *);
void reader_release (struct rw_lock *);

/* Optimization barrier.

   The compiler will not reorder operations across an
   optimization barrier.  See "Optimization Barriers" in the
   reference guide for more information.*/
#define barrier() asm volatile ("" : : : "memory")

#endif /* threads/synch.h */
