# Pintos Project 3: Virtual Memory Implementation Plan

## Overview

This plan outlines the implementation of a complete virtual memory system for Pintos, based on the Stanford CS140 curriculum. The Berkeley version removed this project, but all the infrastructure exists to support it.

## Current State Analysis

### What Already Exists (Ready to Use)
| Component | Location | Status |
|-----------|----------|--------|
| Physical page allocator | `threads/palloc.c` | Complete |
| Page directory management | `userprog/pagedir.c` | Complete |
| PTE/PDE abstractions | `threads/pte.h` | Complete |
| Virtual address helpers | `threads/vaddr.h` | Complete |
| Process/thread framework | `userprog/process.c` | Complete |
| Exception handling skeleton | `userprog/exception.c` | Needs enhancement |
| VM test suite | `tests/vm/` | ~30 tests ready |

### What Needs to Be Built
| Component | Priority | Complexity |
|-----------|----------|------------|
| Supplemental Page Table | P0 | Medium |
| Frame Table | P0 | Medium |
| Page Fault Handler | P0 | High |
| Lazy Loading | P1 | Medium |
| Stack Growth | P1 | Low |
| Swap Space | P2 | High |
| Memory-Mapped Files | P3 | Medium |

---

## Implementation Phases

### Phase 1: Core Data Structures

#### 1.1 Supplemental Page Table (SPT)

**Purpose:** Track metadata about each virtual page that the hardware page table can't store.

**File:** `vm/page.h` and `vm/page.c`

```c
/* Page status - where is the data? */
enum page_status {
    PAGE_ZERO,      /* All zeros, not yet allocated */
    PAGE_FRAME,     /* In a physical frame */
    PAGE_SWAP,      /* Swapped to disk */
    PAGE_FILE,      /* In a file (executable or mmap) */
};

/* Supplemental page table entry */
struct spt_entry {
    void *upage;              /* User virtual address (page-aligned) */
    enum page_status status;  /* Where is the page? */

    bool writable;            /* Is page writable? */
    bool dirty;               /* Has page been modified? */

    /* For PAGE_FRAME */
    void *kpage;              /* Kernel virtual address of frame */

    /* For PAGE_SWAP */
    size_t swap_slot;         /* Swap slot index */

    /* For PAGE_FILE */
    struct file *file;        /* File to read from */
    off_t file_offset;        /* Offset in file */
    size_t read_bytes;        /* Bytes to read from file */
    size_t zero_bytes;        /* Bytes to zero after read */

    struct hash_elem hash_elem;  /* For hash table */
};

/* Per-process supplemental page table */
struct spt {
    struct hash pages;        /* Hash table of spt_entry */
};
```

**Key Functions:**
- `spt_init(struct spt *)` - Initialize SPT for a process
- `spt_destroy(struct spt *)` - Free all SPT entries
- `spt_find(struct spt *, void *upage)` - Find entry by virtual address
- `spt_insert(struct spt *, struct spt_entry *)` - Add new entry
- `spt_remove(struct spt *, void *upage)` - Remove entry

#### 1.2 Frame Table

**Purpose:** Track physical frames allocated to user pages; enable eviction.

**File:** `vm/frame.h` and `vm/frame.c`

```c
/* Frame table entry */
struct frame_entry {
    void *kpage;              /* Kernel virtual address */
    void *upage;              /* User virtual address */
    struct thread *owner;     /* Thread that owns this frame */
    bool pinned;              /* If true, cannot be evicted */
    struct list_elem elem;    /* For frame list */
};

/* Global frame table */
struct frame_table {
    struct list frames;       /* List of allocated frames */
    struct lock lock;         /* Synchronization */
};
```

**Key Functions:**
- `frame_init()` - Initialize frame table
- `frame_alloc(void *upage, bool writable)` - Allocate frame, evict if needed
- `frame_free(void *kpage)` - Free a frame
- `frame_set_pinned(void *kpage, bool pinned)` - Pin/unpin frame

---

### Phase 2: Page Fault Handler

**File:** `userprog/exception.c` (modify `page_fault()`)

**Logic Flow:**
```
page_fault(fault_addr):
    1. Round fault_addr down to page boundary
    2. Look up page in supplemental page table

    3. If not found:
        a. Check if valid stack growth (fault_addr >= esp - 32)
        b. If valid stack growth, create new SPT entry
        c. Otherwise, kill process (SEGFAULT)

    4. If found, load page based on status:
        a. PAGE_ZERO: Allocate zeroed frame
        b. PAGE_FILE: Read from file into frame
        c. PAGE_SWAP: Read from swap into frame
        d. PAGE_FRAME: Should not happen (already loaded)

    5. Install page in page table (pagedir_set_page)
    6. Update SPT entry status to PAGE_FRAME
```

**Stack Growth Policy:**
- Valid if: `fault_addr >= esp - 32` (PUSHA pushes 32 bytes)
- Limit stack to 8MB (2048 pages)
- Stack region: `PHYS_BASE - 8MB` to `PHYS_BASE`

---

### Phase 3: Lazy Loading

**Modify:** `userprog/process.c` (`load_segment()`)

**Current Behavior:** Loads entire executable into memory at exec time.

**New Behavior:**
1. Don't allocate frames during `load_segment()`
2. Create SPT entries with `PAGE_FILE` status
3. Store file, offset, read_bytes, zero_bytes in entry
4. Pages loaded on-demand when accessed (page fault)

