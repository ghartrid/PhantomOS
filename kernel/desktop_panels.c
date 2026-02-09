/*
 * PhantomOS Desktop Panel Layout
 * "To Create, Not To Destroy"
 *
 * Renders the structured desktop: header, menubar, sidebar,
 * app grid, right panels, dock, and status bar.
 */

#include "desktop_panels.h"
#include "framebuffer.h"
#include "graphics.h"
#include "font.h"
#include "timer.h"
#include "pmm.h"
#include <stdint.h>
#include <stddef.h>

/*============================================================================
 * External Declarations
 *============================================================================*/

extern size_t strlen(const char *s);

/*============================================================================
 * Sidebar Layout
 *============================================================================*/

#define SIDEBAR_CAT_H       22      /* Category header height */
#define SIDEBAR_SUB_H       18      /* Sub-item height */

/*============================================================================
 * AI Button Layout (inside right panel, bottom area)
 *============================================================================*/

#define AI_BTN_Y_OFFSET     420    /* Y offset from CONTENT_Y for buttons */
#define AI_BTN_W            60
#define AI_BTN_H            20
#define AI_BTN_GAP          8
#define AI_INPUT_Y_OFFSET   390    /* Y offset from CONTENT_Y for input */
#define AI_INPUT_H          22

/*============================================================================
 * Panel Drawing: Header Bar
 *============================================================================*/

void panel_draw_header(void)
{
    uint32_t w = fb_get_width();

    gfx_fill_gradient_v(0, 0, (int)w, HEADER_HEIGHT, 0xFF141B22, COLOR_HEADER_BG);

    /* Accent line at bottom of header */
    gfx_draw_hline(0, HEADER_HEIGHT - 2, (int)w,
                   gfx_alpha_blend(COLOR_HIGHLIGHT, COLOR_HEADER_BG, 40));
    gfx_draw_hline(0, HEADER_HEIGHT - 1, (int)w, COLOR_PANEL_BORDER);

    /* Centered "PhantomOS" title with text shadow */
    const char *title = "PhantomOS";
    int title_len = (int)strlen(title);
    int tx = (int)(w / 2) - (title_len * FONT_WIDTH) / 2;
    font_draw_string((uint32_t)(tx + 1), 4, title, 0xFF050508, COLOR_HEADER_BG);
    font_draw_string((uint32_t)tx, 3, title, COLOR_TEXT, COLOR_HEADER_BG);

    /* Subtitle below */
    const char *sub = "\"To Create, Not To Destroy\"";
    int sub_len = (int)strlen(sub);
    int sx = (int)(w / 2) - (sub_len * FONT_WIDTH) / 2;
    font_draw_string((uint32_t)sx, 16, sub, COLOR_TEXT_DIM, COLOR_HEADER_BG);
}

/*============================================================================
 * Panel Drawing: Menu Bar
 *============================================================================*/

void panel_draw_menubar(void)
{
    uint32_t w = fb_get_width();

    fb_fill_rect(0, HEADER_HEIGHT, w, MENUBAR_HEIGHT, COLOR_MENUBAR_BG);
    gfx_draw_hline(0, HEADER_HEIGHT + MENUBAR_HEIGHT - 1, (int)w, COLOR_PANEL_BORDER);

    int y = HEADER_HEIGHT + 4;

    /* Left side: Activities, Applications */
    font_draw_string(12, (uint32_t)y, "Activities", COLOR_TEXT, COLOR_MENUBAR_BG);
    font_draw_string(108, (uint32_t)y, "Applications", COLOR_TEXT_DIM, COLOR_MENUBAR_BG);


    /* Right side: Governor status + Clock */
    /* Governor status */
    font_draw_string(w - 340, (uint32_t)y, "Governor:", COLOR_TEXT_DIM, COLOR_MENUBAR_BG);
    font_draw_string(w - 264, (uint32_t)y, "Active", COLOR_GREEN_ACTIVE, COLOR_MENUBAR_BG);

    /* Separator */
    gfx_draw_vline((int)(w - 216), HEADER_HEIGHT + 4, MENUBAR_HEIGHT - 8, COLOR_PANEL_BORDER);

    /* Date and time */
    uint64_t ticks = timer_get_ticks();
    uint64_t seconds = ticks / 100;
    uint64_t minutes = seconds / 60;
    uint64_t hours = minutes / 60;
    seconds %= 60;
    minutes %= 60;

    char clock[20];
    /* "Thu Feb 05 HH:MM" */
    clock[0] = 'T'; clock[1] = 'h'; clock[2] = 'u'; clock[3] = ' ';
    clock[4] = 'F'; clock[5] = 'e'; clock[6] = 'b'; clock[7] = ' ';
    clock[8] = '0'; clock[9] = '5'; clock[10] = ' ';
    clock[11] = '0' + (char)(hours / 10);
    clock[12] = '0' + (char)(hours % 10);
    clock[13] = ':';
    clock[14] = '0' + (char)(minutes / 10);
    clock[15] = '0' + (char)(minutes % 10);
    clock[16] = '\0';

    font_draw_string(w - 200, (uint32_t)y, clock, COLOR_TEXT, COLOR_MENUBAR_BG);
}

