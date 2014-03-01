#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/fixed-point.h"
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "devices/timer.h"
#ifdef USERPROG
#include "userprog/process.h"
#include "threads/malloc.h"
#endif
#include "vm/page.h"

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

/* LP The Load Average for the system. Used in implimentation of bsd */
static fp_float load_average;

/* LP A list of threads who's recent_cpu value has changed */
static struct list cpu_changed_list;

/* Allows us to coordinate thread updating outside of interrupt context */
static bool should_update_thread_priorities;






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

/* LP Prototypes for helper functions we impliment below */
static void update_thread_priority(struct thread* t);
static void update_load_average(void);
static void update_recent_cpu(struct thread* t, fp_float cpu_scale);
static void update_changed_cpu_threads(void);
static void update_running_thread_cpu(struct thread* t);
static void update_cpu_for_all_threads(void);

/* LP Project 2 additions */
static void init_file_system_info(struct thread* t);
static void init_child_managment_info(struct thread* t);
static void init_vital_info(struct thread* t);




/*
 --------------------------------------------------------------------
 Description: updates the threads priority according to the formula.
    Checks to ensure that priority does not exceed upper and lower
    bounds. 
 NOTE: If we were using a design of 64 queues, we would need to 
    change queues here. In order to minimize code that operates
    in the interrupt handler for timer tick, we use the single
    ready list. When priorities change, it does not affect ordering
    in the list, as we search the list for the highest priority thread
    in get_highest_priority_thread.
 --------------------------------------------------------------------
 */
static void update_thread_priority(struct thread* t) {
    int new_priority = PRI_MAX - fp_to_int(fp_div_int(t->recent_cpu, 4))
                               - (t->nice * 2);
    if (new_priority > PRI_MAX) {
        new_priority = PRI_MAX;
    }
    if (new_priority < PRI_MIN) {
        new_priority = PRI_MIN;
    }
    t->priority = new_priority;
}


/*
 --------------------------------------------------------------------
 Description: Updates the load average according to the formula in 
    the bsd handout. Ensures that num_ready_threads includes
    all ready and running threads, but not the idle thread. 
 NOTE: becaue load average is accessed inside the interrupt handler
    we cannot use locks to synchronize, as interrupt handlers cannot
    aquire locks. Thus, we disable locks externally when accessing
    load average, and inside the interupt handler, interupts are
    disabled. 
 --------------------------------------------------------------------
 */
static void update_load_average(void) {
    int num_ready_threads = list_size(&ready_list);
    if (thread_current() != idle_thread) {
        num_ready_threads++;
    }
    load_average = fp_add(fp_mul(load_average,
                                 fp_div(int_to_fp(59), int_to_fp(60))),
                          fp_mul_int(fp_div(int_to_fp(1), int_to_fp(60)),
                                     num_ready_threads));
}


/*
 --------------------------------------------------------------------
 Description: Updates the recent_cpu value for a thread according 
    to the formula in the handout. 
 NOTE: function currently not used so as to optimize. Function 
    left in place for future modifications of desired. 
 --------------------------------------------------------------------
 */
UNUSED static void update_recent_cpu(struct thread* t, fp_float cpu_scale) {
    t->recent_cpu = fp_add_int(fp_mul(cpu_scale, t->recent_cpu), t->nice);
}


/*
 --------------------------------------------------------------------
 Description: Updates CPU for all threads, called once every second.
    Uses the formula supplied in the handout to compute recent_cpu.
    Also, for each thread who's recent_cpu value has changed, we 
    add it to the list of changed cpu's. 
 NOTE: in order to ensure round robin scheduling, we always push
    to the back of the list, and then search from the front.
 NOTE: as an optimization, we only compute cpu_scale once, outside 
    of the loop through the ready list. 
 --------------------------------------------------------------------
 */
