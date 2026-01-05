---
sidebar_position: 4
---

import AnimatedFlow from '@site/src/components/AnimatedFlow';
import MemoryLayout from '@site/src/components/MemoryLayout';
import InteractiveDiagram from '@site/src/components/InteractiveDiagram';

# Virtual Memory

Virtual memory is an abstraction that gives each process the illusion of having its own large, contiguous address space, while the actual physical memory is shared and managed by the kernel.

## Why Virtual Memory?

| Problem | Solution |
|---------|----------|
| Programs assume fixed addresses | Each process gets its own address space |
| Programs larger than physical RAM | Demand paging loads only needed pages |
| Processes shouldn't access each other | Hardware-enforced isolation |
| Memory fragmentation | Virtual addresses can map to any physical frame |

## Address Translation

<InteractiveDiagram
  title="Virtual to Physical Address Translation"
  nodes={[
    { id: 'vaddr', label: 'Virtual Address', x: 100, y: 50, description: '32-bit address from CPU' },
    { id: 'vpn', label: 'Virtual Page Number', x: 100, y: 150, description: 'Upper 20 bits' },
    { id: 'offset', label: 'Page Offset', x: 300, y: 150, description: 'Lower 12 bits (unchanged)' },
    { id: 'pgtbl', label: 'Page Table', x: 100, y: 250, description: 'Maps VPN to PFN' },
    { id: 'pfn', label: 'Physical Frame Number', x: 100, y: 350, description: 'Upper 20 bits of physical' },
    { id: 'paddr', label: 'Physical Address', x: 200, y: 450, description: 'PFN + Offset' },
  ]}
  edges={[
    { from: 'vaddr', to: 'vpn', label: 'split' },
    { from: 'vaddr', to: 'offset', label: 'split' },
    { from: 'vpn', to: 'pgtbl', label: 'index' },
    { from: 'pgtbl', to: 'pfn', label: 'lookup' },
    { from: 'pfn', to: 'paddr', label: 'combine' },
    { from: 'offset', to: 'paddr', label: 'combine' },
  ]}
/>

### Address Breakdown (32-bit x86)

```
Virtual Address (32 bits):
┌────────────────────────┬────────────────┐
│  Virtual Page Number   │  Page Offset   │
│      (20 bits)         │   (12 bits)    │
└────────────────────────┴────────────────┘
         ↓                      │
    Page Table                  │
         ↓                      │
┌────────────────────────┬──────┴─────────┐
│  Physical Frame Number │  Page Offset   │
│      (20 bits)         │   (12 bits)    │
└────────────────────────┴────────────────┘
Physical Address (32 bits)
```

## Page Tables

### Two-Level Page Table (x86)

x86 uses a two-level page table to save memory:

```
┌──────────────────┬──────────────────┬────────────┐
│ Page Directory   │ Page Table Index │   Offset   │
│   Index (10)     │     (10)         │   (12)     │
└────────┬─────────┴────────┬─────────┴────────────┘
         │                  │
         ▼                  │
    ┌─────────┐             │
    │   PDE   │──────►┌─────┴───┐
    │   PDE   │       │   PTE   │──────► Frame
    │   PDE   │       │   PTE   │
    │   ...   │       │   ...   │
    └─────────┘       └─────────┘
    Page Directory    Page Table
    (1024 entries)    (1024 entries)
```

### Page Table Entry Flags

| Bit | Name | Meaning |
|-----|------|---------|
| 0 | Present | Page is in memory |
| 1 | Writable | Page can be written |
| 2 | User | User mode can access |
| 5 | Accessed | Page was read |
| 6 | Dirty | Page was written |

## Demand Paging

Instead of loading all pages at startup, load them on first access:

<AnimatedFlow
  title="Page Fault Handling"
  states={[
    { id: 'access', label: 'Memory Access', description: 'CPU tries to access address' },
    { id: 'fault', label: 'Page Fault', description: 'Present bit is 0' },
    { id: 'lookup', label: 'SPT Lookup', description: 'Find page metadata' },
    { id: 'alloc', label: 'Frame Alloc', description: 'Get physical frame' },
    { id: 'load', label: 'Load Data', description: 'From file/swap/zeros' },
    { id: 'install', label: 'Install PTE', description: 'Update page table' },
    { id: 'retry', label: 'Retry Access', description: 'Instruction re-executed' },
  ]}
  transitions={[
    { from: 'access', to: 'fault', label: 'not present' },
    { from: 'fault', to: 'lookup', label: '' },
    { from: 'lookup', to: 'alloc', label: 'found' },
    { from: 'alloc', to: 'load', label: '' },
    { from: 'load', to: 'install', label: '' },
    { from: 'install', to: 'retry', label: 'IRET' },
    { from: 'retry', to: 'access', label: '' },
  ]}
/>

### Benefits of Demand Paging

1. **Faster startup**: Only load pages as needed
2. **Memory savings**: Unused code never loaded
3. **Larger programs**: Can run programs > physical memory
4. **Copy-on-write**: Share pages until modified

## Page Replacement

When physical memory is full, evict a page to make room.

### Clock (Second-Chance) Algorithm

```
       clock_hand
           ↓
Frames: [ A ] → [ B ] → [ C ] → [ D ] → ...
        acc=1   acc=0   acc=1   acc=0

Algorithm:
1. Check frame at clock_hand
2. If accessed bit set:
   - Clear it (second chance)
   - Move clock_hand forward
   - Goto 1
3. If accessed bit clear:
   - Evict this frame
   - Return it to caller
```

### Eviction Steps

