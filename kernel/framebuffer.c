/*
 * PhantomOS Framebuffer Driver
 * "To Create, Not To Destroy"
 *
 * Linear framebuffer implementation with double buffering.
 */

#include "framebuffer.h"
#include "gpu_hal.h"
#include "timer.h"
#include "io.h"
#include "vmm.h"
#include "heap.h"
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
 * Resolution Table
 *============================================================================*/

static const struct fb_resolution resolutions[] = {
    {  800,  600, "800x600"   },
    { 1024,  768, "1024x768"  },
    { 1280,  720, "1280x720"  },
    { 1280, 1024, "1280x1024" },
};

#define NUM_RESOLUTIONS  (int)(sizeof(resolutions) / sizeof(resolutions[0]))

/*============================================================================
 * Framebuffer State
 *============================================================================*/

static struct framebuffer_info fb;

/* Runtime tile counts (depend on current resolution) */
#define FB_TILE_COLS  ((fb.width + FB_TILE_SIZE - 1) / FB_TILE_SIZE)
#define FB_TILE_ROWS  ((fb.height + FB_TILE_SIZE - 1) / FB_TILE_SIZE)

/*============================================================================
 * Dirty Rectangle Tracking State
 *============================================================================*/

static uint8_t dirty_bitmap[FB_DIRTY_BYTES];
static int dirty_tracking_enabled = 0;
static int vm_mode_enabled = 0;
static uint64_t last_flip_tick = 0;

/* ~33fps at 100Hz PIT (3 ticks = 30ms) */
#define VM_FRAME_TICKS  3

static inline void mark_tile_dirty(uint32_t tx, uint32_t ty)
{
    uint32_t idx = ty * FB_TILE_COLS + tx;
    dirty_bitmap[idx / 8] |= (1U << (idx % 8));
}

static inline int is_tile_dirty(uint32_t tx, uint32_t ty)
{
    uint32_t idx = ty * FB_TILE_COLS + tx;
    return (dirty_bitmap[idx / 8] >> (idx % 8)) & 1;
}

/* Forward declaration */
static void fb_flip_dirty(void);

/*============================================================================
 * Initialization
 *============================================================================*/

int fb_init(uint64_t phys_addr, uint32_t width, uint32_t height,
            uint32_t pitch, uint32_t bpp)
{
    if (bpp != 32) {
        kprintf("[FB] Error: Only 32bpp supported, got %u\n", bpp);
        return -1;
    }

    fb.phys_addr = phys_addr;
    fb.width = width;
    fb.height = height;
    fb.pitch = pitch;
    fb.bpp = bpp;
    fb.size = pitch * height;

    kprintf("[FB] Framebuffer: %ux%u %ubpp at 0x%lx (%lu KB)\n",
            width, height, bpp, (unsigned long)phys_addr,
            (unsigned long)(fb.size / 1024));

    /* Map framebuffer MMIO pages into virtual address space
     * The framebuffer is typically at a high physical address (e.g., 0xFD000000)
     * which is above our 1GB identity mapping. We need to explicitly map it. */
    uint64_t fb_pages = (fb.size + 4095) / 4096;
    for (uint64_t i = 0; i < fb_pages; i++) {
        uint64_t addr = phys_addr + i * 4096;
        /* Map with write-combining for better performance */
        int ret = vmm_map_page(addr, addr,
                               PTE_PRESENT | PTE_WRITABLE | PTE_NOCACHE | PTE_WRITETHROUGH);
        if (ret != 0) {
            kprintf("[FB] Warning: Failed to map page at 0x%lx\n",
                    (unsigned long)addr);
        }
    }

    fb.base = (uint32_t *)phys_addr;

    /* Allocate backbuffer in kernel heap */
    uint32_t bb_size = width * height * 4;  /* 32bpp = 4 bytes */
    fb.backbuffer = (uint32_t *)kmalloc(bb_size);
    if (!fb.backbuffer) {
        kprintf("[FB] Error: Cannot allocate backbuffer (%lu bytes)\n",
                (unsigned long)bb_size);
        return -1;
    }

    /* Clear both buffers to black */
    memset(fb.backbuffer, 0, bb_size);
    memset(fb.base, 0, fb.size);

    fb.initialized = 1;
    kprintf("[FB] Initialized: %ux%u backbuffer at 0x%lx\n",
            width, height, (unsigned long)(uintptr_t)fb.backbuffer);

    return 0;
}

int fb_is_initialized(void)
{
    return fb.initialized;
}

