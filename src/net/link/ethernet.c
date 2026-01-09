/**
 * @file net/link/ethernet.c
 * @brief Ethernet frame handling implementation.
 */

#include "net/link/ethernet.h"
#include "net/link/arp.h"
#include "net/inet/ip.h"
#include "net/util/byteorder.h"
#include <string.h>
#include <stdio.h>
#include <debug.h>

/* Broadcast MAC address: FF:FF:FF:FF:FF:FF */
const uint8_t eth_broadcast_addr[ETH_ADDR_LEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

void ethernet_init(void) { /* Nothing to initialize */
}

void ethernet_input(struct netdev* dev, struct pbuf* p) {
  struct eth_hdr* eth;
  uint16_t type;

  ASSERT(dev != NULL);
  ASSERT(p != NULL);

  /* Check minimum frame size */
  if (p->len < ETH_HEADER_LEN) {
    pbuf_free(p);
    return;
  }

  eth = p->payload;

  /* Check if frame is for us (unicast, broadcast, or promiscuous) */
  if (!eth_addr_matches(eth->dest, dev->mac_addr) && !eth_is_broadcast(eth->dest) &&
      !(dev->flags & NETDEV_FLAG_PROMISC)) {
    pbuf_free(p);
    return;
  }

  /* Get EtherType */
  type = ntohs(eth->type);

  /* Strip Ethernet header */
  if (!pbuf_header(p, ETH_HEADER_LEN)) {
    pbuf_free(p);
    return;
  }

  /* Dispatch based on EtherType */
  switch (type) {
    case ETH_TYPE_IP:
      ip_input(dev, p);
      break;

    case ETH_TYPE_ARP:
      arp_input(dev, p);
      break;

    default:
      /* Unknown protocol */
      pbuf_free(p);
      break;
  }
}

int ethernet_output(struct netdev* dev, struct pbuf* p, const uint8_t* dest, uint16_t type) {
  struct eth_hdr* eth;

  ASSERT(dev != NULL);
  ASSERT(p != NULL);
  ASSERT(dest != NULL);

  /* Add space for Ethernet header */
  if (!pbuf_header(p, -(int16_t)ETH_HEADER_LEN)) {
    pbuf_free(p);
    return -1;
  }

  eth = p->payload;

  /* Fill in header */
  memcpy(eth->dest, dest, ETH_ADDR_LEN);
  memcpy(eth->src, dev->mac_addr, ETH_ADDR_LEN);
  eth->type = htons(type);

  /* Transmit */
  return netdev_transmit(dev, p);
}

int eth_is_broadcast(const uint8_t* addr) {
  return memcmp(addr, eth_broadcast_addr, ETH_ADDR_LEN) == 0;
}

int eth_addr_matches(const uint8_t* addr, const uint8_t* dev_addr) {
  return memcmp(addr, dev_addr, ETH_ADDR_LEN) == 0;
}

char* eth_addr_to_str(const uint8_t* addr, char* buf) {
  snprintf(buf, 18, "%02x:%02x:%02x:%02x:%02x:%02x", addr[0], addr[1], addr[2], addr[3], addr[4],
           addr[5]);
  return buf;
}
