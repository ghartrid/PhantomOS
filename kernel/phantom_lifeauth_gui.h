/*
 * PhantomOS LifeAuth GUI Interface
 *
 * Visual interface for blood plasma authentication system
 *
 * Copyright (c) 2025 PhantomOS Project
 * License: CC BY-NC-SA 4.0
 */

#ifndef PHANTOM_LIFEAUTH_GUI_H
#define PHANTOM_LIFEAUTH_GUI_H

#include "phantom_lifeauth.h"
#include <stdint.h>
#include <stdbool.h>

/* GUI dimensions */
#define LIFEAUTH_GUI_WIDTH      450
#define LIFEAUTH_GUI_HEIGHT     380

/* Number of biomarker bars per category */
#define LIFEAUTH_VIS_PROTEINS   8
#define LIFEAUTH_VIS_ANTIBODIES 6
#define LIFEAUTH_VIS_METABOLITES 8
#define LIFEAUTH_VIS_ENZYMES    6

/* Colors (RGBA) - Medical/scientific theme */
#define LIFEAUTH_COLOR_BG           0x0a1628FF  /* Deep navy */
#define LIFEAUTH_COLOR_PANEL        0x132238FF  /* Dark blue-gray */
#define LIFEAUTH_COLOR_BORDER       0x1e3a5fFF  /* Accent border */
#define LIFEAUTH_COLOR_PROTEIN      0x4a9fffFF  /* Blue - proteins */
#define LIFEAUTH_COLOR_ANTIBODY     0x4ade80FF  /* Green - antibodies */
#define LIFEAUTH_COLOR_METABOLITE   0xfbbf24FF  /* Amber - metabolites */
#define LIFEAUTH_COLOR_ENZYME       0xf472b6FF  /* Pink - enzymes */
#define LIFEAUTH_COLOR_ELECTROLYTE  0x22d3eeFF  /* Cyan - electrolytes */
#define LIFEAUTH_COLOR_SUCCESS      0x10b981FF  /* Green */
#define LIFEAUTH_COLOR_ERROR        0xef4444FF  /* Red */
#define LIFEAUTH_COLOR_WARNING      0xf59e0bFF  /* Amber */
#define LIFEAUTH_COLOR_TEXT         0xe2e8f0FF  /* Light gray */
#define LIFEAUTH_COLOR_TEXT_DIM     0x64748bFF  /* Dim gray */
#define LIFEAUTH_COLOR_PULSE        0xff6b6bFF  /* Pulse indicator */
#define LIFEAUTH_COLOR_FINGERPRINT  0x8b5cf6FF  /* Purple - fingerprint */

/* GUI states */
typedef enum {
    LIFEAUTH_GUI_IDLE,
    LIFEAUTH_GUI_SAMPLING,
    LIFEAUTH_GUI_ANALYZING,
    LIFEAUTH_GUI_ENROLLING,
    LIFEAUTH_GUI_AUTHENTICATING,
    LIFEAUTH_GUI_SUCCESS,
    LIFEAUTH_GUI_FAILURE,
    LIFEAUTH_GUI_HEALTH_ALERT,
    LIFEAUTH_GUI_LOCKED
} lifeauth_gui_state_t;

/* Animation types */
typedef enum {
    LIFEAUTH_ANIM_NONE,
    LIFEAUTH_ANIM_SAMPLING,
    LIFEAUTH_ANIM_PULSE,
    LIFEAUTH_ANIM_ANALYZING,
    LIFEAUTH_ANIM_SUCCESS_GLOW,
    LIFEAUTH_ANIM_FAILURE_SHAKE
} lifeauth_anim_t;

/* Biomarker visualization bar */
typedef struct {
    float value;            /* Current display value 0.0-1.0 */
    float target;           /* Target value for animation */
    float baseline;         /* Baseline reference */
    uint32_t color;
    bool is_abnormal;
} lifeauth_vis_bar_t;

/* Liveness gauge */
typedef struct {
    float value;            /* 0.0-1.0 */
    float target;
    char label[16];
    uint32_t color;
} lifeauth_gauge_t;

/* Fingerprint cell (for visual fingerprint display) */
typedef struct {
    uint8_t value;          /* Intensity 0-255 */
    uint8_t target;
} lifeauth_fp_cell_t;

/* Input field */
typedef struct {
    char text[256];
    int cursor_pos;
    bool is_focused;
    bool is_password;
    char placeholder[64];
} lifeauth_input_t;

/* Button */
typedef struct {
    int x, y, width, height;
    char label[32];
    bool is_hovered;
    bool is_pressed;
    bool is_enabled;
    uint32_t color;
} lifeauth_button_t;

/* Health alert indicator */
typedef struct {
    bool active;
    char message[128];
    float fade;
} lifeauth_health_alert_t;

