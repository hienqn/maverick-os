/*
 * ╔══════════════════════════════════════════════════════════════════════════╗
 * ║                     FIXED-POINT ARITHMETIC                                ║
 * ╠══════════════════════════════════════════════════════════════════════════╣
 * ║                                                                          ║
 * ║  Fixed-point arithmetic for the MLFQS scheduler. The kernel doesn't      ║
 * ║  have floating-point support, so we use fixed-point for precise          ║
 * ║  calculations like load_avg and recent_cpu.                              ║
 * ║                                                                          ║
 * ║  FORMAT: 16.16 (p.q where p=16 integer bits, q=16 fractional bits)       ║
 * ║  ──────────────────────────────────────────────────────────────────      ║
 * ║                                                                          ║
 * ║    31                16 15                 0                             ║
 * ║    ├─────────────────┼───────────────────┤                               ║
 * ║    │   Integer Part  │  Fractional Part  │                               ║
 * ║    │    (16 bits)    │     (16 bits)     │                               ║
 * ║    └─────────────────┴───────────────────┘                               ║
 * ║                                                                          ║
 * ║  The value represented is: (internal_value) / 2^16                       ║
 * ║                                                                          ║
 * ║  PRECISION:                                                              ║
 * ║  ──────────                                                              ║
 * ║  • Integer range: -32768 to +32767                                       ║
 * ║  • Fractional resolution: 1/65536 ≈ 0.000015                             ║
 * ║                                                                          ║
 * ║  USAGE IN MLFQS:                                                         ║
 * ║  ───────────────                                                         ║
 * ║  • load_avg: System load average (typically 0.0 to ~10.0)                ║
 * ║  • recent_cpu: CPU time used by a thread (can grow large)                ║
 * ║  • Coefficients: 59/60, 1/60 for exponential decay                       ║
 * ║                                                                          ║
 * ║  EXAMPLE:                                                                ║
 * ║  ────────                                                                ║
 * ║    fixed_point_t half = fix_frac(1, 2);    // 0.5                        ║
 * ║    fixed_point_t two = fix_int(2);         // 2.0                        ║
 * ║    fixed_point_t one = fix_mul(half, two); // 1.0                        ║
 * ║    int result = fix_round(one);            // 1                          ║
 * ║                                                                          ║
 * ╚══════════════════════════════════════════════════════════════════════════╝
 */

#ifndef THREADS_FIXED_POINT_H
#define THREADS_FIXED_POINT_H

#include <debug.h>

/* ═══════════════════════════════════════════════════════════════════════════
 * FIXED-POINT FORMAT PARAMETERS
 * ═══════════════════════════════════════════════════════════════════════════*/

#define FIX_BITS 32        /* Total bits per fixed-point number. */
#define FIX_P 16           /* Number of integer bits. */
#define FIX_Q 16           /* Number of fractional bits. */
#define FIX_F (1 << FIX_Q) /* Scaling factor = 2^Q = 65536. */

#define FIX_MIN_INT (-FIX_MAX_INT)     /* Smallest representable integer (-32767). */
#define FIX_MAX_INT ((1 << FIX_P) - 1) /* Largest representable integer (+32767). */

/* ═══════════════════════════════════════════════════════════════════════════
 * FIXED-POINT TYPE
 * ═══════════════════════════════════════════════════════════════════════════*/

/* A fixed-point number.
   Wrapper struct prevents accidental mixing with regular integers. */
typedef struct {
  int f; /* Internal representation: actual_value * FIX_F. */
} fixed_point_t;

/* ═══════════════════════════════════════════════════════════════════════════
 * CONSTRUCTION
 * ═══════════════════════════════════════════════════════════════════════════*/

/* Creates a fixed-point number from raw internal value.
   INTERNAL USE ONLY - prefer fix_int() or fix_frac(). */
static inline fixed_point_t __mk_fix(int f) {
  fixed_point_t x;
  x.f = f;
  return x;
}

/* Converts integer N to fixed-point.
   Example: fix_int(5) represents 5.0 */
static inline fixed_point_t fix_int(int n) {
  ASSERT(n >= FIX_MIN_INT && n <= FIX_MAX_INT);
  return __mk_fix(n * FIX_F);
}

/* Creates fixed-point number representing N/D.
   Example: fix_frac(59, 60) represents 0.9833... */
static inline fixed_point_t fix_frac(int n, int d) {
  ASSERT(d != 0);
  ASSERT(n / d >= FIX_MIN_INT && n / d <= FIX_MAX_INT);
  return __mk_fix((long long)n * FIX_F / d);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * CONVERSION TO INTEGER
 * ═══════════════════════════════════════════════════════════════════════════*/

/* Rounds X to the nearest integer.
   Example: fix_round(1.6) = 2, fix_round(1.4) = 1 */
static inline int fix_round(fixed_point_t x) { return (x.f + FIX_F / 2) / FIX_F; }

/* Truncates X toward zero.
   Example: fix_trunc(1.9) = 1, fix_trunc(-1.9) = -1 */
static inline int fix_trunc(fixed_point_t x) { return x.f / FIX_F; }

/* ═══════════════════════════════════════════════════════════════════════════
 * ARITHMETIC OPERATIONS
 * ═══════════════════════════════════════════════════════════════════════════*/

/* Returns X + Y. */
static inline fixed_point_t fix_add(fixed_point_t x, fixed_point_t y) {
  return __mk_fix(x.f + y.f);
}

/* Returns X - Y. */
static inline fixed_point_t fix_sub(fixed_point_t x, fixed_point_t y) {
  return __mk_fix(x.f - y.f);
}

/* Returns X * Y.
   Uses 64-bit intermediate to prevent overflow. */
static inline fixed_point_t fix_mul(fixed_point_t x, fixed_point_t y) {
  return __mk_fix((long long)x.f * y.f / FIX_F);
}

/* Returns X * N where N is an integer.
   More efficient than fix_mul when multiplying by an integer. */
static inline fixed_point_t fix_scale(fixed_point_t x, int n) {
  ASSERT(n >= 0);
  return __mk_fix(x.f * n);
}

/* Returns X / Y.
   Uses 64-bit intermediate to prevent overflow. */
static inline fixed_point_t fix_div(fixed_point_t x, fixed_point_t y) {
  return __mk_fix((long long)x.f * FIX_F / y.f);
}

/* Returns X / N where N is an integer.
   More efficient than fix_div when dividing by an integer. */
static inline fixed_point_t fix_unscale(fixed_point_t x, int n) {
  ASSERT(n > 0);
  return __mk_fix(x.f / n);
}

/* Returns 1 / X. */
static inline fixed_point_t fix_inv(fixed_point_t x) { return fix_div(fix_int(1), x); }

/* ═══════════════════════════════════════════════════════════════════════════
 * COMPARISON
 * ═══════════════════════════════════════════════════════════════════════════*/

/* Compares X and Y.
   Returns: -1 if X < Y, 0 if X == Y, 1 if X > Y */
static inline int fix_compare(fixed_point_t x, fixed_point_t y) {
  return x.f < y.f ? -1 : x.f > y.f;
}

#endif /* threads/fixed-point.h */
