/**
 * @file net/transport/tcp.c
 * @brief TCP protocol implementation.
 *
 * SCAFFOLD - IMPLEMENT ME!
 *
 * This is the most complex part of the network stack.
 * Take your time and implement in phases.
 *
 * PHASE 6A - STATE MACHINE:
 * - tcp_input(): Route to appropriate state handler
 * - Handle SYN (passive open), SYN+ACK (active open)
 * - Handle FIN (connection close)
 * - Handle RST (connection abort)
 *
 * PHASE 6B - DATA TRANSFER:
 * - Send buffer: queue data, track what's sent/acked
 * - Receive buffer: handle out-of-order, deliver in order
 * - Window management
 *
 * PHASE 6C - TIMERS:
 * - RTT measurement and RTO calculation
 * - Retransmission on timeout
 * - TIME_WAIT timer
 *
 * PHASE 6D - CONGESTION CONTROL:
 * - Slow start (exponential increase)
 * - Congestion avoidance (linear increase)
 * - Fast retransmit (3 dup ACKs)
 * - Fast recovery
 */

#include "net/transport/tcp.h"
#include "net/inet/ip.h"
#include "net/util/checksum.h"
#include "net/util/byteorder.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include <string.h>
#include <stdio.h>
#include <debug.h>

/* Global list of TCP PCBs */
static struct list tcp_pcb_list;
static struct lock tcp_lock;
static bool tcp_initialized = false;

/* Next ephemeral port */
static uint16_t tcp_next_port = 49152;

/* TCP state names for debugging */
static const char* state_names[] = {"CLOSED",      "LISTEN",     "SYN_SENT",   "SYN_RCVD",
                                    "ESTABLISHED", "FIN_WAIT_1", "FIN_WAIT_2", "CLOSE_WAIT",
                                    "CLOSING",     "LAST_ACK",   "TIME_WAIT"};

const char* tcp_state_name(enum tcp_state state) {
  if (state <= TCP_TIME_WAIT)
    return state_names[state];
  return "UNKNOWN";
}

void tcp_init(void) {
  list_init(&tcp_pcb_list);
  lock_init(&tcp_lock);
  tcp_initialized = true;
  printf("tcp: initialized (SCAFFOLD - implement me!)\n");
}

void tcp_input(struct netdev* dev UNUSED, struct pbuf* p, uint32_t src_ip UNUSED,
               uint32_t dst_ip UNUSED) {
  /*
   * TODO: Implement TCP input processing
   *
   * This is the main entry point for received TCP segments.
   * It should:
   *
   * 1. Validate segment:
   *    - Check length >= TCP_HEADER_LEN
   *    - Verify checksum (with pseudo-header)
   *    - Extract header fields
   *
   * 2. Find matching PCB:
   *    - Look up by (local_ip, local_port, remote_ip, remote_port)
   *    - Also check LISTEN sockets (just local_port)
   *
   * 3. Process based on current state:
   *
   *    CLOSED:
   *      - Send RST (unless incoming is RST)
   *
   *    LISTEN:
   *      - If SYN: create new PCB, send SYN+ACK, go to SYN_RCVD
   *
   *    SYN_SENT:
   *      - If SYN+ACK: send ACK, go to ESTABLISHED
   *      - If SYN: send SYN+ACK, go to SYN_RCVD (simultaneous open)
   *
   *    SYN_RCVD:
   *      - If ACK: go to ESTABLISHED, add to parent's accept queue
   *
   *    ESTABLISHED:
   *      - If FIN: send ACK, go to CLOSE_WAIT
   *      - If data: process, send ACK, add to receive buffer
   *      - If ACK: update send window, free acked data
   *
   *    FIN_WAIT_1:
   *      - If FIN+ACK: send ACK, go to TIME_WAIT
   *      - If FIN: send ACK, go to CLOSING
   *      - If ACK: go to FIN_WAIT_2
   *
   *    FIN_WAIT_2:
   *      - If FIN: send ACK, go to TIME_WAIT
   *
   *    CLOSE_WAIT:
   *      - Wait for close() from application
   *
   *    CLOSING:
   *      - If ACK: go to TIME_WAIT
   *
   *    LAST_ACK:
   *      - If ACK: go to CLOSED
   *
   *    TIME_WAIT:
   *      - If FIN: resend ACK
   *      - After 2*MSL: go to CLOSED
   */

  printf("tcp: received segment (not implemented)\n");
  pbuf_free(p);
}

