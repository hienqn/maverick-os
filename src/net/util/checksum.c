/**
 * @file net/util/checksum.c
 * @brief Internet checksum implementation.
 */

#include "net/util/checksum.h"
#include "net/util/byteorder.h"

uint32_t checksum_partial(const void* data, size_t len, uint32_t sum) {
  const uint8_t* bytes = data;

  /* Sum 16-bit words */
  while (len >= 2) {
    sum += (bytes[0] << 8) | bytes[1];
    bytes += 2;
    len -= 2;
  }

  /* Handle odd byte */
  if (len > 0) {
    sum += bytes[0] << 8;
  }

  return sum;
}

uint16_t checksum_finish(uint32_t sum) {
  /* Fold 32-bit sum to 16 bits */
  while (sum >> 16) {
    sum = (sum & 0xFFFF) + (sum >> 16);
  }

  /* Return one's complement in network byte order */
  return htons(~sum & 0xFFFF);
}

uint16_t checksum(const void* data, size_t len) {
  return checksum_finish(checksum_partial(data, len, 0));
}

uint32_t checksum_pseudo_header(uint32_t src_ip, uint32_t dst_ip, uint8_t protocol, uint16_t len) {
  uint32_t sum = 0;

  /* Add source IP (network order, so we need to swap for our checksum) */
  sum += (ntohl(src_ip) >> 16) & 0xFFFF;
  sum += ntohl(src_ip) & 0xFFFF;

  /* Add destination IP */
  sum += (ntohl(dst_ip) >> 16) & 0xFFFF;
  sum += ntohl(dst_ip) & 0xFFFF;

  /* Add zero + protocol */
  sum += protocol;

  /* Add length */
  sum += len;

  return sum;
}
