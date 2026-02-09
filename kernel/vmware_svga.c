/*
 * PhantomOS VMware SVGA II GPU Driver
 * "To Create, Not To Destroy"
 *
 * 2D-accelerated graphics via VMware SVGA II FIFO command queue.
 * Detected on PCI as vendor 0x15AD (VMware), device 0x0405 (SVGA II).
 *
 * Architecture:
 *   1. Detect VMware SVGA II device on PCI bus
 *   2. Negotiate SVGA ID version (prefer ID_2 for 2D accel)
 *   3. Map FIFO memory (BAR2) and guest framebuffer (BAR1)
 *   4. Initialize FIFO command queue
 *   5. Provide 2D-accelerated fill, copy, and display update
 */

#include "vmware_svga.h"
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
extern void *memset(void *s, int c, size_t n);
extern void *memcpy(void *dest, const void *src, size_t n);
extern void *memmove(void *dest, const void *src, size_t n);

/*============================================================================
 * Driver State
 *============================================================================*/

static struct {
    /* Detection */
    int                     detected;
    int                     initialized;
    const struct pci_device *pci_dev;

    /* I/O ports (BAR0) */
    uint16_t                iobase;

    /* Guest framebuffer (BAR1) */
    volatile uint32_t      *gfb;            /* Mapped GFB address */
    uint64_t                gfb_phys;
    uint32_t                gfb_size;

    /* FIFO command buffer (BAR2) */
    volatile uint32_t      *fifo;           /* Mapped FIFO address */
    uint64_t                fifo_phys;
    uint32_t                fifo_size;

    /* Version and capabilities */
    uint32_t                svga_id;
    uint32_t                capabilities;
    int                     has_rect_fill;
    int                     has_rect_copy;

    /* Display mode */
    uint32_t                width;
    uint32_t                height;
    uint32_t                bpp;
    uint32_t                pitch;          /* Bytes per scanline */

    /* Operation tracking */
    uint32_t                pending_ops;

    /* Statistics */
    uint64_t                fills;
    uint64_t                clears;
    uint64_t                copies;
    uint64_t                screen_copies;
    uint64_t                flips;
    uint64_t                updates;
    uint64_t                batched_ops;
    uint64_t                sw_fallbacks;
    uint64_t                bytes_transferred;
} svga;

/*============================================================================
 * Register Access
 *============================================================================*/

static void svga_write_reg(uint32_t index, uint32_t value)
{
    outl(svga.iobase + SVGA_INDEX_PORT, index);
    outl(svga.iobase + SVGA_VALUE_PORT, value);
}

static uint32_t svga_read_reg(uint32_t index)
{
    outl(svga.iobase + SVGA_INDEX_PORT, index);
    return inl(svga.iobase + SVGA_VALUE_PORT);
}

/*============================================================================
 * FIFO Management
 *============================================================================*/

static int fifo_has_space(uint32_t dwords)
{
    uint32_t bytes_needed = dwords * 4;
    uint32_t next_cmd = svga.fifo[SVGA_FIFO_NEXT_CMD];
    uint32_t stop = svga.fifo[SVGA_FIFO_STOP];
    uint32_t max = svga.fifo[SVGA_FIFO_MAX];
    uint32_t min = svga.fifo[SVGA_FIFO_MIN];
    uint32_t space;

    if (next_cmd >= stop) {
        space = (max - next_cmd) + (stop - min);
    } else {
        space = stop - next_cmd;
    }

    /* Keep 4-byte slack to avoid full==empty ambiguity */
    return space > (bytes_needed + 4);
}

static void fifo_write_cmd(uint32_t value)
{
    uint32_t next = svga.fifo[SVGA_FIFO_NEXT_CMD];
    svga.fifo[next / 4] = value;
    next += 4;
    if (next >= svga.fifo[SVGA_FIFO_MAX])
        next = svga.fifo[SVGA_FIFO_MIN];
    svga.fifo[SVGA_FIFO_NEXT_CMD] = next;
}

static void fifo_sync(void)
{
    svga_write_reg(SVGA_REG_SYNC, 1);

    /* Poll BUSY until device drains the FIFO */
    int timeout = 2000000;
    while (svga_read_reg(SVGA_REG_BUSY) != 0) {
        __asm__ volatile("pause");
        if (--timeout <= 0) {
            kprintf("[VMware SVGA] Warning: FIFO sync timeout\n");
            break;
        }
    }
}

static int fifo_ensure_space(uint32_t dwords)
{
    if (fifo_has_space(dwords))
        return 0;

    /* Force drain and retry */
    fifo_sync();
    svga.pending_ops = 0;

    if (fifo_has_space(dwords))
        return 0;

    return -1;  /* Still full after sync (shouldn't happen) */
}

/*============================================================================
 * Initialization
 *============================================================================*/