/*============================================================================
 * Panel Drawing: Left Sidebar (with expandable sub-items + category icons)
 *============================================================================*/

/* Sidebar category icon lookup (indexed by category 0-6) */
static const struct sidebar_icon_sprite * const sidebar_cat_icons[SIDEBAR_CAT_COUNT] = {
    &sidebar_icon_core,
    &sidebar_icon_system,
    &sidebar_icon_security,
    &sidebar_icon_network,
    &sidebar_icon_apps,
    &sidebar_icon_utilities,
    &sidebar_icon_reference,
};

void panel_draw_sidebar(int selected_category, const struct sidebar_category *cats,
                        int hover_cat, int hover_sub, int anim_expand_h)
{
    int y_start = CONTENT_Y;
    int y_end = DOCK_Y;

    fb_fill_rect(0, (uint32_t)y_start, SIDEBAR_WIDTH, (uint32_t)(y_end - y_start), COLOR_SIDEBAR_BG);

    /* Right border */
    gfx_draw_vline(SIDEBAR_WIDTH - 1, y_start, y_end - y_start, COLOR_PANEL_BORDER);

    if (!cats) return;

    int py = y_start + 8;

    for (int i = 0; i < SIDEBAR_CAT_COUNT; i++) {
        int is_sel = (i == selected_category);
        int is_hover = (!is_sel && i == hover_cat && hover_sub < 0);

        /* Draw category header (rounded selection highlight) */
        if (is_sel) {
            gfx_fill_rounded_rect(4, py, SIDEBAR_WIDTH - 8, SIDEBAR_CAT_H, 4, COLOR_SIDEBAR_SEL);
            fb_fill_rect(0, (uint32_t)py, 3, SIDEBAR_CAT_H, COLOR_HIGHLIGHT);
        } else if (is_hover) {
            /* Subtle hover highlight for non-selected categories */
            gfx_fill_rounded_rect(4, py, SIDEBAR_WIDTH - 8, SIDEBAR_CAT_H, 4, 0xFF151C24);
        }

        /* Category mini-icon (8x8) */
        uint32_t bg = is_sel ? COLOR_SIDEBAR_SEL : (is_hover ? 0xFF151C24 : COLOR_SIDEBAR_BG);
        sidebar_icon_draw(6, py + 7, sidebar_cat_icons[i], bg);

        /* Category name (right of icon) */
        font_draw_string(18, (uint32_t)(py + 3), cats[i].name,
                        is_sel ? COLOR_TEXT : COLOR_TEXT_DIM, bg);

        py += SIDEBAR_CAT_H;

        /* If selected, draw sub-items expanded (with animation clipping) */
        if (is_sel && cats[i].sub_count > 0) {
            int sub_region_start = py;
            int full_h = cats[i].sub_count * SIDEBAR_SUB_H + 4;
            int visible_h = (anim_expand_h >= 0) ? anim_expand_h : full_h;

            for (int j = 0; j < cats[i].sub_count; j++) {
                /* Clip: skip sub-items beyond the animated visible height */
                if ((py - sub_region_start) + SIDEBAR_SUB_H > visible_h)
                    break;

                int sub_hover = (i == hover_cat && j == hover_sub);
                uint32_t sub_bg = COLOR_SIDEBAR_BG;
                if (sub_hover) {
                    /* Hover highlight for sub-item */
                    gfx_fill_rounded_rect(8, py, SIDEBAR_WIDTH - 16, SIDEBAR_SUB_H, 3, 0xFF1A2332);
                    sub_bg = 0xFF1A2332;
                }
                /* Sub-item with colored dot indicator */
                font_draw_char(16, (uint32_t)(py + 1), '*', COLOR_GREEN_ACTIVE, sub_bg);
                font_draw_string(26, (uint32_t)(py + 1), cats[i].items[j].label,
                                COLOR_TEXT, sub_bg);
                py += SIDEBAR_SUB_H;
            }
            /* Advance py by the visible height (animation or full) */
            if (anim_expand_h >= 0) {
                py = sub_region_start + visible_h;
            } else {
                /* Small gap after sub-items (already advanced py in loop) */
                py += 4;
            }
        }
    }
}

