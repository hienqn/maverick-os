/* arch/riscv64/virtio-blk.h - VirtIO block device definitions.
 *
 * The VirtIO block device provides disk access for QEMU virt machine.
 */

#ifndef ARCH_RISCV64_VIRTIO_BLK_H
#define ARCH_RISCV64_VIRTIO_BLK_H

#include "arch/riscv64/virtio.h"

/* VirtIO block device features */
#define VIRTIO_BLK_F_SIZE_MAX 1  /* Max segment size */
#define VIRTIO_BLK_F_SEG_MAX 2   /* Max segments per request */
#define VIRTIO_BLK_F_GEOMETRY 4  /* Geometry available */
#define VIRTIO_BLK_F_RO 5        /* Read-only device */
#define VIRTIO_BLK_F_BLK_SIZE 6  /* Block size available */
#define VIRTIO_BLK_F_FLUSH 9     /* Flush command supported */
#define VIRTIO_BLK_F_TOPOLOGY 10 /* Topology info available */
#define VIRTIO_BLK_F_MQ 12       /* Multi-queue support */

/* VirtIO block request types */
#define VIRTIO_BLK_T_IN 0            /* Read */
#define VIRTIO_BLK_T_OUT 1           /* Write */
#define VIRTIO_BLK_T_FLUSH 4         /* Flush */
#define VIRTIO_BLK_T_GET_ID 8        /* Get device ID */
#define VIRTIO_BLK_T_DISCARD 11      /* Discard */
#define VIRTIO_BLK_T_WRITE_ZEROES 13 /* Write zeroes */

/* VirtIO block status values */
#define VIRTIO_BLK_S_OK 0     /* Success */
#define VIRTIO_BLK_S_IOERR 1  /* I/O error */
#define VIRTIO_BLK_S_UNSUPP 2 /* Unsupported request */

/* Sector size */
#define VIRTIO_BLK_SECTOR_SIZE 512

/*
 * VirtIO block request header.
 * Precedes the data buffer(s) in the descriptor chain.
 */
struct virtio_blk_req {
  uint32_t type; /* VIRTIO_BLK_T_* */
  uint32_t reserved;
  uint64_t sector; /* Starting sector for IN/OUT */
} __attribute__((packed));

/*
 * VirtIO block device configuration (from config space).
 */
struct virtio_blk_config {
  uint64_t capacity; /* Number of 512-byte sectors */
  uint32_t size_max; /* Max segment size (if VIRTIO_BLK_F_SIZE_MAX) */
  uint32_t seg_max;  /* Max segments (if VIRTIO_BLK_F_SEG_MAX) */
  struct {
    uint16_t cylinders;
    uint8_t heads;
    uint8_t sectors;
  } geometry;        /* If VIRTIO_BLK_F_GEOMETRY */
  uint32_t blk_size; /* Block size (if VIRTIO_BLK_F_BLK_SIZE) */
} __attribute__((packed));

/* VirtIO block device structure */
struct virtio_blk {
  struct virtio_device dev;
  struct virtqueue vq;
  uint64_t capacity; /* Disk capacity in sectors */
  uint32_t blk_size; /* Block size */

  /* Request tracking */
  struct virtio_blk_req req __attribute__((aligned(8)));
  uint8_t status; /* Status byte from device */
};

/* Global VirtIO block device */
extern struct virtio_blk* virtio_blk_dev;

/* Function prototypes */
void virtio_blk_init(void);
bool virtio_blk_read(uint64_t sector, void* buf, uint32_t count);
bool virtio_blk_write(uint64_t sector, const void* buf, uint32_t count);
uint64_t virtio_blk_capacity(void);

#endif /* ARCH_RISCV64_VIRTIO_BLK_H */
