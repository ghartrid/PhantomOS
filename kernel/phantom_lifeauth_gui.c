/*
 * PhantomOS LifeAuth GUI Implementation
 *
 * Visual interface for blood plasma authentication
 *
 * Copyright (c) 2025 PhantomOS Project
 * License: CC BY-NC-SA 4.0
 */

#include "phantom_lifeauth_gui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Layout constants */
#define MARGIN          10
#define PANEL_PADDING   8
#define BAR_HEIGHT      12
#define BAR_GAP         3
#define GAUGE_SIZE      50
#define FP_CELL_SIZE    8
#define FP_GRID_SIZE    8
#define BUTTON_HEIGHT   28

/* Section positions */
#define HEADER_Y        5
#define HEADER_H        25
#define BIOMARKER_Y     35
#define BIOMARKER_H     140
#define LIVENESS_Y      180
#define LIVENESS_H      60
#define FINGERPRINT_Y   245
#define FINGERPRINT_H   70
#define INPUT_Y         320
#define STATUS_Y        355

/*
 * Drawing primitives
 */

static void fill_rect(uint32_t *fb, int fb_w, int fb_h,
                      int x, int y, int w, int h, uint32_t color) {
    for (int py = y; py < y + h && py < fb_h; py++) {
        if (py < 0) continue;
        for (int px = x; px < x + w && px < fb_w; px++) {
            if (px < 0) continue;
            fb[py * fb_w + px] = color;
        }
    }
}

static void draw_rect(uint32_t *fb, int fb_w, int fb_h,
                      int x, int y, int w, int h, uint32_t color) {
    /* Top and bottom */
    for (int px = x; px < x + w && px < fb_w; px++) {
        if (px >= 0) {
            if (y >= 0 && y < fb_h) fb[y * fb_w + px] = color;
            if (y + h - 1 >= 0 && y + h - 1 < fb_h) fb[(y + h - 1) * fb_w + px] = color;
        }
    }
    /* Left and right */
    for (int py = y; py < y + h && py < fb_h; py++) {
        if (py >= 0) {
            if (x >= 0 && x < fb_w) fb[py * fb_w + x] = color;
            if (x + w - 1 >= 0 && x + w - 1 < fb_w) fb[py * fb_w + x + w - 1] = color;
        }
    }
}

static uint32_t blend_color(uint32_t c1, uint32_t c2, float t) {
    if (t <= 0) return c1;
    if (t >= 1) return c2;

    uint8_t r1 = (c1 >> 24) & 0xFF, g1 = (c1 >> 16) & 0xFF;
    uint8_t b1 = (c1 >> 8) & 0xFF, a1 = c1 & 0xFF;
    uint8_t r2 = (c2 >> 24) & 0xFF, g2 = (c2 >> 16) & 0xFF;
    uint8_t b2 = (c2 >> 8) & 0xFF, a2 = c2 & 0xFF;

    uint8_t r = (uint8_t)(r1 + (r2 - r1) * t);
    uint8_t g = (uint8_t)(g1 + (g2 - g1) * t);
    uint8_t b = (uint8_t)(b1 + (b2 - b1) * t);
    uint8_t a = (uint8_t)(a1 + (a2 - a1) * t);

    return (r << 24) | (g << 16) | (b << 8) | a;
}

static uint32_t dim_color(uint32_t color, float factor) {
    uint8_t r = (uint8_t)(((color >> 24) & 0xFF) * factor);
    uint8_t g = (uint8_t)(((color >> 16) & 0xFF) * factor);
    uint8_t b = (uint8_t)(((color >> 8) & 0xFF) * factor);
    uint8_t a = color & 0xFF;
    return (r << 24) | (g << 16) | (b << 8) | a;
}

