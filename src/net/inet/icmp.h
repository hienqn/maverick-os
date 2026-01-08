/**
 * @file net/inet/icmp.h
 * @brief ICMP (Internet Control Message Protocol).
 *
 * Implements ICMP for network diagnostics and error reporting.
 * Primary use case: ping (echo request/reply).
 */

#ifndef NET_INET_ICMP_H
#define NET_INET_ICMP_H

#include <stdint.h>
#include "net/buf/pbuf.h"
#include "net/driver/netdev.h"

/* ICMP types */
#define ICMP_TYPE_ECHO_REPLY 0
#define ICMP_TYPE_DEST_UNREACHABLE 3
#define ICMP_TYPE_ECHO_REQUEST 8
#define ICMP_TYPE_TIME_EXCEEDED 11

/* ICMP codes for Destination Unreachable */
#define ICMP_CODE_NET_UNREACHABLE 0
#define ICMP_CODE_HOST_UNREACHABLE 1
#define ICMP_CODE_PROTO_UNREACHABLE 2
#define ICMP_CODE_PORT_UNREACHABLE 3

/**
 * @brief ICMP header structure.
 */
struct icmp_hdr {
  uint8_t type;      /* ICMP type */
  uint8_t code;      /* ICMP code */
  uint16_t checksum; /* Checksum */
  union {
    struct {
      uint16_t id;    /* Identifier */
      uint16_t seq;   /* Sequence number */
    } echo;           /* Echo request/reply */
    uint32_t gateway; /* Redirect gateway */
    struct {
      uint16_t unused;
      uint16_t mtu;  /* Path MTU */
    } frag;          /* Fragmentation needed */
    uint32_t unused; /* Other types */
  } un;
} __attribute__((packed));

/* ICMP header size */
#define ICMP_HEADER_LEN sizeof(struct icmp_hdr)

/**
 * @brief Initialize ICMP subsystem.
 */
void icmp_init(void);

/**
 * @brief Process received ICMP packet.
 * @param dev Device that received packet.
 * @param p Packet buffer with ICMP data.
 * @param src Source IP address.
 * @param dst Destination IP address.
 *
 * Handles echo requests by sending replies.
 * Consumes the pbuf.
 */
void icmp_input(struct netdev* dev, struct pbuf* p, uint32_t src, uint32_t dst);

/**
 * @brief Send ICMP echo request (ping).
 * @param dev Device to use (NULL for routing).
 * @param dst Destination IP address.
 * @param id Identifier (usually process ID).
 * @param seq Sequence number.
 * @param data Ping data (optional).
 * @param len Data length.
 * @return 0 on success.
 */
int icmp_echo_request(struct netdev* dev, uint32_t dst, uint16_t id, uint16_t seq, const void* data,
                      size_t len);

/**
 * @brief Send ICMP destination unreachable.
 * @param dev Device.
 * @param dst Destination IP of original packet.
 * @param code Unreachable code.
 * @param orig Original IP header + 8 bytes.
 * @param orig_len Length of original data.
 */
void icmp_dest_unreachable(struct netdev* dev, uint32_t dst, uint8_t code, const void* orig,
                           size_t orig_len);

#endif /* NET_INET_ICMP_H */
