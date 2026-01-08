/**
 * @file net/link/arp.c
 * @brief ARP protocol implementation.
 */

#include "net/link/arp.h"
#include "net/link/ethernet.h"
#include "net/util/byteorder.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "devices/timer.h"
#include <string.h>
#include <stdio.h>
#include <debug.h>

/* Pending packet entry */
struct arp_pending_pkt {
  struct pbuf* p;
  struct netdev* dev;
  struct list_elem elem;
};

/* ARP cache entry */
struct arp_entry {
  uint32_t ip_addr;     /* IP address (network order) */
  uint8_t mac_addr[6];  /* MAC address */
  enum arp_state state; /* Entry state */
  int64_t expire_time;  /* When entry expires (ticks) */
  int64_t retry_time;   /* When to retry request */
  int retry_count;      /* Request retry counter */
  struct list pending;  /* Packets waiting for resolution */
};

/* ARP cache */
static struct arp_entry arp_cache[ARP_CACHE_SIZE];
static struct lock arp_lock;
static bool arp_initialized = false;

/* Forward declarations */
static void arp_send_request(struct netdev* dev, uint32_t ip_addr);
static void arp_send_reply(struct netdev* dev, uint32_t target_ip, const uint8_t* target_mac);
static struct arp_entry* arp_find_entry(uint32_t ip_addr);
static struct arp_entry* arp_alloc_entry(void);
static void arp_send_pending(struct arp_entry* entry, struct netdev* dev);

void arp_init(void) {
  int i;

  lock_init(&arp_lock);

  for (i = 0; i < ARP_CACHE_SIZE; i++) {
    arp_cache[i].state = ARP_STATE_EMPTY;
    list_init(&arp_cache[i].pending);
  }

  arp_initialized = true;
}

void arp_input(struct netdev* dev, struct pbuf* p) {
  struct arp_hdr* arp;
  uint16_t opcode;
  struct arp_entry* entry;

  ASSERT(arp_initialized);
  ASSERT(p != NULL);

  if (p->len < ARP_HEADER_LEN) {
    pbuf_free(p);
    return;
  }

  arp = p->payload;

  /* Validate ARP packet */
  if (ntohs(arp->hw_type) != ARP_HWTYPE_ETHERNET || ntohs(arp->proto_type) != ARP_PROTO_IP ||
      arp->hw_len != 6 || arp->proto_len != 4) {
    pbuf_free(p);
    return;
  }

  opcode = ntohs(arp->opcode);

  lock_acquire(&arp_lock);

  /* Update cache with sender's info (even for requests) */
  entry = arp_find_entry(arp->sender_ip);
  if (entry != NULL && entry->state != ARP_STATE_EMPTY) {
    /* Update existing entry */
    memcpy(entry->mac_addr, arp->sender_mac, 6);
    entry->state = ARP_STATE_VALID;
    entry->expire_time = timer_ticks() + ARP_CACHE_TIMEOUT;

    /* Send any pending packets */
    arp_send_pending(entry, dev);
  } else if (arp->target_ip == dev->ip_addr) {
    /* New entry for someone asking about us */
    entry = arp_alloc_entry();
    if (entry != NULL) {
      entry->ip_addr = arp->sender_ip;
      memcpy(entry->mac_addr, arp->sender_mac, 6);
      entry->state = ARP_STATE_VALID;
      entry->expire_time = timer_ticks() + ARP_CACHE_TIMEOUT;
    }
  }

  lock_release(&arp_lock);

  /* Handle ARP request */
  if (opcode == ARP_OP_REQUEST) {
    /* Is this request for our IP? */
    if (arp->target_ip == dev->ip_addr) {
      arp_send_reply(dev, arp->sender_ip, arp->sender_mac);
    }
  }

  pbuf_free(p);
}