static int vmware_svga_init(void)
{
    memset(&svga, 0, sizeof(svga));

    /* Find VMware SVGA II on PCI */
    const struct pci_device *dev = pci_find_by_id(VMWARE_SVGA_VENDOR_ID,
                                                    VMWARE_SVGA_DEVICE_ID);
    if (!dev) return -1;

    svga.detected = 1;
    svga.pci_dev = dev;

    kprintf("[VMware SVGA] PCI device found: vendor 0x%04x device 0x%04x\n",
            dev->vendor_id, dev->device_id);

    /* Enable PCI I/O space, memory space, and bus mastering */
    uint16_t cmd = pci_config_read16(dev->bus, dev->device, dev->function,
                                      PCI_REG_COMMAND);
    cmd |= PCI_CMD_IO_SPACE | PCI_CMD_MEMORY_SPACE | PCI_CMD_BUS_MASTER;
    pci_config_write16(dev->bus, dev->device, dev->function,
                       PCI_REG_COMMAND, cmd);

    /* Extract BAR addresses */
    uint32_t raw_bar0 = pci_config_read32(dev->bus, dev->device,
                                            dev->function, 0x10);
    if (!(raw_bar0 & 0x01)) {
        kprintf("[VMware SVGA] BAR0 is not I/O space\n");
        return -1;
    }
    svga.iobase = (uint16_t)(raw_bar0 & 0xFFFC);
    svga.gfb_phys = dev->bar_addr[1];
    svga.gfb_size = dev->bar_size[1];
    svga.fifo_phys = dev->bar_addr[2];
    svga.fifo_size = dev->bar_size[2];

    /* Version negotiation: try SVGA_ID_2 first (needed for 2D accel) */
    svga.svga_id = 0;
    for (int ver = 2; ver >= 0; ver--) {
        uint32_t id = (uint32_t)SVGA_MAKE_ID(ver);
        svga_write_reg(SVGA_REG_ID, id);
        uint32_t readback = svga_read_reg(SVGA_REG_ID);
        if (readback == id) {
            svga.svga_id = id;
            break;
        }
    }

    if (svga.svga_id == 0) {
        kprintf("[VMware SVGA] Version negotiation failed\n");
        return -1;
    }

    /* Read capabilities */
    svga.capabilities = svga_read_reg(SVGA_REG_CAPABILITIES);
    svga.has_rect_fill = (svga.capabilities & SVGA_CAP_RECT_FILL) ? 1 : 0;
    svga.has_rect_copy = (svga.capabilities & SVGA_CAP_RECT_COPY) ? 1 : 0;

    /* Map FIFO memory */
    uint64_t fifo_pages = (svga.fifo_size + 4095) / 4096;
    for (uint64_t i = 0; i < fifo_pages; i++) {
        uint64_t addr = svga.fifo_phys + i * 4096;
        vmm_map_page(addr, addr,
                     PTE_PRESENT | PTE_WRITABLE |
                     PTE_NOCACHE | PTE_WRITETHROUGH);
    }
    svga.fifo = (volatile uint32_t *)svga.fifo_phys;

    /* Map guest framebuffer */
    uint64_t gfb_pages = (svga.gfb_size + 4095) / 4096;
    for (uint64_t i = 0; i < gfb_pages; i++) {
        uint64_t addr = svga.gfb_phys + i * 4096;
        vmm_map_page(addr, addr,
                     PTE_PRESENT | PTE_WRITABLE |
                     PTE_NOCACHE | PTE_WRITETHROUGH);
    }
    svga.gfb = (volatile uint32_t *)svga.gfb_phys;

    /* Set display mode */
    svga_write_reg(SVGA_REG_WIDTH, 1024);
    svga_write_reg(SVGA_REG_HEIGHT, 768);
    svga_write_reg(SVGA_REG_BITS_PER_PIXEL, 32);
    svga_write_reg(SVGA_REG_ENABLE, 1);

    svga.width = svga_read_reg(SVGA_REG_WIDTH);
    svga.height = svga_read_reg(SVGA_REG_HEIGHT);
    svga.bpp = svga_read_reg(SVGA_REG_BITS_PER_PIXEL);
    svga.pitch = svga_read_reg(SVGA_REG_BYTES_PER_LINE);

    /* Initialize FIFO */
    uint32_t fifo_min = SVGA_FIFO_NUM_REGS * sizeof(uint32_t);
    svga.fifo[SVGA_FIFO_MIN] = fifo_min;
    svga.fifo[SVGA_FIFO_MAX] = svga.fifo_size;
    svga.fifo[SVGA_FIFO_NEXT_CMD] = fifo_min;
    svga.fifo[SVGA_FIFO_STOP] = fifo_min;

    /* Signal FIFO initialization complete */
    svga_write_reg(SVGA_REG_CONFIG_DONE, 1);

    svga.initialized = 1;
    kprintf("[VMware SVGA] %ux%ux%u SVGA_ID_%u (VRAM %u KB, caps 0x%x)\n",
            svga.width, svga.height, svga.bpp, svga.svga_id & 0xFF,
            svga_read_reg(SVGA_REG_VRAM_SIZE) / 1024, svga.capabilities);
    return 0;
}

