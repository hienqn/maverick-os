/**
 * @file net/inet/route.c
 * @brief IP routing table implementation.
 */

#include "net/inet/route.h"
#include "net/inet/ip.h"
#include "net/util/byteorder.h"
#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include <debug.h>

#define MAX_ROUTES 16

struct route_entry {
  uint32_t dst;       /* Destination network */
  uint32_t mask;      /* Network mask */
  uint32_t gateway;   /* Gateway (0 for direct) */
  struct netdev* dev; /* Output device */
  int flags;          /* Route flags */
  int valid;          /* Entry in use */
};

static struct route_entry routes[MAX_ROUTES];
static struct lock route_lock;
static struct netdev* default_dev = NULL;
static uint32_t default_gateway = 0;
static bool route_initialized = false;

void route_init(void) {
  int i;

  lock_init(&route_lock);

  for (i = 0; i < MAX_ROUTES; i++) {
    routes[i].valid = 0;
  }

  route_initialized = true;
}

int route_lookup(uint32_t dst, struct route_result* result) {
  int i;
  int best_match = -1;
  uint32_t best_mask = 0;
  struct netdev* local_dev;

  ASSERT(route_initialized);
  ASSERT(result != NULL);

  /* Check for loopback */
  if ((ntohl(dst) >> 24) == 127) {
    result->dev = netdev_get_loopback();
    result->gateway = 0;
    result->flags = ROUTE_FLAG_LOCAL;
    return (result->dev != NULL) ? 0 : -1;
  }

  /* Check if destination is one of our local addresses */
  local_dev = netdev_find_by_ip(dst);
  if (local_dev != NULL) {
    result->dev = local_dev;
    result->gateway = 0;
    result->flags = ROUTE_FLAG_LOCAL;
    return 0;
  }

  lock_acquire(&route_lock);

  /* Find best matching route (longest prefix match) */
  for (i = 0; i < MAX_ROUTES; i++) {
    if (!routes[i].valid)
      continue;

    if ((dst & routes[i].mask) == routes[i].dst) {
      if (routes[i].mask >= best_mask) {
        best_mask = routes[i].mask;
        best_match = i;
      }
    }
  }

  if (best_match >= 0) {
    result->dev = routes[best_match].dev;
    result->gateway = routes[best_match].gateway;
    result->flags = routes[best_match].flags;
    lock_release(&route_lock);
    return 0;
  }

  /* Check default route */
  if (default_dev != NULL) {
    result->dev = default_dev;
    result->gateway = default_gateway;
    result->flags = (default_gateway != 0) ? ROUTE_FLAG_GATEWAY : 0;
    lock_release(&route_lock);
    return 0;
  }

  /* No route found - try default device anyway */
  lock_release(&route_lock);

  result->dev = netdev_get_default();
  if (result->dev != NULL) {
    /* Check if destination is on same subnet */
    if ((dst & result->dev->netmask) == (result->dev->ip_addr & result->dev->netmask)) {
      result->gateway = 0; /* Direct */
      result->flags = 0;
    } else if (result->dev->gateway != 0) {
      result->gateway = result->dev->gateway;
      result->flags = ROUTE_FLAG_GATEWAY;
    } else {
      result->gateway = 0;
      result->flags = 0;
    }
    return 0;
  }

  return -1; /* No route */
}

int route_add(uint32_t dst, uint32_t mask, uint32_t gateway, struct netdev* dev) {
  int i;

  ASSERT(route_initialized);

  lock_acquire(&route_lock);

  /* Find empty slot */
  for (i = 0; i < MAX_ROUTES; i++) {
    if (!routes[i].valid) {
      routes[i].dst = dst;
      routes[i].mask = mask;
      routes[i].gateway = gateway;
      routes[i].dev = dev;
      routes[i].flags = (gateway != 0) ? ROUTE_FLAG_GATEWAY : 0;
      routes[i].valid = 1;
      lock_release(&route_lock);
      return 0;
    }
  }

  lock_release(&route_lock);
  return -1; /* Table full */
}

void route_set_default(uint32_t gateway, struct netdev* dev) {
  ASSERT(route_initialized);

  lock_acquire(&route_lock);
  default_gateway = gateway;
  default_dev = dev;
  lock_release(&route_lock);
}

void route_print(void) {
  int i;
  char dst_buf[16], mask_buf[16], gw_buf[16];

  printf("\nRouting Table:\n");
  printf("%-16s %-16s %-16s %s\n", "Destination", "Netmask", "Gateway", "Iface");

  lock_acquire(&route_lock);

  for (i = 0; i < MAX_ROUTES; i++) {
    if (!routes[i].valid)
      continue;

    ip_addr_to_str(routes[i].dst, dst_buf);
    ip_addr_to_str(routes[i].mask, mask_buf);
    if (routes[i].gateway != 0)
      ip_addr_to_str(routes[i].gateway, gw_buf);
    else
      strlcpy(gw_buf, "*", sizeof(gw_buf));

    printf("%-16s %-16s %-16s %s\n", dst_buf, mask_buf, gw_buf, routes[i].dev->name);
  }

  /* Print default route */
  if (default_dev != NULL) {
    if (default_gateway != 0)
      ip_addr_to_str(default_gateway, gw_buf);
    else
      strlcpy(gw_buf, "*", sizeof(gw_buf));
    printf("%-16s %-16s %-16s %s\n", "default", "0.0.0.0", gw_buf, default_dev->name);
  }

  lock_release(&route_lock);
}
