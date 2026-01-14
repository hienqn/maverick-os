# VM Challenge: Page Access Tracker

**Difficulty:** Intermediate
**Estimated Time:** 2-4 hours
**Prerequisites:** Understanding of SPT, frame table, and page fault handling

---

## The Scenario

You're a kernel developer at a company building a memory-intensive application. Users are complaining that the system feels sluggish when memory pressure is high. Your task: **build observability tools** so developers can understand their application's memory access patterns.

You'll implement a **Page Access Tracker** that:
1. Tracks statistics about page faults and evictions
2. Identifies "hot" pages (frequently accessed) vs "cold" pages (rarely accessed)
3. Exposes this data via a new system call

This is exactly what real OS developers do — Linux has similar features via `/proc/vmstat` and `perf`!

---

## Learning Goals

By completing this challenge, you will:

- [ ] Understand how the hardware "accessed" bit works
- [ ] Modify the frame table to track additional metadata
- [ ] Implement a new system call from scratch
- [ ] Think about synchronization when adding global counters
- [ ] See how real profiling tools work under the hood

---

## Part 1: Add Global VM Statistics

### Goal
Track these statistics globally (across all processes):

```c
struct vm_stats {
    unsigned long page_faults;      /* Total page faults handled */
    unsigned long pages_loaded;     /* Pages loaded from file/swap/zero */
    unsigned long pages_evicted;    /* Pages evicted by clock algorithm */
    unsigned long cow_faults;       /* Copy-on-write faults handled */
    unsigned long stack_growths;    /* Stack growth page faults */
    unsigned long swap_ins;         /* Pages read from swap */
    unsigned long swap_outs;        /* Pages written to swap */
};
```

### Where to Look

| What | File | Line (approx) |
|------|------|---------------|
| Page fault entry point | `src/vm/vm.c` | `vm_handle_fault()` |
| COW handling | `src/vm/vm.c` | Lines 77-131 |
| Stack growth detection | `src/vm/vm.c` | `vm_is_stack_access()` |
| Frame eviction | `src/vm/frame.c` | `frame_evict()` |
| Swap operations | `src/vm/swap.c` | `swap_in()`, `swap_out()` |

### Hints

1. **Where to put the stats struct?**
   Add it to `src/vm/vm.c` as a static global, or create a new `src/vm/vmstat.c` file.

2. **Synchronization?**
   For simple counters, you have options:
   - Use a lock (safe but slower)
   - Use atomic operations (faster, see `<stdatomic.h>` or GCC builtins)
   - Accept minor inaccuracy for performance (common in real OSes!)

3. **Incrementing counters:**
   ```c
   /* Option 1: Simple (slight race possible, often acceptable for stats) */
   vm_stats.page_faults++;

   /* Option 2: With lock */
   lock_acquire(&vm_stats_lock);
   vm_stats.page_faults++;
   lock_release(&vm_stats_lock);

   /* Option 3: Atomic (best for counters) */
   __atomic_fetch_add(&vm_stats.page_faults, 1, __ATOMIC_RELAXED);
   ```

### Checkpoint 1

Add `printf` statements (temporarily) to verify your counters are incrementing:
```c
/* In vm_handle_fault, before returning true: */
printf("VM Stats: faults=%lu, loaded=%lu, evicted=%lu\n",
       vm_stats.page_faults, vm_stats.pages_loaded, vm_stats.pages_evicted);
```

---

## Part 2: Track Per-Frame "Hotness"

### Goal

Extend `struct frame_entry` to track how "hot" each page is:

```c
struct frame_entry {
    /* ... existing fields ... */

    /* NEW: Access tracking */
    unsigned int access_count;    /* Times accessed bit was found set */
    unsigned int age;             /* Clock sweeps survived */
};
```

### The Idea

The clock algorithm already checks the "accessed" bit during eviction. Currently it just clears it. Instead:

