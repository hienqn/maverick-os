/**
 * @file net/driver/netdev.h
 * @brief Network device abstraction layer.
 *
 * This module provides a unified interface for network devices,
 * similar to the block device abstraction in devices/block.h.
 *
 * Each network device registers with this layer and provides
 * transmit operations. Received packets are delivered through
 * a callback mechanism.
 */

#ifndef NET_DRIVER_NETDEV_H
#define NET_DRIVER_NETDEV_H

#include <stdint.h>
#include <stdbool.h>
#include <list.h>
#include "threads/synch.h"
#include "net/buf/pbuf.h"

/* Maximum Transmission Unit (standard Ethernet) */
#define NETDEV_MTU 1500

/* Device flags */
#define NETDEV_FLAG_UP 0x01        /* Interface is up */
#define NETDEV_FLAG_BROADCAST 0x02 /* Supports broadcast */
#define NETDEV_FLAG_LOOPBACK 0x04  /* Is loopback device */
#define NETDEV_FLAG_PROMISC 0x08   /* Promiscuous mode */

struct netdev;

/**
 * @brief Network device operations.
 */
struct netdev_ops {
  /**
   * @brief Initialize the device.
   * @return 0 on success, negative error code on failure.
   */
  int (*init)(struct netdev* dev);

  /**
   * @brief Transmit a packet.
   * @param dev Device to transmit on.
   * @param p Packet buffer to send.
   * @return 0 on success, negative error code on failure.
   *
   * The pbuf is consumed (freed) by this function on success.
   */
  int (*transmit)(struct netdev* dev, struct pbuf* p);

  /**
   * @brief Set the MAC address.
   * @param dev Device.
   * @param mac New MAC address (6 bytes).
   */
  void (*set_mac)(struct netdev* dev, const uint8_t* mac);

  /**
   * @brief Poll for received packets.
   * @param dev Device to poll.
   *
   * Optional. If provided, the driver should check for received
   * packets and call netdev_input() for each one. This is called
   * from the network input thread, not from interrupt context.
   */
  void (*poll)(struct netdev* dev);
};

/**
 * @brief Network device structure.
 */
struct netdev {
  char name[8];        /* Device name (e.g., "eth0", "lo") */
  uint8_t mac_addr[6]; /* MAC address */
  uint32_t ip_addr;    /* IPv4 address (network order) */
  uint32_t netmask;    /* Network mask (network order) */
  uint32_t gateway;    /* Default gateway (network order) */
  uint16_t mtu;        /* Maximum transmission unit */
  uint16_t flags;      /* Device flags */

  const struct netdev_ops* ops; /* Device operations */
  void* priv;                   /* Private driver data */

  /* Receive queue */
  struct list rx_queue;    /* Received packets */
  struct semaphore rx_sem; /* Signals packet arrival */
  struct lock rx_lock;     /* Protects rx_queue */

  struct list_elem elem; /* In global device list */

  /* Statistics */
  uint32_t rx_packets;
  uint32_t tx_packets;
  uint32_t rx_bytes;
  uint32_t tx_bytes;
  uint32_t rx_errors;
  uint32_t tx_errors;
  uint32_t rx_dropped;
};

/**
 * @brief Received packet queue entry.
 */
struct netdev_rx_entry {
  struct pbuf* p;        /* Received packet */
  struct list_elem elem; /* In rx_queue */
};

/**
 * @brief Initialize the network device subsystem.
 */
void netdev_init(void);

/**
 * @brief Register a network device.
 * @param name Device name.
 * @param ops Device operations.
 * @param priv Private driver data.
 * @return Pointer to device, or NULL on failure.
 */
struct netdev* netdev_register(const char* name, const struct netdev_ops* ops, void* priv);

/**
 * @brief Find a network device by name.
 * @param name Device name to find.
 * @return Device pointer, or NULL if not found.
 */
struct netdev* netdev_find_by_name(const char* name);

/**
 * @brief Find a network device by IP address.
 * @param ip IP address (network order).
 * @return Device pointer, or NULL if not found.
 */
struct netdev* netdev_find_by_ip(uint32_t ip);

/**
 * @brief Get the default network device.
 * @return Default device (first non-loopback), or NULL.
 */
struct netdev* netdev_get_default(void);

/**
 * @brief Get the loopback device.
 * @return Loopback device, or NULL if not registered.
 */
struct netdev* netdev_get_loopback(void);

/**
 * @brief Transmit a packet on a device.
 * @param dev Device to use.
 * @param p Packet to send.
 * @return 0 on success, negative error code on failure.
 */
int netdev_transmit(struct netdev* dev, struct pbuf* p);

/**
 * @brief Called by driver when packet is received.
 * @param dev Device that received the packet.
 * @param p Received packet.
 *
 * This queues the packet for processing by the network thread.
 */
void netdev_input(struct netdev* dev, struct pbuf* p);

/**
 * @brief Dequeue a received packet.
 * @param dev Device to check.
 * @return Received packet, or NULL if queue empty.
 *
 * Non-blocking. Use rx_sem to wait for packets.
 */
struct pbuf* netdev_receive(struct netdev* dev);

/**
 * @brief Configure device IP settings.
 * @param dev Device to configure.
 * @param ip IP address (network order).
 * @param netmask Network mask (network order).
 * @param gateway Default gateway (network order).
 */
void netdev_set_ip(struct netdev* dev, uint32_t ip, uint32_t netmask, uint32_t gateway);

/**
 * @brief Bring device up.
 * @param dev Device to bring up.
 * @return 0 on success.
 */
int netdev_up(struct netdev* dev);

/**
 * @brief Bring device down.
 * @param dev Device to bring down.
 */
void netdev_down(struct netdev* dev);

/**
 * @brief Print device statistics.
 */
void netdev_print_stats(void);

#endif /* NET_DRIVER_NETDEV_H */