/* Simple 3x5 font */
static const uint8_t font_3x5[128][5] = {
    ['A'] = {0x7, 0x5, 0x7, 0x5, 0x5}, ['B'] = {0x6, 0x5, 0x6, 0x5, 0x6},
    ['C'] = {0x7, 0x4, 0x4, 0x4, 0x7}, ['D'] = {0x6, 0x5, 0x5, 0x5, 0x6},
    ['E'] = {0x7, 0x4, 0x6, 0x4, 0x7}, ['F'] = {0x7, 0x4, 0x6, 0x4, 0x4},
    ['G'] = {0x7, 0x4, 0x5, 0x5, 0x7}, ['H'] = {0x5, 0x5, 0x7, 0x5, 0x5},
    ['I'] = {0x7, 0x2, 0x2, 0x2, 0x7}, ['J'] = {0x7, 0x1, 0x1, 0x5, 0x7},
    ['K'] = {0x5, 0x5, 0x6, 0x5, 0x5}, ['L'] = {0x4, 0x4, 0x4, 0x4, 0x7},
    ['M'] = {0x5, 0x7, 0x5, 0x5, 0x5}, ['N'] = {0x5, 0x5, 0x7, 0x7, 0x5},
    ['O'] = {0x7, 0x5, 0x5, 0x5, 0x7}, ['P'] = {0x7, 0x5, 0x7, 0x4, 0x4},
    ['Q'] = {0x7, 0x5, 0x5, 0x7, 0x1}, ['R'] = {0x7, 0x5, 0x6, 0x5, 0x5},
    ['S'] = {0x7, 0x4, 0x7, 0x1, 0x7}, ['T'] = {0x7, 0x2, 0x2, 0x2, 0x2},
    ['U'] = {0x5, 0x5, 0x5, 0x5, 0x7}, ['V'] = {0x5, 0x5, 0x5, 0x5, 0x2},
    ['W'] = {0x5, 0x5, 0x5, 0x7, 0x5}, ['X'] = {0x5, 0x5, 0x2, 0x5, 0x5},
    ['Y'] = {0x5, 0x5, 0x2, 0x2, 0x2}, ['Z'] = {0x7, 0x1, 0x2, 0x4, 0x7},
    ['0'] = {0x7, 0x5, 0x5, 0x5, 0x7}, ['1'] = {0x2, 0x6, 0x2, 0x2, 0x7},
    ['2'] = {0x7, 0x1, 0x7, 0x4, 0x7}, ['3'] = {0x7, 0x1, 0x7, 0x1, 0x7},
    ['4'] = {0x5, 0x5, 0x7, 0x1, 0x1}, ['5'] = {0x7, 0x4, 0x7, 0x1, 0x7},
    ['6'] = {0x7, 0x4, 0x7, 0x5, 0x7}, ['7'] = {0x7, 0x1, 0x1, 0x1, 0x1},
    ['8'] = {0x7, 0x5, 0x7, 0x5, 0x7}, ['9'] = {0x7, 0x5, 0x7, 0x1, 0x7},
    [':'] = {0x0, 0x2, 0x0, 0x2, 0x0}, ['.'] = {0x0, 0x0, 0x0, 0x0, 0x2},
    ['%'] = {0x5, 0x1, 0x2, 0x4, 0x5}, ['-'] = {0x0, 0x0, 0x7, 0x0, 0x0},
    [' '] = {0x0, 0x0, 0x0, 0x0, 0x0}, ['!'] = {0x2, 0x2, 0x2, 0x0, 0x2},
    ['*'] = {0x5, 0x2, 0x7, 0x2, 0x5},
    ['a'] = {0x0, 0x7, 0x5, 0x7, 0x5}, ['b'] = {0x4, 0x6, 0x5, 0x5, 0x6},
    ['c'] = {0x0, 0x7, 0x4, 0x4, 0x7}, ['d'] = {0x1, 0x3, 0x5, 0x5, 0x3},
    ['e'] = {0x7, 0x5, 0x7, 0x4, 0x7}, ['f'] = {0x3, 0x4, 0x6, 0x4, 0x4},
    ['g'] = {0x7, 0x5, 0x7, 0x1, 0x7}, ['h'] = {0x4, 0x6, 0x5, 0x5, 0x5},
    ['i'] = {0x2, 0x0, 0x2, 0x2, 0x2}, ['j'] = {0x1, 0x0, 0x1, 0x5, 0x7},
    ['k'] = {0x4, 0x5, 0x6, 0x5, 0x5}, ['l'] = {0x6, 0x2, 0x2, 0x2, 0x7},
    ['m'] = {0x0, 0x5, 0x7, 0x5, 0x5}, ['n'] = {0x0, 0x6, 0x5, 0x5, 0x5},
    ['o'] = {0x0, 0x7, 0x5, 0x5, 0x7}, ['p'] = {0x7, 0x5, 0x7, 0x4, 0x4},
    ['q'] = {0x7, 0x5, 0x7, 0x1, 0x1}, ['r'] = {0x0, 0x7, 0x4, 0x4, 0x4},
    ['s'] = {0x7, 0x4, 0x7, 0x1, 0x7}, ['t'] = {0x4, 0x7, 0x4, 0x4, 0x3},
    ['u'] = {0x0, 0x5, 0x5, 0x5, 0x7}, ['v'] = {0x0, 0x5, 0x5, 0x5, 0x2},
    ['w'] = {0x0, 0x5, 0x5, 0x7, 0x5}, ['x'] = {0x0, 0x5, 0x2, 0x2, 0x5},
    ['y'] = {0x5, 0x5, 0x7, 0x1, 0x7}, ['z'] = {0x7, 0x1, 0x2, 0x4, 0x7},
};

static void draw_char(uint32_t *fb, int fb_w, int fb_h,
                      int x, int y, char c, uint32_t color, int scale) {
    unsigned char uc = (unsigned char)c;
    if (uc > 127) return;
    const uint8_t *glyph = font_3x5[uc];

    for (int row = 0; row < 5; row++) {
        for (int col = 0; col < 3; col++) {
            if (glyph[row] & (4 >> col)) {
                fill_rect(fb, fb_w, fb_h,
                          x + col * scale, y + row * scale,
                          scale, scale, color);
            }
        }
    }
}

static void draw_text(uint32_t *fb, int fb_w, int fb_h,
                      int x, int y, const char *text, uint32_t color, int scale) {
    int cx = x;
    while (*text) {
        draw_char(fb, fb_w, fb_h, cx, y, *text, color, scale);
        cx += 4 * scale;
        text++;
    }
}

static int text_width(const char *text, int scale) {
    return strlen(text) * 4 * scale - scale;
}

static void draw_text_centered(uint32_t *fb, int fb_w, int fb_h,
                               int cx, int y, const char *text,
                               uint32_t color, int scale) {
    int w = text_width(text, scale);
    draw_text(fb, fb_w, fb_h, cx - w / 2, y, text, color, scale);
}

/*
 * Component rendering
 */

static void render_header(lifeauth_gui_t *gui) {
    int y = HEADER_Y;

    /* Title */
    draw_text_centered(gui->framebuffer, gui->fb_width, gui->fb_height,
                       gui->fb_width / 2, y + 5, "LIFEAUTH PLASMA SCANNER",
                       LIFEAUTH_COLOR_TEXT, 1);

    /* State indicator */
    const char *state_text = "READY";
    uint32_t state_color = LIFEAUTH_COLOR_TEXT_DIM;

    switch (gui->state) {
        case LIFEAUTH_GUI_SAMPLING:
            state_text = "SAMPLING...";
            state_color = LIFEAUTH_COLOR_WARNING;
            break;
        case LIFEAUTH_GUI_ANALYZING:
            state_text = "ANALYZING...";
            state_color = LIFEAUTH_COLOR_PROTEIN;
            break;
        case LIFEAUTH_GUI_ENROLLING:
            state_text = "ENROLLING...";
            state_color = LIFEAUTH_COLOR_ANTIBODY;
            break;
        case LIFEAUTH_GUI_AUTHENTICATING:
            state_text = "AUTHENTICATING...";
            state_color = LIFEAUTH_COLOR_METABOLITE;
            break;
        case LIFEAUTH_GUI_SUCCESS:
            state_text = "VERIFIED";
            state_color = LIFEAUTH_COLOR_SUCCESS;
            break;
        case LIFEAUTH_GUI_FAILURE:
            state_text = "FAILED";
            state_color = LIFEAUTH_COLOR_ERROR;
            break;
        case LIFEAUTH_GUI_HEALTH_ALERT:
            state_text = "HEALTH ALERT";
            state_color = LIFEAUTH_COLOR_WARNING;
            break;
        case LIFEAUTH_GUI_LOCKED:
            state_text = "LOCKED";
            state_color = LIFEAUTH_COLOR_ERROR;
            break;
        default:
            break;
    }

    draw_text_centered(gui->framebuffer, gui->fb_width, gui->fb_height,
                       gui->fb_width / 2, y + 15, state_text, state_color, 1);
}

