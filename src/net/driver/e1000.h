/**
 * @file net/driver/e1000.h
 * @brief Intel E1000 (82540EM) network driver.
 *
 * This driver supports the Intel 82540EM Gigabit Ethernet controller,
 * commonly emulated by QEMU and VirtualBox.
 *
 * FEATURES:
 * - DMA-based transmit and receive
 * - Descriptor ring buffers
 * - Interrupt-driven receive
 * - Full-duplex 1Gbps operation
 *
 * HARDWARE OVERVIEW:
 * The E1000 uses memory-mapped I/O (MMIO) for register access and
 * DMA descriptor rings for packet transfer. Each descriptor points
 * to a buffer in main memory.
 */

#ifndef NET_DRIVER_E1000_H
#define NET_DRIVER_E1000_H

#include <stdint.h>
#include "net/driver/netdev.h"

/*
 * ============================================================
 * PCI Configuration
 * ============================================================
 */

#define E1000_VENDOR_ID 0x8086
#define E1000_DEVICE_ID 0x100E /* 82540EM (QEMU default) */

/*
 * ============================================================
 * E1000 Register Offsets (from MMIO base)
 * ============================================================
 */

/* Device Control */
#define E1000_CTRL 0x00000     /* Device Control */
#define E1000_STATUS 0x00008   /* Device Status */
#define E1000_EECD 0x00010     /* EEPROM/Flash Control */
#define E1000_EERD 0x00014     /* EEPROM Read */
#define E1000_CTRL_EXT 0x00018 /* Extended Device Control */

/* Interrupt */
#define E1000_ICR 0x000C0 /* Interrupt Cause Read */
#define E1000_ICS 0x000C8 /* Interrupt Cause Set */
#define E1000_IMS 0x000D0 /* Interrupt Mask Set */
#define E1000_IMC 0x000D8 /* Interrupt Mask Clear */

/* Receive */
#define E1000_RCTL 0x00100  /* Receive Control */
#define E1000_RDBAL 0x02800 /* RX Descriptor Base Low */
#define E1000_RDBAH 0x02804 /* RX Descriptor Base High */
#define E1000_RDLEN 0x02808 /* RX Descriptor Length */
#define E1000_RDH 0x02810   /* RX Descriptor Head */
#define E1000_RDT 0x02818   /* RX Descriptor Tail */

/* Transmit */
#define E1000_TCTL 0x00400  /* Transmit Control */
#define E1000_TIPG 0x00410  /* TX Inter-Packet Gap */
#define E1000_TDBAL 0x03800 /* TX Descriptor Base Low */
#define E1000_TDBAH 0x03804 /* TX Descriptor Base High */
#define E1000_TDLEN 0x03808 /* TX Descriptor Length */
#define E1000_TDH 0x03810   /* TX Descriptor Head */
#define E1000_TDT 0x03818   /* TX Descriptor Tail */

/* Receive Address */
#define E1000_RAL 0x05400 /* Receive Address Low */
#define E1000_RAH 0x05404 /* Receive Address High */

/* Multicast Table Array */
#define E1000_MTA 0x05200 /* Multicast Table Array (128 entries) */

/*
 * ============================================================
 * Register Bit Definitions
 * ============================================================
 */

/* CTRL - Device Control */
#define E1000_CTRL_SLU (1 << 6)  /* Set Link Up */
#define E1000_CTRL_RST (1 << 26) /* Device Reset */

/* RCTL - Receive Control */
#define E1000_RCTL_EN (1 << 1)          /* Receiver Enable */
#define E1000_RCTL_SBP (1 << 2)         /* Store Bad Packets */
#define E1000_RCTL_UPE (1 << 3)         /* Unicast Promiscuous */
#define E1000_RCTL_MPE (1 << 4)         /* Multicast Promiscuous */
#define E1000_RCTL_LPE (1 << 5)         /* Long Packet Enable */
#define E1000_RCTL_BAM (1 << 15)        /* Broadcast Accept Mode */
#define E1000_RCTL_BSIZE_2048 (0 << 16) /* Buffer size 2048 */
#define E1000_RCTL_BSIZE_1024 (1 << 16) /* Buffer size 1024 */
#define E1000_RCTL_BSIZE_512 (2 << 16)  /* Buffer size 512 */
#define E1000_RCTL_BSIZE_256 (3 << 16)  /* Buffer size 256 */
#define E1000_RCTL_SECRC (1 << 26)      /* Strip Ethernet CRC */