/*============================================================================
 * HAL Operations
 *============================================================================*/

static int hal_vmware_available(void)
{
    return svga.initialized;
}

static int hal_vmware_fill_rect(uint32_t x, uint32_t y, uint32_t w,
                                 uint32_t h, uint32_t color)
{
    if (!svga.initialized || !svga.has_rect_fill)
        return -1;

    /* Skip small rects — overhead not worth it */
    if (w < 16 || h < 16)
        return -1;

    /* Clip to screen bounds */
    if (x >= svga.width || y >= svga.height) return -1;
    if (x + w > svga.width)  w = svga.width - x;
    if (y + h > svga.height) h = svga.height - y;

    /* Fill the CPU backbuffer — flip() pushes it to GFB via SVGA_CMD_UPDATE */
    const struct framebuffer_info *fb = fb_get_info();
    if (fb && fb->backbuffer) {
        for (uint32_t row = 0; row < h; row++) {
            uint32_t *dst = &fb->backbuffer[(y + row) * fb->width + x];
            for (uint32_t col = 0; col < w; col++)
                dst[col] = color;
        }
    }

    svga.fills++;
    svga.bytes_transferred += (uint64_t)w * h * 4;

    return 0;
}

static int hal_vmware_clear(uint32_t color)
{
    if (!svga.initialized)
        return -1;
    return hal_vmware_fill_rect(0, 0, svga.width, svga.height, color);
}

static int hal_vmware_copy_region(uint32_t dst_x, uint32_t dst_y,
                                    uint32_t src_x, uint32_t src_y,
                                    uint32_t w, uint32_t h)
{
    if (!svga.initialized || !svga.has_rect_copy)
        return -1;

    /* Skip small copies */
    if (w < 16 || h < 16)
        return -1;

    /* Clip to screen bounds */
    if (dst_x >= svga.width || dst_y >= svga.height) return -1;
    if (src_x >= svga.width || src_y >= svga.height) return -1;
    if (dst_x + w > svga.width)  w = svga.width - dst_x;
    if (dst_y + h > svga.height) h = svga.height - dst_y;
    if (src_x + w > svga.width)  w = svga.width - src_x;
    if (src_y + h > svga.height) h = svga.height - src_y;

    /* CPU memmove on backbuffer — flip() pushes it to GFB via SVGA_CMD_UPDATE */
    const struct framebuffer_info *fb = fb_get_info();
    if (fb && fb->backbuffer) {
        if (dst_y < src_y || (dst_y == src_y && dst_x < src_x)) {
            for (uint32_t row = 0; row < h; row++) {
                uint32_t *d = &fb->backbuffer[(dst_y + row) * fb->width + dst_x];
                uint32_t *s = &fb->backbuffer[(src_y + row) * fb->width + src_x];
                memmove(d, s, w * 4);
            }
        } else {
            for (uint32_t row = h; row > 0; row--) {
                uint32_t *d = &fb->backbuffer[(dst_y + row - 1) * fb->width + dst_x];
                uint32_t *s = &fb->backbuffer[(src_y + row - 1) * fb->width + src_x];
                memmove(d, s, w * 4);
            }
        }
    }

    svga.screen_copies++;
    svga.bytes_transferred += (uint64_t)w * h * 4;

    return 0;
}

static int hal_vmware_flip(void)
{
    if (!svga.initialized)
        return -1;

    /* Copy backbuffer to guest framebuffer */
    const struct framebuffer_info *fb = fb_get_info();
    if (!fb || !fb->backbuffer)
        return -1;

    uint32_t row_bytes = fb->width * 4;

    if (svga.pitch == row_bytes) {
        /* Pitch matches: single bulk copy */
        memcpy((void *)svga.gfb, fb->backbuffer, row_bytes * fb->height);
    } else {
        /* Pitch differs: row-by-row copy */
        uint8_t *src = (uint8_t *)fb->backbuffer;
        uint8_t *dst = (uint8_t *)svga.gfb;
        for (uint32_t row = 0; row < fb->height; row++) {
            memcpy(dst, src, row_bytes);
            src += row_bytes;
            dst += svga.pitch;
        }
    }

    /* Tell device to push GFB to display: UPDATE (1 + 4 = 5 dwords) */
    if (fifo_ensure_space(5) != 0) {
        svga.sw_fallbacks++;
        return 0;
    }

    fifo_write_cmd(SVGA_CMD_UPDATE);
    fifo_write_cmd(0);
    fifo_write_cmd(0);
    fifo_write_cmd(svga.width);
    fifo_write_cmd(svga.height);

    svga.flips++;
    svga.updates++;
    svga.bytes_transferred += (uint64_t)svga.width * svga.height * 4;

    return 0;
}

