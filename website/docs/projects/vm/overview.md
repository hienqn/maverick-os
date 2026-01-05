---
sidebar_position: 3
---

import AnimatedFlow from '@site/src/components/AnimatedFlow';
import MemoryLayout from '@site/src/components/MemoryLayout';
import InteractiveDiagram from '@site/src/components/InteractiveDiagram';

# Project 3: Virtual Memory

In this project, you'll implement demand paging, frame management with eviction, swap support, and memory-mapped files.

## Learning Goals

- Understand how virtual memory enables processes to use more memory than physically available
- Implement lazy loading (demand paging) for efficient memory use
- Build a frame allocator with eviction using the clock algorithm
- Support memory-mapped files with `mmap`/`munmap`

## Tasks Overview

| Task | Difficulty | Key Concepts |
|------|------------|--------------|
| Supplemental Page Table | ★★☆ | Page metadata, lazy loading |
| Frame Management | ★★★ | Clock eviction, pinning |
| Swap Space | ★★☆ | Block device I/O, slot management |
| Memory-Mapped Files | ★★★ | File-backed pages, writeback |
| Stack Growth | ★☆☆ | Fault handling, heuristics |

## Key Files

| File | Purpose |
|------|---------|
| `vm/page.c` | Supplemental page table (SPT) |
| `vm/page.h` | SPT entry structure and API |
| `vm/frame.c` | Frame table and clock eviction |
| `vm/frame.h` | Frame entry structure and API |
| `vm/swap.c` | Swap partition management |
| `vm/mmap.c` | Memory-mapped file regions |
| `userprog/exception.c` | Page fault handler |

## Getting Started

```bash
cd src/vm
make

# Run a specific test
cd build
make tests/vm/page-linear.result

# Run all VM tests
make check
```

## Virtual Memory Architecture

<InteractiveDiagram
  title="VM Subsystem Components"
  nodes={[
    { id: 'pagefault', label: 'Page Fault Handler', x: 300, y: 50, description: 'Entry point when page not present' },
    { id: 'spt', label: 'Supplemental Page Table', x: 150, y: 150, description: 'Tracks page metadata (file, swap, zero)' },
    { id: 'frame', label: 'Frame Table', x: 450, y: 150, description: 'Manages physical frames, eviction' },
    { id: 'swap', label: 'Swap Space', x: 150, y: 250, description: 'Disk storage for evicted pages' },
    { id: 'file', label: 'File System', x: 450, y: 250, description: 'Source for executable and mmap pages' },
    { id: 'pagedir', label: 'Page Directory', x: 300, y: 350, description: 'Hardware page table (x86)' },
  ]}
  edges={[
    { from: 'pagefault', to: 'spt', label: 'lookup' },
    { from: 'pagefault', to: 'frame', label: 'allocate' },
    { from: 'spt', to: 'swap', label: 'read/write' },
    { from: 'spt', to: 'file', label: 'read' },
    { from: 'frame', to: 'swap', label: 'evict to' },
    { from: 'frame', to: 'pagedir', label: 'install' },
  ]}
/>

## Address Space Layout

<MemoryLayout
  title="32-bit User Address Space"
  regions={[
    {
      name: 'Kernel Space',
      size: '0xC0000000 - 0xFFFFFFFF (1GB)',
      color: '#ef4444',
      description: 'Not accessible to user code'
    },
    {
      name: 'User Stack',
      size: 'Grows down from PHYS_BASE',
      color: '#f59e0b',
      description: 'Grows on demand via page faults'
    },
    {
      name: 'Memory-Mapped Regions',
      size: 'Variable',
      color: '#10b981',
      description: 'mmap() allocated regions'
    },
    {
      name: 'Heap',
      size: 'Grows up (not implemented)',
      color: '#3b82f6',
      description: 'Would be managed by malloc'
    },
    {
      name: 'BSS (Uninitialized Data)',
      size: 'Variable',
      color: '#6b7280',
      description: 'Zero-filled on demand'
    },
    {
      name: 'Data Segment',
      size: 'Variable',
      color: '#8b5cf6',
      description: 'Initialized global variables'
    },
    {
      name: 'Code Segment',
      size: '0x08048000+',
      color: '#ec4899',
      description: 'Executable instructions (read-only)'
    },
  ]}
/>

## Task 1: Supplemental Page Table (SPT)

### Why Do We Need It?

The hardware page table only stores:
- Physical frame number
- Present bit, writable bit, user bit

It **cannot** track:
- Where to load the page from (file? swap? zeros?)
- File offset and read size for lazy loading
- Whether the page has been swapped out

