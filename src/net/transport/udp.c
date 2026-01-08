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

void udp_init(void) {
  list_init(&udp_pcb_list);
  lock_init(&udp_lock);
  udp_initialized = true;
  printf("udp: initialized (SCAFFOLD - implement me!)\n");
}

void udp_input(struct netdev* dev UNUSED, struct pbuf* p, uint32_t src_ip UNUSED,
               uint32_t dst_ip UNUSED) {
  /*
   * TODO: Implement UDP input processing
   *
   * Steps:
   * 1. Validate packet length >= UDP_HEADER_LEN
   * 2. Extract header fields (remember: network byte order!)
   * 3. Verify checksum (optional for UDP, but recommended)
   * 4. Find matching PCB by destination port
   * 5. Queue packet to PCB's receive queue
   * 6. Signal waiting receiver
   *
   * If no matching PCB, you could send ICMP port unreachable.
   */

  printf("udp: received packet (not implemented)\n");
  pbuf_free(p);
}

int udp_output(struct udp_pcb* pcb UNUSED, struct pbuf* p, uint32_t dst_ip,
               uint16_t dst_port UNUSED) {
  /*
   * TODO: Implement UDP output
   *
   * Steps:
   * 1. Prepend UDP header to pbuf
   * 2. Fill in header fields:
   *    - Source port (from PCB or ephemeral)
   *    - Destination port
   *    - Length = UDP_HEADER_LEN + data length
   *    - Checksum (use checksum_pseudo_header + checksum_partial)
   * 3. Call ip_output() to send
   *
   * Return 0 on success, negative on error.
   */

  printf("udp: send packet (not implemented)\n");
  pbuf_free(p);
  (void)dst_ip;
  return -1;
}

struct udp_pcb* udp_new(void) {
  /*
   * TODO: Allocate and initialize a new UDP PCB
   *
   * Initialize:
   * - All addresses/ports to 0
   * - recv_queue (list_init)
   * - recv_sem (sema_init with value 0)
   * - lock (lock_init)
   * - bound = false
   * - Add to global PCB list
   */

  printf("udp: new PCB (not implemented)\n");
  return NULL;
}

void udp_free(struct udp_pcb* pcb) {
  /*
   * TODO: Free a UDP PCB
   *
   * Steps:
   * 1. Remove from global PCB list
   * 2. Free any queued packets
   * 3. Free the PCB structure
   */

  if (pcb == NULL)
    return;

  printf("udp: free PCB (not implemented)\n");
  (void)pcb;
}

int udp_bind(struct udp_pcb* pcb, uint32_t ip, uint16_t port) {
  /*
   * TODO: Bind PCB to local address/port
   *
   * Steps:
   * 1. If port == 0, allocate ephemeral port
   * 2. Check if port already in use
   * 3. Set pcb->local_ip and pcb->local_port
   * 4. Set pcb->bound = true
   *
   * Return 0 on success, -1 if port in use.
   */

  printf("udp: bind (not implemented)\n");
  (void)pcb;
  (void)ip;
  (void)port;
  return -1;
}

int udp_connect(struct udp_pcb* pcb, uint32_t ip, uint16_t port) {
  /*
   * TODO: Set default remote address for PCB
   *
   * Just store the address - no actual "connection" happens.
   * This allows using send() instead of sendto().
   */

  printf("udp: connect (not implemented)\n");
  (void)pcb;
  (void)ip;
  (void)port;
  return -1;
}

int udp_recv(struct udp_pcb* pcb UNUSED, void* buf UNUSED, size_t len UNUSED, uint32_t* src_ip,
             uint16_t* src_port) {
  /*
   * TODO: Receive a datagram (blocking)
   *
   * Steps:
   * 1. Wait on recv_sem
   * 2. Dequeue packet from recv_queue
   * 3. Copy data to buf
   * 4. If src_ip/src_port provided, fill in sender info
   * 5. Free the pbuf
   * 6. Return bytes copied
   */

  printf("udp: recv (not implemented)\n");
  (void)src_ip;
  (void)src_port;
  return -1;
}