static void update_cpu_for_all_threads(void) {
    fp_float numerator = fp_mul_int(load_average, 2);
    fp_float cpu_scale = fp_div(numerator, fp_add_int(numerator, 1));
    
    struct list_elem* curr = list_head(&all_list);
    struct list_elem* tail = list_tail(&all_list);
    while (true) {
        curr = list_next(curr);
        if (curr == tail) break;
        struct thread* t = list_entry(curr, struct thread, allelem);
        t->recent_cpu = fp_add_int(fp_mul(cpu_scale, t->recent_cpu), t->nice);
        if (t->cpu_has_changed == false) {
            t->cpu_has_changed = true;
            list_push_back(&cpu_changed_list, &t->cpu_list_elem);
        }
    }
}


/*
 --------------------------------------------------------------------
 Description: updates the priorities for the threads whos cpu changed
    As a coordination mechanism, also sets cpu_changed to false, 
    as the thread is no longer in the list of cpu_changed. 
 NOTE: the reason to have this boolean cpu_changed is to ensure
    that when a thread exits, it is not still contained in the list
    of cpu_changed. If it is, will generate a page fault when 
    the update function is called on this list of changed cpu's. 
 --------------------------------------------------------------------
 */
static void update_changed_cpu_threads(void) {
    struct list_elem* curr = list_head(&cpu_changed_list);
    struct list_elem* tail = list_tail(&cpu_changed_list);
    while (true) {
        curr = list_next(curr);
        if (curr == tail) break;
        struct thread* t = list_entry(curr, struct thread, cpu_list_elem);
        update_thread_priority(t);
        list_remove(&t->cpu_list_elem);
        t->cpu_has_changed = false;
    }
}

/*
 --------------------------------------------------------------------
 Description: Incriments the running threads recent cpu by 1. 
    Also, ensures to add it to the list of updated cpu's if it has
    not been already. 
 NOTE: we need the conditional boolean check, because you can imagine
    the case where a thread runs for miltiple ticks within the time-slice
    and in each, its cpu is getting incrimented by one, yet we only 
    add it to the cpu's changed list once. 
 --------------------------------------------------------------------
 */
UNUSED static void update_running_thread_cpu(struct thread* t) {
    t->recent_cpu = fp_add_int(t->recent_cpu, 1);
    if (t->cpu_has_changed == false) {
        t->cpu_has_changed = true;
        list_push_back(&cpu_changed_list, &t->cpu_list_elem);
    }
}

/* 
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
    
    /*Here we initialize the thread system. */
    /*We do any thread_system init here */
    lock_init (&tid_lock);
    lock_init(&file_system_lock);
    list_init (&ready_list);
    list_init (&all_list);
    list_init (&cpu_changed_list);
    load_average = 0;
    
    /* Set up a thread structure for the running thread. */
    initial_thread = running_thread (); 
    init_thread (initial_thread, "main", PRI_DEFAULT);
    initial_thread->status = THREAD_RUNNING;
    initial_thread->tid = allocate_tid ();
}


/* 
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
    init_vital_info(initial_thread);
    thread_create ("idle", PRI_MIN, idle, &idle_started);
    
    /* Start preemptive thread scheduling. */
    intr_enable ();
    
    /* Wait for the idle thread to initialize idle_thread. */
    sema_down (&idle_started);
}


/*
 --------------------------------------------------------------------
 Description: Called by the timer interrupt handler at each timer tick.
    Thus, this function runs in an external interrupt context.
 NOTE: if we are running the idle thread, we yield as we want to get
    it off the cpu as fast as possible if we have another thread ready. 
 NOTE: we have a conditional check on the state of the thread_mlfqs 
    flag to ensure that we allow both types of scheduling. 
 NOTE: we adhere to the intervals specified in the handout as to when
    to update the load average,recent_cpu, and thread_priority. 
 NOTE: this function is long. This is intential, as we minimize
    the number of function calls made by pushing as much code
    into this function as possible, and thus minimize the time
    spent in the timer interrupt handler. 
 --------------------------------------------------------------------
 */
