/*
 * PhantomOS GUI Widgets
 * "To Create, Not To Destroy"
 *
 * Renders widgets into window content buffers.
 */

#include "widgets.h"
#include "framebuffer.h"
#include "graphics.h"
#include "font.h"
#include <stdint.h>

/*============================================================================
 * External Declarations
 *============================================================================*/

extern size_t strlen(const char *s);
extern void *memmove(void *dest, const void *src, size_t n);
extern char *strncpy(char *dest, const char *src, size_t n);

/*============================================================================
 * Internal: Draw into window content buffer
 *
 * Widgets draw directly to the framebuffer at the window's content
 * position rather than into the content buffer, for simplicity.
 * The window manager redraws the content buffer each frame anyway.
 *============================================================================*/

/*
 * Get absolute position of window content area
 */
static void get_content_origin(struct wm_window *win, int *ox, int *oy)
{
    *ox = win->x;
    *oy = win->y + WM_TITLE_HEIGHT;
}

/*============================================================================
 * Widget Implementations
 *============================================================================*/

void widget_label(struct wm_window *win, int x, int y,
                  const char *text, uint32_t color)
{
    int ox, oy;
    get_content_origin(win, &ox, &oy);
    font_draw_string((uint32_t)(ox + x), (uint32_t)(oy + y),
                     text, color, COLOR_BG_PANEL);
}

void widget_button_draw(struct wm_window *win, struct widget_button *btn)
{
    int ox, oy;
    get_content_origin(win, &ox, &oy);

    int ax = ox + btn->x;
    int ay = oy + btn->y;

    /* Use hover color if hovered */
    uint32_t draw_bg = btn->hovered ? COLOR_BUTTON_HOVER : btn->bg_color;

    /* Rounded button background */
    int brad = 4;
    gfx_fill_rounded_rect(ax, ay, btn->w, btn->h, brad, draw_bg);

    /* Top highlight line for subtle bevel effect */
    uint32_t r = ((draw_bg >> 16) & 0xFF);
    uint32_t g = ((draw_bg >> 8) & 0xFF);
    uint32_t b = (draw_bg & 0xFF);
    r = (r + 25 > 255) ? 255 : r + 25;
    g = (g + 25 > 255) ? 255 : g + 25;
    b = (b + 25 > 255) ? 255 : b + 25;
    uint32_t highlight = 0xFF000000 | (r << 16) | (g << 8) | b;
    gfx_draw_hline(ax + brad, ay + 1, btn->w - 2 * brad, highlight);

    /* Center text in button */
    if (btn->text) {
        int text_w = (int)strlen(btn->text) * FONT_WIDTH;
        int tx = ax + (btn->w - text_w) / 2;
        int ty = ay + (btn->h - FONT_HEIGHT) / 2;
        font_draw_string((uint32_t)tx, (uint32_t)ty,
                         btn->text, btn->text_color, draw_bg);
    }
}

int widget_button_hit(struct widget_button *btn, int x, int y)
{
    return (x >= btn->x && x < btn->x + btn->w &&
            y >= btn->y && y < btn->y + btn->h);
}

void widget_textbox(struct wm_window *win, int x, int y, int w, int h,
                    const char *text, uint32_t fg, uint32_t bg)
{
    int ox, oy;
    get_content_origin(win, &ox, &oy);

    /* Draw background */
    fb_fill_rect((uint32_t)(ox + x), (uint32_t)(oy + y),
                 (uint32_t)w, (uint32_t)h, bg);

    if (!text) return;

    /* Render text with simple word wrapping */
    int cx = ox + x + 4;   /* 4px padding */
    int cy = oy + y + 2;
    int max_x = ox + x + w - 4;
    int max_y = oy + y + h - FONT_HEIGHT;

    while (*text && cy <= max_y) {
        if (*text == '\n') {
            cx = ox + x + 4;
            cy += FONT_HEIGHT;
            text++;
            continue;
        }

        if (cx + FONT_WIDTH > max_x) {
            cx = ox + x + 4;
            cy += FONT_HEIGHT;
            if (cy > max_y) break;
        }

        font_draw_char((uint32_t)cx, (uint32_t)cy, *text, fg, bg);
        cx += FONT_WIDTH;
        text++;
    }
}

