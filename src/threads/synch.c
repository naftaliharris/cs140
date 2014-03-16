/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
*/

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/*
 --------------------------------------------------------------------
 Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
     decrement it.

   - up or "V": increment the value (and wake up one waiting
     thread, if any). 
 --------------------------------------------------------------------
 */
void
sema_init (struct semaphore *sema, unsigned value) 
{
  ASSERT (sema != NULL);

  sema->value = value;
  list_init (&sema->waiters);
}

/* 
 --------------------------------------------------------------------
 Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. 
 --------------------------------------------------------------------
 */
void
sema_down (struct semaphore *sema) 
{
  enum intr_level old_level;

  ASSERT (sema != NULL);
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  while (sema->value == 0) 
    {
      list_push_back (&sema->waiters, &(thread_current()->elem));
      thread_block ();
    }

  sema->value--;
  intr_set_level (old_level);
}

/* 
 --------------------------------------------------------------------
 Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. 
 --------------------------------------------------------------------
 */
bool
sema_try_down (struct semaphore *sema) 
{
  enum intr_level old_level;
  bool success;

  ASSERT (sema != NULL);

  old_level = intr_disable ();
  if (sema->value > 0) 
    {
      sema->value--;
      success = true; 
    }
  else
    success = false;
  intr_set_level (old_level);

  return success;
}

/* 
 --------------------------------------------------------------------
 Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. 
 NOTE: In order to ensure that the highest priority thread gets 
    woken up first, we call get_highest_priority_thread, which 
    removes from the sema->waiters list, and returns to use the thread
    with the highest priority. 
 NOTE: Because we are signaling the semaphore, we yield the processor
    only if we are not in an interrupt context. 
 --------------------------------------------------------------------
 */
void
sema_up (struct semaphore *sema)
{
    enum intr_level old_level;
    struct thread* thread_to_unblock;
    
    ASSERT (sema != NULL);
    
    old_level = intr_disable ();
    if (!list_empty (&sema->waiters)) {
        thread_to_unblock = get_highest_priority_thread(&sema->waiters, true);
        ASSERT(thread_to_unblock != NULL);
        thread_unblock(thread_to_unblock);
    }
    
    sema->value++;
    
    if (!intr_context()) { 
        thread_yield();
    }
    intr_set_level (old_level);
}

static void sema_test_helper (void *sema_);

/* 
 --------------------------------------------------------------------
 Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. 
 --------------------------------------------------------------------
 */