bool arp_resolve(struct netdev* dev, uint32_t ip_addr, uint8_t* mac_out) {
  struct arp_entry* entry;
  bool found = false;

  ASSERT(arp_initialized);

  /* Check for broadcast */
  if (ip_addr == 0xFFFFFFFF) {
    memcpy(mac_out, eth_broadcast_addr, 6);
    return true;
  }

  /* Check for loopback */
  if ((ntohl(ip_addr) >> 24) == 127) {
    /* Loopback doesn't need ARP */
    memset(mac_out, 0, 6);
    return true;
  }

  lock_acquire(&arp_lock);

  entry = arp_find_entry(ip_addr);
  if (entry != NULL && entry->state == ARP_STATE_VALID) {
    memcpy(mac_out, entry->mac_addr, 6);
    found = true;
  } else if (entry == NULL || entry->state == ARP_STATE_EMPTY) {
    /* Need to send ARP request */
    entry = arp_alloc_entry();
    if (entry != NULL) {
      entry->ip_addr = ip_addr;
      entry->state = ARP_STATE_PENDING;
      entry->retry_count = 0;
      entry->retry_time = timer_ticks() + 100; /* Retry in 1 second */
      entry->expire_time = timer_ticks() + ARP_CACHE_TIMEOUT;
    }

    lock_release(&arp_lock);
    arp_send_request(dev, ip_addr);
    return false;
  } else if (entry->state == ARP_STATE_PENDING) {
    /* Already waiting for reply */
    found = false;
  }

  lock_release(&arp_lock);
  return found;
}

bool arp_queue_packet(struct netdev* dev, uint32_t ip_addr, struct pbuf* p) {
  struct arp_entry* entry;
  struct arp_pending_pkt* pkt;
  int pending_count = 0;
  struct list_elem* e;

  ASSERT(arp_initialized);

  lock_acquire(&arp_lock);

  entry = arp_find_entry(ip_addr);
  if (entry == NULL || entry->state != ARP_STATE_PENDING) {
    lock_release(&arp_lock);
    return false;
  }

  /* Count pending packets */
  for (e = list_begin(&entry->pending); e != list_end(&entry->pending); e = list_next(e)) {
    pending_count++;
  }

  if (pending_count >= ARP_MAX_PENDING) {
    /* Too many pending, drop oldest */
    struct arp_pending_pkt* oldest;
    oldest = list_entry(list_pop_front(&entry->pending), struct arp_pending_pkt, elem);
    pbuf_free(oldest->p);
    free(oldest);
  }

  /* Queue new packet */
  pkt = malloc(sizeof(struct arp_pending_pkt));
  if (pkt == NULL) {
    lock_release(&arp_lock);
    return false;
  }

  pbuf_ref(p);
  pkt->p = p;
  pkt->dev = dev;
  list_push_back(&entry->pending, &pkt->elem);

  lock_release(&arp_lock);
  return true;
}

void arp_cache_update(uint32_t ip_addr, const uint8_t* mac) {
  struct arp_entry* entry;

  ASSERT(arp_initialized);

  lock_acquire(&arp_lock);

  entry = arp_find_entry(ip_addr);
  if (entry == NULL) {
    entry = arp_alloc_entry();
    if (entry == NULL) {
      lock_release(&arp_lock);
      return;
    }
    entry->ip_addr = ip_addr;
    list_init(&entry->pending);
  }

  memcpy(entry->mac_addr, mac, 6);
  entry->state = ARP_STATE_VALID;
  entry->expire_time = timer_ticks() + ARP_CACHE_TIMEOUT;

  lock_release(&arp_lock);
}

void arp_announce(struct netdev* dev) {
  /* Send gratuitous ARP: request for our own IP */
  arp_send_request(dev, dev->ip_addr);
}

void arp_print_cache(void) {
  int i;
  char ip_buf[16];
  char mac_buf[18];

  printf("\nARP Cache:\n");
  printf("%-16s %-18s %s\n", "IP Address", "MAC Address", "State");

  lock_acquire(&arp_lock);

  for (i = 0; i < ARP_CACHE_SIZE; i++) {
    struct arp_entry* e = &arp_cache[i];
    if (e->state != ARP_STATE_EMPTY) {
      uint32_t ip = ntohl(e->ip_addr);
      snprintf(ip_buf, sizeof(ip_buf), "%u.%u.%u.%u", (ip >> 24) & 0xFF, (ip >> 16) & 0xFF,
               (ip >> 8) & 0xFF, ip & 0xFF);
      eth_addr_to_str(e->mac_addr, mac_buf);
      printf("%-16s %-18s %s\n", ip_buf, mac_buf,
             e->state == ARP_STATE_VALID ? "valid" : "pending");
    }
  }

  lock_release(&arp_lock);
}

