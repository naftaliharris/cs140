#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Stack frame for kernel_thread(). */
struct kernel_thread_frame 
  {
    void *eip;                  /* Return address. */
    thread_func *function;      /* Function to call. */
    void *aux;                  /* Auxiliary data for function. */
  };

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);

/* LP Functions */
struct lock* get_lock_holder_package(struct lock *lock);
int get_highest_priority(struct thread *t);



/* 
 Function: thread_init
 --------------------------------------------------------------------
   Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

 
   It is not safe to call thread_current() until this function
   finishes.
 --------------------------------------------------------------------
 */
void thread_init (void) 
{
  ASSERT (intr_get_level () == INTR_OFF);
    printf("in thread_init");

    /*Here we initialize the thread system, this is a good place to add any
     initialization code we think is necessary */
  lock_init (&tid_lock);
  list_init (&ready_list);
  list_init (&all_list);

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread (); /*recall that initial_thread is the thread running init.main.c*/
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();
}




/* 
 Function: thread_start
 --------------------------------------------------------------------
   Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. 
 --------------------------------------------------------------------
 */
void thread_start (void) 
{
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init (&idle_started, 0);
    printf("in thread_start");
  thread_create ("idle", PRI_MIN, idle, &idle_started);

  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&idle_started);
}






/* 
 Function: thread_tick
 --------------------------------------------------------------------
   Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context.
 --------------------------------------------------------------------
 */
void thread_tick (void) 
{
  struct thread *t = thread_current ();

  /* Update statistics. */
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;

  /* Enforce preemption. */
  if (++thread_ticks >= TIME_SLICE)
    intr_yield_on_return (); /*simply sets the boolean yield_on_return to true so 
                              that the current thread will yield the processor */
}





/* 
 --------------------------------------------------------------------
 Prints thread statistics. 
 --------------------------------------------------------------------
 */
void thread_print_stats (void) 
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_ticks, kernel_ticks, user_ticks);
}





/*
 Function: thread_create
 --------------------------------------------------------------------
   Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. 
 --------------------------------------------------------------------
 */
tid_t thread_create (const char *name, int priority, thread_func *function, void *aux) 
{
    printf("int thread_create");
    
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;

  ASSERT (function != NULL);

  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  init_thread (t, name, priority);
  tid = t->tid = allocate_tid ();

  /* Stack frame for kernel_thread(). */
  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_entry(). */
  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

  /* Add to run queue. */
  thread_unblock (t);

  return tid;
}




/* 
 Function: thread_block
 --------------------------------------------------------------------
   Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. 
 --------------------------------------------------------------------
 */
void thread_block (void) 
{
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);

  thread_current ()->status = THREAD_BLOCKED;
  schedule ();
}






/* 
 --------------------------------------------------------------------
 Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. 
 --------------------------------------------------------------------
 */
void thread_unblock (struct thread *t) 
{
  enum intr_level old_level;

  ASSERT (is_thread (t));

  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);
  list_push_back (&ready_list, &t->elem);
  t->status = THREAD_READY;
  intr_set_level (old_level);
}






/* 
 --------------------------------------------------------------------
 Returns the name of the running thread. 
 --------------------------------------------------------------------
 */
const char * thread_name (void) 
{
  return thread_current ()->name;
}






/* 
 --------------------------------------------------------------------
 Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. 
 --------------------------------------------------------------------
 */
struct thread * thread_current (void) 
{
  struct thread *t = running_thread ();
  
  /* Make sure T is really a thread.
     If either of these assertions fire, then your thread may
     have overflowed its stack.  Each thread has less than 4 kB
     of stack, so a few big automatic arrays or moderate
     recursion can cause stack overflow. */
  ASSERT (is_thread (t));
    
    if(t->status != THREAD_RUNNING) {
        printf("%s", thread_name());
        printf("%i",t->magic);
        thread_print_stats();
    }
   
  ASSERT (t->status == THREAD_RUNNING);

  return t;
}




/* 
 --------------------------------------------------------------------
 Returns the running thread's tid. 
 --------------------------------------------------------------------
 */
tid_t thread_tid (void) {
  return thread_current ()->tid;
}





/* 
 --------------------------------------------------------------------
 Deschedules the current thread and destroys it.  Never
   returns to the caller.
 --------------------------------------------------------------------
 */
void thread_exit (void) 
{
  ASSERT (!intr_context ());

#ifdef USERPROG
  process_exit ();
#endif

  /* Remove thread from all threads list, set our status to dying,
     and schedule another process.  That process will destroy us
     when it calls thread_schedule_tail(). */
  intr_disable ();
  list_remove (&thread_current()->allelem);
  thread_current ()->status = THREAD_DYING;
  schedule ();
  NOT_REACHED ();
}