struct tcp_pcb* tcp_new(void) {
  /*
   * TODO: Allocate and initialize a new TCP PCB
   *
   * Initialize all fields to appropriate defaults:
   * - State: CLOSED
   * - Addresses/ports: 0
   * - Sequence numbers: 0 (will be set on connect/accept)
   * - Windows: reasonable defaults (e.g., 65535)
   * - Congestion: cwnd = 1 MSS, ssthresh = 65535
   * - Synchronization primitives
   */

  printf("tcp: new PCB (not implemented)\n");
  return NULL;
}

void tcp_free(struct tcp_pcb* pcb) {
  if (pcb == NULL)
    return;

  printf("tcp: free PCB (not implemented)\n");
  (void)pcb;
}

int tcp_bind(struct tcp_pcb* pcb, uint32_t ip, uint16_t port) {
  /*
   * TODO: Bind to local address/port
   *
   * - If port == 0, allocate ephemeral port
   * - Check port not already in use
   * - Update PCB
   */

  printf("tcp: bind (not implemented)\n");
  (void)pcb;
  (void)ip;
  (void)port;
  return -1;
}

int tcp_listen(struct tcp_pcb* pcb, int backlog) {
  /*
   * TODO: Put socket in LISTEN state
   *
   * - PCB must be bound
   * - Initialize accept queue
   * - Set state to LISTEN
   */

  printf("tcp: listen (not implemented)\n");
  (void)pcb;
  (void)backlog;
  return -1;
}

struct tcp_pcb* tcp_accept(struct tcp_pcb* pcb) {
  /*
   * TODO: Accept incoming connection (blocking)
   *
   * - Wait on accept_cond until accept_queue not empty
   * - Dequeue and return new PCB
   */

  printf("tcp: accept (not implemented)\n");
  (void)pcb;
  return NULL;
}

int tcp_connect(struct tcp_pcb* pcb, uint32_t ip, uint16_t port) {
  /*
   * TODO: Active open (blocking)
   *
   * 1. If not bound, bind to ephemeral port
   * 2. Generate ISS (Initial Send Sequence number)
   * 3. Send SYN segment
   * 4. Set state to SYN_SENT
   * 5. Wait on connect_cond for ESTABLISHED or error
   *
   * Return 0 on success, negative on error.
   */

  printf("tcp: connect (not implemented)\n");
  (void)pcb;
  (void)ip;
  (void)port;
  return -1;
}

int tcp_send(struct tcp_pcb* pcb, const void* data, size_t len) {
  /*
   * TODO: Send data on connection (may block)
   *
   * 1. Check state is ESTABLISHED
   * 2. Add data to send buffer
   * 3. Transmit what the window allows
   * 4. If buffer full, wait on send_cond
   *
   * Return bytes sent, or negative on error.
   */

  printf("tcp: send (not implemented)\n");
  (void)pcb;
  (void)data;
  (void)len;
  return -1;
}

int tcp_recv(struct tcp_pcb* pcb, void* buf, size_t len) {
  /*
   * TODO: Receive data from connection (blocking)
   *
   * 1. Wait on recv_cond until data available or FIN received
   * 2. Copy data from receive buffer
   * 3. Update receive window
   * 4. Return bytes read (0 on EOF/FIN)
   */

  printf("tcp: recv (not implemented)\n");
  (void)pcb;
  (void)buf;
  (void)len;
  return -1;
}

int tcp_close(struct tcp_pcb* pcb) {
  /*
   * TODO: Close connection
   *
   * From ESTABLISHED or CLOSE_WAIT:
   * - Send FIN
   * - Wait for ACK
   * - Eventually free PCB
   *
   * State transitions:
   * ESTABLISHED -> FIN_WAIT_1 -> FIN_WAIT_2 -> TIME_WAIT -> CLOSED
   * CLOSE_WAIT -> LAST_ACK -> CLOSED
   */

  printf("tcp: close (not implemented)\n");
  (void)pcb;
  return -1;
}

void tcp_abort(struct tcp_pcb* pcb) {
  /*
   * TODO: Abort connection
   *
   * - Send RST
   * - Free PCB immediately
   */

  printf("tcp: abort (not implemented)\n");
  (void)pcb;
}

void tcp_timer(void) {
  /*
   * TODO: TCP timer processing
   *
   * Called periodically to handle:
   * - Retransmission timeouts
   * - TIME_WAIT expiration
   * - Keepalive (optional)
   */
}