static void render_biomarker_section(lifeauth_gui_t *gui) {
    int x = MARGIN;
    int y = BIOMARKER_Y;
    int section_w = (gui->fb_width - MARGIN * 3) / 2;

    /* Panel backgrounds */
    fill_rect(gui->framebuffer, gui->fb_width, gui->fb_height,
              x, y, section_w, BIOMARKER_H / 2 - 2, LIFEAUTH_COLOR_PANEL);
    fill_rect(gui->framebuffer, gui->fb_width, gui->fb_height,
              x + section_w + MARGIN, y, section_w, BIOMARKER_H / 2 - 2, LIFEAUTH_COLOR_PANEL);
    fill_rect(gui->framebuffer, gui->fb_width, gui->fb_height,
              x, y + BIOMARKER_H / 2 + 2, section_w, BIOMARKER_H / 2 - 2, LIFEAUTH_COLOR_PANEL);
    fill_rect(gui->framebuffer, gui->fb_width, gui->fb_height,
              x + section_w + MARGIN, y + BIOMARKER_H / 2 + 2, section_w, BIOMARKER_H / 2 - 2, LIFEAUTH_COLOR_PANEL);

    /* Section labels */
    draw_text(gui->framebuffer, gui->fb_width, gui->fb_height,
              x + 3, y + 3, "PROTEINS", LIFEAUTH_COLOR_PROTEIN, 1);
    draw_text(gui->framebuffer, gui->fb_width, gui->fb_height,
              x + section_w + MARGIN + 3, y + 3, "ANTIBODIES", LIFEAUTH_COLOR_ANTIBODY, 1);
    draw_text(gui->framebuffer, gui->fb_width, gui->fb_height,
              x + 3, y + BIOMARKER_H / 2 + 5, "METABOLITES", LIFEAUTH_COLOR_METABOLITE, 1);
    draw_text(gui->framebuffer, gui->fb_width, gui->fb_height,
              x + section_w + MARGIN + 3, y + BIOMARKER_H / 2 + 5, "ENZYMES", LIFEAUTH_COLOR_ENZYME, 1);

    /* Protein bars */
    int bar_y = y + 15;
    int bar_w = section_w - 10;
    for (int i = 0; i < LIFEAUTH_VIS_PROTEINS; i++) {
        int by = bar_y + i * (BAR_HEIGHT + BAR_GAP);
        /* Background */
        fill_rect(gui->framebuffer, gui->fb_width, gui->fb_height,
                  x + 5, by, bar_w, BAR_HEIGHT, dim_color(LIFEAUTH_COLOR_PROTEIN, 0.2f));
        /* Value bar */
        int fill_w = (int)(bar_w * gui->proteins[i].value);
        uint32_t bar_color = gui->proteins[i].is_abnormal ? LIFEAUTH_COLOR_ERROR : LIFEAUTH_COLOR_PROTEIN;
        fill_rect(gui->framebuffer, gui->fb_width, gui->fb_height,
                  x + 5, by, fill_w, BAR_HEIGHT, bar_color);
        /* Baseline marker */
        int baseline_x = x + 5 + (int)(bar_w * gui->proteins[i].baseline);
        fill_rect(gui->framebuffer, gui->fb_width, gui->fb_height,
                  baseline_x, by, 2, BAR_HEIGHT, LIFEAUTH_COLOR_TEXT);
    }

    /* Antibody bars */
    int ab_x = x + section_w + MARGIN + 5;
    for (int i = 0; i < LIFEAUTH_VIS_ANTIBODIES; i++) {
        int by = bar_y + i * (BAR_HEIGHT + BAR_GAP);
        fill_rect(gui->framebuffer, gui->fb_width, gui->fb_height,
                  ab_x, by, bar_w, BAR_HEIGHT, dim_color(LIFEAUTH_COLOR_ANTIBODY, 0.2f));
        int fill_w = (int)(bar_w * gui->antibodies[i].value);
        uint32_t bar_color = gui->antibodies[i].is_abnormal ? LIFEAUTH_COLOR_ERROR : LIFEAUTH_COLOR_ANTIBODY;
        fill_rect(gui->framebuffer, gui->fb_width, gui->fb_height,
                  ab_x, by, fill_w, BAR_HEIGHT, bar_color);
        int baseline_x = ab_x + (int)(bar_w * gui->antibodies[i].baseline);
        fill_rect(gui->framebuffer, gui->fb_width, gui->fb_height,
                  baseline_x, by, 2, BAR_HEIGHT, LIFEAUTH_COLOR_TEXT);
    }

    /* Metabolite bars */
    bar_y = y + BIOMARKER_H / 2 + 17;
    for (int i = 0; i < LIFEAUTH_VIS_METABOLITES; i++) {
        int by = bar_y + i * (BAR_HEIGHT + BAR_GAP);
        fill_rect(gui->framebuffer, gui->fb_width, gui->fb_height,
                  x + 5, by, bar_w, BAR_HEIGHT, dim_color(LIFEAUTH_COLOR_METABOLITE, 0.2f));
        int fill_w = (int)(bar_w * gui->metabolites[i].value);
        uint32_t bar_color = gui->metabolites[i].is_abnormal ? LIFEAUTH_COLOR_ERROR : LIFEAUTH_COLOR_METABOLITE;
        fill_rect(gui->framebuffer, gui->fb_width, gui->fb_height,
                  x + 5, by, fill_w, BAR_HEIGHT, bar_color);
    }

    /* Enzyme bars */
    for (int i = 0; i < LIFEAUTH_VIS_ENZYMES; i++) {
        int by = bar_y + i * (BAR_HEIGHT + BAR_GAP);
        fill_rect(gui->framebuffer, gui->fb_width, gui->fb_height,
                  ab_x, by, bar_w, BAR_HEIGHT, dim_color(LIFEAUTH_COLOR_ENZYME, 0.2f));
        int fill_w = (int)(bar_w * gui->enzymes[i].value);
        uint32_t bar_color = gui->enzymes[i].is_abnormal ? LIFEAUTH_COLOR_ERROR : LIFEAUTH_COLOR_ENZYME;
        fill_rect(gui->framebuffer, gui->fb_width, gui->fb_height,
                  ab_x, by, fill_w, BAR_HEIGHT, bar_color);
    }
}

