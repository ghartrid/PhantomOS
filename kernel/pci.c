/*
 * PhantomOS PCI Bus Driver
 * "To Create, Not To Destroy"
 *
 * Enumerates PCI bus 0 via configuration space I/O ports 0xCF8/0xCFC.
 * Detects devices, reads BARs, and provides lookup functions.
 */

#include "pci.h"
#include "io.h"
#include <stdint.h>
#include <stddef.h>

/*============================================================================
 * External Declarations
 *============================================================================*/

extern int kprintf(const char *fmt, ...);
extern void *memset(void *s, int c, size_t n);

/*============================================================================
 * Driver State
 *============================================================================*/

static struct pci_device pci_devices[PCI_MAX_DEVICES];
static int pci_num_devices = 0;

/*============================================================================
 * PCI Configuration Space Access
 *============================================================================*/

static uint32_t pci_make_address(uint8_t bus, uint8_t dev, uint8_t func,
                                  uint8_t offset)
{
    return (uint32_t)((1U << 31) |
                      ((uint32_t)bus << 16) |
                      ((uint32_t)(dev & 0x1F) << 11) |
                      ((uint32_t)(func & 0x07) << 8) |
                      ((uint32_t)offset & 0xFC));
}

uint32_t pci_config_read32(uint8_t bus, uint8_t dev, uint8_t func,
                           uint8_t offset)
{
    outl(PCI_CONFIG_ADDRESS, pci_make_address(bus, dev, func, offset));
    return inl(PCI_CONFIG_DATA);
}

uint16_t pci_config_read16(uint8_t bus, uint8_t dev, uint8_t func,
                           uint8_t offset)
{
    uint32_t val = pci_config_read32(bus, dev, func, offset & 0xFC);
    return (uint16_t)(val >> ((offset & 2) * 8));
}

uint8_t pci_config_read8(uint8_t bus, uint8_t dev, uint8_t func,
                          uint8_t offset)
{
    uint32_t val = pci_config_read32(bus, dev, func, offset & 0xFC);
    return (uint8_t)(val >> ((offset & 3) * 8));
}

void pci_config_write32(uint8_t bus, uint8_t dev, uint8_t func,
                        uint8_t offset, uint32_t value)
{
    outl(PCI_CONFIG_ADDRESS, pci_make_address(bus, dev, func, offset));
    outl(PCI_CONFIG_DATA, value);
}

void pci_config_write16(uint8_t bus, uint8_t dev, uint8_t func,
                        uint8_t offset, uint16_t value)
{
    uint32_t addr = pci_make_address(bus, dev, func, offset & 0xFC);
    outl(PCI_CONFIG_ADDRESS, addr);
    uint32_t old = inl(PCI_CONFIG_DATA);
    int shift = (offset & 2) * 8;
    old &= ~(0xFFFF << shift);
    old |= ((uint32_t)value << shift);
    outl(PCI_CONFIG_DATA, old);
}

/*============================================================================
 * BAR Probing
 *============================================================================*/