void thread_tick (void)
{
    struct thread *t = thread_current ();
    int64_t ticks = timer_ticks();
    /* Update statistics. */
    if (t == idle_thread) {
        idle_ticks++;
        intr_yield_on_return();
    }
#ifdef USERPROG
    else if (t->pagedir != NULL)
        user_ticks++;
#endif
    else
        kernel_ticks++;
    if (thread_mlfqs) {
        if (t != idle_thread) {
            t->recent_cpu = fp_add_int(t->recent_cpu, 1);
            if (t->cpu_has_changed == false) {
                t->cpu_has_changed = true;
                list_push_back(&cpu_changed_list, &t->cpu_list_elem);
            }
        }
        if (ticks % TIMER_FREQ == 0) {
            update_load_average();
            update_cpu_for_all_threads();
        }
        if (ticks % 4) {
            should_update_thread_priorities = true;
        }
    }
    /* Enforce preemption. */
    if (++thread_ticks >= TIME_SLICE) {
        intr_yield_on_return ();
    }
}


/* 
 --------------------------------------------------------------------
 Description: Prints thread statistics. 
 --------------------------------------------------------------------
 */
void thread_print_stats (void) 
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_ticks, kernel_ticks, user_ticks);
}


/*
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
 NOTE: we did not modify this function. 
 --------------------------------------------------------------------
 */
tid_t thread_create (const char *name, int priority,
                     thread_func *function, void *aux)
{
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
    init_vital_info(t);

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
 --------------------------------------------------------------------
   Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. 
 
 NOTE: we did not modify this function. 
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
 
 NOTE: in this function, we yield the current thread only if two 
    conditions hold. First, the context in which this function 
    is called must be with interrupts enabled. Second, the priority
    of the unblocked thread must exceed that of the currently running
    thread. 
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
    if (old_level == INTR_ON && t->priority > thread_current()->priority) {
        thread_yield();
    }
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
 NOTE: we modify this function only for the operation of the 
    bsd_scheduler. We add logic to ensure that if the exiting
    thread currently has an list_element in the cpu_changed
    list, we remove that list_elem. If we do not doe this, 
    we will allow ourselves to be susceptibel to a page_fault
    in the update_cpus_changed, as the surrounding thread
    struct is removed after this function call. 
 NOTE: as an optimization, we store thread_current in a local 
    variable so as to avoid making repeated calls to thread_current().
    This is a good idea because we want ouy system code to run as fast 
    as possible. 
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
    struct thread* curr = thread_current();
    ASSERT(curr != NULL);
    list_remove (&curr->allelem);
    if (thread_mlfqs) {
        if (curr->cpu_has_changed) {
            list_remove(&curr->cpu_list_elem);
        }
    }
    curr->status = THREAD_DYING;
    schedule ();
    NOT_REACHED ();
}




/* 
 --------------------------------------------------------------------
   Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. 
 NOTE: we do not need to add logic to allow for both schedulers
    on the thread_mlfqs flag, as each uses the same ready_list. 
 --------------------------------------------------------------------
 */
void thread_yield (void)
{
    enum intr_level old_level;
    
    ASSERT (!intr_context ());
    old_level = intr_disable ();
    struct thread *cur = thread_current ();
    if (cur != idle_thread)
        list_push_back (&ready_list, &cur->elem);
    cur->status = THREAD_READY;
    schedule ();
    intr_set_level (old_level);
}

/*
 --------------------------------------------------------------------
 Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. 
 NOTE: we do not use this function, and we do not modify it. 
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
 --------------------------------------------------------------------
 Descritpion: Sets the current thread's priority to NEW_PRIORITY.
 NOTE: we do not call this function in the bsd_scheduler,
    thus we assert that we are not running that scheduler. 
 NOTE: if we are in an interrupt context, we do not want to yield
    as the interrupt handler cannot yield. If we not in an interrupt
    context, we do yeild, as it is possible that the shedding
    of prriority caused another thread to have a higher priority 
    than us. Note, we allow thread yield to take care of checking 
    priority, and do not do it here. 
 --------------------------------------------------------------------
 */