static void hal_vmware_sync(void)
{
    if (!svga.initialized || svga.pending_ops == 0)
        return;
    fifo_sync();
    svga.pending_ops = 0;
}

static void hal_vmware_wait(void)
{
    if (!svga.initialized)
        return;
    fifo_sync();
    svga.pending_ops = 0;
}

static int hal_vmware_pending(void)
{
    return svga.pending_ops;
}

/*============================================================================
 * Diagnostics
 *============================================================================*/

static void hal_vmware_get_stats(struct gpu_stats *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->fills = svga.fills;
    out->clears = svga.clears;
    out->copies = svga.copies;
    out->screen_copies = svga.screen_copies;
    out->flips = svga.flips;
    out->batched_ops = svga.batched_ops;
    out->sw_fallbacks = svga.sw_fallbacks;
    out->bytes_transferred = svga.bytes_transferred;
}

static void hal_vmware_dump_info(void)
{
    kprintf("\nGPU Backend: VMware SVGA II\n");
    kprintf("  SVGA ID:       %u\n", svga.svga_id & 0xFF);
    kprintf("  Capabilities:  0x%08x\n", svga.capabilities);
    kprintf("  2D Rect Fill:  %s\n", svga.has_rect_fill ? "Yes" : "No");
    kprintf("  2D Rect Copy:  %s\n", svga.has_rect_copy ? "Yes" : "No");
    kprintf("  Resolution:    %ux%ux%u (pitch %u)\n",
            svga.width, svga.height, svga.bpp, svga.pitch);
    kprintf("  GFB:           0x%lx (%u KB)\n",
            (unsigned long)svga.gfb_phys, svga.gfb_size / 1024);
    kprintf("  FIFO:          0x%lx (%u KB)\n",
            (unsigned long)svga.fifo_phys, svga.fifo_size / 1024);
    kprintf("  Statistics:\n");
    kprintf("    Fills:       %lu\n", (unsigned long)svga.fills);
    kprintf("    Clears:      %lu\n", (unsigned long)svga.clears);
    kprintf("    Copies:      %lu\n", (unsigned long)svga.screen_copies);
    kprintf("    Flips:       %lu\n", (unsigned long)svga.flips);
    kprintf("    Updates:     %lu\n", (unsigned long)svga.updates);
    kprintf("    Batched:     %lu\n", (unsigned long)svga.batched_ops);
    kprintf("    Fallbacks:   %lu\n", (unsigned long)svga.sw_fallbacks);
    kprintf("    Transferred: %lu KB\n",
            (unsigned long)(svga.bytes_transferred / 1024));
}

/*============================================================================
 * Resolution Change
 *============================================================================*/

static int vmware_set_resolution(uint32_t width, uint32_t height)
{
    if (!svga.initialized) return -1;

    svga_write_reg(SVGA_REG_ENABLE, 0);
    svga_write_reg(SVGA_REG_WIDTH, width);
    svga_write_reg(SVGA_REG_HEIGHT, height);
    svga_write_reg(SVGA_REG_BITS_PER_PIXEL, 32);
    svga_write_reg(SVGA_REG_ENABLE, 1);

    svga.width = svga_read_reg(SVGA_REG_WIDTH);
    svga.height = svga_read_reg(SVGA_REG_HEIGHT);
    svga.pitch = svga_read_reg(SVGA_REG_BYTES_PER_LINE);

    kprintf("[VMware SVGA] Resolution changed to %ux%u\n", svga.width, svga.height);
    return 0;
}

/*============================================================================
 * HAL Registration
 *============================================================================*/

static struct gpu_ops vmware_svga_ops = {
    .name        = "VMware SVGA",
    .type        = GPU_BACKEND_VMWARE,
    .priority    = 60,
    .init        = vmware_svga_init,
    .available   = hal_vmware_available,
    .fill_rect   = hal_vmware_fill_rect,
    .clear       = hal_vmware_clear,
    .copy_region = hal_vmware_copy_region,
    .flip        = hal_vmware_flip,
    .set_resolution = vmware_set_resolution,
    .sync        = hal_vmware_sync,
    .wait        = hal_vmware_wait,
    .pending_ops = hal_vmware_pending,
    .get_stats   = hal_vmware_get_stats,
    .dump_info   = hal_vmware_dump_info,
};

void vmware_svga_register_hal(void)
{
    gpu_hal_register(&vmware_svga_ops);
}