static void render_liveness_section(lifeauth_gui_t *gui) {
    int y = LIVENESS_Y;
    int gauge_spacing = (gui->fb_width - MARGIN * 2) / 4;

    /* Background panel */
    fill_rect(gui->framebuffer, gui->fb_width, gui->fb_height,
              MARGIN, y, gui->fb_width - MARGIN * 2, LIVENESS_H, LIFEAUTH_COLOR_PANEL);

    draw_text(gui->framebuffer, gui->fb_width, gui->fb_height,
              MARGIN + 5, y + 3, "LIVENESS", LIFEAUTH_COLOR_TEXT_DIM, 1);

    /* Gauge positions */
    int gauge_y = y + 15;
    lifeauth_gauge_t *gauges[] = {
        &gui->pulse_gauge, &gui->temp_gauge, &gui->spo2_gauge, &gui->activity_gauge
    };

    for (int i = 0; i < 4; i++) {
        int cx = MARGIN + gauge_spacing / 2 + i * gauge_spacing;
        lifeauth_gauge_t *g = gauges[i];

        /* Gauge background arc (simplified as rectangle) */
        int gauge_w = 45;
        int gauge_h = 25;
        fill_rect(gui->framebuffer, gui->fb_width, gui->fb_height,
                  cx - gauge_w / 2, gauge_y, gauge_w, gauge_h,
                  dim_color(g->color, 0.2f));

        /* Gauge fill */
        int fill_w = (int)(gauge_w * g->value);
        fill_rect(gui->framebuffer, gui->fb_width, gui->fb_height,
                  cx - gauge_w / 2, gauge_y, fill_w, gauge_h, g->color);

        /* Label */
        draw_text_centered(gui->framebuffer, gui->fb_width, gui->fb_height,
                           cx, gauge_y + gauge_h + 5, g->label, LIFEAUTH_COLOR_TEXT_DIM, 1);

        /* Value text */
        char val_str[16];
        snprintf(val_str, sizeof(val_str), "%d%%", (int)(g->value * 100));
        draw_text_centered(gui->framebuffer, gui->fb_width, gui->fb_height,
                           cx, gauge_y + 8, val_str, LIFEAUTH_COLOR_TEXT, 1);
    }
}

static void render_fingerprint_section(lifeauth_gui_t *gui) {
    int y = FINGERPRINT_Y;
    int fp_total_size = FP_GRID_SIZE * FP_CELL_SIZE + (FP_GRID_SIZE - 1) * 2;
    int fp_x = (gui->fb_width - fp_total_size) / 2 - 80;

    /* Background panel */
    fill_rect(gui->framebuffer, gui->fb_width, gui->fb_height,
              MARGIN, y, gui->fb_width - MARGIN * 2, FINGERPRINT_H, LIFEAUTH_COLOR_PANEL);

    draw_text(gui->framebuffer, gui->fb_width, gui->fb_height,
              MARGIN + 5, y + 3, "PLASMA FINGERPRINT", LIFEAUTH_COLOR_FINGERPRINT, 1);

    /* Fingerprint grid */
    int fp_y = y + 15;
    for (int row = 0; row < FP_GRID_SIZE; row++) {
        for (int col = 0; col < FP_GRID_SIZE; col++) {
            int idx = row * FP_GRID_SIZE + col;
            int cx = fp_x + col * (FP_CELL_SIZE + 2);
            int cy = fp_y + row * (FP_CELL_SIZE + 2);

            /* Apply reveal animation */
            float reveal = gui->fp_reveal_progress;
            float cell_reveal = (reveal * 64.0f - idx) / 8.0f;
            if (cell_reveal < 0) cell_reveal = 0;
            if (cell_reveal > 1) cell_reveal = 1;

            uint8_t intensity = (uint8_t)(gui->fingerprint[idx].value * cell_reveal);
            uint32_t cell_color = blend_color(LIFEAUTH_COLOR_PANEL, LIFEAUTH_COLOR_FINGERPRINT,
                                               intensity / 255.0f);

            fill_rect(gui->framebuffer, gui->fb_width, gui->fb_height,
                      cx, cy, FP_CELL_SIZE, FP_CELL_SIZE, cell_color);
        }
    }

    /* Similarity meter */
    int meter_x = fp_x + fp_total_size + 30;
    int meter_w = gui->fb_width - meter_x - MARGIN - 10;
    int meter_y = fp_y + 10;

    draw_text(gui->framebuffer, gui->fb_width, gui->fb_height,
              meter_x, meter_y - 8, "MATCH", LIFEAUTH_COLOR_TEXT_DIM, 1);

    /* Meter background */
    fill_rect(gui->framebuffer, gui->fb_width, gui->fb_height,
              meter_x, meter_y, meter_w, 20, dim_color(LIFEAUTH_COLOR_SUCCESS, 0.2f));

    /* Meter fill */
    int fill_w = (int)(meter_w * gui->similarity_value);
    uint32_t meter_color = gui->similarity_value >= 0.85f ? LIFEAUTH_COLOR_SUCCESS :
                           gui->similarity_value >= 0.5f ? LIFEAUTH_COLOR_WARNING :
                           LIFEAUTH_COLOR_ERROR;
    fill_rect(gui->framebuffer, gui->fb_width, gui->fb_height,
              meter_x, meter_y, fill_w, 20, meter_color);

    /* Percentage text */
    char pct_str[16];
    snprintf(pct_str, sizeof(pct_str), "%d%%", (int)(gui->similarity_value * 100));
    draw_text_centered(gui->framebuffer, gui->fb_width, gui->fb_height,
                       meter_x + meter_w / 2, meter_y + 6, pct_str, LIFEAUTH_COLOR_TEXT, 1);

    /* Threshold marker */
    int thresh_x = meter_x + (int)(meter_w * 0.85f);
    fill_rect(gui->framebuffer, gui->fb_width, gui->fb_height,
              thresh_x, meter_y - 2, 2, 24, LIFEAUTH_COLOR_TEXT);

    /* Quality indicator */
    draw_text(gui->framebuffer, gui->fb_width, gui->fb_height,
              meter_x, meter_y + 28, "QUALITY:", LIFEAUTH_COLOR_TEXT_DIM, 1);

    const char *quality_text = gui->sample_quality >= 0.9f ? "EXCELLENT" :
                               gui->sample_quality >= 0.7f ? "GOOD" :
                               gui->sample_quality >= 0.5f ? "FAIR" : "POOR";
    uint32_t quality_color = gui->sample_quality >= 0.7f ? LIFEAUTH_COLOR_SUCCESS :
                             gui->sample_quality >= 0.5f ? LIFEAUTH_COLOR_WARNING :
                             LIFEAUTH_COLOR_ERROR;
    draw_text(gui->framebuffer, gui->fb_width, gui->fb_height,
              meter_x + 40, meter_y + 28, quality_text, quality_color, 1);
}

