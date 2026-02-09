/*
 * PhantomOS Bochs/QEMU VGA Driver
 * "To Create, Not To Destroy"
 *
 * Driver for the Bochs Display Interface (BDI) / QEMU stdvga.
 * Provides mode control via DISPI I/O registers and LFB access.
 * No 2D hardware acceleration — all ops return -1 for software fallback.
 */

#ifndef PHANTOMOS_BOCHS_VGA_H
#define PHANTOMOS_BOCHS_VGA_H

#include <stdint.h>

/*============================================================================
 * Bochs DISPI (Display Interface) Constants
 *============================================================================*/

/* PCI identification */
#define BOCHS_VGA_VENDOR_ID     0x1234
#define BOCHS_VGA_DEVICE_ID     0x1111

/* DISPI I/O ports */
#define VBE_DISPI_IOPORT_INDEX  0x01CE
#define VBE_DISPI_IOPORT_DATA   0x01CF

/* DISPI register indices */
#define VBE_DISPI_INDEX_ID          0x00
#define VBE_DISPI_INDEX_XRES        0x01
#define VBE_DISPI_INDEX_YRES        0x02
#define VBE_DISPI_INDEX_BPP         0x03
#define VBE_DISPI_INDEX_ENABLE      0x04
#define VBE_DISPI_INDEX_BANK        0x05
#define VBE_DISPI_INDEX_VIRT_WIDTH  0x06
#define VBE_DISPI_INDEX_VIRT_HEIGHT 0x07
#define VBE_DISPI_INDEX_X_OFFSET    0x08
#define VBE_DISPI_INDEX_Y_OFFSET    0x09

/* DISPI ID versions */
#define VBE_DISPI_ID0              0xB0C0
#define VBE_DISPI_ID5              0xB0C5

/* DISPI enable flags */
#define VBE_DISPI_ENABLED          0x01
#define VBE_DISPI_LFB_ENABLED     0x40
#define VBE_DISPI_NOCLEARMEM      0x80

/*============================================================================
 * API
 *============================================================================*/

/* Register Bochs VGA as a GPU HAL backend */
void bochs_vga_register_hal(void);

#endif /* PHANTOMOS_BOCHS_VGA_H */
