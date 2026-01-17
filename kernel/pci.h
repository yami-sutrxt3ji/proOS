#ifndef PCI_H
#define PCI_H

#include <stdint.h>

struct pci_device_info
{
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t revision;
    uint8_t header_type;
    uint8_t interrupt_line;
    uint32_t bar[6];
};

#define PCI_COMMAND_IO_SPACE        0x0001u
#define PCI_COMMAND_MEMORY_SPACE    0x0002u
#define PCI_COMMAND_BUS_MASTER      0x0004u

int pci_find_device(uint16_t vendor, uint16_t device, struct pci_device_info *out);
int pci_enable_device(const struct pci_device_info *info, uint16_t command_flags);
uint16_t pci_config_read16(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset);
void pci_config_write16(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint16_t value);

#endif
