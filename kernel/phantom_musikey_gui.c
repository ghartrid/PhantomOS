/*
 * PhantomOS MusiKey GUI Implementation
 *
 * Visual interface for musical authentication with piano visualization
 */

#include "phantom_musikey_gui.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Layout constants */
#define HEADER_HEIGHT    40
#define PIANO_HEIGHT     80
#define PIANO_Y          (MUSIKEY_GUI_HEIGHT - PIANO_HEIGHT - 10)
#define VIS_HEIGHT       60
#define VIS_Y            (PIANO_Y - VIS_HEIGHT - 10)
#define INPUT_HEIGHT     30
#define BUTTON_HEIGHT    35
#define PADDING          15

/* Piano key layout (C4 to C6) */
static const int BLACK_KEY_PATTERN[] = {0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0};

/*
 * Drawing primitives
 */

static void draw_rect(uint32_t *fb, int fb_w, int fb_h,
                      int x, int y, int w, int h, uint32_t color) {
    for (int py = y; py < y + h && py < fb_h; py++) {
        if (py < 0) continue;
        for (int px = x; px < x + w && px < fb_w; px++) {
            if (px < 0) continue;
            fb[py * fb_w + px] = color;
        }
    }
}

static void draw_rect_outline(uint32_t *fb, int fb_w, int fb_h,
                              int x, int y, int w, int h, uint32_t color, int thickness) {
    draw_rect(fb, fb_w, fb_h, x, y, w, thickness, color);           /* Top */
    draw_rect(fb, fb_w, fb_h, x, y + h - thickness, w, thickness, color); /* Bottom */
    draw_rect(fb, fb_w, fb_h, x, y, thickness, h, color);           /* Left */
    draw_rect(fb, fb_w, fb_h, x + w - thickness, y, thickness, h, color); /* Right */
}

static void draw_rounded_rect(uint32_t *fb, int fb_w, int fb_h,
                              int x, int y, int w, int h, int r, uint32_t color) {
    /* Simplified rounded rect - just draw regular rect with corner adjustments */
    draw_rect(fb, fb_w, fb_h, x + r, y, w - 2*r, h, color);
    draw_rect(fb, fb_w, fb_h, x, y + r, w, h - 2*r, color);

    /* Corner circles approximation */
    for (int cy = 0; cy < r; cy++) {
        for (int cx = 0; cx < r; cx++) {
            if (cx*cx + cy*cy <= r*r) {
                /* Top-left */
                if (x + r - cx >= 0 && y + r - cy >= 0)
                    fb[(y + r - cy) * fb_w + (x + r - cx)] = color;
                /* Top-right */
                if (x + w - r + cx < fb_w && y + r - cy >= 0)
                    fb[(y + r - cy) * fb_w + (x + w - r + cx)] = color;
                /* Bottom-left */
                if (x + r - cx >= 0 && y + h - r + cy < fb_h)
                    fb[(y + h - r + cy) * fb_w + (x + r - cx)] = color;
                /* Bottom-right */
                if (x + w - r + cx < fb_w && y + h - r + cy < fb_h)
                    fb[(y + h - r + cy) * fb_w + (x + w - r + cx)] = color;
            }
        }
    }
}

