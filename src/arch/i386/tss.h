/* arch/i386/tss.h - x86 Task State Segment definitions.
 *
 * TSS is required for ring transitions (user<->kernel).
 * RISC-V handles this via sscratch CSR instead.
 */

#ifndef ARCH_I386_TSS_H
#define ARCH_I386_TSS_H

#include <stdint.h>

struct tss;
void tss_init(void);
struct tss* tss_get(void);
void tss_update(void);

#endif /* ARCH_I386_TSS_H */