/*============================================================================
 * Panel Drawing: Center App Grid
 *============================================================================*/

void panel_draw_app_grid(const struct app_entry *apps, int count, int hover_idx)
{
    int y_start = CONTENT_Y;
    int y_end = DOCK_Y;

    /* Fill center content area */
    fb_fill_rect(CENTER_X, (uint32_t)y_start, CENTER_WIDTH, (uint32_t)(y_end - y_start), COLOR_CONTENT_BG);

    if (!apps || count <= 0) return;

    /* Calculate grid origin - center the grid */
    int grid_w = APP_ICON_COLS * APP_ICON_CELL_W;
    int grid_x = CENTER_X + (CENTER_WIDTH - grid_w) / 2;
    int grid_y = y_start + 40;  /* Top margin */

    for (int i = 0; i < count && i < APP_GRID_MAX; i++) {
        int col = i % APP_ICON_COLS;
        int row = i / APP_ICON_COLS;

        int cell_x = grid_x + col * APP_ICON_CELL_W;
        int cell_y = grid_y + row * APP_ICON_CELL_H;

        int is_hover = (i == hover_idx);

        /* Rounded background card behind each app icon */
        int card_pad = 6;
        int card_x = cell_x + card_pad;
        int card_y = cell_y + 2;
        int card_w = APP_ICON_CELL_W - 2 * card_pad;
        int card_h = APP_ICON_CELL_H - 4;
        uint32_t card_bg = is_hover ? 0xFF182030 : 0xFF111827;
        /* Drop shadow behind card */
        gfx_draw_shadow(card_x, card_y, card_w, card_h, 2, 50);
        gfx_fill_rounded_rect(card_x, card_y, card_w, card_h, 8, card_bg);
        if (is_hover) {
            /* Accent border on hover */
            gfx_draw_rounded_rect(card_x, card_y, card_w, card_h, 8, 0xFF2A4A6F);
        }

        /* Center icon in card */
        int icon_x = cell_x + (APP_ICON_CELL_W - ICON_SIZE) / 2;
        int icon_y = cell_y + 4;

        /* Draw icon */
        if (apps[i].icon) {
            icon_draw(icon_x, icon_y, apps[i].icon, card_bg);
        }

        /* Draw label centered below icon */
        if (apps[i].name) {
            int name_len = (int)strlen(apps[i].name);
            int label_x = cell_x + (APP_ICON_CELL_W - name_len * FONT_WIDTH) / 2;
            font_draw_string((uint32_t)label_x, (uint32_t)(icon_y + ICON_SIZE + 6),
                           apps[i].name, COLOR_TEXT, card_bg);
        }
    }
}

/*============================================================================
 * Panel Drawing: Right Panel - AI Governor
 *============================================================================*/

