/**
 * @file net/buf/pbuf.c
 * @brief Packet buffer implementation.
 */

#include "net/buf/pbuf.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include <string.h>
#include <debug.h>

/* Statistics */
static uint32_t pbufs_allocated;
static uint32_t pbufs_freed;

/* Header space for each layer */
static const uint16_t layer_header_size[] = {
    [PBUF_TRANSPORT] = PBUF_HEADER_SPACE, /* Full headers */
    [PBUF_IP] = 34,                       /* Ethernet(14) + IP(20) */
    [PBUF_LINK] = 14,                     /* Ethernet only */
    [PBUF_RAW] = 0                        /* No headers */
};

void pbuf_init(void) {
  pbufs_allocated = 0;
  pbufs_freed = 0;
}

struct pbuf* pbuf_alloc(int layer, uint16_t size, enum pbuf_type type) {
  struct pbuf* p;
  uint16_t header_space;

  ASSERT(layer >= PBUF_TRANSPORT && layer <= PBUF_RAW);
  header_space = layer_header_size[layer];

  if (type == PBUF_RAM) {
    /* Allocate pbuf + header space + payload in one block */
    size_t total = sizeof(struct pbuf) + header_space + size;
    p = malloc(total);
    if (p == NULL)
      return NULL;

    /* Set payload to after reserved header space */
    p->payload = (uint8_t*)(p + 1) + header_space;
    p->len = size;
  } else {
    /* PBUF_REF: just allocate the structure */
    p = malloc(sizeof(struct pbuf));
    if (p == NULL)
      return NULL;

    p->payload = NULL;
    p->len = 0;
  }

  p->next = NULL;
  p->tot_len = size;
  p->type = type;
  p->ref = 1;
  p->flags = 0;
  p->_pad = 0;

  pbufs_allocated++;
  return p;
}

struct pbuf* pbuf_free(struct pbuf* p) {
  struct pbuf* next;

  if (p == NULL)
    return NULL;

  ASSERT(p->ref > 0);

  /* Decrement reference count */
  p->ref--;
  if (p->ref > 0) {
    /* Still referenced, don't free */
    return p->next;
  }

  next = p->next;

  /* Free based on type */
  if (p->type == PBUF_RAM) {
    free(p);
  } else {
    /* PBUF_REF: don't free external data */
    free(p);
  }

  pbufs_freed++;
  return next;
}

void pbuf_ref(struct pbuf* p) {
  ASSERT(p != NULL);
  ASSERT(p->ref < 255);
  p->ref++;
}

bool pbuf_header(struct pbuf* p, int16_t delta) {
  ASSERT(p != NULL);

  if (delta < 0) {
    /* Adding header: move payload pointer back */
    uint8_t* new_payload = (uint8_t*)p->payload + delta;
    uint8_t* pbuf_start;

    if (p->type == PBUF_RAM) {
      pbuf_start = (uint8_t*)(p + 1);
    } else {
      /* Can't add headers to PBUF_REF */
      return false;
    }

    if (new_payload < pbuf_start)
      return false;

    p->payload = new_payload;
    p->len -= delta; /* delta is negative, so this adds */
    p->tot_len -= delta;
  } else if (delta > 0) {
    /* Removing header: move payload pointer forward */
    if ((uint16_t)delta > p->len)
      return false;

    p->payload = (uint8_t*)p->payload + delta;
    p->len -= delta;
    p->tot_len -= delta;
  }

  return true;
}

size_t pbuf_copy_in(struct pbuf* p, const void* data, size_t len, size_t offset) {
  const uint8_t* src = data;
  size_t copied = 0;

  /* Skip to offset */
  while (p != NULL && offset >= p->len) {
    offset -= p->len;
    p = p->next;
  }

  /* Copy data */
  while (p != NULL && copied < len) {
    size_t chunk = p->len - offset;
    if (chunk > len - copied)
      chunk = len - copied;

    memcpy((uint8_t*)p->payload + offset, src + copied, chunk);
    copied += chunk;
    offset = 0;
    p = p->next;
  }

  return copied;
}

size_t pbuf_copy_out(const struct pbuf* p, void* data, size_t len, size_t offset) {
  uint8_t* dst = data;
  size_t copied = 0;

  /* Skip to offset */
  while (p != NULL && offset >= p->len) {
    offset -= p->len;
    p = p->next;
  }

  /* Copy data */
  while (p != NULL && copied < len) {
    size_t chunk = p->len - offset;
    if (chunk > len - copied)
      chunk = len - copied;

    memcpy(dst + copied, (const uint8_t*)p->payload + offset, chunk);
    copied += chunk;
    offset = 0;
    p = p->next;
  }

  return copied;
}

void* pbuf_get_contiguous(const struct pbuf* p, size_t offset, size_t len) {
  /* Skip to offset */
  while (p != NULL && offset >= p->len) {
    offset -= p->len;
    p = p->next;
  }

  if (p == NULL)
    return NULL;

  /* Check if data is contiguous in this pbuf */
  if (offset + len <= p->len)
    return (uint8_t*)p->payload + offset;

  return NULL;
}
