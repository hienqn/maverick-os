# RISC-V 64-bit Architecture Support

This directory contains RISC-V RV64GC specific code for PintOS.

## Current Status: Partial Implementation

The RISC-V port boots and runs basic thread tests. Priority scheduling and
user programs are not yet functional.

### What Works

| Component | Status | Notes |
|-----------|--------|-------|
| Boot/Init | ✅ Working | OpenSBI -> kernel boot sequence |
| Console | ✅ Working | SBI console output |
| Timer | ✅ Working | SBI timer interrupts at 100Hz |
| Threading | ✅ Working | Basic thread create/switch/exit |
| FIFO Scheduler | ✅ Working | Round-robin scheduling |
| Memory (Sv39) | ✅ Working | 3-level page tables, kernel mapping |
| Test Framework | ✅ Working | `make check ARCH=riscv64` runs tests |

### What's Missing / Not Working

| Component | Status | What's Needed |
|-----------|--------|---------------|
| Priority Scheduler | ❌ Not implemented | Port `SCHED_PRIO` logic to RISC-V |
| MLFQS Scheduler | ❌ Not implemented | Port `SCHED_MLFQS` logic |
| SMFS Scheduler | ❌ Not implemented | Port `SCHED_SMFS` logic |
| Fair Scheduler | ❌ Not implemented | Port `SCHED_FAIR` logic |
| User Programs | ❌ Not implemented | User-mode trap handling, syscalls |
| VirtIO Block | ⚠️ Partial | Driver exists but untested with tests |
| Filesystem | ❌ Blocked | Needs working VirtIO block device |
| VM (demand paging) | ❌ Not implemented | Page fault handling for user pages |
| Swap | ❌ Blocked | Needs VirtIO block |

## Running Tests

### Build for RISC-V
```bash
cd src/threads
make clean
make ARCH=riscv64
```

### Run All Tests
```bash
cd src/threads/build
make check ARCH=riscv64
```

### Run Single Test
```bash
# Using pintos directly
../../utils/bun/bin/pintos --arch=riscv64 -v -k -T 30 -- rtkt alarm-single

# Or with QEMU directly
qemu-system-riscv64 -machine virt -bios default -nographic \
  -kernel kernel.bin -append "rtkt alarm-single"
```

### Test Results (threads project)

As of the current implementation:
- **Passing**: alarm-*, basic threading tests (~5 tests)
- **Failing**: priority-*, mlfqs-*, smfs-*, fair-* (scheduler not implemented)

## Implementation Files

| File | Purpose | Status |
|------|---------|--------|
| `start.S` | Entry from OpenSBI, BSS clear, call main() | ✅ Done |
| `switch.S` | Thread context switch (s0-s11, ra, sp) | ✅ Done |
| `trap.S` | Trap entry/exit, save/restore registers | ✅ Done |
| `init.c` | Kernel initialization, boot sequence | ✅ Done |
| `intr.c` | Trap dispatch, interrupt handling | ✅ Done |
| `intr.h` | RISC-V intr_frame structure | ✅ Done |
| `pagedir.c` | Sv39 3-level page tables | ✅ Done |
| `pte.h` | Sv39 PTE format definitions | ✅ Done |
| `kernel.lds.S` | ELF64-riscv linker script | ✅ Done |
| `csr.h` | CSR read/write macros | ✅ Done |
| `sbi.h/c` | SBI ecall wrappers | ✅ Done |
| `timer.c` | SBI timer implementation | ✅ Done |
| `plic.c` | PLIC interrupt controller | ✅ Done |
| `virtio.c` | VirtIO core (virtqueue) | ✅ Done |
| `virtio-blk.c` | VirtIO block device | ⚠️ Untested |
| `userprog.c` | User program support stubs | ❌ Incomplete |
| `mmu.c` | MMU setup and utilities | ✅ Done |

## Target Platform

- QEMU `virt` machine
- OpenSBI firmware (M-mode)
- Kernel runs in S-mode (Supervisor)
- Sv39 paging (39-bit virtual addresses, 3-level page tables)
- 128MB+ RAM recommended

## Architecture Differences from i386

| Aspect | i386 | RISC-V |
|--------|------|--------|
| Word size | 32-bit | 64-bit |
| Boot | BIOS -> loader.bin | OpenSBI -> kernel.bin |
| Paging | 2-level (32-bit) | 3-level Sv39 (39-bit) |
| Interrupts | PIC/APIC | PLIC + SBI |
| Timer | PIT | SBI timer |
| Syscalls | INT 0x30 | ECALL |
| Disks | IDE | VirtIO |
| Serial | 8250 UART | SBI console |

## Next Steps to Complete RISC-V Port

1. **Priority Scheduler**: Implement `active_sched_policy` handling in RISC-V
   - Currently all tests using `-sched=prio` fail
   - Need to port scheduler selection from i386

2. **VirtIO Block Testing**: Verify disk I/O works
   - Driver code exists but needs integration testing
   - Required for filesystem and swap tests

3. **User Program Support**:
   - Implement user-mode trap handling in `trap.S`
   - Port syscall argument passing (a0-a5 registers)
   - Set up user page tables

4. **Demand Paging**:
   - Handle page faults for user pages
   - Integrate with SPT (supplemental page table)