void panel_draw_right_governor(void)
{
    int x = RIGHT_PANEL_X;
    int y = CONTENT_Y;
    int h = CONTENT_HEIGHT / 2;

    fb_fill_rect((uint32_t)x, (uint32_t)y, RIGHT_PANEL_WIDTH, (uint32_t)h, COLOR_PANEL_BG);

    /* Left border */
    gfx_draw_vline(x, y, h, COLOR_PANEL_BORDER);

    /* Bottom border (separator between governor and assistant) */
    gfx_draw_hline(x, y + h - 1, RIGHT_PANEL_WIDTH, COLOR_PANEL_BORDER);

    /* Title area */
    int py = y + 12;

    /* Shield icon indicator (rounded) */
    gfx_fill_rounded_rect(x + 12, py, 12, 12, 3, COLOR_ICON_ORANGE);
    font_draw_string((uint32_t)(x + 30), (uint32_t)py, "AI Governor", COLOR_TEXT, COLOR_PANEL_BG);
    py += 18;
    font_draw_string((uint32_t)(x + 30), (uint32_t)py, "PhantomOS AI Interface", COLOR_TEXT_DIM, COLOR_PANEL_BG);
    py += 28;

    /* Status fields with separator lines */
    font_draw_string((uint32_t)(x + 16), (uint32_t)py, "Protection:", COLOR_TEXT_DIM, COLOR_PANEL_BG);
    font_draw_string((uint32_t)(x + 112), (uint32_t)py, "Active", COLOR_GREEN_ACTIVE, COLOR_PANEL_BG);
    py += 20;
    gfx_draw_hline(x + 12, py - 3, RIGHT_PANEL_WIDTH - 24, 0xFF1A2030);

    /* Dynamic threat level */
    font_draw_string((uint32_t)(x + 16), (uint32_t)py, "Threat Level:", COLOR_TEXT_DIM, COLOR_PANEL_BG);
    font_draw_string((uint32_t)(x + 128), (uint32_t)py,
                     desktop_gov_threat_str(), desktop_gov_threat_color(), COLOR_PANEL_BG);
    py += 20;

    /* Last scan time from actual scan counter */
    font_draw_string((uint32_t)(x + 16), (uint32_t)py, "Last Scan:", COLOR_TEXT_DIM, COLOR_PANEL_BG);
    uint64_t last = desktop_gov_last_scan_ticks();
    uint64_t now = timer_get_ticks();
    uint64_t ago_secs = (last > 0 && now > last) ? (now - last) / 100 : 0;
    uint64_t ago_mins = ago_secs / 60;
    ago_secs = ago_secs % 60;
    char scan_str[24];
    int pos = 0;
    if (last == 0) {
        scan_str[0] = 'n'; scan_str[1] = 'e'; scan_str[2] = 'v';
        scan_str[3] = 'e'; scan_str[4] = 'r'; scan_str[5] = '\0';
    } else {
        if (ago_mins >= 10) scan_str[pos++] = '0' + (char)((ago_mins / 10) % 10);
        scan_str[pos++] = '0' + (char)(ago_mins % 10);
        scan_str[pos++] = 'm'; scan_str[pos++] = ' ';
        if (ago_secs >= 10) scan_str[pos++] = '0' + (char)((ago_secs / 10) % 10);
        scan_str[pos++] = '0' + (char)(ago_secs % 10);
        scan_str[pos++] = 's'; scan_str[pos++] = ' ';
        scan_str[pos++] = 'a'; scan_str[pos++] = 'g'; scan_str[pos++] = 'o';
        scan_str[pos] = '\0';
    }
    font_draw_string((uint32_t)(x + 104), (uint32_t)py, scan_str, COLOR_TEXT, COLOR_PANEL_BG);
    py += 20;

    /* Threat trend */
    font_draw_string((uint32_t)(x + 16), (uint32_t)py, "Trend:", COLOR_TEXT_DIM, COLOR_PANEL_BG);
    {
        const char *trend = desktop_gov_trend_str();
        uint32_t tc = COLOR_TEXT;
        if (trend[0] == 'R') tc = 0xFFE94560;      /* Rising = red */
        else if (trend[0] == 'F') tc = 0xFF22C55E;  /* Falling = green */
        else if (trend[0] == 'S') tc = 0xFFEAB308;  /* Stable = yellow */
        font_draw_string((uint32_t)(x + 80), (uint32_t)py, trend, tc, COLOR_PANEL_BG);
    }
    py += 20;

    /* Health score */
    font_draw_string((uint32_t)(x + 16), (uint32_t)py, "Health:", COLOR_TEXT_DIM, COLOR_PANEL_BG);
    {
        int hs = desktop_gov_health_score();
        char hstr[8];
        int hi = 0;
        if (hs >= 100) { hstr[hi++] = '1'; hstr[hi++] = '0'; hstr[hi++] = '0'; }
        else {
            if (hs >= 10) hstr[hi++] = '0' + (char)(hs / 10);
            hstr[hi++] = '0' + (char)(hs % 10);
        }
        hstr[hi] = '\0';
        uint32_t hc = 0xFF22C55E;
        if (hs < 40) hc = 0xFFE94560;
        else if (hs <= 70) hc = 0xFFEAB308;
        font_draw_string((uint32_t)(x + 88), (uint32_t)py, hstr, hc, COLOR_PANEL_BG);
    }
    py += 20;

    /* Alert status */
    font_draw_string((uint32_t)(x + 16), (uint32_t)py, "Alerts:", COLOR_TEXT_DIM, COLOR_PANEL_BG);
    {
        int asev = desktop_gov_alert_severity();
        const char *astr = desktop_gov_alert_str();
        uint32_t ac = 0xFF22C55E; /* green = none */
        if (asev == 1) ac = 0xFFEAB308;      /* yellow = warning */
        else if (asev >= 2) ac = 0xFFE94560;  /* red = critical */
        font_draw_string((uint32_t)(x + 80), (uint32_t)py, astr, ac, COLOR_PANEL_BG);
    }
    py += 20;

    font_draw_string((uint32_t)(x + 16), (uint32_t)py, "AI Mode:", COLOR_TEXT_DIM, COLOR_PANEL_BG);
    font_draw_string((uint32_t)(x + 88), (uint32_t)py, "Autonomous", COLOR_TEXT, COLOR_PANEL_BG);
}