1. **When accessed bit is set:** Increment `access_count`, then clear the bit
2. **Every clock sweep:** Increment `age` for all frames that survive (weren't evicted)

This gives you two metrics:
- `access_count`: How frequently is this page touched?
- `age`: How long has this page been in memory?

**Hot pages** = high `access_count`, any `age`
**Cold pages** = low `access_count`, high `age`

### Where to Modify

Look at `frame_evict()` in `src/vm/frame.c`. The clock algorithm loop is where you'll add tracking:

```c
/* Pseudocode of what exists: */
while (true) {
    frame = get_next_frame();
    if (frame->pinned) continue;

    if (pagedir_is_accessed(pd, upage)) {
        pagedir_set_accessed(pd, upage, false);  /* Clear accessed bit */
        /* TODO: Increment access_count here! */
        continue;  /* Give second chance */
    }

    /* Not accessed - evict this one */
    evict(frame);
    return frame;
}
```

### Checkpoint 2

Add a debug function to dump frame statistics:
```c
void frame_dump_stats(void) {
    /* Iterate through frame list, print each frame's stats */
    /* Call this from a test or temporarily from a syscall */
}
```

---

## Part 3: Create a System Call

### Goal

Implement `SYS_VMSTAT` that returns the VM statistics to user programs:

```c
/* User program usage: */
#include <syscall.h>

int main(void) {
    struct vm_stats stats;
    if (vmstat(&stats) == 0) {
        printf("Page faults: %lu\n", stats.page_faults);
        printf("Evictions: %lu\n", stats.pages_evicted);
        /* ... etc ... */
    }
    return 0;
}
```

### Files to Modify

| File | What to Add |
|------|-------------|
| `src/lib/syscall-nr.h` | Add `SYS_VMSTAT` constant |
| `src/lib/user/syscall.h` | Add `vmstat()` function declaration |
| `src/lib/user/syscall.c` | Implement user-side `vmstat()` wrapper |
| `src/userprog/syscall.c` | Handle `SYS_VMSTAT` in syscall handler |

### Pattern to Follow

Look at how existing syscalls work. For example, `SYS_WRITE`:

1. **User side** (`lib/user/syscall.c`):
   ```c
   int write(int fd, const void* buffer, unsigned size) {
       return syscall3(SYS_WRITE, fd, buffer, size);
   }
   ```

2. **Kernel side** (`userprog/syscall.c`):
   ```c
   case SYS_WRITE:
       /* Validate arguments, do the work, return result */
   ```

### Critical: Validate User Pointers!

When copying data TO user space, you must verify the pointer is valid:

```c
case SYS_VMSTAT: {
    struct vm_stats* user_stats = (struct vm_stats*)args[1];

    /* CRITICAL: Validate user pointer before writing! */
    if (!is_valid_user_ptr(user_stats, sizeof(struct vm_stats))) {
        syscall_exit(-1);
    }

    /* Copy kernel stats to user buffer */
    struct vm_stats kernel_stats;
    vm_get_stats(&kernel_stats);  /* You'll implement this */

    memcpy(user_stats, &kernel_stats, sizeof(struct vm_stats));
    f->eax = 0;  /* Success */
    break;
}
```

### Checkpoint 3

Write a simple test program:
```c
/* tests/vm/vmstat-test.c */
#include <syscall.h>
#include <stdio.h>

int main(void) {
    struct vm_stats before, after;

    vmstat(&before);

    /* Do something that causes page faults */
    char* mem = malloc(1024 * 1024);  /* 1 MB */
    memset(mem, 'A', 1024 * 1024);    /* Touch all pages */

    vmstat(&after);

    printf("Page faults during test: %lu\n",
           after.page_faults - before.page_faults);

    return 0;
}
```

---

## Part 4: Bonus - Hot Page Report

### Goal (Optional)

Add `SYS_VMSTAT_HOTPAGES` that returns the N hottest pages for the current process:

```c
struct hot_page_info {
    void* vaddr;           /* Virtual address */
    unsigned int accesses; /* Access count */
    unsigned int age;      /* Time in memory */
};

int vmstat_hotpages(struct hot_page_info* buffer, int max_pages);
```

### Hints

1. Iterate through the current process's frames
2. Sort by `access_count` (descending)
3. Copy top N to user buffer
4. Consider: Should this reset the counters?

---

## Testing Your Implementation

### Manual Testing

```bash
# Build Pintos
cd src/vm
make

# Run your test
cd build
pintos -v -k -T 60 -- -q run vmstat-test
```

### Things to Verify

- [ ] Counters increment correctly
- [ ] No kernel panics or crashes
- [ ] Syscall returns correct data
- [ ] Invalid pointers are rejected (test with NULL)
- [ ] Stats are consistent (e.g., `pages_loaded >= swap_ins`)

### Edge Cases to Consider

1. **What if vmstat is called before any page faults?**
   All counters should be 0.

2. **What if user passes NULL pointer?**
   Should return error, not crash.

3. **What about concurrent access to stats?**
   Multiple processes calling vmstat simultaneously should work.

4. **Overflow?**
   What happens when counters exceed `unsigned long` max?

---

## Reference: How Linux Does It

Linux exposes similar stats via `/proc/vmstat`:

```bash
$ cat /proc/vmstat | head -20
nr_free_pages 123456
nr_inactive_anon 78901
pgfault 987654321
pgmajfault 12345
pgpgin 567890
pgpgout 234567
pswpin 1234
pswpout 5678
...
```

Your implementation is a simplified version of this!

---

## Submission Checklist

When you're done, verify:

- [ ] `struct vm_stats` defined with all required fields
- [ ] Counters increment in correct locations
- [ ] `frame_entry` extended with access tracking (Part 2)
- [ ] `SYS_VMSTAT` syscall implemented end-to-end
- [ ] User pointer validation in syscall handler
- [ ] Test program compiles and runs
- [ ] No compiler warnings
- [ ] Code follows Pintos style (2-space indent, etc.)

---

## Hints if You Get Stuck

<details>
<summary>Hint 1: Where to declare vm_stats</summary>

Add to `src/vm/vm.c`:
```c
/* Global VM statistics. */
static struct vm_stats vm_stats;
static struct lock vm_stats_lock;

void vm_init(void) {
    frame_init();
    swap_init();
    lock_init(&vm_stats_lock);
    memset(&vm_stats, 0, sizeof(vm_stats));
}
```
</details>

<details>
<summary>Hint 2: Incrementing page_faults counter</summary>

In `vm_handle_fault()`, increment at the start:
```c
bool vm_handle_fault(void* fault_addr, ...) {
    __atomic_fetch_add(&vm_stats.page_faults, 1, __ATOMIC_RELAXED);

    /* ... rest of function ... */
}
```
</details>

<details>
<summary>Hint 3: Syscall number</summary>

In `src/lib/syscall-nr.h`, add after the last syscall:
```c
SYS_VMSTAT,    /* 20 or whatever is next */
```
</details>

<details>
<summary>Hint 4: User-side syscall wrapper</summary>

In `src/lib/user/syscall.c`:
```c
int vmstat(struct vm_stats* stats) {
    return syscall1(SYS_VMSTAT, stats);
}
```
</details>

---

## What You'll Learn

After completing this challenge, you'll understand:

1. **Observability** — How OS developers debug and optimize memory systems
2. **Hardware/Software Interface** — How the accessed bit bridges hardware and OS
3. **System Call Design** — The full path from user code to kernel and back
4. **Synchronization Trade-offs** — When to use locks vs atomics vs nothing
5. **Real-World Patterns** — This is how `vmstat`, `top`, and `perf` work!

---

Good luck! Remember: every kernel developer started exactly where you are. The confusion you feel means you're learning! 🚀
