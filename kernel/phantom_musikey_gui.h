/*
 * PhantomOS MusiKey GUI Interface
 *
 * Visual interface for musical authentication system
 */

#ifndef PHANTOM_MUSIKEY_GUI_H
#define PHANTOM_MUSIKEY_GUI_H

#include "phantom_musikey.h"
#include <stdint.h>
#include <stdbool.h>

/* GUI dimensions */
#define MUSIKEY_GUI_WIDTH       400
#define MUSIKEY_GUI_HEIGHT      300
#define MUSIKEY_PIANO_KEYS      25      /* 2 octaves + 1 */
#define MUSIKEY_VISUALIZER_BARS 32

/* Colors (RGBA) */
#define MUSIKEY_COLOR_BG        0x1a1a2eFF
#define MUSIKEY_COLOR_PANEL     0x16213eFF
#define MUSIKEY_COLOR_ACCENT    0x0f3460FF
#define MUSIKEY_COLOR_HIGHLIGHT 0xe94560FF
#define MUSIKEY_COLOR_SUCCESS   0x4ecca3FF
#define MUSIKEY_COLOR_ERROR     0xff6b6bFF
#define MUSIKEY_COLOR_TEXT      0xecececFF
#define MUSIKEY_COLOR_WHITE_KEY 0xf0f0f0FF
#define MUSIKEY_COLOR_BLACK_KEY 0x2a2a2aFF
#define MUSIKEY_COLOR_KEY_PRESS 0x4ecca3FF

/* GUI states */
typedef enum {
    MUSIKEY_STATE_IDLE,
    MUSIKEY_STATE_ENROLLING,
    MUSIKEY_STATE_AUTHENTICATING,
    MUSIKEY_STATE_PLAYING,
    MUSIKEY_STATE_SUCCESS,
    MUSIKEY_STATE_FAILURE,
    MUSIKEY_STATE_LOCKED
} musikey_gui_state_t;

/* Animation types */
typedef enum {
    MUSIKEY_ANIM_NONE,
    MUSIKEY_ANIM_GENERATING,
    MUSIKEY_ANIM_SCRAMBLING,
    MUSIKEY_ANIM_VERIFYING,
    MUSIKEY_ANIM_PULSE_SUCCESS,
    MUSIKEY_ANIM_SHAKE_FAILURE
} musikey_anim_t;

/* Piano key state */
typedef struct {
    bool is_black;
    bool is_pressed;
    uint8_t note;
    float highlight;    /* 0.0-1.0 for fade effect */
} musikey_piano_key_t;

/* Visualizer bar */
typedef struct {
    float height;       /* 0.0-1.0 */
    float target;       /* Target height for animation */
    uint32_t color;
} musikey_vis_bar_t;

/* Input field */
typedef struct {
    char text[256];
    int cursor_pos;
    bool is_focused;
    bool is_password;
    char placeholder[64];
} musikey_input_t;

/* Button */
typedef struct {
    int x, y, width, height;
    char label[32];
    bool is_hovered;
    bool is_pressed;
    bool is_enabled;
    uint32_t color;
} musikey_button_t;

/* Main GUI context */
typedef struct {
    /* State */
    musikey_gui_state_t state;
    musikey_anim_t animation;
    float anim_progress;        /* 0.0-1.0 */
    uint32_t anim_start_time;

    /* Visual elements */
    musikey_piano_key_t piano[MUSIKEY_PIANO_KEYS];
    musikey_vis_bar_t visualizer[MUSIKEY_VISUALIZER_BARS];

    /* UI elements */
    musikey_input_t username_input;
    musikey_input_t password_input;
    musikey_button_t enroll_btn;
    musikey_button_t auth_btn;
    musikey_button_t cancel_btn;
    musikey_button_t play_btn;

    /* Status */
    char status_message[128];
    uint32_t status_color;
    float status_fade;

    /* Current song visualization */
    musikey_song_t *current_song;
    uint32_t playback_position;     /* ms */
    bool is_playing;

    /* Credentials */
    musikey_credential_t *credential;

    /* Framebuffer */
    uint32_t *framebuffer;
    int fb_width;
    int fb_height;

    /* Position (for windowed mode) */
    int window_x;
    int window_y;
    bool is_visible;

    /* Callbacks */
    void (*on_enroll_complete)(bool success, void *userdata);
    void (*on_auth_complete)(bool success, void *userdata);
    void *callback_userdata;

} musikey_gui_t;

/*
 * Lifecycle
 */

/* Initialize GUI */
musikey_gui_t* musikey_gui_create(int x, int y);

/* Destroy GUI */
void musikey_gui_destroy(musikey_gui_t *gui);

/* Show/hide */
void musikey_gui_show(musikey_gui_t *gui);
void musikey_gui_hide(musikey_gui_t *gui);

/*
 * Rendering
 */

/* Render to internal framebuffer */
void musikey_gui_render(musikey_gui_t *gui);

/* Get framebuffer for blitting */
uint32_t* musikey_gui_get_framebuffer(musikey_gui_t *gui);

/* Update animations (call every frame) */
void musikey_gui_update(musikey_gui_t *gui, uint32_t delta_ms);

/*
 * Input handling
 */

/* Mouse events */
void musikey_gui_mouse_move(musikey_gui_t *gui, int x, int y);
void musikey_gui_mouse_down(musikey_gui_t *gui, int x, int y, int button);
void musikey_gui_mouse_up(musikey_gui_t *gui, int x, int y, int button);

/* Keyboard events */
void musikey_gui_key_down(musikey_gui_t *gui, int keycode, int modifiers);
void musikey_gui_key_up(musikey_gui_t *gui, int keycode);
void musikey_gui_char_input(musikey_gui_t *gui, char c);

/*
 * Actions
 */

/* Start enrollment process */
void musikey_gui_start_enroll(musikey_gui_t *gui);

/* Start authentication process */
void musikey_gui_start_auth(musikey_gui_t *gui, musikey_credential_t *cred);

/* Play current song preview */
void musikey_gui_play_preview(musikey_gui_t *gui);

/* Stop playback */
void musikey_gui_stop_preview(musikey_gui_t *gui);

/*
 * Callbacks
 */

/* Set completion callbacks */
void musikey_gui_set_callbacks(musikey_gui_t *gui,
                                void (*on_enroll)(bool success, void *data),
                                void (*on_auth)(bool success, void *data),
                                void *userdata);

/*
 * Utility
 */

/* Set status message */
void musikey_gui_set_status(musikey_gui_t *gui, const char *message, uint32_t color);

/* Get current credential (after successful enrollment) */
musikey_credential_t* musikey_gui_get_credential(musikey_gui_t *gui);

#endif /* PHANTOM_MUSIKEY_GUI_H */
