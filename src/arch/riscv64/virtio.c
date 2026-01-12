/* arch/riscv64/virtio.c - VirtIO device support for RISC-V.
 *
 * Implements VirtIO MMIO transport and virtqueue management.
 */

#include "arch/riscv64/virtio.h"
#include "arch/riscv64/memlayout.h"
#include <string.h>

#define UNUSED __attribute__((unused))

/* Forward declarations */
static void console_puts(const char* s);
static void console_puthex(uint64_t val);

/* External console functions from init.c */
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

/*
 * virtio_probe - Probe for a VirtIO device at given MMIO address.
 *
 * Returns true if a valid VirtIO device was found and dev is populated.
 */
bool virtio_probe(uint64_t base, struct virtio_device* dev) {
  uint32_t magic = virtio_read32(base, VIRTIO_MMIO_MAGIC_VALUE);
  if (magic != VIRTIO_MAGIC) {
    return false;
  }

  uint32_t version = virtio_read32(base, VIRTIO_MMIO_VERSION);
  if (version != 2) {
    /* We only support VirtIO 1.0+ (version 2 in MMIO) */
    console_puts("  Warning: VirtIO version ");
    console_puthex(version);
    console_puts(" not supported\n");
    return false;
  }

  uint32_t device_id = virtio_read32(base, VIRTIO_MMIO_DEVICE_ID);
  if (device_id == 0) {
    /* No device at this address */
    return false;
  }

  dev->base = base;
  dev->device_id = device_id;
  dev->vq = NULL;
  dev->initialized = false;

  return true;
}

/*
 * virtio_setup_queue - Set up a virtqueue for a device.
 *
 * The virtqueue must be page-aligned and persist for device lifetime.
 */
bool virtio_setup_queue(struct virtio_device* dev, int queue_idx, struct virtqueue* vq) {
  uint64_t base = dev->base;

  /* Select the queue */
  virtio_write32(base, VIRTIO_MMIO_QUEUE_SEL, queue_idx);

  /* Check if queue exists */
  uint32_t max_size = virtio_read32(base, VIRTIO_MMIO_QUEUE_NUM_MAX);
  if (max_size == 0) {
    console_puts("  Queue ");
    console_puthex(queue_idx);
    console_puts(" does not exist\n");
    return false;
  }

  /* Check if already initialized */
  uint32_t ready = virtio_read32(base, VIRTIO_MMIO_QUEUE_READY);
  if (ready) {
    console_puts("  Queue already initialized\n");
    return false;
  }

  /* Initialize virtqueue structure */
  memset(vq, 0, sizeof(*vq));
  vq->base_addr = base;
  vq->queue_idx = queue_idx;
  vq->num_free = VIRTIO_RING_SIZE;
  vq->free_head = 0;
  vq->last_used_idx = 0;

  /* Set up free descriptor chain */
  for (int i = 0; i < VIRTIO_RING_SIZE - 1; i++) {
    vq->desc[i].next = i + 1;
  }
  vq->desc[VIRTIO_RING_SIZE - 1].next = 0xFFFF; /* End marker */

  /* Configure queue size */
  uint32_t size = VIRTIO_RING_SIZE;
  if (size > max_size)
    size = max_size;
  virtio_write32(base, VIRTIO_MMIO_QUEUE_NUM, size);

  /* Set queue addresses (physical addresses) */
  uint64_t desc_addr = (uint64_t)&vq->desc[0];
  uint64_t avail_addr = (uint64_t)&vq->avail;
  uint64_t used_addr = (uint64_t)&vq->used;

  virtio_write32(base, VIRTIO_MMIO_QUEUE_DESC_LOW, desc_addr & 0xFFFFFFFF);
  virtio_write32(base, VIRTIO_MMIO_QUEUE_DESC_HIGH, desc_addr >> 32);
  virtio_write32(base, VIRTIO_MMIO_QUEUE_AVAIL_LOW, avail_addr & 0xFFFFFFFF);
  virtio_write32(base, VIRTIO_MMIO_QUEUE_AVAIL_HIGH, avail_addr >> 32);
  virtio_write32(base, VIRTIO_MMIO_QUEUE_USED_LOW, used_addr & 0xFFFFFFFF);
  virtio_write32(base, VIRTIO_MMIO_QUEUE_USED_HIGH, used_addr >> 32);

  /* Enable the queue */
  virtio_write32(base, VIRTIO_MMIO_QUEUE_READY, 1);

  dev->vq = vq;
  return true;
}