/* Simple 5x7 font bitmap */
static const uint8_t FONT_5X7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, /* Space */
    {0x00,0x00,0x5F,0x00,0x00}, /* ! */
    {0x00,0x07,0x00,0x07,0x00}, /* " */
    {0x14,0x7F,0x14,0x7F,0x14}, /* # */
    {0x24,0x2A,0x7F,0x2A,0x12}, /* $ */
    {0x23,0x13,0x08,0x64,0x62}, /* % */
    {0x36,0x49,0x55,0x22,0x50}, /* & */
    {0x00,0x05,0x03,0x00,0x00}, /* ' */
    {0x00,0x1C,0x22,0x41,0x00}, /* ( */
    {0x00,0x41,0x22,0x1C,0x00}, /* ) */
    {0x08,0x2A,0x1C,0x2A,0x08}, /* * */
    {0x08,0x08,0x3E,0x08,0x08}, /* + */
    {0x00,0x50,0x30,0x00,0x00}, /* , */
    {0x08,0x08,0x08,0x08,0x08}, /* - */
    {0x00,0x60,0x60,0x00,0x00}, /* . */
    {0x20,0x10,0x08,0x04,0x02}, /* / */
    {0x3E,0x51,0x49,0x45,0x3E}, /* 0 */
    {0x00,0x42,0x7F,0x40,0x00}, /* 1 */
    {0x42,0x61,0x51,0x49,0x46}, /* 2 */
    {0x21,0x41,0x45,0x4B,0x31}, /* 3 */
    {0x18,0x14,0x12,0x7F,0x10}, /* 4 */
    {0x27,0x45,0x45,0x45,0x39}, /* 5 */
    {0x3C,0x4A,0x49,0x49,0x30}, /* 6 */
    {0x01,0x71,0x09,0x05,0x03}, /* 7 */
    {0x36,0x49,0x49,0x49,0x36}, /* 8 */
    {0x06,0x49,0x49,0x29,0x1E}, /* 9 */
    {0x00,0x36,0x36,0x00,0x00}, /* : */
    {0x00,0x56,0x36,0x00,0x00}, /* ; */
    {0x00,0x08,0x14,0x22,0x41}, /* < */
    {0x14,0x14,0x14,0x14,0x14}, /* = */
    {0x41,0x22,0x14,0x08,0x00}, /* > */
    {0x02,0x01,0x51,0x09,0x06}, /* ? */
    {0x32,0x49,0x79,0x41,0x3E}, /* @ */
    {0x7E,0x11,0x11,0x11,0x7E}, /* A */
    {0x7F,0x49,0x49,0x49,0x36}, /* B */
    {0x3E,0x41,0x41,0x41,0x22}, /* C */
    {0x7F,0x41,0x41,0x22,0x1C}, /* D */
    {0x7F,0x49,0x49,0x49,0x41}, /* E */
    {0x7F,0x09,0x09,0x01,0x01}, /* F */
    {0x3E,0x41,0x41,0x51,0x32}, /* G */
    {0x7F,0x08,0x08,0x08,0x7F}, /* H */
    {0x00,0x41,0x7F,0x41,0x00}, /* I */
    {0x20,0x40,0x41,0x3F,0x01}, /* J */
    {0x7F,0x08,0x14,0x22,0x41}, /* K */
    {0x7F,0x40,0x40,0x40,0x40}, /* L */
    {0x7F,0x02,0x04,0x02,0x7F}, /* M */
    {0x7F,0x04,0x08,0x10,0x7F}, /* N */
    {0x3E,0x41,0x41,0x41,0x3E}, /* O */
    {0x7F,0x09,0x09,0x09,0x06}, /* P */
    {0x3E,0x41,0x51,0x21,0x5E}, /* Q */
    {0x7F,0x09,0x19,0x29,0x46}, /* R */
    {0x46,0x49,0x49,0x49,0x31}, /* S */
    {0x01,0x01,0x7F,0x01,0x01}, /* T */
    {0x3F,0x40,0x40,0x40,0x3F}, /* U */
    {0x1F,0x20,0x40,0x20,0x1F}, /* V */
    {0x7F,0x20,0x18,0x20,0x7F}, /* W */
    {0x63,0x14,0x08,0x14,0x63}, /* X */
    {0x03,0x04,0x78,0x04,0x03}, /* Y */
    {0x61,0x51,0x49,0x45,0x43}, /* Z */
};

static void draw_char(uint32_t *fb, int fb_w, int fb_h,
                      int x, int y, char c, uint32_t color, int scale) {
    int idx = 0;
    if (c >= 'A' && c <= 'Z') idx = c - 'A' + 33;
    else if (c >= 'a' && c <= 'z') idx = c - 'a' + 33;  /* Uppercase only font */
    else if (c >= '0' && c <= '9') idx = c - '0' + 16;
    else if (c >= ' ' && c <= '?') idx = c - ' ';
    else return;

    for (int col = 0; col < 5; col++) {
        uint8_t line = FONT_5X7[idx][col];
        for (int row = 0; row < 7; row++) {
            if (line & (1 << row)) {
                for (int sy = 0; sy < scale; sy++) {
                    for (int sx = 0; sx < scale; sx++) {
                        int px = x + col * scale + sx;
                        int py = y + row * scale + sy;
                        if (px >= 0 && px < fb_w && py >= 0 && py < fb_h) {
                            fb[py * fb_w + px] = color;
                        }
                    }
                }
            }
        }
    }
}

static void draw_text(uint32_t *fb, int fb_w, int fb_h,
                      int x, int y, const char *text, uint32_t color, int scale) {
    int cx = x;
    for (const char *p = text; *p; p++) {
        draw_char(fb, fb_w, fb_h, cx, y, *p, color, scale);
        cx += 6 * scale;
    }
}

