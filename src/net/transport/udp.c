/**
 * @file net/transport/udp.c
 * @brief UDP protocol implementation.
 *
 * SCAFFOLD - IMPLEMENT ME!
 *
 * This file contains stub implementations for the UDP protocol.
 * Your task is to complete these functions.
 *
 * RECOMMENDED IMPLEMENTATION ORDER:
 * 1. udp_init() - Set up PCB list
 * 2. udp_new() / udp_free() - PCB allocation
 * 3. udp_bind() - Port binding
 * 4. udp_output() - Send datagrams
 * 5. udp_input() - Receive datagrams
 * 6. udp_recv() - Blocking receive
 */

#include "net/transport/udp.h"
#include "net/inet/ip.h"
#include "net/util/checksum.h"
#include "net/util/byteorder.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include <string.h>
#include <stdio.h>
#include <debug.h>

/* Global list of UDP PCBs */
static struct list udp_pcb_list;
static struct lock udp_lock;
static bool udp_initialized = false;

/* Next ephemeral port to try */
static uint16_t udp_next_port = 49152;

#define UDP_RECV_QUEUE_MAX 16

/* Wrapper for queued received packets */
struct udp_queued_pkt {
  struct pbuf* p;
  uint32_t src_ip;
  uint16_t src_port;
  struct list_elem elem;
};

void udp_init(void) {
  list_init(&udp_pcb_list);
  lock_init(&udp_lock);
  udp_initialized = true;
  printf("udp: initialized\n");
}

void udp_input(struct netdev* dev UNUSED, struct pbuf* p, uint32_t src_ip, uint32_t dst_ip UNUSED) {
  /* 1. Validate packet length */
  if (p->len < UDP_HEADER_LEN) {
    pbuf_free(p);
    return;
  }

  /* 2. Extract header fields */
  struct udp_hdr* udp = (struct udp_hdr*)p->payload;
  uint16_t src_port = ntohs(udp->src_port);
  uint16_t dst_port = ntohs(udp->dst_port);

  /* 3. Verify checksum (if not disabled) */
  if (udp->checksum != 0) {
    uint16_t total_len = ntohs(udp->length);
    uint32_t sum = checksum_pseudo_header(src_ip, dst_ip, IP_PROTO_UDP, total_len);
    sum = checksum_partial(p->payload, total_len, sum);
    if (checksum_finish(sum) != 0) {
      pbuf_free(p);
      return; /* Checksum failed, drop packet */
    }
  }

  /* 4. Find matching PCB by destination port */
  struct udp_pcb* pcb = NULL;
  lock_acquire(&udp_lock);
  struct list_elem* e;
  for (e = list_begin(&udp_pcb_list); e != list_end(&udp_pcb_list); e = list_next(e)) {
    struct udp_pcb* tmp = list_entry(e, struct udp_pcb, elem);
    if (tmp->bound && tmp->local_port == dst_port) {
      pcb = tmp;
      break;
    }
  }
  lock_release(&udp_lock);

  if (pcb == NULL) {
    pbuf_free(p);
    return; /* No matching socket */
  }

  /* 4. Check queue limit */
  lock_acquire(&pcb->lock);
  if (list_size(&pcb->recv_queue) >= UDP_RECV_QUEUE_MAX) {
    lock_release(&pcb->lock);
    pbuf_free(p);
    return; /* Queue full, drop packet */
  }

  /* 5. Strip UDP header */
  pbuf_header(p, UDP_HEADER_LEN);

  /* 6. Create queued packet wrapper */
  struct udp_queued_pkt* pkt = malloc(sizeof(struct udp_queued_pkt));
  if (pkt == NULL) {
    lock_release(&pcb->lock);
    pbuf_free(p);
    return;
  }
  pkt->p = p;
  pkt->src_ip = src_ip;
  pkt->src_port = src_port;

  /* 7. Queue packet and signal receiver */
  list_push_back(&pcb->recv_queue, &pkt->elem);
  lock_release(&pcb->lock);
  sema_up(&pcb->recv_sem);
}

