/**
 * @file net/socket/socket.c
 * @brief POSIX Socket API implementation.
 *
 * ============================================================================
 * WHY SOCKETS EXIST
 * ============================================================================
 *
 * The socket layer provides a UNIFIED INTERFACE for network communication.
 * Without it, applications would need to know the details of each protocol:
 *
 *   Without sockets:          With sockets:
 *   ─────────────────         ─────────────────
 *   struct udp_pcb* pcb;      int fd = socket(AF_INET, SOCK_DGRAM, 0);
 *   pcb = udp_new();          bind(fd, &addr, sizeof(addr));
 *   udp_bind(pcb, ip, port);  sendto(fd, buf, len, 0, &dest, sizeof(dest));
 *   struct pbuf* p = ...;     recvfrom(fd, buf, len, 0, &from, &fromlen);
 *   udp_output(pcb, p, ...);  close(fd);
 *   udp_recv(pcb, ...);
 *   udp_free(pcb);
 *
 * ============================================================================
 * ROLE IN THE NETWORK STACK
 * ============================================================================
 *
 *   ┌─────────────────────────────────────────────────────────────────────┐
 *   │  APPLICATION                                                        │
 *   │    socket(), bind(), sendto(), recvfrom(), close()                  │
 *   ├─────────────────────────────────────────────────────────────────────┤
 *   │  SOCKET LAYER  (this file)                                          │
 *   │    - Translates POSIX API to protocol-specific calls                │
 *   │    - Manages socket state (bound, connected, shutdown)              │
 *   │    - Handles byte order conversion (network <-> host)               │
 *   │    - Allocates/frees pbufs for sending                              │
 *   ├─────────────────────────────────────────────────────────────────────┤
 *   │  TRANSPORT LAYER  (udp.c, tcp.c)                                    │
 *   │    - Protocol-specific logic (ports, checksums, reliability)        │
 *   │    - UDP: udp_new, udp_bind, udp_output, udp_recv, udp_free         │
 *   │    - TCP: connection state, retransmission, flow control            │
 *   ├─────────────────────────────────────────────────────────────────────┤
 *   │  NETWORK LAYER  (ip.c)                                              │
 *   │    - Routing, IP addresses                                          │
 *   ├─────────────────────────────────────────────────────────────────────┤
 *   │  LINK LAYER  (ethernet.c, arp.c)                                    │
 *   │    - MAC addresses, physical transmission                           │
 *   └─────────────────────────────────────────────────────────────────────┘
 *
 * ============================================================================
 * WHAT THIS LAYER DOES
 * ============================================================================
 *
 * 1. ABSTRACTION: Hide protocol differences behind a common API
 *    - Same sendto()/recvfrom() works for UDP
 *    - Same send()/recv() works for TCP (when implemented)
 *
 * 2. STATE MANAGEMENT: Track socket lifecycle
 *    - bound: Has a local address/port been assigned?
 *    - connected: Is there a default remote address?
 *    - listening: Is this a TCP server socket?
 *
 * 3. BUFFER MANAGEMENT: Handle pbuf allocation for applications
 *    - App passes raw buffer to sendto()
 *    - Socket layer allocates pbuf, copies data, sends via UDP
 *
 * 4. BYTE ORDER: Convert between network and host byte order
 *    - sockaddr_in uses network byte order (big-endian)
 *    - UDP functions expect host byte order for ports
 *
 * ============================================================================
 * KEY CONCEPTS
 * ============================================================================
 *
 * - sockaddr_in fields are in NETWORK byte order
 * - Use ntohs() when passing port to udp_bind/udp_connect
 * - Use htons() when filling sockaddr_in from udp_recv
 * - pbuf is consumed by udp_output, don't free it
 */

#include "net/socket/socket.h"
#include "net/transport/tcp.h"
#include "net/transport/udp.h"
#include "net/buf/pbuf.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include <string.h>
#include <stdio.h>
#include <debug.h>

static struct lock socket_lock;
static bool socket_initialized = false;

void socket_init(void) {
  lock_init(&socket_lock);
  socket_initialized = true;
  printf("socket: initialized\n");
}

