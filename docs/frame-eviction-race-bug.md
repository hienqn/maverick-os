# Frame Eviction Race Condition Bug

## Overview

A race condition in the frame eviction code caused intermittent data corruption in multi-process workloads under memory pressure. This bug manifested as ~30% failure rate in the `page-parallel` test.

## Symptom

```
(child-linear) byte 738112 != 0: FAILED
child-linear: exit(1)
```

The `child-linear` test performs:
1. Allocate 1MB buffer initialized to zeros
2. ARC4 encrypt (in-place XOR transformation)
3. ARC4 decrypt (reverse transformation)
4. Verify all bytes are zero

ARC4 is a stream cipher where each byte depends on all previous cipher state. If ANY byte is corrupted during encrypt/decrypt, the cipher state becomes wrong and all subsequent bytes are incorrectly transformed.

## Root Cause

**Location:** `src/vm/frame.c`, function `frame_evict()`

### The Buggy Code Sequence

```
Timeline of frame_evict() (BEFORE FIX):
───────────────────────────────────────────────────────────────
Step 1: Flush TLB (ensure dirty bit is accurate)
Step 2: Acquire victim's SPT lock
Step 3: Read dirty bit from PTE
Step 4: If dirty, call swap_out(kpage)
        ├── Allocates swap slot
        └── Writes frame content to swap disk

        ╔═══════════════════════════════════════════════════╗
        ║  RACE WINDOW: Page saved to swap but PTE still    ║
        ║  points to it - owner can still read/write!       ║
        ╚═══════════════════════════════════════════════════╝

Step 5: Update SPT entry (status = PAGE_SWAP)
Step 6: Clear PTE ← TOO LATE!
Step 7: Release SPT lock
───────────────────────────────────────────────────────────────
```

### Race Scenario

```
     CPU 0 (Evicting Thread)              CPU 1 (Victim Process)
     ───────────────────────              ────────────────────────
           │                                    │
     swap_out(kpage)                            │
     ├─ Saves frame to swap slot S             │
     └─ Returns                                 │
           │                                    │
           │                              Writes to page
           │                              └─ Goes directly to frame
           │                                 (PTE still valid!)
           │                                    │
     spte->status = PAGE_SWAP                   │
           │                                    │
     pagedir_clear_page()                       │
           │                                    │
     Return frame to allocator                  │
           │                                    │
           │                              Later: touches page again
           │                              └─ PAGE FAULT
           │                                    │
           │                              Load from swap slot S
           │                              └─ Gets OLD data!
           │                                 (Write was lost)
```

## The Fix

**Solution:** Clear the PTE BEFORE `swap_out()` so any access during swap triggers a page fault. The fault handler blocks on SPT lock until eviction completes.

### Fixed Code Sequence

```
Timeline of frame_evict() (AFTER FIX):
───────────────────────────────────────────────────────────────
Step 1: Flush TLB (ensure dirty bit is accurate)
Step 2: Acquire victim's SPT lock
Step 3: Read dirty bit from PTE
Step 4: Clear PTE ← MOVED UP (page now not-present)
Step 5: Flush TLB (ensure CPUs see PTE change)
Step 6: If dirty, call swap_out(kpage) ← NOW SAFE
Step 7: Update SPT entry (status = PAGE_SWAP)
Step 8: Release SPT lock
───────────────────────────────────────────────────────────────
```

Now if the victim touches the page after Step 4:
- Page fault occurs (PTE is clear)
- Fault handler tries to acquire SPT lock
- Blocks until eviction completes (Step 8)
- Then loads from swap with correct data

### Error Path Handling

If `swap_out()` fails after clearing the PTE, we must restore the PTE before continuing to the next frame candidate:

```c
if (swap_slot == SWAP_SLOT_INVALID) {
    /* Swap failed. Restore PTE and try next frame. */
    pagedir_set_page(pd, upage, kpage, writable);
    lock_release(&spt->spt_lock);
    continue;  /* Try next frame */
}
```

## Code Changes

File: `src/vm/frame.c`

**Added after reading dirty bit (before swap_out):**
```c
/* CRITICAL: Clear the PTE BEFORE swap_out to prevent race condition.
   Without this, the owner process can write to the page after swap_out
   saves it but before we clear the PTE, causing those writes to be lost.
   By clearing the PTE first, any access triggers a page fault. The fault
   handler will block on the SPT lock (which we hold) until eviction
   completes, ensuring serialization. */
pagedir_clear_page(pd, upage);

/* Flush TLB to ensure all CPUs see the PTE is now clear. */
uint32_t* orig_pd2 = active_pd();
pagedir_activate(pd);
pagedir_activate(orig_pd2);
```

**Modified error paths:** Added `pagedir_set_page()` to restore PTE if swap fails.

**Removed:** Duplicate `pagedir_clear_page()` that was at the end of the function.

## Why Byte 738112?

```
738112 bytes / 4096 bytes per page = page 180.2
```

This is approximately 70% through the 1MB buffer (256 pages). The consistent offset suggests:
- Eviction happens predictably at this memory pressure point (4 processes, 4MB RAM)
- The clock algorithm reaches this page at a consistent time
- Context switch timing puts the victim in its write loop at this point

## Classification

| Attribute | Value |
|-----------|-------|
| Type | Race condition |
| Severity | Data corruption |
| Reproducibility | ~30% under parallel workload |
| Root cause | Missing synchronization between swap_out and PTE clear |
| Fix complexity | Low (reorder operations + error handling) |

## Key Lessons

1. **Order matters in concurrent systems:** The relative ordering of operations that affect visibility to other threads is critical.

2. **TLB consistency:** Changes to page tables aren't immediately visible to all CPUs. Must flush TLB to ensure consistency.

3. **Hold locks across critical sections:** The SPT lock was being held, but the critical section (swap_out) was happening while the page was still accessible.

4. **Error paths need cleanup:** When failing partway through a multi-step operation, must restore invariants (restore PTE if swap fails).

5. **Latent bugs:** This bug existed but was only exposed when kernel binary size changes affected memory layout timing.

## Verification

After fix: 100/100 page-parallel test runs passed (previously ~70% pass rate).
