/*
 * PhantomOS Graphics Primitives
 * "To Create, Not To Destroy"
 *
 * Drawing functions, cursor sprite, and color utilities.
 */

#include "graphics.h"
#include "framebuffer.h"
#include "font.h"
#include <stdint.h>

/*============================================================================
 * External Declarations
 *============================================================================*/

extern void *memcpy(void *dest, const void *src, size_t n);

/*============================================================================
 * Line Drawing
 *============================================================================*/

void gfx_draw_hline(int x, int y, int w, uint32_t color)
{
    if (w <= 0) return;
    for (int i = 0; i < w; i++) {
        fb_put_pixel((uint32_t)(x + i), (uint32_t)y, color);
    }
}

void gfx_draw_vline(int x, int y, int h, uint32_t color)
{
    if (h <= 0) return;
    for (int i = 0; i < h; i++) {
        fb_put_pixel((uint32_t)x, (uint32_t)(y + i), color);
    }
}

void gfx_draw_line(int x1, int y1, int x2, int y2, uint32_t color)
{
    /* Bresenham's line algorithm */
    int dx = x2 - x1;
    int dy = y2 - y1;
    int sx = (dx > 0) ? 1 : -1;
    int sy = (dy > 0) ? 1 : -1;

    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;

    int err = dx - dy;

    while (1) {
        fb_put_pixel((uint32_t)x1, (uint32_t)y1, color);
        if (x1 == x2 && y1 == y2) break;

        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x1 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y1 += sy;
        }
    }
}

/*============================================================================
 * Text Drawing
 *============================================================================*/

void gfx_draw_text(int x, int y, const char *str, uint32_t fg, uint32_t bg)
{
    font_draw_string((uint32_t)x, (uint32_t)y, str, fg, bg);
}

/*============================================================================
 * Modern Visual Primitives
 *============================================================================*/

/* Font bitmap data from font.c */
extern const uint8_t font_data[95][16];

uint32_t gfx_alpha_blend(uint32_t fg, uint32_t bg, uint8_t alpha)
{
    uint32_t inv = 255 - alpha;
    uint32_t r = (((fg >> 16) & 0xFF) * alpha + ((bg >> 16) & 0xFF) * inv) >> 8;
    uint32_t g = (((fg >> 8) & 0xFF) * alpha + ((bg >> 8) & 0xFF) * inv) >> 8;
    uint32_t b = ((fg & 0xFF) * alpha + (bg & 0xFF) * inv) >> 8;
    return 0xFF000000 | (r << 16) | (g << 8) | b;
}

void gfx_fill_gradient_v(int x, int y, int w, int h,
                         uint32_t color_top, uint32_t color_bottom)
{
    uint32_t *backbuf = fb_get_backbuffer();
    uint32_t fb_w = fb_get_width();
    uint32_t fb_h = fb_get_height();

    if (!backbuf || w <= 0 || h <= 0) return;

    int x0 = (x < 0) ? 0 : x;
    int y0 = (y < 0) ? 0 : y;
    int x1 = (x + w > (int)fb_w) ? (int)fb_w : x + w;
    int y1 = (y + h > (int)fb_h) ? (int)fb_h : y + h;

    int rt = (color_top >> 16) & 0xFF, gt = (color_top >> 8) & 0xFF, bt = color_top & 0xFF;
    int rb = (color_bottom >> 16) & 0xFF, gb = (color_bottom >> 8) & 0xFF, bb = color_bottom & 0xFF;
    int denom = (h > 1) ? (h - 1) : 1;

    for (int row = y0; row < y1; row++) {
        int t = row - y;
        int r = rt + (rb - rt) * t / denom;
        int g = gt + (gb - gt) * t / denom;
        int b = bt + (bb - bt) * t / denom;
        uint32_t color = 0xFF000000 | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;

        uint32_t *dst = &backbuf[row * fb_w + x0];
        for (int col = x0; col < x1; col++)
            *dst++ = color;
    }
}