static void pci_probe_bars(struct pci_device *d)
{
    int max_bars = (d->header_type & 0x7F) == 0 ? 6 : 2;

    for (int i = 0; i < max_bars; i++) {
        uint8_t bar_offset = PCI_REG_BAR0 + (uint8_t)(i * 4);

        /* Read original BAR value */
        uint32_t bar = pci_config_read32(d->bus, d->device, d->function,
                                          bar_offset);
        d->bar[i] = bar;

        if (bar == 0) {
            d->bar_addr[i] = 0;
            d->bar_size[i] = 0;
            continue;
        }

        /* Determine BAR type */
        if (bar & PCI_BAR_IO_MASK) {
            /* I/O BAR */
            d->bar_is_io[i] = 1;
            d->bar_addr[i] = bar & PCI_BAR_ADDR_IO_MASK;

            /* Probe size */
            pci_config_write32(d->bus, d->device, d->function,
                               bar_offset, 0xFFFFFFFF);
            uint32_t size_mask = pci_config_read32(d->bus, d->device,
                                                    d->function, bar_offset);
            pci_config_write32(d->bus, d->device, d->function,
                               bar_offset, bar); /* restore */

            size_mask &= PCI_BAR_ADDR_IO_MASK;
            if (size_mask)
                d->bar_size[i] = (~size_mask) + 1;
        } else {
            /* Memory BAR */
            d->bar_is_io[i] = 0;
            uint8_t mem_type = (bar >> 1) & 0x03;

            if (mem_type == 2) {
                /* 64-bit BAR */
                d->bar_is_64bit[i] = 1;
                uint32_t bar_high = 0;
                if (i + 1 < max_bars) {
                    bar_high = pci_config_read32(d->bus, d->device,
                                                  d->function,
                                                  bar_offset + 4);
                    d->bar[i + 1] = bar_high;
                }
                d->bar_addr[i] = ((uint64_t)bar_high << 32) |
                                 (bar & PCI_BAR_ADDR_MEM_MASK);

                /* Probe size: write all 1s to both halves */
                pci_config_write32(d->bus, d->device, d->function,
                                   bar_offset, 0xFFFFFFFF);
                if (i + 1 < max_bars)
                    pci_config_write32(d->bus, d->device, d->function,
                                       bar_offset + 4, 0xFFFFFFFF);

                uint32_t size_lo = pci_config_read32(d->bus, d->device,
                                                      d->function,
                                                      bar_offset);
                uint32_t size_hi = 0;
                if (i + 1 < max_bars)
                    size_hi = pci_config_read32(d->bus, d->device,
                                                d->function, bar_offset + 4);

                /* Restore */
                pci_config_write32(d->bus, d->device, d->function,
                                   bar_offset, bar);
                if (i + 1 < max_bars)
                    pci_config_write32(d->bus, d->device, d->function,
                                       bar_offset + 4, bar_high);

                uint64_t size64 = ((uint64_t)size_hi << 32) |
                                  (size_lo & PCI_BAR_ADDR_MEM_MASK);
                if (size64)
                    d->bar_size[i] = (uint32_t)((~size64) + 1);

                /* Skip next BAR (upper 32 bits of 64-bit BAR) */
                i++;
            } else {
                /* 32-bit BAR */
                d->bar_addr[i] = bar & PCI_BAR_ADDR_MEM_MASK;

                /* Probe size */
                pci_config_write32(d->bus, d->device, d->function,
                                   bar_offset, 0xFFFFFFFF);
                uint32_t size_mask = pci_config_read32(d->bus, d->device,
                                                        d->function,
                                                        bar_offset);
                pci_config_write32(d->bus, d->device, d->function,
                                   bar_offset, bar); /* restore */

                size_mask &= PCI_BAR_ADDR_MEM_MASK;
                if (size_mask)
                    d->bar_size[i] = (~size_mask) + 1;
            }
        }
    }
}

/*============================================================================
 * Device Enumeration
 *============================================================================*/

static void pci_scan_device(uint8_t bus, uint8_t dev, uint8_t func)
{
    uint16_t vendor = pci_config_read16(bus, dev, func, PCI_REG_VENDOR_ID);
    if (vendor == 0xFFFF)
        return; /* No device */

    if (pci_num_devices >= PCI_MAX_DEVICES)
        return;

    struct pci_device *d = &pci_devices[pci_num_devices];
    memset(d, 0, sizeof(*d));

    d->bus = bus;
    d->device = dev;
    d->function = func;
    d->vendor_id = vendor;
    d->device_id = pci_config_read16(bus, dev, func, PCI_REG_DEVICE_ID);
    d->class_code = pci_config_read8(bus, dev, func, PCI_REG_CLASS);
    d->subclass = pci_config_read8(bus, dev, func, PCI_REG_SUBCLASS);
    d->prog_if = pci_config_read8(bus, dev, func, PCI_REG_PROG_IF);
    d->revision = pci_config_read8(bus, dev, func, PCI_REG_REVISION);
    d->header_type = pci_config_read8(bus, dev, func, PCI_REG_HEADER_TYPE);
    d->irq_line = pci_config_read8(bus, dev, func, PCI_REG_IRQ_LINE);
    d->irq_pin = pci_config_read8(bus, dev, func, PCI_REG_IRQ_PIN);

    /* Probe BARs */
    pci_probe_bars(d);

    pci_num_devices++;
}

void pci_init(void)
{
    pci_num_devices = 0;
    memset(pci_devices, 0, sizeof(pci_devices));

    /* Scan bus 0, all 32 device slots */
    for (uint8_t dev = 0; dev < 32; dev++) {
        uint16_t vendor = pci_config_read16(0, dev, 0, PCI_REG_VENDOR_ID);
        if (vendor == 0xFFFF)
            continue;

        pci_scan_device(0, dev, 0);

        /* Check if multi-function device */
        uint8_t header = pci_config_read8(0, dev, 0, PCI_REG_HEADER_TYPE);
        if (header & 0x80) {
            for (uint8_t func = 1; func < 8; func++) {
                pci_scan_device(0, dev, func);
            }
        }
    }

    kprintf("  PCI: %d device%s on bus 0\n",
            pci_num_devices, pci_num_devices != 1 ? "s" : "");
}

