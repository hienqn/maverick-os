/**
 * @file net/util/byteorder.h
 * @brief Network byte order conversion utilities.
 *
 * Network protocols use big-endian (network) byte order.
 * x86 uses little-endian (host) byte order.
 * These macros convert between them.
 */

#ifndef NET_UTIL_BYTEORDER_H
#define NET_UTIL_BYTEORDER_H

#include <stdint.h>

/**
 * @brief Convert 16-bit value from host to network byte order.
 */
static inline uint16_t htons(uint16_t x) { return ((x & 0xFF) << 8) | ((x >> 8) & 0xFF); }

/**
 * @brief Convert 16-bit value from network to host byte order.
 */
static inline uint16_t ntohs(uint16_t x) { return htons(x); /* Same operation */ }

/**
 * @brief Convert 32-bit value from host to network byte order.
 */
static inline uint32_t htonl(uint32_t x) {
  return ((x & 0xFF) << 24) | ((x & 0xFF00) << 8) | ((x >> 8) & 0xFF00) | ((x >> 24) & 0xFF);
}

/**
 * @brief Convert 32-bit value from network to host byte order.
 */
static inline uint32_t ntohl(uint32_t x) { return htonl(x); /* Same operation */ }

#endif /* NET_UTIL_BYTEORDER_H */
