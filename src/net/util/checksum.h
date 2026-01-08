/**
 * @file net/util/checksum.h
 * @brief Internet checksum calculation.
 *
 * The Internet checksum is the 16-bit one's complement of the one's complement
 * sum of all 16-bit words in the data. Used by IP, TCP, UDP, and ICMP.
 */

#ifndef NET_UTIL_CHECKSUM_H
#define NET_UTIL_CHECKSUM_H

#include <stdint.h>
#include <stddef.h>

/**
 * @brief Compute partial checksum over a buffer.
 * @param data Pointer to data.
 * @param len Length in bytes.
 * @param sum Initial/accumulated sum (pass 0 to start fresh).
 * @return Accumulated checksum (not yet folded or complemented).
 *
 * Call multiple times to checksum non-contiguous data, then
 * call checksum_finish() on the final sum.
 */
uint32_t checksum_partial(const void* data, size_t len, uint32_t sum);

/**
 * @brief Fold and complement a partial checksum to get final value.
 * @param sum Accumulated checksum from checksum_partial().
 * @return Final 16-bit checksum in network byte order.
 */
uint16_t checksum_finish(uint32_t sum);

/**
 * @brief Compute complete checksum over a single buffer.
 * @param data Pointer to data.
 * @param len Length in bytes.
 * @return 16-bit checksum in network byte order.
 */
uint16_t checksum(const void* data, size_t len);

/**
 * @brief Compute TCP/UDP pseudo-header checksum.
 * @param src_ip Source IP address (network order).
 * @param dst_ip Destination IP address (network order).
 * @param protocol IP protocol number.
 * @param len TCP/UDP length (host order).
 * @return Partial checksum of pseudo-header.
 */
uint32_t checksum_pseudo_header(uint32_t src_ip, uint32_t dst_ip, uint8_t protocol, uint16_t len);

#endif /* NET_UTIL_CHECKSUM_H */