/*
 * virtio_alloc_desc - Allocate a descriptor from the free list.
 *
 * Returns descriptor index, or -1 if none available.
 */
int virtio_alloc_desc(struct virtqueue* vq) {
  if (vq->num_free == 0)
    return -1;

  int idx = vq->free_head;
  vq->free_head = vq->desc[idx].next;
  vq->num_free--;
  return idx;
}

/*
 * virtio_free_desc - Return a descriptor to the free list.
 */
void virtio_free_desc(struct virtqueue* vq, int idx) {
  vq->desc[idx].next = vq->free_head;
  vq->desc[idx].flags = 0;
  vq->free_head = idx;
  vq->num_free++;
}

/*
 * virtio_free_chain - Free a chain of descriptors.
 */
void virtio_free_chain(struct virtqueue* vq, int head) {
  int idx = head;
  while (1) {
    int next = vq->desc[idx].next;
    bool has_next = (vq->desc[idx].flags & VIRTQ_DESC_F_NEXT) != 0;
    virtio_free_desc(vq, idx);
    if (!has_next)
      break;
    idx = next;
  }
}

/*
 * virtio_submit - Submit a descriptor chain to the device.
 *
 * head is the index of the first descriptor in the chain.
 */
void virtio_submit(struct virtqueue* vq, int head) {
  /* Add to available ring */
  uint16_t avail_idx = vq->avail.idx;
  vq->avail.ring[avail_idx % VIRTIO_RING_SIZE] = head;

  /* Memory barrier before updating idx */
  __sync_synchronize();

  vq->avail.idx = avail_idx + 1;

  /* Memory barrier before notifying device */
  __sync_synchronize();

  /* Notify the device */
  virtio_write32(vq->base_addr, VIRTIO_MMIO_QUEUE_NOTIFY, vq->queue_idx);
}

/*
 * virtio_poll - Poll for completed requests.
 *
 * Returns true if a request completed. If len is non-NULL, stores
 * the bytes written by the device.
 */
bool virtio_poll(struct virtqueue* vq, uint32_t* len) {
  /* Memory barrier before reading */
  __sync_synchronize();

  if (vq->last_used_idx == vq->used.idx)
    return false;

  struct virtq_used_elem* elem = &vq->used.ring[vq->last_used_idx % VIRTIO_RING_SIZE];

  if (len)
    *len = elem->len;

  /* Free the descriptor chain */
  virtio_free_chain(vq, elem->id);

  vq->last_used_idx++;
  return true;
}

/*
 * virtio_init - Initialize VirtIO subsystem.
 *
 * Probes for VirtIO devices at known MMIO addresses.
 */
void virtio_init(void) {
  console_puts("\nProbing VirtIO devices...\n");

  for (int i = 0; i < VIRTIO_MMIO_COUNT; i++) {
    uint64_t base = VIRTIO_MMIO_BASE + i * VIRTIO_MMIO_SIZE;
    struct virtio_device dev;

    if (virtio_probe(base, &dev)) {
      console_puts("  Found VirtIO device at ");
      console_puthex(base);
      console_puts(": type ");
      console_puthex(dev.device_id);

      switch (dev.device_id) {
        case VIRTIO_DEV_NET:
          console_puts(" (network)\n");
          break;
        case VIRTIO_DEV_BLK:
          console_puts(" (block)\n");
          break;
        case VIRTIO_DEV_CONSOLE:
          console_puts(" (console)\n");
          break;
        case VIRTIO_DEV_RNG:
          console_puts(" (RNG)\n");
          break;
        default:
          console_puts(" (unknown)\n");
          break;
      }
    }
  }
}
