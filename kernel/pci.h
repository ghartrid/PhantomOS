/*
 * PhantomOS PCI Bus Driver
 * "To Create, Not To Destroy"
 *
 * PCI Configuration Space access via I/O ports 0xCF8/0xCFC.
 * Scans bus 0 for integrated devices (iGPU, bridges, etc.).
 */

#ifndef PHANTOMOS_PCI_H
#define PHANTOMOS_PCI_H

#include <stdint.h>

/*============================================================================
 * PCI Configuration Space I/O Ports
 *============================================================================*/

#define PCI_CONFIG_ADDRESS      0x0CF8
#define PCI_CONFIG_DATA         0x0CFC

/*============================================================================
 * PCI Configuration Register Offsets
 *============================================================================*/

#define PCI_REG_VENDOR_ID       0x00    /* 16-bit */
#define PCI_REG_DEVICE_ID       0x02    /* 16-bit */
#define PCI_REG_COMMAND         0x04    /* 16-bit */
#define PCI_REG_STATUS          0x06    /* 16-bit */
#define PCI_REG_REVISION        0x08    /* 8-bit */
#define PCI_REG_PROG_IF         0x09    /* 8-bit */
#define PCI_REG_SUBCLASS        0x0A    /* 8-bit */
#define PCI_REG_CLASS           0x0B    /* 8-bit */
#define PCI_REG_CACHE_LINE      0x0C    /* 8-bit */
#define PCI_REG_LATENCY         0x0D    /* 8-bit */
#define PCI_REG_HEADER_TYPE     0x0E    /* 8-bit */
#define PCI_REG_BIST            0x0F    /* 8-bit */
#define PCI_REG_BAR0            0x10    /* 32-bit */
#define PCI_REG_BAR1            0x14    /* 32-bit */
#define PCI_REG_BAR2            0x18    /* 32-bit */
#define PCI_REG_BAR3            0x1C    /* 32-bit */
#define PCI_REG_BAR4            0x20    /* 32-bit */
#define PCI_REG_BAR5            0x24    /* 32-bit */
#define PCI_REG_SUBSYS_VENDOR   0x2C    /* 16-bit */
#define PCI_REG_SUBSYS_ID       0x2E    /* 16-bit */
#define PCI_REG_IRQ_LINE        0x3C    /* 8-bit */
#define PCI_REG_IRQ_PIN         0x3D    /* 8-bit */

/*============================================================================
 * PCI Command Register Bits
 *============================================================================*/

#define PCI_CMD_IO_SPACE        (1 << 0)
#define PCI_CMD_MEMORY_SPACE    (1 << 1)
#define PCI_CMD_BUS_MASTER      (1 << 2)

/*============================================================================
 * PCI Class Codes
 *============================================================================*/

#define PCI_CLASS_HOST_BRIDGE   0x06
#define PCI_CLASS_DISPLAY       0x03
#define PCI_CLASS_MULTIMEDIA    0x04
#define PCI_CLASS_STORAGE       0x01
#define PCI_CLASS_NETWORK       0x02
#define PCI_CLASS_BRIDGE        0x06
#define PCI_CLASS_SERIAL        0x0C

#define PCI_SUBCLASS_VGA        0x00
#define PCI_SUBCLASS_IDE        0x01
#define PCI_SUBCLASS_ETHERNET   0x00
#define PCI_SUBCLASS_USB        0x03
#define PCI_SUBCLASS_ISA        0x01
#define PCI_SUBCLASS_PCI        0x04

/*============================================================================
 * BAR Type Detection
 *============================================================================*/

#define PCI_BAR_IO_MASK         0x01
#define PCI_BAR_MEM_TYPE_MASK   0x06
#define PCI_BAR_MEM_32BIT       0x00
#define PCI_BAR_MEM_64BIT       0x04
#define PCI_BAR_MEM_PREFETCH    0x08
#define PCI_BAR_ADDR_MEM_MASK   0xFFFFFFF0
#define PCI_BAR_ADDR_IO_MASK    0xFFFFFFFC

/*============================================================================
 * Limits
 *============================================================================*/

#define PCI_MAX_DEVICES         32

/*============================================================================
 * PCI Device Structure
 *============================================================================*/

struct pci_device {
    uint8_t     bus;
    uint8_t     device;
    uint8_t     function;
    uint16_t    vendor_id;
    uint16_t    device_id;
    uint8_t     class_code;
    uint8_t     subclass;
    uint8_t     prog_if;
    uint8_t     revision;
    uint8_t     header_type;
    uint32_t    bar[6];             /* Raw BAR values */
    uint64_t    bar_addr[6];        /* Decoded base addresses */
    uint32_t    bar_size[6];        /* BAR region sizes */
    uint8_t     bar_is_io[6];       /* 1 = I/O, 0 = memory */
    uint8_t     bar_is_64bit[6];    /* 1 = 64-bit memory BAR */
    uint8_t     irq_line;
    uint8_t     irq_pin;
};

/*============================================================================
 * API Functions
 *============================================================================*/

/* Initialize PCI subsystem and enumerate bus 0 */
void pci_init(void);

/* Read PCI configuration space */
uint32_t pci_config_read32(uint8_t bus, uint8_t dev, uint8_t func,
                           uint8_t offset);
uint16_t pci_config_read16(uint8_t bus, uint8_t dev, uint8_t func,
                           uint8_t offset);
uint8_t  pci_config_read8(uint8_t bus, uint8_t dev, uint8_t func,
                           uint8_t offset);

/* Write PCI configuration space */
void pci_config_write32(uint8_t bus, uint8_t dev, uint8_t func,
                        uint8_t offset, uint32_t value);
void pci_config_write16(uint8_t bus, uint8_t dev, uint8_t func,
                        uint8_t offset, uint16_t value);

/* Find device by class/subclass (returns first match, or NULL) */
const struct pci_device *pci_find_device(uint8_t class_code,
                                         uint8_t subclass);

/* Find device by vendor/device ID */
const struct pci_device *pci_find_by_id(uint16_t vendor_id,
                                        uint16_t device_id);

/* Get device count and by index */
int pci_device_count(void);
const struct pci_device *pci_get_device(int index);

/* Enable bus mastering for a device */
void pci_enable_bus_master(const struct pci_device *dev);

/* Enable memory space access */
void pci_enable_memory_space(const struct pci_device *dev);

/* Print all detected PCI devices */
void pci_dump_devices(void);

#endif /* PHANTOMOS_PCI_H */