void widget_list_draw(struct wm_window *win, struct widget_list *list)
{
    int ox, oy;
    get_content_origin(win, &ox, &oy);

    int ax = ox + list->x;
    int ay = oy + list->y;

    /* Background */
    fb_fill_rect((uint32_t)ax, (uint32_t)ay,
                 (uint32_t)list->w, (uint32_t)list->h, 0xFF0D0D1A);

    /* Border */
    fb_draw_rect((uint32_t)ax, (uint32_t)ay,
                 (uint32_t)list->w, (uint32_t)list->h, COLOR_BORDER);

    /* Draw visible items */
    int visible = list->h / WIDGET_LIST_ITEM_HEIGHT;
    int start = list->scroll_offset;

    for (int i = 0; i < visible && (start + i) < list->count; i++) {
        int idx = start + i;
        int iy = ay + i * WIDGET_LIST_ITEM_HEIGHT;

        /* Highlight selected item (rounded) */
        if (idx == list->selected) {
            gfx_fill_rounded_rect(ax + 2, iy, list->w - 4,
                                  WIDGET_LIST_ITEM_HEIGHT, 3, COLOR_ACCENT);
        }

        /* Item text */
        if (list->items[idx]) {
            uint32_t text_color = (idx == list->selected) ?
                                  COLOR_WHITE : COLOR_TEXT;
            uint32_t bg_color = (idx == list->selected) ?
                                COLOR_ACCENT : 0xFF0D0D1A;
            font_draw_string((uint32_t)(ax + 6),
                             (uint32_t)(iy + 2),
                             list->items[idx], text_color, bg_color);
        }
    }
}

int widget_list_click(struct widget_list *list, int click_x, int click_y)
{
    /* Check if click is within list bounds */
    if (click_x < list->x || click_x >= list->x + list->w ||
        click_y < list->y || click_y >= list->y + list->h)
        return -1;

    int rel_y = click_y - list->y;
    int idx = list->scroll_offset + rel_y / WIDGET_LIST_ITEM_HEIGHT;

    if (idx >= 0 && idx < list->count) {
        list->selected = idx;
        return idx;
    }
    return -1;
}

void widget_progress(struct wm_window *win, int x, int y, int w, int h,
                     int percent, uint32_t fg, uint32_t bg)
{
    int ox, oy;
    get_content_origin(win, &ox, &oy);

    int ax = ox + x;
    int ay = oy + y;

    /* Pill-shaped background (radius = half height) */
    int pr = h / 2;
    if (pr < 2) pr = 2;
    gfx_fill_rounded_rect(ax, ay, w, h, pr, bg);

    /* Pill-shaped fill */
    if (percent > 0) {
        if (percent > 100) percent = 100;
        int fill_w = w * percent / 100;
        if (fill_w < pr * 2) fill_w = pr * 2;
        gfx_fill_rounded_rect(ax, ay, fill_w, h, pr, fg);

        /* Top highlight on fill for subtle gradient effect */
        uint32_t fr = ((fg >> 16) & 0xFF);
        uint32_t fg2 = ((fg >> 8) & 0xFF);
        uint32_t fb2 = (fg & 0xFF);
        fr = (fr + 30 > 255) ? 255 : fr + 30;
        fg2 = (fg2 + 30 > 255) ? 255 : fg2 + 30;
        fb2 = (fb2 + 30 > 255) ? 255 : fb2 + 30;
        uint32_t hl = 0xFF000000 | (fr << 16) | (fg2 << 8) | fb2;
        if (fill_w > pr * 2)
            gfx_draw_hline(ax + pr, ay + 1, fill_w - 2 * pr, hl);
    }
}

/*============================================================================
 * Widget: Text Input
 *============================================================================*/

void widget_textinput_init(struct widget_textinput *ti, int x, int y, int w, int h)
{
    ti->x = x;
    ti->y = y;
    ti->w = w;
    ti->h = (h > 0) ? h : 20;
    ti->length = 0;
    ti->cursor = 0;
    ti->max_length = WIDGET_TEXTINPUT_MAX - 1;
    ti->scroll_offset = 0;
    ti->buffer[0] = '\0';
    ti->fg_color = COLOR_TEXT;
    ti->bg_color = COLOR_INPUT_BG;
    ti->border_color = COLOR_BORDER;
}

