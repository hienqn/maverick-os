/*
 * ╔══════════════════════════════════════════════════════════════════════════╗
 * ║                           THREAD MODULE                                   ║
 * ╠══════════════════════════════════════════════════════════════════════════╣
 * ║                                                                          ║
 * ║  The thread module is the core of the PintOS kernel, managing the        ║
 * ║  execution context for both kernel threads and user processes.           ║
 * ║                                                                          ║
 * ║  THREAD vs PROCESS:                                                      ║
 * ║  ─────────────────                                                       ║
 * ║  • Thread: Execution context (registers, stack, scheduling state)        ║
 * ║  • Process: Resource container (address space, file descriptors, etc.)   ║
 * ║                                                                          ║
 * ║  In PintOS:                                                              ║
 * ║  • Pure kernel threads: struct thread only (no PCB)                      ║
 * ║  • User processes: struct thread + struct process (PCB)                  ║
 * ║  • Multi-threaded processes: Multiple threads share one PCB              ║
 * ║                                                                          ║
 * ║  THREAD MEMORY LAYOUT:                                                   ║
 * ║  ─────────────────────                                                   ║
 * ║                                                                          ║
 * ║       4 kB ┌─────────────────────────────────────┐                       ║
 * ║            │           Kernel Stack              │                       ║
 * ║            │               │                     │                       ║
 * ║            │               │                     │                       ║
 * ║            │               ▼                     │                       ║
 * ║            │          grows downward             │                       ║
 * ║            │                                     │                       ║
 * ║            │    (less than 4 kB available -      │                       ║
 * ║            │     avoid large local variables!)   │                       ║
 * ║            │                                     │                       ║
 * ║            ├─────────────────────────────────────┤                       ║
 * ║            │             magic                   │  ← Stack overflow     ║
 * ║            │               :                     │    detection          ║
 * ║            │            name[16]                 │                       ║
 * ║            │            status                   │                       ║
 * ║            │             tid                     │                       ║
 * ║       0 kB └─────────────────────────────────────┘                       ║
 * ║            ↑ struct thread starts at page boundary                       ║
 * ║                                                                          ║
 * ║  SCHEDULING POLICIES:                                                    ║
 * ║  ────────────────────                                                    ║
 * ║  • SCHED_FIFO:  First-in, first-out (simple round-robin)                 ║
 * ║  • SCHED_PRIO:  Strict priority with priority donation                   ║
 * ║  • SCHED_FAIR:  Proportional share (stride, lottery, CFS, EEVDF)         ║
 * ║  • SCHED_MLFQS: Multi-level feedback queue (BSD-style)                   ║
 * ║                                                                          ║
 * ║  THREAD LIFECYCLE:                                                       ║
 * ║  ─────────────────                                                       ║
 * ║                                                                          ║
 * ║    thread_create()                                                       ║
 * ║         │                                                                ║
 * ║         ▼                                                                ║
 * ║    ┌──────────┐    thread_unblock()    ┌──────────┐                      ║
 * ║    │ BLOCKED  │ ◄────────────────────► │  READY   │                      ║
 * ║    └──────────┘    thread_block()      └────┬─────┘                      ║
 * ║                                             │                            ║
 * ║                                    schedule()                            ║
 * ║                                             │                            ║
 * ║                                             ▼                            ║
 * ║                                       ┌──────────┐                       ║
 * ║                                       │ RUNNING  │                       ║
 * ║                                       └────┬─────┘                       ║
 * ║                                            │                             ║
 * ║                                    thread_exit()                         ║
 * ║                                            │                             ║
 * ║                                            ▼                             ║
 * ║                                       ┌──────────┐                       ║
 * ║                                       │  DYING   │──► freed              ║
 * ║                                       └──────────┘                       ║
 * ║                                                                          ║
 * ╚══════════════════════════════════════════════════════════════════════════╝
 */

#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/synch.h"
#include "threads/fixed-point.h"
#include "synch.h"

/* ═══════════════════════════════════════════════════════════════════════════
 * THREAD STATES
 * ═══════════════════════════════════════════════════════════════════════════*/

