/**
 * @file net/inet/ip.c
 * @brief IPv4 protocol implementation.
 */

#include "net/inet/ip.h"
#include "net/inet/route.h"
#include "net/inet/icmp.h"
#include "net/link/ethernet.h"
#include "net/link/arp.h"
#include "net/util/checksum.h"
#include "net/util/byteorder.h"
#include "threads/synch.h"
#include <string.h>
#include <stdio.h>
#include <debug.h>

/* IP identification counter */
static uint16_t ip_id_counter = 0;
static struct lock ip_lock;
static bool ip_initialized = false;

/* Forward declarations for transport protocols */
extern void tcp_input(struct netdev* dev, struct pbuf* p, uint32_t src, uint32_t dst);
extern void udp_input(struct netdev* dev, struct pbuf* p, uint32_t src, uint32_t dst);

void ip_init(void) {
  lock_init(&ip_lock);
  ip_id_counter = 1;
  ip_initialized = true;
}

void ip_input(struct netdev* dev, struct pbuf* p) {
  struct ip_hdr* ip;
  uint16_t hlen;
  uint16_t tot_len;
  uint16_t cksum;

  ASSERT(ip_initialized);
  ASSERT(p != NULL);

  /* Check minimum packet size */
  if (p->len < IP_HEADER_LEN) {
    pbuf_free(p);
    return;
  }

  ip = p->payload;

  /* Validate IP version */
  if (IP_HDR_VERSION(ip) != IP_VERSION) {
    pbuf_free(p);
    return;
  }

  /* Get header length */
  hlen = IP_HDR_HLEN(ip);
  if (hlen < IP_HEADER_LEN || hlen > p->len) {
    pbuf_free(p);
    return;
  }

  /* Verify header checksum */
  cksum = checksum(ip, hlen);
  if (cksum != 0) {
    pbuf_free(p);
    return;
  }

  /* Get total length and validate */
  tot_len = ntohs(ip->tot_len);
  if (tot_len < hlen || tot_len > p->tot_len) {
    pbuf_free(p);
    return;
  }

  /* Trim any padding */
  if (tot_len < p->tot_len) {
    p->tot_len = tot_len;
    p->len = (p->len < tot_len) ? p->len : tot_len;
  }

  /* Check if packet is for us */
  if (!ip_is_local(ip->dst_addr) && !ip_is_broadcast(ip->dst_addr, dev)) {
    /* Not for us - could forward, but we don't */
    pbuf_free(p);
    return;
  }

  /* Check for fragmentation (we don't support reassembly yet) */
  if (ntohs(ip->frag_off) & (IP_FLAG_MF | IP_FRAG_OFFSET)) {
    /* Fragment - drop it */
    pbuf_free(p);
    return;
  }

  /* Save addresses before stripping header */
  uint32_t src_addr = ip->src_addr;
  uint32_t dst_addr = ip->dst_addr;
  uint8_t protocol = ip->protocol;

  /* Strip IP header to get to payload */
  if (!pbuf_header(p, hlen)) {
    pbuf_free(p);
    return;
  }

  /* Dispatch based on protocol */
  switch (protocol) {
    case IP_PROTO_ICMP:
      icmp_input(dev, p, src_addr, dst_addr);
      break;

    case IP_PROTO_TCP:
      /* TCP not yet implemented - drop or send ICMP */
      pbuf_free(p);
      break;

    case IP_PROTO_UDP:
      /* UDP not yet implemented - drop or send ICMP */
      pbuf_free(p);
      break;

    default:
      pbuf_free(p);
      break;
  }
}

