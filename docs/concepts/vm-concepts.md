# Virtual Memory Concepts

## Paging vs Virtual Memory

| Feature | Paging | Virtual Memory |
|---------|--------|----------------|
| Address translation | Yes | Yes |
| Process isolation | Yes | Yes |
| Solves fragmentation | Yes | Yes |
| All pages must fit in RAM | **Yes** | **No** |
| Pages can live on disk | No | Yes |
| Demand loading | No | Yes |

**Key insight:** Paging is the *mechanism* (MMU, page tables). Virtual memory is the *abstraction* that creates the illusion of unlimited memory.

---

## The Universal VM Pattern

All VM features follow the same pattern:

```
1. Don't do work upfront
2. Set a "trap" (page not present, or read-only)
3. Record HOW to do the work later (SPT entry)
4. Wait for page fault
5. Do the work just-in-time
6. Process never knows anything happened
```

The **SPT entry** is the "recipe" that tells the fault handler how to materialize the page.

---

## Feature: Demand Paging

**Problem:** Loading entire executables upfront is slow and wasteful.

**Solution:** Load pages only when accessed.

```
Eager Loading:                  Demand Paging:
─────────────                   ──────────────
Load 100 pages                  Create 100 SPT entries
Start program                   Start program immediately
(2 seconds)                     (instant)

                                Access page 7 → fault → load
                                Access page 12 → fault → load
                                (only load ~30 pages actually used)
```

**SPT Entry:**
```
status = PAGE_FILE
file = executable
offset = 0x1000
read_bytes = 4096
```

---

## Feature: Copy-on-Write (COW)

**Problem:** fork() copying all pages is slow, especially when child calls exec() immediately.

**Solution:** Share pages until someone writes.

```
fork()
  │
  ▼
Both processes share same physical frames
Mark all pages READ-ONLY in both page tables
  │
  ▼
First write → PAGE FAULT
  │
  ▼
Copy that one page
Give writer the copy
Mark it writable
```

**The trick:** Use read-only permission as a "trip wire" to detect writes.

---

## Feature: Memory-Mapped Files (mmap)

**Problem:** Reading large files into buffers wastes memory and time.

**Solution:** Map file directly into address space.

```c
// Instead of:
buffer = malloc(size);
read(fd, buffer, size);

// Do:
ptr = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
// ptr IS the file - no copying
```

**SPT Entry:**
```
status = PAGE_FILE
file = data.bin
offset = 0
read_bytes = 4096
```

Same machinery as demand paging, just backed by user files instead of executables.

---

## Feature: Stack Growth

**Problem:** Unknown stack size at process start.

**Solution:** Start small, grow on demand.

```
PHYS_BASE
─────────
    │ ← Stack starts (1 page)
    ▼

    Access below mapped region
    → PAGE FAULT
    → Is fault_addr >= ESP - 32?
    → Yes: allocate zero page, map it
    → No: SEGFAULT
```

**Why 32 bytes?** The PUSHA instruction pushes 32 bytes before ESP moves.

**Limit:** Maximum stack size (typically 8MB).

---

## Feature: Swap

**Problem:** More virtual pages than physical frames.

**Solution:** Use disk as overflow storage.

```
RAM full, need new frame
  │
  ▼
Pick victim (clock algorithm)
  │
  ▼
If dirty: write to swap disk
  │
  ▼
Update victim's SPT: status = PAGE_SWAP, slot = N
  │
  ▼
Reclaim frame for new page
```

**SPT Entry (after eviction):**
```
status = PAGE_SWAP
swap_slot = 42
```

---

## Page Fault Costs

| Feature | Fault Type | Disk I/O | Cost |
|---------|-----------|:--------:|------|
| Zero page | Minor | No | ~1-10 us |
| Stack growth | Minor | No | ~1-10 us |
| COW | Minor | No | ~1-10 us |
| Demand paging | Major | Yes | ~1-10 ms |
| Swap in | Major | Yes | ~1-10 ms |
| mmap | Major | Yes | ~1-10 ms |

**Rule of thumb:** If data comes from disk, it's 1000x slower.

---

## Module Responsibilities

| Module | Single Responsibility | Scope |
|--------|----------------------|-------|
| `page.c` | Track virtual page metadata (SPT) | Per-process |
| `frame.c` | Track physical frame ownership | Global |
| `swap.c` | Read/write pages to disk | Global |
| `vm.c` | Coordinate page fault handling | Entry point |

```
Page fault occurs
       │
       ▼
    vm.c ─────► "Where is this page?"
       │
       ▼
   page.c ────► "PAGE_FILE at offset X" (or SWAP, ZERO, etc.)
       │
       ▼
  frame.c ────► "Here's a frame" (may trigger eviction)
       │
       ▼
   swap.c ────► (only if evicting dirty page or loading from swap)
       │
       ▼
  Install in page table, resume process
```

---

## Key Takeaways

1. **Page faults are features, not bugs** - They're the mechanism for lazy work.

2. **Indirection enables laziness** - SPT entries describe "how to get data" without having the data.

3. **One pattern, many applications** - Demand paging, COW, mmap, stack growth all use the same fault-based mechanism.

4. **Trade-off: fault overhead vs avoided work** - Faults are expensive, but skipping unused pages is worth it.

5. **Disk I/O dominates cost** - Minor faults (memory-only) are 1000x faster than major faults (disk).
