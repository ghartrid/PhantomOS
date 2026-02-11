/*
 * PhantomOS Window Manager
 * "To Create, Not To Destroy"
 *
 * Manages draggable windows with title bars, z-ordering, and focus.
 * Renders to the framebuffer backbuffer, then flips.
 */

#include "wm.h"
#include "framebuffer.h"
#include "graphics.h"
#include "font.h"
#include "heap.h"
#include <stdint.h>
#include <stddef.h>

/*============================================================================
 * External Declarations
 *============================================================================*/

extern int kprintf(const char *fmt, ...);
extern void *memset(void *s, int c, size_t n);
extern void *memcpy(void *dest, const void *src, size_t n);
extern size_t strlen(const char *s);
extern char *strncpy(char *dest, const char *src, size_t n);

/*============================================================================
 * Window Manager State
 *============================================================================*/

static struct wm_window windows[WM_MAX_WINDOWS];

/* Z-order: array of window IDs from back to front */
static int z_order[WM_MAX_WINDOWS];
static int z_count = 0;

static int focused_id = 0;
static int prev_buttons = 0;
static int initialized = 0;

/*============================================================================
 * Z-Order Management
 *============================================================================*/

/*
 * Bring window to front of z-order
 */
static void z_bring_to_front(int id)
{
    /* Find and remove from current position */
    int found = -1;
    for (int i = 0; i < z_count; i++) {
        if (z_order[i] == id) {
            found = i;
            break;
        }
    }

    if (found >= 0) {
        /* Shift everything after it down */
        for (int i = found; i < z_count - 1; i++) {
            z_order[i] = z_order[i + 1];
        }
        z_order[z_count - 1] = id;
    }
}

/*
 * Remove window from z-order
 */
static void z_remove(int id)
{
    int found = -1;
    for (int i = 0; i < z_count; i++) {
        if (z_order[i] == id) {
            found = i;
            break;
        }
    }

    if (found >= 0) {
        for (int i = found; i < z_count - 1; i++) {
            z_order[i] = z_order[i + 1];
        }
        z_count--;
    }
}

/*============================================================================
 * Window Drawing
 *============================================================================*/

/*
 * Draw a gradient title bar with AA rounded top corners.
 * Combines gradient interpolation with anti-aliased corner-arc clipping.
 */
static void draw_title_gradient(int x, int y, int w, int h, int radius,
                                uint32_t color_top, uint32_t color_bottom)
{
    uint32_t *backbuf = fb_get_backbuffer();
    uint32_t fb_w = fb_get_width();
    uint32_t fb_h = fb_get_height();
    if (!backbuf || w <= 0 || h <= 0) return;

    int r2 = radius * radius;
    int r_inner = (radius - 1) * (radius - 1);
    int rt = (color_top >> 16) & 0xFF, gt_c = (color_top >> 8) & 0xFF, bt = color_top & 0xFF;
    int rb = (color_bottom >> 16) & 0xFF, gb = (color_bottom >> 8) & 0xFF, bb = color_bottom & 0xFF;
    int denom = (h > 1) ? (h - 1) : 1;

    for (int row = 0; row < h; row++) {
        int py = y + row;
        if (py < 0 || py >= (int)fb_h) continue;

        int r = rt + (rb - rt) * row / denom;
        int g = gt_c + (gb - gt_c) * row / denom;
        int b = bt + (bb - bt) * row / denom;
        uint32_t color = 0xFF000000 | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;

        if (row >= radius) {
            /* Below corners: full-width solid fill */
            int xs = (x < 0) ? 0 : x;
            int xe = (x + w > (int)fb_w) ? (int)fb_w : x + w;
            uint32_t *dst = &backbuf[py * fb_w + xs];
            for (int col = xs; col < xe; col++)
                *dst++ = color;
        } else {
            /* Corner rows: AA edge treatment */
            int dy = radius - 1 - row;
            int dy2 = dy * dy;

            for (int col = 0; col < w; col++) {
                int px = x + col;
                if (px < 0 || px >= (int)fb_w) continue;

                int cx_off = -1;
                if (col < radius) cx_off = radius - 1 - col;
                else if (col >= w - radius) cx_off = col - (w - radius);

                if (cx_off < 0) {
                    backbuf[py * fb_w + px] = color;
                    continue;
                }

                int dist2 = cx_off * cx_off + dy2;
                if (dist2 <= r_inner) {
                    backbuf[py * fb_w + px] = color;
                } else if (dist2 <= r2 + radius) {
                    int coverage = 255 - 255 * (dist2 - r_inner) / (r2 - r_inner + 1);
                    if (coverage < 0) coverage = 0;
                    if (coverage > 255) coverage = 255;
                    if (coverage > 0) {
                        uint32_t bg = backbuf[py * fb_w + px];
                        backbuf[py * fb_w + px] = gfx_alpha_blend(color, bg, (uint8_t)coverage);
                    }
                }
            }
        }
    }

    fb_mark_dirty((uint32_t)(x < 0 ? 0 : x), (uint32_t)(y < 0 ? 0 : y),
                  (uint32_t)w, (uint32_t)h);
}