void gfx_fill_rounded_rect(int x, int y, int w, int h, int radius,
                           uint32_t color)
{
    uint32_t *backbuf = fb_get_backbuffer();
    uint32_t fb_w = fb_get_width();
    uint32_t fb_h = fb_get_height();

    if (!backbuf || w <= 0 || h <= 0) return;
    if (radius < 0) radius = 0;
    if (radius > w / 2) radius = w / 2;
    if (radius > h / 2) radius = h / 2;

    int r2 = radius * radius;

    for (int row = 0; row < h; row++) {
        int py = y + row;
        if (py < 0 || py >= (int)fb_h) continue;

        int x_start = x;
        int x_end = x + w;

        if (row < radius) {
            /* Top corners */
            int dy = radius - 1 - row;
            int inset = 0;
            for (int dx = radius - 1; dx >= 0; dx--) {
                if (dx * dx + dy * dy <= r2) { inset = dx; break; }
            }
            int skip = radius - 1 - inset;
            x_start = x + skip;
            x_end = x + w - skip;
        } else if (row >= h - radius) {
            /* Bottom corners */
            int dy = row - (h - radius);
            int inset = 0;
            for (int dx = radius - 1; dx >= 0; dx--) {
                if (dx * dx + dy * dy <= r2) { inset = dx; break; }
            }
            int skip = radius - 1 - inset;
            x_start = x + skip;
            x_end = x + w - skip;
        }

        if (x_start < 0) x_start = 0;
        if (x_end > (int)fb_w) x_end = (int)fb_w;

        uint32_t *dst = &backbuf[py * fb_w + x_start];
        for (int col = x_start; col < x_end; col++)
            *dst++ = color;
    }
}

void gfx_draw_rounded_rect(int x, int y, int w, int h, int radius,
                           uint32_t color)
{
    if (w <= 0 || h <= 0) return;
    if (radius < 0) radius = 0;
    if (radius > w / 2) radius = w / 2;
    if (radius > h / 2) radius = h / 2;

    /* Top and bottom edges (excluding corners) */
    gfx_draw_hline(x + radius, y, w - 2 * radius, color);
    gfx_draw_hline(x + radius, y + h - 1, w - 2 * radius, color);

    /* Left and right edges (excluding corners) */
    gfx_draw_vline(x, y + radius, h - 2 * radius, color);
    gfx_draw_vline(x + w - 1, y + radius, h - 2 * radius, color);

    /* Draw corner arcs using quarter-circle */
    int r2 = radius * radius;
    for (int dy = 0; dy < radius; dy++) {
        for (int dx = 0; dx < radius; dx++) {
            if (dx * dx + dy * dy <= r2 &&
                ((dx+1)*(dx+1) + dy*dy > r2 ||
                 dx*dx + (dy+1)*(dy+1) > r2)) {
                int cx = radius - 1 - dx;
                int cy = radius - 1 - dy;
                fb_put_pixel((uint32_t)(x + cx), (uint32_t)(y + cy), color);                     /* TL */
                fb_put_pixel((uint32_t)(x + w - 1 - cx), (uint32_t)(y + cy), color);             /* TR */
                fb_put_pixel((uint32_t)(x + cx), (uint32_t)(y + h - 1 - cy), color);             /* BL */
                fb_put_pixel((uint32_t)(x + w - 1 - cx), (uint32_t)(y + h - 1 - cy), color);    /* BR */
            }
        }
    }
}

void gfx_draw_shadow(int x, int y, int w, int h, int offset, uint8_t alpha)
{
    uint32_t *backbuf = fb_get_backbuffer();
    uint32_t fb_w = fb_get_width();
    uint32_t fb_h = fb_get_height();

    if (!backbuf || w <= 0 || h <= 0 || offset <= 0) return;

    int sx = x + offset;
    int sy = y + offset;
    int x0 = (sx < 0) ? 0 : sx;
    int y0 = (sy < 0) ? 0 : sy;
    int x1 = (sx + w > (int)fb_w) ? (int)fb_w : sx + w;
    int y1 = (sy + h > (int)fb_h) ? (int)fb_h : sy + h;

    for (int row = y0; row < y1; row++) {
        uint32_t *dst = &backbuf[row * fb_w + x0];
        for (int col = x0; col < x1; col++) {
            *dst = gfx_alpha_blend(COLOR_BLACK, *dst, alpha);
            dst++;
        }
    }
}