void
sema_self_test (void) 
{
  struct semaphore sema[2];
  int i;

  printf ("Testing semaphores...");
  sema_init (&sema[0], 0);
  sema_init (&sema[1], 0);
  thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
  for (i = 0; i < 10; i++) 
    {
      sema_up (&sema[0]);
      sema_down (&sema[1]);
    }
  printf ("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper (void *sema_) 
{
  struct semaphore *sema = sema_;
  int i;

  for (i = 0; i < 10; i++) 
    {
      sema_down (&sema[0]);
      sema_up (&sema[1]);
    }
}
/* 
 --------------------------------------------------------------------
 Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. 
 NOTE: because we use locks as the mode through which priority  
    donation happens, we have to set the field in the lock to 
    PRI_MIN to ensure that priority donation starts at the 
    proper baseline. 
 --------------------------------------------------------------------
 */
void
lock_init (struct lock *lock)
{
    ASSERT (lock != NULL);
    
    lock->holder = NULL;
    lock->priority = PRI_MIN;
    sema_init (&lock->semaphore, 1);
}

/* 
 --------------------------------------------------------------------
 Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. 
 NOTE: if we cannot aquire the lock and we are in regular priority
    donation scheduling, then we invoke the call to donate our
    priority. Once we move past the semaphore, we have aquired the 
    lock, and thus add it to our list of locks_held, so that this
    thread can properly recieve priortity donations. 
 --------------------------------------------------------------------
 */
void
lock_acquire (struct lock *lock)
{
    ASSERT (lock != NULL);
    ASSERT (!intr_context ());
    ASSERT (!lock_held_by_current_thread (lock));
    
    enum intr_level old_level = intr_disable();
    
    if (!thread_mlfqs) { 
        if (lock->holder != NULL) {
            thread_current()->lock_waiting_on = lock;
            //donate_priority();
        }
    }
    
    sema_down (&lock->semaphore);
    
    if (!thread_mlfqs) {
        lock->priority = PRI_MIN;
        list_push_front(&(thread_current()->locks_held), &(lock->elem));
        thread_current()->lock_waiting_on = NULL;
    }
    lock->holder = thread_current ();
    
    intr_set_level(old_level);
}

/* 
 --------------------------------------------------------------------
 Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. 
 NOTE: we add code to ensure that if the thread is successful 
    in aquiring the lock it is trying to aquire, that the lock
    is then added to the threads list of locks_held, so that the 
    system of proirity donation stays in tact. 
 --------------------------------------------------------------------
 */
bool
lock_try_acquire (struct lock *lock)
{
    bool success;
    
    ASSERT (lock != NULL);
    
    ASSERT (!lock_held_by_current_thread (lock));
    
    success = sema_try_down (&lock->semaphore);
    if (success) {
        if (!thread_mlfqs) {
            lock->priority = PRI_MIN;
            list_push_front(&(thread_current()->locks_held), &(lock->elem));
            thread_current()->lock_waiting_on = NULL;
        }
        lock->holder = thread_current ();
    }
    return success;
}

/* 
 --------------------------------------------------------------------
 Releases LOCK, which must be owned by the current thread.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. 
 NOTE: in order to properly release the lock if we are running 
    not in mlfqs mode, we have to shed priority. First, we remove
    the lock we are releasing from our list of locks held, and then
    we shed priority, which decreases our priority to the next 
    highest value in our list of locks held. If we have 
    released all locks that we are currently holding, then we will
    have our dummy lock with our original priority, and the balance
    of the thread will be restored. 
 --------------------------------------------------------------------
 */
void
lock_release (struct lock *lock)
{
    ASSERT (lock != NULL);
    ASSERT (lock_held_by_current_thread (lock));
    
    enum intr_level old_level = intr_disable();
    
    if (!thread_mlfqs) {
        list_remove(&(lock->elem)); 
        lock->priority = PRI_MIN;
        //shed_priority();
    }
    lock->holder = NULL;
    sema_up (&lock->semaphore);
    intr_set_level(old_level);
}

/*
 --------------------------------------------------------------------
 Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) 
 --------------------------------------------------------------------
 */
bool
lock_held_by_current_thread (const struct lock *lock) 
{
  ASSERT (lock != NULL);

  return lock->holder == thread_current ();
}
/* One semaphore in a list. */
struct semaphore_elem 
  {
    struct list_elem elem;              /* List element. */
    struct semaphore semaphore;         /* This semaphore. */
  };

/* 
 --------------------------------------------------------------------
 Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. 
 --------------------------------------------------------------------
 */
void
cond_init (struct condition *cond)
{
  ASSERT (cond != NULL);

  list_init (&cond->waiters);
}

/* 
 --------------------------------------------------------------------
 Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. 
 --------------------------------------------------------------------
 */
void
cond_wait (struct condition *cond, struct lock *lock) 
{
  struct semaphore_elem waiter;

  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));
  
  sema_init (&waiter.semaphore, 0);
  list_push_back (&cond->waiters, &waiter.elem);
  lock_release (lock);
  sema_down (&waiter.semaphore);
  lock_acquire (lock);
}

struct semaphore* get_semaphore_to_signal(struct condition* cond);

/*
 --------------------------------------------------------------------
 Description: traverses the web of the condition variable list. We
    go through each semphore_elem in the list, and then consult
    each semaphore->waiters list for the highest priority thread. 
    We track the sema_elem with the highest priority thread waiting
    on it, and return this. 
 --------------------------------------------------------------------
 */
struct semaphore* get_semaphore_to_signal(struct condition* cond) {
    
    struct list_elem* curr = list_head(&(cond->waiters));
    struct list_elem* tail = list_tail(&(cond->waiters));
    
    int curr_highest_priority = PRI_MIN;
    struct semaphore_elem* toSignal = NULL;
    struct semaphore_elem* currSemaElem;

    struct thread* curr_t;
    
    while (true) {
        curr = list_next(curr);
        if (curr == tail) break;
        currSemaElem = list_entry(curr, struct semaphore_elem, elem);
        ASSERT(currSemaElem != NULL);
        curr_t = get_highest_priority_thread(&(currSemaElem->semaphore.waiters),
                                             false);
        if (curr_t == NULL) {
            continue;
        }
        if (curr_t->priority > curr_highest_priority || toSignal == NULL) {
            curr_highest_priority = curr_t->priority;
            toSignal = currSemaElem;
        }
    }
    if (toSignal == NULL) {
        return NULL;
    }
    list_remove(&(toSignal->elem));
    return &(toSignal->semaphore);
}

/* 
 --------------------------------------------------------------------
 If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. 
 --------------------------------------------------------------------
 */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) 
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));

    if (!list_empty (&cond->waiters)) {
        struct semaphore* sema = get_semaphore_to_signal(cond);
        if (sema != NULL) {
             sema_up(sema);
        }
    }
}

/* 
 --------------------------------------------------------------------
 Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. 
 --------------------------------------------------------------------
 */
void
cond_broadcast (struct condition *cond, struct lock *lock) 
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);

  while (!list_empty (&cond->waiters))
    cond_signal (cond, lock);
}