/* TCTL - Transmit Control */
#define E1000_TCTL_EN (1 << 1)   /* Transmitter Enable */
#define E1000_TCTL_PSP (1 << 3)  /* Pad Short Packets */
#define E1000_TCTL_CT_SHIFT 4    /* Collision Threshold shift */
#define E1000_TCTL_COLD_SHIFT 12 /* Collision Distance shift */

/* TIPG - Transmit Inter-Packet Gap */
#define E1000_TIPG_IPGT_DEFAULT 10
#define E1000_TIPG_IPGR1_DEFAULT 8
#define E1000_TIPG_IPGR2_DEFAULT 6

/* ICR/IMS/IMC - Interrupt bits */
#define E1000_ICR_TXDW (1 << 0) /* TX Descriptor Written Back */
#define E1000_ICR_TXQE (1 << 1) /* TX Queue Empty */
#define E1000_ICR_LSC (1 << 2)  /* Link Status Change */
#define E1000_ICR_RXO (1 << 6)  /* Receiver Overrun */
#define E1000_ICR_RXT0 (1 << 7) /* Receiver Timer Interrupt */

/* RAH - Receive Address High */
#define E1000_RAH_AV (1 << 31) /* Address Valid */

/* EERD - EEPROM Read */
#define E1000_EERD_START (1 << 0) /* Start Read */
#define E1000_EERD_DONE (1 << 4)  /* Read Done */
#define E1000_EERD_ADDR_SHIFT 8   /* Address shift */
#define E1000_EERD_DATA_SHIFT 16  /* Data shift */

/*
 * ============================================================
 * Descriptor Structures
 * ============================================================
 */

/* Transmit Descriptor (Legacy format) */
struct e1000_tx_desc {
  uint64_t buffer_addr; /* Physical address of data buffer */
  uint16_t length;      /* Data length */
  uint8_t cso;          /* Checksum offset */
  uint8_t cmd;          /* Command field */
  uint8_t status;       /* Status field */
  uint8_t css;          /* Checksum start */
  uint16_t special;     /* Special field */
} __attribute__((packed));

/* TX Descriptor Command bits */
#define E1000_TXD_CMD_EOP (1 << 0)  /* End of Packet */
#define E1000_TXD_CMD_IFCS (1 << 1) /* Insert FCS */
#define E1000_TXD_CMD_RS (1 << 3)   /* Report Status */

/* TX Descriptor Status bits */
#define E1000_TXD_STAT_DD (1 << 0) /* Descriptor Done */

/* Receive Descriptor */
struct e1000_rx_desc {
  uint64_t buffer_addr; /* Physical address of data buffer */
  uint16_t length;      /* Received data length */
  uint16_t checksum;    /* Packet checksum */
  uint8_t status;       /* Status field */
  uint8_t errors;       /* Error field */
  uint16_t special;     /* Special field */
} __attribute__((packed));

/* RX Descriptor Status bits */
#define E1000_RXD_STAT_DD (1 << 0)  /* Descriptor Done */
#define E1000_RXD_STAT_EOP (1 << 1) /* End of Packet */

/*
 * ============================================================
 * Driver Configuration
 * ============================================================
 */

#define E1000_TX_RING_SIZE 32     /* Must be multiple of 8 */
#define E1000_RX_RING_SIZE 32     /* Must be multiple of 8 */
#define E1000_RX_BUFFER_SIZE 2048 /* Receive buffer size */

/*
 * ============================================================
 * Public Interface
 * ============================================================
 */

/**
 * @brief Initialize the E1000 network driver.
 *
 * Scans PCI bus for E1000 device and initializes it.
 * Registers network device "eth0" if found.
 */
void e1000_driver_init(void);

#endif /* NET_DRIVER_E1000_H */