/*============================================================================
 * Panel Drawing: Right Panel - AI Assistant
 *============================================================================*/

void panel_draw_right_assistant(struct ai_assistant_state *state)
{
    int x = RIGHT_PANEL_X;
    int y = CONTENT_Y + CONTENT_HEIGHT / 2;
    int h = CONTENT_HEIGHT / 2;

    fb_fill_rect((uint32_t)x, (uint32_t)y, RIGHT_PANEL_WIDTH, (uint32_t)h, COLOR_PANEL_BG);

    /* Left border */
    gfx_draw_vline(x, y, h, COLOR_PANEL_BORDER);

    int py = y + 12;

    /* Title (rounded indicator) */
    gfx_fill_rounded_rect(x + 12, py, 12, 12, 3, COLOR_ICON_PURPLE);
    font_draw_string((uint32_t)(x + 30), (uint32_t)py, "AI Assistant", COLOR_TEXT, COLOR_PANEL_BG);
    py += 24;

    /* Welcome text */
    font_draw_string((uint32_t)(x + 12), (uint32_t)py, "Welcome to PhantomOS", COLOR_TEXT, COLOR_PANEL_BG);
    py += 16;
    font_draw_string((uint32_t)(x + 12), (uint32_t)py, "AI Governor Interface.", COLOR_TEXT, COLOR_PANEL_BG);
    py += 24;

    font_draw_string((uint32_t)(x + 12), (uint32_t)py, "I can help you:", COLOR_TEXT_DIM, COLOR_PANEL_BG);
    py += 20;

    /* Bullet points */
    font_draw_string((uint32_t)(x + 12), (uint32_t)py, "* Navigate the system", COLOR_TEXT_DIM, COLOR_PANEL_BG);
    py += 16;
    font_draw_string((uint32_t)(x + 12), (uint32_t)py, "* Check security status", COLOR_TEXT_DIM, COLOR_PANEL_BG);
    py += 16;
    font_draw_string((uint32_t)(x + 12), (uint32_t)py, "* Run system commands", COLOR_TEXT_DIM, COLOR_PANEL_BG);
    py += 16;
    font_draw_string((uint32_t)(x + 12), (uint32_t)py, "* Manage files", COLOR_TEXT_DIM, COLOR_PANEL_BG);
    py += 24;

    /* Response area (word-wrapped multi-line) */
    if (state && state->has_response) {
        const char *text = state->response_buf;
        int max_chars = (RIGHT_PANEL_WIDTH - 24) / 8; /* ~27 chars per line */
        int lines_shown = 0;
        int ti = 0;
        while (text[ti] && lines_shown < 5) {
            /* Find line break point */
            int line_end = ti;
            int last_space = ti;
            int col = 0;
            while (text[line_end] && col < max_chars) {
                if (text[line_end] == ' ') last_space = line_end;
                line_end++;
                col++;
            }
            /* If we didn't reach end of string, break at last space */
            if (text[line_end] && last_space > ti)
                line_end = last_space + 1;
            /* Draw this line */
            char line[64];
            int li = 0;
            for (int j = ti; j < line_end && li < 62; j++)
                line[li++] = text[j];
            /* Trim trailing space */
            while (li > 0 && line[li - 1] == ' ') li--;
            line[li] = '\0';
            font_draw_string((uint32_t)(x + 12), (uint32_t)py,
                            line, COLOR_GREEN_ACTIVE, COLOR_PANEL_BG);
            py += 14;
            lines_shown++;
            ti = line_end;
            /* Skip leading spaces on next line */
            while (text[ti] == ' ') ti++;
        }
        py += 6;
    }

    /* Smart recommendation tip */
    {
        const char *rec = desktop_gov_recommendation();
        if (rec) {
            font_draw_string((uint32_t)(x + 12), (uint32_t)py, "Tip:", COLOR_ICON_YELLOW, COLOR_PANEL_BG);
            py += 14;
            int ri = 0, lines = 0;
            int max_ch = (RIGHT_PANEL_WIDTH - 24) / 8;
            while (rec[ri] && lines < 2) {
                int le = ri, ls = ri;
                int col = 0;
                while (rec[le] && col < max_ch) {
                    if (rec[le] == ' ') ls = le;
                    le++; col++;
                }
                if (rec[le] && ls > ri) le = ls + 1;
                char tl[64];
                int li = 0;
                for (int j = ri; j < le && li < 62; j++) tl[li++] = rec[j];
                while (li > 0 && tl[li-1] == ' ') li--;
                tl[li] = '\0';
                font_draw_string((uint32_t)(x + 16), (uint32_t)py, tl, COLOR_ICON_YELLOW, COLOR_PANEL_BG);
                py += 14;
                ri = le;
                while (rec[ri] == ' ') ri++;
                lines++;
            }
            py += 4;
        }
    }

    /* Input field (rounded) */
    int input_y = y + h - 60;
    gfx_fill_rounded_rect(x + 8, input_y, RIGHT_PANEL_WIDTH - 16, AI_INPUT_H, 4, COLOR_INPUT_BG);
    gfx_draw_rounded_rect(x + 8, input_y, RIGHT_PANEL_WIDTH - 16, AI_INPUT_H, 4, COLOR_PANEL_BORDER);

    if (state && state->input_len > 0) {
        /* Show typed text */
        char display[AI_INPUT_MAX + 2];
        int i;
        for (i = 0; i < state->input_len && i < 26; i++)
            display[i] = state->input_buf[i];
        display[i] = '_';
        display[i + 1] = '\0';
        font_draw_string((uint32_t)(x + 12), (uint32_t)(input_y + 3), display, COLOR_TEXT, COLOR_INPUT_BG);
    } else {
        font_draw_string((uint32_t)(x + 12), (uint32_t)(input_y + 3),
                        "Ask the AI Governor...", COLOR_TEXT_DIM, COLOR_INPUT_BG);
    }

    /* Buttons row */
    int btn_y = input_y + AI_INPUT_H + 8;
    int btn_x = x + 8;

    /* Scan button (rounded) */
    gfx_fill_rounded_rect(btn_x, btn_y, AI_BTN_W, AI_BTN_H, 4, COLOR_BUTTON_PRIMARY);
    font_draw_string((uint32_t)(btn_x + 12), (uint32_t)(btn_y + 2), "Scan", COLOR_TEXT, COLOR_BUTTON_PRIMARY);
    btn_x += AI_BTN_W + AI_BTN_GAP;

    /* Status button (rounded) */
    gfx_fill_rounded_rect(btn_x, btn_y, AI_BTN_W, AI_BTN_H, 4, COLOR_BUTTON);
    font_draw_string((uint32_t)(btn_x + 6), (uint32_t)(btn_y + 2), "Status", COLOR_TEXT, COLOR_BUTTON);
    btn_x += AI_BTN_W + AI_BTN_GAP;

    /* Help button (rounded) */
    gfx_fill_rounded_rect(btn_x, btn_y, AI_BTN_W, AI_BTN_H, 4, COLOR_BUTTON);
    font_draw_string((uint32_t)(btn_x + 10), (uint32_t)(btn_y + 2), "? Help", COLOR_TEXT, COLOR_BUTTON);
}

