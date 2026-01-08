/**
 * @file net/driver/e1000.c
 * @brief Intel E1000 network driver implementation.
 */

#include "net/driver/e1000.h"
#include "net/driver/netdev.h"
#include "net/buf/pbuf.h"
#include "devices/pci.h"
#include "threads/vaddr.h"
#include "threads/ioremap.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/interrupt.h"
#include "threads/synch.h"
#include <string.h>
#include <stdio.h>
#include <debug.h>

/*
 * ============================================================
 * Driver Private Data
 * ============================================================
 */

struct e1000_priv {
  /* MMIO base address (virtual) */
  volatile uint32_t* mmio;

  /* TX descriptor ring (16-byte aligned) */
  struct e1000_tx_desc* tx_ring;
  uint32_t tx_ring_phys; /* Physical address */
  uint16_t tx_head;      /* Next to reclaim */
  uint16_t tx_tail;      /* Next to use */
  struct pbuf* tx_bufs[E1000_TX_RING_SIZE];
  struct lock tx_lock;
  struct semaphore tx_sem; /* Available TX descriptors */

  /* RX descriptor ring (16-byte aligned) */
  struct e1000_rx_desc* rx_ring;
  uint32_t rx_ring_phys;                /* Physical address */
  uint16_t rx_tail;                     /* Next to check */
  uint8_t* rx_bufs[E1000_RX_RING_SIZE]; /* Pre-allocated buffers */
  uint32_t rx_bufs_phys[E1000_RX_RING_SIZE];

  /* IRQ */
  uint8_t irq_line;

  /* Reference to network device */
  struct netdev* netdev;
};

static struct e1000_priv* e1000_device = NULL;

/*
 * ============================================================
 * MMIO Register Access
 * ============================================================
 */

static inline uint32_t e1000_read(struct e1000_priv* priv, uint32_t reg) {
  return priv->mmio[reg / 4];
}

static inline void e1000_write(struct e1000_priv* priv, uint32_t reg, uint32_t val) {
  priv->mmio[reg / 4] = val;
}

/*
 * ============================================================
 * EEPROM Access
 * ============================================================
 */

static uint16_t e1000_eeprom_read(struct e1000_priv* priv, uint8_t addr) {
  uint32_t val;

  /* Start EEPROM read */
  e1000_write(priv, E1000_EERD, (addr << E1000_EERD_ADDR_SHIFT) | E1000_EERD_START);

  /* Wait for completion */
  do {
    val = e1000_read(priv, E1000_EERD);
  } while (!(val & E1000_EERD_DONE));

  return (val >> E1000_EERD_DATA_SHIFT) & 0xFFFF;
}

/*
 * ============================================================
 * MAC Address
 * ============================================================
 */

static void e1000_read_mac(struct e1000_priv* priv, uint8_t* mac) {
  uint16_t word;

  /* Read MAC from EEPROM */
  word = e1000_eeprom_read(priv, 0);
  mac[0] = word & 0xFF;
  mac[1] = (word >> 8) & 0xFF;

  word = e1000_eeprom_read(priv, 1);
  mac[2] = word & 0xFF;
  mac[3] = (word >> 8) & 0xFF;

  word = e1000_eeprom_read(priv, 2);
  mac[4] = word & 0xFF;
  mac[5] = (word >> 8) & 0xFF;
}

static void e1000_set_mac(struct e1000_priv* priv, const uint8_t* mac) {
  uint32_t ral, rah;

  /* Set Receive Address Low */
  ral = mac[0] | (mac[1] << 8) | (mac[2] << 16) | (mac[3] << 24);
  e1000_write(priv, E1000_RAL, ral);

  /* Set Receive Address High with Address Valid bit */
  rah = mac[4] | (mac[5] << 8) | E1000_RAH_AV;
  e1000_write(priv, E1000_RAH, rah);
}

/*
 * ============================================================
 * TX Ring Management
 * ============================================================
 */

