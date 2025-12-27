/*
 * ╔══════════════════════════════════════════════════════════════════════════╗
 * ║                      THREAD IMPLEMENTATION                                ║
 * ╠══════════════════════════════════════════════════════════════════════════╣
 * ║                                                                          ║
 * ║  This file implements the core threading system for PintOS, including    ║
 * ║  thread creation, scheduling, blocking/unblocking, and termination.      ║
 * ║                                                                          ║
 * ║  ARCHITECTURE OVERVIEW:                                                  ║
 * ║  ──────────────────────                                                  ║
 * ║                                                                          ║
 * ║    ┌──────────────────────────────────────────────────────────────────┐  ║
 * ║    │                    SCHEDULER SUBSYSTEM                           │  ║
 * ║    │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌──────────┐ │  ║
 * ║    │  │ FIFO Sched  │  │ PRIO Sched  │  │ FAIR Sched  │  │  MLFQS   │ │  ║
 * ║    │  │ (simple RR) │  │ (donation)  │  │ (prop.share)│  │ (BSD)    │ │  ║
 * ║    │  └──────┬──────┘  └──────┬──────┘  └──────┬──────┘  └────┬─────┘ │  ║
 * ║    │         └────────────────┴────────────────┴──────────────┘       │  ║
 * ║    │                               │                                   │  ║
 * ║    │                    next_thread_to_run()                          │  ║
 * ║    │                    (dispatcher via jump table)                   │  ║
 * ║    └──────────────────────────────────┬───────────────────────────────┘  ║
 * ║                                       │                                  ║
 * ║    ┌──────────────────────────────────▼───────────────────────────────┐  ║
 * ║    │                      READY QUEUES                                │  ║
 * ║    │  • fifo_ready_list     - FIFO scheduler queue                    │  ║
 * ║    │  • priority_ready_list - PRIO/MLFQS queue (sorted by priority)   │  ║
 * ║    │  • fair_ready_list     - Fair scheduler queue                    │  ║
 * ║    └──────────────────────────────────────────────────────────────────┘  ║
 * ║                                                                          ║
 * ║  CONTEXT SWITCH FLOW:                                                    ║
 * ║  ────────────────────                                                    ║
 * ║                                                                          ║
 * ║    thread_yield() / thread_block() / thread_exit()                       ║
 * ║           │                                                              ║
 * ║           ▼                                                              ║
 * ║    schedule()                                                            ║
 * ║           │                                                              ║
 * ║           ├─► next_thread_to_run()  ──► Pick next thread                 ║
 * ║           │                                                              ║
 * ║           ├─► switch_threads()      ──► Save/restore registers           ║
 * ║           │   (assembly in switch.S)                                     ║
 * ║           │                                                              ║
 * ║           └─► thread_switch_tail()  ──► Activate page tables,            ║
 * ║                                         free dying threads               ║
 * ║                                                                          ║
 * ║  INTERRUPT CONTEXT:                                                      ║
 * ║  ──────────────────                                                      ║
 * ║  • Timer interrupt calls thread_tick() every tick                        ║
 * ║  • After TIME_SLICE ticks, sets yield_on_return flag                     ║
 * ║  • thread_yield() is called after interrupt handler returns              ║
 * ║  • MLFQS updates: every tick (recent_cpu), every 4 ticks (priorities),   ║
 * ║    every second (load_avg, recent_cpu recalculation)                     ║
 * ║                                                                          ║
 * ╚══════════════════════════════════════════════════════════════════════════╝
 */

#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/fixed-point.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* ═══════════════════════════════════════════════════════════════════════════
 * CONSTANTS AND MAGIC NUMBERS
 * ═══════════════════════════════════════════════════════════════════════════*/

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow. If this value is corrupted,
   the kernel stack has overflowed into the thread struct. */
#define THREAD_MAGIC 0xcd6abf4b

