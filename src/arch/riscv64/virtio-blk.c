/* arch/riscv64/virtio-blk.c - VirtIO block device driver.
 *
 * Provides disk access via VirtIO block device.
 */

#include "arch/riscv64/virtio-blk.h"
#include "arch/riscv64/virtio.h"
#include "arch/riscv64/memlayout.h"
#include <string.h>

#define UNUSED __attribute__((unused))

/* Global block device */
struct virtio_blk* virtio_blk_dev = NULL;

/* Static storage for the block device */
static struct virtio_blk blk_device;

/* Forward declarations for console output */
extern void sbi_console_putchar(int c);

static void console_putchar(char c) { sbi_console_putchar(c); }

static void console_puts(const char* s) {
  while (*s) {
    if (*s == '\n')
      console_putchar('\r');
    console_putchar(*s++);
  }
}

static void console_puthex(uint64_t val) {
  static const char hex[] = "0123456789abcdef";
  char buf[19];
  buf[0] = '0';
  buf[1] = 'x';
  for (int i = 0; i < 16; i++) {
    buf[17 - i] = hex[val & 0xf];
    val >>= 4;
  }
  buf[18] = '\0';
  console_puts(buf);
}

static void console_putdec(uint64_t val) {
  char buf[21];
  char* p = buf + sizeof(buf) - 1;
  *p = '\0';
  if (val == 0) {
    *--p = '0';
  } else {
    while (val > 0) {
      *--p = '0' + (val % 10);
      val /= 10;
    }
  }
  console_puts(p);
}

/*
 * virtio_blk_init - Initialize the VirtIO block device.
 *
 * Scans for a VirtIO block device and initializes it.
 */
void virtio_blk_init(void) {
  console_puts("\nInitializing VirtIO block device...\n");

  /* Scan for block device */
  for (int i = 0; i < VIRTIO_MMIO_COUNT; i++) {
    uint64_t base = VIRTIO_MMIO_BASE + i * VIRTIO_MMIO_SIZE;

    if (!virtio_probe(base, &blk_device.dev))
      continue;

    if (blk_device.dev.device_id != VIRTIO_DEV_BLK)
      continue;

    /* Found a block device */
    console_puts("  Found block device at ");
    console_puthex(base);
    console_puts("\n");

    /* Reset the device */
    virtio_write32(base, VIRTIO_MMIO_STATUS, 0);

    /* Set ACKNOWLEDGE status bit */
    virtio_write32(base, VIRTIO_MMIO_STATUS, VIRTIO_STATUS_ACK);

    /* Set DRIVER status bit */
    uint32_t status = virtio_read32(base, VIRTIO_MMIO_STATUS);
    virtio_write32(base, VIRTIO_MMIO_STATUS, status | VIRTIO_STATUS_DRIVER);

    /* Read device features */
    virtio_write32(base, VIRTIO_MMIO_DEVICE_FEATURES_SEL, 0);
    uint32_t features = virtio_read32(base, VIRTIO_MMIO_DEVICE_FEATURES);
    console_puts("  Features: ");
    console_puthex(features);
    console_puts("\n");

    /* Accept no features for simplicity */
    virtio_write32(base, VIRTIO_MMIO_DRIVER_FEATURES_SEL, 0);
    virtio_write32(base, VIRTIO_MMIO_DRIVER_FEATURES, 0);

    /* For modern (v2), set FEATURES_OK */
    if (blk_device.dev.version == 2) {
      status = virtio_read32(base, VIRTIO_MMIO_STATUS);
      virtio_write32(base, VIRTIO_MMIO_STATUS, status | VIRTIO_STATUS_FEATURES_OK);

      /* Verify FEATURES_OK was accepted */
      status = virtio_read32(base, VIRTIO_MMIO_STATUS);
      if (!(status & VIRTIO_STATUS_FEATURES_OK)) {
        console_puts("  ERROR: Features not accepted\n");
        continue;
      }
    }
    /* Legacy (v1) doesn't use FEATURES_OK - proceed directly */

    /* Set up the virtqueue */
    if (!virtio_setup_queue(&blk_device.dev, 0, &blk_device.vq)) {
      console_puts("  ERROR: Failed to set up queue\n");
      continue;
    }

    /* Set DRIVER_OK to finish initialization */
    status = virtio_read32(base, VIRTIO_MMIO_STATUS);
    virtio_write32(base, VIRTIO_MMIO_STATUS, status | VIRTIO_STATUS_DRIVER_OK);

    /* Read device capacity from config space */
    blk_device.capacity = virtio_read32(base, VIRTIO_MMIO_CONFIG);
    blk_device.capacity |= ((uint64_t)virtio_read32(base, VIRTIO_MMIO_CONFIG + 4)) << 32;
    blk_device.blk_size = VIRTIO_BLK_SECTOR_SIZE;

    console_puts("  Capacity: ");
    console_putdec(blk_device.capacity / 2); /* In KB */
    console_puts(" KB (");
    console_putdec(blk_device.capacity);
    console_puts(" sectors)\n");

    blk_device.dev.initialized = true;
    virtio_blk_dev = &blk_device;

    console_puts("  VirtIO block device initialized\n");
    return;
  }

  console_puts("  No VirtIO block device found\n");
}

