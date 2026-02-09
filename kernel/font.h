/*
 * PhantomOS Bitmap Font
 * "To Create, Not To Destroy"
 *
 * 8x16 VGA-style bitmap font for framebuffer text rendering.
 */

#ifndef PHANTOMOS_FONT_H
#define PHANTOMOS_FONT_H

#include <stdint.h>

#define FONT_WIDTH      8
#define FONT_HEIGHT     16

/*
 * Draw a single character onto the framebuffer backbuffer
 *
 * @x:  Pixel X coordinate
 * @y:  Pixel Y coordinate
 * @ch: ASCII character to draw
 * @fg: Foreground color (0xAARRGGBB)
 * @bg: Background color (0xAARRGGBB)
 */
void font_draw_char(uint32_t x, uint32_t y, char ch, uint32_t fg, uint32_t bg);

/*
 * Draw a null-terminated string onto the framebuffer backbuffer
 *
 * @x:   Pixel X coordinate
 * @y:   Pixel Y coordinate
 * @str: String to draw
 * @fg:  Foreground color
 * @bg:  Background color
 */
void font_draw_string(uint32_t x, uint32_t y, const char *str,
                      uint32_t fg, uint32_t bg);

#endif /* PHANTOMOS_FONT_H */