int udp_output(struct udp_pcb* pcb, struct pbuf* p, uint32_t dst_ip, uint16_t dst_port) {
  uint16_t src_port = 0;
  uint32_t src_ip = 0;

  if (pcb != NULL) {
    src_port = pcb->local_port;
    src_ip = pcb->local_ip;
  }

  uint16_t total_len = UDP_HEADER_LEN + p->len;

  if (!pbuf_header(p, -(int16_t)UDP_HEADER_LEN)) {
    pbuf_free(p);
    return -1;
  }

  struct udp_hdr* udp = (struct udp_hdr*)p->payload;
  udp->src_port = htons(src_port);
  udp->dst_port = htons(dst_port);
  udp->length = htons(total_len);
  udp->checksum = 0; /* Set to 0 before calculating */

  uint32_t sum = checksum_pseudo_header(src_ip, dst_ip, IP_PROTO_UDP, total_len);
  sum = checksum_partial(p->payload, p->len, sum);
  udp->checksum = checksum_finish(sum);

  /* If checksum is 0, use 0xFFFF (0 means "no checksum" in UDP) */
  if (udp->checksum == 0)
    udp->checksum = 0xFFFF;

  return ip_output(NULL, p, src_ip, dst_ip, IP_PROTO_UDP, 0);
}

struct udp_pcb* udp_new(void) {
  struct udp_pcb* pcb = malloc(sizeof(struct udp_pcb));
  if (pcb == NULL)
    return NULL;

  /* Initialize addresses/ports to 0 */
  pcb->local_ip = 0;
  pcb->local_port = 0;
  pcb->remote_ip = 0;
  pcb->remote_port = 0;

  /* Initialize receive queue and synchronization */
  list_init(&pcb->recv_queue);
  sema_init(&pcb->recv_sem, 0);
  lock_init(&pcb->lock);

  /* Not bound yet */
  pcb->bound = false;

  /* Add to global PCB list */
  lock_acquire(&udp_lock);
  list_push_back(&udp_pcb_list, &pcb->elem);
  lock_release(&udp_lock);

  return pcb;
}

void udp_free(struct udp_pcb* pcb) {
  if (pcb == NULL)
    return;

  /* 1. Remove from global PCB list */
  lock_acquire(&udp_lock);
  list_remove(&pcb->elem);
  lock_release(&udp_lock);

  /* 2. Free any queued packets */
  while (!list_empty(&pcb->recv_queue)) {
    struct list_elem* e = list_pop_front(&pcb->recv_queue);
    struct udp_queued_pkt* pkt = list_entry(e, struct udp_queued_pkt, elem);
    pbuf_free(pkt->p);
    free(pkt);
  }

  /* 3. Free the PCB */
  free(pcb);
}

int udp_bind(struct udp_pcb* pcb, uint32_t ip, uint16_t port) {
  if (pcb == NULL)
    return -1;

  if (pcb->bound)
    return -1; /* Already bound */

  lock_acquire(&udp_lock);

  /* If port == 0, allocate ephemeral port */
  if (port == 0) {
    port = udp_next_port++;
    if (udp_next_port == 0) /* Wrap around */
      udp_next_port = 49152;
  } else {
    /* Check if port already in use */
    struct list_elem* e;
    for (e = list_begin(&udp_pcb_list); e != list_end(&udp_pcb_list); e = list_next(e)) {
      struct udp_pcb* other = list_entry(e, struct udp_pcb, elem);
      if (other->bound && other->local_port == port) {
        lock_release(&udp_lock);
        return -1; /* Port in use */
      }
    }
  }

  pcb->local_ip = ip;
  pcb->local_port = port;
  pcb->bound = true;

  lock_release(&udp_lock);
  return 0;
}

int udp_connect(struct udp_pcb* pcb, uint32_t ip, uint16_t port) {
  if (pcb == NULL)
    return -1;

  pcb->remote_ip = ip;
  pcb->remote_port = port;
  return 0;
}

int udp_recv(struct udp_pcb* pcb, void* buf, size_t len, uint32_t* src_ip, uint16_t* src_port) {
  if (pcb == NULL || buf == NULL)
    return -1;

  /* 1. Wait for packet (blocks if queue empty) */
  sema_down(&pcb->recv_sem);

  /* 2. Dequeue packet */
  lock_acquire(&pcb->lock);
  struct list_elem* e = list_pop_front(&pcb->recv_queue);
  lock_release(&pcb->lock);

  struct udp_queued_pkt* pkt = list_entry(e, struct udp_queued_pkt, elem);

  /* 3. Copy data to user buffer */
  size_t copy_len = pkt->p->len < len ? pkt->p->len : len;
  memcpy(buf, pkt->p->payload, copy_len);

  /* 4. Fill in sender info if requested */
  if (src_ip != NULL)
    *src_ip = pkt->src_ip;
  if (src_port != NULL)
    *src_port = pkt->src_port;

  /* 5. Free the pbuf and wrapper */
  pbuf_free(pkt->p);
  free(pkt);

  /* 6. Return bytes copied */
  return (int)copy_len;
}
