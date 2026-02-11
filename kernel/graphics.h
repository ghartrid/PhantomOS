/*
 * PhantomOS Graphics Primitives
 * "To Create, Not To Destroy"
 *
 * Drawing functions and color constants for the GUI.
 */

#ifndef PHANTOMOS_GRAPHICS_H
#define PHANTOMOS_GRAPHICS_H

#include <stdint.h>

/*============================================================================
 * PhantomOS Color Palette (0xAARRGGBB)
 *============================================================================*/

#define COLOR_BG_DARK       0xFF1A1A2E      /* Dark blue-black desktop */
#define COLOR_BG_PANEL      0xFF16213E      /* Panel background */
#define COLOR_ACCENT        0xFF0F3460      /* Window title bar */
#define COLOR_HIGHLIGHT     0xFFE94560      /* Active/hover accent */
#define COLOR_TEXT           0xFFEEEEEE      /* Primary text */
#define COLOR_TEXT_DIM       0xFF888888      /* Secondary text */
#define COLOR_BORDER         0xFF333355      /* Window borders */
#define COLOR_TASKBAR        0xFF0A0A1A      /* Taskbar background */
#define COLOR_WHITE          0xFFFFFFFF
#define COLOR_BLACK          0xFF000000
#define COLOR_BUTTON         0xFF1E3A5F      /* Button face */
#define COLOR_BUTTON_HOVER   0xFF2A4A6F      /* Button hover */
#define COLOR_TITLE_FOCUS    0xFF0F3460      /* Focused window title */
#define COLOR_TITLE_UNFOCUS  0xFF0A0A1A      /* Unfocused window title */
#define COLOR_CLOSE_BTN      0xFFE94560      /* Close button */
#define COLOR_CLOSE_HOVER    0xFFFF5577      /* Close button hover */

/* Desktop panel layout colors */
#define COLOR_HEADER_BG      0xFF0D1117      /* Header bar */
#define COLOR_MENUBAR_BG     0xFF111827      /* Menu bar */
#define COLOR_SIDEBAR_BG     0xFF0F1218      /* Left sidebar */
#define COLOR_CONTENT_BG     0xFF0A0E1A      /* Center content area */
#define COLOR_PANEL_BG       0xFF111827      /* Right panel */
#define COLOR_DOCK_BG        0xFF0D1117      /* Bottom dock */
#define COLOR_STATUS_BG      0xFF0A0E1A      /* Status bar */
#define COLOR_PANEL_BORDER   0xFF1E293B      /* Panel borders */
#define COLOR_SIDEBAR_SEL    0xFF1E293B      /* Sidebar selected item */
#define COLOR_GREEN_ACTIVE   0xFF22C55E      /* Active/Low status */
#define COLOR_INPUT_BG       0xFF1E293B      /* Input field background */
#define COLOR_BUTTON_PRIMARY 0xFF2563EB      /* Primary action button */
#define COLOR_ICON_YELLOW    0xFFEAB308      /* File icon */
#define COLOR_ICON_GREEN     0xFF22C55E      /* Terminal icon */
#define COLOR_ICON_PURPLE    0xFF8B5CF6      /* AI/ArtOS icon */
#define COLOR_ICON_GRAY      0xFF6B7280      /* Settings icon */
#define COLOR_ICON_ORANGE    0xFFF97316      /* Security icon */

/*============================================================================
 * Drawing Primitives
 *============================================================================*/

/* Line drawing (Bresenham's algorithm) */
void gfx_draw_line(int x1, int y1, int x2, int y2, uint32_t color);

/* Fast horizontal and vertical lines */
void gfx_draw_hline(int x, int y, int w, uint32_t color);
void gfx_draw_vline(int x, int y, int h, uint32_t color);

/* Text rendering (wraps font module) */
void gfx_draw_text(int x, int y, const char *str, uint32_t fg, uint32_t bg);

/*============================================================================
 * Modern Visual Primitives
 *============================================================================*/

/* Alpha-blend foreground over background. alpha: 0=transparent, 255=opaque */
uint32_t gfx_alpha_blend(uint32_t fg, uint32_t bg, uint8_t alpha);

/* Fill a rectangle with a vertical gradient (top color to bottom color) */
void gfx_fill_gradient_v(int x, int y, int w, int h,
                         uint32_t color_top, uint32_t color_bottom);

/* Fill a rectangle with rounded corners */
void gfx_fill_rounded_rect(int x, int y, int w, int h, int radius,
                           uint32_t color);

/* Draw a rounded rectangle outline */
void gfx_draw_rounded_rect(int x, int y, int w, int h, int radius,
                           uint32_t color);

/* Draw a drop shadow behind a rectangle (reads+blends existing pixels) */
void gfx_draw_shadow(int x, int y, int w, int h, int offset, uint8_t alpha);

/* Draw a soft multi-layer shadow with rounded corners (5 layers, diffused) */
void gfx_draw_soft_shadow(int x, int y, int w, int h, int radius);

/* Fill a rounded rectangle with anti-aliased corners */
void gfx_fill_rounded_rect_aa(int x, int y, int w, int h, int radius,
                               uint32_t color);

/* Fill a radial-style gradient (Manhattan distance approximation) */
void gfx_fill_gradient_radial(int x, int y, int w, int h,
                               int cx, int cy,
                               uint32_t color_center, uint32_t color_edge);

/* Draw text at integer scale (2 = each pixel becomes 2x2 block) */
void gfx_draw_text_scaled(int x, int y, const char *str,
                          uint32_t fg, uint32_t bg, int scale);

/*============================================================================
 * Mouse Cursor
 *============================================================================*/

#define CURSOR_WIDTH    14
#define CURSOR_HEIGHT   21

/* Draw the mouse cursor at the given position */
void gfx_draw_cursor(int x, int y);

/* Save the pixels under the cursor before drawing it */
void gfx_save_under_cursor(int x, int y);

/* Restore the pixels that were under the cursor */
void gfx_restore_under_cursor(void);

#endif /* PHANTOMOS_GRAPHICS_H */