/* ═══════════════════════════════════════════════════════════════════════════
 * READY QUEUES
 * ─────────────────────────────────────────────────────────────────────────────
 * Each scheduling policy uses a different ready list. This avoids mixing
 * threads from different policies and allows each scheduler to organize
 * its queue optimally (e.g., PRIO keeps threads sorted by priority).
 * ═══════════════════════════════════════════════════════════════════════════*/

/* FIFO scheduler ready queue (simple round-robin order). */
static struct list fifo_ready_list;

/* Priority-based ready queue (SCHED_PRIO and SCHED_MLFQS).
   Kept sorted with highest priority at front. */
static struct list priority_ready_list;

/* Fair scheduler ready queue (stride, lottery, CFS, EEVDF).
   Organization depends on the active fair scheduler. */
static struct list fair_ready_list;

/* ═══════════════════════════════════════════════════════════════════════════
 * GLOBAL THREAD LISTS
 * ═══════════════════════════════════════════════════════════════════════════*/

/* List of ALL threads (all states). Used for:
   • Iterating over all threads (thread_foreach)
   • MLFQS priority recalculation
   Threads are added on creation and removed on exit. */
static struct list all_list;

/* ═══════════════════════════════════════════════════════════════════════════
 * SPECIAL THREADS
 * ═══════════════════════════════════════════════════════════════════════════*/

/* The idle thread - runs when no other thread is ready.
   Never appears in ready lists; returned by next_thread_to_run()
   when all ready lists are empty. */
static struct thread* idle_thread;

/* The initial thread - first thread, runs init.c:main().
   Special because its memory wasn't allocated via palloc. */
static struct thread* initial_thread;

/* ═══════════════════════════════════════════════════════════════════════════
 * SYNCHRONIZATION
 * ═══════════════════════════════════════════════════════════════════════════*/

/* Lock protecting tid allocation (ensures unique tids). */
static struct lock tid_lock;

/* ═══════════════════════════════════════════════════════════════════════════
 * KERNEL THREAD BOOTSTRAP FRAME
 * ═══════════════════════════════════════════════════════════════════════════
 * When a new kernel thread starts, it needs this stack frame to know
 * which function to call. The frame is set up by thread_create().
 * ═══════════════════════════════════════════════════════════════════════════*/

struct kernel_thread_frame {
  void* eip;             /* Return address (NULL, since kernel_thread doesn't return). */
  thread_func* function; /* Function to call in the new thread. */
  void* aux;             /* Auxiliary data passed to the function. */
};

/* ═══════════════════════════════════════════════════════════════════════════
 * CPU TIME STATISTICS
 * ═══════════════════════════════════════════════════════════════════════════*/

static long long idle_ticks;   /* Timer ticks spent running idle thread. */
static long long kernel_ticks; /* Timer ticks spent in kernel threads. */
static long long user_ticks;   /* Timer ticks spent in user programs. */

/* ═══════════════════════════════════════════════════════════════════════════
 * MLFQS GLOBAL STATE
 * ─────────────────────────────────────────────────────────────────────────────
 * The Multi-Level Feedback Queue Scheduler maintains system-wide state:
 * • load_avg: Exponential moving average of ready thread count
 * 
 * Formula: load_avg = (59/60)*load_avg + (1/60)*ready_threads
 * Updated once per second (every TIMER_FREQ ticks).
 * ═══════════════════════════════════════════════════════════════════════════*/

static int load_avg; /* System load average (17.14 fixed-point format). */

/* ═══════════════════════════════════════════════════════════════════════════
 * TIME SLICE MANAGEMENT
 * ═══════════════════════════════════════════════════════════════════════════*/

#define TIME_SLICE 4          /* Timer ticks per thread time slice. */
static unsigned thread_ticks; /* Ticks since current thread started running. */

/* ═══════════════════════════════════════════════════════════════════════════
 * FORWARD DECLARATIONS
 * ═══════════════════════════════════════════════════════════════════════════*/

/* Thread initialization and utilities. */
static void init_thread(struct thread*, const char* name, int priority);
static bool is_thread(struct thread*) UNUSED;
static void* alloc_frame(struct thread*, size_t size);
static tid_t allocate_tid(void);