void widget_textinput_draw(struct wm_window *win, struct widget_textinput *ti)
{
    int ox, oy;
    get_content_origin(win, &ox, &oy);

    int ax = ox + ti->x;
    int ay = oy + ti->y;

    /* Rounded background */
    int ti_rad = 3;
    gfx_fill_rounded_rect(ax, ay, ti->w, ti->h, ti_rad, ti->bg_color);

    /* Inner shadow line at top (subtle inset) */
    gfx_draw_hline(ax + ti_rad, ay + 1, ti->w - 2 * ti_rad, 0xFF0A0A15);

    /* Rounded border segments */
    gfx_draw_rounded_rect(ax, ay, ti->w, ti->h, ti_rad, ti->border_color);

    /* Calculate visible characters */
    int pad = 4;
    int visible_chars = (ti->w - pad * 2) / FONT_WIDTH;
    if (visible_chars < 1) visible_chars = 1;

    /* Adjust scroll to keep cursor visible */
    if (ti->cursor < ti->scroll_offset)
        ti->scroll_offset = ti->cursor;
    if (ti->cursor >= ti->scroll_offset + visible_chars)
        ti->scroll_offset = ti->cursor - visible_chars + 1;

    /* Draw visible text */
    int tx = ax + pad;
    int ty = ay + (ti->h - FONT_HEIGHT) / 2;
    for (int i = 0; i < visible_chars && (ti->scroll_offset + i) < ti->length; i++) {
        font_draw_char((uint32_t)(tx + i * FONT_WIDTH), (uint32_t)ty,
                       ti->buffer[ti->scroll_offset + i],
                       ti->fg_color, ti->bg_color);
    }

    /* Draw cursor */
    int cursor_x = tx + (ti->cursor - ti->scroll_offset) * FONT_WIDTH;
    if (cursor_x >= ax + pad && cursor_x < ax + ti->w - pad) {
        font_draw_char((uint32_t)cursor_x, (uint32_t)ty,
                       '_', COLOR_HIGHLIGHT, ti->bg_color);
    }
}

void widget_textinput_key(struct widget_textinput *ti, int key)
{
    if (key == '\b' || key == 8) {
        /* Backspace */
        if (ti->cursor > 0) {
            memmove(&ti->buffer[ti->cursor - 1],
                    &ti->buffer[ti->cursor],
                    (size_t)(ti->length - ti->cursor));
            ti->length--;
            ti->cursor--;
            ti->buffer[ti->length] = '\0';
        }
    } else if (key == 0x109) {
        /* KEY_DELETE */
        if (ti->cursor < ti->length) {
            memmove(&ti->buffer[ti->cursor],
                    &ti->buffer[ti->cursor + 1],
                    (size_t)(ti->length - ti->cursor - 1));
            ti->length--;
            ti->buffer[ti->length] = '\0';
        }
    } else if (key == 0x102) {
        /* KEY_LEFT */
        if (ti->cursor > 0) ti->cursor--;
    } else if (key == 0x103) {
        /* KEY_RIGHT */
        if (ti->cursor < ti->length) ti->cursor++;
    } else if (key == 0x104) {
        /* KEY_HOME */
        ti->cursor = 0;
    } else if (key == 0x105) {
        /* KEY_END */
        ti->cursor = ti->length;
    } else if (key >= 32 && key < 127) {
        /* Printable character: insert at cursor */
        if (ti->length < ti->max_length) {
            memmove(&ti->buffer[ti->cursor + 1],
                    &ti->buffer[ti->cursor],
                    (size_t)(ti->length - ti->cursor));
            ti->buffer[ti->cursor] = (char)key;
            ti->length++;
            ti->cursor++;
            ti->buffer[ti->length] = '\0';
        }
    }
}

void widget_textinput_click(struct widget_textinput *ti, int click_x, int click_y)
{
    if (click_x < ti->x || click_x >= ti->x + ti->w ||
        click_y < ti->y || click_y >= ti->y + ti->h)
        return;

    int pad = 4;
    int char_pos = (click_x - ti->x - pad) / FONT_WIDTH + ti->scroll_offset;
    if (char_pos < 0) char_pos = 0;
    if (char_pos > ti->length) char_pos = ti->length;
    ti->cursor = char_pos;
}

const char *widget_textinput_text(struct widget_textinput *ti)
{
    ti->buffer[ti->length] = '\0';
    return ti->buffer;
}

