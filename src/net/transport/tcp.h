/**
 * @file net/transport/tcp.h
 * @brief TCP protocol (Transmission Control Protocol).
 *
 * SCAFFOLD FOR YOUR IMPLEMENTATION
 * ================================
 *
 * TCP provides reliable, ordered, connection-oriented byte streams.
 * This is the most complex protocol in the stack.
 *
 * YOUR TASKS (in recommended order):
 *
 * Phase 6A - State Machine:
 * 1. Implement the 11-state TCP FSM
 * 2. Handle connection establishment (3-way handshake)
 * 3. Handle connection termination (4-way close)
 *
 * Phase 6B - Data Transfer:
 * 4. Implement sliding window protocol
 * 5. Handle out-of-order segments
 * 6. Implement retransmission buffer
 *
 * Phase 6C - Timers:
 * 7. Implement RTT estimation (Jacobson/Karels)
 * 8. Implement retransmission timeout (RTO)
 * 9. Implement TIME_WAIT timer
 *
 * Phase 6D - Congestion Control:
 * 10. Implement slow start
 * 11. Implement congestion avoidance
 * 12. Implement fast retransmit/recovery
 *
 * KEY DATA STRUCTURES:
 * - TCP PCB (Protocol Control Block): Per-connection state
 * - Send buffer: Data waiting to be sent/acknowledged
 * - Receive buffer: Data received but not read by application
 *
 * REFERENCE: RFC 793 (TCP), RFC 6298 (RTO), RFC 5681 (Congestion Control)
 */

#ifndef NET_TRANSPORT_TCP_H
#define NET_TRANSPORT_TCP_H

#include <stdint.h>
#include <stdbool.h>
#include <list.h>
#include "net/buf/pbuf.h"
#include "net/driver/netdev.h"
#include "threads/synch.h"

/**
 * @brief TCP header structure.
 */
struct tcp_hdr {
  uint16_t src_port;   /* Source port */
  uint16_t dst_port;   /* Destination port */
  uint32_t seq_num;    /* Sequence number */
  uint32_t ack_num;    /* Acknowledgment number */
  uint8_t data_offset; /* Data offset (4 bits) + reserved (4 bits) */
  uint8_t flags;       /* TCP flags */
  uint16_t window;     /* Window size */
  uint16_t checksum;   /* Checksum */
  uint16_t urgent_ptr; /* Urgent pointer */
  /* Options may follow */
} __attribute__((packed));

#define TCP_HEADER_LEN sizeof(struct tcp_hdr)

/* TCP Flags */
#define TCP_FLAG_FIN 0x01
#define TCP_FLAG_SYN 0x02
#define TCP_FLAG_RST 0x04
#define TCP_FLAG_PSH 0x08
#define TCP_FLAG_ACK 0x10
#define TCP_FLAG_URG 0x20

/* Macro to get data offset in bytes */
#define TCP_DATA_OFFSET(hdr) (((hdr)->data_offset >> 4) * 4)

/**
 * @brief TCP connection states.
 *
 * See RFC 793 for the complete state diagram.
 *
 *                              +---------+
 *                              |  CLOSED |
 *                              +---------+
 *                                |     |
 *           active OPEN         |     |  passive OPEN
 *           send SYN            |     |  (LISTEN)
 *                              \|/   \|/
 *                          +---------+ +---------+
 *                          |SYN_SENT | | LISTEN  |
 *                          +---------+ +---------+
 *                               |           |
 *            rcv SYN+ACK        |           | rcv SYN
 *            send ACK           |           | send SYN+ACK
 *                              \|/         \|/
 *                          +---------+ +---------+
 *                          |  ESTAB  |<|SYN_RCVD |
 *                          +---------+ +---------+
 *               ...continues with FIN states...
 */
enum tcp_state {
  TCP_CLOSED,      /* No connection */
  TCP_LISTEN,      /* Waiting for connection request */
  TCP_SYN_SENT,    /* SYN sent, waiting for SYN+ACK */
  TCP_SYN_RCVD,    /* SYN received, SYN+ACK sent */
  TCP_ESTABLISHED, /* Connection established */
  TCP_FIN_WAIT_1,  /* FIN sent, waiting for ACK */
  TCP_FIN_WAIT_2,  /* FIN ACKed, waiting for FIN */
  TCP_CLOSE_WAIT,  /* FIN received, waiting for close */
  TCP_CLOSING,     /* Both sides sent FIN */
  TCP_LAST_ACK,    /* Waiting for final ACK */
  TCP_TIME_WAIT    /* Waiting for 2*MSL */
};

/**
 * @brief Convert TCP state to string for debugging.
 */
const char* tcp_state_name(enum tcp_state state);