```c
void *evict_frame(void) {
  struct frame_entry *victim = clock_select_victim();

  /* 1. Write out if dirty */
  if (is_dirty(victim)) {
    if (victim->is_mmap)
      write_to_file(victim);  /* mmap page */
    else
      write_to_swap(victim);  /* regular page */
  }

  /* 2. Update supplemental page table */
  victim->spt_entry->status = PAGE_SWAP;
  victim->spt_entry->swap_slot = slot;

  /* 3. Clear page table entry */
  pagedir_clear_page(victim->pd, victim->upage);

  /* 4. Return freed frame */
  return victim->kpage;
}
```

## Address Space Layout

<MemoryLayout
  title="32-bit Process Address Space"
  regions={[
    {
      name: 'Kernel Space',
      size: '0xC0000000 - 0xFFFFFFFF',
      color: '#ef4444',
      description: 'Kernel code, data, and mappings (inaccessible to user)'
    },
    {
      name: 'PHYS_BASE',
      size: '0xC0000000',
      color: '#6b7280',
      description: 'Boundary between user and kernel space'
    },
    {
      name: 'User Stack',
      size: 'Grows downward',
      color: '#f59e0b',
      description: 'Function frames, local variables'
    },
    {
      name: 'mmap Region',
      size: 'Variable',
      color: '#10b981',
      description: 'Memory-mapped files'
    },
    {
      name: 'Heap',
      size: 'Grows upward',
      color: '#3b82f6',
      description: 'Dynamic allocation (malloc)'
    },
    {
      name: 'BSS',
      size: 'Variable',
      color: '#8b5cf6',
      description: 'Uninitialized globals (zero-filled)'
    },
    {
      name: 'Data',
      size: 'Variable',
      color: '#ec4899',
      description: 'Initialized global variables'
    },
    {
      name: 'Text (Code)',
      size: '0x08048000+',
      color: '#14b8a6',
      description: 'Executable instructions (read-only)'
    },
    {
      name: 'Reserved',
      size: '0x00000000 - 0x08048000',
      color: '#6b7280',
      description: 'NULL pointer trap, etc.'
    },
  ]}
/>

## Swap Space

When physical memory is full, evicted pages go to a swap partition:

```c
/* Swap out a page */
size_t swap_out(void *kpage) {
  /* Find free slot in swap bitmap */
  size_t slot = bitmap_scan_and_flip(swap_bitmap, 0, 1, false);

  /* Write page to swap (8 sectors per page) */
  block_sector_t sector = slot * SECTORS_PER_PAGE;
  for (int i = 0; i < SECTORS_PER_PAGE; i++) {
    block_write(swap_device, sector + i,
                kpage + i * BLOCK_SECTOR_SIZE);
  }

  return slot;
}

/* Swap in a page */
void swap_in(size_t slot, void *kpage) {
  block_sector_t sector = slot * SECTORS_PER_PAGE;
  for (int i = 0; i < SECTORS_PER_PAGE; i++) {
    block_read(swap_device, sector + i,
               kpage + i * BLOCK_SECTOR_SIZE);
  }

  /* Free the swap slot */
  bitmap_reset(swap_bitmap, slot);
}
```

## Copy-on-Write (COW)

Optimization for `fork()`: share pages until one process writes.

```
Before fork():
  Parent: [Page A] → Frame 1

After fork():
  Parent: [Page A] ──┐
                     ├──► Frame 1 (read-only, ref_count=2)
  Child:  [Page A] ──┘

After child writes to Page A:
  Parent: [Page A] ──► Frame 1 (ref_count=1)
  Child:  [Page A] ──► Frame 2 (copy of Frame 1)
```

### COW Implementation

```c
/* Mark page as COW during fork */
void mark_cow(void *upage, uint32_t *pd) {
  pagedir_set_writable(pd, upage, false);
  spt_entry->status = PAGE_COW;
  frame_share(kpage);  /* Increment ref count */
}

/* Handle write fault on COW page */
bool handle_cow_fault(struct spt_entry *entry) {
  /* Allocate new frame */
  void *new_frame = frame_alloc(entry->upage, true);

  /* Copy contents */
  memcpy(new_frame, entry->kpage, PGSIZE);

  /* Release reference to old frame */
  frame_unshare(entry->kpage);

  /* Update entry to point to new frame */
  entry->kpage = new_frame;
  entry->status = PAGE_FRAME;

  /* Make writable */
  pagedir_set_page(pd, entry->upage, new_frame, true);

  return true;
}
```

## TLB (Translation Lookaside Buffer)

The TLB caches recent address translations for speed:

```
Virtual Address
      │
      ▼
  ┌───────┐  hit    ┌─────────────┐
  │  TLB  │────────►│ Phys Address │
  └───┬───┘         └─────────────┘
      │ miss
      ▼
  ┌────────────┐
  │ Page Table │
  │  Walk      │
  └──────┬─────┘
         ▼
    Fill TLB
```

### TLB Management

When a page table entry changes, the TLB must be invalidated:

```c
/* After unmapping a page */
pagedir_clear_page(pd, upage);
/* TLB entry automatically invalidated by x86 */

/* After context switch to different process */
/* Loading CR3 flushes entire TLB */
```

## Summary

| Concept | Purpose |
|---------|---------|
| **Virtual Address Space** | Isolation between processes |
| **Page Tables** | Map virtual to physical addresses |
| **Demand Paging** | Load pages only when accessed |
| **Page Replacement** | Make room when memory is full |
| **Swap Space** | Disk storage for evicted pages |
| **Copy-on-Write** | Efficient process forking |
| **TLB** | Cache translations for speed |

## Related Topics

- [Project 3: Virtual Memory](/docs/projects/vm/overview) - Implement VM subsystem
- [Page Fault Handling](/docs/deep-dives/page-fault-handling) - Detailed fault handler walkthrough
