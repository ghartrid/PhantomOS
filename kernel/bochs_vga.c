/*
 * PhantomOS Bochs/QEMU VGA Driver
 * "To Create, Not To Destroy"
 *
 * Drives the Bochs Display Interface (BDI) found in QEMU -vga std.
 * Provides mode control via DISPI I/O registers.
 * No 2D acceleration — all drawing ops fall back to CPU software paths.
 */

#include "bochs_vga.h"
#include "gpu_hal.h"
#include "pci.h"
#include "framebuffer.h"
#include "io.h"
#include "vmm.h"
#include <stdint.h>
#include <stddef.h>

/*============================================================================
 * External Declarations
 *============================================================================*/

extern int kprintf(const char *fmt, ...);
extern void *memcpy(void *dest, const void *src, size_t n);
extern void *memset(void *s, int c, size_t n);

/*============================================================================
 * Driver State
 *============================================================================*/

static struct {
    int         detected;
    int         initialized;
    uint16_t    dispi_id;
    uint64_t    lfb_phys;       /* Linear framebuffer physical address (BAR0) */
    uint32_t    lfb_size;
    uint32_t    width;
    uint32_t    height;
    uint32_t    bpp;
    uint64_t    flip_count;
} bochs;

/*============================================================================
 * DISPI Register Access
 *============================================================================*/

static void dispi_write(uint16_t index, uint16_t value)
{
    outw(VBE_DISPI_IOPORT_INDEX, index);
    outw(VBE_DISPI_IOPORT_DATA, value);
}

static uint16_t dispi_read(uint16_t index)
{
    outw(VBE_DISPI_IOPORT_INDEX, index);
    return inw(VBE_DISPI_IOPORT_DATA);
}

/*============================================================================
 * Initialization
 *============================================================================*/

static int bochs_vga_init(void)
{
    memset(&bochs, 0, sizeof(bochs));

    /* Find Bochs/QEMU VGA on PCI */
    const struct pci_device *dev = pci_find_by_id(BOCHS_VGA_VENDOR_ID,
                                                    BOCHS_VGA_DEVICE_ID);
    if (!dev) {
        /* Try DISPI ID probe as fallback (no PCI match) */
        uint16_t id = dispi_read(VBE_DISPI_INDEX_ID);
        if (id < VBE_DISPI_ID0 || id > VBE_DISPI_ID5)
            return -1;
        bochs.dispi_id = id;
        bochs.detected = 1;
        kprintf("[Bochs VGA] Detected via DISPI probe (ID 0x%04x)\n", id);
    } else {
        bochs.detected = 1;
        bochs.dispi_id = dispi_read(VBE_DISPI_INDEX_ID);
        bochs.lfb_phys = dev->bar_addr[0];
        bochs.lfb_size = dev->bar_size[0];
        kprintf("[Bochs VGA] PCI device found: vendor 0x%04x device 0x%04x\n",
                dev->vendor_id, dev->device_id);
        kprintf("[Bochs VGA] DISPI ID: 0x%04x, LFB: 0x%lx (%u MB)\n",
                bochs.dispi_id, (unsigned long)bochs.lfb_phys,
                bochs.lfb_size / 1024 / 1024);
    }

    /* Set mode: 1024x768x32 via DISPI registers */
    dispi_write(VBE_DISPI_INDEX_ENABLE, 0);  /* Disable first */
    dispi_write(VBE_DISPI_INDEX_XRES, 1024);
    dispi_write(VBE_DISPI_INDEX_YRES, 768);
    dispi_write(VBE_DISPI_INDEX_BPP, 32);
    dispi_write(VBE_DISPI_INDEX_ENABLE,
                VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED |
                VBE_DISPI_NOCLEARMEM);

    /* Verify mode was set */
    uint16_t xres = dispi_read(VBE_DISPI_INDEX_XRES);
    uint16_t yres = dispi_read(VBE_DISPI_INDEX_YRES);
    uint16_t bpp  = dispi_read(VBE_DISPI_INDEX_BPP);

    if (xres != 1024 || yres != 768 || bpp != 32) {
        kprintf("[Bochs VGA] Mode set failed: got %ux%ux%u\n", xres, yres, bpp);
        return -1;
    }

    bochs.width = xres;
    bochs.height = yres;
    bochs.bpp = bpp;

    /* Map LFB if we have it from PCI */
    if (bochs.lfb_phys) {
        uint64_t fb_pages = (bochs.width * bochs.height * 4 + 4095) / 4096;
        for (uint64_t i = 0; i < fb_pages; i++) {
            uint64_t addr = bochs.lfb_phys + i * 4096;
            vmm_map_page(addr, addr,
                         PTE_PRESENT | PTE_WRITABLE |
                         PTE_NOCACHE | PTE_WRITETHROUGH);
        }
    }

    bochs.initialized = 1;
    kprintf("[Bochs VGA] Mode: %ux%ux%u (LFB at 0x%lx)\n",
            bochs.width, bochs.height, bochs.bpp,
            (unsigned long)bochs.lfb_phys);
    return 0;
}