/* 
 Function: thread_yield
 --------------------------------------------------------------------
   Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. 
 
 LP comment: thread_yield is for when we want to swap the current
            thread for another, and put the old thread back in the 
            ready list. thread_block is for when we want to change
            the state of the current thread to THREAD_BLOCKED
            and then run another thread. 
 
 LP comment: So it seems like the chain of calls to schedule a new 
            thread is:
            1. intr_yield_on_return
            2. intr_handler
            3. thread_yield
 --------------------------------------------------------------------
 */
void thread_yield (void) 
{
  struct thread *cur = thread_current ();
  enum intr_level old_level;
  
  ASSERT (!intr_context ()); /*This asserts we are not in an external interrupt */

  old_level = intr_disable ();
  if (cur != idle_thread) 
    list_push_back (&ready_list, &cur->elem);
  cur->status = THREAD_READY; /*for blocked threads, we would call thread_block */
  schedule ();
  intr_set_level (old_level); /*we temporarily disable interrupts so that we can swap threads, and then we 
                               restore the previous interrupt level */
}



/*
 Function: thread_foreach
 --------------------------------------------------------------------
 Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. 
 --------------------------------------------------------------------
 */
void thread_foreach (thread_action_func *func, void *aux)
{
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      func (t, aux);
    }
}




/* 
 Function: thread_set_priority
 --------------------------------------------------------------------
 Sets the current thread's priority to NEW_PRIORITY.
 
 LP: Will need to protect this function with a priority lock, so that at all times, 
 only one thread can access another thread's priority. 
 --------------------------------------------------------------------
 */
void thread_set_priority (int new_priority) 
{
  thread_current ()->priority = new_priority;
}



/* 
 --------------------------------------------------------------------
 Returns the current thread's priority. 
 
 LP: Will need to protect this function with a priority lock, so that at all times,
 only one thread can access another thread's priority.
 --------------------------------------------------------------------
 */
int thread_get_priority (void) 
{
  return thread_current ()->priority;
}




/* 
 --------------------------------------------------------------------
 Sets the current thread's nice value to NICE.
 
 LP: Will need to protect this function with a nice lock, so that at all times,
 only one thread can access another thread's priority.
 --------------------------------------------------------------------
 */
void thread_set_nice (int nice UNUSED) 
{
  /* Not yet implemented. */
}





/*
 --------------------------------------------------------------------
 Returns the current thread's nice value. 
 
 LP: Will need to protect this function with a nice lock, so that at all times,
 only one thread can access another thread's priority.
 --------------------------------------------------------------------
 */
int thread_get_nice (void) 
{
  /* Not yet implemented. */
  return 0;
}





/* 
 --------------------------------------------------------------------
 Returns 100 times the system load average. 
 
 LP: Will need to protect this function with a load_avg lock, so that at all times,
 only one thread can access another thread's priority.
 --------------------------------------------------------------------
 */
int
thread_get_load_avg (void) 
{
  /* Not yet implemented. */
  return 0;
}





/* 
 --------------------------------------------------------------------
 Returns 100 times the current thread's recent_cpu value. 
 
 LP: Will need to protect this function with a recent_cpu lock, so that at all times,
 only one thread can access another thread's priority.
 --------------------------------------------------------------------
 */
int thread_get_recent_cpu (void) 
{
  /* Not yet implemented. */
  return 0;
}



/* 
 --------------------------------------------------------------------
 Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. 
 --------------------------------------------------------------------
 */
static void idle (void *idle_started_ UNUSED) 
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  sema_up (idle_started);

  for (;;) 
    {
      /* Let someone else run. */
      intr_disable ();
      thread_block ();

      /* Re-enable interrupts and wait for the next one.

         The `sti' instruction disables interrupts until the
         completion of the next instruction, so these two
         instructions are executed atomically.  This atomicity is
         important; otherwise, an interrupt could be handled
         between re-enabling interrupts and waiting for the next
         one to occur, wasting as much as one clock tick worth of
         time.

         See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
         7.11.1 "HLT Instruction". */
      asm volatile ("sti; hlt" : : : "memory");
    }
}





/* 
 --------------------------------------------------------------------
 Function used as the basis for a kernel thread. 
 --------------------------------------------------------------------
 */
static void kernel_thread (thread_func *function, void *aux) 
{
  ASSERT (function != NULL);

  intr_enable ();       /* The scheduler runs with interrupts off. */
  function (aux);       /* Execute the thread function. */
  thread_exit ();       /* If function() returns, kill the thread. */
}




/* 
 --------------------------------------------------------------------
 Returns the running thread. 
 --------------------------------------------------------------------
 */