/* Core scheduling. */
static void schedule(void);
static void thread_enqueue(struct thread* t);
void thread_switch_tail(struct thread* prev);

/* Thread entry points. */
static void kernel_thread(thread_func*, void* aux);
static void idle(void* aux UNUSED);
static struct thread* running_thread(void);

/* Scheduler implementations (one per policy). */
static struct thread* next_thread_to_run(void);
static struct thread* thread_schedule_fifo(void);
static struct thread* thread_schedule_prio(void);
static struct thread* thread_schedule_fair(void);
static struct thread* thread_schedule_mlfqs(void);
static struct thread* thread_schedule_reserved(void);

/* MLFQS helper functions (called from timer interrupt). */
static void mlfqs_update_priority(struct thread* t);
static void mlfqs_update_recent_cpu(struct thread* t);
static void mlfqs_update_load_avg(void);

/* ═══════════════════════════════════════════════════════════════════════════
 * SCHEDULER CONFIGURATION
 * ─────────────────────────────────────────────────────────────────────────────
 * The active scheduling policy is set at boot time via command-line options.
 * This allows testing different schedulers without recompiling.
 *
 * Command-line options:
 *   -sched=fifo    First-in, first-out (default)
 *   -sched=prio    Strict priority with donation
 *   -sched=fair    Proportional share scheduling
 *   -sched=mlfqs   Multi-level feedback queue
 *
 * For fair scheduling, additional options select the implementation:
 *   -fair=stride   Stride scheduling (default)
 *   -fair=lottery  Lottery scheduling
 *   -fair=cfs      Completely Fair Scheduler
 *   -fair=eevdf    Earliest Eligible Virtual Deadline First
 * ═══════════════════════════════════════════════════════════════════════════*/

/* Currently active scheduling policy. */
enum sched_policy active_sched_policy;

/* Currently active fair scheduler implementation. */
enum fair_sched_type active_fair_sched_type;

/* ═══════════════════════════════════════════════════════════════════════════
 * SCHEDULER DISPATCH TABLE
 * ─────────────────────────────────────────────────────────────────────────────
 * Jump table for O(1) dispatch to the active scheduler.
 * Indexed by enum sched_policy value.
 * ═══════════════════════════════════════════════════════════════════════════*/

typedef struct thread* scheduler_func(void);