static void e1000_tx_init(struct e1000_priv* priv) {
  void* ring_page;
  int i;

  /* Allocate TX descriptor ring (page-aligned for DMA) */
  ring_page = palloc_get_page(PAL_ZERO);
  ASSERT(ring_page != NULL);

  priv->tx_ring = ring_page;
  priv->tx_ring_phys = vtop(ring_page);

  /* Initialize TX descriptors */
  for (i = 0; i < E1000_TX_RING_SIZE; i++) {
    priv->tx_ring[i].buffer_addr = 0;
    priv->tx_ring[i].cmd = 0;
    priv->tx_ring[i].status = E1000_TXD_STAT_DD; /* Mark as done */
    priv->tx_bufs[i] = NULL;
  }

  priv->tx_head = 0;
  priv->tx_tail = 0;
  lock_init(&priv->tx_lock);
  sema_init(&priv->tx_sem, E1000_TX_RING_SIZE);

  /* Configure TX registers */
  e1000_write(priv, E1000_TDBAL, priv->tx_ring_phys);
  e1000_write(priv, E1000_TDBAH, 0); /* 32-bit addresses only */
  e1000_write(priv, E1000_TDLEN, E1000_TX_RING_SIZE * sizeof(struct e1000_tx_desc));
  e1000_write(priv, E1000_TDH, 0);
  e1000_write(priv, E1000_TDT, 0);

  /* Configure TX inter-packet gap */
  e1000_write(priv, E1000_TIPG,
              E1000_TIPG_IPGT_DEFAULT | (E1000_TIPG_IPGR1_DEFAULT << 10) |
                  (E1000_TIPG_IPGR2_DEFAULT << 20));

  /* Enable transmitter */
  e1000_write(priv, E1000_TCTL,
              E1000_TCTL_EN | E1000_TCTL_PSP |
                  (0x10 << E1000_TCTL_CT_SHIFT) |   /* Collision threshold */
                  (0x40 << E1000_TCTL_COLD_SHIFT)); /* Collision distance */
}

/*
 * ============================================================
 * RX Ring Management
 * ============================================================
 */

static void e1000_rx_init(struct e1000_priv* priv) {
  void* ring_page;
  int i;

  /* Allocate RX descriptor ring */
  ring_page = palloc_get_page(PAL_ZERO);
  ASSERT(ring_page != NULL);

  priv->rx_ring = ring_page;
  priv->rx_ring_phys = vtop(ring_page);

  /* Allocate RX buffers and initialize descriptors */
  for (i = 0; i < E1000_RX_RING_SIZE; i++) {
    /* Allocate buffer (page-aligned for DMA) */
    priv->rx_bufs[i] = palloc_get_page(0);
    ASSERT(priv->rx_bufs[i] != NULL);
    priv->rx_bufs_phys[i] = vtop(priv->rx_bufs[i]);

    /* Set up descriptor */
    priv->rx_ring[i].buffer_addr = priv->rx_bufs_phys[i];
    priv->rx_ring[i].status = 0; /* Not yet received */
  }

  priv->rx_tail = 0;

  /* Configure RX registers */
  e1000_write(priv, E1000_RDBAL, priv->rx_ring_phys);
  e1000_write(priv, E1000_RDBAH, 0);
  e1000_write(priv, E1000_RDLEN, E1000_RX_RING_SIZE * sizeof(struct e1000_rx_desc));
  e1000_write(priv, E1000_RDH, 0);
  e1000_write(priv, E1000_RDT, E1000_RX_RING_SIZE - 1); /* All but one available */

  /* Clear multicast table */
  for (i = 0; i < 128; i++) {
    e1000_write(priv, E1000_MTA + i * 4, 0);
  }

  /* Enable receiver */
  e1000_write(priv, E1000_RCTL,
              E1000_RCTL_EN | E1000_RCTL_BAM | /* Accept broadcast */
                  E1000_RCTL_BSIZE_2048 |      /* 2KB buffers */
                  E1000_RCTL_SECRC);           /* Strip CRC */
}

/*
 * ============================================================
 * Interrupt Handler
 * ============================================================
 */

static void e1000_interrupt(struct intr_frame* frame UNUSED) {
  struct e1000_priv* priv = e1000_device;

  if (priv == NULL)
    return;

  /* Read and clear interrupt cause.
     The actual packet processing is done by the polling thread
     (e1000_receive and e1000_reclaim_tx) to avoid doing work that
     requires locks from interrupt context. */
  (void)e1000_read(priv, E1000_ICR);
}

/* Reclaim completed TX descriptors.
   Called from e1000_transmit, not from interrupt context. */
