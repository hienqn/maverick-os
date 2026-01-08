/**
 * @file net/buf/pbuf.h
 * @brief Packet buffer management.
 *
 * Packet buffers (pbufs) are the fundamental unit for network data.
 * They can be chained together for large packets and support
 * efficient header prepend/removal operations.
 *
 * DESIGN:
 * - PBUF_RAM: Data allocated with the pbuf header (most common)
 * - PBUF_REF: Points to external data (zero-copy receive)
 *
 * HEADER SPACE:
 * Each pbuf reserves space at the beginning for headers to be prepended
 * without copying. The payload pointer can be adjusted to add/remove headers.
 */

#ifndef NET_BUF_PBUF_H
#define NET_BUF_PBUF_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Maximum header space to reserve (Ethernet + IP + TCP = ~60 bytes) */
#define PBUF_HEADER_SPACE 64

/* Maximum single pbuf payload size */
#define PBUF_MAX_SIZE 1536

/* Pbuf types */
enum pbuf_type {
  PBUF_RAM, /* Data follows pbuf structure */
  PBUF_REF  /* Data is external reference */
};

/**
 * @brief Packet buffer structure.
 */
struct pbuf {
  struct pbuf* next; /* Next pbuf in chain (NULL if last) */
  void* payload;     /* Current data pointer */
  uint16_t tot_len;  /* Total length of chain from here */
  uint16_t len;      /* Length of this buffer's data */
  uint8_t type;      /* PBUF_RAM or PBUF_REF */
  uint8_t ref;       /* Reference count */
  uint8_t flags;     /* Reserved for future use */
  uint8_t _pad;      /* Padding for alignment */

  /* For PBUF_RAM: actual data follows this structure */
};

/**
 * @brief Initialize the pbuf subsystem.
 */
void pbuf_init(void);

/**
 * @brief Allocate a packet buffer.
 * @param layer Protocol layer (determines header space to reserve).
 * @param size Payload size needed.
 * @param type PBUF_RAM or PBUF_REF.
 * @return Allocated pbuf, or NULL if out of memory.
 *
 * Layer values adjust the initial payload offset:
 *   PBUF_TRANSPORT: Full header space (Ethernet + IP + TCP)
 *   PBUF_IP: Ethernet + IP headers
 *   PBUF_LINK: Ethernet header only
 *   PBUF_RAW: No header space reserved
 */
#define PBUF_TRANSPORT 0
#define PBUF_IP 1
#define PBUF_LINK 2
#define PBUF_RAW 3

struct pbuf* pbuf_alloc(int layer, uint16_t size, enum pbuf_type type);

/**
 * @brief Free a packet buffer (or chain).
 * @param p Pbuf to free.
 * @return Next pbuf in chain (if any), for iterating.
 *
 * Decrements reference count. Only frees when count reaches zero.
 */
struct pbuf* pbuf_free(struct pbuf* p);

/**
 * @brief Increment reference count.
 * @param p Pbuf to reference.
 */
void pbuf_ref(struct pbuf* p);

/**
 * @brief Adjust payload pointer (add/remove headers).
 * @param p Pbuf to adjust.
 * @param delta Negative to add header space, positive to remove.
 * @return true on success, false if insufficient space.
 *
 * Example: To add an IP header:
 *   pbuf_header(p, -sizeof(struct ip_hdr));
 *   struct ip_hdr *ip = p->payload;
 *   // fill in header...
 */
bool pbuf_header(struct pbuf* p, int16_t delta);

/**
 * @brief Copy data into a pbuf chain.
 * @param p Destination pbuf.
 * @param data Source data.
 * @param len Length to copy.
 * @param offset Offset into pbuf to start.
 * @return Bytes copied.
 */
size_t pbuf_copy_in(struct pbuf* p, const void* data, size_t len, size_t offset);

/**
 * @brief Copy data out of a pbuf chain.
 * @param p Source pbuf.
 * @param data Destination buffer.
 * @param len Length to copy.
 * @param offset Offset into pbuf to start.
 * @return Bytes copied.
 */
size_t pbuf_copy_out(const struct pbuf* p, void* data, size_t len, size_t offset);

/**
 * @brief Get pointer to contiguous data at offset.
 * @param p Pbuf chain.
 * @param offset Offset into chain.
 * @param len Required contiguous length.
 * @return Pointer to data, or NULL if not contiguous.
 */
void* pbuf_get_contiguous(const struct pbuf* p, size_t offset, size_t len);

#endif /* NET_BUF_PBUF_H */
