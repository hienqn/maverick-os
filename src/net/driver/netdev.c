/**
 * @file net/driver/netdev.c
 * @brief Network device abstraction implementation.
 */

#include "net/driver/netdev.h"
#include "threads/malloc.h"
#include "threads/interrupt.h"
#include <string.h>
#include <stdio.h>
#include <debug.h>

/* Global list of registered network devices */
static struct list netdev_list;
static struct lock netdev_list_lock;
static bool netdev_initialized = false;

void netdev_init(void) {
  list_init(&netdev_list);
  lock_init(&netdev_list_lock);
  netdev_initialized = true;
}

struct netdev* netdev_register(const char* name, const struct netdev_ops* ops, void* priv) {
  struct netdev* dev;

  ASSERT(netdev_initialized);
  ASSERT(name != NULL);
  ASSERT(ops != NULL);

  dev = malloc(sizeof(struct netdev));
  if (dev == NULL)
    return NULL;

  /* Initialize device structure */
  strlcpy(dev->name, name, sizeof(dev->name));
  memset(dev->mac_addr, 0, sizeof(dev->mac_addr));
  dev->ip_addr = 0;
  dev->netmask = 0;
  dev->gateway = 0;
  dev->mtu = NETDEV_MTU;
  dev->flags = 0;

  dev->ops = ops;
  dev->priv = priv;

  /* Initialize receive queue */
  list_init(&dev->rx_queue);
  sema_init(&dev->rx_sem, 0);
  lock_init(&dev->rx_lock);

  /* Initialize statistics */
  dev->rx_packets = 0;
  dev->tx_packets = 0;
  dev->rx_bytes = 0;
  dev->tx_bytes = 0;
  dev->rx_errors = 0;
  dev->tx_errors = 0;
  dev->rx_dropped = 0;

  /* Call driver init if provided */
  if (ops->init != NULL) {
    int err = ops->init(dev);
    if (err != 0) {
      free(dev);
      return NULL;
    }
  }

  /* Add to global list */
  lock_acquire(&netdev_list_lock);
  list_push_back(&netdev_list, &dev->elem);
  lock_release(&netdev_list_lock);

  printf("net: registered device %s\n", name);
  return dev;
}

struct netdev* netdev_find_by_name(const char* name) {
  struct list_elem* e;

  ASSERT(netdev_initialized);

  lock_acquire(&netdev_list_lock);
  for (e = list_begin(&netdev_list); e != list_end(&netdev_list); e = list_next(e)) {
    struct netdev* dev = list_entry(e, struct netdev, elem);
    if (strcmp(dev->name, name) == 0) {
      lock_release(&netdev_list_lock);
      return dev;
    }
  }
  lock_release(&netdev_list_lock);
  return NULL;
}

struct netdev* netdev_find_by_ip(uint32_t ip) {
  struct list_elem* e;

  ASSERT(netdev_initialized);

  lock_acquire(&netdev_list_lock);
  for (e = list_begin(&netdev_list); e != list_end(&netdev_list); e = list_next(e)) {
    struct netdev* dev = list_entry(e, struct netdev, elem);
    if (dev->ip_addr == ip) {
      lock_release(&netdev_list_lock);
      return dev;
    }
  }
  lock_release(&netdev_list_lock);
  return NULL;
}

struct netdev* netdev_get_default(void) {
  struct list_elem* e;

  ASSERT(netdev_initialized);

  lock_acquire(&netdev_list_lock);
  for (e = list_begin(&netdev_list); e != list_end(&netdev_list); e = list_next(e)) {
    struct netdev* dev = list_entry(e, struct netdev, elem);
    if (!(dev->flags & NETDEV_FLAG_LOOPBACK) && (dev->flags & NETDEV_FLAG_UP)) {
      lock_release(&netdev_list_lock);
      return dev;
    }
  }
  lock_release(&netdev_list_lock);
  return NULL;
}

struct netdev* netdev_get_loopback(void) {
  struct list_elem* e;

  ASSERT(netdev_initialized);

  lock_acquire(&netdev_list_lock);
  for (e = list_begin(&netdev_list); e != list_end(&netdev_list); e = list_next(e)) {
    struct netdev* dev = list_entry(e, struct netdev, elem);
    if (dev->flags & NETDEV_FLAG_LOOPBACK) {
      lock_release(&netdev_list_lock);
      return dev;
    }
  }
  lock_release(&netdev_list_lock);
  return NULL;
}

int netdev_transmit(struct netdev* dev, struct pbuf* p) {
  int err;

  ASSERT(dev != NULL);
  ASSERT(p != NULL);

  if (!(dev->flags & NETDEV_FLAG_UP)) {
    pbuf_free(p);
    return -1; /* Device not up */
  }

  if (dev->ops->transmit == NULL) {
    pbuf_free(p);
    return -1; /* No transmit function */
  }

  err = dev->ops->transmit(dev, p);
  if (err == 0) {
    dev->tx_packets++;
    dev->tx_bytes += p->tot_len;
  } else {
    dev->tx_errors++;
  }

  return err;
}

void netdev_input(struct netdev* dev, struct pbuf* p) {
  struct netdev_rx_entry* entry;

  ASSERT(dev != NULL);
  ASSERT(p != NULL);

  entry = malloc(sizeof(struct netdev_rx_entry));
  if (entry == NULL) {
    dev->rx_dropped++;
    pbuf_free(p);
    return;
  }

  entry->p = p;

  lock_acquire(&dev->rx_lock);
  list_push_back(&dev->rx_queue, &entry->elem);
  lock_release(&dev->rx_lock);

  dev->rx_packets++;
  dev->rx_bytes += p->tot_len;

  /* Signal that a packet is available */
  sema_up(&dev->rx_sem);
}

struct pbuf* netdev_receive(struct netdev* dev) {
  struct netdev_rx_entry* entry;
  struct pbuf* p;

  ASSERT(dev != NULL);

  lock_acquire(&dev->rx_lock);
  if (list_empty(&dev->rx_queue)) {
    lock_release(&dev->rx_lock);
    return NULL;
  }

  entry = list_entry(list_pop_front(&dev->rx_queue), struct netdev_rx_entry, elem);
  lock_release(&dev->rx_lock);

  p = entry->p;
  free(entry);
  return p;
}

void netdev_set_ip(struct netdev* dev, uint32_t ip, uint32_t netmask, uint32_t gateway) {
  ASSERT(dev != NULL);
  dev->ip_addr = ip;
  dev->netmask = netmask;
  dev->gateway = gateway;
}

int netdev_up(struct netdev* dev) {
  ASSERT(dev != NULL);
  dev->flags |= NETDEV_FLAG_UP;
  return 0;
}

void netdev_down(struct netdev* dev) {
  ASSERT(dev != NULL);
  dev->flags &= ~NETDEV_FLAG_UP;
}

void netdev_print_stats(void) {
  struct list_elem* e;

  printf("\nNetwork device statistics:\n");

  lock_acquire(&netdev_list_lock);
  for (e = list_begin(&netdev_list); e != list_end(&netdev_list); e = list_next(e)) {
    struct netdev* dev = list_entry(e, struct netdev, elem);
    printf("  %s: RX %u packets/%u bytes, TX %u packets/%u bytes\n", dev->name, dev->rx_packets,
           dev->rx_bytes, dev->tx_packets, dev->tx_bytes);
    if (dev->rx_errors || dev->tx_errors || dev->rx_dropped) {
      printf("       errors: RX %u, TX %u, dropped %u\n", dev->rx_errors, dev->tx_errors,
             dev->rx_dropped);
    }
  }
  lock_release(&netdev_list_lock);
}