/* States in a thread's life cycle. */
enum thread_status {
  THREAD_RUNNING, /* Currently executing on CPU. */
  THREAD_READY,   /* Ready to run, in ready queue. */
  THREAD_BLOCKED, /* Waiting for event (I/O, lock, sleep). */
  THREAD_DYING    /* About to be destroyed. */
};

/* ═══════════════════════════════════════════════════════════════════════════
 * THREAD IDENTIFIERS AND PRIORITIES
 * ═══════════════════════════════════════════════════════════════════════════*/

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;

#define TID_ERROR ((tid_t)-1) /* Error value for tid_t. */

/* Thread priorities.
   Higher value = higher priority.
   Used by SCHED_PRIO and SCHED_MLFQS. */
#define PRI_MIN 0      /* Lowest priority. */
#define PRI_DEFAULT 31 /* Default priority. */
#define PRI_MAX 63     /* Highest priority. */

/* ═══════════════════════════════════════════════════════════════════════════
 * THREAD CONTROL BLOCK (TCB)
 * ═══════════════════════════════════════════════════════════════════════════
 * 
 * The thread struct is the Thread Control Block (TCB) - it stores all the
 * kernel-level state needed to manage a thread's execution.
 *
 * MEMORY CONSTRAINTS:
 * Each thread struct sits at the bottom of a 4 kB page, with the kernel
 * stack growing down from the top. This means:
 *   1. struct thread must stay small (well under 1 kB)
 *   2. Kernel stack space is limited - no large local variables!
 *   3. Stack overflow corrupts the thread struct (detected via magic field)
 *
 * LIST ELEMENT USAGE:
 * The `elem` field serves dual purposes (mutually exclusive):
 *   • In ready queue (thread.c) when status == THREAD_READY
 *   • In semaphore waiters list (synch.c) when status == THREAD_BLOCKED
 * This works because a thread is never both ready AND blocked.
 *
 * ═══════════════════════════════════════════════════════════════════════════*/

struct thread {
  /* ═══════════════════════════════════════════════════════════════════════
   * CORE THREAD IDENTITY AND STATE
   * ─────────────────────────────────────────────────────────────────────────
   * Owned by thread.c - these fields manage the thread's basic identity
   * and execution state.
   * ═══════════════════════════════════════════════════════════════════════*/
  tid_t tid;                 /* Unique thread identifier (never reused). */
  enum thread_status status; /* Current state: RUNNING, READY, BLOCKED, DYING. */
  char name[16];             /* Name for debugging (e.g., "main", "idle"). */
  uint8_t* stack;            /* Saved stack pointer (used during context switch). */

  /* ═══════════════════════════════════════════════════════════════════════
   * PRIORITY SCHEDULING (SCHED_PRIO)
   * ─────────────────────────────────────────────────────────────────────────
   * Priority scheduling with priority donation to prevent priority inversion.
   * 
   * Priority Donation Pattern:
   *   Thread H (high priority) → waiting for lock held by → Thread L (low priority)
   *   
   *   Without donation: H waits forever if M (medium) keeps preempting L
   *   With donation: L temporarily inherits H's priority, runs, releases lock
   *   
   * Implementation:
   *   • priority:     Base priority (set by user, used as default)
   *   • eff_priority: Effective priority = max(priority, donated priorities)
   *   • waiting_lock: The lock we're blocked on (for donation chain traversal)
   *   • held_locks:   Locks we hold (to recompute donation when releasing)
   * ═══════════════════════════════════════════════════════════════════════*/
  int priority;              /* Base priority (0-63). */
  int eff_priority;          /* Effective priority after donations. */
  struct lock* waiting_lock; /* Lock we're waiting for (NULL if not waiting). */
  struct list held_locks;    /* List of locks we currently hold. */

  /* ═══════════════════════════════════════════════════════════════════════
   * THREAD LISTS
   * ─────────────────────────────────────────────────────────────────────────
   * List elements for various thread collections.
   * ═══════════════════════════════════════════════════════════════════════*/
  struct list_elem allelem; /* Element in the global all_list of all threads. */
  struct list_elem elem;    /* Element in ready queue OR semaphore waiters. */