/**
 * Create a new socket.
 *
 * @param domain   Must be AF_INET
 * @param type     SOCK_DGRAM (UDP) or SOCK_STREAM (TCP)
 * @param protocol Usually 0 (auto-select based on type)
 * @return New socket, or NULL on error
 *
 * TODO:
 * 1. Validate domain == AF_INET, return NULL otherwise
 * 2. Validate type is SOCK_DGRAM or SOCK_STREAM
 * 3. Allocate struct socket with malloc()
 * 4. Initialize all fields (type, protocol, bound, connected, etc.)
 * 5. Create underlying PCB:
 *    - SOCK_DGRAM: sock->pcb.udp = udp_new()
 *    - SOCK_STREAM: sock->pcb.tcp = NULL (TCP not implemented)
 * 6. Return socket (or NULL if PCB creation failed)
 */
struct socket* socket_create(int domain, int type, int protocol) {
  (void)domain;
  (void)type;
  (void)protocol;

  /* YOUR CODE HERE */

  return NULL;
}

/**
 * Free a socket and its underlying PCB.
 *
 * @param sock Socket to free (NULL is safe)
 *
 * TODO:
 * 1. Check sock != NULL
 * 2. Free underlying PCB based on type:
 *    - SOCK_DGRAM: udp_free(sock->pcb.udp)
 *    - SOCK_STREAM: tcp_free() when implemented
 * 3. free(sock)
 */
void socket_free(struct socket* sock) {
  if (sock == NULL)
    return;

  /* YOUR CODE HERE */
}

/**
 * Bind socket to local address/port.
 *
 * @param sock Socket to bind
 * @param addr Address to bind to (network byte order)
 * @return 0 on success, -1 on error
 *
 * TODO:
 * 1. Validate sock and addr
 * 2. Check not already bound
 * 3. Call udp_bind or tcp_bind:
 *    - udp_bind(sock->pcb.udp, addr->sin_addr, ntohs(addr->sin_port))
 * 4. On success: set sock->bound = true, copy addr to sock->local_addr
 * 5. Return 0 on success, -1 on failure
 */
int socket_bind(struct socket* sock, const struct sockaddr_in* addr) {
  (void)sock;
  (void)addr;

  /* YOUR CODE HERE */

  return -1;
}

/**
 * Start listening for connections (TCP only).
 *
 * @param sock    Socket (must be bound, SOCK_STREAM)
 * @param backlog Max pending connections
 * @return 0 on success, -1 on error
 *
 * TODO (TCP only, return -1 for UDP):
 * 1. Validate sock is SOCK_STREAM
 * 2. Call tcp_listen() when implemented
 * 3. Set sock->listening = true
 */
int socket_listen(struct socket* sock, int backlog) {
  (void)sock;
  (void)backlog;

  /* YOUR CODE HERE - TCP only */

  return -1;
}

/**
 * Accept incoming connection (TCP only).
 *
 * @param sock Listening socket
 * @param addr Output: client address (can be NULL)
 * @return New connected socket, or NULL on error
 *
 * TODO (TCP only, return NULL for UDP):
 * 1. Validate sock is listening SOCK_STREAM
 * 2. Call tcp_accept() to get new PCB (blocks)
 * 3. Create new socket wrapping the accepted PCB
 * 4. Fill addr if provided
 * 5. Return new socket
 */
struct socket* socket_accept(struct socket* sock, struct sockaddr_in* addr) {
  (void)sock;
  (void)addr;

  /* YOUR CODE HERE - TCP only */

  return NULL;
}

/**
 * Connect to remote address.
 *
 * @param sock Socket
 * @param addr Remote address (network byte order)
 * @return 0 on success, -1 on error
 *
 * TODO:
 * 1. Validate sock and addr
 * 2. For SOCK_DGRAM:
 *    - udp_connect(sock->pcb.udp, addr->sin_addr, ntohs(addr->sin_port))
 *    - Just stores remote address, no actual connection
 * 3. For SOCK_STREAM:
 *    - tcp_connect() when implemented (blocking)
 * 4. On success: set sock->connected = true, copy addr to sock->remote_addr
 */