/**
 * @brief TCP Protocol Control Block.
 *
 * This structure maintains all state for a single TCP connection.
 * It's the most important data structure in TCP.
 */
struct tcp_pcb {
  /* Connection identification (socket pair) */
  uint32_t local_ip;
  uint16_t local_port;
  uint32_t remote_ip;
  uint16_t remote_port;

  /* State machine */
  enum tcp_state state;

  /*
   * Send Sequence Space (RFC 793):
   *
   *      1         2          3          4
   *  ---------|---------|---------|---------
   *         SND.UNA   SND.NXT   SND.UNA+SND.WND
   *
   * 1 - Old sequence numbers acknowledged
   * 2 - Sequence numbers sent but unacknowledged
   * 3 - Sequence numbers allowed for new data
   * 4 - Future sequence numbers not allowed
   */
  uint32_t snd_una; /* Send unacknowledged */
  uint32_t snd_nxt; /* Send next */
  uint32_t snd_wnd; /* Send window (from receiver) */
  uint32_t snd_wl1; /* Segment seq for last window update */
  uint32_t snd_wl2; /* Segment ack for last window update */
  uint32_t iss;     /* Initial send sequence number */

  /*
   * Receive Sequence Space (RFC 793):
   *
   *      1          2          3
   *  ---------|---------|---------
   *         RCV.NXT   RCV.NXT+RCV.WND
   *
   * 1 - Old sequence numbers acknowledged
   * 2 - Sequence numbers allowed for new reception
   * 3 - Future sequence numbers not yet allowed
   */
  uint32_t rcv_nxt; /* Receive next */
  uint32_t rcv_wnd; /* Receive window (our buffer) */
  uint32_t irs;     /* Initial receive sequence number */

  /* Buffers - YOU IMPLEMENT THESE */
  void* send_buf; /* Send buffer (retransmission) */
  void* recv_buf; /* Receive buffer (reordering) */

  /* Timer state - YOU IMPLEMENT THESE */
  int64_t rto;    /* Retransmission timeout (ticks) */
  int64_t srtt;   /* Smoothed RTT (scaled) */
  int64_t rttvar; /* RTT variance (scaled) */

  /* Congestion control - YOU IMPLEMENT THESE */
  uint32_t cwnd;     /* Congestion window */
  uint32_t ssthresh; /* Slow start threshold */

  /* For LISTEN sockets */
  struct list accept_queue; /* Completed connections */
  int backlog;              /* Max pending connections */
  struct tcp_pcb* parent;   /* Parent listening socket */

  /* Synchronization */
  struct lock lock;
  struct condition connect_cond;
  struct condition accept_cond;
  struct condition send_cond;
  struct condition recv_cond;

  /* Reference counting */
  int refcount;

  struct list_elem elem; /* In global PCB list */
};

/**
 * @brief Initialize TCP subsystem.
 */
void tcp_init(void);

/**
 * @brief Process received TCP segment.
 * @param dev Device that received packet.
 * @param p Packet buffer with TCP segment.
 * @param src_ip Source IP address.
 * @param dst_ip Destination IP address.
 *
 * TODO: Implement this function
 */
void tcp_input(struct netdev* dev, struct pbuf* p, uint32_t src_ip, uint32_t dst_ip);

/**
 * @brief Create a new TCP PCB.
 * @return New PCB, or NULL on failure.
 */
struct tcp_pcb* tcp_new(void);

/**
 * @brief Free a TCP PCB.
 * @param pcb PCB to free.
 */
void tcp_free(struct tcp_pcb* pcb);

/**
 * @brief Bind TCP PCB to local address/port.
 */
int tcp_bind(struct tcp_pcb* pcb, uint32_t ip, uint16_t port);

/**
 * @brief Put PCB in LISTEN state.
 */
int tcp_listen(struct tcp_pcb* pcb, int backlog);

/**
 * @brief Accept incoming connection (blocking).
 */
struct tcp_pcb* tcp_accept(struct tcp_pcb* pcb);

/**
 * @brief Initiate connection to remote host.
 */
int tcp_connect(struct tcp_pcb* pcb, uint32_t ip, uint16_t port);

/**
 * @brief Send data on established connection.
 */
int tcp_send(struct tcp_pcb* pcb, const void* data, size_t len);

/**
 * @brief Receive data from connection (blocking).
 */
int tcp_recv(struct tcp_pcb* pcb, void* buf, size_t len);

/**
 * @brief Close connection.
 */
int tcp_close(struct tcp_pcb* pcb);

/**
 * @brief Abort connection (send RST).
 */
void tcp_abort(struct tcp_pcb* pcb);

/**
 * @brief TCP timer callback (called periodically).
 */
void tcp_timer(void);

#endif /* NET_TRANSPORT_TCP_H */
