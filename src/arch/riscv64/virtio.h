/* arch/riscv64/virtio.h - VirtIO device definitions for RISC-V.
 *
 * VirtIO is the standard virtual device interface for QEMU/KVM.
 * This implements the VirtIO MMIO transport (not PCI).
 *
 * Reference: https://docs.oasis-open.org/virtio/virtio/v1.1/virtio-v1.1.html
 */

#ifndef ARCH_RISCV64_VIRTIO_H
#define ARCH_RISCV64_VIRTIO_H

#include <stdint.h>
#include <stdbool.h>

/* VirtIO MMIO base addresses on QEMU virt machine */
#define VIRTIO_MMIO_BASE 0x10001000
#define VIRTIO_MMIO_SIZE 0x1000
#define VIRTIO_MMIO_COUNT 8 /* Up to 8 VirtIO devices */

/* VirtIO MMIO register offsets */
#define VIRTIO_MMIO_MAGIC_VALUE 0x000
#define VIRTIO_MMIO_VERSION 0x004
#define VIRTIO_MMIO_DEVICE_ID 0x008
#define VIRTIO_MMIO_VENDOR_ID 0x00c
#define VIRTIO_MMIO_DEVICE_FEATURES 0x010
#define VIRTIO_MMIO_DEVICE_FEATURES_SEL 0x014
#define VIRTIO_MMIO_DRIVER_FEATURES 0x020
#define VIRTIO_MMIO_DRIVER_FEATURES_SEL 0x024
#define VIRTIO_MMIO_QUEUE_SEL 0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX 0x034
#define VIRTIO_MMIO_QUEUE_NUM 0x038
#define VIRTIO_MMIO_QUEUE_READY 0x044
#define VIRTIO_MMIO_QUEUE_NOTIFY 0x050
#define VIRTIO_MMIO_INTERRUPT_STATUS 0x060
#define VIRTIO_MMIO_INTERRUPT_ACK 0x064
#define VIRTIO_MMIO_STATUS 0x070
#define VIRTIO_MMIO_QUEUE_DESC_LOW 0x080
#define VIRTIO_MMIO_QUEUE_DESC_HIGH 0x084
#define VIRTIO_MMIO_QUEUE_AVAIL_LOW 0x090
#define VIRTIO_MMIO_QUEUE_AVAIL_HIGH 0x094
#define VIRTIO_MMIO_QUEUE_USED_LOW 0x0a0
#define VIRTIO_MMIO_QUEUE_USED_HIGH 0x0a4
#define VIRTIO_MMIO_CONFIG 0x100

/* VirtIO magic value */
#define VIRTIO_MAGIC 0x74726976 /* "virt" little-endian */

/* VirtIO device types */
#define VIRTIO_DEV_NET 1
#define VIRTIO_DEV_BLK 2
#define VIRTIO_DEV_CONSOLE 3
#define VIRTIO_DEV_RNG 4
#define VIRTIO_DEV_GPU 16

/* VirtIO status bits */
#define VIRTIO_STATUS_ACK 1
#define VIRTIO_STATUS_DRIVER 2
#define VIRTIO_STATUS_DRIVER_OK 4
#define VIRTIO_STATUS_FEATURES_OK 8
#define VIRTIO_STATUS_FAILED 128

/* VirtIO descriptor flags */
#define VIRTQ_DESC_F_NEXT 1     /* Buffer continues in next descriptor */
#define VIRTQ_DESC_F_WRITE 2    /* Buffer is device-writable */
#define VIRTQ_DESC_F_INDIRECT 4 /* Buffer contains descriptor list */

/* VirtIO used ring flags */
#define VIRTQ_USED_F_NO_NOTIFY 1

/* VirtIO available ring flags */
#define VIRTQ_AVAIL_F_NO_INTERRUPT 1

/* Virtqueue sizes */
#define VIRTIO_RING_SIZE 16

/*
 * Virtqueue descriptor.
 * Each descriptor describes a buffer for device I/O.
 */
struct virtq_desc {
  uint64_t addr;  /* Physical address of buffer */
  uint32_t len;   /* Buffer length */
  uint16_t flags; /* VIRTQ_DESC_F_* flags */
  uint16_t next;  /* Next descriptor if flags & VIRTQ_DESC_F_NEXT */
} __attribute__((packed));

/*
 * Virtqueue available ring.
 * Driver adds buffer indices here for device to consume.
 */
struct virtq_avail {
  uint16_t flags;
  uint16_t idx;
  uint16_t ring[VIRTIO_RING_SIZE];
  uint16_t used_event; /* Only if VIRTIO_F_EVENT_IDX */
} __attribute__((packed));

/*
 * Virtqueue used element.
 */
struct virtq_used_elem {
  uint32_t id;  /* Descriptor chain head index */
  uint32_t len; /* Total bytes written to buffer */
} __attribute__((packed));

/*
 * Virtqueue used ring.
 * Device adds completed buffer indices here.
 */
struct virtq_used {
  uint16_t flags;
  uint16_t idx;
  struct virtq_used_elem ring[VIRTIO_RING_SIZE];
  uint16_t avail_event; /* Only if VIRTIO_F_EVENT_IDX */
} __attribute__((packed));

/*
 * Complete virtqueue structure.
 * Must be page-aligned for MMIO transport.
 */
struct virtqueue {
  /* Descriptor table */
  struct virtq_desc desc[VIRTIO_RING_SIZE];

  /* Available ring */
  struct virtq_avail avail;

  /* Padding to align used ring */
  uint8_t _pad[4094 - sizeof(struct virtq_desc) * VIRTIO_RING_SIZE - sizeof(struct virtq_avail)];

  /* Used ring */
  struct virtq_used used;

  /* Driver state (not part of VirtIO spec) */
  uint16_t free_head;     /* Head of free descriptor list */
  uint16_t num_free;      /* Number of free descriptors */
  uint16_t last_used_idx; /* Last used index we saw */
  uint64_t base_addr;     /* MMIO base address */
  uint16_t queue_idx;     /* Queue index for this virtqueue */
} __attribute__((aligned(4096)));

/* VirtIO device structure */
struct virtio_device {
  uint64_t base;        /* MMIO base address */
  uint32_t device_id;   /* Device type */
  struct virtqueue* vq; /* Virtqueue for this device */
  bool initialized;     /* True if successfully initialized */
};

/* Function prototypes */
bool virtio_probe(uint64_t base, struct virtio_device* dev);
void virtio_init(void);
bool virtio_setup_queue(struct virtio_device* dev, int queue_idx, struct virtqueue* vq);
int virtio_alloc_desc(struct virtqueue* vq);
void virtio_free_desc(struct virtqueue* vq, int idx);
void virtio_submit(struct virtqueue* vq, int head);
bool virtio_poll(struct virtqueue* vq, uint32_t* len);

/* MMIO read/write helpers */
static inline uint32_t virtio_read32(uint64_t base, uint32_t offset) {
  return *(volatile uint32_t*)(base + offset);
}

static inline void virtio_write32(uint64_t base, uint32_t offset, uint32_t val) {
  *(volatile uint32_t*)(base + offset) = val;
}

#endif /* ARCH_RISCV64_VIRTIO_H */
