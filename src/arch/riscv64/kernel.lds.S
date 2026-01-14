/* arch/riscv64/kernel.lds.S - RISC-V kernel linker script.
 *
 * Memory layout for QEMU virt machine:
 *   0x80000000 - OpenSBI firmware (loaded by QEMU -bios default)
 *   0x80200000 - Kernel load address (OpenSBI jumps here)
 *
 * Virtual memory layout (after paging enabled):
 *   0xFFFFFFFF80000000 - Kernel virtual base (high half)
 *
 * This script is preprocessed, so we can use #include and #define.
 */

/*
 * Note: Cannot use memlayout.h because linker scripts don't understand
 * C-style UL suffixes. Define constants directly here.
 */
#define KERN_BASE_ADDR  0x80200000

OUTPUT_FORMAT("elf64-littleriscv")
OUTPUT_ARCH("riscv")
ENTRY(_start)

SECTIONS
{
    /* Kernel starts at physical 0x80200000 (after OpenSBI) */
    . = KERN_BASE_ADDR;
    _start = .;

    /* Text section: code and read-only data */
    .text : AT(KERN_BASE_ADDR)
    {
        *(.text.entry)    /* Entry point first */
        *(.text .text.*)
    }

    . = ALIGN(4096);
    _end_kernel_text = .;

    .rodata :
    {
        *(.rodata .rodata.*)
        *(.srodata .srodata.*)
    }

    /* Exception handling frames (needed for C++) */
    .eh_frame :
    {
        *(.eh_frame)
    }

    . = ALIGN(4096);

    /* Data section: initialized read-write data */
    .data :
    {
        *(.data .data.*)
        *(.sdata .sdata.*)

        /* Signature for debugging */
        _signature = .;
        QUAD(0xaa55aa55aa55aa55)
    }

    /* Small data section for RISC-V global pointer optimization */
    .sdata :
    {
        __global_pointer$ = . + 0x800;
        *(.sdata .sdata.*)
    }

    . = ALIGN(4096);

    /* BSS section: uninitialized data (zeroed at startup) */
    _start_bss = .;
    .bss :
    {
        *(.bss .bss.*)
        *(.sbss .sbss.*)
        *(COMMON)
    }
    . = ALIGN(4096);
    _end_bss = .;

    _end = .;

    /* Calculate kernel size for sanity check */
    _kernel_size = _end - _start;

    /* Kernel should fit in 4MB for initial development */
    ASSERT(_kernel_size <= 4M, "Kernel image is too big (> 4MB)")

    /* Discard unnecessary sections */
    /DISCARD/ :
    {
        *(.comment)
        *(.note .note.*)
    }
}