void widget_textinput_set_text(struct widget_textinput *ti, const char *text)
{
    int len = (int)strlen(text);
    if (len > ti->max_length) len = ti->max_length;
    strncpy(ti->buffer, text, (size_t)len);
    ti->buffer[len] = '\0';
    ti->length = len;
    ti->cursor = len;
    ti->scroll_offset = 0;
}

void widget_textinput_clear(struct widget_textinput *ti)
{
    ti->buffer[0] = '\0';
    ti->length = 0;
    ti->cursor = 0;
    ti->scroll_offset = 0;
}

/*============================================================================
 * Widget: Scrollbar
 *============================================================================*/

void widget_scrollbar_init(struct widget_scrollbar *sb, int x, int y, int h)
{
    sb->x = x;
    sb->y = y;
    sb->h = h;
    sb->total_items = 0;
    sb->visible_items = 0;
    sb->scroll_offset = 0;
    sb->track_color = 0xFF0D0D1A;
    sb->thumb_color = COLOR_ACCENT;
    sb->arrow_color = COLOR_TEXT_DIM;
}

void widget_scrollbar_draw(struct wm_window *win, struct widget_scrollbar *sb)
{
    int ox, oy;
    get_content_origin(win, &ox, &oy);

    int ax = ox + sb->x;
    int ay = oy + sb->y;
    int w = WIDGET_SCROLLBAR_WIDTH;
    int arrow_h = WIDGET_SCROLLBAR_ARROW;

    /* Track background */
    fb_fill_rect((uint32_t)ax, (uint32_t)ay, (uint32_t)w, (uint32_t)sb->h,
                 sb->track_color);
    fb_draw_rect((uint32_t)ax, (uint32_t)ay, (uint32_t)w, (uint32_t)sb->h,
                 COLOR_BORDER);

    /* Up arrow */
    font_draw_char((uint32_t)(ax + 3), (uint32_t)(ay + 0),
                   '^', sb->arrow_color, sb->track_color);

    /* Down arrow */
    font_draw_char((uint32_t)(ax + 3), (uint32_t)(ay + sb->h - arrow_h),
                   'v', sb->arrow_color, sb->track_color);

    /* Thumb */
    if (sb->total_items > sb->visible_items && sb->visible_items > 0) {
        int track_h = sb->h - 2 * arrow_h;
        if (track_h < 4) return;

        int thumb_h = track_h * sb->visible_items / sb->total_items;
        if (thumb_h < 12) thumb_h = 12;
        if (thumb_h > track_h) thumb_h = track_h;

        int max_offset = sb->total_items - sb->visible_items;
        if (max_offset < 1) max_offset = 1;
        int thumb_y = ay + arrow_h +
                      (track_h - thumb_h) * sb->scroll_offset / max_offset;

        int tw = w - 4;
        int tr = tw / 2;
        if (tr < 2) tr = 2;
        gfx_fill_rounded_rect(ax + 2, thumb_y, tw, thumb_h, tr, sb->thumb_color);
    }
}

int widget_scrollbar_click(struct widget_scrollbar *sb, int click_x, int click_y)
{
    if (click_x < sb->x || click_x >= sb->x + WIDGET_SCROLLBAR_WIDTH ||
        click_y < sb->y || click_y >= sb->y + sb->h)
        return sb->scroll_offset;

    int max_offset = sb->total_items - sb->visible_items;
    if (max_offset < 0) max_offset = 0;

    int rel_y = click_y - sb->y;

    if (rel_y < WIDGET_SCROLLBAR_ARROW) {
        /* Up arrow */
        if (sb->scroll_offset > 0) sb->scroll_offset--;
    } else if (rel_y >= sb->h - WIDGET_SCROLLBAR_ARROW) {
        /* Down arrow */
        if (sb->scroll_offset < max_offset) sb->scroll_offset++;
    } else {
        /* Track: jump proportionally */
        int track_h = sb->h - 2 * WIDGET_SCROLLBAR_ARROW;
        if (track_h > 0 && max_offset > 0) {
            int track_pos = rel_y - WIDGET_SCROLLBAR_ARROW;
            sb->scroll_offset = track_pos * max_offset / track_h;
            if (sb->scroll_offset > max_offset) sb->scroll_offset = max_offset;
        }
    }

    return sb->scroll_offset;
}

void widget_scrollbar_update(struct widget_scrollbar *sb,
                             int total, int visible, int offset)
{
    sb->total_items = total;
    sb->visible_items = visible;
    sb->scroll_offset = offset;
}

