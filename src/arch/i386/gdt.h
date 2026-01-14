/* arch/i386/gdt.h - x86 Global Descriptor Table definitions.
 *
 * x86 segmentation requires GDT for privilege separation.
 * RISC-V does not have segmentation (uses page table U bit instead).
 */

#ifndef ARCH_I386_GDT_H
#define ARCH_I386_GDT_H

#include "threads/loader.h"

/* Segment selectors.
   More selectors are defined by the loader in loader.h. */
#define SEL_UCSEG 0x1B /* User code selector. */
#define SEL_UDSEG 0x23 /* User data selector. */
#define SEL_TSS 0x28   /* Task-state segment. */
#define SEL_CNT 6      /* Number of segments. */

void gdt_init(void);

#endif /* ARCH_I386_GDT_H */
