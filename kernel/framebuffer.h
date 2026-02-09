/*
 * PhantomOS Framebuffer Driver
 * "To Create, Not To Destroy"
 *
 * Linear framebuffer driver for graphical display.
 * Initialized from multiboot2 framebuffer info provided by GRUB.
 */

#ifndef PHANTOMOS_FRAMEBUFFER_H
#define PHANTOMOS_FRAMEBUFFER_H

#include <stdint.h>
#include <stddef.h>

/*============================================================================
 * Constants
 *============================================================================*/

#define FB_DEFAULT_WIDTH    1024
#define FB_DEFAULT_HEIGHT   768
#define FB_DEFAULT_BPP      32

/*============================================================================
 * Color Helpers (32-bit ARGB)
 *============================================================================*/

#define FB_RGB(r, g, b)         (0xFF000000 | ((r) << 16) | ((g) << 8) | (b))
#define FB_RGBA(r, g, b, a)     (((a) << 24) | ((r) << 16) | ((g) << 8) | (b))

#define FB_COLOR_BLACK          0xFF000000
#define FB_COLOR_WHITE          0xFFFFFFFF
#define FB_COLOR_RED            0xFFFF0000
#define FB_COLOR_GREEN          0xFF00FF00
#define FB_COLOR_BLUE           0xFF0000FF

/*============================================================================
 * Framebuffer Info Structure
 *============================================================================*/

struct framebuffer_info {
    uint64_t    phys_addr;      /* Physical address of framebuffer MMIO */
    uint32_t   *base;           /* Mapped framebuffer address */
    uint32_t   *backbuffer;     /* Double buffer in kernel heap */
    uint32_t    width;          /* Width in pixels */
    uint32_t    height;         /* Height in pixels */
    uint32_t    pitch;          /* Bytes per scanline */
    uint32_t    bpp;            /* Bits per pixel */
    uint32_t    size;           /* Total framebuffer size in bytes */
    int         initialized;    /* Is framebuffer ready? */
};

/*============================================================================
 * Dirty Rectangle Tracking (VM optimization)
 *============================================================================*/

#define FB_TILE_SIZE        32      /* Tile size in pixels (must be power of 2) */
#define FB_MAX_WIDTH        1280    /* Maximum supported width */
#define FB_MAX_HEIGHT       1024    /* Maximum supported height */
#define FB_TILE_COLS_MAX    (FB_MAX_WIDTH / FB_TILE_SIZE)    /* 40 */
#define FB_TILE_ROWS_MAX    (FB_MAX_HEIGHT / FB_TILE_SIZE)   /* 32 */
#define FB_DIRTY_BYTES      ((FB_TILE_COLS_MAX * FB_TILE_ROWS_MAX + 7) / 8)  /* 160 */

/*============================================================================
 * API Functions
 *============================================================================*/

/*
 * Initialize the framebuffer
 * Maps the MMIO region and allocates backbuffer
 */
int fb_init(uint64_t phys_addr, uint32_t width, uint32_t height,
            uint32_t pitch, uint32_t bpp);

/*
 * Check if framebuffer is initialized
 */
int fb_is_initialized(void);

/*
 * Get framebuffer info
 */
const struct framebuffer_info *fb_get_info(void);

/*
 * Put a single pixel (to backbuffer)
 */
void fb_put_pixel(uint32_t x, uint32_t y, uint32_t color);

/*
 * Get pixel from backbuffer
 */
uint32_t fb_get_pixel(uint32_t x, uint32_t y);

/*
 * Fill a rectangle with a solid color
 */
void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                  uint32_t color);

/*
 * Draw a rectangle outline
 */
void fb_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                  uint32_t color);

/*
 * Copy a pixel buffer to the backbuffer
 */
void fb_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
             const uint32_t *buffer);

/*
 * Clear the entire framebuffer to a color
 */
void fb_clear(uint32_t color);

/*
 * Wait for vertical blanking interval (VSync)
 * Uses VGA Input Status Register 1 (port 0x3DA) bit 3.
 * Call before fb_flip() to prevent tearing on real hardware.
 */
void fb_wait_vsync(void);

/*
 * Flip: copy backbuffer to the actual framebuffer (MMIO)
 */
void fb_flip(void);

/*
 * Copy a region within the backbuffer (for WM window moves/scrolls)
 * GPU-accelerated when available, otherwise uses memmove
 */
void fb_copy_region(uint32_t dst_x, uint32_t dst_y,
                    uint32_t src_x, uint32_t src_y,
                    uint32_t w, uint32_t h);

/*
 * Get direct pointer to backbuffer for fast rendering
 */
uint32_t *fb_get_backbuffer(void);

/*
 * Get framebuffer dimensions
 */
uint32_t fb_get_width(void);
uint32_t fb_get_height(void);

/*============================================================================
 * Resolution Management
 *============================================================================*/

struct fb_resolution {
    uint32_t    width;
    uint32_t    height;
    const char *label;      /* "1024x768" */
};

/* Resize framebuffer (changes GPU mode + reallocates backbuffer) */
int fb_resize(uint32_t new_w, uint32_t new_h);

/* Get number of supported resolutions */
int fb_get_resolution_count(void);

/* Get resolution by index */
const struct fb_resolution *fb_get_resolution(int idx);

/*============================================================================
 * VM Optimization: Dirty Tracking & Frame Timing
 *============================================================================*/

/* Enable/disable dirty rectangle tracking */
void fb_set_dirty_tracking(int enable);

/* Mark a pixel region as dirty (called by drawing functions) */
void fb_mark_dirty(uint32_t x, uint32_t y, uint32_t w, uint32_t h);

/* Mark entire screen dirty */
void fb_mark_all_dirty(void);

/* Check if any tiles are dirty */
int fb_has_dirty(void);

/* Enable VM mode (dirty tracking + timer-based frame limiting) */
void fb_set_vm_mode(int enable);

/* VM-aware frame wait (timer in VM, VSync on bare metal) */
void fb_frame_wait(void);

#endif /* PHANTOMOS_FRAMEBUFFER_H */