/*============================================================================
 * Lookup Functions
 *============================================================================*/

const struct pci_device *pci_find_device(uint8_t class_code,
                                         uint8_t subclass)
{
    for (int i = 0; i < pci_num_devices; i++) {
        if (pci_devices[i].class_code == class_code &&
            pci_devices[i].subclass == subclass)
            return &pci_devices[i];
    }
    return NULL;
}

const struct pci_device *pci_find_by_id(uint16_t vendor_id,
                                        uint16_t device_id)
{
    for (int i = 0; i < pci_num_devices; i++) {
        if (pci_devices[i].vendor_id == vendor_id &&
            pci_devices[i].device_id == device_id)
            return &pci_devices[i];
    }
    return NULL;
}

int pci_device_count(void)
{
    return pci_num_devices;
}

const struct pci_device *pci_get_device(int index)
{
    if (index < 0 || index >= pci_num_devices)
        return NULL;
    return &pci_devices[index];
}

/*============================================================================
 * PCI Command Register Helpers
 *============================================================================*/

void pci_enable_bus_master(const struct pci_device *dev)
{
    if (!dev) return;
    uint16_t cmd = pci_config_read16(dev->bus, dev->device, dev->function,
                                      PCI_REG_COMMAND);
    if (!(cmd & PCI_CMD_BUS_MASTER)) {
        cmd |= PCI_CMD_BUS_MASTER;
        pci_config_write16(dev->bus, dev->device, dev->function,
                           PCI_REG_COMMAND, cmd);
    }
}

void pci_enable_memory_space(const struct pci_device *dev)
{
    if (!dev) return;
    uint16_t cmd = pci_config_read16(dev->bus, dev->device, dev->function,
                                      PCI_REG_COMMAND);
    if (!(cmd & PCI_CMD_MEMORY_SPACE)) {
        cmd |= PCI_CMD_MEMORY_SPACE;
        pci_config_write16(dev->bus, dev->device, dev->function,
                           PCI_REG_COMMAND, cmd);
    }
}

/*============================================================================
 * Debug Output
 *============================================================================*/

static const char *pci_class_name(uint8_t class_code, uint8_t subclass)
{
    switch (class_code) {
    case 0x00:
        return subclass == 0x01 ? "VGA Compatible" : "Unclassified";
    case 0x01:
        switch (subclass) {
        case 0x01: return "IDE Controller";
        case 0x06: return "SATA Controller";
        case 0x08: return "NVMe Controller";
        default:   return "Storage";
        }
    case 0x02:
        return subclass == 0x00 ? "Ethernet" : "Network";
    case 0x03:
        return subclass == 0x00 ? "VGA Controller" : "Display";
    case 0x04:
        return subclass == 0x03 ? "Audio Device" : "Multimedia";
    case 0x06:
        switch (subclass) {
        case 0x00: return "Host Bridge";
        case 0x01: return "ISA Bridge";
        case 0x04: return "PCI Bridge";
        default:   return "Bridge";
        }
    case 0x0C:
        switch (subclass) {
        case 0x03: return "USB Controller";
        case 0x05: return "SMBus";
        default:   return "Serial Bus";
        }
    default:
        return "Other";
    }
}

void pci_dump_devices(void)
{
    kprintf("\nPCI Devices (bus 0):\n");
    kprintf("  %-6s %-11s %-6s %s\n", "BDF", "Vendor:Dev", "Class", "Type");
    kprintf("  %-6s %-11s %-6s %s\n", "------", "-----------",
            "------", "--------------------");

    for (int i = 0; i < pci_num_devices; i++) {
        const struct pci_device *d = &pci_devices[i];
        kprintf("  %d:%02d.%d  %04x:%04x   %02x:%02x  %s\n",
                d->bus, d->device, d->function,
                d->vendor_id, d->device_id,
                d->class_code, d->subclass,
                pci_class_name(d->class_code, d->subclass));

        /* Print non-zero BARs */
        for (int b = 0; b < 6; b++) {
            if (d->bar_addr[b] == 0)
                continue;
            kprintf("         BAR%d: 0x%lx (%u KB, %s)\n",
                    b,
                    (unsigned long)d->bar_addr[b],
                    d->bar_size[b] / 1024,
                    d->bar_is_io[b] ? "I/O" : "Memory");
        }
    }
    kprintf("  Total: %d devices\n", pci_num_devices);
}