static void e1000_reclaim_tx(struct e1000_priv* priv) {
  while (priv->tx_head != priv->tx_tail) {
    struct e1000_tx_desc* desc = &priv->tx_ring[priv->tx_head];

    if (!(desc->status & E1000_TXD_STAT_DD))
      break; /* Not done yet */

    /* Free transmitted pbuf */
    if (priv->tx_bufs[priv->tx_head] != NULL) {
      pbuf_free(priv->tx_bufs[priv->tx_head]);
      priv->tx_bufs[priv->tx_head] = NULL;
    }

    desc->status = 0;
    priv->tx_head = (priv->tx_head + 1) % E1000_TX_RING_SIZE;
    sema_up(&priv->tx_sem);
  }
}

/*
 * ============================================================
 * Network Device Operations
 * ============================================================
 */

static int e1000_netdev_init(struct netdev* dev) {
  struct e1000_priv* priv = dev->priv;

  /* Read MAC address from EEPROM */
  e1000_read_mac(priv, dev->mac_addr);

  /* Program MAC address into hardware */
  e1000_set_mac(priv, dev->mac_addr);

  printf("e1000: MAC address %02x:%02x:%02x:%02x:%02x:%02x\n", dev->mac_addr[0], dev->mac_addr[1],
         dev->mac_addr[2], dev->mac_addr[3], dev->mac_addr[4], dev->mac_addr[5]);

  /* Enable broadcast */
  dev->flags |= NETDEV_FLAG_BROADCAST;

  return 0;
}

static int e1000_transmit(struct netdev* dev, struct pbuf* p) {
  struct e1000_priv* priv = dev->priv;
  struct e1000_tx_desc* desc;
  uint16_t tail;

  /* Wait for available descriptor */
  sema_down(&priv->tx_sem);

  lock_acquire(&priv->tx_lock);

  tail = (e1000_read(priv, E1000_TDT)) % E1000_TX_RING_SIZE;
  desc = &priv->tx_ring[tail];

  /* Copy packet to contiguous buffer if needed */
  if (p->next != NULL) {
    /* Multi-buffer packet: need to linearize */
    struct pbuf* linear = pbuf_alloc(PBUF_RAW, p->tot_len, PBUF_RAM);
    if (linear == NULL) {
      lock_release(&priv->tx_lock);
      sema_up(&priv->tx_sem);
      pbuf_free(p);
      return -1;
    }
    pbuf_copy_out(p, linear->payload, p->tot_len, 0);
    pbuf_free(p);
    p = linear;
  }

  /* Set up descriptor */
  desc->buffer_addr = vtop(p->payload);
  desc->length = p->len;
  desc->cso = 0;
  desc->css = 0;
  desc->status = 0;
  desc->cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS | E1000_TXD_CMD_RS;
  desc->special = 0;

  /* Save pbuf for later freeing */
  priv->tx_bufs[tail] = p;

  /* Advance tail pointer */
  tail = (tail + 1) % E1000_TX_RING_SIZE;
  e1000_write(priv, E1000_TDT, tail);

  lock_release(&priv->tx_lock);

  return 0;
}

static void e1000_set_mac_addr(struct netdev* dev, const uint8_t* mac) {
  struct e1000_priv* priv = dev->priv;
  memcpy(dev->mac_addr, mac, 6);
  e1000_set_mac(priv, mac);
}

/* Poll for received packets.
   Called from the network input thread (not interrupt context). */
static void e1000_poll(struct netdev* dev) {
  struct e1000_priv* priv = dev->priv;

  /* Process received packets */
  while (1) {
    struct e1000_rx_desc* desc = &priv->rx_ring[priv->rx_tail];

    if (!(desc->status & E1000_RXD_STAT_DD))
      break; /* No more received */

    if (desc->status & E1000_RXD_STAT_EOP) {
      /* Complete packet received */
      uint16_t len = desc->length;
      struct pbuf* p;

      /* Allocate pbuf and copy data */
      p = pbuf_alloc(PBUF_RAW, len, PBUF_RAM);
      if (p != NULL) {
        memcpy(p->payload, priv->rx_bufs[priv->rx_tail], len);
        /* Queue for processing */
        netdev_input(dev, p);
      }
    }

    /* Reset descriptor for reuse */
    desc->status = 0;

    /* Advance tail and notify hardware */
    uint16_t old_tail = priv->rx_tail;
    priv->rx_tail = (priv->rx_tail + 1) % E1000_RX_RING_SIZE;
    e1000_write(priv, E1000_RDT, old_tail);
  }

  /* Also reclaim completed TX descriptors */
  e1000_reclaim_tx(priv);
}

