#include "devices/e1000.h"
#include "devices/pci.h"
#include <debug.h>
#include <stdio.h>
#include "threads/vaddr.h"

/* E1000 Vendor/Device IDs */
#define E1000_VENDOR_ID 0x8086
#define E1000_DEVICE_ID 0x100E  /* 82540EM */

/* Initialize E1000 - just detect and print info */
void e1000_init(void) {
  struct pci_device pci_dev;
  
  /* Find E1000 PCI device */
  if (!pci_find_device(E1000_VENDOR_ID, E1000_DEVICE_ID, &pci_dev)) {
    /* Device not found - silently return (network is optional) */
    return;
  }
  
  printf("e1000: found E1000 device\n");
  
  printf("e1000: found at bus %d, device %d, function %d\n",
         pci_dev.bus, pci_dev.device, pci_dev.function);
  printf("e1000: vendor=0x%04x, device=0x%04x\n", 
         pci_dev.vendor_id, pci_dev.device_id);
  
  /* Read BAR0 (MMIO base address) */
  uint32_t bar0 = pci_dev.base_address[0];
  if ((bar0 & 0x1) != 0) {
    printf("e1000: BAR0 is I/O space (0x%08x), not supported\n", bar0);
  } else {
    uintptr_t mmio_paddr = bar0 & ~0xF;  /* Mask out lower bits */
    printf("e1000: MMIO base at physical address 0x%08x\n", (unsigned)mmio_paddr);
    /* Note: MMIO addresses above PHYS_BASE are typically 1:1 mapped */
  }
  
  printf("e1000: IRQ line = %d\n", pci_dev.irq_line);
  printf("e1000: detection complete\n");
}
