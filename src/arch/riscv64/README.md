# RISC-V 64-bit Architecture Support

This directory will contain RISC-V RV64GC specific code for PintOS.

## Status: Not Yet Implemented

The build system supports `ARCH=riscv64` but the actual implementation
is pending. See `docs/future-plans/RISCV_PORT_PLAN.md` for the full plan.

## Planned Files

| File | Purpose |
|------|---------|
| `start.S` | Entry from OpenSBI, BSS clear, call main() |
| `switch.S` | Thread context switch (s0-s11, ra) |
| `trap.S` | Trap entry/exit, save/restore registers |
| `intr.c` | Trap dispatch, CSR operations |
| `intr.h` | RISC-V intr_frame structure |
| `pagedir.c` | Sv39 3-level page tables |
| `pte.h` | Sv39 PTE format definitions |
| `kernel.lds.S` | ELF64-riscv linker script |
| `csr.h` | CSR read/write macros |
| `sbi.h` | SBI extension definitions |
| `sbi.c` | SBI ecall wrappers |
| `timer.c` | CLINT timer via SBI |
| `plic.c` | PLIC interrupt controller |
| `virtio.c` | VirtIO core (virtqueue) |
| `virtio_blk.c` | VirtIO block device |

## Target Platform

- QEMU `virt` machine
- OpenSBI firmware (M-mode)
- Kernel runs in S-mode (Supervisor)
- Sv39 paging (39-bit virtual addresses, 3-level page tables)