  /* ═══════════════════════════════════════════════════════════════════════
   * TIMER SLEEP SUPPORT
   * ─────────────────────────────────────────────────────────────────────────
   * Efficient sleeping implementation - thread sets wake_up_tick and blocks.
   * Timer interrupt wakes threads whose wake_up_tick has passed.
   * ═══════════════════════════════════════════════════════════════════════*/
  int64_t wake_up_tick; /* Timer tick at which to wake (0 = not sleeping). */

  /* ═══════════════════════════════════════════════════════════════════════
   * FAIR SCHEDULER FIELDS (SCHED_FAIR)
   * ─────────────────────────────────────────────────────────────────────────
   * Fair scheduling provides proportional CPU share based on weights/tickets.
   * Multiple implementations available:
   *   • Stride:   Deterministic proportional share
   *   • Lottery:  Probabilistic proportional share
   *   • CFS:      Linux-style virtual runtime
   *   • EEVDF:    Virtual deadline with eligibility
   * ═══════════════════════════════════════════════════════════════════════*/
  int tickets;      /* Lottery: ticket count (more tickets = more CPU). */
  int64_t stride;   /* Stride: inverse of tickets (STRIDE_LARGE / tickets). */
  int64_t pass;     /* Stride: accumulated pass value (lower = run next). */
  int64_t vruntime; /* CFS/EEVDF: virtual runtime (weighted actual time). */
  int64_t deadline; /* EEVDF: virtual deadline for latency guarantees. */
  int nice_fair;    /* Fair scheduler nice value (-20 to +20). */

  /* ═══════════════════════════════════════════════════════════════════════
   * MLFQS SCHEDULER FIELDS (SCHED_MLFQS)
   * ─────────────────────────────────────────────────────────────────────────
   * Multi-Level Feedback Queue Scheduler (BSD-style).
   * Automatically adjusts priorities based on CPU usage.
   * 
   * Formulas (updated every tick/4 ticks/1 second):
   *   recent_cpu = (2*load_avg)/(2*load_avg + 1) * recent_cpu + nice
   *   priority = PRI_MAX - (recent_cpu / 4) - (nice * 2)
   *   load_avg = (59/60)*load_avg + (1/60)*ready_threads
   * ═══════════════════════════════════════════════════════════════════════*/
  int nice;       /* Nice value: -20 (high priority) to +20 (low priority). */
  int recent_cpu; /* Recent CPU usage (17.14 fixed-point format). */

#ifdef USERPROG
  /* ═══════════════════════════════════════════════════════════════════════
   * USER PROGRAM SUPPORT
   * ─────────────────────────────────────────────────────────────────────────
   * When running user programs, threads are associated with a process (PCB).
   * Multiple threads can share a single PCB (multi-threading).
   * ═══════════════════════════════════════════════════════════════════════*/
  struct process* pcb;              /* Process control block (NULL for kernel threads). */
  struct list_elem pcb_elem;        /* Element in process's thread list. */
  void* user_stack;                 /* Base of this thread's user stack page. */
  struct pthread_status* my_status; /* Status struct for pthread join synchronization. */
  struct dir* cwd;                  /* Current working directory (per-thread in PintOS). */
#endif

  /* ═══════════════════════════════════════════════════════════════════════
   * STACK OVERFLOW DETECTION
   * ─────────────────────────────────────────────────────────────────────────
   * Magic number placed at the boundary between thread struct and stack.
   * If stack overflows, it corrupts magic, and thread_current() will ASSERT.
   * ═══════════════════════════════════════════════════════════════════════*/
  unsigned magic; /* Always THREAD_MAGIC (0xcd6abf4b). */
};

/* ═══════════════════════════════════════════════════════════════════════════
 * SCHEDULING POLICIES
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * The kernel supports multiple scheduling algorithms, selectable at boot
 * via command-line options (e.g., "-sched=prio", "-sched=mlfqs").
 *
 * ┌───────────────┬────────────────────────────────────────────────────────┐
 * │ Policy        │ Description                                            │
 * ├───────────────┼────────────────────────────────────────────────────────┤
 * │ SCHED_FIFO    │ Simple first-in-first-out, round-robin tiebreaking     │
 * │ SCHED_PRIO    │ Strict priority with donation, round-robin per level   │
 * │ SCHED_FAIR    │ Proportional share (stride/lottery/CFS/EEVDF)          │
 * │ SCHED_MLFQS   │ BSD-style adaptive priority scheduling                 │
 * └───────────────┴────────────────────────────────────────────────────────┘
 *
 * ═══════════════════════════════════════════════════════════════════════════*/