static void draw_text_centered(uint32_t *fb, int fb_w, int fb_h,
                               int cx, int y, const char *text, uint32_t color, int scale) {
    int len = strlen(text);
    int width = len * 6 * scale;
    draw_text(fb, fb_w, fb_h, cx - width/2, y, text, color, scale);
}

/*
 * Component rendering
 */

static void render_header(musikey_gui_t *gui) {
    draw_rect(gui->framebuffer, gui->fb_width, gui->fb_height,
              0, 0, gui->fb_width, HEADER_HEIGHT, MUSIKEY_COLOR_ACCENT);

    draw_text_centered(gui->framebuffer, gui->fb_width, gui->fb_height,
                       gui->fb_width / 2, 12, "MUSIKEY", MUSIKEY_COLOR_TEXT, 2);

    /* Musical note icon */
    draw_text(gui->framebuffer, gui->fb_width, gui->fb_height,
              10, 15, "#", MUSIKEY_COLOR_HIGHLIGHT, 2);
}

static void render_piano(musikey_gui_t *gui) {
    int white_width = (gui->fb_width - 20) / 15;  /* 15 white keys in 2 octaves */
    int black_width = white_width * 2 / 3;
    int white_x = 10;

    /* Draw white keys first */
    int white_idx = 0;
    for (int i = 0; i < MUSIKEY_PIANO_KEYS; i++) {
        musikey_piano_key_t *key = &gui->piano[i];
        if (!key->is_black) {
            uint32_t color = MUSIKEY_COLOR_WHITE_KEY;
            if (key->is_pressed || key->highlight > 0.1f) {
                /* Blend with highlight color */
                float h = key->highlight > 0 ? key->highlight : 1.0f;
                uint8_t r = ((MUSIKEY_COLOR_KEY_PRESS >> 24) & 0xFF) * h +
                           ((MUSIKEY_COLOR_WHITE_KEY >> 24) & 0xFF) * (1-h);
                uint8_t g = ((MUSIKEY_COLOR_KEY_PRESS >> 16) & 0xFF) * h +
                           ((MUSIKEY_COLOR_WHITE_KEY >> 16) & 0xFF) * (1-h);
                uint8_t b = ((MUSIKEY_COLOR_KEY_PRESS >> 8) & 0xFF) * h +
                           ((MUSIKEY_COLOR_WHITE_KEY >> 8) & 0xFF) * (1-h);
                color = (r << 24) | (g << 16) | (b << 8) | 0xFF;
            }
            draw_rect(gui->framebuffer, gui->fb_width, gui->fb_height,
                      white_x, PIANO_Y, white_width - 2, PIANO_HEIGHT, color);
            draw_rect_outline(gui->framebuffer, gui->fb_width, gui->fb_height,
                              white_x, PIANO_Y, white_width - 2, PIANO_HEIGHT,
                              MUSIKEY_COLOR_BLACK_KEY, 1);
            white_x += white_width;
            white_idx++;
        }
    }

    /* Draw black keys on top */
    white_x = 10;
    int note_in_octave = 0;
    for (int i = 0; i < MUSIKEY_PIANO_KEYS; i++) {
        musikey_piano_key_t *key = &gui->piano[i];
        if (!key->is_black) {
            if (note_in_octave < 12 && BLACK_KEY_PATTERN[(note_in_octave + 1) % 12]) {
                /* Draw black key to the right of this white key */
                int black_x = white_x + white_width - black_width/2 - 1;
                int bi = i + 1;
                if (bi < MUSIKEY_PIANO_KEYS && gui->piano[bi].is_black) {
                    uint32_t color = MUSIKEY_COLOR_BLACK_KEY;
                    if (gui->piano[bi].is_pressed || gui->piano[bi].highlight > 0.1f) {
                        color = MUSIKEY_COLOR_KEY_PRESS;
                    }
                    draw_rect(gui->framebuffer, gui->fb_width, gui->fb_height,
                              black_x, PIANO_Y, black_width, PIANO_HEIGHT * 2/3, color);
                }
            }
            white_x += white_width;
            note_in_octave = (note_in_octave + 1) % 12;
            if (note_in_octave == 3 || note_in_octave == 8) {
                /* Skip E-F and B-C gaps (no black key) */
            }
        }
    }
}

