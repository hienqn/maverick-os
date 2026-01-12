/* RISC-V 64-bit user program linker script for PintOS */
OUTPUT_FORMAT("elf64-littleriscv")
OUTPUT_ARCH(riscv)
ENTRY(_start)

SECTIONS
{
  /* User programs start at a reasonable address.
   * RISC-V Linux uses 0x10000, but PintOS uses higher addresses. */
  __executable_start = 0x08048000;
  . = 0x08048000 + SIZEOF_HEADERS;

  .text : { *(.text .text.*) }
  .rodata : { *(.rodata .rodata.*) }

  /* Adjust the address for the data segment.
   * Align to page boundary. */
  . = ALIGN(0x1000);
  . = DATA_SEGMENT_ALIGN(0x1000, 0x1000);

  .data : { *(.data .data.*) }
  .bss : { *(.bss .bss.*) *(.testEndmem) }

  /* Stabs debugging sections. */
  .stab          0 : { *(.stab) }
  .stabstr       0 : { *(.stabstr) }
  .stab.excl     0 : { *(.stab.excl) }
  .stab.exclstr  0 : { *(.stab.exclstr) }
  .stab.index    0 : { *(.stab.index) }
  .stab.indexstr 0 : { *(.stab.indexstr) }
  .comment       0 : { *(.comment) }

  /* DWARF debug sections. */
  .debug          0 : { *(.debug) }
  .line           0 : { *(.line) }
  .debug_srcinfo  0 : { *(.debug_srcinfo) }
  .debug_sfnames  0 : { *(.debug_sfnames) }
  .debug_aranges  0 : { *(.debug_aranges) }
  .debug_pubnames 0 : { *(.debug_pubnames) }
  .debug_info     0 : { *(.debug_info .gnu.linkonce.wi.*) }
  .debug_abbrev   0 : { *(.debug_abbrev) }
  .debug_line     0 : { *(.debug_line) }
  .debug_frame    0 : { *(.debug_frame) }
  .debug_str      0 : { *(.debug_str) }
  .debug_loc      0 : { *(.debug_loc) }
  .debug_macinfo  0 : { *(.debug_macinfo) }
  .debug_weaknames 0 : { *(.debug_weaknames) }
  .debug_funcnames 0 : { *(.debug_funcnames) }
  .debug_typenames 0 : { *(.debug_typenames) }
  .debug_varnames  0 : { *(.debug_varnames) }

  /DISCARD/ : { *(.note.GNU-stack) *(.eh_frame) *(.riscv.attributes) }
}