enum sched_policy {
  SCHED_FIFO,  /* First-in, first-out scheduler. */
  SCHED_PRIO,  /* Strict-priority scheduler with round-robin tiebreaking. */
  SCHED_FAIR,  /* Fair scheduler (implementation-defined). */
  SCHED_MLFQS, /* Multi-level Feedback Queue Scheduler. */
};
#define SCHED_DEFAULT SCHED_FIFO

/* Fair scheduler implementation types (used when active_sched_policy == SCHED_FAIR). */
enum fair_sched_type {
  FAIR_SCHED_STRIDE,  /* Stride scheduling - deterministic proportional share. */
  FAIR_SCHED_LOTTERY, /* Lottery scheduling - probabilistic proportional share. */
  FAIR_SCHED_CFS,     /* Completely Fair Scheduler (Linux-style vruntime). */
  FAIR_SCHED_EEVDF,   /* Earliest Eligible Virtual Deadline First. */
};
#define FAIR_SCHED_DEFAULT FAIR_SCHED_STRIDE

/* Currently active scheduling policy (set at boot from command line). */
extern enum sched_policy active_sched_policy;

/* Currently active fair scheduler implementation. */
extern enum fair_sched_type active_fair_sched_type;

/* ═══════════════════════════════════════════════════════════════════════════
 * THREAD LIFECYCLE FUNCTIONS
 * ═══════════════════════════════════════════════════════════════════════════*/

/* Initialization (called once during kernel startup). */
void thread_init(void);
void thread_start(void);

/* Timer tick handling (called from timer interrupt). */
void thread_tick(void);
void thread_print_stats(void);

/* Thread creation and termination. */
typedef void thread_func(void* aux);
tid_t thread_create(const char* name, int priority, thread_func*, void*);
void thread_exit(void) NO_RETURN;

/* Blocking and unblocking. */
void thread_block(void);
void thread_unblock(struct thread*);

/* Yielding CPU to other threads. */
void thread_yield(void);

/* Current thread accessors. */
struct thread* thread_current(void);
tid_t thread_tid(void);
const char* thread_name(void);

/* ═══════════════════════════════════════════════════════════════════════════
 * THREAD ITERATION
 * ═══════════════════════════════════════════════════════════════════════════*/

/* Callback type for thread_foreach. */
typedef void thread_action_func(struct thread* t, void* aux);

/* Iterate over all threads (interrupts must be off). */
void thread_foreach(thread_action_func*, void*);

/* ═══════════════════════════════════════════════════════════════════════════
 * PRIORITY MANAGEMENT
 * ═══════════════════════════════════════════════════════════════════════════*/

int thread_get_priority(void);
void thread_set_priority(int);

/* MLFQS-specific priority management. */
int thread_get_nice(void);
void thread_set_nice(int);
int thread_get_recent_cpu(void);
int thread_get_load_avg(void);

/* MLFQS scheduler functions (called from timer interrupt). */
void thread_mlfqs_tick(void);
void thread_mlfqs_update_priorities(void);
void thread_mlfqs_update_stats(void);

/* ═══════════════════════════════════════════════════════════════════════════
 * PRIORITY COMPARISON UTILITIES
 * ═══════════════════════════════════════════════════════════════════════════
 * These comparison functions are used with list_max, list_insert_ordered, etc.
 * ═══════════════════════════════════════════════════════════════════════════*/

/* Returns true if a's priority < b's priority (for list_max to find highest). */
bool thread_priority_less(const struct list_elem* a, const struct list_elem* b, void* aux);

/* Returns true if a's priority > b's priority (for list_insert_ordered). */
bool thread_priority_greater(const struct list_elem* a, const struct list_elem* b, void* aux);

#endif /* threads/thread.h */