static const struct netdev_ops e1000_ops = {.init = e1000_netdev_init,
                                            .transmit = e1000_transmit,
                                            .set_mac = e1000_set_mac_addr,
                                            .poll = e1000_poll};

/*
 * ============================================================
 * Driver Initialization
 * ============================================================
 */

void e1000_driver_init(void) {
  struct pci_device pci_dev;
  struct e1000_priv* priv;
  struct netdev* dev;
  uint32_t bar0;
  uintptr_t mmio_paddr;

  /* Find E1000 PCI device */
  if (!pci_find_device(E1000_VENDOR_ID, E1000_DEVICE_ID, &pci_dev)) {
    /* No E1000 device found (network is optional) */
    return;
  }

  printf("e1000: found device at bus %d, device %d, function %d\n", pci_dev.bus, pci_dev.device,
         pci_dev.function);

  /* Get MMIO base address */
  bar0 = pci_dev.base_address[0];
  if ((bar0 & 0x1) != 0) {
    printf("e1000: BAR0 is I/O space, not supported\n");
    return;
  }
  mmio_paddr = bar0 & ~0xF;

  /* Enable bus mastering for DMA */
  uint32_t cmd = pci_read_config(pci_dev.bus, pci_dev.device, pci_dev.function, 0x04);
  cmd |= (1 << 2); /* Bus Master Enable */
  pci_write_config(pci_dev.bus, pci_dev.device, pci_dev.function, 0x04, cmd);

  /* Allocate driver private data */
  priv = malloc(sizeof(struct e1000_priv));
  if (priv == NULL) {
    printf("e1000: failed to allocate private data\n");
    return;
  }
  memset(priv, 0, sizeof(struct e1000_priv));

  /* Map MMIO region into kernel virtual address space.
     E1000 MMIO region is typically 128KB (0x20000 bytes). */
  priv->mmio = (volatile uint32_t*)ioremap(mmio_paddr, 0x20000);
  if (priv->mmio == NULL) {
    printf("e1000: failed to map MMIO at 0x%08x\n", (unsigned)mmio_paddr);
    free(priv);
    return;
  }
  printf("e1000: mapped MMIO 0x%08x -> %p\n", (unsigned)mmio_paddr, priv->mmio);
  priv->irq_line = pci_dev.irq_line;

  /* Reset device */
  e1000_write(priv, E1000_IMC, 0xFFFFFFFF); /* Disable interrupts */
  e1000_write(priv, E1000_CTRL, E1000_CTRL_RST);

  /* Wait for reset */
  for (int i = 0; i < 1000; i++) {
    if (!(e1000_read(priv, E1000_CTRL) & E1000_CTRL_RST))
      break;
  }

  /* Disable interrupts again after reset */
  e1000_write(priv, E1000_IMC, 0xFFFFFFFF);

  /* Set link up */
  uint32_t ctrl = e1000_read(priv, E1000_CTRL);
  ctrl |= E1000_CTRL_SLU;
  e1000_write(priv, E1000_CTRL, ctrl);

  /* Initialize TX and RX rings */
  e1000_tx_init(priv);
  e1000_rx_init(priv);

  /* Register network device */
  dev = netdev_register("eth0", &e1000_ops, priv);
  if (dev == NULL) {
    printf("e1000: failed to register network device\n");
    free(priv);
    return;
  }
  priv->netdev = dev;
  e1000_device = priv;

  /* Register interrupt handler */
  intr_register_ext(0x20 + priv->irq_line, e1000_interrupt, "e1000");

  /* Enable interrupts */
  e1000_write(priv, E1000_IMS, E1000_ICR_TXDW | E1000_ICR_TXQE | E1000_ICR_RXT0 | E1000_ICR_LSC);

  printf("e1000: initialization complete, IRQ %d\n", priv->irq_line);
}