/*============================================================================
 * Widget: Checkbox
 *============================================================================*/

void widget_checkbox_draw(struct wm_window *win, struct widget_checkbox *cb)
{
    int ox, oy;
    get_content_origin(win, &ox, &oy);

    int ax = ox + cb->x;
    int ay = oy + cb->y;

    /* Rounded checkbox box */
    gfx_fill_rounded_rect(ax, ay, 14, 14, 3,
                          cb->checked ? COLOR_ACCENT : COLOR_INPUT_BG);
    gfx_draw_rounded_rect(ax, ay, 14, 14, 3, COLOR_BORDER);

    /* Draw check mark */
    if (cb->checked) {
        font_draw_char((uint32_t)(ax + 3), (uint32_t)(ay - 1),
                       'X', COLOR_WHITE, COLOR_ACCENT);
    }

    /* Draw label */
    if (cb->label) {
        font_draw_string((uint32_t)(ax + 20), (uint32_t)(ay),
                         cb->label, cb->text_color, COLOR_BG_PANEL);
    }
}

int widget_checkbox_click(struct widget_checkbox *cb, int click_x, int click_y)
{
    int hit_w = 20;
    if (cb->label) {
        hit_w = 20 + (int)strlen(cb->label) * FONT_WIDTH;
    }

    if (click_x >= cb->x && click_x < cb->x + hit_w &&
        click_y >= cb->y && click_y < cb->y + 16) {
        cb->checked = !cb->checked;
        return 1;
    }
    return 0;
}

/*============================================================================
 * Widget: Tab Bar
 *============================================================================*/

void widget_tabbar_init(struct widget_tabbar *tb, int x, int y, int w)
{
    tb->x = x;
    tb->y = y;
    tb->w = w;
    tb->tab_count = 0;
    tb->selected = 0;
    tb->active_bg = COLOR_ACCENT;
    tb->inactive_bg = 0xFF0D0D1A;
    tb->text_color = COLOR_TEXT;
}

void widget_tabbar_draw(struct wm_window *win, struct widget_tabbar *tb)
{
    int ox, oy;
    get_content_origin(win, &ox, &oy);

    int ax = ox + tb->x;
    int ay = oy + tb->y;

    if (tb->tab_count < 1) return;

    int tab_w = tb->w / tb->tab_count;

    for (int i = 0; i < tb->tab_count; i++) {
        int tx = ax + i * tab_w;
        uint32_t bg = (i == tb->selected) ? tb->active_bg : tb->inactive_bg;

        /* Rounded top corners: draw rounded rect then square off bottom */
        gfx_fill_rounded_rect(tx, ay, tab_w, WIDGET_TAB_HEIGHT, 4, bg);
        fb_fill_rect((uint32_t)tx, (uint32_t)(ay + WIDGET_TAB_HEIGHT - 4),
                     (uint32_t)tab_w, 4, bg);

        /* Center text */
        if (tb->tabs[i]) {
            int text_w = (int)strlen(tb->tabs[i]) * FONT_WIDTH;
            int cx = tx + (tab_w - text_w) / 2;
            int cy = ay + (WIDGET_TAB_HEIGHT - FONT_HEIGHT) / 2;
            uint32_t fg = (i == tb->selected) ? COLOR_WHITE : tb->text_color;
            font_draw_string((uint32_t)cx, (uint32_t)cy,
                             tb->tabs[i], fg, bg);
        }
    }

    /* Highlight line under selected tab */
    if (tb->selected < tb->tab_count) {
        int sel_x = ax + tb->selected * tab_w;
        fb_fill_rect((uint32_t)sel_x, (uint32_t)(ay + WIDGET_TAB_HEIGHT - 2),
                     (uint32_t)tab_w, 2, COLOR_HIGHLIGHT);
    }
}

int widget_tabbar_click(struct widget_tabbar *tb, int click_x, int click_y)
{
    if (tb->tab_count < 1) return -1;

    if (click_x < tb->x || click_x >= tb->x + tb->w ||
        click_y < tb->y || click_y >= tb->y + WIDGET_TAB_HEIGHT)
        return -1;

    int tab_w = tb->w / tb->tab_count;
    int idx = (click_x - tb->x) / tab_w;
    if (idx >= 0 && idx < tb->tab_count) {
        tb->selected = idx;
        return idx;
    }
    return -1;
}