int ip_output(struct netdev* dev, struct pbuf* p, uint32_t src, uint32_t dst, uint8_t protocol,
              uint8_t ttl) {
  struct ip_hdr* ip;
  struct route_result route;
  uint8_t mac[6];
  uint32_t next_hop;

  ASSERT(ip_initialized);
  ASSERT(p != NULL);

  /* Routing lookup if no device specified */
  if (dev == NULL) {
    if (route_lookup(dst, &route) != 0) {
      pbuf_free(p);
      return -1; /* No route */
    }
    dev = route.dev;
    next_hop = (route.flags & ROUTE_FLAG_GATEWAY) ? route.gateway : dst;
  } else {
    /* Check if direct or via gateway */
    if ((dst & dev->netmask) == (dev->ip_addr & dev->netmask)) {
      next_hop = dst; /* Direct */
    } else if (dev->gateway != 0) {
      next_hop = dev->gateway; /* Via gateway */
    } else {
      next_hop = dst; /* Hope for the best */
    }
  }

  /* Use device's IP if source not specified */
  if (src == 0) {
    src = dev->ip_addr;
  }

  /* Set default TTL if not specified */
  if (ttl == 0) {
    ttl = IP_DEFAULT_TTL;
  }

  /* Prepend IP header */
  if (!pbuf_header(p, -(int16_t)IP_HEADER_LEN)) {
    pbuf_free(p);
    return -1;
  }

  /* Fill in IP header */
  ip = p->payload;
  ip->version_ihl = (IP_VERSION << 4) | IP_HLEN_MIN;
  ip->tos = 0;
  ip->tot_len = htons(p->tot_len);

  lock_acquire(&ip_lock);
  ip->id = htons(ip_id_counter++);
  lock_release(&ip_lock);

  ip->frag_off = 0;
  ip->ttl = ttl;
  ip->protocol = protocol;
  ip->checksum = 0;
  ip->src_addr = src;
  ip->dst_addr = dst;

  /* Calculate header checksum */
  ip->checksum = checksum(ip, IP_HEADER_LEN);

  /* Handle loopback */
  if (dev->flags & NETDEV_FLAG_LOOPBACK) {
    return netdev_transmit(dev, p);
  }

  /* ARP resolution */
  if (ip_is_broadcast(dst, dev)) {
    memcpy(mac, eth_broadcast_addr, 6);
  } else if (!arp_resolve(dev, next_hop, mac)) {
    /* ARP pending - queue packet */
    if (!arp_queue_packet(dev, next_hop, p)) {
      pbuf_free(p);
    }
    return 0; /* Will be sent when ARP completes */
  }

  /* Send via Ethernet */
  return ethernet_output(dev, p, mac, ETH_TYPE_IP);
}

int ip_output_hdr(struct netdev* dev, struct pbuf* p, const uint8_t* dst_mac) {
  ASSERT(dev != NULL);
  ASSERT(p != NULL);
  ASSERT(dst_mac != NULL);

  return ethernet_output(dev, p, dst_mac, ETH_TYPE_IP);
}

int ip_is_local(uint32_t ip) {
  /* Check loopback */
  if ((ntohl(ip) >> 24) == 127) {
    return 1;
  }

  /* Check if any device has this IP */
  return netdev_find_by_ip(ip) != NULL;
}

int ip_is_broadcast(uint32_t ip, struct netdev* dev) {
  /* All-ones broadcast */
  if (ip == 0xFFFFFFFF) {
    return 1;
  }

  /* Subnet broadcast */
  if (dev != NULL && dev->netmask != 0) {
    uint32_t broadcast = dev->ip_addr | ~dev->netmask;
    if (ip == broadcast) {
      return 1;
    }
  }

  return 0;
}

char* ip_addr_to_str(uint32_t ip, char* buf) {
  uint32_t host_ip = ntohl(ip);
  snprintf(buf, 16, "%u.%u.%u.%u", (host_ip >> 24) & 0xFF, (host_ip >> 16) & 0xFF,
           (host_ip >> 8) & 0xFF, host_ip & 0xFF);
  return buf;
}

uint32_t ip_addr_from_str(const char* str) {
  unsigned int octets[4];
  int octet_idx = 0;
  unsigned int val = 0;
  bool has_digit = false;

  for (const char* p = str;; p++) {
    if (*p >= '0' && *p <= '9') {
      val = val * 10 + (*p - '0');
      has_digit = true;
      if (val > 255)
        return 0;
    } else if (*p == '.' || *p == '\0') {
      if (!has_digit || octet_idx >= 4)
        return 0;
      octets[octet_idx++] = val;
      val = 0;
      has_digit = false;
      if (*p == '\0')
        break;
    } else {
      return 0;
    }
  }

  if (octet_idx != 4)
    return 0;
  return htonl((octets[0] << 24) | (octets[1] << 16) | (octets[2] << 8) | octets[3]);
}