const struct framebuffer_info *fb_get_info(void)
{
    return &fb;
}

/*============================================================================
 * Pixel Operations (all work on backbuffer)
 *============================================================================*/

void fb_put_pixel(uint32_t x, uint32_t y, uint32_t color)
{
    if (x < fb.width && y < fb.height) {
        fb.backbuffer[y * fb.width + x] = color;
        if (dirty_tracking_enabled)
            mark_tile_dirty(x / FB_TILE_SIZE, y / FB_TILE_SIZE);
    }
}

uint32_t fb_get_pixel(uint32_t x, uint32_t y)
{
    if (x < fb.width && y < fb.height) {
        return fb.backbuffer[y * fb.width + x];
    }
    return 0;
}

void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                  uint32_t color)
{
    /* Clip to screen bounds */
    if (x >= fb.width || y >= fb.height) return;
    if (x + w > fb.width) w = fb.width - x;
    if (y + h > fb.height) h = fb.height - y;

    if (dirty_tracking_enabled)
        fb_mark_dirty(x, y, w, h);

    /* Try GPU-accelerated fill (batched, no wait) */
    if (gpu_hal_available()) {
        if (gpu_hal_fill_rect(x, y, w, h, color) == 0) {
            return;  /* Queued; will sync at fb_flip() */
        }
    }

    for (uint32_t row = 0; row < h; row++) {
        uint32_t *dst = &fb.backbuffer[(y + row) * fb.width + x];
        for (uint32_t col = 0; col < w; col++) {
            dst[col] = color;
        }
    }
}

void fb_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                  uint32_t color)
{
    /* Top and bottom edges */
    for (uint32_t i = 0; i < w; i++) {
        fb_put_pixel(x + i, y, color);
        fb_put_pixel(x + i, y + h - 1, color);
    }
    /* Left and right edges */
    for (uint32_t i = 0; i < h; i++) {
        fb_put_pixel(x, y + i, color);
        fb_put_pixel(x + w - 1, y + i, color);
    }
}

void fb_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
             const uint32_t *buffer)
{
    if (!buffer) return;

    if (dirty_tracking_enabled) {
        uint32_t clip_w = (x + w > fb.width) ? fb.width - x : w;
        uint32_t clip_h = (y + h > fb.height) ? fb.height - y : h;
        fb_mark_dirty(x, y, clip_w, clip_h);
    }

    for (uint32_t row = 0; row < h; row++) {
        uint32_t dy = y + row;
        if (dy >= fb.height) break;

        uint32_t copy_w = w;
        if (x + copy_w > fb.width) copy_w = fb.width - x;

        memcpy(&fb.backbuffer[dy * fb.width + x],
               &buffer[row * w],
               copy_w * 4);
    }
}

void fb_clear(uint32_t color)
{
    if (dirty_tracking_enabled)
        fb_mark_all_dirty();

    /* Try GPU-accelerated full-screen fill */
    if (gpu_hal_available()) {
        if (gpu_hal_clear(color) == 0) {
            return;  /* Queued; will sync at fb_flip() */
        }
    }

    uint32_t total = fb.width * fb.height;
    for (uint32_t i = 0; i < total; i++) {
        fb.backbuffer[i] = color;
    }
}

/*============================================================================
 * Region Copy (for WM window dragging/scrolling)
 *============================================================================*/

void fb_copy_region(uint32_t dst_x, uint32_t dst_y,
                    uint32_t src_x, uint32_t src_y,
                    uint32_t w, uint32_t h)
{
    if (w == 0 || h == 0) return;

    /* Clip to screen bounds */
    if (dst_x >= fb.width || dst_y >= fb.height) return;
    if (src_x >= fb.width || src_y >= fb.height) return;
    if (dst_x + w > fb.width)  w = fb.width - dst_x;
    if (dst_y + h > fb.height) h = fb.height - dst_y;
    if (src_x + w > fb.width)  w = fb.width - src_x;
    if (src_y + h > fb.height) h = fb.height - src_y;

    if (dirty_tracking_enabled)
        fb_mark_dirty(dst_x, dst_y, w, h);

    /* Try GPU-accelerated screen-to-screen copy */
    if (gpu_hal_available()) {
        if (gpu_hal_copy_region(dst_x, dst_y, src_x, src_y, w, h) == 0) {
            return;
        }
    }

    /* Software fallback with memmove (handles overlap) */
    if (dst_y < src_y || (dst_y == src_y && dst_x < src_x)) {
        /* Copy top-to-bottom */
        for (uint32_t row = 0; row < h; row++) {
            uint32_t *dst = &fb.backbuffer[(dst_y + row) * fb.width + dst_x];
            uint32_t *src = &fb.backbuffer[(src_y + row) * fb.width + src_x];
            memmove(dst, src, w * 4);
        }
    } else {
        /* Copy bottom-to-top for overlapping downward moves */
        for (uint32_t row = h; row > 0; row--) {
            uint32_t *dst = &fb.backbuffer[(dst_y + row - 1) * fb.width + dst_x];
            uint32_t *src = &fb.backbuffer[(src_y + row - 1) * fb.width + src_x];
            memmove(dst, src, w * 4);
        }
    }
}

