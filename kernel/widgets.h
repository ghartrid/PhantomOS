/*
 * PhantomOS GUI Widgets
 * "To Create, Not To Destroy"
 *
 * Basic widget toolkit for window content rendering.
 */

#ifndef PHANTOMOS_WIDGETS_H
#define PHANTOMOS_WIDGETS_H

#include <stdint.h>
#include "wm.h"

/*============================================================================
 * Widget: Label (static text)
 *============================================================================*/

void widget_label(struct wm_window *win, int x, int y,
                  const char *text, uint32_t color);

/*============================================================================
 * Widget: Button
 *============================================================================*/

struct widget_button {
    int         x, y, w, h;
    const char *text;
    uint32_t    bg_color;
    uint32_t    text_color;
    int         hovered;
};

/* Draw a button in a window's content area */
void widget_button_draw(struct wm_window *win, struct widget_button *btn);

/* Check if a point (relative to content area) is inside the button */
int widget_button_hit(struct widget_button *btn, int x, int y);

/*============================================================================
 * Widget: Text box (multi-line text display)
 *============================================================================*/

/* Draw wrapped text in a region of a window */
void widget_textbox(struct wm_window *win, int x, int y, int w, int h,
                    const char *text, uint32_t fg, uint32_t bg);

/*============================================================================
 * Widget: List (scrollable items)
 *============================================================================*/

#define WIDGET_LIST_ITEM_HEIGHT     20
#define WIDGET_LIST_MAX_ITEMS       64

struct widget_list {
    const char *items[WIDGET_LIST_MAX_ITEMS];
    int         count;
    int         scroll_offset;
    int         selected;
    int         x, y, w, h;
};

/* Draw a list widget */
void widget_list_draw(struct wm_window *win, struct widget_list *list);

/* Handle click on list (returns selected index or -1) */
int widget_list_click(struct widget_list *list, int click_x, int click_y);

/*============================================================================
 * Widget: Progress bar
 *============================================================================*/

void widget_progress(struct wm_window *win, int x, int y, int w, int h,
                     int percent, uint32_t fg, uint32_t bg);

/*============================================================================
 * Widget: Text Input (editable single-line text field)
 *============================================================================*/

#define WIDGET_TEXTINPUT_MAX    128

struct widget_textinput {
    int         x, y, w, h;
    char        buffer[WIDGET_TEXTINPUT_MAX];
    int         length;             /* Current text length */
    int         cursor;             /* Cursor position (0..length) */
    int         max_length;         /* Effective max (capped at MAX-1) */
    int         scroll_offset;      /* Horizontal scroll in characters */
    uint32_t    fg_color;
    uint32_t    bg_color;
    uint32_t    border_color;
};

void widget_textinput_init(struct widget_textinput *ti, int x, int y, int w, int h);
void widget_textinput_draw(struct wm_window *win, struct widget_textinput *ti);
void widget_textinput_key(struct widget_textinput *ti, int key);
void widget_textinput_click(struct widget_textinput *ti, int click_x, int click_y);
const char *widget_textinput_text(struct widget_textinput *ti);
void widget_textinput_set_text(struct widget_textinput *ti, const char *text);
void widget_textinput_clear(struct widget_textinput *ti);

/*============================================================================
 * Widget: Scrollbar (vertical)
 *============================================================================*/

#define WIDGET_SCROLLBAR_WIDTH  14
#define WIDGET_SCROLLBAR_ARROW  14

struct widget_scrollbar {
    int         x, y, h;
    int         total_items;
    int         visible_items;
    int         scroll_offset;
    uint32_t    track_color;
    uint32_t    thumb_color;
    uint32_t    arrow_color;
};

void widget_scrollbar_init(struct widget_scrollbar *sb, int x, int y, int h);
void widget_scrollbar_draw(struct wm_window *win, struct widget_scrollbar *sb);
int  widget_scrollbar_click(struct widget_scrollbar *sb, int click_x, int click_y);
void widget_scrollbar_update(struct widget_scrollbar *sb,
                             int total, int visible, int offset);

/*============================================================================
 * Widget: Checkbox
 *============================================================================*/

struct widget_checkbox {
    int         x, y;
    const char *label;
    int         checked;
    uint32_t    text_color;
};

void widget_checkbox_draw(struct wm_window *win, struct widget_checkbox *cb);
int  widget_checkbox_click(struct widget_checkbox *cb, int click_x, int click_y);

/*============================================================================
 * Widget: Tab Bar
 *============================================================================*/

#define WIDGET_TAB_MAX      8
#define WIDGET_TAB_HEIGHT   24

struct widget_tabbar {
    int         x, y, w;
    const char *tabs[WIDGET_TAB_MAX];
    int         tab_count;
    int         selected;
    uint32_t    active_bg;
    uint32_t    inactive_bg;
    uint32_t    text_color;
};

void widget_tabbar_init(struct widget_tabbar *tb, int x, int y, int w);
void widget_tabbar_draw(struct wm_window *win, struct widget_tabbar *tb);
int  widget_tabbar_click(struct widget_tabbar *tb, int click_x, int click_y);

#endif /* PHANTOMOS_WIDGETS_H */
