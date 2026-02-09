/*
 * PhantomOS Desktop Panel Layout
 * "To Create, Not To Destroy"
 *
 * Structured panel-based desktop: header, menubar, sidebar, app grid,
 * right panels (AI Governor + Assistant), dock, and status bar.
 */

#ifndef PHANTOMOS_DESKTOP_PANELS_H
#define PHANTOMOS_DESKTOP_PANELS_H

#include <stdint.h>
#include "icons.h"
#include "framebuffer.h"

/*============================================================================
 * Layout Constants (dynamic resolution via fb_get_width/height)
 *============================================================================*/

#define HEADER_HEIGHT       30
#define MENUBAR_HEIGHT      24
#define SIDEBAR_WIDTH       120
#define RIGHT_PANEL_WIDTH   240
#define DOCK_HEIGHT         48
#define STATUS_HEIGHT       20

#define CONTENT_Y           (HEADER_HEIGHT + MENUBAR_HEIGHT)
#define DOCK_Y              ((int)fb_get_height() - STATUS_HEIGHT - DOCK_HEIGHT)
#define STATUS_Y            ((int)fb_get_height() - STATUS_HEIGHT)
#define CONTENT_HEIGHT      (DOCK_Y - CONTENT_Y)

#define CENTER_X            SIDEBAR_WIDTH
#define CENTER_WIDTH        ((int)fb_get_width() - SIDEBAR_WIDTH - RIGHT_PANEL_WIDTH)
#define RIGHT_PANEL_X       ((int)fb_get_width() - RIGHT_PANEL_WIDTH)

/*============================================================================
 * Sidebar Sub-Items (matching simulation gui.c categories)
 *============================================================================*/

#define SIDEBAR_CAT_COUNT       7
#define SIDEBAR_SUB_MAX         6       /* Max sub-items per category */

struct sidebar_subitem {
    const char  *label;         /* Display name (e.g., "Files") */
    const char  *panel_id;      /* Panel identifier for launching */
};

struct sidebar_category {
    const char              *name;          /* Category name */
    int                     sub_count;      /* Number of sub-items */
    struct sidebar_subitem  items[SIDEBAR_SUB_MAX];
};

/*============================================================================
 * App Grid
 *============================================================================*/

#define APP_GRID_MAX        12
#define APP_ICON_CELL_W     120
#define APP_ICON_CELL_H     90
#define APP_ICON_COLS       3

struct app_entry {
    const char                  *name;
    const struct icon_sprite    *icon;
    const struct dock_icon_sprite *dock_icon;
    void                        (*on_launch)(void);
};

/*============================================================================
 * AI Assistant State
 *============================================================================*/

#define AI_INPUT_MAX        256
#define AI_RESPONSE_MAX     512

struct ai_assistant_state {
    char    input_buf[AI_INPUT_MAX];
    int     input_len;
    char    response_buf[AI_RESPONSE_MAX];
    int     has_response;
};

/*============================================================================
 * Panel Drawing Functions
 *============================================================================*/

void panel_draw_header(void);
void panel_draw_menubar(void);
void panel_draw_sidebar(int selected_category, const struct sidebar_category *cats,
                        int hover_cat, int hover_sub, int anim_expand_h);
void panel_draw_app_grid(const struct app_entry *apps, int count, int hover_idx);
void panel_draw_right_governor(void);
void panel_draw_right_assistant(struct ai_assistant_state *state);
void panel_draw_dock(const struct app_entry *apps, int count, int hover_idx);
void panel_draw_statusbar(void);

/*============================================================================
 * Hit Testing (returns index or -1)
 *============================================================================*/

int sidebar_hit_test(int mx, int my, int selected_category,
                     const struct sidebar_category *cats,
                     int *out_category, int *out_subitem);
int app_grid_hit_test(int mx, int my, int app_count);
int dock_hit_test(int mx, int my, int icon_count);
int ai_input_hit_test(int mx, int my);
int ai_button_hit_test(int mx, int my);
int statusbar_power_hit_test(int mx, int my);

/*============================================================================
 * Governor Accessor Functions (defined in desktop.c)
 *============================================================================*/

const char *desktop_gov_threat_str(void);
uint32_t    desktop_gov_threat_color(void);
uint64_t    desktop_gov_last_scan_ticks(void);
const char *desktop_gov_trend_str(void);
int         desktop_gov_health_score(void);
const char *desktop_gov_alert_str(void);
int         desktop_gov_alert_severity(void);
const char *desktop_gov_recommendation(void);

#endif /* PHANTOMOS_DESKTOP_PANELS_H */
