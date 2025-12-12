#include "devices/pci.h"
#include "threads/io.h"
#include <debug.h>
#include <stdio.h>

/* PCI Configuration Space Address Port */
#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA 0xCFC

/* Build PCI configuration address */
static uint32_t pci_config_addr(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
  return 0x80000000 | (bus << 16) | (device << 11) | (function << 8) | (offset & 0xFC);
}

/* Read 32-bit value from PCI configuration space */
uint32_t pci_read_config(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
  outl(PCI_CONFIG_ADDRESS, pci_config_addr(bus, device, function, offset));
  return inl(PCI_CONFIG_DATA);
}

/* Write 32-bit value to PCI configuration space */
void pci_write_config(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value) {
  outl(PCI_CONFIG_ADDRESS, pci_config_addr(bus, device, function, offset));
  outl(PCI_CONFIG_DATA, value);
}

/* Find PCI device by vendor/device ID */
bool pci_find_device(uint16_t vendor_id, uint16_t device_id, struct pci_device* dev) {
  uint8_t bus, device, function;
  
  /* Most systems only have a few buses, limit scan to first 8 buses for speed */
  for (bus = 0; bus < 8; bus++) {
    for (device = 0; device < 32; device++) {
      /* Check function 0 first */
      uint32_t id = pci_read_config(bus, device, 0, 0);
      uint16_t ven = id & 0xFFFF;
      uint16_t dev_id = (id >> 16) & 0xFFFF;
      
      if (ven == 0xFFFF) continue; /* Invalid vendor ID */
      
      if (ven == vendor_id && dev_id == device_id) {
        dev->bus = bus;
        dev->device = device;
        dev->function = 0;
        dev->vendor_id = ven;
        dev->device_id = dev_id;
        
        /* Read base address registers */
        for (int i = 0; i < 6; i++) {
          dev->base_address[i] = pci_read_config(bus, device, 0, 0x10 + i * 4);
        }
        
        /* Read IRQ line */
        uint32_t int_line = pci_read_config(bus, device, 0, 0x3C);
        dev->irq_line = int_line & 0xFF;
        
        return true;
      }
      
      /* Check if this is a multi-function device */
      uint32_t header_type = pci_read_config(bus, device, 0, 0x0C);
      if ((header_type & 0x80) != 0) {
        /* Multi-function device - check other functions */
        for (function = 1; function < 8; function++) {
          id = pci_read_config(bus, device, function, 0);
          ven = id & 0xFFFF;
          dev_id = (id >> 16) & 0xFFFF;
          
          if (ven == 0xFFFF) continue;
          
          if (ven == vendor_id && dev_id == device_id) {
            dev->bus = bus;
            dev->device = device;
            dev->function = function;
            dev->vendor_id = ven;
            dev->device_id = dev_id;
            
            /* Read base address registers */
            for (int i = 0; i < 6; i++) {
              dev->base_address[i] = pci_read_config(bus, device, function, 0x10 + i * 4);
            }
            
            /* Read IRQ line */
            uint32_t int_line = pci_read_config(bus, device, function, 0x3C);
            dev->irq_line = int_line & 0xFF;
            
            return true;
          }
        }
      }
    }
  }
  
  return false;
}