static void render_input_section(lifeauth_gui_t *gui) {
    int y = INPUT_Y;
    int input_w = 120;
    int btn_w = 70;

    /* Username input */
    int ux = MARGIN + 5;
    fill_rect(gui->framebuffer, gui->fb_width, gui->fb_height,
              ux, y, input_w, 18, gui->username_input.is_focused ?
              LIFEAUTH_COLOR_BORDER : LIFEAUTH_COLOR_PANEL);
    draw_rect(gui->framebuffer, gui->fb_width, gui->fb_height,
              ux, y, input_w, 18, LIFEAUTH_COLOR_BORDER);

    const char *utext = gui->username_input.text[0] ? gui->username_input.text : "Username";
    uint32_t ucolor = gui->username_input.text[0] ? LIFEAUTH_COLOR_TEXT : LIFEAUTH_COLOR_TEXT_DIM;
    draw_text(gui->framebuffer, gui->fb_width, gui->fb_height,
              ux + 3, y + 5, utext, ucolor, 1);

    /* Password input */
    int px = ux + input_w + 10;
    fill_rect(gui->framebuffer, gui->fb_width, gui->fb_height,
              px, y, input_w, 18, gui->password_input.is_focused ?
              LIFEAUTH_COLOR_BORDER : LIFEAUTH_COLOR_PANEL);
    draw_rect(gui->framebuffer, gui->fb_width, gui->fb_height,
              px, y, input_w, 18, LIFEAUTH_COLOR_BORDER);

    if (gui->password_input.text[0]) {
        /* Show asterisks */
        char masked[64];
        int len = strlen(gui->password_input.text);
        if (len > 63) len = 63;
        memset(masked, '*', len);
        masked[len] = '\0';
        draw_text(gui->framebuffer, gui->fb_width, gui->fb_height,
                  px + 3, y + 5, masked, LIFEAUTH_COLOR_TEXT, 1);
    } else {
        draw_text(gui->framebuffer, gui->fb_width, gui->fb_height,
                  px + 3, y + 5, "Password", LIFEAUTH_COLOR_TEXT_DIM, 1);
    }

    /* Buttons */
    int bx = px + input_w + 15;

    /* Enroll button */
    uint32_t enroll_color = gui->enroll_btn.is_hovered ? LIFEAUTH_COLOR_ANTIBODY :
                            dim_color(LIFEAUTH_COLOR_ANTIBODY, 0.6f);
    fill_rect(gui->framebuffer, gui->fb_width, gui->fb_height,
              bx, y - 2, btn_w, 22, enroll_color);
    draw_text_centered(gui->framebuffer, gui->fb_width, gui->fb_height,
                       bx + btn_w / 2, y + 5, "ENROLL", LIFEAUTH_COLOR_TEXT, 1);
    gui->enroll_btn.x = bx;
    gui->enroll_btn.y = y - 2;
    gui->enroll_btn.width = btn_w;
    gui->enroll_btn.height = 22;

    /* Auth button */
    bx += btn_w + 8;
    uint32_t auth_color = gui->auth_btn.is_hovered ? LIFEAUTH_COLOR_SUCCESS :
                          dim_color(LIFEAUTH_COLOR_SUCCESS, 0.6f);
    fill_rect(gui->framebuffer, gui->fb_width, gui->fb_height,
              bx, y - 2, btn_w, 22, auth_color);
    draw_text_centered(gui->framebuffer, gui->fb_width, gui->fb_height,
                       bx + btn_w / 2, y + 5, "AUTH", LIFEAUTH_COLOR_TEXT, 1);
    gui->auth_btn.x = bx;
    gui->auth_btn.y = y - 2;
    gui->auth_btn.width = btn_w;
    gui->auth_btn.height = 22;
}

static void render_status(lifeauth_gui_t *gui) {
    if (gui->status_message[0] && gui->status_fade > 0.1f) {
        uint32_t color = gui->status_color;
        uint8_t a = (uint8_t)(255 * gui->status_fade);
        color = (color & 0xFFFFFF00) | a;

        draw_text_centered(gui->framebuffer, gui->fb_width, gui->fb_height,
                           gui->fb_width / 2, STATUS_Y, gui->status_message,
                           color, 1);
    }

    /* Health alert */
    if (gui->health_alert.active && gui->health_alert.fade > 0.1f) {
        uint32_t alert_color = blend_color(LIFEAUTH_COLOR_BG, LIFEAUTH_COLOR_WARNING,
                                            gui->health_alert.fade * 0.3f);
        fill_rect(gui->framebuffer, gui->fb_width, gui->fb_height,
                  MARGIN, STATUS_Y - 15, gui->fb_width - MARGIN * 2, 20, alert_color);
        draw_text_centered(gui->framebuffer, gui->fb_width, gui->fb_height,
                           gui->fb_width / 2, STATUS_Y - 10, gui->health_alert.message,
                           LIFEAUTH_COLOR_WARNING, 1);
    }
}

/*
 * Public API
 */

