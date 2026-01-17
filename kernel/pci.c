#include "pci.h"

#include "io.h"

#define PCI_CONFIG_ADDRESS_PORT 0xCF8u
#define PCI_CONFIG_DATA_PORT    0xCFCu
#define PCI_CONFIG_ENABLE       0x80000000u

static uint32_t pci_config_address(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset)
{
    uint32_t aligned = offset & 0xFCu;
    return PCI_CONFIG_ENABLE |
           ((uint32_t)bus << 16) |
           ((uint32_t)slot << 11) |
           ((uint32_t)function << 8) |
           aligned;
}

uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset)
{
    uint32_t address = pci_config_address(bus, slot, function, offset);
    outl(PCI_CONFIG_ADDRESS_PORT, address);
    return inl(PCI_CONFIG_DATA_PORT);
}

void pci_config_write32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint32_t value)
{
    uint32_t address = pci_config_address(bus, slot, function, offset);
    outl(PCI_CONFIG_ADDRESS_PORT, address);
    outl(PCI_CONFIG_DATA_PORT, value);
}

uint16_t pci_config_read16(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset)
{
    uint32_t value = pci_config_read32(bus, slot, function, offset);
    uint8_t shift = (offset & 0x2u) * 8u;
    return (uint16_t)((value >> shift) & 0xFFFFu);
}

void pci_config_write16(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint16_t data)
{
    uint32_t value = pci_config_read32(bus, slot, function, offset);
    uint8_t shift = (offset & 0x2u) * 8u;
    uint32_t mask = 0xFFFFu << shift;
    value &= ~mask;
    value |= ((uint32_t)data) << shift;
    pci_config_write32(bus, slot, function, offset, value);
}

static void pci_fill_device_info(uint8_t bus, uint8_t slot, uint8_t function, struct pci_device_info *out)
{
    out->bus = bus;
    out->slot = slot;
    out->function = function;
    out->vendor_id = pci_config_read16(bus, slot, function, 0x00);
    out->device_id = pci_config_read16(bus, slot, function, 0x02);

    uint32_t class_reg = pci_config_read32(bus, slot, function, 0x08);
    out->revision = (uint8_t)(class_reg & 0xFFu);
    out->prog_if = (uint8_t)((class_reg >> 8) & 0xFFu);
    out->subclass = (uint8_t)((class_reg >> 16) & 0xFFu);
    out->class_code = (uint8_t)((class_reg >> 24) & 0xFFu);

    uint32_t header_reg = pci_config_read32(bus, slot, function, 0x0C);
    out->header_type = (uint8_t)((header_reg >> 16) & 0xFFu);

    for (int bar = 0; bar < 6; ++bar)
        out->bar[bar] = pci_config_read32(bus, slot, function, (uint8_t)(0x10 + bar * 4));

    uint32_t interrupt_reg = pci_config_read32(bus, slot, function, 0x3C);
    out->interrupt_line = (uint8_t)(interrupt_reg & 0xFFu);
}

int pci_find_device(uint16_t vendor, uint16_t device, struct pci_device_info *out)
{
    if (!out)
        return -1;

    for (uint16_t bus = 0; bus < 256; ++bus)
    {
        for (uint8_t slot = 0; slot < 32; ++slot)
        {
            uint16_t first_vendor = pci_config_read16(bus, slot, 0, 0x00);
            if (first_vendor == 0xFFFFu)
                continue;

            uint8_t header = (uint8_t)((pci_config_read32(bus, slot, 0, 0x0C) >> 16) & 0xFFu);
            uint8_t functions = (header & 0x80u) ? 8u : 1u;

            for (uint8_t function = 0; function < functions; ++function)
            {
                uint16_t vendor_id = pci_config_read16(bus, slot, function, 0x00);
                if (vendor_id == 0xFFFFu)
                    continue;

                uint16_t device_id = pci_config_read16(bus, slot, function, 0x02);
                if (vendor_id == vendor && device_id == device)
                {
                    pci_fill_device_info(bus, slot, function, out);
                    return 0;
                }
            }
        }
    }

    return -1;
}

int pci_enable_device(const struct pci_device_info *info, uint16_t command_flags)
{
    if (!info)
        return -1;

    uint16_t command = pci_config_read16(info->bus, info->slot, info->function, 0x04);
    command |= command_flags;
    pci_config_write16(info->bus, info->slot, info->function, 0x04, command);
    return 0;
}