### SPT Entry Structure

```c
struct spt_entry {
  void *upage;           /* User virtual address (key) */
  enum page_status status;

  /* For PAGE_FRAME */
  void *kpage;           /* Kernel address of frame */

  /* For PAGE_FILE (executable or mmap) */
  struct file *file;
  off_t file_offset;
  size_t read_bytes;
  size_t zero_bytes;
  bool is_mmap;

  /* For PAGE_SWAP */
  size_t swap_slot;

  bool writable;
  struct hash_elem hash_elem;
};
```

### Page Status Transitions

<AnimatedFlow
  title="SPT Entry Lifecycle"
  states={[
    { id: 'zero', label: 'PAGE_ZERO', description: 'Unallocated, will be zero-filled' },
    { id: 'file', label: 'PAGE_FILE', description: 'Backed by file (exe or mmap)' },
    { id: 'frame', label: 'PAGE_FRAME', description: 'Loaded in physical memory' },
    { id: 'swap', label: 'PAGE_SWAP', description: 'Evicted to swap partition' },
  ]}
  transitions={[
    { from: 'zero', to: 'frame', label: 'page fault' },
    { from: 'file', to: 'frame', label: 'page fault' },
    { from: 'swap', to: 'frame', label: 'page fault' },
    { from: 'frame', to: 'swap', label: 'eviction' },
  ]}
/>

### Key Functions

```c
/* Initialize SPT for new process */
void spt_init(struct spt *spt);

/* Lookup entry by virtual address */
struct spt_entry *spt_find(struct spt *spt, void *upage);

/* Create lazy-loaded entries */
bool spt_create_file_page(struct spt *spt, void *upage,
                          struct file *file, off_t offset,
                          size_t read_bytes, size_t zero_bytes,
                          bool writable);
bool spt_create_zero_page(struct spt *spt, void *upage, bool writable);

/* Load page into memory (called from page fault handler) */
bool spt_load_page(struct spt_entry *entry);
```

## Task 2: Frame Management

### Frame Table Purpose

Track physical frames allocated to user processes:
- **Ownership**: Which process owns each frame
- **Mapping**: What virtual address maps to it
- **Eviction**: Select victim when memory is full

### Clock (Second-Chance) Algorithm

```
Frame Table: [F0] → [F1] → [F2] → [F3] → [F4] → ...
                      ↑
                  clock_hand

For each frame starting at clock_hand:
  1. If pinned: skip
  2. If accessed bit set:
       - Clear accessed bit
       - Move to next (second chance)
  3. If accessed bit clear:
       - Evict this frame
       - Return to caller
```

### Eviction Process

```c
void *frame_evict(void) {
  /* 1. Find victim using clock algorithm */
  struct frame_entry *victim = find_victim();

  /* 2. Write to swap if dirty */
  if (is_dirty(victim)) {
    if (victim->spt_entry->is_mmap)
      write_to_file(victim);  /* mmap: writeback to file */
    else
      write_to_swap(victim);  /* regular: write to swap */
  }

  /* 3. Update SPT entry */
  victim->spt_entry->status = PAGE_SWAP;
  victim->spt_entry->swap_slot = allocated_slot;

  /* 4. Clear page table entry */
  pagedir_clear_page(victim->owner->pagedir, victim->upage);

  /* 5. Return freed frame */
  return victim->kpage;
}
```

### Frame Pinning

Prevent eviction during kernel operations:

```c
/* During syscall that accesses user buffer */
void sys_read(int fd, void *buffer, size_t size) {
  frame_pin(buffer);  /* Can't evict while we're using it */

  /* ... read data into buffer ... */

  frame_unpin(buffer);  /* Now it can be evicted */
}
```

## Task 3: Swap Space

### Swap Slot Management

```c
/* Bitmap tracks free/used slots */
struct bitmap *swap_bitmap;

/* Each slot holds one page (4KB = 8 sectors) */
#define SECTORS_PER_PAGE (PGSIZE / BLOCK_SECTOR_SIZE)

size_t swap_out(void *kpage) {
  size_t slot = bitmap_scan_and_flip(swap_bitmap, 0, 1, false);
  block_sector_t sector = slot * SECTORS_PER_PAGE;

  for (int i = 0; i < SECTORS_PER_PAGE; i++)
    block_write(swap_device, sector + i, kpage + i * BLOCK_SECTOR_SIZE);

  return slot;
}

void swap_in(size_t slot, void *kpage) {
  block_sector_t sector = slot * SECTORS_PER_PAGE;

  for (int i = 0; i < SECTORS_PER_PAGE; i++)
    block_read(swap_device, sector + i, kpage + i * BLOCK_SECTOR_SIZE);

  bitmap_reset(swap_bitmap, slot);
}
```