/*============================================================================
 * VSync
 *============================================================================*/

/* VGA Input Status Register 1 */
#define VGA_ISR1            0x3DA
#define VGA_ISR1_VRETRACE   (1 << 3)

void fb_wait_vsync(void)
{
    /* Wait for any current retrace to end */
    while (inb(VGA_ISR1) & VGA_ISR1_VRETRACE)
        ;
    /* Wait for the next retrace to begin */
    while (!(inb(VGA_ISR1) & VGA_ISR1_VRETRACE))
        ;
}

/*============================================================================
 * Display
 *============================================================================*/

void fb_flip(void)
{
    if (!fb.initialized) return;

    /* VM-optimized path: only copy dirty tiles */
    if (dirty_tracking_enabled) {
        fb_flip_dirty();
        return;
    }

    /* Try GPU-accelerated copy (backbuffer -> framebuffer) */
    if (gpu_hal_available()) {
        if (gpu_hal_flip() == 0) {
            gpu_hal_wait();  /* Wait for the flip copy itself */
            return;
        }
    }

    /* Software path: sync any pending GPU ops before CPU reads backbuffer */
    gpu_hal_sync();

    /* Software fallback: copy backbuffer to MMIO framebuffer */
    uint32_t row_bytes = fb.width * 4;

    if (fb.pitch == row_bytes) {
        /* Pitch matches width: single bulk copy (fastest path) */
        memcpy(fb.base, fb.backbuffer, row_bytes * fb.height);
    } else {
        /* Pitch differs from width: row-by-row copy */
        uint8_t *src = (uint8_t *)fb.backbuffer;
        uint8_t *dst = (uint8_t *)fb.base;

        for (uint32_t row = 0; row < fb.height; row++) {
            memcpy(dst, src, row_bytes);
            src += row_bytes;
            dst += fb.pitch;
        }
    }
}

/*============================================================================
 * Dirty Rectangle Tracking
 *============================================================================*/

void fb_set_dirty_tracking(int enable)
{
    dirty_tracking_enabled = enable;
    if (enable)
        memset(dirty_bitmap, 0xFF, sizeof(dirty_bitmap)); /* Mark all dirty initially */
}

void fb_mark_dirty(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    if (!dirty_tracking_enabled || w == 0 || h == 0) return;

    uint32_t tx0 = x / FB_TILE_SIZE;
    uint32_t ty0 = y / FB_TILE_SIZE;
    uint32_t tx1 = (x + w - 1) / FB_TILE_SIZE;
    uint32_t ty1 = (y + h - 1) / FB_TILE_SIZE;

    if (tx1 >= FB_TILE_COLS) tx1 = FB_TILE_COLS - 1;
    if (ty1 >= FB_TILE_ROWS) ty1 = FB_TILE_ROWS - 1;

    for (uint32_t ty = ty0; ty <= ty1; ty++)
        for (uint32_t tx = tx0; tx <= tx1; tx++)
            mark_tile_dirty(tx, ty);
}

void fb_mark_all_dirty(void)
{
    if (dirty_tracking_enabled)
        memset(dirty_bitmap, 0xFF, sizeof(dirty_bitmap));
}

int fb_has_dirty(void)
{
    for (uint32_t i = 0; i < FB_DIRTY_BYTES; i++)
        if (dirty_bitmap[i])
            return 1;
    return 0;
}

