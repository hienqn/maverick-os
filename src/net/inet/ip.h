/**
 * @file net/inet/ip.h
 * @brief IPv4 protocol implementation.
 *
 * Handles IPv4 packet processing including:
 * - Header parsing and validation
 * - Checksum verification
 * - Routing decisions
 * - Fragmentation (future)
 */

#ifndef NET_INET_IP_H
#define NET_INET_IP_H

#include <stdint.h>
#include "net/buf/pbuf.h"
#include "net/driver/netdev.h"

/* IP version */
#define IP_VERSION 4

/* IP header length (minimum, in 32-bit words) */
#define IP_HLEN_MIN 5

/* IP protocol numbers */
#define IP_PROTO_ICMP 1
#define IP_PROTO_TCP 6
#define IP_PROTO_UDP 17

/* IP flags */
#define IP_FLAG_DF 0x4000     /* Don't Fragment */
#define IP_FLAG_MF 0x2000     /* More Fragments */
#define IP_FRAG_OFFSET 0x1FFF /* Fragment offset mask */

/* Default TTL */
#define IP_DEFAULT_TTL 64

/**
 * @brief IPv4 header structure.
 */
struct ip_hdr {
  uint8_t version_ihl; /* Version (4 bits) + IHL (4 bits) */
  uint8_t tos;         /* Type of Service */
  uint16_t tot_len;    /* Total length (header + data) */
  uint16_t id;         /* Identification */
  uint16_t frag_off;   /* Fragment offset + flags */
  uint8_t ttl;         /* Time to Live */
  uint8_t protocol;    /* Protocol (TCP, UDP, ICMP, etc.) */
  uint16_t checksum;   /* Header checksum */
  uint32_t src_addr;   /* Source IP address */
  uint32_t dst_addr;   /* Destination IP address */
  /* Options may follow */
} __attribute__((packed));

/* Minimum IP header size */
#define IP_HEADER_LEN sizeof(struct ip_hdr)

/* Macros to extract IP header fields */
#define IP_HDR_VERSION(hdr) (((hdr)->version_ihl >> 4) & 0xF)
#define IP_HDR_IHL(hdr) ((hdr)->version_ihl & 0xF)
#define IP_HDR_HLEN(hdr) (IP_HDR_IHL(hdr) * 4)

/**
 * @brief Initialize IP layer.
 */
void ip_init(void);

/**
 * @brief Process received IP packet.
 * @param dev Device that received packet.
 * @param p Packet buffer containing IP datagram.
 *
 * Validates header and dispatches to protocol handler.
 * Consumes the pbuf.
 */
void ip_input(struct netdev* dev, struct pbuf* p);

/**
 * @brief Send IP packet.
 * @param dev Device to send on (NULL for routing lookup).
 * @param p Packet buffer with payload.
 * @param src Source IP address (0 for auto).
 * @param dst Destination IP address.
 * @param protocol Protocol number (ICMP, TCP, UDP).
 * @param ttl Time to live (0 for default).
 * @return 0 on success, negative on error.
 *
 * Builds IP header and handles routing/ARP.
 */
int ip_output(struct netdev* dev, struct pbuf* p, uint32_t src, uint32_t dst, uint8_t protocol,
              uint8_t ttl);

/**
 * @brief Send IP packet with full header control.
 * @param dev Device to send on.
 * @param p Packet buffer with IP header already prepended.
 * @param dst_mac Destination MAC address.
 * @return 0 on success.
 */
int ip_output_hdr(struct netdev* dev, struct pbuf* p, const uint8_t* dst_mac);

/**
 * @brief Check if IP address is for this host.
 * @param ip IP address to check.
 * @return true if address is local.
 */
int ip_is_local(uint32_t ip);

/**
 * @brief Check if IP address is broadcast.
 */
int ip_is_broadcast(uint32_t ip, struct netdev* dev);

/**
 * @brief Format IP address as string.
 * @param ip IP address (network order).
 * @param buf Output buffer (at least 16 bytes).
 * @return buf pointer.
 */
char* ip_addr_to_str(uint32_t ip, char* buf);

/**
 * @brief Parse IP address from string.
 * @param str String in dotted decimal format.
 * @return IP address in network order, or 0 on error.
 */
uint32_t ip_addr_from_str(const char* str);

#endif /* NET_INET_IP_H */
