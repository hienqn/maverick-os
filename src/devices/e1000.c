/**
 * @file devices/e1000.c
 * @brief Legacy E1000 initialization stub.
 *
 * This file now just calls into the full network stack.
 * The actual E1000 driver is in net/driver/e1000.c.
 *
 * This stub is kept for backward compatibility with existing
 * code that calls e1000_init() from init.c.
 */

#include "devices/e1000.h"

/* This function is now a no-op. Network initialization is handled
 * by net_init() which should be called instead.
 * Kept for backward compatibility. */
void e1000_init(void) { /* Network stack initializes E1000 through net_init() */ }
