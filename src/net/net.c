/**
 * @file net/net.c
 * @brief Network subsystem initialization.
 */

#include "net/net.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "devices/timer.h"
#include <stdio.h>
#include <string.h>

/* Network input thread */
static tid_t net_thread_tid;
static bool net_running = false;

/* Forward declaration */
static void net_input_thread(void* aux);

void net_init(void) {
  printf("net: initializing network stack\n");

  /* Initialize utilities */
  pbuf_init();

  /* Initialize device layer */
  netdev_init();

  /* Initialize link layer */
  ethernet_init();
  arp_init();

  /* Initialize network layer */
  route_init();
  ip_init();
  icmp_init();

  /* Initialize transport layer (scaffolds) */
  udp_init();
  tcp_init();

  /* Initialize socket layer (scaffold) */
  socket_init();

  /* Initialize drivers */
  loopback_init();
  e1000_driver_init();

  printf("net: initialization complete\n");
}

void net_start(void) {
  if (net_running)
    return;

  net_running = true;

  /* Create network input thread */
  net_thread_tid = thread_create("net_input", PRI_DEFAULT, net_input_thread, NULL);
  if (net_thread_tid == TID_ERROR) {
    printf("net: failed to create input thread\n");
    net_running = false;
    return;
  }

  printf("net: input thread started\n");
}

void net_configure(const char* ip_str, const char* mask_str, const char* gw_str) {
  struct netdev* dev;
  uint32_t ip, mask, gw;

  /* Parse addresses */
  ip = ip_addr_from_str(ip_str);
  mask = ip_addr_from_str(mask_str);
  gw = ip_addr_from_str(gw_str);

  if (ip == 0) {
    printf("net: invalid IP address: %s\n", ip_str);
    return;
  }

  /* Configure default (non-loopback) device */
  dev = netdev_get_default();
  if (dev == NULL) {
    /* No device up yet, try to find eth0 */
    dev = netdev_find_by_name("eth0");
  }

  if (dev == NULL) {
    printf("net: no network device available\n");
    return;
  }

  /* Set IP configuration */
  netdev_set_ip(dev, ip, mask, gw);
  netdev_up(dev);

  /* Set default route */
  if (gw != 0) {
    route_set_default(gw, dev);
  }

  printf("net: configured %s with IP %s\n", dev->name, ip_str);

  /* Send gratuitous ARP to announce our presence */
  arp_announce(dev);
}

void net_print_status(void) {
  printf("\n=== Network Status ===\n");
  netdev_print_stats();
  arp_print_cache();
  route_print();
}

/* Simple ping test to verify network stack */
void net_ping_test(const char* dest_ip) {
  struct netdev* dev;
  uint32_t dst_ip;
  int i;

  dst_ip = ip_addr_from_str(dest_ip);
  if (dst_ip == 0) {
    printf("net: invalid IP address: %s\n", dest_ip);
    return;
  }

  dev = netdev_get_default();
  if (dev == NULL) {
    dev = netdev_find_by_name("eth0");
  }
  if (dev == NULL) {
    printf("net: no network device available\n");
    return;
  }

  printf("net: sending 3 ICMP echo requests to %s...\n", dest_ip);

  for (i = 0; i < 3; i++) {
    int result = icmp_echo_request(dev, dst_ip, 1234, i + 1, "ping", 4);
    if (result == 0) {
      printf("  ping %d: sent (waiting for reply...)\n", i + 1);
    } else {
      printf("  ping %d: failed to send (error %d)\n", i + 1, result);
    }
    /* Give some time for the reply */
    timer_msleep(1000);
  }

  printf("net: ping test complete. Check above for ICMP replies.\n");
}

/*
 * Network input thread.
 * Polls all devices for received packets and processes them.
 */
static void net_input_thread(void* aux UNUSED) {
  struct netdev *lo, *eth;

  lo = netdev_get_loopback();
  eth = netdev_find_by_name("eth0");

  while (net_running) {
    struct pbuf* p = NULL;
    bool processed = false;

    /* Poll hardware devices for new packets */
    if (eth != NULL && eth->ops->poll != NULL) {
      eth->ops->poll(eth);
    }

    /* Check loopback device */
    if (lo != NULL) {
      p = netdev_receive(lo);
      if (p != NULL) {
        /* Loopback packets are IP, not Ethernet */
        ip_input(lo, p);
        processed = true;
      }
    }

    /* Check Ethernet device */
    if (eth != NULL && !processed) {
      p = netdev_receive(eth);
      if (p != NULL) {
        ethernet_input(eth, p);
        processed = true;
      }
    }

    /* If no packets processed, sleep briefly */
    if (!processed) {
      timer_msleep(10);
    }
  }
}