lifeauth_gui_t* lifeauth_gui_create(int x, int y) {
    lifeauth_gui_t *gui = calloc(1, sizeof(lifeauth_gui_t));
    if (!gui) return NULL;

    gui->fb_width = LIFEAUTH_GUI_WIDTH;
    gui->fb_height = LIFEAUTH_GUI_HEIGHT;
    gui->framebuffer = calloc(gui->fb_width * gui->fb_height, sizeof(uint32_t));
    if (!gui->framebuffer) {
        free(gui);
        return NULL;
    }

    gui->window_x = x;
    gui->window_y = y;
    gui->is_visible = true;
    gui->state = LIFEAUTH_GUI_IDLE;

    /* Initialize gauges */
    strcpy(gui->pulse_gauge.label, "PULSE");
    gui->pulse_gauge.color = LIFEAUTH_COLOR_PULSE;
    gui->pulse_gauge.value = 0.0f;

    strcpy(gui->temp_gauge.label, "TEMP");
    gui->temp_gauge.color = LIFEAUTH_COLOR_METABOLITE;
    gui->temp_gauge.value = 0.0f;

    strcpy(gui->spo2_gauge.label, "SPO2");
    gui->spo2_gauge.color = LIFEAUTH_COLOR_PROTEIN;
    gui->spo2_gauge.value = 0.0f;

    strcpy(gui->activity_gauge.label, "ACTIVE");
    gui->activity_gauge.color = LIFEAUTH_COLOR_ENZYME;
    gui->activity_gauge.value = 0.0f;

    /* Initialize biomarker bars with baselines */
    for (int i = 0; i < LIFEAUTH_VIS_PROTEINS; i++) {
        gui->proteins[i].baseline = 0.5f + (float)(i % 3) * 0.15f;
        gui->proteins[i].color = LIFEAUTH_COLOR_PROTEIN;
    }
    for (int i = 0; i < LIFEAUTH_VIS_ANTIBODIES; i++) {
        gui->antibodies[i].baseline = 0.4f + (float)(i % 4) * 0.12f;
        gui->antibodies[i].color = LIFEAUTH_COLOR_ANTIBODY;
    }
    for (int i = 0; i < LIFEAUTH_VIS_METABOLITES; i++) {
        gui->metabolites[i].baseline = 0.3f + (float)(i % 5) * 0.1f;
        gui->metabolites[i].color = LIFEAUTH_COLOR_METABOLITE;
    }
    for (int i = 0; i < LIFEAUTH_VIS_ENZYMES; i++) {
        gui->enzymes[i].baseline = 0.35f + (float)(i % 3) * 0.2f;
        gui->enzymes[i].color = LIFEAUTH_COLOR_ENZYME;
    }

    /* Initialize buttons */
    gui->enroll_btn.is_enabled = true;
    gui->auth_btn.is_enabled = true;

    /* Open driver */
    lifeauth_init(NULL);
    lifeauth_open("/dev/lifeauth0", &gui->driver);

    return gui;
}

void lifeauth_gui_destroy(lifeauth_gui_t *gui) {
    if (!gui) return;

    if (gui->driver) {
        lifeauth_close(gui->driver);
    }
    if (gui->current_signature) {
        free(gui->current_signature);
    }
    if (gui->credential) {
        free(gui->credential);
    }
    if (gui->framebuffer) {
        free(gui->framebuffer);
    }
    free(gui);
}

void lifeauth_gui_render(lifeauth_gui_t *gui) {
    if (!gui || !gui->framebuffer) return;

    /* Clear background */
    for (int i = 0; i < gui->fb_width * gui->fb_height; i++) {
        gui->framebuffer[i] = LIFEAUTH_COLOR_BG;
    }

    /* Render sections */
    render_header(gui);
    render_biomarker_section(gui);
    render_liveness_section(gui);
    render_fingerprint_section(gui);
    render_input_section(gui);
    render_status(gui);

    /* Border */
    draw_rect(gui->framebuffer, gui->fb_width, gui->fb_height,
              0, 0, gui->fb_width, gui->fb_height, LIFEAUTH_COLOR_BORDER);
}

void lifeauth_gui_update(lifeauth_gui_t *gui, uint32_t delta_ms) {
    if (!gui) return;

    float dt = delta_ms / 1000.0f;

    /* Animate biomarker bars */
    for (int i = 0; i < LIFEAUTH_VIS_PROTEINS; i++) {
        gui->proteins[i].value += (gui->proteins[i].target - gui->proteins[i].value) * dt * 5.0f;
    }
    for (int i = 0; i < LIFEAUTH_VIS_ANTIBODIES; i++) {
        gui->antibodies[i].value += (gui->antibodies[i].target - gui->antibodies[i].value) * dt * 5.0f;
    }
    for (int i = 0; i < LIFEAUTH_VIS_METABOLITES; i++) {
        gui->metabolites[i].value += (gui->metabolites[i].target - gui->metabolites[i].value) * dt * 5.0f;
    }
    for (int i = 0; i < LIFEAUTH_VIS_ENZYMES; i++) {
        gui->enzymes[i].value += (gui->enzymes[i].target - gui->enzymes[i].value) * dt * 5.0f;
    }

    /* Animate gauges */
    gui->pulse_gauge.value += (gui->pulse_gauge.target - gui->pulse_gauge.value) * dt * 8.0f;
    gui->temp_gauge.value += (gui->temp_gauge.target - gui->temp_gauge.value) * dt * 8.0f;
    gui->spo2_gauge.value += (gui->spo2_gauge.target - gui->spo2_gauge.value) * dt * 8.0f;
    gui->activity_gauge.value += (gui->activity_gauge.target - gui->activity_gauge.value) * dt * 8.0f;

    /* Animate fingerprint reveal */
    if (gui->fp_reveal_progress < 1.0f && gui->sample_ready) {
        gui->fp_reveal_progress += dt * 0.5f;
        if (gui->fp_reveal_progress > 1.0f) gui->fp_reveal_progress = 1.0f;
    }

    /* Animate similarity meter */
    gui->similarity_value += (gui->similarity_target - gui->similarity_value) * dt * 3.0f;

    /* Fade status */
    if (gui->status_fade > 0) {
        gui->status_fade -= dt * 0.3f;
        if (gui->status_fade < 0) gui->status_fade = 0;
    }

    /* Fade health alert */
    if (gui->health_alert.fade > 0 && !gui->health_alert.active) {
        gui->health_alert.fade -= dt * 0.5f;
    }
}

uint32_t* lifeauth_gui_get_framebuffer(lifeauth_gui_t *gui) {
    return gui ? gui->framebuffer : NULL;
}

void lifeauth_gui_set_status(lifeauth_gui_t *gui, const char *message, uint32_t color) {
    if (!gui) return;
    strncpy(gui->status_message, message, sizeof(gui->status_message) - 1);
    gui->status_message[sizeof(gui->status_message) - 1] = '\0';
    gui->status_color = color;
    gui->status_fade = 1.0f;
}

void lifeauth_gui_show_health_alert(lifeauth_gui_t *gui, const char *message) {
    if (!gui) return;
    strncpy(gui->health_alert.message, message, sizeof(gui->health_alert.message) - 1);
    gui->health_alert.message[sizeof(gui->health_alert.message) - 1] = '\0';
    gui->health_alert.active = true;
    gui->health_alert.fade = 1.0f;
}

