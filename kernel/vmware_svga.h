/*
 * PhantomOS VMware SVGA II GPU Driver
 * "To Create, Not To Destroy"
 *
 * 2D-accelerated graphics via VMware SVGA II FIFO command queue.
 * Supports rectangle fill, rectangle copy, and display update commands.
 * Targets VMware Workstation/Fusion/ESXi and QEMU -vga vmware.
 */

#ifndef PHANTOMOS_VMWARE_SVGA_H
#define PHANTOMOS_VMWARE_SVGA_H

#include <stdint.h>

/*============================================================================
 * PCI Identification
 *============================================================================*/

#define VMWARE_SVGA_VENDOR_ID       0x15AD
#define VMWARE_SVGA_DEVICE_ID       0x0405

/*============================================================================
 * I/O Port Offsets (byte offsets from BAR0 I/O base)
 *============================================================================*/

#define SVGA_INDEX_PORT             0   /* 32-bit register index */
#define SVGA_VALUE_PORT             1   /* 32-bit register value */

/*============================================================================
 * Version Negotiation
 *============================================================================*/

#define SVGA_MAGIC                  0x900000UL
#define SVGA_MAKE_ID(ver)           ((SVGA_MAGIC << 8) | (ver))
#define SVGA_ID_2                   SVGA_MAKE_ID(2)
#define SVGA_ID_1                   SVGA_MAKE_ID(1)
#define SVGA_ID_0                   SVGA_MAKE_ID(0)

/*============================================================================
 * Register Indices (written to SVGA_INDEX_PORT)
 *============================================================================*/

#define SVGA_REG_ID                 0
#define SVGA_REG_ENABLE             1
#define SVGA_REG_WIDTH              2
#define SVGA_REG_HEIGHT             3
#define SVGA_REG_MAX_WIDTH          4
#define SVGA_REG_MAX_HEIGHT         5
#define SVGA_REG_DEPTH              6
#define SVGA_REG_BITS_PER_PIXEL     7
#define SVGA_REG_PSEUDOCOLOR        8
#define SVGA_REG_RED_MASK           9
#define SVGA_REG_GREEN_MASK         10
#define SVGA_REG_BLUE_MASK          11
#define SVGA_REG_BYTES_PER_LINE     12
#define SVGA_REG_FB_START           13
#define SVGA_REG_FB_OFFSET          14
#define SVGA_REG_VRAM_SIZE          15
#define SVGA_REG_FB_SIZE            16
#define SVGA_REG_CAPABILITIES       17
#define SVGA_REG_MEM_START          18
#define SVGA_REG_MEM_SIZE           19
#define SVGA_REG_CONFIG_DONE        20
#define SVGA_REG_SYNC               21
#define SVGA_REG_BUSY               22
#define SVGA_REG_GUEST_ID           23
#define SVGA_REG_SCRATCH_SIZE       29
#define SVGA_REG_MEM_REGS           30
#define SVGA_REG_PITCHLOCK          32
#define SVGA_REG_IRQMASK            33

/*============================================================================
 * FIFO Register Indices (uint32_t offsets in FIFO memory)
 *============================================================================*/

#define SVGA_FIFO_MIN               0
#define SVGA_FIFO_MAX               1
#define SVGA_FIFO_NEXT_CMD          2
#define SVGA_FIFO_STOP              3
#define SVGA_FIFO_CAPABILITIES      4
#define SVGA_FIFO_FLAGS             5
#define SVGA_FIFO_FENCE             6
#define SVGA_FIFO_NUM_REGS          7

/*============================================================================
 * FIFO Commands
 *============================================================================*/

#define SVGA_CMD_INVALID            0
#define SVGA_CMD_UPDATE             1   /* x, y, w, h */
#define SVGA_CMD_RECT_FILL          2   /* color, x, y, w, h */
#define SVGA_CMD_RECT_COPY          3   /* srcX, srcY, dstX, dstY, w, h */
#define SVGA_CMD_FENCE              30  /* fence_id */

/*============================================================================
 * Capability Bits (from SVGA_REG_CAPABILITIES)
 *============================================================================*/

#define SVGA_CAP_NONE               0x00000000
#define SVGA_CAP_RECT_FILL          0x00000001
#define SVGA_CAP_RECT_COPY          0x00000002
#define SVGA_CAP_CURSOR             0x00000020
#define SVGA_CAP_CURSOR_BYPASS      0x00000040
#define SVGA_CAP_CURSOR_BYPASS_2    0x00000080
#define SVGA_CAP_8BIT_EMULATION     0x00000100
#define SVGA_CAP_ALPHA_CURSOR       0x00000200
#define SVGA_CAP_EXTENDED_FIFO      0x00008000
#define SVGA_CAP_PITCHLOCK          0x00020000
#define SVGA_CAP_IRQMASK            0x00040000
#define SVGA_CAP_TRACES             0x00200000

/*============================================================================
 * FIFO Capability Bits
 *============================================================================*/

#define SVGA_FIFO_CAP_NONE          0x00000000
#define SVGA_FIFO_CAP_FENCE         (1 << 0)
#define SVGA_FIFO_CAP_ACCELFRONT    (1 << 1)
#define SVGA_FIFO_CAP_PITCHLOCK     (1 << 2)

/*============================================================================
 * API
 *============================================================================*/

/* Register VMware SVGA as a GPU HAL backend */
void vmware_svga_register_hal(void);

#endif /* PHANTOMOS_VMWARE_SVGA_H */