/*============================================================================
 * Panel Drawing: Bottom Dock
 *============================================================================*/

void panel_draw_dock(const struct app_entry *apps, int count, int hover_idx)
{
    uint32_t w = fb_get_width();

    fb_fill_rect(0, DOCK_Y, w, DOCK_HEIGHT, COLOR_DOCK_BG);
    gfx_draw_hline(0, DOCK_Y, (int)w, COLOR_PANEL_BORDER);

    if (!apps || count <= 0) return;

    /* Center the dock icons */
    int icon_slot = DOCK_ICON_SIZE + 16;  /* 16px icon + 16px padding = 32px slot */
    int total_w = count * icon_slot;
    int start_x = ((int)w - total_w) / 2;

    for (int i = 0; i < count; i++) {
        int ix = start_x + i * icon_slot + 8;
        int iy = DOCK_Y + (DOCK_HEIGHT - DOCK_ICON_SIZE) / 2;

        int is_hover = (i == hover_idx);
        uint32_t slot_bg = is_hover ? 0xFF253040 : COLOR_SIDEBAR_SEL;

        /* Rounded dock icon background */
        gfx_fill_rounded_rect(ix - 4, iy - 4,
                              DOCK_ICON_SIZE + 8, DOCK_ICON_SIZE + 8, 6,
                              slot_bg);

        /* Hover dot indicator above icon */
        if (is_hover) {
            fb_fill_rect((uint32_t)(ix + DOCK_ICON_SIZE / 2 - 1),
                         (uint32_t)(iy - 6), 2, 2, COLOR_HIGHLIGHT);
        }

        /* Draw the dock icon */
        if (apps[i].dock_icon) {
            dock_icon_draw(ix, iy, apps[i].dock_icon, slot_bg);
        }
    }
}