scheduler_func* scheduler_jump_table[8] = {
    thread_schedule_fifo,     /* SCHED_FIFO = 0 */
    thread_schedule_prio,     /* SCHED_PRIO = 1 */
    thread_schedule_fair,     /* SCHED_FAIR = 2 */
    thread_schedule_mlfqs,    /* SCHED_MLFQS = 3 */
    thread_schedule_reserved, /* Reserved slots for future policies */
    thread_schedule_reserved, thread_schedule_reserved, thread_schedule_reserved};

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void thread_init(void) {
  ASSERT(intr_get_level() == INTR_OFF);

  lock_init(&tid_lock);
  list_init(&fifo_ready_list);
  list_init(&priority_ready_list);
  list_init(&fair_ready_list);
  list_init(&all_list);

  /* Initialize MLFQS global state */
  load_avg = 0; /* Fixed-point 0 */

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread();
  init_thread(initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void thread_start(void) {
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init(&idle_started, 0);
  thread_create("idle", PRI_MIN, idle, &idle_started);

  /* Start preemptive thread scheduling. */
  intr_enable();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down(&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void thread_tick(void) {
  struct thread* t = thread_current();

  /* Update statistics. */
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pcb != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;

  /* Enforce preemption. */
  if (++thread_ticks >= TIME_SLICE)
    intr_yield_on_return();
}

/* Prints thread statistics. */
void thread_print_stats(void) {
  printf("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n", idle_ticks, kernel_ticks,
         user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
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
   Priority scheduling is the goal of Problem 1-3. */
tid_t thread_create(const char* name, int priority, thread_func* function, void* aux) {
  struct thread* t;
  struct kernel_thread_frame* kf;
  struct switch_entry_frame* ef;
  struct switch_threads_frame* sf;
  tid_t tid;

  ASSERT(function != NULL);

  /* Allocate thread. */
  t = palloc_get_page(PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  init_thread(t, name, priority);
  tid = t->tid = allocate_tid();

  /* Stack frame for kernel_thread(). */
  kf = alloc_frame(t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_entry(). */
  ef = alloc_frame(t, sizeof *ef);
  ef->eip = (void (*)(void))kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame(t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

  /* Add to run queue. */
  thread_unblock(t);

  /* Yield if new thread has higher priority than us */
  if (t->eff_priority > thread_current()->eff_priority)
    thread_yield();

  return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void thread_block(void) {
  ASSERT(!intr_context());
  ASSERT(intr_get_level() == INTR_OFF);

  thread_current()->status = THREAD_BLOCKED;
  schedule();
}

/* a < b : for list_max to find highest, list_insert_ordered for ascending */
bool thread_priority_less(const struct list_elem* a, const struct list_elem* b, void* aux UNUSED) {
  struct thread* ta = list_entry(a, struct thread, elem);
  struct thread* tb = list_entry(b, struct thread, elem);
  return ta->eff_priority < tb->eff_priority;
}

/* a > b : for list_insert_ordered to keep highest at front */
bool thread_priority_greater(const struct list_elem* a, const struct list_elem* b,
                             void* aux UNUSED) {
  struct thread* ta = list_entry(a, struct thread, elem);
  struct thread* tb = list_entry(b, struct thread, elem);
  return ta->eff_priority > tb->eff_priority;
}

/* Places a thread on the ready structure appropriate for the
   current active scheduling policy.
   
   This function must be called with interrupts turned off. */
static void thread_enqueue(struct thread* t) {
  ASSERT(intr_get_level() == INTR_OFF);
  ASSERT(is_thread(t));

  if (active_sched_policy == SCHED_FIFO)
    list_push_back(&fifo_ready_list, &t->elem);
  else if (active_sched_policy == SCHED_PRIO)
    list_insert_ordered(&priority_ready_list, &t->elem, thread_priority_greater, NULL);
  else if (active_sched_policy == SCHED_FAIR)
    list_push_back(&fair_ready_list, &t->elem); /* Fair schedulers will sort/select appropriately */
  else if (active_sched_policy == SCHED_MLFQS)
    list_insert_ordered(&priority_ready_list, &t->elem, thread_priority_greater, NULL);
  else
    PANIC("Unknown scheduling policy value: %d", active_sched_policy);
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void thread_unblock(struct thread* t) {
  enum intr_level old_level;

  ASSERT(is_thread(t));

  old_level = intr_disable();
  ASSERT(t->status == THREAD_BLOCKED);
  thread_enqueue(t);
  t->status = THREAD_READY;
  intr_set_level(old_level);
}

/* Returns the name of the running thread. */
const char* thread_name(void) { return thread_current()->name; }

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread* thread_current(void) {
  struct thread* t = running_thread();

  /* Make sure T is really a thread.
     If either of these assertions fire, then your thread may
     have overflowed its stack.  Each thread has less than 4 kB
     of stack, so a few big automatic arrays or moderate
     recursion can cause stack overflow. */
  ASSERT(is_thread(t));
  ASSERT(t->status == THREAD_RUNNING);

  return t;
}

/* Returns the running thread's tid. */
tid_t thread_tid(void) { return thread_current()->tid; }

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void thread_exit(void) {
  ASSERT(!intr_context());

  /* Remove thread from all threads list, set our status to dying,
     and schedule another process.  That process will destroy us
     when it calls thread_switch_tail(). */
  intr_disable();
  list_remove(&thread_current()->allelem);
  thread_current()->status = THREAD_DYING;
  schedule();
  NOT_REACHED();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void thread_yield(void) {
  struct thread* cur = thread_current();
  enum intr_level old_level;

  ASSERT(!intr_context());

  old_level = intr_disable();
  if (cur != idle_thread)
    thread_enqueue(cur);
  cur->status = THREAD_READY;
  schedule();
  intr_set_level(old_level);
}

/* Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. */
void thread_foreach(thread_action_func* func, void* aux) {
  struct list_elem* e;

  ASSERT(intr_get_level() == INTR_OFF);

  for (e = list_begin(&all_list); e != list_end(&all_list); e = list_next(e)) {
    struct thread* t = list_entry(e, struct thread, allelem);
    func(t, aux);
  }
}

/* Sets the current thread's priority to NEW_PRIORITY.
   Ignored when MLFQS is active (priorities are calculated automatically). */
void thread_set_priority(int new_priority) {
  /* MLFQS calculates priorities automatically - ignore manual changes */
  if (active_sched_policy == SCHED_MLFQS)
    return;

  struct thread* cur = thread_current();
  cur->priority = new_priority;

  if (active_sched_policy == SCHED_PRIO) {
    /* Recalculate eff_priority: max of new base and any donations from held locks */
    int max_prio = new_priority;

    for (struct list_elem* e = list_begin(&cur->held_locks); e != list_end(&cur->held_locks);
         e = list_next(e)) {
      struct lock* held = list_entry(e, struct lock, elem);

      /* Find max priority among this lock's waiters */
      if (!list_empty(&held->semaphore.waiters)) {
        struct list_elem* max_waiter =
            list_max(&held->semaphore.waiters, thread_priority_less, NULL);
        struct thread* t = list_entry(max_waiter, struct thread, elem);
        if (t->eff_priority > max_prio)
          max_prio = t->eff_priority;
      }
    }

    cur->eff_priority = max_prio;

    /* Yield if a higher-priority thread is ready */
    if (!list_empty(&priority_ready_list)) {
      struct thread* front = list_entry(list_front(&priority_ready_list), struct thread, elem);
      if (front->eff_priority > cur->eff_priority)
        thread_yield();
    }
  } else {
    cur->eff_priority = new_priority;
  }
}

/* Returns the current thread's priority. */
int thread_get_priority(void) { return thread_current()->eff_priority; }

/* Sets the current thread's nice value to NICE. */
void thread_set_nice(int nice) {
  ASSERT(nice >= -20 && nice <= 20);

  enum intr_level old_level = intr_disable();
  struct thread* cur = thread_current();
  cur->nice = nice;

  /* Recalculate priority if using MLFQS */
  if (active_sched_policy == SCHED_MLFQS) {
    mlfqs_update_priority(cur);
    /* Yield if we're no longer highest priority */
    thread_yield();
  }
  intr_set_level(old_level);
}

/* Returns the current thread's nice value. */
int thread_get_nice(void) {
  enum intr_level old_level = intr_disable();
  int nice = thread_current()->nice;
  intr_set_level(old_level);
  return nice;
}

/* Returns 100 times the system load average. */
int thread_get_load_avg(void) {
  enum intr_level old_level = intr_disable();
  int result = fix_round(fix_scale(__mk_fix(load_avg), 100));
  intr_set_level(old_level);
  return result;
}

/* Returns 100 times the current thread's recent_cpu value. */
int thread_get_recent_cpu(void) {
  enum intr_level old_level = intr_disable();
  int result = fix_round(fix_scale(__mk_fix(thread_current()->recent_cpu), 100));
  intr_set_level(old_level);
  return result;
}

/* MLFQS: Updates the priority of thread T based on recent_cpu and nice.
   priority = PRI_MAX - (recent_cpu / 4) - (nice * 2)
   Result is clamped to [PRI_MIN, PRI_MAX]. */
static void mlfqs_update_priority(struct thread* t) {
  if (t == idle_thread)
    return;

  /* recent_cpu / 4 in fixed-point, then truncate to int */
  int recent_cpu_term = fix_trunc(fix_unscale(__mk_fix(t->recent_cpu), 4));
  int nice_term = t->nice * 2;
  int new_priority = PRI_MAX - recent_cpu_term - nice_term;

  /* Clamp to valid priority range */
  if (new_priority < PRI_MIN)
    new_priority = PRI_MIN;
  if (new_priority > PRI_MAX)
    new_priority = PRI_MAX;

  t->priority = new_priority;
}

/* MLFQS: Updates recent_cpu for thread T.
   recent_cpu = (2*load_avg)/(2*load_avg + 1) * recent_cpu + nice */
static void mlfqs_update_recent_cpu(struct thread* t) {
  if (t == idle_thread)
    return;

  /* coefficient = (2*load_avg) / (2*load_avg + 1) */
  fixed_point_t load = __mk_fix(load_avg);
  fixed_point_t twice_load = fix_scale(load, 2);
  fixed_point_t twice_load_plus_1 = fix_add(twice_load, fix_int(1));
  fixed_point_t coefficient = fix_div(twice_load, twice_load_plus_1);

  /* recent_cpu = coefficient * recent_cpu + nice */
  fixed_point_t recent = __mk_fix(t->recent_cpu);
  fixed_point_t new_recent = fix_add(fix_mul(coefficient, recent), fix_int(t->nice));
  t->recent_cpu = new_recent.f;
}

/* MLFQS: Updates the system load average.
   load_avg = (59/60)*load_avg + (1/60)*ready_threads
   ready_threads = number of running + ready threads (excluding idle) */
static void mlfqs_update_load_avg(void) {
  int ready_threads = (int)list_size(&priority_ready_list);

  /* Add 1 if current thread is not idle (it's running) */
  if (thread_current() != idle_thread)
    ready_threads++;

  /* load_avg = (59/60)*load_avg + (1/60)*ready_threads */
  fixed_point_t load = __mk_fix(load_avg);
  fixed_point_t coef_59_60 = fix_frac(59, 60);
  fixed_point_t coef_1_60 = fix_frac(1, 60);

  fixed_point_t term1 = fix_mul(coef_59_60, load);
  fixed_point_t term2 = fix_mul(coef_1_60, fix_int(ready_threads));
  fixed_point_t new_load = fix_add(term1, term2);

  load_avg = new_load.f;
}

/* MLFQS: Called every timer tick to update the running thread's recent_cpu.
   Increments recent_cpu by 1 for the running thread (if not idle). */
void thread_mlfqs_tick(void) {
  struct thread* cur = thread_current();
  if (cur != idle_thread) {
    cur->recent_cpu = fix_add(__mk_fix(cur->recent_cpu), fix_int(1)).f;
  }
}

/* MLFQS: Called every 4 ticks to recalculate priorities for all threads. */
void thread_mlfqs_update_priorities(void) {
  struct list_elem* e;
  for (e = list_begin(&all_list); e != list_end(&all_list); e = list_next(e)) {
    struct thread* t = list_entry(e, struct thread, allelem);
    mlfqs_update_priority(t);
  }
}

/* MLFQS: Called every second (TIMER_FREQ ticks) to update load_avg and recent_cpu. */
void thread_mlfqs_update_stats(void) {
  mlfqs_update_load_avg();

  struct list_elem* e;
  for (e = list_begin(&all_list); e != list_end(&all_list); e = list_next(e)) {
    struct thread* t = list_entry(e, struct thread, allelem);
    mlfqs_update_recent_cpu(t);
    mlfqs_update_priority(t);
  }
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void idle(void* idle_started_ UNUSED) {
  struct semaphore* idle_started = idle_started_;
  idle_thread = thread_current();
  sema_up(idle_started);

  for (;;) {
    /* Let someone else run. */
    intr_disable();
    thread_block();

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
    asm volatile("sti; hlt" : : : "memory");
  }
}

/* Function used as the basis for a kernel thread. */
static void kernel_thread(thread_func* function, void* aux) {
  ASSERT(function != NULL);

  intr_enable(); /* The scheduler runs with interrupts off. */
  function(aux); /* Execute the thread function. */
  thread_exit(); /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
struct thread* running_thread(void) {
  uint32_t* esp;

  /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Because `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
  asm("mov %%esp, %0" : "=g"(esp));
  return pg_round_down(esp);
}

/* Returns true if T appears to point to a valid thread. */
static bool is_thread(struct thread* t) { return t != NULL && t->magic == THREAD_MAGIC; }

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void init_thread(struct thread* t, const char* name, int priority) {
  enum intr_level old_level;

  ASSERT(t != NULL);
  ASSERT(PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT(name != NULL);

  memset(t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy(t->name, name, sizeof t->name);
  t->stack = (uint8_t*)t + PGSIZE;
  t->priority = priority;
  t->eff_priority = priority; // Initially same as base priority
  t->waiting_lock = NULL;     // Not waiting for any lock
  list_init(&t->held_locks);  // No locks held initially
#ifdef USERPROG
  t->pcb = NULL;
  /* pcb_elem will be added to pcb->threads when pcb_init() is called */
  t->cwd = NULL;
#endif
  t->magic = THREAD_MAGIC;
  t->wake_up_tick = 0;

  /* Fair scheduler fields initialization */
  t->tickets = 100; // Default ticket count for lottery
  t->stride = 0;    // Will be computed from tickets
  t->pass = 0;      // Initial pass value
  t->vruntime = 0;  // Initial virtual runtime
  t->deadline = 0;  // Initial virtual deadline
  t->nice_fair = 0; // Default nice value (neutral weight)

  /* MLFQS scheduler fields initialization */
  t->nice = 0;       // Default nice value (neutral)
  t->recent_cpu = 0; // No recent CPU usage (fixed-point 0)

  old_level = intr_disable();
  list_push_back(&all_list, &t->allelem);
  intr_set_level(old_level);
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void* alloc_frame(struct thread* t, size_t size) {
  /* Stack data is always allocated in word-size units. */
  ASSERT(is_thread(t));
  ASSERT(size % sizeof(uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

/* First-in first-out scheduler */
static struct thread* thread_schedule_fifo(void) {
  if (!list_empty(&fifo_ready_list))
    return list_entry(list_pop_front(&fifo_ready_list), struct thread, elem);
  else
    return idle_thread;
}

/* Strict priority scheduler */
static struct thread* thread_schedule_prio(void) {
  if (!list_empty(&priority_ready_list))
    return list_entry(list_pop_front(&priority_ready_list), struct thread, elem);
  else
    return idle_thread;
}

/* Fair scheduler implementations */

/* Stride scheduler:
   - Each thread has a stride = STRIDE_LARGE / tickets
   - Each thread has a pass value that starts at 0
   - Pick thread with minimum pass, run it, then add stride to its pass
   - Provides proportional share based on ticket allocation */
static struct thread* thread_schedule_stride(void) {
  if (list_empty(&fair_ready_list))
    return idle_thread;
  /* TODO: Implement stride scheduling
     - Find thread with minimum pass value
     - Update pass value after selection */
  PANIC("Unimplemented fair scheduler: stride");
}

/* Lottery scheduler:
   - Each thread has a number of tickets
   - Randomly select a ticket number from total pool
   - Run the thread holding that ticket
   - Provides probabilistic proportional share */
static struct thread* thread_schedule_lottery(void) {
  if (list_empty(&fair_ready_list))
    return idle_thread;
  /* TODO: Implement lottery scheduling
     - Count total tickets in ready list
     - Generate random number in [0, total_tickets)
     - Find and return winning thread */
  PANIC("Unimplemented fair scheduler: lottery");
}

/* Completely Fair Scheduler (CFS):
   - Each thread tracks vruntime (virtual runtime)
   - vruntime increases based on actual runtime / weight
   - Always pick thread with smallest vruntime
   - Uses red-black tree in Linux, can use sorted list here */
static struct thread* thread_schedule_cfs(void) {
  if (list_empty(&fair_ready_list))
    return idle_thread;
  /* TODO: Implement CFS
     - Find thread with minimum vruntime
     - Update vruntime after running */
  PANIC("Unimplemented fair scheduler: CFS");
}

/* Earliest Eligible Virtual Deadline First (EEVDF):
   - Extension of CFS with deadline awareness
   - Each thread has vruntime and virtual deadline
   - Only consider "eligible" threads (vruntime <= current time)
   - Among eligible, pick earliest virtual deadline
   - Better latency guarantees than pure CFS */
static struct thread* thread_schedule_eevdf(void) {
  if (list_empty(&fair_ready_list))
    return idle_thread;
  /* TODO: Implement EEVDF
     - Filter eligible threads (vruntime <= min_vruntime)
     - Among eligible, find earliest virtual deadline
     - Update vruntime and deadline after running */
  PANIC("Unimplemented fair scheduler: EEVDF");
}

/* Jump table for fair scheduler implementations */
typedef struct thread* fair_scheduler_func(void);
static fair_scheduler_func* fair_scheduler_jump_table[4] = {
    thread_schedule_stride,  /* FAIR_SCHED_STRIDE */
    thread_schedule_lottery, /* FAIR_SCHED_LOTTERY */
    thread_schedule_cfs,     /* FAIR_SCHED_CFS */
    thread_schedule_eevdf,   /* FAIR_SCHED_EEVDF */
};

/* Fair priority scheduler - dispatches to the active fair scheduler implementation */
static struct thread* thread_schedule_fair(void) {
  return (fair_scheduler_jump_table[active_fair_sched_type])();
}

/* Multi-level feedback queue scheduler.
   Returns the highest-priority thread from priority_ready_list.
   Similar to SCHED_PRIO but priorities are calculated automatically. */
static struct thread* thread_schedule_mlfqs(void) {
  if (list_empty(&priority_ready_list))
    return idle_thread;

  /* List is ordered by priority, so front has highest priority */
  return list_entry(list_pop_front(&priority_ready_list), struct thread, elem);
}

/* Not an actual scheduling policy — placeholder for empty
 * slots in the scheduler jump table. */
static struct thread* thread_schedule_reserved(void) {
  PANIC("Invalid scheduler policy value: %d", active_sched_policy);
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread* next_thread_to_run(void) {
  return (scheduler_jump_table[active_sched_policy])();
}

/* Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.  This function is normally invoked by
   thread_switch() as its final action before returning, but
   the first time a thread is scheduled it is called by
   switch_entry() (see switch.S).

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   After this function and its caller returns, the thread switch
   is complete. */
void thread_switch_tail(struct thread* prev) {
  struct thread* cur = running_thread();

  ASSERT(intr_get_level() == INTR_OFF);

  /* Mark us as running. */
  cur->status = THREAD_RUNNING;

  /* Start new time slice. */
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  process_activate();
#endif

  /* If the thread we switched from is dying, destroy its struct
     thread.  This must happen late so that thread_exit() doesn't
     pull out the rug under itself.  (We don't free
     initial_thread because its memory was not obtained via
     palloc().) */
  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread) {
    ASSERT(prev != cur);
    palloc_free_page(prev);
  }
}

/* Schedules a new thread.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until thread_switch_tail()
   has completed. */
static void schedule(void) {
  struct thread* cur = running_thread();
  struct thread* next = next_thread_to_run();
  struct thread* prev = NULL;

  ASSERT(intr_get_level() == INTR_OFF);
  ASSERT(cur->status != THREAD_RUNNING);
  ASSERT(is_thread(next));

  if (cur != next)
    prev = switch_threads(cur, next);
  thread_switch_tail(prev);
}

/* Returns a tid to use for a new thread. */
static tid_t allocate_tid(void) {
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire(&tid_lock);
  tid = next_tid++;
  lock_release(&tid_lock);

  return tid;
}

/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof(struct thread, stack);
