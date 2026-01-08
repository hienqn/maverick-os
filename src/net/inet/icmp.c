/**
 * @file net/inet/icmp.c
 * @brief ICMP protocol implementation.
 */

#include "net/inet/icmp.h"
#include "net/inet/ip.h"
#include "net/util/checksum.h"
#include "net/util/byteorder.h"
#include <string.h>
#include <stdio.h>
#include <debug.h>

static bool icmp_initialized = false;

void icmp_init(void) { icmp_initialized = true; }

void icmp_input(struct netdev* dev, struct pbuf* p, uint32_t src, uint32_t dst) {
  struct icmp_hdr* icmp;
  uint16_t cksum;

  ASSERT(icmp_initialized);
  ASSERT(p != NULL);

  if (p->len < ICMP_HEADER_LEN) {
    pbuf_free(p);
    return;
  }

  icmp = p->payload;

  /* Verify checksum (over entire ICMP message) */
  cksum = checksum(p->payload, p->len);
  if (cksum != 0) {
    pbuf_free(p);
    return;
  }

  switch (icmp->type) {
    case ICMP_TYPE_ECHO_REQUEST: {
      /*
       * Echo Request - send reply
       *
       * We reuse the incoming packet for the reply:
       * 1. Change type to ECHO_REPLY
       * 2. Recalculate checksum
       * 3. Swap source/destination and send
       */
      char src_buf[16], dst_buf[16];

      ip_addr_to_str(src, src_buf);
      ip_addr_to_str(dst, dst_buf);
      printf("icmp: echo request from %s (seq=%d)\n", src_buf, ntohs(icmp->un.echo.seq));

      /* Change type to reply */
      icmp->type = ICMP_TYPE_ECHO_REPLY;
      icmp->code = 0;

      /* Recalculate checksum */
      icmp->checksum = 0;
      icmp->checksum = checksum(p->payload, p->len);

      /* Prepend IP header space and send reply */
      /* Note: We send to the source of the request */
      ip_output(dev, p, dst, src, IP_PROTO_ICMP, 0);
      /* pbuf consumed by ip_output */
      return;
    }

    case ICMP_TYPE_ECHO_REPLY: {
      /* Echo Reply - could notify waiting process */
      printf("icmp: echo reply from ");
      char buf[16];
      ip_addr_to_str(src, buf);
      printf("%s seq=%d\n", buf, ntohs(icmp->un.echo.seq));
      pbuf_free(p);
      return;
    }

    case ICMP_TYPE_DEST_UNREACHABLE: {
      printf("icmp: destination unreachable (code %d)\n", icmp->code);
      pbuf_free(p);
      return;
    }

    case ICMP_TYPE_TIME_EXCEEDED: {
      printf("icmp: time exceeded (code %d)\n", icmp->code);
      pbuf_free(p);
      return;
    }

    default:
      /* Unknown ICMP type */
      pbuf_free(p);
      return;
  }
}

int icmp_echo_request(struct netdev* dev, uint32_t dst, uint16_t id, uint16_t seq, const void* data,
                      size_t len) {
  struct pbuf* p;
  struct icmp_hdr* icmp;

  ASSERT(icmp_initialized);

  /* Allocate packet */
  p = pbuf_alloc(PBUF_IP, ICMP_HEADER_LEN + len, PBUF_RAM);
  if (p == NULL)
    return -1;

  icmp = p->payload;
  icmp->type = ICMP_TYPE_ECHO_REQUEST;
  icmp->code = 0;
  icmp->checksum = 0;
  icmp->un.echo.id = htons(id);
  icmp->un.echo.seq = htons(seq);

  /* Copy ping data */
  if (data != NULL && len > 0) {
    memcpy((uint8_t*)p->payload + ICMP_HEADER_LEN, data, len);
  }

  /* Calculate checksum */
  icmp->checksum = checksum(p->payload, p->len);

  /* Send via IP */
  return ip_output(dev, p, 0, dst, IP_PROTO_ICMP, 0);
}

void icmp_dest_unreachable(struct netdev* dev, uint32_t dst, uint8_t code, const void* orig,
                           size_t orig_len) {
  struct pbuf* p;
  struct icmp_hdr* icmp;

  ASSERT(icmp_initialized);

  /* Limit original data to IP header + 8 bytes */
  if (orig_len > 28)
    orig_len = 28;

  /* Allocate packet */
  p = pbuf_alloc(PBUF_IP, ICMP_HEADER_LEN + orig_len, PBUF_RAM);
  if (p == NULL)
    return;

  icmp = p->payload;
  icmp->type = ICMP_TYPE_DEST_UNREACHABLE;
  icmp->code = code;
  icmp->checksum = 0;
  icmp->un.unused = 0;

  /* Copy original packet header */
  if (orig != NULL && orig_len > 0) {
    memcpy((uint8_t*)p->payload + ICMP_HEADER_LEN, orig, orig_len);
  }

  /* Calculate checksum */
  icmp->checksum = checksum(p->payload, p->len);

  /* Send via IP */
  ip_output(dev, p, 0, dst, IP_PROTO_ICMP, 0);
}