/*
 * Draw a single window's decorations and content
 */
static void draw_window(struct wm_window *win)
{
    if (!(win->flags & WM_FLAG_VISIBLE)) return;

    int is_focused = (win->flags & WM_FLAG_FOCUSED) != 0;
    int x = win->x;
    int y = win->y;
    int w = win->width;
    int h = win->height;
    int rad = WM_CORNER_RADIUS;

    /* 1. Soft multi-layer drop shadow */
    gfx_draw_soft_shadow(x, y, w, h, rad);

    /* 2. Content area with AA bottom corners */
    int content_y = y + WM_TITLE_HEIGHT;
    int content_h = h - WM_TITLE_HEIGHT;
    fb_fill_rect((uint32_t)x, (uint32_t)content_y,
                 (uint32_t)w, (uint32_t)content_h, COLOR_BG_PANEL);

    /* 3. Title bar with gradient and AA rounded top corners */
    uint32_t title_top = is_focused ? 0xFF182848 : 0xFF0A0A15;
    uint32_t title_bot = is_focused ? COLOR_TITLE_FOCUS : COLOR_TITLE_UNFOCUS;
    draw_title_gradient(x, y, w, WM_TITLE_HEIGHT, rad, title_top, title_bot);

    /* 4. Subtle top-edge highlight + inner glow */
    if (is_focused) {
        int hl_skip = rad / 2 + 1;
        gfx_draw_hline(x + hl_skip, y + 1, w - 2 * hl_skip, 0xFF2A4A7A);
        /* Inner glow: very subtle bright line */
        uint32_t *backbuf = fb_get_backbuffer();
        uint32_t fb_w = fb_get_width();
        if (backbuf) {
            int glow_y = y + 2;
            if (glow_y >= 0 && glow_y < (int)fb_get_height()) {
                for (int gx = x + hl_skip; gx < x + w - hl_skip; gx++) {
                    if (gx >= 0 && gx < (int)fb_w) {
                        uint32_t *px = &backbuf[glow_y * fb_w + gx];
                        *px = gfx_alpha_blend(0xFF4A6A9A, *px, 25);
                    }
                }
            }
        }
    }

    /* 5. Bottom border line on title bar */
    gfx_draw_hline(x, y + WM_TITLE_HEIGHT - 1, w, 0xFF0A0A1A);

    /* 6. Side borders (subtle) */
    uint32_t border_color = is_focused ? 0xFF1A3050 : COLOR_BORDER;
    gfx_draw_vline(x, y + rad, h - rad, border_color);
    gfx_draw_vline(x + w - 1, y + rad, h - rad, border_color);
    gfx_draw_hline(x, y + h - 1, w, border_color);

    /* 7. Title text (centered vertically) */
    int text_y = y + (WM_TITLE_HEIGHT - FONT_HEIGHT) / 2;
    font_draw_string((uint32_t)(x + 10), (uint32_t)text_y,
                     win->title, COLOR_TEXT, title_bot);

    /* 8. Window buttons: [minimize] [maximize] [close] */
    if (win->flags & WM_FLAG_CLOSEABLE) {
        /* Close button (red, rightmost, larger) */
        int cbx = x + w - WM_CLOSE_SIZE - 6;
        int cby = y + (WM_TITLE_HEIGHT - WM_CLOSE_SIZE) / 2;
        gfx_fill_rounded_rect_aa(cbx, cby, WM_CLOSE_SIZE, WM_CLOSE_SIZE, 4,
                                 COLOR_CLOSE_BTN);
        int pad = 5;
        gfx_draw_line(cbx + pad, cby + pad,
                      cbx + WM_CLOSE_SIZE - pad - 1, cby + WM_CLOSE_SIZE - pad - 1,
                      COLOR_WHITE);
        gfx_draw_line(cbx + WM_CLOSE_SIZE - pad - 1, cby + pad,
                      cbx + pad, cby + WM_CLOSE_SIZE - pad - 1,
                      COLOR_WHITE);

        /* Maximize button (green, middle) */
        int mbx = cbx - WM_BTN_SIZE - WM_BTN_GAP;
        int mby = y + (WM_TITLE_HEIGHT - WM_BTN_SIZE) / 2;
        gfx_fill_rounded_rect_aa(mbx, mby, WM_BTN_SIZE, WM_BTN_SIZE, 3,
                                 0xFF22C55E);
        /* Square icon */
        gfx_draw_rounded_rect(mbx + 3, mby + 3, WM_BTN_SIZE - 6, WM_BTN_SIZE - 6, 1,
                               COLOR_WHITE);

        /* Minimize button (yellow, leftmost) */
        int nbx = mbx - WM_BTN_SIZE - WM_BTN_GAP;
        int nby = y + (WM_TITLE_HEIGHT - WM_BTN_SIZE) / 2;
        gfx_fill_rounded_rect_aa(nbx, nby, WM_BTN_SIZE, WM_BTN_SIZE, 3,
                                 0xFFEAB308);
        /* Horizontal line icon */
        gfx_draw_hline(nbx + 3, nby + WM_BTN_SIZE / 2, WM_BTN_SIZE - 6, COLOR_WHITE);
    }

    /* 9. Blit content buffer */
    if (win->content) {
        fb_blit((uint32_t)x, (uint32_t)content_y,
                (uint32_t)w, (uint32_t)content_h,
                win->content);
    }

    /* 10. Paint callback for custom rendering */
    if (win->on_paint) {
        win->on_paint(win);
    }

    /* 11. Fade overlay: blend toward black for partially transparent windows */
    if (win->fade_alpha < 255) {
        uint32_t *backbuf = fb_get_backbuffer();
        uint32_t fb_w = fb_get_width();
        uint32_t fb_h = fb_get_height();
        uint8_t darken = (uint8_t)(255 - win->fade_alpha);
        for (int row = 0; row < h; row++) {
            int py2 = y + row;
            if (py2 < 0 || py2 >= (int)fb_h) continue;
            for (int col = 0; col < w; col++) {
                int px2 = x + col;
                if (px2 < 0 || px2 >= (int)fb_w) continue;
                uint32_t *px = &backbuf[py2 * fb_w + px2];
                *px = gfx_alpha_blend(0xFF000000, *px, darken);
            }
        }
    }
}