static void render_visualizer(musikey_gui_t *gui) {
    int bar_width = (gui->fb_width - 20) / MUSIKEY_VISUALIZER_BARS;

    draw_rect(gui->framebuffer, gui->fb_width, gui->fb_height,
              10, VIS_Y, gui->fb_width - 20, VIS_HEIGHT, MUSIKEY_COLOR_PANEL);

    for (int i = 0; i < MUSIKEY_VISUALIZER_BARS; i++) {
        int bar_height = (int)(gui->visualizer[i].height * (VIS_HEIGHT - 4));
        if (bar_height < 2) bar_height = 2;

        uint32_t color = gui->visualizer[i].color;
        if (color == 0) color = MUSIKEY_COLOR_HIGHLIGHT;

        draw_rect(gui->framebuffer, gui->fb_width, gui->fb_height,
                  12 + i * bar_width, VIS_Y + VIS_HEIGHT - bar_height - 2,
                  bar_width - 2, bar_height, color);
    }
}

static void render_input(musikey_gui_t *gui, musikey_input_t *input,
                         int x, int y, int width, const char *label) {
    /* Label */
    draw_text(gui->framebuffer, gui->fb_width, gui->fb_height,
              x, y - 12, label, MUSIKEY_COLOR_TEXT, 1);

    /* Input box */
    uint32_t border_color = input->is_focused ? MUSIKEY_COLOR_HIGHLIGHT : MUSIKEY_COLOR_ACCENT;
    draw_rect(gui->framebuffer, gui->fb_width, gui->fb_height,
              x, y, width, INPUT_HEIGHT, MUSIKEY_COLOR_PANEL);
    draw_rect_outline(gui->framebuffer, gui->fb_width, gui->fb_height,
                      x, y, width, INPUT_HEIGHT, border_color, 2);

    /* Text or placeholder */
    const char *display_text = input->text[0] ? input->text : input->placeholder;
    uint32_t text_color = input->text[0] ? MUSIKEY_COLOR_TEXT : 0x888888FF;

    if (input->is_password && input->text[0]) {
        /* Show dots for password */
        char dots[256];
        int len = strlen(input->text);
        for (int i = 0; i < len && i < 255; i++) dots[i] = '*';
        dots[len < 255 ? len : 255] = '\0';
        draw_text(gui->framebuffer, gui->fb_width, gui->fb_height,
                  x + 5, y + 10, dots, text_color, 1);
    } else {
        draw_text(gui->framebuffer, gui->fb_width, gui->fb_height,
                  x + 5, y + 10, display_text, text_color, 1);
    }

    /* Cursor */
    if (input->is_focused) {
        int cursor_x = x + 5 + input->cursor_pos * 6;
        draw_rect(gui->framebuffer, gui->fb_width, gui->fb_height,
                  cursor_x, y + 5, 2, INPUT_HEIGHT - 10, MUSIKEY_COLOR_TEXT);
    }
}

static void render_button(musikey_gui_t *gui, musikey_button_t *btn) {
    uint32_t bg_color = btn->color ? btn->color : MUSIKEY_COLOR_ACCENT;
    if (!btn->is_enabled) bg_color = 0x444444FF;
    else if (btn->is_pressed) bg_color = MUSIKEY_COLOR_HIGHLIGHT;
    else if (btn->is_hovered) {
        /* Lighten */
        uint8_t r = ((bg_color >> 24) & 0xFF) + 30;
        uint8_t g = ((bg_color >> 16) & 0xFF) + 30;
        uint8_t b = ((bg_color >> 8) & 0xFF) + 30;
        bg_color = (r << 24) | (g << 16) | (b << 8) | 0xFF;
    }

    draw_rounded_rect(gui->framebuffer, gui->fb_width, gui->fb_height,
                      btn->x, btn->y, btn->width, btn->height, 5, bg_color);

    uint32_t text_color = btn->is_enabled ? MUSIKEY_COLOR_TEXT : 0x888888FF;
    draw_text_centered(gui->framebuffer, gui->fb_width, gui->fb_height,
                       btn->x + btn->width/2, btn->y + 12, btn->label, text_color, 1);
}

static void render_status(musikey_gui_t *gui) {
    if (gui->status_message[0] && gui->status_fade > 0.1f) {
        /* Apply fade to status color */
        uint32_t color = gui->status_color;
        uint8_t a = (uint8_t)(255 * gui->status_fade);
        color = (color & 0xFFFFFF00) | a;

        draw_text_centered(gui->framebuffer, gui->fb_width, gui->fb_height,
                           gui->fb_width / 2, VIS_Y - 20, gui->status_message,
                           color, 1);  /* Use faded color */
    }
}

