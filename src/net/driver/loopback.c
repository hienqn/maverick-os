/**
 * @file net/driver/loopback.c
 * @brief Loopback network device implementation.
 */

#include "net/driver/loopback.h"
#include "net/driver/netdev.h"
#include "net/util/byteorder.h"
#include <stdio.h>

/* Loopback IP: 127.0.0.1 */
#define LOOPBACK_IP htonl(0x7F000001)
#define LOOPBACK_MASK htonl(0xFF000000)

static int loopback_init_dev(struct netdev* dev);
static int loopback_transmit(struct netdev* dev, struct pbuf* p);

static const struct netdev_ops loopback_ops = {
    .init = loopback_init_dev, .transmit = loopback_transmit, .set_mac = NULL};

static int loopback_init_dev(struct netdev* dev) {
  /* Loopback has no MAC address */
  dev->mac_addr[0] = 0;
  dev->mac_addr[1] = 0;
  dev->mac_addr[2] = 0;
  dev->mac_addr[3] = 0;
  dev->mac_addr[4] = 0;
  dev->mac_addr[5] = 0;

  dev->flags |= NETDEV_FLAG_LOOPBACK;
  return 0;
}

static int loopback_transmit(struct netdev* dev, struct pbuf* p) {
  /*
   * Loopback simply queues packets back to the receive queue.
   * The packet is already in the format it would be received.
   */

  /* Don't free the pbuf - pass ownership to receive queue */
  netdev_input(dev, p);

  return 0;
}

void loopback_init(void) {
  struct netdev* dev;

  dev = netdev_register("lo", &loopback_ops, NULL);
  if (dev == NULL) {
    printf("loopback: failed to register device\n");
    return;
  }

  /* Configure loopback IP */
  netdev_set_ip(dev, LOOPBACK_IP, LOOPBACK_MASK, 0);
  netdev_up(dev);

  printf("loopback: initialized with IP 127.0.0.1\n");
}