void gfx_draw_text_scaled(int x, int y, const char *str,
                          uint32_t fg, uint32_t bg, int scale)
{
    uint32_t *backbuf = fb_get_backbuffer();
    uint32_t fb_w = fb_get_width();
    uint32_t fb_h = fb_get_height();

    if (!backbuf || !str || scale < 1) return;

    int cx = x;
    while (*str) {
        int idx = (unsigned char)*str - 32;
        if (idx < 0 || idx >= 95) { idx = 0; }

        const uint8_t *glyph = font_data[idx];

        for (int row = 0; row < FONT_HEIGHT; row++) {
            uint8_t bits = glyph[row];
            for (int col = 0; col < FONT_WIDTH; col++) {
                uint32_t color = (bits & 0x80) ? fg : bg;
                bits <<= 1;

                for (int sy = 0; sy < scale; sy++) {
                    int py = y + row * scale + sy;
                    if (py < 0 || py >= (int)fb_h) continue;
                    for (int sx = 0; sx < scale; sx++) {
                        int px = cx + col * scale + sx;
                        if (px >= 0 && px < (int)fb_w)
                            backbuf[py * fb_w + px] = color;
                    }
                }
            }
        }
        cx += FONT_WIDTH * scale;
        str++;
    }
}

/*============================================================================
 * Mouse Cursor
 *
 * 12x19 arrow cursor sprite.
 * Legend: 'B' = black, 'W' = white, '.' = transparent
 *============================================================================*/

static const char cursor_data[CURSOR_HEIGHT][CURSOR_WIDTH] = {
    {'B','.','.','.','.','.','.','.','.','.','.','.'},
    {'B','B','.','.','.','.','.','.','.','.','.','.'},
    {'B','W','B','.','.','.','.','.','.','.','.','.'},
    {'B','W','W','B','.','.','.','.','.','.','.','.'},
    {'B','W','W','W','B','.','.','.','.','.','.','.'},
    {'B','W','W','W','W','B','.','.','.','.','.','.'},
    {'B','W','W','W','W','W','B','.','.','.','.','.'},
    {'B','W','W','W','W','W','W','B','.','.','.','.'},
    {'B','W','W','W','W','W','W','W','B','.','.','.'},
    {'B','W','W','W','W','W','W','W','W','B','.','.'},
    {'B','W','W','W','W','W','W','W','W','W','B','.'},
    {'B','W','W','W','W','W','W','W','W','W','W','B'},
    {'B','W','W','W','W','W','W','B','B','B','B','B'},
    {'B','W','W','W','B','W','W','B','.','.','.','.'},
    {'B','W','W','B','.','B','W','W','B','.','.','.'},
    {'B','W','B','.','.','B','W','W','B','.','.','.'},
    {'B','B','.','.','.','.','B','W','W','B','.','.'},
    {'B','.','.','.','.','.','B','W','W','B','.','.'},
    {'.','.','.','.','.','.','.','B','B','.','.','.'},
};

/* Saved pixels under cursor */
static uint32_t saved_pixels[CURSOR_WIDTH * CURSOR_HEIGHT];
static int saved_x = -1;
static int saved_y = -1;
static int saved_valid = 0;

void gfx_save_under_cursor(int x, int y)
{
    saved_x = x;
    saved_y = y;
    saved_valid = 1;

    for (int row = 0; row < CURSOR_HEIGHT; row++) {
        for (int col = 0; col < CURSOR_WIDTH; col++) {
            saved_pixels[row * CURSOR_WIDTH + col] =
                fb_get_pixel((uint32_t)(x + col), (uint32_t)(y + row));
        }
    }
}

void gfx_restore_under_cursor(void)
{
    if (!saved_valid) return;

    for (int row = 0; row < CURSOR_HEIGHT; row++) {
        for (int col = 0; col < CURSOR_WIDTH; col++) {
            fb_put_pixel((uint32_t)(saved_x + col), (uint32_t)(saved_y + row),
                        saved_pixels[row * CURSOR_WIDTH + col]);
        }
    }

    saved_valid = 0;
}

void gfx_draw_cursor(int x, int y)
{
    for (int row = 0; row < CURSOR_HEIGHT; row++) {
        for (int col = 0; col < CURSOR_WIDTH; col++) {
            char c = cursor_data[row][col];
            if (c == 'B') {
                fb_put_pixel((uint32_t)(x + col), (uint32_t)(y + row),
                            COLOR_BLACK);
            } else if (c == 'W') {
                fb_put_pixel((uint32_t)(x + col), (uint32_t)(y + row),
                            COLOR_WHITE);
            }
            /* '.' = transparent, don't draw */
        }
    }
}
