/**
 * @file net/socket/socket.h
 * @brief BSD-style socket API.
 *
 * SCAFFOLD FOR YOUR IMPLEMENTATION
 * ================================
 *
 * This provides the user-facing socket API (syscalls).
 * It wraps the TCP and UDP protocol control blocks.
 *
 * YOUR TASKS:
 * 1. Implement socket creation and management
 * 2. Integrate with file descriptor table
 * 3. Implement blocking operations with proper synchronization
 * 4. Add socket syscalls to syscall.c
 *
 * SOCKET SYSCALLS TO IMPLEMENT:
 * - SYS_SOCKET:    Create a socket
 * - SYS_BIND:      Bind to local address
 * - SYS_LISTEN:    Start listening (TCP)
 * - SYS_ACCEPT:    Accept connection (TCP)
 * - SYS_CONNECT:   Connect to remote (TCP/UDP)
 * - SYS_SEND:      Send data
 * - SYS_RECV:      Receive data
 * - SYS_SENDTO:    Send to specific address (UDP)
 * - SYS_RECVFROM:  Receive with source address (UDP)
 * - SYS_SHUTDOWN:  Shutdown connection
 */

#ifndef NET_SOCKET_SOCKET_H
#define NET_SOCKET_SOCKET_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Address families */
#define AF_INET 2 /* IPv4 */

/* Socket types */
#define SOCK_STREAM 1 /* TCP */
#define SOCK_DGRAM 2  /* UDP */

/* Protocol numbers (usually 0 = default) */
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17

/* Shutdown modes */
#define SHUT_RD 0   /* No more reads */
#define SHUT_WR 1   /* No more writes */
#define SHUT_RDWR 2 /* No more reads or writes */

/**
 * @brief Socket address structure (IPv4).
 *
 * This is what user programs pass to bind/connect/etc.
 */
struct sockaddr_in {
  uint16_t sin_family; /* AF_INET */
  uint16_t sin_port;   /* Port (network order) */
  uint32_t sin_addr;   /* IP address (network order) */
  uint8_t sin_zero[8]; /* Padding */
};

#define SOCKADDR_IN_SIZE sizeof(struct sockaddr_in)

/**
 * @brief Generic socket address (for API compatibility).
 */
struct sockaddr {
  uint16_t sa_family;  /* Address family */
  uint8_t sa_data[14]; /* Address data */
};

/**
 * @brief Kernel socket structure.
 */
struct socket {
  int type;     /* SOCK_STREAM or SOCK_DGRAM */
  int protocol; /* IPPROTO_TCP or IPPROTO_UDP */

  union {
    struct tcp_pcb* tcp; /* TCP protocol control block */
    struct udp_pcb* udp; /* UDP protocol control block */
  } pcb;

  /* Bound addresses */
  struct sockaddr_in local_addr;
  struct sockaddr_in remote_addr;

  /* State flags */
  bool bound;
  bool connected;
  bool listening;
  bool shutdown_read;
  bool shutdown_write;

  /* Reference count */
  int refcount;
};

/**
 * @brief Initialize socket subsystem.
 */
void socket_init(void);

/**
 * @brief Create a new socket.
 * @param domain Address family (AF_INET).
 * @param type Socket type (SOCK_STREAM or SOCK_DGRAM).
 * @param protocol Protocol (0 for default).
 * @return Socket pointer, or NULL on error.
 *
 * TODO: Implement this function
 */
struct socket* socket_create(int domain, int type, int protocol);

/**
 * @brief Free a socket.
 * @param sock Socket to free.
 *
 * TODO: Implement this function
 */
void socket_free(struct socket* sock);

/**
 * @brief Bind socket to local address.
 * @param sock Socket.
 * @param addr Address to bind to.
 * @return 0 on success, negative on error.
 *
 * TODO: Implement this function
 */
int socket_bind(struct socket* sock, const struct sockaddr_in* addr);

/**
 * @brief Start listening for connections (TCP).
 * @param sock Socket.
 * @param backlog Maximum pending connections.
 * @return 0 on success, negative on error.
 *
 * TODO: Implement this function
 */
int socket_listen(struct socket* sock, int backlog);

/**
 * @brief Accept incoming connection (TCP, blocking).
 * @param sock Listening socket.
 * @param addr Output: client address (can be NULL).
 * @return New connected socket, or NULL on error.
 *
 * TODO: Implement this function
 */
struct socket* socket_accept(struct socket* sock, struct sockaddr_in* addr);

/**
 * @brief Connect to remote address.
 * @param sock Socket.
 * @param addr Remote address.
 * @return 0 on success, negative on error.
 *
 * For TCP: blocking until connected or error.
 * For UDP: just sets default destination.
 *
 * TODO: Implement this function
 */
int socket_connect(struct socket* sock, const struct sockaddr_in* addr);

/**
 * @brief Send data on socket.
 * @param sock Socket.
 * @param buf Data buffer.
 * @param len Data length.
 * @param flags Send flags (currently ignored).
 * @return Bytes sent, or negative on error.
 *
 * TODO: Implement this function
 */
int socket_send(struct socket* sock, const void* buf, size_t len, int flags);

/**
 * @brief Receive data from socket.
 * @param sock Socket.
 * @param buf Buffer for data.
 * @param len Buffer size.
 * @param flags Receive flags (currently ignored).
 * @return Bytes received, 0 on EOF, negative on error.
 *
 * TODO: Implement this function
 */
int socket_recv(struct socket* sock, void* buf, size_t len, int flags);

/**
 * @brief Send data to specific address (UDP).
 * @param sock Socket.
 * @param buf Data buffer.
 * @param len Data length.
 * @param flags Send flags.
 * @param addr Destination address.
 * @return Bytes sent, or negative on error.
 *
 * TODO: Implement this function
 */
int socket_sendto(struct socket* sock, const void* buf, size_t len, int flags,
                  const struct sockaddr_in* addr);

/**
 * @brief Receive data with source address (UDP).
 * @param sock Socket.
 * @param buf Buffer for data.
 * @param len Buffer size.
 * @param flags Receive flags.
 * @param addr Output: source address (can be NULL).
 * @return Bytes received, or negative on error.
 *
 * TODO: Implement this function
 */
int socket_recvfrom(struct socket* sock, void* buf, size_t len, int flags,
                    struct sockaddr_in* addr);

/**
 * @brief Shutdown part of connection.
 * @param sock Socket.
 * @param how SHUT_RD, SHUT_WR, or SHUT_RDWR.
 * @return 0 on success, negative on error.
 *
 * TODO: Implement this function
 */
int socket_shutdown(struct socket* sock, int how);

#endif /* NET_SOCKET_SOCKET_H */