/*
 * Public API
 */

musikey_gui_t* musikey_gui_create(int x, int y) {
    musikey_gui_t *gui = calloc(1, sizeof(musikey_gui_t));
    if (!gui) return NULL;

    gui->fb_width = MUSIKEY_GUI_WIDTH;
    gui->fb_height = MUSIKEY_GUI_HEIGHT;
    gui->framebuffer = calloc(gui->fb_width * gui->fb_height, sizeof(uint32_t));
    if (!gui->framebuffer) {
        free(gui);
        return NULL;
    }

    gui->window_x = x;
    gui->window_y = y;
    gui->state = MUSIKEY_STATE_IDLE;

    /* Initialize piano keys (C4 to C6) */
    int key_idx = 0;
    for (int octave = 4; octave <= 5; octave++) {
        for (int note = 0; note < 12 && key_idx < MUSIKEY_PIANO_KEYS; note++) {
            gui->piano[key_idx].note = octave * 12 + note;
            gui->piano[key_idx].is_black = BLACK_KEY_PATTERN[note];
            gui->piano[key_idx].is_pressed = false;
            gui->piano[key_idx].highlight = 0;
            key_idx++;
        }
    }
    if (key_idx < MUSIKEY_PIANO_KEYS) {
        gui->piano[key_idx].note = 72;  /* C6 */
        gui->piano[key_idx].is_black = false;
    }

    /* Initialize visualizer */
    for (int i = 0; i < MUSIKEY_VISUALIZER_BARS; i++) {
        gui->visualizer[i].height = 0.1f;
        gui->visualizer[i].target = 0.1f;
        gui->visualizer[i].color = MUSIKEY_COLOR_HIGHLIGHT;
    }

    /* Initialize inputs */
    strcpy(gui->username_input.placeholder, "Username");
    strcpy(gui->password_input.placeholder, "Passphrase");
    gui->password_input.is_password = true;

    /* Initialize buttons */
    int btn_width = 100;
    int btn_y = VIS_Y - 50;

    gui->enroll_btn = (musikey_button_t){
        .x = PADDING,
        .y = btn_y,
        .width = btn_width,
        .height = BUTTON_HEIGHT,
        .is_enabled = true,
        .color = MUSIKEY_COLOR_SUCCESS
    };
    strcpy(gui->enroll_btn.label, "ENROLL");

    gui->auth_btn = (musikey_button_t){
        .x = gui->fb_width - btn_width - PADDING,
        .y = btn_y,
        .width = btn_width,
        .height = BUTTON_HEIGHT,
        .is_enabled = false,
        .color = MUSIKEY_COLOR_HIGHLIGHT
    };
    strcpy(gui->auth_btn.label, "AUTH");

    gui->play_btn = (musikey_button_t){
        .x = (gui->fb_width - btn_width) / 2,
        .y = btn_y,
        .width = btn_width,
        .height = BUTTON_HEIGHT,
        .is_enabled = false,
        .color = MUSIKEY_COLOR_ACCENT
    };
    strcpy(gui->play_btn.label, "PLAY");

    gui->status_fade = 0;
    gui->is_visible = true;

    return gui;
}

void musikey_gui_destroy(musikey_gui_t *gui) {
    if (!gui) return;
    if (gui->framebuffer) free(gui->framebuffer);
    if (gui->current_song) free(gui->current_song);
    if (gui->credential) free(gui->credential);
    free(gui);
}

void musikey_gui_show(musikey_gui_t *gui) {
    if (gui) gui->is_visible = true;
}

void musikey_gui_hide(musikey_gui_t *gui) {
    if (gui) gui->is_visible = false;
}

void musikey_gui_render(musikey_gui_t *gui) {
    if (!gui || !gui->is_visible) return;

    /* Clear background */
    for (int i = 0; i < gui->fb_width * gui->fb_height; i++) {
        gui->framebuffer[i] = MUSIKEY_COLOR_BG;
    }

    render_header(gui);

    /* Input fields */
    int input_y = HEADER_HEIGHT + 20;
    int input_width = gui->fb_width - 2 * PADDING;

    render_input(gui, &gui->username_input, PADDING, input_y, input_width, "USER");
    render_input(gui, &gui->password_input, PADDING, input_y + INPUT_HEIGHT + 20,
                 input_width, "KEY");

    render_button(gui, &gui->enroll_btn);
    render_button(gui, &gui->auth_btn);
    render_button(gui, &gui->play_btn);

    render_status(gui);
    render_visualizer(gui);
    render_piano(gui);
}

