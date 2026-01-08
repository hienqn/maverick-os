/**
 * @file net/tests/net_tests.h
 * @brief Network stack test suite.
 *
 * Comprehensive tests for layers 1-4 of the network stack:
 * - Packet buffers (pbuf)
 * - Network device abstraction (netdev)
 * - Ethernet frame handling
 * - ARP protocol
 * - IP layer and routing
 * - ICMP
 *
 * Run these tests before implementing UDP/TCP to ensure the
 * foundation is solid.
 */

#ifndef NET_TESTS_NET_TESTS_H
#define NET_TESTS_NET_TESTS_H

/**
 * Run all network stack tests.
 * @return Number of failed tests (0 = all passed).
 */
int net_run_all_tests(void);

/* Individual test suites - basic layer tests */
int net_test_pbuf(void);
int net_test_checksum(void);
int net_test_ethernet(void);
int net_test_arp(void);
int net_test_ip(void);
int net_test_icmp(void);
int net_test_loopback(void);
int net_test_e1000(void);

/* Advanced tests - critical for UDP/TCP implementation */
int net_test_pbuf_operations(void);
int net_test_checksum_advanced(void);
int net_test_transport_headers(void);
int net_test_ip_advanced(void);
int net_test_loopback_integration(void);

#endif /* NET_TESTS_NET_TESTS_H */