void lifeauth_gui_update_from_signature(lifeauth_gui_t *gui,
                                         const lifeauth_plasma_signature_t *sig) {
    if (!gui || !sig) return;

    /* Update protein bars */
    gui->proteins[0].target = sig->proteins.albumin.value / 5.0f;
    gui->proteins[1].target = sig->proteins.ag_ratio / 2.5f;
    gui->proteins[2].target = sig->proteins.alpha1_globulin.value / 0.4f;
    gui->proteins[3].target = sig->proteins.alpha2_globulin.value / 1.0f;
    gui->proteins[4].target = sig->proteins.beta_globulin.value / 1.2f;
    gui->proteins[5].target = sig->proteins.gamma_globulin.value / 1.5f;
    gui->proteins[6].target = sig->proteins.fibrinogen.value / 500.0f;
    gui->proteins[7].target = sig->proteins.transferrin.value / 350.0f;

    /* Update antibody bars (IgG subclass ratios) */
    for (int i = 0; i < 4; i++) {
        gui->antibodies[i].target = sig->antibodies.igg_subclass_ratios[i];
    }
    gui->antibodies[4].target = sig->antibodies.igg_total.value / 1500.0f;
    gui->antibodies[5].target = sig->antibodies.iga_total.value / 400.0f;

    /* Update metabolite bars */
    gui->metabolites[0].target = sig->metabolites.glucose.value / 200.0f;
    gui->metabolites[1].target = sig->metabolites.urea.value / 40.0f;
    gui->metabolites[2].target = sig->metabolites.creatinine.value / 2.0f;
    gui->metabolites[3].target = sig->metabolites.uric_acid.value / 10.0f;
    gui->metabolites[4].target = sig->metabolites.bilirubin.value / 2.0f;
    for (int i = 5; i < LIFEAUTH_VIS_METABOLITES; i++) {
        gui->metabolites[i].target = 0.3f + (float)(i % 3) * 0.2f;
    }

    /* Update enzyme bars */
    gui->enzymes[0].target = sig->enzymes.alt.value / 60.0f;
    gui->enzymes[1].target = sig->enzymes.ast.value / 50.0f;
    gui->enzymes[2].target = sig->enzymes.alp.value / 140.0f;
    gui->enzymes[3].target = sig->enzymes.ggt.value / 80.0f;
    gui->enzymes[4].target = sig->enzymes.ldh.value / 300.0f;
    gui->enzymes[5].target = sig->enzymes.enzyme_signature[5];

    /* Update liveness gauges */
    gui->pulse_gauge.target = 0.95f;
    gui->temp_gauge.target = 0.92f;
    gui->spo2_gauge.target = 0.97f;
    gui->activity_gauge.target = 0.88f;

    /* Update fingerprint from signature */
    for (int i = 0; i < 64; i++) {
        gui->fingerprint[i].value = sig->plasma_fingerprint[i];
    }

    gui->sample_quality = sig->overall_confidence;
    gui->sample_ready = true;
}

void lifeauth_gui_start_sample(lifeauth_gui_t *gui) {
    if (!gui || !gui->driver) return;

    gui->state = LIFEAUTH_GUI_SAMPLING;
    gui->fp_reveal_progress = 0.0f;
    gui->sample_ready = false;

    lifeauth_gui_set_status(gui, "Place finger on sensor...", LIFEAUTH_COLOR_WARNING);

    /* Collect sample */
    if (!gui->current_signature) {
        gui->current_signature = calloc(1, sizeof(lifeauth_plasma_signature_t));
    }

    lifeauth_sample_quality_t quality;
    lifeauth_error_t err = lifeauth_sample(gui->driver, gui->current_signature, &quality);

    if (err == LIFEAUTH_OK) {
        gui->state = LIFEAUTH_GUI_ANALYZING;
        lifeauth_gui_update_from_signature(gui, gui->current_signature);
        lifeauth_gui_set_status(gui, "Sample collected successfully", LIFEAUTH_COLOR_SUCCESS);
    } else {
        gui->state = LIFEAUTH_GUI_FAILURE;
        lifeauth_gui_set_status(gui, "Sample collection failed", LIFEAUTH_COLOR_ERROR);
    }
}

void lifeauth_gui_start_enroll(lifeauth_gui_t *gui) {
    if (!gui || !gui->driver) return;

    if (strlen(gui->username_input.text) == 0 || strlen(gui->password_input.text) == 0) {
        lifeauth_gui_set_status(gui, "Enter username and password", LIFEAUTH_COLOR_WARNING);
        return;
    }

    gui->state = LIFEAUTH_GUI_ENROLLING;
    lifeauth_gui_set_status(gui, "Enrolling...", LIFEAUTH_COLOR_METABOLITE);

    if (!gui->credential) {
        gui->credential = calloc(1, sizeof(lifeauth_credential_t));
    }

    lifeauth_error_t err = lifeauth_enroll(gui->driver,
                                            gui->username_input.text,
                                            (uint8_t*)gui->password_input.text,
                                            strlen(gui->password_input.text),
                                            gui->credential);

    if (err == LIFEAUTH_OK) {
        gui->state = LIFEAUTH_GUI_SUCCESS;
        lifeauth_gui_set_status(gui, "Enrollment successful!", LIFEAUTH_COLOR_SUCCESS);

        /* Update display from enrolled signature */
        if (gui->current_signature) {
            lifeauth_gui_update_from_signature(gui, gui->current_signature);
        }

        gui->similarity_target = 1.0f;

        if (gui->on_enroll_complete) {
            gui->on_enroll_complete(true, gui->callback_userdata);
        }
    } else {
        gui->state = LIFEAUTH_GUI_FAILURE;
        lifeauth_gui_set_status(gui, lifeauth_error_string(err), LIFEAUTH_COLOR_ERROR);

        if (gui->on_enroll_complete) {
            gui->on_enroll_complete(false, gui->callback_userdata);
        }
    }
}

