/**
 * @file net/inet/route.h
 * @brief IP routing table.
 *
 * Simple routing table for determining next-hop addresses
 * and output interfaces.
 */

#ifndef NET_INET_ROUTE_H
#define NET_INET_ROUTE_H

#include <stdint.h>
#include "net/driver/netdev.h"

/**
 * @brief Route lookup result.
 */
struct route_result {
  struct netdev* dev; /* Output device */
  uint32_t gateway;   /* Next-hop gateway (0 if direct) */
  int flags;          /* Route flags */
};

/* Route flags */
#define ROUTE_FLAG_GATEWAY 0x01 /* Use gateway */
#define ROUTE_FLAG_HOST 0x02    /* Host route */
#define ROUTE_FLAG_LOCAL 0x04   /* Local address */

/**
 * @brief Initialize routing subsystem.
 */
void route_init(void);

/**
 * @brief Look up route for destination.
 * @param dst Destination IP address (network order).
 * @param result Output route result.
 * @return 0 on success, -1 if no route.
 */
int route_lookup(uint32_t dst, struct route_result* result);

/**
 * @brief Add a route.
 * @param dst Destination network (network order).
 * @param mask Network mask (network order).
 * @param gateway Gateway address (network order, 0 for direct).
 * @param dev Output device.
 * @return 0 on success.
 */
int route_add(uint32_t dst, uint32_t mask, uint32_t gateway, struct netdev* dev);

/**
 * @brief Set default gateway.
 * @param gateway Gateway IP address.
 * @param dev Output device.
 */
void route_set_default(uint32_t gateway, struct netdev* dev);

/**
 * @brief Print routing table.
 */
void route_print(void);

#endif /* NET_INET_ROUTE_H */