void arp_timer(void) {
  int i;
  int64_t now;

  if (!arp_initialized)
    return;

  now = timer_ticks();

  lock_acquire(&arp_lock);

  for (i = 0; i < ARP_CACHE_SIZE; i++) {
    struct arp_entry* e = &arp_cache[i];

    if (e->state == ARP_STATE_EMPTY)
      continue;

    /* Check for expiration */
    if (now >= e->expire_time) {
      /* Free pending packets */
      while (!list_empty(&e->pending)) {
        struct arp_pending_pkt* pkt;
        pkt = list_entry(list_pop_front(&e->pending), struct arp_pending_pkt, elem);
        pbuf_free(pkt->p);
        free(pkt);
      }
      e->state = ARP_STATE_EMPTY;
    }
  }

  lock_release(&arp_lock);
}

/* ============================================================
 * Internal Functions
 * ============================================================ */

static void arp_send_request(struct netdev* dev, uint32_t ip_addr) {
  struct pbuf* p;
  struct arp_hdr* arp;

  p = pbuf_alloc(PBUF_LINK, ARP_HEADER_LEN, PBUF_RAM);
  if (p == NULL)
    return;

  arp = p->payload;
  arp->hw_type = htons(ARP_HWTYPE_ETHERNET);
  arp->proto_type = htons(ARP_PROTO_IP);
  arp->hw_len = 6;
  arp->proto_len = 4;
  arp->opcode = htons(ARP_OP_REQUEST);
  memcpy(arp->sender_mac, dev->mac_addr, 6);
  arp->sender_ip = dev->ip_addr;
  memset(arp->target_mac, 0, 6); /* Unknown */
  arp->target_ip = ip_addr;

  /* Send as broadcast */
  ethernet_output(dev, p, eth_broadcast_addr, ETH_TYPE_ARP);
}

static void arp_send_reply(struct netdev* dev, uint32_t target_ip, const uint8_t* target_mac) {
  struct pbuf* p;
  struct arp_hdr* arp;

  p = pbuf_alloc(PBUF_LINK, ARP_HEADER_LEN, PBUF_RAM);
  if (p == NULL)
    return;

  arp = p->payload;
  arp->hw_type = htons(ARP_HWTYPE_ETHERNET);
  arp->proto_type = htons(ARP_PROTO_IP);
  arp->hw_len = 6;
  arp->proto_len = 4;
  arp->opcode = htons(ARP_OP_REPLY);
  memcpy(arp->sender_mac, dev->mac_addr, 6);
  arp->sender_ip = dev->ip_addr;
  memcpy(arp->target_mac, target_mac, 6);
  arp->target_ip = target_ip;

  /* Send directly to requester */
  ethernet_output(dev, p, target_mac, ETH_TYPE_ARP);
}

static struct arp_entry* arp_find_entry(uint32_t ip_addr) {
  int i;

  for (i = 0; i < ARP_CACHE_SIZE; i++) {
    if (arp_cache[i].state != ARP_STATE_EMPTY && arp_cache[i].ip_addr == ip_addr) {
      return &arp_cache[i];
    }
  }
  return NULL;
}

static struct arp_entry* arp_alloc_entry(void) {
  int i;
  int64_t oldest_time = INT64_MAX;
  int oldest_idx = -1;

  /* Find empty or oldest entry */
  for (i = 0; i < ARP_CACHE_SIZE; i++) {
    if (arp_cache[i].state == ARP_STATE_EMPTY) {
      list_init(&arp_cache[i].pending);
      return &arp_cache[i];
    }
    if (arp_cache[i].expire_time < oldest_time) {
      oldest_time = arp_cache[i].expire_time;
      oldest_idx = i;
    }
  }

  /* Reuse oldest entry */
  if (oldest_idx >= 0) {
    struct arp_entry* e = &arp_cache[oldest_idx];

    /* Free pending packets */
    while (!list_empty(&e->pending)) {
      struct arp_pending_pkt* pkt;
      pkt = list_entry(list_pop_front(&e->pending), struct arp_pending_pkt, elem);
      pbuf_free(pkt->p);
      free(pkt);
    }

    list_init(&e->pending);
    return e;
  }

  return NULL;
}

static void arp_send_pending(struct arp_entry* entry, struct netdev* dev) {
  while (!list_empty(&entry->pending)) {
    struct arp_pending_pkt* pkt;
    pkt = list_entry(list_pop_front(&entry->pending), struct arp_pending_pkt, elem);

    /* Send the packet (add Ethernet header and transmit) */
    ethernet_output(pkt->dev, pkt->p, entry->mac_addr, ETH_TYPE_IP);
    free(pkt);
  }
}
