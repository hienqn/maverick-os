/* Checks stack pointer alignment at main entry. */

#include "tests/lib.h"
#include <stdint.h>

int main(int argc UNUSED, char* argv[] UNUSED) {
#ifdef ARCH_I386
  register unsigned int sp_val asm("esp");
#elif defined(ARCH_RISCV64)
  register uintptr_t sp_val asm("sp");
#else
#error "No architecture defined"
#endif
  return sp_val % 16;
}