void thread_set_priority (int new_priority) 
{
    ASSERT(!thread_mlfqs);
    if (intr_context()) {
        thread_current ()->original_priority_info.priority = new_priority;
        shed_priority();
    } else {
        enum intr_level old_level = intr_disable();
        thread_current ()->original_priority_info.priority = new_priority;
        shed_priority();
        thread_yield();
        intr_set_level(old_level);
    }
}

/* 
 --------------------------------------------------------------------
 Returns the current thread's priority. 
 --------------------------------------------------------------------
 */
int thread_get_priority (void)
{
    return thread_current()->priority;
}

/* 
 --------------------------------------------------------------------
 Description: Sets the current thread's nice value to NICE. 
 NOTE: because the nice value of a thread is accessed in the 
    interrupt handler to a timer interrupt, we cannot use
    locks, sempahores, or condition variables, because interrupt
    handlers cannot aquire these. Therefore, in order to ensure
    proper synchronization, we disable interrupts. 
 NOTE: we take precautions against the user passing in a nice
    value that exceeds the bounds of nice values. We could 
    use an assert, but that would end program with a bad nice
    value passed in. We allow for program to continue to run
    and enforce our restrictions ourselves. 
 NOTE: when we update the nice value of a thread, it will affect
    its priority. Thus as specified in the handout, we recompute
    thread priority here. If we are not in an interrupt context, and
    the old priority of the thread is higher than the new priority,
    then we yield the current thread to ensure that the highest priority
    thread is running at all times. 
 --------------------------------------------------------------------
 */
#define MAX_NICE 20
#define MIN_NICE -20
void thread_set_nice (int nice UNUSED) 
{
    enum intr_level old_level = intr_disable();
    struct thread* curr = thread_current();
    if (nice > MAX_NICE) {
        nice = MAX_NICE;
    } else if (nice < MIN_NICE) {
        nice = MIN_NICE;
    }
    curr->nice = nice;
    int old_priority = curr->priority;
    update_thread_priority(curr);
    if (!intr_context() && curr->priority < old_priority) {
        thread_yield();
    }

    intr_set_level(old_level);
}

/*
 --------------------------------------------------------------------
 Returns the current thread's nice value. 
 --------------------------------------------------------------------
 */
int thread_get_nice (void)
{
    return thread_current()->nice;
}

/* 
 --------------------------------------------------------------------
 Returns 100 times the system load average. 
 --------------------------------------------------------------------
 */
int
thread_get_load_avg (void)
{
    return fp_to_int (fp_mul_int (load_average, 100));
    
}

/* 
 --------------------------------------------------------------------
 Returns 100 times the current thread's recent_cpu value. 
 --------------------------------------------------------------------
 */