static void fb_flip_dirty(void)
{
    if (!fb.initialized) return;

    /* Sync any pending GPU ops */
    gpu_hal_sync();

    for (uint32_t ty = 0; ty < FB_TILE_ROWS; ty++) {
        for (uint32_t tx = 0; tx < FB_TILE_COLS; tx++) {
            if (!is_tile_dirty(tx, ty))
                continue;

            uint32_t px = tx * FB_TILE_SIZE;
            uint32_t py = ty * FB_TILE_SIZE;

            /* Clamp tile to screen bounds (partial tiles at edges) */
            uint32_t tw = FB_TILE_SIZE;
            uint32_t th = FB_TILE_SIZE;
            if (px + tw > fb.width)  tw = fb.width - px;
            if (py + th > fb.height) th = fb.height - py;

            /* Copy this tile row-by-row from backbuffer to MMIO */
            for (uint32_t row = 0; row < th; row++) {
                uint32_t *src = &fb.backbuffer[(py + row) * fb.width + px];
                uint8_t *dst = (uint8_t *)fb.base + (py + row) * fb.pitch + px * 4;
                memcpy(dst, src, tw * 4);
            }
        }
    }

    /* Clear dirty bitmap for next frame */
    memset(dirty_bitmap, 0, sizeof(dirty_bitmap));
}

/*============================================================================
 * VM Mode: Timer-Based Frame Limiting
 *============================================================================*/

void fb_set_vm_mode(int enable)
{
    vm_mode_enabled = enable;
    if (enable) {
        dirty_tracking_enabled = 1;
        memset(dirty_bitmap, 0xFF, sizeof(dirty_bitmap)); /* First frame: full redraw */
        last_flip_tick = timer_get_ticks();
        kprintf("[FB] VM mode enabled: dirty tracking + timer frame limiting\n");
    }
}

void fb_frame_wait(void)
{
    if (vm_mode_enabled) {
        /* Timer-based frame limiting for VMs (~33fps) */
        uint64_t now = timer_get_ticks();
        while (now - last_flip_tick < VM_FRAME_TICKS) {
            __asm__ volatile("hlt");
            now = timer_get_ticks();
        }
        last_flip_tick = now;
    } else {
        /* Bare metal: use VGA VSync */
        fb_wait_vsync();
    }
}

/*============================================================================
 * Resolution Management
 *============================================================================*/

int fb_get_resolution_count(void)
{
    return NUM_RESOLUTIONS;
}

const struct fb_resolution *fb_get_resolution(int idx)
{
    if (idx < 0 || idx >= NUM_RESOLUTIONS)
        return (void *)0;
    return &resolutions[idx];
}

int fb_resize(uint32_t new_w, uint32_t new_h)
{
    if (!fb.initialized) return -1;
    if (new_w == fb.width && new_h == fb.height) return 0;  /* Already at this resolution */
    if (new_w > FB_MAX_WIDTH || new_h > FB_MAX_HEIGHT) return -1;

    kprintf("[FB] Resizing: %ux%u -> %ux%u\n", fb.width, fb.height, new_w, new_h);

    /* Ask GPU backend to change mode */
    int ret = gpu_hal_set_resolution(new_w, new_h);
    if (ret != 0) {
        kprintf("[FB] GPU backend failed to set resolution\n");
        return -1;
    }

    /* Free old backbuffer */
    if (fb.backbuffer) {
        kfree(fb.backbuffer);
        fb.backbuffer = (void *)0;
    }

    /* Update framebuffer info */
    uint32_t old_w = fb.width;
    uint32_t old_h = fb.height;
    fb.width = new_w;
    fb.height = new_h;
    fb.pitch = new_w * 4;  /* 32bpp */
    fb.size = fb.pitch * new_h;

    /* Allocate new backbuffer */
    uint32_t bb_size = new_w * new_h * 4;
    fb.backbuffer = (uint32_t *)kmalloc(bb_size);
    if (!fb.backbuffer) {
        kprintf("[FB] Error: Cannot allocate new backbuffer (%lu bytes)\n",
                (unsigned long)bb_size);
        /* Restore old dimensions */
        fb.width = old_w;
        fb.height = old_h;
        fb.pitch = old_w * 4;
        fb.size = fb.pitch * old_h;
        fb.backbuffer = (uint32_t *)kmalloc(old_w * old_h * 4);
        return -1;
    }

    /* Clear new backbuffer */
    memset(fb.backbuffer, 0, bb_size);

    /* Reset dirty tracking for new resolution */
    memset(dirty_bitmap, 0xFF, sizeof(dirty_bitmap));

    kprintf("[FB] Resized to %ux%u, backbuffer at 0x%lx\n",
            new_w, new_h, (unsigned long)(uintptr_t)fb.backbuffer);

    return 0;
}

/*============================================================================
 * Getters
 *============================================================================*/

uint32_t *fb_get_backbuffer(void)
{
    return fb.backbuffer;
}

uint32_t fb_get_width(void)
{
    return fb.width;
}

uint32_t fb_get_height(void)
{
    return fb.height;
}