void musikey_gui_update(musikey_gui_t *gui, uint32_t delta_ms) {
    if (!gui) return;

    float dt = delta_ms / 1000.0f;

    /* Update visualizer animation */
    for (int i = 0; i < MUSIKEY_VISUALIZER_BARS; i++) {
        float diff = gui->visualizer[i].target - gui->visualizer[i].height;
        gui->visualizer[i].height += diff * dt * 10.0f;

        /* Decay target */
        gui->visualizer[i].target *= (1.0f - dt * 2.0f);
        if (gui->visualizer[i].target < 0.1f) gui->visualizer[i].target = 0.1f;
    }

    /* Update piano key highlights */
    for (int i = 0; i < MUSIKEY_PIANO_KEYS; i++) {
        if (!gui->piano[i].is_pressed && gui->piano[i].highlight > 0) {
            gui->piano[i].highlight -= dt * 3.0f;
            if (gui->piano[i].highlight < 0) gui->piano[i].highlight = 0;
        }
    }

    /* Update status fade */
    if (gui->status_fade > 0) {
        gui->status_fade -= dt * 0.5f;
    }

    /* Update animation */
    if (gui->animation != MUSIKEY_ANIM_NONE) {
        gui->anim_progress += dt;
        if (gui->anim_progress > 1.0f) {
            gui->animation = MUSIKEY_ANIM_NONE;
            gui->anim_progress = 0;
        }
    }

    /* Playback animation */
    if (gui->is_playing && gui->current_song) {
        gui->playback_position += delta_ms;

        /* Highlight notes being played */
        for (uint32_t i = 0; i < gui->current_song->event_count; i++) {
            musikey_event_t *event = &gui->current_song->events[i];
            if (event->timestamp <= gui->playback_position &&
                event->timestamp + event->duration > gui->playback_position) {

                /* Find piano key */
                for (int k = 0; k < MUSIKEY_PIANO_KEYS; k++) {
                    if (gui->piano[k].note == event->note) {
                        gui->piano[k].highlight = 1.0f;
                        break;
                    }
                }

                /* Update visualizer */
                int bar = (event->note - 48) * MUSIKEY_VISUALIZER_BARS / 24;
                if (bar >= 0 && bar < MUSIKEY_VISUALIZER_BARS) {
                    gui->visualizer[bar].target = event->velocity / 127.0f;
                }
            }
        }

        if (gui->playback_position > gui->current_song->total_duration) {
            gui->is_playing = false;
            gui->playback_position = 0;
        }
    }
}

uint32_t* musikey_gui_get_framebuffer(musikey_gui_t *gui) {
    return gui ? gui->framebuffer : NULL;
}

/* Input handling */
static bool point_in_rect(int px, int py, int x, int y, int w, int h) {
    return px >= x && px < x + w && py >= y && py < y + h;
}

void musikey_gui_mouse_move(musikey_gui_t *gui, int x, int y) {
    if (!gui) return;

    gui->enroll_btn.is_hovered = point_in_rect(x, y, gui->enroll_btn.x, gui->enroll_btn.y,
                                                gui->enroll_btn.width, gui->enroll_btn.height);
    gui->auth_btn.is_hovered = point_in_rect(x, y, gui->auth_btn.x, gui->auth_btn.y,
                                              gui->auth_btn.width, gui->auth_btn.height);
    gui->play_btn.is_hovered = point_in_rect(x, y, gui->play_btn.x, gui->play_btn.y,
                                              gui->play_btn.width, gui->play_btn.height);
}