/* Main GUI context */
typedef struct {
    /* State */
    lifeauth_gui_state_t state;
    lifeauth_anim_t animation;
    float anim_progress;
    uint32_t anim_start_time;

    /* Biomarker visualizations */
    lifeauth_vis_bar_t proteins[LIFEAUTH_VIS_PROTEINS];
    lifeauth_vis_bar_t antibodies[LIFEAUTH_VIS_ANTIBODIES];
    lifeauth_vis_bar_t metabolites[LIFEAUTH_VIS_METABOLITES];
    lifeauth_vis_bar_t enzymes[LIFEAUTH_VIS_ENZYMES];

    /* Liveness gauges */
    lifeauth_gauge_t pulse_gauge;
    lifeauth_gauge_t temp_gauge;
    lifeauth_gauge_t spo2_gauge;
    lifeauth_gauge_t activity_gauge;

    /* Fingerprint visualization (8x8 grid) */
    lifeauth_fp_cell_t fingerprint[64];
    float fp_reveal_progress;       /* 0.0-1.0 for reveal animation */

    /* Similarity/match meter */
    float similarity_value;
    float similarity_target;

    /* UI elements */
    lifeauth_input_t username_input;
    lifeauth_input_t password_input;
    lifeauth_button_t enroll_btn;
    lifeauth_button_t auth_btn;
    lifeauth_button_t sample_btn;
    lifeauth_button_t cancel_btn;

    /* Status */
    char status_message[128];
    uint32_t status_color;
    float status_fade;

    /* Health alert */
    lifeauth_health_alert_t health_alert;

    /* Current data */
    lifeauth_plasma_signature_t *current_signature;
    lifeauth_credential_t *credential;
    lifeauth_driver_t *driver;

    /* Sample quality */
    float sample_quality;
    bool sample_ready;

    /* Framebuffer */
    uint32_t *framebuffer;
    int fb_width;
    int fb_height;

    /* Position */
    int window_x;
    int window_y;
    bool is_visible;

    /* Callbacks */
    void (*on_enroll_complete)(bool success, void *userdata);
    void (*on_auth_complete)(bool success, void *userdata);
    void *callback_userdata;

} lifeauth_gui_t;

/*
 * Lifecycle
 */

/* Create GUI */
lifeauth_gui_t* lifeauth_gui_create(int x, int y);

/* Destroy GUI */
void lifeauth_gui_destroy(lifeauth_gui_t *gui);

/* Show/hide */
void lifeauth_gui_show(lifeauth_gui_t *gui);
void lifeauth_gui_hide(lifeauth_gui_t *gui);

/*
 * Rendering
 */

/* Render to internal framebuffer */
void lifeauth_gui_render(lifeauth_gui_t *gui);

/* Get framebuffer */
uint32_t* lifeauth_gui_get_framebuffer(lifeauth_gui_t *gui);

/* Update animations */
void lifeauth_gui_update(lifeauth_gui_t *gui, uint32_t delta_ms);

/*
 * Input handling
 */

void lifeauth_gui_mouse_move(lifeauth_gui_t *gui, int x, int y);
void lifeauth_gui_mouse_down(lifeauth_gui_t *gui, int x, int y, int button);
void lifeauth_gui_mouse_up(lifeauth_gui_t *gui, int x, int y, int button);
void lifeauth_gui_key_down(lifeauth_gui_t *gui, int keycode, int modifiers);
void lifeauth_gui_key_up(lifeauth_gui_t *gui, int keycode);
void lifeauth_gui_char_input(lifeauth_gui_t *gui, char c);

/*
 * Actions
 */

/* Start sample collection */
void lifeauth_gui_start_sample(lifeauth_gui_t *gui);

/* Start enrollment */
void lifeauth_gui_start_enroll(lifeauth_gui_t *gui);

/* Start authentication */
void lifeauth_gui_start_auth(lifeauth_gui_t *gui, lifeauth_credential_t *cred);

/* Cancel current operation */
void lifeauth_gui_cancel(lifeauth_gui_t *gui);

/*
 * Callbacks
 */

void lifeauth_gui_set_callbacks(lifeauth_gui_t *gui,
                                 void (*on_enroll)(bool success, void *data),
                                 void (*on_auth)(bool success, void *data),
                                 void *userdata);

/*
 * Utility
 */

/* Set status message */
void lifeauth_gui_set_status(lifeauth_gui_t *gui, const char *message, uint32_t color);

/* Show health alert */
void lifeauth_gui_show_health_alert(lifeauth_gui_t *gui, const char *message);

/* Get credential after enrollment */
lifeauth_credential_t* lifeauth_gui_get_credential(lifeauth_gui_t *gui);

/* Update visualization from signature data */
void lifeauth_gui_update_from_signature(lifeauth_gui_t *gui,
                                         const lifeauth_plasma_signature_t *sig);

#endif /* PHANTOM_LIFEAUTH_GUI_H */
