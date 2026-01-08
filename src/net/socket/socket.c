/**
 * @file net/socket/socket.c
 * @brief Socket API implementation.
 *
 * SCAFFOLD - IMPLEMENT ME!
 *
 * This wraps TCP and UDP PCBs in a user-friendly socket abstraction.
 * It will be called from socket syscalls in userprog/syscall.c.
 *
 * IMPLEMENTATION STEPS:
 * 1. socket_create() - Allocate socket, create underlying PCB
 * 2. socket_bind() - Call tcp_bind/udp_bind
 * 3. socket_listen() - Call tcp_listen
 * 4. socket_connect() - Call tcp_connect/udp_connect
 * 5. socket_accept() - Call tcp_accept
 * 6. socket_send/recv() - Call tcp_send/recv or udp_output/recv
 * 7. socket_close() - Call tcp_close/udp_free
 *
 * INTEGRATION WITH SYSCALLS:
 * Add socket syscalls to userprog/syscall.c:
 *   - Allocate file descriptor for socket
 *   - Map FD operations to socket operations
 *   - Handle close() for sockets
 */

#include "net/socket/socket.h"
#include "net/transport/tcp.h"
#include "net/transport/udp.h"
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
  printf("socket: initialized (SCAFFOLD - implement me!)\n");
}

struct socket* socket_create(int domain, int type, int protocol) {
  /*
   * TODO: Create a new socket
   *
   * Steps:
   * 1. Validate domain == AF_INET
   * 2. Validate type is SOCK_STREAM or SOCK_DGRAM
   * 3. Allocate socket structure
   * 4. Create underlying PCB:
   *    - SOCK_STREAM -> tcp_new()
   *    - SOCK_DGRAM  -> udp_new()
   * 5. Initialize socket fields
   * 6. Return socket
   */

  printf("socket: create (not implemented)\n");
  (void)domain;
  (void)type;
  (void)protocol;
  return NULL;
}

void socket_free(struct socket* sock) {
  /*
   * TODO: Free a socket
   *
   * Steps:
   * 1. Free underlying PCB:
   *    - SOCK_STREAM -> tcp_free()
   *    - SOCK_DGRAM  -> udp_free()
   * 2. Free socket structure
   */

  if (sock == NULL)
    return;

  printf("socket: free (not implemented)\n");
  (void)sock;
}

int socket_bind(struct socket* sock, const struct sockaddr_in* addr) {
  /*
   * TODO: Bind socket to local address
   *
   * Call tcp_bind or udp_bind as appropriate.
   */

  printf("socket: bind (not implemented)\n");
  (void)sock;
  (void)addr;
  return -1;
}

int socket_listen(struct socket* sock, int backlog) {
  /*
   * TODO: Start listening (TCP only)
   *
   * Call tcp_listen.
   */

  printf("socket: listen (not implemented)\n");
  (void)sock;
  (void)backlog;
  return -1;
}

struct socket* socket_accept(struct socket* sock, struct sockaddr_in* addr) {
  /*
   * TODO: Accept incoming connection (TCP only)
   *
   * Steps:
   * 1. Call tcp_accept to get new PCB
   * 2. Create new socket for accepted connection
   * 3. Fill in addr if provided
   * 4. Return new socket
   */

  printf("socket: accept (not implemented)\n");
  (void)sock;
  (void)addr;
  return NULL;
}

int socket_connect(struct socket* sock, const struct sockaddr_in* addr) {
  /*
   * TODO: Connect to remote address
   *
   * TCP: Call tcp_connect (blocking)
   * UDP: Call udp_connect (just sets destination)
   */

  printf("socket: connect (not implemented)\n");
  (void)sock;
  (void)addr;
  return -1;
}

int socket_send(struct socket* sock, const void* buf, size_t len, int flags) {
  /*
   * TODO: Send data
   *
   * TCP: Call tcp_send
   * UDP: Call udp_output with connected address
   */

  printf("socket: send (not implemented)\n");
  (void)sock;
  (void)buf;
  (void)len;
  (void)flags;
  return -1;
}

int socket_recv(struct socket* sock, void* buf, size_t len, int flags) {
  /*
   * TODO: Receive data
   *
   * TCP: Call tcp_recv
   * UDP: Call udp_recv
   */

  printf("socket: recv (not implemented)\n");
  (void)sock;
  (void)buf;
  (void)len;
  (void)flags;
  return -1;
}

int socket_sendto(struct socket* sock, const void* buf, size_t len, int flags,
                  const struct sockaddr_in* addr) {
  /*
   * TODO: Send to specific address (UDP)
   *
   * Call udp_output with specified address.
   */

  printf("socket: sendto (not implemented)\n");
  (void)sock;
  (void)buf;
  (void)len;
  (void)flags;
  (void)addr;
  return -1;
}

int socket_recvfrom(struct socket* sock, void* buf, size_t len, int flags,
                    struct sockaddr_in* addr) {
  /*
   * TODO: Receive with source address (UDP)
   *
   * Call udp_recv and fill in source address.
   */

  printf("socket: recvfrom (not implemented)\n");
  (void)sock;
  (void)buf;
  (void)len;
  (void)flags;
  (void)addr;
  return -1;
}

int socket_shutdown(struct socket* sock, int how) {
  /*
   * TODO: Shutdown connection
   *
   * For TCP, this initiates FIN sequence.
   */

  printf("socket: shutdown (not implemented)\n");
  (void)sock;
  (void)how;
  return -1;
}