void lifeauth_gui_start_auth(lifeauth_gui_t *gui, lifeauth_credential_t *cred) {
    if (!gui || !gui->driver) return;

    lifeauth_credential_t *use_cred = cred ? cred : gui->credential;
    if (!use_cred) {
        lifeauth_gui_set_status(gui, "No credential to authenticate", LIFEAUTH_COLOR_ERROR);
        return;
    }

    if (strlen(gui->password_input.text) == 0) {
        lifeauth_gui_set_status(gui, "Enter password", LIFEAUTH_COLOR_WARNING);
        return;
    }

    gui->state = LIFEAUTH_GUI_AUTHENTICATING;
    lifeauth_gui_set_status(gui, "Authenticating...", LIFEAUTH_COLOR_PROTEIN);

    lifeauth_match_result_t result;
    lifeauth_error_t err = lifeauth_authenticate(gui->driver, use_cred,
                                                  (uint8_t*)gui->password_input.text,
                                                  strlen(gui->password_input.text),
                                                  &result);

    gui->similarity_target = result.overall_similarity;

    /* Update visualization */
    if (gui->current_signature) {
        lifeauth_gui_update_from_signature(gui, gui->current_signature);
    }

    if (err == LIFEAUTH_OK && result.is_match) {
        gui->state = LIFEAUTH_GUI_SUCCESS;
        lifeauth_gui_set_status(gui, "Authentication successful!", LIFEAUTH_COLOR_SUCCESS);

        /* Check for health alert */
        if (result.health_alert) {
            gui->state = LIFEAUTH_GUI_HEALTH_ALERT;
            lifeauth_gui_show_health_alert(gui, result.health_message);
        }

        if (gui->on_auth_complete) {
            gui->on_auth_complete(true, gui->callback_userdata);
        }
    } else if (err == LIFEAUTH_ERR_LOCKED) {
        gui->state = LIFEAUTH_GUI_LOCKED;
        lifeauth_gui_set_status(gui, "Account locked", LIFEAUTH_COLOR_ERROR);

        if (gui->on_auth_complete) {
            gui->on_auth_complete(false, gui->callback_userdata);
        }
    } else {
        gui->state = LIFEAUTH_GUI_FAILURE;
        lifeauth_gui_set_status(gui, "Authentication failed", LIFEAUTH_COLOR_ERROR);

        if (gui->on_auth_complete) {
            gui->on_auth_complete(false, gui->callback_userdata);
        }
    }
}

void lifeauth_gui_cancel(lifeauth_gui_t *gui) {
    if (!gui) return;
    gui->state = LIFEAUTH_GUI_IDLE;
    lifeauth_gui_set_status(gui, "Cancelled", LIFEAUTH_COLOR_TEXT_DIM);
}

void lifeauth_gui_set_callbacks(lifeauth_gui_t *gui,
                                 void (*on_enroll)(bool success, void *data),
                                 void (*on_auth)(bool success, void *data),
                                 void *userdata) {
    if (!gui) return;
    gui->on_enroll_complete = on_enroll;
    gui->on_auth_complete = on_auth;
    gui->callback_userdata = userdata;
}

lifeauth_credential_t* lifeauth_gui_get_credential(lifeauth_gui_t *gui) {
    return gui ? gui->credential : NULL;
}

/* Input handlers */
void lifeauth_gui_mouse_move(lifeauth_gui_t *gui, int x, int y) {
    if (!gui) return;

    /* Check button hovers */
    gui->enroll_btn.is_hovered = (x >= gui->enroll_btn.x &&
                                   x < gui->enroll_btn.x + gui->enroll_btn.width &&
                                   y >= gui->enroll_btn.y &&
                                   y < gui->enroll_btn.y + gui->enroll_btn.height);

    gui->auth_btn.is_hovered = (x >= gui->auth_btn.x &&
                                 x < gui->auth_btn.x + gui->auth_btn.width &&
                                 y >= gui->auth_btn.y &&
                                 y < gui->auth_btn.y + gui->auth_btn.height);
}

void lifeauth_gui_mouse_down(lifeauth_gui_t *gui, int x, int y, int button) {
    if (!gui || button != 1) return;

    /* Check input field clicks */
    int ux = MARGIN + 5;
    int px = ux + 130;
    int iy = INPUT_Y;

    if (x >= ux && x < ux + 120 && y >= iy && y < iy + 18) {
        gui->username_input.is_focused = true;
        gui->password_input.is_focused = false;
    } else if (x >= px && x < px + 120 && y >= iy && y < iy + 18) {
        gui->username_input.is_focused = false;
        gui->password_input.is_focused = true;
    } else {
        gui->username_input.is_focused = false;
        gui->password_input.is_focused = false;
    }

    /* Check button clicks */
    if (gui->enroll_btn.is_hovered) {
        lifeauth_gui_start_enroll(gui);
    } else if (gui->auth_btn.is_hovered) {
        lifeauth_gui_start_auth(gui, NULL);
    }
}

void lifeauth_gui_mouse_up(lifeauth_gui_t *gui, int x, int y, int button) {
    (void)gui; (void)x; (void)y; (void)button;
}

void lifeauth_gui_key_down(lifeauth_gui_t *gui, int keycode, int modifiers) {
    (void)modifiers;
    if (!gui) return;

    lifeauth_input_t *input = gui->username_input.is_focused ? &gui->username_input :
                               gui->password_input.is_focused ? &gui->password_input : NULL;

    if (!input) return;

    if (keycode == 8) { /* Backspace */
        int len = strlen(input->text);
        if (len > 0) {
            input->text[len - 1] = '\0';
        }
    } else if (keycode == 9) { /* Tab */
        if (gui->username_input.is_focused) {
            gui->username_input.is_focused = false;
            gui->password_input.is_focused = true;
        } else {
            gui->password_input.is_focused = false;
            gui->username_input.is_focused = true;
        }
    } else if (keycode == 13) { /* Enter */
        if (gui->credential) {
            lifeauth_gui_start_auth(gui, NULL);
        } else {
            lifeauth_gui_start_enroll(gui);
        }
    }
}

void lifeauth_gui_key_up(lifeauth_gui_t *gui, int keycode) {
    (void)gui; (void)keycode;
}

void lifeauth_gui_char_input(lifeauth_gui_t *gui, char c) {
    if (!gui) return;

    lifeauth_input_t *input = gui->username_input.is_focused ? &gui->username_input :
                               gui->password_input.is_focused ? &gui->password_input : NULL;

    if (!input) return;

    int len = strlen(input->text);
    if (len < 254 && c >= 32 && c < 127) {
        input->text[len] = c;
        input->text[len + 1] = '\0';
    }
}

void lifeauth_gui_show(lifeauth_gui_t *gui) {
    if (gui) gui->is_visible = true;
}

void lifeauth_gui_hide(lifeauth_gui_t *gui) {
    if (gui) gui->is_visible = false;
}