/*============================================================================
 * Window Manager API
 *============================================================================*/

void wm_init(void)
{
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        windows[i].id = 0;
    }
    z_count = 0;
    focused_id = 0;
    prev_buttons = 0;
    initialized = 1;
}

int wm_create_window(int x, int y, int w, int h, const char *title)
{
    if (!initialized) return 0;

    /* Find free slot (slot 0 reserved for "no window") */
    int slot = -1;
    for (int i = 1; i < WM_MAX_WINDOWS; i++) {
        if (windows[i].id == 0) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return 0;

    struct wm_window *win = &windows[slot];
    win->id = slot;
    win->x = x;
    win->y = y;
    win->width = w;
    win->height = h + WM_TITLE_HEIGHT;  /* Add title bar to total height */
    win->flags = WM_FLAG_VISIBLE | WM_FLAG_CLOSEABLE;
    win->drag_ox = 0;
    win->drag_oy = 0;
    win->fade_alpha = 0;
    win->fading_in = 1;
    win->fading_out = 0;
    win->on_paint = NULL;
    win->on_key = NULL;
    win->on_click = NULL;
    win->on_close = NULL;

    /* Copy title */
    strncpy(win->title, title, WM_TITLE_MAX - 1);
    win->title[WM_TITLE_MAX - 1] = '\0';

    /* Allocate content buffer */
    int content_size = w * h * 4;
    win->content = (uint32_t *)kmalloc(content_size);
    if (win->content) {
        memset(win->content, 0, content_size);
    }

    /* Add to z-order (on top) */
    if (z_count < WM_MAX_WINDOWS) {
        z_order[z_count++] = slot;
    }

    /* Focus the new window */
    if (focused_id > 0 && focused_id < WM_MAX_WINDOWS) {
        windows[focused_id].flags &= ~WM_FLAG_FOCUSED;
    }
    win->flags |= WM_FLAG_FOCUSED;
    focused_id = slot;

    return slot;
}

/*
 * Actually finalize window destruction (called after fade-out completes)
 */
static void wm_finalize_destroy(int id)
{
    if (id <= 0 || id >= WM_MAX_WINDOWS) return;

    struct wm_window *win = &windows[id];
    if (win->id == 0) return;

    /* Notify owner before destroying */
    if (win->on_close)
        win->on_close(win);

    if (win->content) {
        kfree(win->content);
        win->content = NULL;
    }

    z_remove(id);

    /* If this was focused, focus the next topmost window */
    if (focused_id == id) {
        focused_id = 0;
        if (z_count > 0) {
            focused_id = z_order[z_count - 1];
            windows[focused_id].flags |= WM_FLAG_FOCUSED;
        }
    }

    win->id = 0;
    win->flags = 0;
}

void wm_destroy_window(int id)
{
    if (id <= 0 || id >= WM_MAX_WINDOWS) return;

    struct wm_window *win = &windows[id];
    if (win->id == 0) return;

    /* Start fade-out transition instead of instant destroy */
    if (!win->fading_out) {
        win->fading_out = 1;
        win->fading_in = 0;
    }
}

struct wm_window *wm_get_window(int id)
{
    if (id <= 0 || id >= WM_MAX_WINDOWS) return NULL;
    if (windows[id].id == 0) return NULL;
    return &windows[id];
}

void wm_set_on_paint(int id, void (*callback)(struct wm_window *))
{
    struct wm_window *win = wm_get_window(id);
    if (win) win->on_paint = callback;
}

void wm_set_on_key(int id, void (*callback)(struct wm_window *, int))
{
    struct wm_window *win = wm_get_window(id);
    if (win) win->on_key = callback;
}

void wm_set_on_click(int id, void (*callback)(struct wm_window *, int, int, int))
{
    struct wm_window *win = wm_get_window(id);
    if (win) win->on_click = callback;
}

void wm_set_on_close(int id, void (*callback)(struct wm_window *))
{
    struct wm_window *win = wm_get_window(id);
    if (win) win->on_close = callback;
}

/*============================================================================
 * Rendering
 *============================================================================*/

void wm_draw_all(void)
{
    if (!initialized) return;

    /* Tick fade animations for all windows */
    for (int i = 1; i < WM_MAX_WINDOWS; i++) {
        struct wm_window *w = &windows[i];
        if (w->id == 0) continue;

        if (w->fading_in) {
            int a = (int)w->fade_alpha + 42;
            if (a >= 255) {
                w->fade_alpha = 255;
                w->fading_in = 0;
            } else {
                w->fade_alpha = (uint8_t)a;
            }
        } else if (w->fading_out) {
            int a = (int)w->fade_alpha - 42;
            if (a <= 0) {
                /* Fade-out complete: actually destroy */
                w->fading_out = 0;
                wm_finalize_destroy(i);
                continue;
            } else {
                w->fade_alpha = (uint8_t)a;
            }
        }
    }

    /* Draw windows in z-order (back to front) */
    for (int i = 0; i < z_count; i++) {
        int id = z_order[i];
        if (id > 0 && id < WM_MAX_WINDOWS && windows[id].id != 0) {
            draw_window(&windows[id]);
        }
    }
}

/*============================================================================
 * Input Handling
 *============================================================================*/

/*
 * Hit test: find topmost window at screen coordinates
 * Returns window ID, or 0 if no window hit
 */
static int hit_test(int mx, int my)
{
    /* Search z-order from front to back */
    for (int i = z_count - 1; i >= 0; i--) {
        int id = z_order[i];
        struct wm_window *win = &windows[id];
        if (win->id == 0 || !(win->flags & WM_FLAG_VISIBLE)) continue;

        int bx = win->x - WM_BORDER_WIDTH;
        int by = win->y - WM_BORDER_WIDTH;
        int bw = win->width + 2 * WM_BORDER_WIDTH;
        int bh = win->height + 2 * WM_BORDER_WIDTH;

        if (mx >= bx && mx < bx + bw && my >= by && my < by + bh) {
            return id;
        }
    }
    return 0;
}

/*
 * Check if click is on the close button
 */
static int is_close_button_hit(struct wm_window *win, int mx, int my)
{
    if (!(win->flags & WM_FLAG_CLOSEABLE)) return 0;

    int cx = win->x + win->width - WM_CLOSE_SIZE - 6;
    int cy = win->y + (WM_TITLE_HEIGHT - WM_CLOSE_SIZE) / 2;

    return (mx >= cx && mx < cx + WM_CLOSE_SIZE &&
            my >= cy && my < cy + WM_CLOSE_SIZE);
}

/*
 * Check if click is in the title bar (for dragging)
 */
static int is_title_bar_hit(struct wm_window *win, int mx, int my)
{
    return (mx >= win->x && mx < win->x + win->width &&
            my >= win->y && my < win->y + WM_TITLE_HEIGHT);
}

void wm_handle_mouse(int x, int y, int buttons)
{
    if (!initialized) return;

    int left_pressed = (buttons & 1) && !(prev_buttons & 1);
    int left_held = (buttons & 1);
    int left_released = !(buttons & 1) && (prev_buttons & 1);
    prev_buttons = buttons;

    /* Handle active drag */
    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        if (windows[i].id != 0 && (windows[i].flags & WM_FLAG_DRAGGING)) {
            if (left_held) {
                windows[i].x = x - windows[i].drag_ox;
                windows[i].y = y - windows[i].drag_oy;
                return;
            } else {
                windows[i].flags &= ~WM_FLAG_DRAGGING;
            }
        }
    }

    if (left_pressed) {
        int hit_id = hit_test(x, y);

        if (hit_id > 0) {
            struct wm_window *win = &windows[hit_id];

            /* Focus this window */
            if (focused_id != hit_id) {
                if (focused_id > 0 && focused_id < WM_MAX_WINDOWS) {
                    windows[focused_id].flags &= ~WM_FLAG_FOCUSED;
                }
                win->flags |= WM_FLAG_FOCUSED;
                focused_id = hit_id;
                z_bring_to_front(hit_id);
            }

            /* Close button? */
            if (is_close_button_hit(win, x, y)) {
                wm_destroy_window(hit_id);
                return;
            }

            /* Title bar drag? */
            if (is_title_bar_hit(win, x, y)) {
                win->flags |= WM_FLAG_DRAGGING;
                win->drag_ox = x - win->x;
                win->drag_oy = y - win->y;
                return;
            }

            /* Content area click */
            int content_y = win->y + WM_TITLE_HEIGHT;
            if (y >= content_y && win->on_click) {
                win->on_click(win, x - win->x, y - content_y, buttons);
            }
        }
    }

    /* Forward held-button motion to focused window content area (for drawing) */
    if (left_held && !left_pressed && focused_id > 0 && focused_id < WM_MAX_WINDOWS) {
        struct wm_window *win = &windows[focused_id];
        if (win->id != 0 && !(win->flags & WM_FLAG_DRAGGING) && win->on_click) {
            int content_y = win->y + WM_TITLE_HEIGHT;
            if (y >= content_y) {
                win->on_click(win, x - win->x, y - content_y, buttons | 0x80);
            }
        }
    }

    /* Forward button release to focused window */
    if (left_released && focused_id > 0 && focused_id < WM_MAX_WINDOWS) {
        struct wm_window *win = &windows[focused_id];
        if (win->id != 0 && win->on_click) {
            int content_y = win->y + WM_TITLE_HEIGHT;
            win->on_click(win, x - win->x, y - content_y, 0x40);
        }
    }
}

void wm_handle_key(int key)
{
    if (!initialized) return;

    if (focused_id > 0 && focused_id < WM_MAX_WINDOWS) {
        struct wm_window *win = &windows[focused_id];
        if (win->id != 0 && win->on_key) {
            win->on_key(win, key);
        }
    }
}

/*============================================================================
 * Utility
 *============================================================================*/

int wm_content_width(struct wm_window *win)
{
    return win ? win->width : 0;
}

int wm_content_height(struct wm_window *win)
{
    return win ? (win->height - WM_TITLE_HEIGHT) : 0;
}

uint32_t *wm_content_buffer(struct wm_window *win)
{
    return win ? win->content : NULL;
}

int wm_window_count(void)
{
    int count = 0;
    for (int i = 1; i < WM_MAX_WINDOWS; i++) {
        if (windows[i].id != 0) count++;
    }
    return count;
}
