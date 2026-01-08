/**
 * @file net/transport/udp.h
 * @brief UDP protocol (User Datagram Protocol).
 *
 * SCAFFOLD FOR YOUR IMPLEMENTATION
 * ================================
 *
 * UDP is a simple, connectionless transport protocol.
 * Each UDP datagram is independent and may arrive out of order
 * or not at all.
 *
 * YOUR TASKS:
 * 1. Implement udp_input() to process received datagrams
 * 2. Implement udp_output() to send datagrams
 * 3. Implement UDP PCB (Protocol Control Block) management
 * 4. Implement port demultiplexing
 *
 * KEY CONCEPTS:
 * - No connection state (unlike TCP)
 * - No reliability guarantees
 * - Pseudo-header checksum (includes IP addresses)
 * - Port-based demultiplexing
 */

#ifndef NET_TRANSPORT_UDP_H
#define NET_TRANSPORT_UDP_H

#include <stdint.h>
#include <stdbool.h>
#include <list.h>
#include "net/buf/pbuf.h"
#include "net/driver/netdev.h"

/**
 * @brief UDP header structure.
 */
struct udp_hdr {
  uint16_t src_port; /* Source port */
  uint16_t dst_port; /* Destination port */
  uint16_t length;   /* UDP length (header + data) */
  uint16_t checksum; /* Checksum (optional for IPv4) */
} __attribute__((packed));

#define UDP_HEADER_LEN sizeof(struct udp_hdr)

/**
 * @brief UDP Protocol Control Block.
 *
 * Each bound UDP socket has a PCB that tracks:
 * - Local and remote addresses/ports
 * - Receive queue for incoming datagrams
 *
 * TODO: Implement this structure
 */
struct udp_pcb {
  uint32_t local_ip;    /* Local IP (0 = any) */
  uint16_t local_port;  /* Local port */
  uint32_t remote_ip;   /* Remote IP (0 = any) */
  uint16_t remote_port; /* Remote port (0 = any) */

  /* Receive queue - datagrams waiting to be read */
  struct list recv_queue;
  struct semaphore recv_sem;
  struct lock lock;

  /* Flags */
  bool bound; /* Socket is bound */

  struct list_elem elem; /* In global PCB list */
};

/**
 * @brief Initialize UDP subsystem.
 */
void udp_init(void);

/**
 * @brief Process received UDP datagram.
 * @param dev Device that received packet.
 * @param p Packet buffer with UDP datagram.
 * @param src_ip Source IP address.
 * @param dst_ip Destination IP address.
 *
 * TODO: Implement this function
 * - Validate header and checksum
 * - Find matching PCB by port
 * - Queue datagram for receiving socket
 */
void udp_input(struct netdev* dev, struct pbuf* p, uint32_t src_ip, uint32_t dst_ip);

/**
 * @brief Send UDP datagram.
 * @param pcb UDP PCB (can be NULL for unbound send).
 * @param p Packet buffer with data.
 * @param dst_ip Destination IP.
 * @param dst_port Destination port.
 * @return 0 on success, negative on error.
 *
 * TODO: Implement this function
 * - Build UDP header
 * - Calculate checksum with pseudo-header
 * - Send via ip_output()
 */
int udp_output(struct udp_pcb* pcb, struct pbuf* p, uint32_t dst_ip, uint16_t dst_port);

/**
 * @brief Create a new UDP PCB.
 * @return New PCB, or NULL on failure.
 *
 * TODO: Implement this function
 */
struct udp_pcb* udp_new(void);

/**
 * @brief Free a UDP PCB.
 * @param pcb PCB to free.
 *
 * TODO: Implement this function
 */
void udp_free(struct udp_pcb* pcb);

/**
 * @brief Bind UDP PCB to local address/port.
 * @param pcb PCB to bind.
 * @param ip Local IP (0 for any).
 * @param port Local port.
 * @return 0 on success, negative if port in use.
 *
 * TODO: Implement this function
 */
int udp_bind(struct udp_pcb* pcb, uint32_t ip, uint16_t port);

/**
 * @brief Connect UDP PCB to remote address/port.
 * @param pcb PCB to connect.
 * @param ip Remote IP.
 * @param port Remote port.
 * @return 0 on success.
 *
 * Note: UDP "connect" just sets default destination,
 * no actual connection is made.
 *
 * TODO: Implement this function
 */
int udp_connect(struct udp_pcb* pcb, uint32_t ip, uint16_t port);

/**
 * @brief Receive datagram from UDP PCB.
 * @param pcb PCB to receive from.
 * @param buf Buffer for data.
 * @param len Buffer size.
 * @param src_ip Output: source IP (can be NULL).
 * @param src_port Output: source port (can be NULL).
 * @return Bytes received, or negative on error.
 *
 * Blocks until datagram is available.
 *
 * TODO: Implement this function
 */
int udp_recv(struct udp_pcb* pcb, void* buf, size_t len, uint32_t* src_ip, uint16_t* src_port);

#endif /* NET_TRANSPORT_UDP_H */
