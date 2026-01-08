/**
 * @file net/link/ethernet.h
 * @brief Ethernet frame handling.
 *
 * Handles Ethernet frame parsing and construction.
 * Demultiplexes received frames to appropriate protocol handlers.
 */

#ifndef NET_LINK_ETHERNET_H
#define NET_LINK_ETHERNET_H

#include <stdint.h>
#include "net/buf/pbuf.h"
#include "net/driver/netdev.h"

/* Ethernet constants */
#define ETH_ADDR_LEN 6    /* MAC address length */
#define ETH_HEADER_LEN 14 /* Ethernet header size */
#define ETH_MIN_LEN 60    /* Minimum frame size (no CRC) */
#define ETH_MAX_LEN 1514  /* Maximum frame size (no CRC) */
#define ETH_MTU 1500      /* Maximum payload */

/* EtherTypes (network byte order) */
#define ETH_TYPE_IP 0x0800
#define ETH_TYPE_ARP 0x0806
#define ETH_TYPE_IPV6 0x86DD

/* Broadcast MAC address */
extern const uint8_t eth_broadcast_addr[ETH_ADDR_LEN];

/**
 * @brief Ethernet frame header.
 */
struct eth_hdr {
  uint8_t dest[ETH_ADDR_LEN]; /* Destination MAC address */
  uint8_t src[ETH_ADDR_LEN];  /* Source MAC address */
  uint16_t type;              /* EtherType (network order) */
} __attribute__((packed));

/**
 * @brief Initialize Ethernet layer.
 */
void ethernet_init(void);

/**
 * @brief Process received Ethernet frame.
 * @param dev Device that received the frame.
 * @param p Packet buffer containing frame.
 *
 * Parses header and dispatches to IP or ARP handler.
 * Consumes (frees) the pbuf.
 */
void ethernet_input(struct netdev* dev, struct pbuf* p);

/**
 * @brief Send packet over Ethernet.
 * @param dev Device to send on.
 * @param p Packet buffer (payload already set up).
 * @param dest Destination MAC address.
 * @param type EtherType (host order).
 * @return 0 on success, negative on error.
 *
 * Prepends Ethernet header and transmits.
 * Consumes the pbuf on success.
 */
int ethernet_output(struct netdev* dev, struct pbuf* p, const uint8_t* dest, uint16_t type);

/**
 * @brief Check if MAC address is broadcast.
 */
int eth_is_broadcast(const uint8_t* addr);

/**
 * @brief Check if MAC address matches device or is broadcast.
 */
int eth_addr_matches(const uint8_t* addr, const uint8_t* dev_addr);

/**
 * @brief Format MAC address as string.
 * @param addr MAC address.
 * @param buf Output buffer (at least 18 bytes).
 * @return buf pointer.
 */
char* eth_addr_to_str(const uint8_t* addr, char* buf);

#endif /* NET_LINK_ETHERNET_H */