int socket_connect(struct socket* sock, const struct sockaddr_in* addr) {
  (void)sock;
  (void)addr;

  /* YOUR CODE HERE */

  return -1;
}

/**
 * Send data on connected socket.
 *
 * @param sock  Connected socket
 * @param buf   Data to send
 * @param len   Data length
 * @param flags Ignored for now
 * @return Bytes sent, or -1 on error
 *
 * TODO:
 * 1. Validate sock is connected
 * 2. For SOCK_DGRAM: use socket_sendto() with sock->remote_addr
 * 3. For SOCK_STREAM: tcp_send() when implemented
 */
int socket_send(struct socket* sock, const void* buf, size_t len, int flags) {
  (void)sock;
  (void)buf;
  (void)len;
  (void)flags;

  /* YOUR CODE HERE */

  return -1;
}

/**
 * Receive data from socket.
 *
 * @param sock  Socket
 * @param buf   Buffer for data
 * @param len   Buffer size
 * @param flags Ignored for now
 * @return Bytes received, or -1 on error
 *
 * TODO:
 * 1. For SOCK_DGRAM: use socket_recvfrom() with NULL addr
 * 2. For SOCK_STREAM: tcp_recv() when implemented
 */
int socket_recv(struct socket* sock, void* buf, size_t len, int flags) {
  (void)sock;
  (void)buf;
  (void)len;
  (void)flags;

  /* YOUR CODE HERE */

  return -1;
}

/**
 * Send datagram to specific address (UDP).
 *
 * @param sock  Socket (SOCK_DGRAM)
 * @param buf   Data to send
 * @param len   Data length
 * @param flags Ignored
 * @param addr  Destination address (network byte order)
 * @return Bytes sent, or -1 on error
 *
 * TODO:
 * 1. Validate sock is SOCK_DGRAM
 * 2. Allocate pbuf: pbuf_alloc(PBUF_TRANSPORT, len, PBUF_RAM)
 * 3. Copy data: memcpy(p->payload, buf, len)
 * 4. Send: udp_output(sock->pcb.udp, p, addr->sin_addr, ntohs(addr->sin_port))
 * 5. NOTE: pbuf is consumed by udp_output, don't free it!
 * 6. Return len on success, -1 on failure
 */
int socket_sendto(struct socket* sock, const void* buf, size_t len, int flags,
                  const struct sockaddr_in* addr) {
  (void)sock;
  (void)buf;
  (void)len;
  (void)flags;
  (void)addr;

  /* YOUR CODE HERE */

  return -1;
}

/**
 * Receive datagram with source address (UDP).
 *
 * @param sock  Socket (SOCK_DGRAM)
 * @param buf   Buffer for data
 * @param len   Buffer size
 * @param flags Ignored
 * @param addr  Output: source address (can be NULL)
 * @return Bytes received, or -1 on error
 *
 * TODO:
 * 1. Validate sock is SOCK_DGRAM
 * 2. Call: udp_recv(sock->pcb.udp, buf, len, &src_ip, &src_port)
 *    - This blocks until a packet arrives
 * 3. If addr != NULL, fill it in:
 *    - addr->sin_family = AF_INET
 *    - addr->sin_addr = src_ip (already network order)
 *    - addr->sin_port = htons(src_port)
 * 4. Return bytes received
 */
int socket_recvfrom(struct socket* sock, void* buf, size_t len, int flags,
                    struct sockaddr_in* addr) {
  (void)sock;
  (void)buf;
  (void)len;
  (void)flags;
  (void)addr;

  /* YOUR CODE HERE */

  return -1;
}

/**
 * Shutdown socket.
 *
 * @param sock Socket
 * @param how  SHUT_RD, SHUT_WR, or SHUT_RDWR
 * @return 0 on success, -1 on error
 *
 * TODO:
 * 1. Set sock->shutdown_read and/or sock->shutdown_write based on 'how'
 * 2. For TCP: initiate FIN sequence when implemented
 */
int socket_shutdown(struct socket* sock, int how) {
  (void)sock;
  (void)how;

  /* YOUR CODE HERE */

  return -1;
}