/*============================================================================
 * HAL Operations
 *============================================================================*/

static int bochs_available(void)
{
    return bochs.initialized;
}

/* No 2D acceleration — all return -1 */
static int bochs_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                             uint32_t color)
{
    (void)x; (void)y; (void)w; (void)h; (void)color;
    return -1;
}

static int bochs_clear(uint32_t color)
{
    (void)color;
    return -1;
}

static int bochs_copy_region(uint32_t dx, uint32_t dy, uint32_t sx,
                              uint32_t sy, uint32_t w, uint32_t h)
{
    (void)dx; (void)dy; (void)sx; (void)sy; (void)w; (void)h;
    return -1;
}

static int bochs_flip(void)
{
    /* Bochs VGA flip: memcpy backbuffer -> LFB
     * This is basically the same as software but through our mapped LFB.
     * The framebuffer.c fb_flip() already does this, so we return -1
     * to let it handle it. */
    (void)0;
    bochs.flip_count++;
    return -1;
}

static void bochs_sync(void)  { }
static void bochs_wait(void)  { }
static int  bochs_pending(void) { return 0; }

static void bochs_get_stats(struct gpu_stats *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->flips = bochs.flip_count;
}

static void bochs_dump_info(void)
{
    kprintf("\nGPU Backend: Bochs VGA (QEMU stdvga)\n");
    kprintf("  DISPI ID:     0x%04x\n", bochs.dispi_id);
    kprintf("  Resolution:   %ux%ux%u\n", bochs.width, bochs.height, bochs.bpp);
    kprintf("  LFB Address:  0x%lx\n", (unsigned long)bochs.lfb_phys);
    kprintf("  LFB Size:     %u MB\n", bochs.lfb_size / 1024 / 1024);
    kprintf("  2D Accel:     None (CPU software rendering)\n");
    kprintf("  Flip count:   %lu\n", (unsigned long)bochs.flip_count);
}

/*============================================================================
 * Resolution Change
 *============================================================================*/

static int bochs_set_resolution(uint32_t width, uint32_t height)
{
    if (!bochs.initialized) return -1;

    dispi_write(VBE_DISPI_INDEX_ENABLE, 0);
    dispi_write(VBE_DISPI_INDEX_XRES, (uint16_t)width);
    dispi_write(VBE_DISPI_INDEX_YRES, (uint16_t)height);
    dispi_write(VBE_DISPI_INDEX_BPP, 32);
    dispi_write(VBE_DISPI_INDEX_ENABLE,
                VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED |
                VBE_DISPI_NOCLEARMEM);

    uint16_t xres = dispi_read(VBE_DISPI_INDEX_XRES);
    uint16_t yres = dispi_read(VBE_DISPI_INDEX_YRES);
    if (xres != width || yres != height) return -1;

    bochs.width = xres;
    bochs.height = yres;
    kprintf("[Bochs VGA] Resolution changed to %ux%u\n", width, height);
    return 0;
}

/*============================================================================
 * HAL Registration
 *============================================================================*/

static struct gpu_ops bochs_vga_ops = {
    .name        = "Bochs VGA",
    .type        = GPU_BACKEND_BOCHS,
    .priority    = 40,
    .init        = bochs_vga_init,
    .available   = bochs_available,
    .fill_rect   = bochs_fill_rect,
    .clear       = bochs_clear,
    .copy_region = bochs_copy_region,
    .flip        = bochs_flip,
    .set_resolution = bochs_set_resolution,
    .sync        = bochs_sync,
    .wait        = bochs_wait,
    .pending_ops = bochs_pending,
    .get_stats   = bochs_get_stats,
    .dump_info   = bochs_dump_info,
};

void bochs_vga_register_hal(void)
{
    gpu_hal_register(&bochs_vga_ops);
}
