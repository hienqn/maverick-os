#ifndef DEVICES_PCI_H
#define DEVICES_PCI_H

#include <stdint.h>
#include <stdbool.h>

/* PCI Configuration Space Access */
uint32_t pci_read_config(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
void pci_write_config(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset,
                      uint32_t value);

/* PCI Device Information */
struct pci_device {
  uint8_t bus;
  uint8_t device;
  uint8_t function;
  uint16_t vendor_id;
  uint16_t device_id;
  uint32_t base_address[6];
  uint8_t irq_line;
};

/* Find PCI device by vendor/device ID */
bool pci_find_device(uint16_t vendor_id, uint16_t device_id, struct pci_device* dev);

#endif /* devices/pci.h */
