/**
 * @file net/link/arp.h
 * @brief Address Resolution Protocol (ARP).
 *
 * ARP maps IPv4 addresses to Ethernet MAC addresses.
 * Maintains a cache of recent resolutions.
 *
 * OPERATION:
 * 1. When IP needs to send to an address, it calls arp_resolve()
 * 2. If MAC is in cache, return immediately
 * 3. If not, send ARP request and queue the packet
 * 4. When reply arrives, send queued packets
 */

#ifndef NET_LINK_ARP_H
#define NET_LINK_ARP_H

#include <stdint.h>
#include <stdbool.h>
#include "net/buf/pbuf.h"
#include "net/driver/netdev.h"

/* ARP constants */
#define ARP_HWTYPE_ETHERNET 1
#define ARP_PROTO_IP 0x0800

/* ARP opcodes */
#define ARP_OP_REQUEST 1
#define ARP_OP_REPLY 2

/* ARP cache configuration */
#define ARP_CACHE_SIZE 32
#define ARP_CACHE_TIMEOUT (300 * 100) /* 300 seconds in ticks */
#define ARP_MAX_PENDING 4             /* Max packets queued per entry */

/**
 * @brief ARP packet header.
 */
struct arp_hdr {
  uint16_t hw_type;      /* Hardware type (1 = Ethernet) */
  uint16_t proto_type;   /* Protocol type (0x0800 = IPv4) */
  uint8_t hw_len;        /* Hardware address length (6) */
  uint8_t proto_len;     /* Protocol address length (4) */
  uint16_t opcode;       /* Operation (request/reply) */
  uint8_t sender_mac[6]; /* Sender hardware address */
  uint32_t sender_ip;    /* Sender protocol address */
  uint8_t target_mac[6]; /* Target hardware address */
  uint32_t target_ip;    /* Target protocol address */
} __attribute__((packed));

/* ARP header size */
#define ARP_HEADER_LEN sizeof(struct arp_hdr)

/**
 * @brief ARP cache entry state.
 */
enum arp_state {
  ARP_STATE_EMPTY,   /* Entry not in use */
  ARP_STATE_PENDING, /* Request sent, waiting for reply */
  ARP_STATE_VALID    /* Entry is valid */
};

/**
 * @brief Initialize ARP subsystem.
 */
void arp_init(void);

/**
 * @brief Process received ARP packet.
 * @param dev Device that received packet.
 * @param p Packet buffer containing ARP data.
 *
 * Handles ARP requests (sends reply) and ARP replies (updates cache).
 * Consumes the pbuf.
 */
void arp_input(struct netdev* dev, struct pbuf* p);

/**
 * @brief Resolve IP address to MAC address.
 * @param dev Network device to use.
 * @param ip_addr IP address to resolve (network order).
 * @param mac_out Output buffer for MAC address (6 bytes).
 * @return true if MAC found, false if resolution pending.
 *
 * If the address is not in cache, sends an ARP request.
 * Caller should retry later or queue packet.
 */
bool arp_resolve(struct netdev* dev, uint32_t ip_addr, uint8_t* mac_out);

/**
 * @brief Queue packet waiting for ARP resolution.
 * @param dev Network device.
 * @param ip_addr Destination IP (network order).
 * @param p Packet to queue.
 * @return true if queued successfully.
 *
 * When ARP reply arrives, queued packets will be sent automatically.
 */
bool arp_queue_packet(struct netdev* dev, uint32_t ip_addr, struct pbuf* p);

/**
 * @brief Add or update ARP cache entry.
 * @param ip_addr IP address (network order).
 * @param mac MAC address.
 */
void arp_cache_update(uint32_t ip_addr, const uint8_t* mac);

/**
 * @brief Send gratuitous ARP announcement.
 * @param dev Network device.
 *
 * Announces our IP/MAC mapping to the network.
 */
void arp_announce(struct netdev* dev);

/**
 * @brief Print ARP cache contents.
 */
void arp_print_cache(void);

/**
 * @brief Periodic ARP cache maintenance.
 *
 * Called by timer to expire old entries.
 */
void arp_timer(void);

#endif /* NET_LINK_ARP_H */
