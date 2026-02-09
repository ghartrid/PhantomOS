/*
 * PhantomOS Framebuffer Console
 * "To Create, Not To Destroy"
 *
 * Renders text on the framebuffer using the bitmap font.
 * Provides scrolling, cursor tracking, and integrates with kprintf.
 */

#include "fbcon.h"
#include "framebuffer.h"
#include "font.h"
#include <stdint.h>

/*============================================================================
 * External Declarations
 *============================================================================*/

extern void *memcpy(void *dest, const void *src, size_t n);
extern void *memset(void *s, int c, size_t n);

/*============================================================================
 * Console State
 *============================================================================*/

/* Colors */
#define FBCON_FG    0xFFEEEEEE      /* Light gray text */
#define FBCON_BG    0xFF000000      /* Black background */

static uint32_t cols;           /* Characters per row */
static uint32_t rows;           /* Character rows */
static uint32_t cursor_x;      /* Current column (0-based) */
static uint32_t cursor_y;      /* Current row (0-based) */
static int      active;        /* Console initialized? */

/*============================================================================
 * Implementation
 *============================================================================*/

void fbcon_init(void)
{
    if (!fb_is_initialized())
        return;

    cols = fb_get_width() / FONT_WIDTH;     /* 1024/8 = 128 */
    rows = fb_get_height() / FONT_HEIGHT;   /* 768/16 = 48 */
    cursor_x = 0;
    cursor_y = 0;
    active = 1;

    /* Clear screen to black */
    fb_clear(FBCON_BG);
    fb_flip();
}

int fbcon_is_active(void)
{
    return active;
}

/*
 * Scroll the console up by one line
 */
static void fbcon_scroll(void)
{
    uint32_t *backbuf = fb_get_backbuffer();
    uint32_t fb_w = fb_get_width();
    uint32_t fb_h = fb_get_height();

    if (!backbuf) return;

    /* Move everything up by FONT_HEIGHT pixels */
    uint32_t line_pixels = FONT_HEIGHT * fb_w;
    uint32_t total_pixels = fb_w * fb_h;

    memcpy(backbuf, backbuf + line_pixels,
           (total_pixels - line_pixels) * 4);

    /* Clear the last line */
    memset(backbuf + (total_pixels - line_pixels), 0,
           line_pixels * 4);
}

static void fbcon_newline(void)
{
    cursor_x = 0;
    cursor_y++;
    if (cursor_y >= rows) {
        cursor_y = rows - 1;
        fbcon_scroll();
    }
}

void fbcon_putchar(char c)
{
    if (!active) return;

    switch (c) {
    case '\n':
        fbcon_newline();
        break;

    case '\r':
        cursor_x = 0;
        break;

    case '\t':
        /* Tab to next 8-column stop */
        cursor_x = (cursor_x + 8) & ~7;
        if (cursor_x >= cols)
            fbcon_newline();
        break;

    case '\b':
        if (cursor_x > 0) {
            cursor_x--;
            font_draw_char(cursor_x * FONT_WIDTH, cursor_y * FONT_HEIGHT,
                          ' ', FBCON_FG, FBCON_BG);
        }
        break;

    default:
        if ((unsigned char)c >= 32 && (unsigned char)c <= 126) {
            font_draw_char(cursor_x * FONT_WIDTH, cursor_y * FONT_HEIGHT,
                          c, FBCON_FG, FBCON_BG);
            cursor_x++;
            if (cursor_x >= cols)
                fbcon_newline();
        }
        break;
    }

    /* Flip to screen on newlines for reasonable performance during boot.
     * Individual characters are batched until a newline triggers the flip. */
    if (c == '\n') {
        fb_flip();
    }
}

void fbcon_clear(void)
{
    if (!active) return;

    fb_clear(FBCON_BG);
    fb_flip();
    cursor_x = 0;
    cursor_y = 0;
}

void fbcon_disable(void)
{
    active = 0;
}