void musikey_gui_mouse_down(musikey_gui_t *gui, int x, int y, int button) {
    if (!gui || button != 0) return;

    int input_y = HEADER_HEIGHT + 20;
    int input_width = gui->fb_width - 2 * PADDING;

    /* Check inputs */
    gui->username_input.is_focused = point_in_rect(x, y, PADDING, input_y,
                                                    input_width, INPUT_HEIGHT);
    gui->password_input.is_focused = point_in_rect(x, y, PADDING, input_y + INPUT_HEIGHT + 20,
                                                    input_width, INPUT_HEIGHT);

    /* Check buttons */
    if (gui->enroll_btn.is_enabled && gui->enroll_btn.is_hovered) {
        gui->enroll_btn.is_pressed = true;
        musikey_gui_start_enroll(gui);
    }
    if (gui->auth_btn.is_enabled && gui->auth_btn.is_hovered) {
        gui->auth_btn.is_pressed = true;
        musikey_gui_start_auth(gui, gui->credential);
    }
    if (gui->play_btn.is_enabled && gui->play_btn.is_hovered) {
        gui->play_btn.is_pressed = true;
        if (gui->is_playing) {
            musikey_gui_stop_preview(gui);
        } else {
            musikey_gui_play_preview(gui);
        }
    }

    /* Check piano keys */
    if (y >= PIANO_Y && y < PIANO_Y + PIANO_HEIGHT) {
        /* Simplified - just activate nearby key */
        int key_idx = (x - 10) * MUSIKEY_PIANO_KEYS / (gui->fb_width - 20);
        if (key_idx >= 0 && key_idx < MUSIKEY_PIANO_KEYS) {
            gui->piano[key_idx].is_pressed = true;
            gui->piano[key_idx].highlight = 1.0f;

            /* Update visualizer */
            int bar = key_idx * MUSIKEY_VISUALIZER_BARS / MUSIKEY_PIANO_KEYS;
            if (bar < MUSIKEY_VISUALIZER_BARS) {
                gui->visualizer[bar].target = 1.0f;
            }
        }
    }
}

void musikey_gui_mouse_up(musikey_gui_t *gui, int x, int y, int button) {
    (void)x; (void)y;
    if (!gui || button != 0) return;

    gui->enroll_btn.is_pressed = false;
    gui->auth_btn.is_pressed = false;
    gui->play_btn.is_pressed = false;

    for (int i = 0; i < MUSIKEY_PIANO_KEYS; i++) {
        gui->piano[i].is_pressed = false;
    }
}

void musikey_gui_key_down(musikey_gui_t *gui, int keycode, int modifiers) {
    (void)modifiers;
    if (!gui) return;

    /* Tab to switch focus */
    if (keycode == 9) {  /* Tab */
        if (gui->username_input.is_focused) {
            gui->username_input.is_focused = false;
            gui->password_input.is_focused = true;
        } else {
            gui->username_input.is_focused = true;
            gui->password_input.is_focused = false;
        }
    }

    /* Backspace */
    if (keycode == 8) {
        musikey_input_t *input = gui->username_input.is_focused ? &gui->username_input :
                                 gui->password_input.is_focused ? &gui->password_input : NULL;
        if (input && input->cursor_pos > 0) {
            input->cursor_pos--;
            input->text[input->cursor_pos] = '\0';
        }
    }

    /* Enter to submit */
    if (keycode == 13) {
        if (gui->enroll_btn.is_enabled && gui->username_input.text[0] && gui->password_input.text[0]) {
            musikey_gui_start_enroll(gui);
        } else if (gui->auth_btn.is_enabled) {
            musikey_gui_start_auth(gui, gui->credential);
        }
    }
}

void musikey_gui_key_up(musikey_gui_t *gui, int keycode) {
    (void)gui; (void)keycode;
}

void musikey_gui_char_input(musikey_gui_t *gui, char c) {
    if (!gui) return;

    musikey_input_t *input = gui->username_input.is_focused ? &gui->username_input :
                             gui->password_input.is_focused ? &gui->password_input : NULL;

    if (input && c >= 32 && c < 127 && input->cursor_pos < 254) {
        input->text[input->cursor_pos++] = c;
        input->text[input->cursor_pos] = '\0';
    }
}