## Task 4: Memory-Mapped Files

### mmap() Implementation

```c
void *mmap(void *addr, size_t length, int fd, off_t offset) {
  /* 1. Validate arguments */
  if (addr == NULL || length == 0 || fd < 2)
    return MAP_FAILED;

  /* 2. Check for overlapping mappings */
  if (overlaps_existing_mapping(addr, length))
    return MAP_FAILED;

  /* 3. Get file and create independent reference */
  struct file *file = file_reopen(get_file(fd));

  /* 4. Create mmap_region to track this mapping */
  struct mmap_region *region = malloc(sizeof(*region));
  region->addr = addr;
  region->length = length;
  region->file = file;

  /* 5. Create SPT entries for each page (lazy loading) */
  for (size_t i = 0; i < length; i += PGSIZE) {
    size_t read_bytes = min(PGSIZE, length - i);
    spt_create_file_page(&spt, addr + i, file, offset + i,
                         read_bytes, PGSIZE - read_bytes, true);
    /* Mark as mmap page for writeback */
    entry->is_mmap = true;
  }

  /* 6. Add to process's mmap list */
  list_push_back(&pcb->mmap_list, &region->elem);

  return addr;
}
```

### munmap() and Writeback

```c
void munmap(void *addr, size_t length) {
  struct mmap_region *region = mmap_find_region(addr);

  /* Write back dirty pages */
  for (size_t i = 0; i < length; i += PGSIZE) {
    struct spt_entry *entry = spt_find(&spt, addr + i);
    if (entry->status == PAGE_FRAME && is_dirty(entry)) {
      file_write_at(region->file, entry->kpage,
                    entry->read_bytes, entry->file_offset);
    }
    spt_remove(&spt, addr + i);
  }

  file_close(region->file);
  list_remove(&region->elem);
  free(region);
}
```

## Task 5: Stack Growth

### Detection Heuristic

Stack growth is detected when:
1. Fault address is below current stack pointer
2. Fault address is within reasonable distance (32 bytes for PUSHA)
3. Fault address is above stack limit

```c
/* In page fault handler */
bool is_stack_growth(void *fault_addr, void *esp) {
  /* Stack grows down, so fault_addr < esp */
  /* Allow 32 bytes below esp (for PUSHA instruction) */
  return fault_addr >= esp - 32 &&
         fault_addr < PHYS_BASE &&
         fault_addr >= STACK_LIMIT;
}

if (is_stack_growth(fault_addr, f->esp)) {
  /* Allocate new stack page */
  spt_create_zero_page(&spt, pg_round_down(fault_addr), true);
  spt_load_page(entry);
}
```

## Testing

### Run All VM Tests

```bash
cd src/vm
make check
```

### Key Test Categories

```bash
# Page table and demand loading
make tests/vm/pt-grow-stack.result
make tests/vm/pt-grow-pusha.result

# Frame eviction
make tests/vm/page-linear.result
make tests/vm/page-shuffle.result

# Memory-mapped files
make tests/vm/mmap-read.result
make tests/vm/mmap-write.result
make tests/vm/mmap-close.result

# Combined stress tests
make tests/vm/page-merge-seq.result
make tests/vm/page-merge-par.result
```

## Common Issues

### TLB Dirty Bit Caching

**Problem**: Hardware dirty bit may be cached in TLB, not visible to kernel.

**Solution**: Use a software `pinned_dirty` flag:
```c
/* Set when loading from swap */
entry->pinned_dirty = true;

/* Check both flags when evicting */
if (pagedir_is_dirty(pd, upage) || entry->pinned_dirty)
  write_to_swap(entry);
```

### Evicting Pages During I/O

**Problem**: Page is evicted while kernel is reading/writing to it.

**Solution**: Pin frames during syscalls that access user memory.

### Stack Growth vs. Invalid Access

**Problem**: Hard to distinguish stack growth from segfault.

**Solution**: Check that fault address is close to ESP (within 32 bytes for PUSHA).

## Next Steps

After completing this project:

- [Project 4: File System](/docs/projects/filesys/overview) - Buffer cache and indexed files
- [Virtual Memory Concepts](/docs/concepts/virtual-memory) - Deep dive into paging theory
- [Page Fault Handling Deep Dive](/docs/deep-dives/page-fault-handling) - Detailed exception flow