int thread_get_recent_cpu (void) 
{
    return fp_to_int (fp_mul_int (thread_current()->recent_cpu, 100));
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
   special case when the ready list is empty. \
 
 NOTE: we did not modify this function. 
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
 Description: Does basic initialization of T as a blocked thread named
   NAME. 
 NOTE: we do seperat initialization for threads depending on which 
    scheduler we are running. 
 NOTE: Again, this function is long, but it is long because of the 
    conditional initiliazation between the two scheudlers. In order
    to optimize for speed, we push as much code into this function
    to avoid function calls. 
 --------------------------------------------------------------------
 */
static void init_thread (struct thread *t, const char *name, int priority)
{
    enum intr_level old_level;
    
    ASSERT (t != NULL);
    ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
    ASSERT (name != NULL);
    
    memset (t, 0, sizeof *t);
    t->status = THREAD_BLOCKED;
    strlcpy (t->name, name, sizeof t->name);
    t->stack = (uint8_t *) t + PGSIZE;
    t->priority = priority;
    t->magic = THREAD_MAGIC;
    
    t->is_running_user_program = false;
    
    if (thread_mlfqs) {
        if (t == initial_thread) { /* special inittial_thread init */
            t->nice = 0;
            t->recent_cpu = 0;
        } else {
            struct thread* curr = thread_current(); /* parent thread */
            t->nice = curr->nice;
            t->recent_cpu = curr->recent_cpu;
        }
        update_thread_priority(t);
    } else {
        list_init(&(t->locks_held));
        t->original_priority_info.priority = priority;
        t->original_priority_info.holder = NULL; /* orig priority package */
        list_push_front(&(t->locks_held), &(t->original_priority_info.elem));
        t->lock_waiting_on = NULL;
    }
    
    //PROJECT 2 ADDIDITION
    init_file_system_info(t);
    init_child_managment_info(t);
    
    old_level = intr_disable ();
    list_push_back (&all_list, &t->allelem);
    intr_set_level (old_level);
}

/*
 --------------------------------------------------------------------
 Description: initiliazes the file system info for the thread t
 NOTE: we set fd_counter to 2 to account for STD_IN, STD_OUT, 
 NOTE: I am not sure if we have to account for std_error, 
     the lecture slides include it, BUT THE ASSINGMENT SPEC DOES
    NOT! Therefore, we will not include it either. 
 --------------------------------------------------------------------
 */
static void init_file_system_info(struct thread* t) {
    list_init(&t->open_files);
    t->fd_counter = 2;
}

/*
 --------------------------------------------------------------------
 Description: mallocs a pointer for the vital info struct 
    and then sets the data and then updates t to point
    to the data. 
 NOTE: we only add the vital_info to a child_list if t is not the 
    initial thread. 
 --------------------------------------------------------------------
 */
static void init_vital_info(struct thread* t) {
    struct vital_info* vital_info = malloc(sizeof(struct vital_info));
    if (vital_info == NULL) {
        thread_exit();
    }
    vital_info->t = t;
    vital_info->exit_status = 0;
    vital_info->has_allready_been_waited_on = false;
    vital_info->tid = t->tid;
    vital_info->parent_is_finished = false;
    vital_info->child_is_finished = false;
    lock_init(&vital_info->vital_info_lock);
    t->vital_info = vital_info;
    if (t != initial_thread) {
        list_push_back(&t->parent_thread->child_threads, &vital_info->child_elem);
    }
}

/*
 --------------------------------------------------------------------
 Description: intiliazes the variables used to manange the process
    of creating, tracking, and communicating with child processes.
 NOTE: we must take special precation when handling the initial thread
    as the initial thread will never be a child of a parent threads, 
    and the initial thread does not have a parent thread. 
 --------------------------------------------------------------------
 */
static void init_child_managment_info(struct thread* t) {
    sema_init(&t->sema_child_load, 0);
    sema_init(&t->wait_on_me, 0);
    list_init(&t->child_threads);
    
    if (t == initial_thread) {
        t->parent_thread = NULL;
    } else {
        t->parent_thread = thread_current();
    }
    
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
 Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. 
 NOTE: In order to keep the code clean and simple, we used the same
    ready list for both priority donation and the bsd_scheduler. 
 NOTE: There are several methods of keeping this list. One way to do
    it is to order the list at all times, so that list_pop_front
    always returns the highest priority thread. The advantage to 
    this approach is that it is very fast to remove elements. The 
    downside is that the whenver thread priorities change or when 
    a thread gets added to the list, it has to be updated, and 
    re-ordered. The second method of keeping the list is to leave it
    in any state, and then search for the next thread to run when
    necessary. The advantage of this approach is that list insertion
    and priority re-ordering is hastle free and very fast. The downside
    is that getting the next thread requires a search. 
 --------------------------------------------------------------------
 */
static struct thread * next_thread_to_run (void) 
{
    
    if (list_empty (&ready_list)) {
        return idle_thread;
    } else {
        struct thread* next_thread = get_highest_priority_thread(&ready_list,
                                                                 true);
        ASSERT(next_thread != NULL);
        return next_thread;
    }
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
  if (cur->pagedir != NULL)
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
    if (thread_mlfqs) {
        if (should_update_thread_priorities) {
            should_update_thread_priorities = false;
            update_changed_cpu_threads();
        }
    }
    struct thread *cur = running_thread ();
    struct thread *next = next_thread_to_run ();
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
 LP Description: Donate priority function. A thread the is aquiring
    a locked lock will donate its priority to the lock-holder. 
    Then, this donated priority will propogate up to the thread that
    is not locked by following the struct lock* lock_waiting_on
    field within each struct. If this field is NULL, we can end 
    priority donation
 Note, because this function is only
    called from sema_down, and sema_down disables interrupts, we do not
    have to do it here as well. 
 NOTE: As mentioned in the design doc, we cannot use a lock or a 
    semaphore here, because we do not know if a thread's priority
    field will be accessed by an interrupt handler, and interrupt
    handlers cannot aquire locks/semaphores...Thus, our only
    concurreny option is to disable interrupts, which happens
    in the lock_aquire, which is the only placed this function is 
    called. 
 --------------------------------------------------------------------
 */
void donate_priority(void) {
    ASSERT(intr_get_level() == INTR_OFF);
    struct thread* currThread = thread_current();
    while (currThread->lock_waiting_on != NULL) {
        struct lock* currLock = currThread->lock_waiting_on;
        if (currThread->priority > currLock->priority) {
            currLock->priority = currThread->priority;
        }
        if (currThread == NULL || currLock == NULL) {
            printf("cur thread null");
        }
        if (currThread->priority > currLock->holder->priority) {
            currLock->holder->priority = currThread->priority;
        }
        currThread = currThread->lock_waiting_on->holder;
    }
}

/*
 --------------------------------------------------------------------
 LP Description: Final steps to release a lock and remove any donated
    priority associated with the lock. 
 NOTE: this function is only called from lock_release, which disables
    interrupts as required, for the same reasons mentioned in the 
    comment above. 
 NOTE: by design, at thread creation, each thread is given a dummy
    lock, which holds the thread's original priority. Thus dummy
    lock is always an element of the threads locks_held list, and thus
    will restore the thread back to its oringinal priority once
    the thread has released all of its locks that it is holding. 
 --------------------------------------------------------------------
 */
void shed_priority() {
    ASSERT(intr_get_level() == INTR_OFF);
    struct thread* curr_t = thread_current();
    struct list_elem* curr = list_head(&(curr_t->locks_held));
    struct list_elem* tail = list_tail(&(curr_t->locks_held));
    int highest_remaining_priority = PRI_MIN;
    while (true) {
        curr = list_next(curr);
        if(curr == tail) break;
        struct lock* currLock = list_entry(curr, struct lock, elem);
        if (currLock->priority > highest_remaining_priority) {
            highest_remaining_priority = currLock->priority;
        }
    }
    curr_t->priority = highest_remaining_priority;
}

/*
 --------------------------------------------------------------------
 LP: finds the highest priority thread in the list, removes it from the
    list if it should, and then returns a pointer to the thread.
 NOTE: this is the worker for our scheudler system. Given the random
    order in which the list of threads could be in, this function
    ensures that the highest_priority_thread is always returned. 
 NOTE: because the list passed in to this function might be accessed
    by an interrupt handler, we cannot use locks and sempahores to 
    prevent concurrency issues. Thus, disabling interrupts is the only
    option for this function. 
 --------------------------------------------------------------------
 */
struct thread* get_highest_priority_thread(struct list* list,
                                           bool should_remove)
{
    enum intr_level old_level = intr_disable();
    struct list_elem* curr = list_head(list);
    struct list_elem* tail = list_tail(list);
    struct thread* currHighest = NULL;
    while (true) {
        curr = list_next(curr);
        if (curr == tail) break;
        struct thread* currThread = list_entry(curr, struct thread, elem);
        if (currHighest == NULL) currHighest = currThread;
        if (currThread->priority > currHighest->priority) {
            currHighest = currThread;
        }
        if (currHighest->priority == PRI_MAX) break;
    }
    if (currHighest != NULL && should_remove) list_remove(&(currHighest->elem));
    intr_set_level(old_level);
    return currHighest;
}


/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);