void musikey_gui_start_enroll(musikey_gui_t *gui) {
    if (!gui) return;
    if (!gui->username_input.text[0] || !gui->password_input.text[0]) {
        musikey_gui_set_status(gui, "ENTER USERNAME AND KEY", MUSIKEY_COLOR_ERROR);
        return;
    }

    gui->state = MUSIKEY_STATE_ENROLLING;
    gui->animation = MUSIKEY_ANIM_GENERATING;
    gui->anim_progress = 0;

    /* Initialize MusiKey if needed */
    musikey_init(NULL);

    /* Allocate credential */
    if (gui->credential) free(gui->credential);
    gui->credential = calloc(1, sizeof(musikey_credential_t));

    musikey_error_t err = musikey_enroll(gui->username_input.text,
                                          (uint8_t*)gui->password_input.text,
                                          strlen(gui->password_input.text),
                                          gui->credential);

    if (err == MUSIKEY_OK) {
        gui->state = MUSIKEY_STATE_SUCCESS;
        musikey_gui_set_status(gui, "ENROLLED SUCCESSFULLY", MUSIKEY_COLOR_SUCCESS);
        gui->auth_btn.is_enabled = true;
        gui->play_btn.is_enabled = true;

        /* Store the song for playback */
        if (gui->current_song) free(gui->current_song);
        gui->current_song = calloc(1, sizeof(musikey_song_t));
        musikey_descramble(&gui->credential->scrambled_song,
                           (uint8_t*)gui->password_input.text,
                           strlen(gui->password_input.text),
                           gui->current_song);

        if (gui->on_enroll_complete) {
            gui->on_enroll_complete(true, gui->callback_userdata);
        }
    } else {
        gui->state = MUSIKEY_STATE_FAILURE;
        musikey_gui_set_status(gui, "ENROLLMENT FAILED", MUSIKEY_COLOR_ERROR);

        if (gui->on_enroll_complete) {
            gui->on_enroll_complete(false, gui->callback_userdata);
        }
    }
}

void musikey_gui_start_auth(musikey_gui_t *gui, musikey_credential_t *cred) {
    if (!gui || !cred) return;

    gui->state = MUSIKEY_STATE_AUTHENTICATING;
    gui->animation = MUSIKEY_ANIM_VERIFYING;

    musikey_error_t err = musikey_authenticate(cred,
                                                (uint8_t*)gui->password_input.text,
                                                strlen(gui->password_input.text));

    if (err == MUSIKEY_OK) {
        gui->state = MUSIKEY_STATE_SUCCESS;
        gui->animation = MUSIKEY_ANIM_PULSE_SUCCESS;
        musikey_gui_set_status(gui, "AUTHENTICATION SUCCESS", MUSIKEY_COLOR_SUCCESS);

        /* Trigger success visualization */
        for (int i = 0; i < MUSIKEY_VISUALIZER_BARS; i++) {
            gui->visualizer[i].target = 1.0f;
            gui->visualizer[i].color = MUSIKEY_COLOR_SUCCESS;
        }

        if (gui->on_auth_complete) {
            gui->on_auth_complete(true, gui->callback_userdata);
        }
    } else if (err == MUSIKEY_ERR_LOCKED) {
        gui->state = MUSIKEY_STATE_LOCKED;
        musikey_gui_set_status(gui, "ACCOUNT LOCKED", MUSIKEY_COLOR_ERROR);
        gui->auth_btn.is_enabled = false;

        if (gui->on_auth_complete) {
            gui->on_auth_complete(false, gui->callback_userdata);
        }
    } else {
        gui->state = MUSIKEY_STATE_FAILURE;
        gui->animation = MUSIKEY_ANIM_SHAKE_FAILURE;
        musikey_gui_set_status(gui, "AUTHENTICATION FAILED", MUSIKEY_COLOR_ERROR);

        /* Error visualization */
        for (int i = 0; i < MUSIKEY_VISUALIZER_BARS; i++) {
            gui->visualizer[i].color = MUSIKEY_COLOR_ERROR;
        }

        if (gui->on_auth_complete) {
            gui->on_auth_complete(false, gui->callback_userdata);
        }
    }
}

void musikey_gui_play_preview(musikey_gui_t *gui) {
    if (!gui || !gui->current_song) return;

    gui->is_playing = true;
    gui->playback_position = 0;
    strcpy(gui->play_btn.label, "STOP");
}

void musikey_gui_stop_preview(musikey_gui_t *gui) {
    if (!gui) return;

    gui->is_playing = false;
    gui->playback_position = 0;
    strcpy(gui->play_btn.label, "PLAY");
}

void musikey_gui_set_callbacks(musikey_gui_t *gui,
                                void (*on_enroll)(bool success, void *data),
                                void (*on_auth)(bool success, void *data),
                                void *userdata) {
    if (!gui) return;
    gui->on_enroll_complete = on_enroll;
    gui->on_auth_complete = on_auth;
    gui->callback_userdata = userdata;
}

void musikey_gui_set_status(musikey_gui_t *gui, const char *message, uint32_t color) {
    if (!gui) return;
    strncpy(gui->status_message, message, sizeof(gui->status_message) - 1);
    gui->status_color = color;
    gui->status_fade = 1.0f;
}

musikey_credential_t* musikey_gui_get_credential(musikey_gui_t *gui) {
    return gui ? gui->credential : NULL;
}