/*============================================================================
 * Panel Drawing: Status Bar
 *============================================================================*/

void panel_draw_statusbar(void)
{
    uint32_t w = fb_get_width();

    /* 2px gradient transition from content bg to status bg */
    gfx_fill_gradient_v(0, STATUS_Y, (int)w, 2, COLOR_CONTENT_BG, COLOR_STATUS_BG);
    fb_fill_rect(0, STATUS_Y + 2, w, STATUS_HEIGHT - 2, COLOR_STATUS_BG);
    gfx_draw_hline(0, STATUS_Y, (int)w, 0xFF1E293B);
    gfx_draw_hline(0, STATUS_Y + 1, (int)w, 0xFF141D2B);

    /* Left text */
    font_draw_string(8, STATUS_Y + 2,
                    "Ready - All data preserved in geology",
                    COLOR_TEXT_DIM, COLOR_STATUS_BG);

    /* Right text */
    const struct pmm_stats *pmm = pmm_get_stats();
    int used_pct = 0;
    if (pmm->total_pages > 0)
        used_pct = (int)(((pmm->total_pages - pmm->free_pages) * 100) / pmm->total_pages);

    char storage[20];
    int pos = 0;
    storage[pos++] = 'S'; storage[pos++] = 't'; storage[pos++] = 'o';
    storage[pos++] = 'r'; storage[pos++] = 'a'; storage[pos++] = 'g';
    storage[pos++] = 'e'; storage[pos++] = ':'; storage[pos++] = ' ';
    if (used_pct >= 10) storage[pos++] = '0' + (char)(used_pct / 10);
    storage[pos++] = '0' + (char)(used_pct % 10);
    storage[pos++] = '%';
    storage[pos] = '\0';

    int str_w = pos * FONT_WIDTH;
    font_draw_string(w - (uint32_t)str_w - 8, STATUS_Y + 2,
                    storage, COLOR_GREEN_ACTIVE, COLOR_STATUS_BG);

    /* Shutdown button: dark red pill left of storage text */
    int pwr_w = 72;
    int pwr_h = 18;
    int pwr_x = (int)w - str_w - 8 - pwr_w - 12;
    int pwr_y = STATUS_Y + 1;
    fb_fill_rect((uint32_t)pwr_x, (uint32_t)pwr_y,
                 (uint32_t)pwr_w, (uint32_t)pwr_h, 0xFF6B1010);
    fb_draw_rect((uint32_t)pwr_x, (uint32_t)pwr_y,
                 (uint32_t)pwr_w, (uint32_t)pwr_h, 0xFFAA3333);
    font_draw_string((uint32_t)pwr_x + 4, (uint32_t)pwr_y + 1,
                     "Shutdown", 0xFFFF9999, 0xFF6B1010);
}

/*============================================================================
 * Hit Testing
 *============================================================================*/

/* Power button hit test (returns 1 if clicked) */
int statusbar_power_hit_test(int mx, int my)
{
    uint32_t w = fb_get_width();

    /* Reconstruct the storage text width to find power button position */
    const struct pmm_stats *pmm = pmm_get_stats();
    int used_pct = 0;
    if (pmm->total_pages > 0)
        used_pct = (int)(((pmm->total_pages - pmm->free_pages) * 100) / pmm->total_pages);
    int str_len = 10;  /* "Storage: X%" = 11 chars min */
    if (used_pct >= 10) str_len = 11;
    int str_w = str_len * FONT_WIDTH;

    int pwr_w = 72;
    int pwr_h = 18;
    int pwr_x = (int)w - str_w - 8 - pwr_w - 12;
    int pwr_y = STATUS_Y + 1;

    return (mx >= pwr_x && mx < pwr_x + pwr_w &&
            my >= pwr_y && my < pwr_y + pwr_h);
}