struct thread * running_thread (void) 
{
  uint32_t *esp;

  /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Because `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}





/*
 --------------------------------------------------------------------
 Returns true if T appears to point to a valid thread.
 --------------------------------------------------------------------
 */
static bool is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}





/* 
 --------------------------------------------------------------------
 Does basic initialization of T as a blocked thread named
   NAME. 
 --------------------------------------------------------------------
 */
static void init_thread (struct thread *t, const char *name, int priority)
{
  enum intr_level old_level;
    
    printf("in init_thread");

  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  t->priority = priority;
  t->magic = THREAD_MAGIC;
    
    t->lock_waiting_on = NULL;
    t->lock_to_sema_indicator = false;
    t->lock_to_sema_lock = NULL;
    list_init(&(t->locks_held));
    //struct lock_holder_package *package = malloc(sizeof(struct lock_holder_package));
    //package->highest_donated_priority = priority;
    //package->lockID = NULL;
    t->original_package->donated_priority = priority;
    list_push_back(&(t->locks_held), &(t->original_package->elem));

  old_level = intr_disable ();
  list_push_back (&all_list, &t->allelem);
    /*this thread is added to the ready list in the thread_unblock
     function, which is called in thread_create, which calls
     this function*/
  intr_set_level (old_level);
}





/* 
 --------------------------------------------------------------------
 Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. 
 --------------------------------------------------------------------
 */
static void * alloc_frame (struct thread *t, size_t size) 
{
  /* Stack data is always allocated in word-size units. */
  ASSERT (is_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

/*
 --------------------------------------------------------------------
 LP: this function returns the highest priority thread in the list
 and also removes it from the list.
 
 NOTE: Also removes it from the list
 --------------------------------------------------------------------
 */
struct thread* get_highest_priority_thread(struct list *list) {
    ASSERT (intr_get_level () == INTR_OFF);
    struct list_elem *curr = list_head(list);
    struct list_elem *tail = list_end(list);
    
    struct thread *currHighest = NULL;
    while (true) {
        curr = list_next(curr);
        if(curr == tail) break;
        struct thread *currThread = list_entry(curr, struct thread, elem);
        if (currHighest == NULL) {
            currHighest = currThread;
        } else if (currThread->priority > currHighest->priority) {
            currHighest = currThread;
        }
    }
    if(currHighest != NULL) {
        list_remove(&(currHighest->elem));
    }
    
    return currHighest;
}





/* 
 --------------------------------------------------------------------
 Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. 
 --------------------------------------------------------------------
 */
static struct thread * next_thread_to_run (void) 
{
  if (list_empty (&ready_list))
    return idle_thread;
  else
      return get_highest_priority_thread(&ready_list);
    //return list_entry (list_pop_front (&ready_list), struct thread, elem);
}






/* 
 --------------------------------------------------------------------
 Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.  This function is normally invoked by
   thread_schedule() as its final action before returning, but
   the first time a thread is scheduled it is called by
   switch_entry() (see switch.S).

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   After this function and its caller returns, the thread switch
   is complete. 
 --------------------------------------------------------------------
 */
void thread_schedule_tail (struct thread *prev)
{
  struct thread *cur = running_thread ();
  
  ASSERT (intr_get_level () == INTR_OFF);

  /* Mark us as running. */
  cur->status = THREAD_RUNNING;

  /* Start new time slice. */
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  process_activate ();
#endif

  /* If the thread we switched from is dying, destroy its struct
     thread.  This must happen late so that thread_exit() doesn't
     pull out the rug under itself.  (We don't free
     initial_thread because its memory was not obtained via
     palloc().) */
  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread) 
    {
      ASSERT (prev != cur);
      palloc_free_page (prev);
    }
}





/* 
 --------------------------------------------------------------------
 Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until thread_schedule_tail()
   has completed. 
 --------------------------------------------------------------------
 */
static void schedule (void) 
{
  struct thread *cur = running_thread ();
  struct thread *next = next_thread_to_run ();
    ASSERT(next != NULL);
  struct thread *prev = NULL;

  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (cur->status != THREAD_RUNNING);
  ASSERT (is_thread (next));

  if (cur != next)
    prev = switch_threads (cur, next);
  thread_schedule_tail (prev);
}





/* 
 --------------------------------------------------------------------
 Returns a tid to use for a new thread. 
 --------------------------------------------------------------------
 */
static tid_t allocate_tid (void) 
{
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire (&tid_lock);
  tid = next_tid++;
  lock_release (&tid_lock);

  return tid;
}

/*
 --------------------------------------------------------------------
 LP: this function is a helper function for the donate_priority function. 
 It returns the pointer to the lock_holder_package for the given lock
 in lock->holder. 
 
 NULL on error. 
 --------------------------------------------------------------------
 */
struct lock* get_lock_holder_package(struct lock *lock) {
    struct list_elem *curr = list_head(&(lock->holder->locks_held));
    struct list_elem *tail = list_end(&(lock->holder->locks_held));
    
    while (true) {
        curr = list_next(curr);
        if(curr == tail) break;
        struct lock *package = list_entry(curr, struct lock, elem);
        if(package == lock) return package;
    }
    return NULL;
}

/*
 --------------------------------------------------------------------
 LP: This function handles the process of donating a priority to
 another thread. The process works as follows:
 1A. disable interrupts.
 1. update donaters->lock_waiting_on field;
 2. follow the pointer to lock_to_aquire->holder. find the lock_holder_package
 for the lock_to_aquire trying to be acquired. Update the priority with the donater
 priority, and then update lock_waiting_on->holder priority if necessary 
 (given priority shceduling, will always have to update, as donated will 
 be higher.) 
 3. Check lock_to_aquire->holder->lock_waiting_on
    if null, exit from function, if non null, repeat steps 2 and 3
    where lock_to_aquire->holder is now the donater.
 
 //go to the thread with currLockToAquire
 //find the lock_package for the currLockToAquire
 //update the priority in the package with currThread->priority
 //update the priority for currLockToAquire->holder with currThread->priority
 //update currThread to currLockToAquire->holder
 //update currLockToAquire to currThread->lock_waiting_on
 
 After the current thread has donated priorty, once it blocks, the highest priority
 thread will run, which is what we want. This swap will be called by thread_block, which will execute as this thread is blocking. 
 --------------------------------------------------------------------
 */
void donate_priority(struct thread *donater, struct lock *lock_to_aquire) {
    enum intr_level old_level = intr_disable();
    
    donater->lock_waiting_on = lock_to_aquire;
    struct thread *currThread = donater;
    struct lock *currLockToAquire = lock_to_aquire;
    while (currLockToAquire != NULL) {
        struct lock *lock_package = get_lock_holder_package(currLockToAquire);
        ASSERT(lock_package != NULL);
        ASSERT(currThread->priority >= lock_package->donated_priority);
        
        lock_package->donated_priority = currThread->priority;
        
        ASSERT(currThread->priority >= currLockToAquire->holder->priority);
        
        currLockToAquire->holder->priority = currThread->priority;
        
        currThread = currLockToAquire->holder;
        
        currLockToAquire = currThread->lock_waiting_on;
        
    }
    
    intr_set_level(old_level);
}


/*
 --------------------------------------------------------------------
 LP: helper function for shed_priority. Becuase interrupts are
 disabled in shed_priority, no synchronization required. 
 
 Simply returns the highest priority value of all the lock_holder_packages
 in the t->locks_held. If t is not holding any locks, than the only
 remaining package will be the thread's initial information, ie its
 intial priority. 
 --------------------------------------------------------------------
 */
int get_highest_priority(struct thread *t) {
    struct list_elem *curr = list_head(&(t->locks_held));
    struct list_elem *tail = list_end(&(t->locks_held));
    
    int currHighest = PRI_MIN;
    while (true) {
        curr = list_next(curr);
        if(curr == tail) break;
        struct lock *lock_package = list_entry(curr, struct lock, elem);
        if(lock_package->donated_priority > currHighest) currHighest = lock_package->donated_priority;
    }
    return currHighest;
}


/*
 --------------------------------------------------------------------
 LP: This function undoes the priority donation that pertains to 
 the lock_being_released. As a consequence of priority scheduling, we
 only need to track the highest priority donated for a lock. And once 
 the lock gets released, we do not care about the other priorities that
 may have been donated previously for the lock_being_released, as the new
 thread that has aquired the lock is the highest prioty of the bunch by 
 selection. We do however, need to drop the thread who released
 lock_being_released priotiyy down to the next highest donated priority,
 or back to its oringial prioirity if it is no longer holding anymore
 locks. We also ensure that we call free on the malloc lock_holder_package
 for the lock_being_released. 
 --------------------------------------------------------------------
 */
void shed_priority(struct lock *lock_being_released) {
    enum intr_level old_level = intr_disable();
    
    struct lock *lock_package = get_lock_holder_package(lock_being_released);
    list_remove(&(lock_package->elem));
    
    int new_thread_priority = get_highest_priority(thread_current());
    thread_current()->priority = new_thread_priority;
    //lock_being_released->holder->priority = new_thread_priority;
    
    intr_set_level(old_level);
}




/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);
