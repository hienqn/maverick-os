/**
 * @file net/net.h
 * @brief Main network subsystem header.
 *
 * This is the primary header for the network stack.
 * Include this to get access to all network functionality.
 */

#ifndef NET_NET_H
#define NET_NET_H

/* Utilities */
#include "net/util/byteorder.h"
#include "net/util/checksum.h"

/* Buffer management */
#include "net/buf/pbuf.h"

/* Device layer */
#include "net/driver/netdev.h"
#include "net/driver/loopback.h"
#include "net/driver/e1000.h"

/* Link layer */
#include "net/link/ethernet.h"
#include "net/link/arp.h"

/* Internet layer */
#include "net/inet/ip.h"
#include "net/inet/icmp.h"
#include "net/inet/route.h"

/* Transport layer (scaffolds for your implementation) */
#include "net/transport/udp.h"
#include "net/transport/tcp.h"

/* Socket layer (scaffold for your implementation) */
#include "net/socket/socket.h"

/**
 * @brief Initialize the entire network stack.
 *
 * Call this once during kernel initialization, after memory
 * and threading are available but before filesystems.
 */
void net_init(void);

/**
 * @brief Start the network input thread.
 *
 * This should be called after net_init() once the scheduler is running.
 * The input thread processes received packets from all devices.
 */
void net_start(void);

/**
 * @brief Configure the default network interface.
 * @param ip IP address string (e.g., "10.0.2.15").
 * @param netmask Netmask string (e.g., "255.255.255.0").
 * @param gateway Gateway string (e.g., "10.0.2.2").
 *
 * This is typically called with QEMU's default user networking addresses.
 */
void net_configure(const char* ip, const char* netmask, const char* gateway);

/**
 * @brief Print network stack status.
 */
void net_print_status(void);

/**
 * @brief Send test ping to verify network stack.
 * @param dest_ip Destination IP address string.
 *
 * Sends 3 ICMP echo requests to the given IP and waits for replies.
 */
void net_ping_test(const char* dest_ip);

/**
 * @brief Run all network stack tests.
 * @return Number of failed tests (0 = all passed).
 *
 * Comprehensive test suite for layers 1-4. Run this before
 * implementing UDP/TCP to ensure the foundation is solid.
 */
int net_run_all_tests(void);

#endif /* NET_NET_H */