int sidebar_hit_test(int mx, int my, int selected_category,
                     const struct sidebar_category *cats,
                     int *out_category, int *out_subitem)
{
    if (mx < 0 || mx >= SIDEBAR_WIDTH) return 0;
    if (my < CONTENT_Y || my >= DOCK_Y) return 0;
    if (!cats || !out_category || !out_subitem) return 0;

    *out_category = -1;
    *out_subitem = -1;

    /* Walk the same layout as panel_draw_sidebar */
    int py = CONTENT_Y + 8;

    for (int i = 0; i < SIDEBAR_CAT_COUNT; i++) {
        /* Category header region */
        if (my >= py && my < py + SIDEBAR_CAT_H) {
            *out_category = i;
            return 1;
        }
        py += SIDEBAR_CAT_H;

        /* Sub-items (only if this category is selected/expanded) */
        if (i == selected_category && cats[i].sub_count > 0) {
            for (int j = 0; j < cats[i].sub_count; j++) {
                if (my >= py && my < py + SIDEBAR_SUB_H) {
                    *out_category = i;
                    *out_subitem = j;
                    return 1;
                }
                py += SIDEBAR_SUB_H;
            }
            py += 4;  /* gap after sub-items */
        }
    }
    return 0;
}

int app_grid_hit_test(int mx, int my, int app_count)
{
    if (mx < CENTER_X || mx >= RIGHT_PANEL_X) return -1;
    if (my < CONTENT_Y || my >= DOCK_Y) return -1;

    /* Same grid layout as drawing */
    int grid_w = APP_ICON_COLS * APP_ICON_CELL_W;
    int grid_x = CENTER_X + (CENTER_WIDTH - grid_w) / 2;
    int grid_y = CONTENT_Y + 40;

    int rel_x = mx - grid_x;
    int rel_y = my - grid_y;

    if (rel_x < 0 || rel_y < 0) return -1;

    int col = rel_x / APP_ICON_CELL_W;
    int row = rel_y / APP_ICON_CELL_H;

    if (col >= APP_ICON_COLS) return -1;

    int idx = row * APP_ICON_COLS + col;
    if (idx >= 0 && idx < app_count)
        return idx;
    return -1;
}

int dock_hit_test(int mx, int my, int icon_count)
{
    if (my < DOCK_Y || my >= DOCK_Y + DOCK_HEIGHT) return -1;

    int icon_slot = DOCK_ICON_SIZE + 16;
    int total_w = icon_count * icon_slot;
    int start_x = ((int)fb_get_width() - total_w) / 2;

    int rel_x = mx - start_x;
    if (rel_x < 0) return -1;

    int idx = rel_x / icon_slot;
    if (idx >= 0 && idx < icon_count)
        return idx;
    return -1;
}

int ai_input_hit_test(int mx, int my)
{
    int x = RIGHT_PANEL_X + 8;
    int y = CONTENT_Y + CONTENT_HEIGHT / 2 + (CONTENT_HEIGHT / 2) - 60;
    int w = RIGHT_PANEL_WIDTH - 16;

    return (mx >= x && mx < x + w && my >= y && my < y + AI_INPUT_H);
}

int ai_button_hit_test(int mx, int my)
{
    int y = CONTENT_Y + CONTENT_HEIGHT / 2 + (CONTENT_HEIGHT / 2) - 60 + AI_INPUT_H + 8;
    int btn_x = RIGHT_PANEL_X + 8;

    if (my < y || my >= y + AI_BTN_H) return -1;

    /* Scan */
    if (mx >= btn_x && mx < btn_x + AI_BTN_W) return 0;
    btn_x += AI_BTN_W + AI_BTN_GAP;

    /* Status */
    if (mx >= btn_x && mx < btn_x + AI_BTN_W) return 1;
    btn_x += AI_BTN_W + AI_BTN_GAP;

    /* Help */
    if (mx >= btn_x && mx < btn_x + AI_BTN_W) return 2;

    return -1;
}