```c
/* In load_segment - change from: */
palloc_get_page() + file_read()

/* To: */
spt_entry = malloc(sizeof(struct spt_entry));
spt_entry->status = PAGE_FILE;
spt_entry->file = file;
spt_entry->file_offset = ofs;
spt_entry->read_bytes = page_read_bytes;
spt_entry->zero_bytes = page_zero_bytes;
spt_insert(&thread_current()->spt, spt_entry);
```

---

### Phase 4: Swap Space

**Files:** `vm/swap.h` and `vm/swap.c`

**Data Structures:**
```c
/* Swap table - tracks swap slot usage */
struct swap_table {
    struct block *swap_block;   /* Swap partition */
    struct bitmap *used_slots;  /* Which slots are in use */
    struct lock lock;
};

#define SECTORS_PER_PAGE (PGSIZE / BLOCK_SECTOR_SIZE)  /* 8 sectors */
```

**Key Functions:**
- `swap_init()` - Initialize swap table, find swap block
- `swap_out(void *kpage)` - Write page to swap, return slot index
- `swap_in(size_t slot, void *kpage)` - Read page from swap
- `swap_free(size_t slot)` - Mark slot as available

**Eviction Algorithm (Clock/Second-Chance):**
```
evict_frame():
    while true:
        frame = next frame in circular list
        if frame.pinned:
            continue
        if pagedir_is_accessed(frame.owner->pagedir, frame.upage):
            pagedir_set_accessed(..., false)  // Clear accessed bit
            continue  // Give second chance
        else:
            // Evict this frame
            if pagedir_is_dirty(...) or frame.spt_entry.status == PAGE_SWAP:
                slot = swap_out(frame.kpage)
                frame.spt_entry.status = PAGE_SWAP
                frame.spt_entry.swap_slot = slot
            pagedir_clear_page(...)
            return frame
```

---

### Phase 5: Memory-Mapped Files

**Files:** `vm/mmap.h` and `vm/mmap.c`

**System Calls to Implement:**
```c
/* Map file into memory */
void *mmap(int fd, void *addr);

/* Unmap memory region */
void munmap(void *addr);
```

**Data Structure:**
```c
struct mmap_entry {
    int mapid;                /* Mapping ID */
    struct file *file;        /* Mapped file */
    void *addr;               /* Start address */
    size_t page_count;        /* Number of pages */
    struct list_elem elem;    /* For process's mmap list */
};
```

**mmap() Logic:**
1. Validate: addr page-aligned, non-zero, doesn't overlap existing mappings
2. Reopen file (so closing fd doesn't affect mapping)
3. Create SPT entries for each page (PAGE_FILE)
4. Add to process's mmap list
5. Return mapping ID

**munmap() Logic:**
1. Find mmap_entry by address
2. For each page:
   - If dirty, write back to file
   - Remove from page table
   - Remove SPT entry
3. Close file, free mmap_entry

---

## File Structure

```
src/vm/
├── vm.h          # Common VM definitions
├── vm.c          # VM initialization
├── page.h        # Supplemental page table
├── page.c
├── frame.h       # Frame table
├── frame.c
├── swap.h        # Swap management
├── swap.c
├── mmap.h        # Memory-mapped files
├── mmap.c
├── Makefile      # (already exists)
└── Make.vars     # (already exists)
```

---

## Files to Modify

| File | Changes |
|------|---------|
| `userprog/exception.c` | Enhance page_fault() handler |
| `userprog/process.c` | Add SPT to process, lazy loading |
| `userprog/process.h` | Add SPT field to struct process |
| `userprog/syscall.c` | Add mmap/munmap syscalls |
| `threads/thread.h` | May need VM-related fields |
| `Makefile.build` | Add vm directory to build |

---

## Implementation Order

### Milestone 1: Basic Demand Paging (Tests: pt-grow-*)
1. Implement SPT (page.c/h)
2. Implement frame table without eviction (frame.c/h)
3. Modify page_fault() for stack growth
4. Modify load_segment() for lazy loading

### Milestone 2: Page Eviction (Tests: page-*)
1. Implement swap table (swap.c/h)
2. Implement eviction in frame_alloc()
3. Implement clock algorithm
4. Handle swap-in on page fault

### Milestone 3: Memory-Mapped Files (Tests: mmap-*)
1. Implement mmap data structures
2. Implement mmap() syscall
3. Implement munmap() syscall
4. Handle mmap page faults and eviction

---

## Testing Strategy

```bash
# Run specific test categories
make tests/vm/pt-grow-stack.result
make tests/vm/page-linear.result
make tests/vm/mmap-read.result

# Run all VM tests
make check TESTS=tests/vm
```

---

## Key Challenges & Solutions

### Challenge 1: Synchronization
- Frame table accessed by multiple processes
- Solution: Global frame table lock, per-process SPT (no lock needed)

### Challenge 2: Page Fault During System Call
- Kernel accessing user memory can fault
- Solution: Pin frames while kernel accesses them

### Challenge 3: Eviction Deadlocks
- Eviction during file read could deadlock on file lock
- Solution: Don't evict frames of current process during I/O

### Challenge 4: Fork with VM
- Child must have copy of parent's virtual memory
- Solution: Copy SPT entries, mark pages copy-on-write (optional optimization)

---

## Resources

- [Stanford CS140 Project 3 Spec](https://web.stanford.edu/class/cs140/projects/pintos/pintos_4.html)
- [KAIST Pintos Guide](https://casys-kaist.github.io/pintos-kaist/project3/intro.html)
- [Pintos Reference Guide](https://jeason.gitbooks.io/pintos-reference-guide-sysu/VM.html)