/*
 * virtio_blk_rw - Perform a read or write operation.
 *
 * type is VIRTIO_BLK_T_IN (read) or VIRTIO_BLK_T_OUT (write).
 */
static bool virtio_blk_rw(uint32_t type, uint64_t sector, void* buf, uint32_t count) {
  if (!virtio_blk_dev || !virtio_blk_dev->dev.initialized)
    return false;

  struct virtqueue* vq = &virtio_blk_dev->vq;
  struct virtio_blk_req* req = &virtio_blk_dev->req;

  /* Set up request header */
  req->type = type;
  req->reserved = 0;
  req->sector = sector;

  /* Clear status */
  virtio_blk_dev->status = 0xFF;

  /* Allocate descriptors: header, data, status */
  int d0 = virtio_alloc_desc(vq);
  int d1 = virtio_alloc_desc(vq);
  int d2 = virtio_alloc_desc(vq);

  if (d0 < 0 || d1 < 0 || d2 < 0) {
    if (d0 >= 0)
      virtio_free_desc(vq, d0);
    if (d1 >= 0)
      virtio_free_desc(vq, d1);
    if (d2 >= 0)
      virtio_free_desc(vq, d2);
    return false;
  }

  /* Set up descriptor 0: request header (device reads) */
  vq->desc[d0].addr = (uint64_t)req;
  vq->desc[d0].len = sizeof(struct virtio_blk_req);
  vq->desc[d0].flags = VIRTQ_DESC_F_NEXT;
  vq->desc[d0].next = d1;

  /* Set up descriptor 1: data buffer */
  vq->desc[d1].addr = (uint64_t)buf;
  vq->desc[d1].len = count * VIRTIO_BLK_SECTOR_SIZE;
  vq->desc[d1].flags = VIRTQ_DESC_F_NEXT;
  if (type == VIRTIO_BLK_T_IN) {
    vq->desc[d1].flags |= VIRTQ_DESC_F_WRITE; /* Device writes to buffer */
  }
  vq->desc[d1].next = d2;

  /* Set up descriptor 2: status byte (device writes) */
  vq->desc[d2].addr = (uint64_t)&virtio_blk_dev->status;
  vq->desc[d2].len = 1;
  vq->desc[d2].flags = VIRTQ_DESC_F_WRITE;
  vq->desc[d2].next = 0;

  /* Submit the request */
  virtio_submit(vq, d0);

  /* Wait for completion (polling) */
  uint32_t len;
  while (!virtio_poll(vq, &len)) {
    /* Busy wait */
    asm volatile("" ::: "memory");
  }

  return virtio_blk_dev->status == VIRTIO_BLK_S_OK;
}

/*
 * virtio_blk_read - Read sectors from the block device.
 */
bool virtio_blk_read(uint64_t sector, void* buf, uint32_t count) {
  return virtio_blk_rw(VIRTIO_BLK_T_IN, sector, buf, count);
}

/*
 * virtio_blk_write - Write sectors to the block device.
 */
bool virtio_blk_write(uint64_t sector, const void* buf, uint32_t count) {
  return virtio_blk_rw(VIRTIO_BLK_T_OUT, sector, (void*)buf, count);
}

/*
 * virtio_blk_capacity - Return disk capacity in sectors.
 */
uint64_t virtio_blk_capacity(void) {
  if (!virtio_blk_dev)
    return 0;
  return virtio_blk_dev->capacity;
}
