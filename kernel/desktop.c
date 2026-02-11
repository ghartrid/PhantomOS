/*
 * PhantomOS Desktop Environment
 * "To Create, Not To Destroy"
 *
 * Panel-based desktop matching the PhantomOS simulation GUI.
 * Sidebar with expandable categories, app grid, right panels
 * (AI Governor + Assistant), dock, and status bar.
 * Sidebar sub-items and app icons open floating WM windows.
 */

#include "desktop.h"
#include "desktop_panels.h"
#include "icons.h"
#include "framebuffer.h"
#include "graphics.h"
#include "font.h"
#include "wm.h"
#include "widgets.h"
#include "mouse.h"
#include "keyboard.h"
#include "timer.h"
#include "heap.h"
#include "pmm.h"
#include "geofs.h"
#include "fbcon.h"
#include "shell.h"
#include "process.h"
#include "governor.h"
#include "gpu_hal.h"
#include "pci.h"
#include "usb.h"
#include "usb_hid.h"
#include "acpi.h"
#include "vm_detect.h"
#include "virtio_net.h"
#include <stdint.h>
#include <stddef.h>

/*============================================================================
 * External Declarations
 *============================================================================*/

extern int kprintf(const char *fmt, ...);
extern void *memset(void *s, int c, size_t n);
extern void *memcpy(void *dest, const void *src, size_t n);
extern void *memmove(void *dest, const void *src, size_t n);
extern size_t strlen(const char *s);
extern int strcmp(const char *s1, const char *s2);
extern int strncmp(const char *s1, const char *s2, size_t n);
extern char *strncpy(char *dest, const char *src, size_t n);
extern char *strcpy(char *dest, const char *src);
extern char *strrchr(const char *s, int c);

/* kprintf capture hook (defined in freestanding/stdio.c) */
extern char *kprintf_capture_buf;
extern int  *kprintf_capture_len;
extern int   kprintf_capture_max;

/*============================================================================
 * Sidebar Categories (matching simulation gui.c)
 *============================================================================*/

static struct sidebar_category sidebar_cats[SIDEBAR_CAT_COUNT] = {
    { "CORE", 3, {
        { "Desktop",   "desktop"  },
        { "Files",     "files"    },
        { "Terminal",  "terminal" },
    }},
    { "SYSTEM", 6, {
        { "Processes", "processes" },
        { "Services",  "services"  },
        { "Governor",  "governor"  },
        { "Geology",   "geology"   },
        { "GPU",       "gpumon"    },
        { "VM Info",   "vminfo"    },
    }},
    { "SECURITY", 6, {
        { "Security",  "security"  },
        { "DNAuth",    "dnauth"    },
        { "MusiKey",   "musikey"   },
        { "LifeAuth",  "lifeauth"  },
        { "BioSense",  "biosense"  },
        { "PVE Encrypt","pve"      },
    }},
    { "NETWORK", 2, {
        { "Network",   "network"   },
        { "QRNet",     "qrnet"     },
    }},
    { "APPS", 3, {
        { "Notes",     "notes"     },
        { "Media",     "media"     },
        { "ArtOS",     "artos"     },
    }},
    { "UTILITIES", 4, {
        { "Users",       "users"      },
        { "PhantomPods", "pods"       },
        { "Backup",      "backup"     },
        { "Desktop Lab", "desktoplab" },
    }},
    { "REFERENCE", 2, {
        { "Constitution", "constitution" },
        { "AI Assistant", "ai"           },
    }},
};

/*============================================================================
 * Desktop State
 *============================================================================*/

static kgeofs_volume_t *fs_vol = NULL;
static int selected_category = 0;       /* Default: CORE (like simulation) */
static int active_input = 0;            /* 0=none, 1=AI input */
static uint8_t prev_buttons = 0;        /* For click edge detection */

/* Sidebar expand animation state */
static int sidebar_anim_height = -1;    /* -1 = no animation, else current px height */
static int sidebar_anim_target = 0;     /* Target expanded height in pixels */

/* AI Assistant state */
static struct ai_assistant_state ai_state;

/* AI Tutorial state */
static struct {
    int active;
    int page;
    int total_pages;
} ai_tutorial;

/* App entries (for center grid and dock) */
static struct app_entry desktop_apps[APP_GRID_MAX];
static int desktop_app_count = 0;

/* Window IDs for launched apps (0 = not open) */
static int sysinfo_win = 0;
static int filebrowser_win = 0;
static int terminal_win = 0;
static int processes_win = 0;
static int governor_win = 0;
static int geology_win = 0;
static int constitution_win = 0;
static int network_win = 0;
static int artos_win = 0;
static int musikey_win = 0;
static int vminfo_win = 0;
static int settings_win = 0;
static int security_win = 0;
static int dnauth_win = 0;
static int lifeauth_win = 0;
static int biosense_win = 0;
static int qrnet_win = 0;
static int notes_win = 0;
static int media_win = 0;
static int users_win = 0;
static int pods_win = 0;
static int backup_win = 0;
static int desktoplab_win = 0;
static int gpumon_win = 0;
static int pve_win = 0;

/* Close callback: reset window ID so it can be reopened */
static void desktop_on_close(struct wm_window *win)
{
    int id = win->id;
    if (id == sysinfo_win) sysinfo_win = 0;
    else if (id == filebrowser_win) filebrowser_win = 0;
    else if (id == terminal_win) terminal_win = 0;
    else if (id == processes_win) processes_win = 0;
    else if (id == governor_win) governor_win = 0;
    else if (id == geology_win) geology_win = 0;
    else if (id == constitution_win) constitution_win = 0;
    else if (id == network_win) network_win = 0;
    else if (id == artos_win) artos_win = 0;
    else if (id == musikey_win) musikey_win = 0;
    else if (id == vminfo_win) vminfo_win = 0;
    else if (id == settings_win) settings_win = 0;
    else if (id == security_win) security_win = 0;
    else if (id == dnauth_win) dnauth_win = 0;
    else if (id == lifeauth_win) lifeauth_win = 0;
    else if (id == biosense_win) biosense_win = 0;
    else if (id == qrnet_win) qrnet_win = 0;
    else if (id == notes_win) notes_win = 0;
    else if (id == media_win) media_win = 0;
    else if (id == users_win) users_win = 0;
    else if (id == pods_win) pods_win = 0;
    else if (id == backup_win) backup_win = 0;
    else if (id == desktoplab_win) desktoplab_win = 0;
    else if (id == gpumon_win) gpumon_win = 0;
    else if (id == pve_win) pve_win = 0;
}

/* Terminal window state */
#define TERM_OUTPUT_SIZE    (16 * 1024)
#define TERM_INPUT_MAX      256
#define TERM_HISTORY_SIZE   16
#define TERM_HISTORY_CMD    256

static struct {
    /* Output scrollback */
    char    output[TERM_OUTPUT_SIZE];
    int     output_len;
    int     scroll_lines;       /* Lines scrolled up from bottom (0=bottom) */

    /* Input with cursor */
    char    input[TERM_INPUT_MAX];
    int     input_len;
    int     input_cursor;

    /* Command history ring buffer */
    char    history[TERM_HISTORY_SIZE][TERM_HISTORY_CMD];
    int     history_count;
    int     history_write;
    int     history_browse;     /* -1 = not browsing */
    char    saved_input[TERM_INPUT_MAX];
    int     saved_input_len;

    /* Scrollbar */
    struct widget_scrollbar scrollbar;
} term;

/* File browser state */
#define FB_HISTORY_MAX      16
#define FB_PREVIEW_SIZE     2048

static struct {
    /* Current path */
    char                    path[512];

    /* Navigation history */
    char                    history[FB_HISTORY_MAX][512];
    int                     history_count;

    /* File listing */
    struct widget_list      file_list;
    char                    file_names[64][128];
    int                     file_is_dir[64];

    /* Preview */
    char                    preview_buf[FB_PREVIEW_SIZE];
    int                     preview_valid;
    char                    preview_name[128];
    uint64_t                preview_size;

    /* Filter */
    struct widget_textinput filter_input;
    int                     filter_active;   /* 1=filter input has focus */

    /* Multi-mode dialog (0=none, 1=mkdir, 2=new file, 3=rename, 4=copy, 5=hide confirm, 6=snapshot) */
    int                     dialog_mode;
    char                    dialog_title[64];
    struct widget_textinput dialog_input;
    struct widget_textinput dialog_input2;   /* Second input (new file content) */
    int                     dialog_focus;    /* 0=first input, 1=second */

    /* Buttons */
    struct widget_button    back_btn;
    struct widget_button    up_btn;

    /* Action toolbar */
    struct widget_button    newfile_btn;
    struct widget_button    hide_btn;
    struct widget_button    rename_btn;
    struct widget_button    copy_btn;
    struct widget_button    snap_btn;
    struct widget_button    save_btn;

    /* Tab bar: Files / Views */
    int                     active_tab;     /* 0=Files, 1=Views */

    /* View history list */
    struct widget_list      view_list;
    char                    view_names[32][128];
    uint64_t                view_ids[32];
    int                     view_count;

    /* Selected file full path (for operations) */
    char                    selected_path[512];

    /* Scrollbar */
    struct widget_scrollbar scrollbar;
} fb;

/*============================================================================
 * ArtOS State (Digital Art Studio) — v2 Overhaul
 *============================================================================*/

/* Canvas dimensions */
#define ARTOS_CANVAS_W      400
#define ARTOS_CANVAS_H      300
#define ARTOS_MAX_UNDO      10
#define ARTOS_MAX_LAYERS    4
#define ARTOS_PALETTE_COUNT 16
#define ARTOS_MAX_POLY_VERTS 32
#define ARTOS_MAX_BRUSH     10
#define ARTOS_MAX_OPACITY   255
#define ARTOS_OPACITY_STEP  16

/* Layout constants */
#define ARTOS_TOOLBAR_H     132
#define ARTOS_PALETTE_H     44
#define ARTOS_LAYER_PANEL_W 60
#define ARTOS_MARGIN        8
#define ARTOS_BTN_W         44
#define ARTOS_BTN_H         18
#define ARTOS_BTN_GAP       2
#define ARTOS_HUE_BAR_W     128
#define ARTOS_HUE_BAR_H     12
#define ARTOS_SV_BOX_SIZE   32

/* Tool types */
#define ARTOS_TOOL_PENCIL   0
#define ARTOS_TOOL_LINE     1
#define ARTOS_TOOL_RECT     2
#define ARTOS_TOOL_FILLRECT 3
#define ARTOS_TOOL_ELLIPSE  4
#define ARTOS_TOOL_FILL     5
#define ARTOS_TOOL_ERASER   6
#define ARTOS_TOOL_EYEDROP  7
#define ARTOS_TOOL_TEXT     8
#define ARTOS_TOOL_POLYGON  9
#define ARTOS_TOOL_SPRAY    10
#define ARTOS_TOOL_SELECT   11
#define ARTOS_TOOL_RNDRECT  12
#define ARTOS_TOOL_CIRCLE   13
#define ARTOS_TOOL_STAR     14
#define ARTOS_TOOL_ARROW    15
#define ARTOS_TOOL_BEZIER   16
#define ARTOS_TOOL_GRADFILL 17
#define ARTOS_TOOL_DITHFILL 18
#define ARTOS_TOOL_CALLIG   19
#define ARTOS_TOOL_SOFTBRUSH 20
#define ARTOS_TOOL_PATBRUSH 21
#define ARTOS_TOOL_CLONE    22
#define ARTOS_TOOL_SMUDGE   23
#define ARTOS_TOOL_COUNT    24

static const char *artos_tool_names[ARTOS_TOOL_COUNT] = {
    "Pencil", "Line", "Rect", "FillR", "Ellip", "Fill",
    "Erase", "Pick", "Text", "Poly", "Spray", "Select",
    "RndRc", "Circl", "Star", "Arrow", "Bezir", "GradF",
    "DithF", "Calli", "SoftB", "PatBr", "Clone", "Smudg"
};

static const uint32_t artos_palette[ARTOS_PALETTE_COUNT] = {
    0xFF000000, 0xFFFFFFFF, 0xFFFF0000, 0xFF00FF00,
    0xFF0000FF, 0xFFFFFF00, 0xFFFF00FF, 0xFF00FFFF,
    0xFF808080, 0xFFC0C0C0, 0xFF800000, 0xFF008000,
    0xFF000080, 0xFF808000, 0xFF800080, 0xFF008080,
};

/* Layer structure */
struct artos_layer {
    uint32_t    pixels[ARTOS_CANVAS_W * ARTOS_CANVAS_H];
    uint8_t     visible;
    uint8_t     opacity;    /* 0-255 */
    char        name[8];    /* "Layer 1" etc */
};

static struct {
    /* Layer stack */
    struct artos_layer layers[ARTOS_MAX_LAYERS];
    int         active_layer;
    int         layer_count;        /* 1-4 */

    /* Composite canvas (flattened for display) */
    uint32_t    composite[ARTOS_CANVAS_W * ARTOS_CANVAS_H];

    /* Undo stack: snapshots of active layer only */
    uint32_t    undo[ARTOS_MAX_UNDO][ARTOS_CANVAS_W * ARTOS_CANVAS_H];
    int         undo_count;
    int         undo_pos;

    /* Current tool and colors */
    int         tool;
    uint32_t    fg_color;
    uint32_t    bg_color;
    int         brush_size;         /* 1-10 */
    int         brush_opacity;      /* 0-255 */

    /* Shape drawing state */
    int         drawing;
    int         start_cx, start_cy;
    int         last_cx, last_cy;
    uint32_t    shape_save[ARTOS_CANVAS_W * ARTOS_CANVAS_H];

    /* Zoom and pan */
    int         zoom;               /* 1, 2, or 3 */
    int         scroll_x, scroll_y; /* Pan offset in canvas pixels */

    /* Text tool state */
    char        text_buf[128];
    int         text_cursor;
    int         text_cx, text_cy;   /* Canvas insertion point */
    int         text_active;

    /* Polygon tool state */
    int         poly_verts[ARTOS_MAX_POLY_VERTS][2];
    int         poly_count;

    /* Selection tool state */
    int         sel_active;
    int         sel_x1, sel_y1, sel_x2, sel_y2;
    int         sel_moving;
    int         sel_move_ox, sel_move_oy;
    uint32_t    sel_buf[ARTOS_CANVAS_W * ARTOS_CANVAS_H];

    /* Bezier tool state */
    int         bezier_pts[4][2];
    int         bezier_count;

    /* Star tool */
    int         star_sides;         /* 3-8 */

    /* Clone stamp state */
    int         clone_src_x, clone_src_y;
    int         clone_src_set;
    int         clone_off_x, clone_off_y;

    /* Smudge tool buffer (max 21x21 brush area) */
    uint32_t    smudge_buf[441];

    /* Edit modes */
    int         mirror_mode;
    int         grid_snap;
    int         grid_size;          /* 4 or 8 */

    /* HSV color picker state */
    int         hsv_h;              /* 0-359 */
    int         hsv_s;              /* 0-255 */
    int         hsv_v;              /* 0-255 */

    /* UI layout (computed on paint) */
    int         toolbar_h;
    int         palette_h;
    int         canvas_ox, canvas_oy;
    int         pixel_scale;

    int         modified;

    /* AI Art Generator */
    char        ai_prompt[64];
    int         ai_prompt_cursor;
    int         ai_input_active;
    uint32_t    ai_rand_seed;

    /* DrawNet Collaboration */
    int         drawnet_enabled;
    char        drawnet_session_id[16];
    uint64_t    drawnet_last_sync_ms;
    int         drawnet_peer_count;
    struct {
        char        name[16];
        int         cursor_x, cursor_y;
        uint32_t    color;
        uint64_t    last_seen_ms;
    } drawnet_peers[8];
    uint32_t    drawnet_stroke_seq;
    char        drawnet_input[16];
    int         drawnet_input_active;
} art;

/*============================================================================
 * MusiKey State (Musical Authentication)
 *============================================================================*/

#define MK_MAX_USERS        8
#define MK_USERNAME_MAX     32
#define MK_PASSPHRASE_MAX   64
#define MK_COMPOSITION_LEN  32      /* Notes in a musical key (was 16) */
#define MK_PIANO_KEYS       24      /* 2 octaves of white keys */
#define MK_VIS_BARS         32      /* Audio visualizer bars */

/* Authentication animation phases */
#define MK_ANIM_NONE        0
#define MK_ANIM_GENERATING  1
#define MK_ANIM_ANALYZING   2
#define MK_ANIM_VERIFYING   3
#define MK_ANIM_RESULT      4

/* Musicality score indices */
#define MK_SCORE_HARMONIC   0
#define MK_SCORE_MELODIC    1
#define MK_SCORE_RHYTHM     2
#define MK_SCORE_SCALE      3
#define MK_NUM_SCORES       4

/* Note duration types */
#define MK_DUR_SHORT        1
#define MK_DUR_NORMAL       2
#define MK_DUR_LONG         3

/* Which white key indices have a black key to the right
 * (piano pattern: C#, D#, skip E#, F#, G#, A#, skip B#) */
static int mk_has_black(int white_idx)
{
    int n = white_idx % 7;
    return (n != 2 && n != 6); /* No black after E(2) and B(6) */
}

/* Pentatonic scale: C D E G A mapped to white key indices across 3 octaves */
static const int mk_pentatonic[15] = {
    0, 1, 2, 4, 5,      /* Octave 0: C D E G A */
    7, 8, 9, 11, 12,    /* Octave 1: C D E G A */
    14, 15, 16, 18, 19   /* Octave 2: C D E G A */
};

/* Key names for display */
static const char * const mk_key_names[12] = {
    "C", "C#", "D", "D#", "E", "F",
    "F#", "G", "G#", "A", "A#", "B"
};

/* White key note labels (C-B repeating) */
static const char mk_white_labels[] = "CDEFGABCDEFGABCDEFGABCDEF";

/* Integer Hz frequencies for the 15 pentatonic tones (C4-A6) */
static const uint16_t mk_penta_freq[15] = {
    262, 294, 330, 392, 440,       /* C4 D4 E4 G4 A4 */
    523, 587, 659, 784, 880,       /* C5 D5 E5 G5 A5 */
    1047, 1175, 1319, 1568, 1760   /* C6 D6 E6 G6 A6 */
};

/* Tone data: 32 freqs (2 bytes each) + 32 durations (1 byte each) = 96 bytes */
#define MK_TONE_DATA_LEN  (MK_COMPOSITION_LEN * 3)

/* Derive keystream from passphrase + salt via key-stretched LCG */
static void mk_derive_keystream(const char *passphrase, uint32_t salt,
                                uint8_t *out, int len)
{
    uint32_t state = salt ^ 0xA5A5A5A5;
    for (const char *p = passphrase; *p; p++)
        state = state * 2654435761u + (uint8_t)*p;
    /* Key stretching: 256 LCG rounds */
    for (int r = 0; r < 256; r++)
        state = state * 1103515245 + 12345;
    /* Generate keystream */
    for (int i = 0; i < len; i++) {
        state = state * 1103515245 + 12345;
        out[i] = (uint8_t)(state >> 16);
    }
}

/* FNV-1a 32-bit hash for verification */
static uint32_t mk_compute_hash(const uint8_t *data, int len)
{
    uint32_t hash = 2166136261u;
    for (int i = 0; i < len; i++) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

/* XOR scramble/descramble (symmetric) */
static void mk_scramble(uint8_t *data, int data_len,
                        const char *passphrase, uint32_t salt)
{
    uint8_t ks[MK_TONE_DATA_LEN];
    mk_derive_keystream(passphrase, salt, ks, data_len);
    for (int i = 0; i < data_len; i++)
        data[i] ^= ks[i];
}

/* Pack frequencies + durations into flat buffer */
static void mk_pack_tone_data(const uint16_t *freqs, const char *durs,
                              uint8_t *buf)
{
    for (int i = 0; i < MK_COMPOSITION_LEN; i++) {
        buf[i * 2]     = (uint8_t)(freqs[i] & 0xFF);
        buf[i * 2 + 1] = (uint8_t)(freqs[i] >> 8);
    }
    for (int i = 0; i < MK_COMPOSITION_LEN; i++)
        buf[MK_COMPOSITION_LEN * 2 + i] = (uint8_t)durs[i];
}

/* Unpack flat buffer to frequencies + durations */
static void mk_unpack_tone_data(const uint8_t *buf, uint16_t *freqs,
                                char *durs)
{
    for (int i = 0; i < MK_COMPOSITION_LEN; i++)
        freqs[i] = (uint16_t)buf[i * 2] | ((uint16_t)buf[i * 2 + 1] << 8);
    for (int i = 0; i < MK_COMPOSITION_LEN; i++)
        durs[i] = (char)buf[MK_COMPOSITION_LEN * 2 + i];
}

struct mk_user {
    char        username[MK_USERNAME_MAX];
    uint8_t     scrambled_data[MK_TONE_DATA_LEN]; /* XOR-encrypted freqs + durs */
    uint32_t    verify_hash;    /* FNV-1a of plaintext tone data */
    uint32_t    salt;           /* From timer_ticks for key derivation */
    int         entropy_bits;
    int         enrolled;       /* 1 = active */
    int         scale_key;      /* 0-11 root note */
    int         scores[MK_NUM_SCORES]; /* Musicality scores 0-100 */
};

static struct {
    /* Enrolled users */
    struct mk_user  users[MK_MAX_USERS];
    int             user_count;

    /* Input fields */
    struct widget_textinput  username_input;
    struct widget_textinput  passphrase_input;
    int             active_field;   /* 0=username, 1=passphrase */

    /* Buttons */
    struct widget_button    enroll_btn;
    struct widget_button    auth_btn;
    struct widget_button    play_btn;

    /* Status */
    char            status_msg[128];
    uint32_t        status_color;
    int             authenticated;  /* 1 = last auth succeeded */

    /* Audio visualizer bars (0-15 height each) */
    int             vis_bars[MK_VIS_BARS];
    int             vis_target[MK_VIS_BARS];   /* Target heights for decay */
    int             vis_active;     /* Animating? */
    int             vis_tick;

    /* Piano state */
    int             key_pressed;    /* -1 = none, 0-23 = white key */
    int             black_pressed;  /* -1 = none */

    /* Generated composition for preview */
    char            preview_comp[MK_COMPOSITION_LEN];
    char            preview_dur[MK_COMPOSITION_LEN]; /* Duration per note */
    int             preview_len;
    int             preview_playing;
    int             preview_pos;
    int             preview_tick;

    /* Authentication animation state machine */
    int             anim_phase;     /* MK_ANIM_NONE..MK_ANIM_RESULT */
    int             anim_tick;
    int             anim_progress;  /* 0-100 */
    int             anim_result;    /* 1=pass, 0=fail */
    char            anim_comp[MK_COMPOSITION_LEN];
    char            anim_dur[MK_COMPOSITION_LEN];
    int             anim_entropy;

    /* Musicality analysis display */
    int             show_analysis;  /* 1 = show panel */
    int             analysis_scores[MK_NUM_SCORES]; /* 0-100 each */
    int             analysis_key;   /* Scale key 0-11 */
    char            analysis_key_name[16];

    /* Melody contour */
    int             contour_notes[MK_COMPOSITION_LEN]; /* Pitch values */
    int             contour_len;

    /* PC Speaker tone playback */
    int             tone_playing;    /* 1 = speaker active */
    int             tone_index;      /* Current note being played */
    int             tone_tick;       /* Ticks on current note */
    uint16_t        tone_freqs[MK_COMPOSITION_LEN]; /* Descrambled frequencies */
    char            tone_durs[MK_COMPOSITION_LEN];  /* Descrambled durations */
    int             tone_len;        /* Number of notes to play */
    int             tone_error;      /* 1 = play error buzz */
} mk;

/*============================================================================
 * Helper: copy string to buffer
 *============================================================================*/

static void str_copy(char *dst, const char *src, int max)
{
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

/*============================================================================
 * System Info / Monitor Window
 *============================================================================*/

static void sysinfo_paint(struct wm_window *win)
{
    int y = 8;

    widget_label(win, 8, y, "SYSTEM MONITOR", COLOR_HIGHLIGHT);
    y += 24;

    const struct pmm_stats *pmm = pmm_get_stats();
    const struct heap_stats *heap = heap_get_stats();

    widget_label(win, 8, y, "Physical Memory:", COLOR_TEXT_DIM);
    y += 18;

    char buf[64];
    uint64_t total_mb = (pmm->total_pages * 4) / 1024;
    uint64_t used_mb = ((pmm->total_pages - pmm->free_pages) * 4) / 1024;

    buf[0] = ' '; buf[1] = ' ';
    int pos = 2;
    if (used_mb >= 100) buf[pos++] = '0' + (char)(used_mb / 100);
    if (used_mb >= 10) buf[pos++] = '0' + (char)((used_mb / 10) % 10);
    buf[pos++] = '0' + (char)(used_mb % 10);
    buf[pos++] = '/';
    if (total_mb >= 100) buf[pos++] = '0' + (char)(total_mb / 100);
    if (total_mb >= 10) buf[pos++] = '0' + (char)((total_mb / 10) % 10);
    buf[pos++] = '0' + (char)(total_mb % 10);
    buf[pos++] = ' '; buf[pos++] = 'M'; buf[pos++] = 'B';
    buf[pos] = '\0';

    widget_label(win, 8, y, buf, COLOR_TEXT);
    y += 18;

    int mem_pct = 0;
    if (pmm->total_pages > 0)
        mem_pct = (int)(((pmm->total_pages - pmm->free_pages) * 100) / pmm->total_pages);
    widget_progress(win, 8, y, wm_content_width(win) - 16, 12,
                    mem_pct, COLOR_HIGHLIGHT, 0xFF0D0D1A);
    y += 20;

    widget_label(win, 8, y, "Kernel Heap:", COLOR_TEXT_DIM);
    y += 18;

    uint64_t heap_kb = heap->used_size / 1024;
    uint64_t heap_total_kb = heap->total_size / 1024;
    pos = 2;
    buf[0] = ' '; buf[1] = ' ';
    if (heap_kb >= 100) buf[pos++] = '0' + (char)(heap_kb / 100);
    if (heap_kb >= 10) buf[pos++] = '0' + (char)((heap_kb / 10) % 10);
    buf[pos++] = '0' + (char)(heap_kb % 10);
    buf[pos++] = '/';
    if (heap_total_kb >= 1000) buf[pos++] = '0' + (char)(heap_total_kb / 1000);
    if (heap_total_kb >= 100) buf[pos++] = '0' + (char)((heap_total_kb / 100) % 10);
    if (heap_total_kb >= 10) buf[pos++] = '0' + (char)((heap_total_kb / 10) % 10);
    buf[pos++] = '0' + (char)(heap_total_kb % 10);
    buf[pos++] = ' '; buf[pos++] = 'K'; buf[pos++] = 'B';
    buf[pos] = '\0';

    widget_label(win, 8, y, buf, COLOR_TEXT);
    y += 24;

    widget_label(win, 8, y, "Uptime:", COLOR_TEXT_DIM);
    y += 18;

    uint64_t ticks = timer_get_ticks();
    uint64_t secs = ticks / 100;
    uint64_t mins = secs / 60;
    secs %= 60;

    pos = 2;
    buf[0] = ' '; buf[1] = ' ';
    if (mins >= 10) buf[pos++] = '0' + (char)(mins / 10);
    buf[pos++] = '0' + (char)(mins % 10);
    buf[pos++] = 'm'; buf[pos++] = ' ';
    buf[pos++] = '0' + (char)(secs / 10);
    buf[pos++] = '0' + (char)(secs % 10);
    buf[pos++] = 's';
    buf[pos] = '\0';

    widget_label(win, 8, y, buf, COLOR_TEXT);
    y += 24;

    if (fs_vol) {
        widget_label(win, 8, y, "GeoFS Volume:", COLOR_TEXT_DIM);
        y += 18;

        struct kgeofs_stats stats;
        kgeofs_volume_stats(fs_vol, &stats);

        pos = 2;
        buf[0] = ' '; buf[1] = ' ';
        uint64_t refs = stats.ref_count;
        if (refs >= 100) buf[pos++] = '0' + (char)(refs / 100);
        if (refs >= 10) buf[pos++] = '0' + (char)((refs / 10) % 10);
        buf[pos++] = '0' + (char)(refs % 10);
        buf[pos++] = ' '; buf[pos++] = 'r'; buf[pos++] = 'e';
        buf[pos++] = 'f'; buf[pos++] = 's';
        buf[pos] = '\0';
        widget_label(win, 8, y, buf, COLOR_TEXT);
    }
}

/*============================================================================
 * File Browser Window
 *============================================================================*/

/* Filter context for listing callback */
static const char *fb_filter_str = NULL;

/* Case-insensitive substring match */
static int str_contains_ci(const char *haystack, const char *needle)
{
    if (!needle || !needle[0]) return 1;
    int nlen = (int)strlen(needle);
    int hlen = (int)strlen(haystack);
    for (int i = 0; i <= hlen - nlen; i++) {
        int match = 1;
        for (int j = 0; j < nlen; j++) {
            char a = haystack[i + j];
            char b = needle[j];
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) { match = 0; break; }
        }
        if (match) return 1;
    }
    return 0;
}

static int fb_list_callback(const struct kgeofs_dirent *entry, void *ctx)
{
    (void)ctx;
    if (fb.file_list.count >= WIDGET_LIST_MAX_ITEMS) return 1;

    /* Apply filter */
    if (fb_filter_str && fb_filter_str[0]) {
        if (!str_contains_ci(entry->name, fb_filter_str))
            return 0;
    }

    int i = fb.file_list.count;
    if (entry->is_directory) {
        fb.file_names[i][0] = '['; fb.file_names[i][1] = 'D';
        fb.file_names[i][2] = ']'; fb.file_names[i][3] = ' ';
        strncpy(fb.file_names[i] + 4, entry->name, 120);
        fb.file_is_dir[i] = 1;
    } else {
        fb.file_names[i][0] = ' '; fb.file_names[i][1] = ' ';
        fb.file_names[i][2] = ' '; fb.file_names[i][3] = ' ';
        strncpy(fb.file_names[i] + 4, entry->name, 120);
        fb.file_is_dir[i] = 0;
    }
    fb.file_names[i][127] = '\0';
    fb.file_list.items[i] = fb.file_names[i];
    fb.file_list.count++;
    return 0;
}

static void fb_refresh(void)
{
    fb.file_list.count = 0;
    fb.file_list.scroll_offset = 0;
    fb.file_list.selected = -1;
    fb.preview_valid = 0;

    /* Set filter from text input */
    const char *filter = widget_textinput_text(&fb.filter_input);
    fb_filter_str = (filter && filter[0]) ? filter : NULL;

    /* Add ".." entry if not at root */
    if (fb.path[0] == '/' && fb.path[1] != '\0') {
        int i = fb.file_list.count;
        fb.file_names[i][0] = '['; fb.file_names[i][1] = 'D';
        fb.file_names[i][2] = ']'; fb.file_names[i][3] = ' ';
        fb.file_names[i][4] = '.'; fb.file_names[i][5] = '.';
        fb.file_names[i][6] = '\0';
        fb.file_is_dir[i] = 1;
        fb.file_list.items[i] = fb.file_names[i];
        fb.file_list.count++;
    }

    if (fs_vol) {
        kgeofs_ref_list(fs_vol, fb.path, fb_list_callback, NULL);
    }
    fb_filter_str = NULL;
}

static void fb_navigate(const char *new_path)
{
    /* Push current path to history */
    if (fb.history_count < FB_HISTORY_MAX) {
        strcpy(fb.history[fb.history_count], fb.path);
        fb.history_count++;
    }
    strcpy(fb.path, new_path);
    fb_refresh();
}

static void fb_go_up(void)
{
    if (fb.path[0] == '/' && fb.path[1] == '\0') return;
    char *last = strrchr(fb.path, '/');
    if (last && last != fb.path) {
        *last = '\0';
    } else {
        fb.path[0] = '/';
        fb.path[1] = '\0';
    }
    fb_refresh();
}

static void fb_go_back(void)
{
    if (fb.history_count > 0) {
        fb.history_count--;
        strcpy(fb.path, fb.history[fb.history_count]);
        fb_refresh();
    }
}

/* Select file and load preview */
static void fb_select_entry(int idx)
{
    if (idx < 0 || idx >= fb.file_list.count) return;

    /* Extract name (skip [D] prefix) */
    const char *display = fb.file_names[idx];
    const char *name = display + 4;

    if (fb.file_is_dir[idx]) {
        if (name[0] == '.' && name[1] == '.' && name[2] == '\0') {
            fb_go_up();
            return;
        }
        /* Navigate into directory */
        char new_path[512];
        int plen = (int)strlen(fb.path);
        strncpy(new_path, fb.path, 500);
        if (plen > 1) {
            new_path[plen] = '/';
            strncpy(new_path + plen + 1, name, (size_t)(500 - plen));
        } else {
            new_path[0] = '/';
            strncpy(new_path + 1, name, 498);
        }
        new_path[511] = '\0';
        fb_navigate(new_path);
    } else {
        /* Preview file */
        char full_path[512];
        int plen = (int)strlen(fb.path);
        strncpy(full_path, fb.path, 500);
        if (plen > 1) {
            full_path[plen] = '/';
            strncpy(full_path + plen + 1, name, (size_t)(500 - plen));
        } else {
            full_path[0] = '/';
            strncpy(full_path + 1, name, 498);
        }
        full_path[511] = '\0';

        str_copy(fb.preview_name, name, 128);
        fb.preview_valid = 0;

        if (fs_vol) {
            size_t size_out;
            kgeofs_error_t err = kgeofs_file_read(fs_vol, full_path,
                                                   fb.preview_buf,
                                                   FB_PREVIEW_SIZE - 1,
                                                   &size_out);
            if (err == KGEOFS_OK) {
                fb.preview_buf[size_out] = '\0';
                fb.preview_valid = 1;
            }
            uint64_t fsize;
            int is_dir;
            if (kgeofs_file_stat(fs_vol, full_path, &fsize, &is_dir) == KGEOFS_OK) {
                fb.preview_size = fsize;
            } else {
                fb.preview_size = 0;
            }
        }
    }
}

static void fb_init_state(void)
{
    strcpy(fb.path, "/");
    fb.history_count = 0;
    fb.preview_valid = 0;
    fb.dialog_mode = 0;
    fb.dialog_focus = 0;
    fb.filter_active = 0;
    fb.active_tab = 0;
    fb.view_count = 0;
    fb.selected_path[0] = '\0';

    memset(&fb.file_list, 0, sizeof(fb.file_list));
    memset(&fb.view_list, 0, sizeof(fb.view_list));

    widget_textinput_init(&fb.filter_input, 170, 40, 220, 16);
    widget_textinput_init(&fb.dialog_input, 20, 40, 200, 20);
    widget_textinput_init(&fb.dialog_input2, 20, 70, 200, 20);

    /* Top bar buttons */
    fb.back_btn.x = 4; fb.back_btn.y = 2;
    fb.back_btn.w = 40; fb.back_btn.h = 18;
    fb.back_btn.text = "Back";
    fb.back_btn.bg_color = COLOR_BUTTON_PRIMARY;
    fb.back_btn.text_color = COLOR_WHITE;
    fb.back_btn.hovered = 0;

    fb.up_btn.x = 48; fb.up_btn.y = 2;
    fb.up_btn.w = 24; fb.up_btn.h = 18;
    fb.up_btn.text = "Up";
    fb.up_btn.bg_color = COLOR_BUTTON_PRIMARY;
    fb.up_btn.text_color = COLOR_WHITE;
    fb.up_btn.hovered = 0;

    /* Action toolbar (y=22) */
    fb.newfile_btn.x = 4;   fb.newfile_btn.y = 22;
    fb.newfile_btn.w = 42;  fb.newfile_btn.h = 16;
    fb.newfile_btn.text = "New";
    fb.newfile_btn.bg_color = COLOR_BUTTON_PRIMARY;
    fb.newfile_btn.text_color = COLOR_WHITE;
    fb.newfile_btn.hovered = 0;

    fb.hide_btn.x = 50;     fb.hide_btn.y = 22;
    fb.hide_btn.w = 42;     fb.hide_btn.h = 16;
    fb.hide_btn.text = "Hide";
    fb.hide_btn.bg_color = COLOR_HIGHLIGHT;
    fb.hide_btn.text_color = COLOR_WHITE;
    fb.hide_btn.hovered = 0;

    fb.rename_btn.x = 96;   fb.rename_btn.y = 22;
    fb.rename_btn.w = 36;   fb.rename_btn.h = 16;
    fb.rename_btn.text = "Ren";
    fb.rename_btn.bg_color = COLOR_BUTTON;
    fb.rename_btn.text_color = COLOR_WHITE;
    fb.rename_btn.hovered = 0;

    fb.copy_btn.x = 136;    fb.copy_btn.y = 22;
    fb.copy_btn.w = 42;     fb.copy_btn.h = 16;
    fb.copy_btn.text = "Copy";
    fb.copy_btn.bg_color = COLOR_BUTTON;
    fb.copy_btn.text_color = COLOR_WHITE;
    fb.copy_btn.hovered = 0;

    fb.snap_btn.x = 182;    fb.snap_btn.y = 22;
    fb.snap_btn.w = 42;     fb.snap_btn.h = 16;
    fb.snap_btn.text = "Snap";
    fb.snap_btn.bg_color = 0xFF8B5CF6;
    fb.snap_btn.text_color = COLOR_WHITE;
    fb.snap_btn.hovered = 0;

    fb.save_btn.x = 228;    fb.save_btn.y = 22;
    fb.save_btn.w = 42;     fb.save_btn.h = 16;
    fb.save_btn.text = "Save";
    fb.save_btn.bg_color = COLOR_GREEN_ACTIVE;
    fb.save_btn.text_color = COLOR_WHITE;
    fb.save_btn.hovered = 0;

    widget_scrollbar_init(&fb.scrollbar, 0, 0, 0);

    fb_refresh();
}

/* Build full path for selected file */
static void fb_get_selected_path(void)
{
    fb.selected_path[0] = '\0';
    if (fb.file_list.selected < 0 || fb.file_list.selected >= fb.file_list.count)
        return;
    /* Skip directories for file ops */
    if (fb.file_is_dir[fb.file_list.selected])
        return;

    const char *name = fb.file_names[fb.file_list.selected] + 4; /* skip "    " or "[D] " */
    int plen = (int)strlen(fb.path);
    strncpy(fb.selected_path, fb.path, 500);
    if (plen > 1) {
        fb.selected_path[plen] = '/';
        strncpy(fb.selected_path + plen + 1, name, (size_t)(500 - plen));
    } else {
        fb.selected_path[0] = '/';
        strncpy(fb.selected_path + 1, name, 498);
    }
    fb.selected_path[511] = '\0';
}

/* Callback for view listing */
static void fb_view_list_cb(kgeofs_view_t id, kgeofs_view_t parent_id,
                              const char *label, kgeofs_time_t created, void *ctx)
{
    (void)parent_id; (void)created; (void)ctx;
    if (fb.view_count >= 32) return;
    int i = fb.view_count;
    fb.view_ids[i] = id;

    int pos = 0;
    if (fs_vol && id == kgeofs_view_current(fs_vol))
        fb.view_names[i][pos++] = '*';
    fb.view_names[i][pos++] = '[';

    /* int-to-string for view id */
    char tmp[20];
    int tl = 0;
    uint64_t v = id;
    do { tmp[tl++] = '0' + (char)(v % 10); v /= 10; } while (v);
    for (int j = tl - 1; j >= 0; j--)
        fb.view_names[i][pos++] = tmp[j];

    fb.view_names[i][pos++] = ']';
    fb.view_names[i][pos++] = ' ';
    strncpy(fb.view_names[i] + pos, label, (size_t)(127 - pos));
    fb.view_names[i][127] = '\0';
    fb.view_list.items[fb.view_count] = fb.view_names[i];
    fb.view_count++;
    fb.view_list.count = fb.view_count;
}

static void fb_refresh_views(void)
{
    fb.view_count = 0;
    fb.view_list.count = 0;
    fb.view_list.scroll_offset = 0;
    fb.view_list.selected = -1;
    if (fs_vol)
        kgeofs_view_list(fs_vol, fb_view_list_cb, NULL);
}

/* Build full path helper for dialog operations */
static void fb_build_full_path(const char *name, char *out, int out_size)
{
    int plen = (int)strlen(fb.path);
    strncpy(out, fb.path, (size_t)(out_size - 2));
    if (plen > 1) {
        out[plen] = '/';
        strncpy(out + plen + 1, name, (size_t)(out_size - plen - 2));
    } else {
        out[0] = '/';
        strncpy(out + 1, name, (size_t)(out_size - 2));
    }
    out[out_size - 1] = '\0';
}

/* Execute dialog confirm based on dialog_mode */
static void fb_dialog_confirm(void)
{
    const char *text = widget_textinput_text(&fb.dialog_input);

    switch (fb.dialog_mode) {
    case 1: /* New Folder */
        if (text[0] && fs_vol) {
            char full[512];
            fb_build_full_path(text, full, 512);
            kgeofs_mkdir(fs_vol, full);
            fb_refresh();
        }
        break;

    case 2: /* New File */
        if (text[0] && fs_vol) {
            char full[512];
            fb_build_full_path(text, full, 512);
            const char *content = widget_textinput_text(&fb.dialog_input2);
            kgeofs_file_write(fs_vol, full, content, strlen(content));
            fb_refresh();
        }
        break;

    case 3: /* Rename */
        if (text[0] && fb.selected_path[0] && fs_vol) {
            char new_path[512];
            fb_build_full_path(text, new_path, 512);
            kgeofs_file_rename(fs_vol, fb.selected_path, new_path);
            fb_refresh();
        }
        break;

    case 4: /* Copy */
        if (text[0] && fb.selected_path[0] && fs_vol) {
            char dst_path[512];
            fb_build_full_path(text, dst_path, 512);
            kgeofs_file_copy(fs_vol, fb.selected_path, dst_path);
            fb_refresh();
        }
        break;

    case 5: /* Hide (confirm) */
        if (fb.selected_path[0] && fs_vol) {
            char reason[128];
            gov_verdict_t verdict = governor_check_filesystem(
                POLICY_FS_HIDE, fb.selected_path, GOV_CAP_KERNEL, reason);
            if (verdict == GOV_ALLOW) {
                kgeofs_view_hide(fs_vol, fb.selected_path);
                fb_refresh();
            }
        }
        break;

    case 6: /* Snapshot */
        if (text[0] && fs_vol) {
            kgeofs_view_t new_view;
            kgeofs_view_create(fs_vol, text, &new_view);
            if (fb.active_tab == 1) fb_refresh_views();
        }
        break;
    }

    fb.dialog_mode = 0;
    fb.dialog_focus = 0;
    widget_textinput_clear(&fb.dialog_input);
    widget_textinput_clear(&fb.dialog_input2);
}

static void filebrowser_paint(struct wm_window *win)
{
    int cw = wm_content_width(win);
    int ch = wm_content_height(win);

    /* Row 0 (y=0..20): Back, Up, Path display */
    widget_button_draw(win, &fb.back_btn);
    widget_button_draw(win, &fb.up_btn);

    widget_label(win, 78, 5, "Path:", COLOR_TEXT_DIM);
    char path_display[40];
    int plen = (int)strlen(fb.path);
    if (plen > 38) {
        path_display[0] = '.'; path_display[1] = '.';
        strncpy(path_display + 2, fb.path + plen - 36, 37);
        path_display[39] = '\0';
    } else {
        strncpy(path_display, fb.path, 39);
        path_display[39] = '\0';
    }
    widget_label(win, 120, 5, path_display, COLOR_TEXT);

    /* Row 1 (y=22..38): Action toolbar */
    widget_button_draw(win, &fb.newfile_btn);
    widget_button_draw(win, &fb.hide_btn);
    widget_button_draw(win, &fb.rename_btn);
    widget_button_draw(win, &fb.copy_btn);
    widget_button_draw(win, &fb.snap_btn);
    widget_button_draw(win, &fb.save_btn);

    /* Row 2 (y=40..56): Tab selector + Filter */
    /* Simple tab drawing (Files | Views) */
    {
        int tab_w = 60;
        for (int t = 0; t < 2; t++) {
            int tx = 4 + t * (tab_w + 2);
            uint32_t bg = (t == fb.active_tab) ? COLOR_HIGHLIGHT : COLOR_BUTTON;
            fb_fill_rect((uint32_t)(win->x + tx), (uint32_t)(win->y + WM_TITLE_HEIGHT + 40),
                         (uint32_t)tab_w, 16, bg);
            const char *lbl = (t == 0) ? "Files" : "Views";
            font_draw_string((uint32_t)(win->x + tx + 12),
                             (uint32_t)(win->y + WM_TITLE_HEIGHT + 43),
                             lbl, COLOR_WHITE, bg);
        }
    }
    if (fb.active_tab == 0) {
        widget_label(win, 136, 43, "Filter:", COLOR_TEXT_DIM);
        widget_textinput_draw(win, &fb.filter_input);
    }

    /* Row 3+ (y=58..): Content area */
    int list_top = 58;
    int sb_w = WIDGET_SCROLLBAR_WIDTH;
    int split_x = cw * 55 / 100;
    int list_h = ch - list_top;

    if (fb.active_tab == 0) {
        /* === FILES TAB === */
        fb.file_list.x = 4;
        fb.file_list.y = list_top;
        fb.file_list.w = split_x - 4 - sb_w;
        fb.file_list.h = list_h;
        widget_list_draw(win, &fb.file_list);

        fb.scrollbar.x = split_x - sb_w;
        fb.scrollbar.y = list_top;
        fb.scrollbar.h = list_h;
        widget_scrollbar_update(&fb.scrollbar, fb.file_list.count,
                                list_h / WIDGET_LIST_ITEM_HEIGHT,
                                fb.file_list.scroll_offset);
        widget_scrollbar_draw(win, &fb.scrollbar);

        gfx_draw_vline(win->x + split_x, win->y + WM_TITLE_HEIGHT + list_top,
                       list_h, COLOR_BORDER);

        /* Preview/info panel */
        int prev_x = split_x + 4;
        int prev_w = cw - split_x - 8;
        int py = list_top + 4;

        if (fb.file_list.selected >= 0 && fb.file_list.selected < fb.file_list.count) {
            if (fb.preview_valid) {
                widget_label(win, prev_x, py, "Name:", COLOR_TEXT_DIM);
                widget_label(win, prev_x + 48, py, fb.preview_name, COLOR_TEXT);
                py += 18;

                char size_buf[32];
                int sp = 0;
                uint64_t sz = fb.preview_size;
                if (sz >= 10000) size_buf[sp++] = '0' + (char)((sz / 10000) % 10);
                if (sz >= 1000) size_buf[sp++] = '0' + (char)((sz / 1000) % 10);
                if (sz >= 100) size_buf[sp++] = '0' + (char)((sz / 100) % 10);
                if (sz >= 10) size_buf[sp++] = '0' + (char)((sz / 10) % 10);
                size_buf[sp++] = '0' + (char)(sz % 10);
                size_buf[sp++] = ' '; size_buf[sp++] = 'B';
                size_buf[sp] = '\0';

                widget_label(win, prev_x, py, "Size:", COLOR_TEXT_DIM);
                widget_label(win, prev_x + 48, py, size_buf, COLOR_TEXT);
                py += 18;

                widget_label(win, prev_x, py, "Type:", COLOR_TEXT_DIM);
                widget_label(win, prev_x + 48, py, "file", COLOR_TEXT);
                py += 22;

                widget_label(win, prev_x, py, "Preview:", COLOR_TEXT_DIM);
                py += 16;
                widget_textbox(win, prev_x, py, prev_w, ch - py - 4,
                               fb.preview_buf, COLOR_TEXT, 0xFF0D0D1A);
            } else if (!fb.file_is_dir[fb.file_list.selected]) {
                widget_label(win, prev_x, py, "Select a file", COLOR_TEXT_DIM);
                py += 16;
                widget_label(win, prev_x, py, "to preview", COLOR_TEXT_DIM);
            } else {
                const char *name = fb.file_names[fb.file_list.selected] + 4;
                widget_label(win, prev_x, py, "Directory:", COLOR_TEXT_DIM);
                widget_label(win, prev_x, py + 18, name, COLOR_TEXT);
                py += 36;
                widget_label(win, prev_x, py, "Click to enter", COLOR_TEXT_DIM);
            }
        } else {
            widget_label(win, prev_x, py, "Select a file", COLOR_TEXT_DIM);
            py += 16;
            widget_label(win, prev_x, py, "to preview", COLOR_TEXT_DIM);
        }
    } else {
        /* === VIEWS TAB === */
        fb.view_list.x = 4;
        fb.view_list.y = list_top;
        fb.view_list.w = cw - 8;
        fb.view_list.h = list_h;
        widget_list_draw(win, &fb.view_list);

        if (fb.view_count == 0) {
            widget_label(win, 20, list_top + 20, "No views found", COLOR_TEXT_DIM);
        }
    }

    /* Dialog overlay (modes 1-6) */
    if (fb.dialog_mode > 0) {
        int dh = (fb.dialog_mode == 2) ? 110 : ((fb.dialog_mode == 5) ? 70 : 85);
        int dx = 40, dy = ch / 2 - dh / 2;
        int dw = cw - 80;

        fb_fill_rect((uint32_t)(win->x + dx),
                     (uint32_t)(win->y + WM_TITLE_HEIGHT + dy),
                     (uint32_t)dw, (uint32_t)dh, COLOR_BG_PANEL);
        fb_draw_rect((uint32_t)(win->x + dx),
                     (uint32_t)(win->y + WM_TITLE_HEIGHT + dy),
                     (uint32_t)dw, (uint32_t)dh, COLOR_HIGHLIGHT);

        widget_label(win, dx + 8, dy + 6, fb.dialog_title, COLOR_TEXT);

        if (fb.dialog_mode == 5) {
            /* Hide confirm — show file name, no text input */
            const char *fn = fb.file_names[fb.file_list.selected] + 4;
            widget_label(win, dx + 8, dy + 22, fn, COLOR_ICON_ORANGE);
        } else {
            /* Primary text input */
            const char *prompt = "Name:";
            if (fb.dialog_mode == 4) prompt = "Dest:";
            if (fb.dialog_mode == 6) prompt = "Label:";
            widget_label(win, dx + 8, dy + 22, prompt, COLOR_TEXT_DIM);

            fb.dialog_input.x = dx + 48;
            fb.dialog_input.y = dy + 20;
            fb.dialog_input.w = dw - 56;
            widget_textinput_draw(win, &fb.dialog_input);

            /* Second input for new file content (mode 2 only) */
            if (fb.dialog_mode == 2) {
                widget_label(win, dx + 8, dy + 44, "Text:", COLOR_TEXT_DIM);
                fb.dialog_input2.x = dx + 48;
                fb.dialog_input2.y = dy + 42;
                fb.dialog_input2.w = dw - 56;
                widget_textinput_draw(win, &fb.dialog_input2);
            }
        }

        /* OK / Cancel buttons */
        int btn_y = dy + dh - 22;
        struct widget_button ok_btn = {dx + dw / 2 - 60, btn_y, 50, 18,
                                       "OK", COLOR_BUTTON_PRIMARY, COLOR_WHITE, 0};
        struct widget_button cancel_btn = {dx + dw / 2 + 10, btn_y, 50, 18,
                                            "Cancel", COLOR_ACCENT, COLOR_WHITE, 0};
        widget_button_draw(win, &ok_btn);
        widget_button_draw(win, &cancel_btn);
    }
}

static void filebrowser_click(struct wm_window *win, int x, int y, int button)
{
    (void)button;
    int cw = wm_content_width(win);
    int ch = wm_content_height(win);

    /* Handle dialog clicks first */
    if (fb.dialog_mode > 0) {
        int dh = (fb.dialog_mode == 2) ? 110 : ((fb.dialog_mode == 5) ? 70 : 85);
        int dx = 40, dy = ch / 2 - dh / 2;
        int dw = cw - 80;

        /* Click on text inputs */
        if (fb.dialog_mode != 5) {
            if (fb.dialog_mode == 2) {
                /* Check which input was clicked */
                if (y >= fb.dialog_input2.y && y < fb.dialog_input2.y + 20) {
                    fb.dialog_focus = 1;
                    widget_textinput_click(&fb.dialog_input2, x, y);
                } else {
                    fb.dialog_focus = 0;
                    widget_textinput_click(&fb.dialog_input, x, y);
                }
            } else {
                widget_textinput_click(&fb.dialog_input, x, y);
            }
        }

        /* OK button */
        int btn_y = dy + dh - 22;
        int ok_x = dx + dw / 2 - 60;
        if (x >= ok_x && x < ok_x + 50 && y >= btn_y && y < btn_y + 18) {
            fb_dialog_confirm();
            return;
        }

        /* Cancel button */
        int cancel_x = dx + dw / 2 + 10;
        if (x >= cancel_x && x < cancel_x + 50 && y >= btn_y && y < btn_y + 18) {
            fb.dialog_mode = 0;
            fb.dialog_focus = 0;
            widget_textinput_clear(&fb.dialog_input);
            widget_textinput_clear(&fb.dialog_input2);
        }
        return;
    }

    /* Back button */
    if (widget_button_hit(&fb.back_btn, x, y)) {
        fb_go_back();
        return;
    }

    /* Up button */
    if (widget_button_hit(&fb.up_btn, x, y)) {
        fb_go_up();
        return;
    }

    /* === Action toolbar buttons === */

    /* New File */
    if (widget_button_hit(&fb.newfile_btn, x, y)) {
        fb.dialog_mode = 2;
        strcpy(fb.dialog_title, "New File");
        fb.dialog_focus = 0;
        widget_textinput_clear(&fb.dialog_input);
        widget_textinput_clear(&fb.dialog_input2);
        return;
    }

    /* Hide */
    if (widget_button_hit(&fb.hide_btn, x, y)) {
        fb_get_selected_path();
        if (fb.selected_path[0]) {
            fb.dialog_mode = 5;
            strcpy(fb.dialog_title, "Hide file?");
        }
        return;
    }

    /* Rename */
    if (widget_button_hit(&fb.rename_btn, x, y)) {
        fb_get_selected_path();
        if (fb.selected_path[0]) {
            fb.dialog_mode = 3;
            strcpy(fb.dialog_title, "Rename");
            const char *name = fb.file_names[fb.file_list.selected] + 4;
            widget_textinput_set_text(&fb.dialog_input, name);
        }
        return;
    }

    /* Copy */
    if (widget_button_hit(&fb.copy_btn, x, y)) {
        fb_get_selected_path();
        if (fb.selected_path[0]) {
            fb.dialog_mode = 4;
            strcpy(fb.dialog_title, "Copy To");
            widget_textinput_clear(&fb.dialog_input);
        }
        return;
    }

    /* Snapshot */
    if (widget_button_hit(&fb.snap_btn, x, y)) {
        fb.dialog_mode = 6;
        strcpy(fb.dialog_title, "Create Snapshot");
        widget_textinput_clear(&fb.dialog_input);
        return;
    }

    /* Save to Disk */
    if (widget_button_hit(&fb.save_btn, x, y)) {
        if (fs_vol) {
            kgeofs_error_t err = kgeofs_volume_save(fs_vol, 0, 2048);
            kprintf("[FileBrowser] Volume saved: %s\n",
                    err == KGEOFS_OK ? "OK" : kgeofs_strerror(err));
        }
        return;
    }

    /* === Tab bar click === */
    if (y >= 40 && y < 56) {
        int tab_w = 60;
        for (int t = 0; t < 2; t++) {
            int tx = 4 + t * (tab_w + 2);
            if (x >= tx && x < tx + tab_w) {
                fb.active_tab = t;
                if (t == 1) fb_refresh_views();
                return;
            }
        }
    }

    /* Filter input (Files tab only) */
    if (fb.active_tab == 0) {
        if (x >= fb.filter_input.x && x < fb.filter_input.x + fb.filter_input.w &&
            y >= fb.filter_input.y && y < fb.filter_input.y + fb.filter_input.h) {
            fb.filter_active = 1;
            widget_textinput_click(&fb.filter_input, x, y);
            return;
        }
        fb.filter_active = 0;

        /* Scrollbar */
        int sb_w = WIDGET_SCROLLBAR_WIDTH;
        int split_x = cw * 55 / 100;
        if (x >= split_x - sb_w && x < split_x) {
            int new_off = widget_scrollbar_click(&fb.scrollbar, x, y);
            fb.file_list.scroll_offset = new_off;
            return;
        }

        /* File list click */
        int old_sel = fb.file_list.selected;
        int idx = widget_list_click(&fb.file_list, x, y);
        if (idx >= 0) {
            if (fb.file_is_dir[idx] && idx == old_sel) {
                fb_select_entry(idx);
            } else if (fb.file_is_dir[idx]) {
                fb.file_list.selected = idx;
            } else {
                fb_select_entry(idx);
            }
        }
    } else {
        /* === Views tab: view list click === */
        int idx = widget_list_click(&fb.view_list, x, y);
        if (idx >= 0 && idx < fb.view_count && fs_vol) {
            kgeofs_view_switch(fs_vol, fb.view_ids[idx]);
            fb_refresh_views();
            fb_refresh();
        }
    }
}

static void filebrowser_key(struct wm_window *win, int key)
{
    (void)win;

    /* Dialog mode — multi-mode support */
    if (fb.dialog_mode > 0) {
        if (key == '\n') {
            fb_dialog_confirm();
        } else if (key == KEY_ESCAPE) {
            fb.dialog_mode = 0;
            widget_textinput_clear(&fb.dialog_input);
            widget_textinput_clear(&fb.dialog_input2);
            fb.dialog_focus = 0;
        } else if (key == KEY_TAB && fb.dialog_mode == 2) {
            /* Tab toggles between name and content inputs in new-file mode */
            fb.dialog_focus = fb.dialog_focus ? 0 : 1;
        } else {
            /* Route key to the active input */
            if (fb.dialog_mode == 2 && fb.dialog_focus == 1)
                widget_textinput_key(&fb.dialog_input2, key);
            else
                widget_textinput_key(&fb.dialog_input, key);
        }
        return;
    }

    /* Filter mode */
    if (fb.filter_active) {
        if (key == '\n' || key == KEY_ESCAPE) {
            fb.filter_active = 0;
        } else {
            widget_textinput_key(&fb.filter_input, key);
            fb_refresh();
        }
        return;
    }

    /* Normal mode */
    if (key == KEY_UP) {
        if (fb.file_list.selected > 0) {
            fb.file_list.selected--;
            if (fb.file_list.selected < fb.file_list.scroll_offset)
                fb.file_list.scroll_offset = fb.file_list.selected;
        }
    } else if (key == KEY_DOWN) {
        if (fb.file_list.selected < fb.file_list.count - 1) {
            fb.file_list.selected++;
            int vis = fb.file_list.h / WIDGET_LIST_ITEM_HEIGHT;
            if (fb.file_list.selected >= fb.file_list.scroll_offset + vis)
                fb.file_list.scroll_offset = fb.file_list.selected - vis + 1;
        }
    } else if (key == '\n') {
        if (fb.file_list.selected >= 0)
            fb_select_entry(fb.file_list.selected);
    } else if (key == '\b' || key == KEY_BACKSPACE) {
        fb_go_back();
    }
}

/*============================================================================
 * Terminal Window
 *============================================================================*/

static void term_append(const char *text)
{
    while (*text) {
        if (term.output_len >= TERM_OUTPUT_SIZE - 1) {
            /* Compact: discard first quarter of buffer */
            int discard = TERM_OUTPUT_SIZE / 4;
            memmove(term.output, term.output + discard,
                    (size_t)(term.output_len - discard));
            term.output_len -= discard;
        }
        term.output[term.output_len++] = *text++;
    }
    term.output[term.output_len] = '\0';
}

/* Count total lines in output (for scrollbar) */
static int term_count_lines(int chars_per_line)
{
    if (chars_per_line < 1) chars_per_line = 1;
    int lines = 1;
    int col = 0;
    for (int i = 0; i < term.output_len; i++) {
        if (term.output[i] == '\n') {
            lines++;
            col = 0;
        } else {
            col++;
            if (col >= chars_per_line) { lines++; col = 0; }
        }
    }
    return lines;
}

static void terminal_paint(struct wm_window *win)
{
    int ox = win->x;
    int oy = win->y + WM_TITLE_HEIGHT;
    int cw = wm_content_width(win);
    int ch = wm_content_height(win);
    int input_h = 22;
    int sb_w = WIDGET_SCROLLBAR_WIDTH;
    int out_w = cw - sb_w;
    int out_h = ch - input_h;

    /* Output area background */
    fb_fill_rect((uint32_t)ox, (uint32_t)oy,
                 (uint32_t)out_w, (uint32_t)out_h, 0xFF0A0A14);

    /* Word-wrap rendering with scroll */
    int chars_per_line = (out_w - 8) / FONT_WIDTH;
    if (chars_per_line < 1) chars_per_line = 1;
    int visible_lines = out_h / FONT_HEIGHT;
    int total_lines = term_count_lines(chars_per_line);

    /* Clamp scroll */
    int max_scroll = total_lines - visible_lines;
    if (max_scroll < 0) max_scroll = 0;
    if (term.scroll_lines > max_scroll) term.scroll_lines = max_scroll;
    if (term.scroll_lines < 0) term.scroll_lines = 0;

    /* Target line range to display */
    int start_line = total_lines - visible_lines - term.scroll_lines;
    if (start_line < 0) start_line = 0;
    int end_line = start_line + visible_lines;

    /* Walk output buffer, rendering visible lines */
    int cur_line = 0;
    int col = 0;
    int draw_y = oy + 2;
    for (int i = 0; i < term.output_len && cur_line < end_line; i++) {
        char c = term.output[i];
        if (c == '\n') {
            cur_line++;
            col = 0;
            if (cur_line > start_line) draw_y += FONT_HEIGHT;
            continue;
        }
        if (col >= chars_per_line) {
            cur_line++;
            col = 0;
            if (cur_line > start_line) draw_y += FONT_HEIGHT;
            if (cur_line >= end_line) break;
        }
        if (cur_line >= start_line && cur_line < end_line) {
            font_draw_char((uint32_t)(ox + 4 + col * FONT_WIDTH),
                           (uint32_t)draw_y,
                           c, COLOR_TEXT, 0xFF0A0A14);
        }
        col++;
    }

    /* Scrollbar */
    term.scrollbar.x = cw - sb_w;
    term.scrollbar.y = 0;
    term.scrollbar.h = out_h;
    widget_scrollbar_update(&term.scrollbar, total_lines, visible_lines,
                            max_scroll - term.scroll_lines);
    widget_scrollbar_draw(win, &term.scrollbar);

    /* Input line separator */
    int input_y = oy + out_h;
    fb_fill_rect((uint32_t)ox, (uint32_t)input_y,
                 (uint32_t)cw, (uint32_t)input_h, 0xFF0D0D1A);
    gfx_draw_hline(ox, input_y, cw, COLOR_BORDER);

    /* Green prompt */
    font_draw_string((uint32_t)(ox + 4), (uint32_t)(input_y + 3),
                     "phantom>", COLOR_GREEN_ACTIVE, 0xFF0D0D1A);

    /* Input text with cursor */
    int prompt_w = 9 * FONT_WIDTH; /* "phantom> " */
    int avail = (cw - prompt_w - 8) / FONT_WIDTH;
    int scroll = 0;
    if (term.input_cursor > avail) scroll = term.input_cursor - avail;

    for (int i = 0; i < avail && (scroll + i) < term.input_len; i++) {
        font_draw_char((uint32_t)(ox + 4 + prompt_w + i * FONT_WIDTH),
                       (uint32_t)(input_y + 3),
                       term.input[scroll + i], COLOR_TEXT, 0xFF0D0D1A);
    }
    /* Cursor */
    int cursor_x = ox + 4 + prompt_w + (term.input_cursor - scroll) * FONT_WIDTH;
    font_draw_char((uint32_t)cursor_x, (uint32_t)(input_y + 3),
                   '_', COLOR_HIGHLIGHT, 0xFF0D0D1A);
}

static void terminal_click(struct wm_window *win, int x, int y, int button)
{
    (void)button;
    (void)win;
    /* Check scrollbar */
    int new_off = widget_scrollbar_click(&term.scrollbar, x, y);
    int max_scroll = term_count_lines((wm_content_width(win) - WIDGET_SCROLLBAR_WIDTH - 8) / FONT_WIDTH)
                     - (wm_content_height(win) - 22) / FONT_HEIGHT;
    if (max_scroll < 0) max_scroll = 0;
    term.scroll_lines = max_scroll - new_off;
    if (term.scroll_lines < 0) term.scroll_lines = 0;
}

static void terminal_key(struct wm_window *win, int key)
{
    (void)win;

    if (key == '\n') {
        /* Execute command */
        term.input[term.input_len] = '\0';

        /* Add to history if non-empty and different from last */
        if (term.input_len > 0) {
            int dup = 0;
            if (term.history_count > 0) {
                int last = (term.history_write - 1 + TERM_HISTORY_SIZE)
                           % TERM_HISTORY_SIZE;
                if (strcmp(term.history[last], term.input) == 0)
                    dup = 1;
            }
            if (!dup) {
                strncpy(term.history[term.history_write],
                        term.input, TERM_HISTORY_CMD - 1);
                term.history[term.history_write][TERM_HISTORY_CMD - 1] = '\0';
                term.history_write = (term.history_write + 1) % TERM_HISTORY_SIZE;
                if (term.history_count < TERM_HISTORY_SIZE)
                    term.history_count++;
            }
        }
        term.history_browse = -1;

        /* Echo command */
        term_append("phantom> ");
        term_append(term.input);
        term_append("\n");

        /* Execute */
        if (term.input_len > 0) {
            if (strcmp(term.input, "clear") == 0) {
                term.output_len = 0;
                term.output[0] = '\0';
                term.scroll_lines = 0;
            } else {
                /* Capture kprintf output */
                char capture_buf[4096];
                int capture_len = 0;
                capture_buf[0] = '\0';
                kprintf_capture_buf = capture_buf;
                kprintf_capture_len = &capture_len;
                kprintf_capture_max = (int)sizeof(capture_buf);

                shell_execute(term.input);

                /* Stop capture and append */
                kprintf_capture_buf = 0;
                kprintf_capture_len = 0;
                kprintf_capture_max = 0;

                if (capture_len > 0) {
                    term_append(capture_buf);
                }
            }
        }
        term.input_len = 0;
        term.input_cursor = 0;
        term.scroll_lines = 0;

    } else if (key == '\b' || key == KEY_BACKSPACE) {
        if (term.input_cursor > 0) {
            memmove(&term.input[term.input_cursor - 1],
                    &term.input[term.input_cursor],
                    (size_t)(term.input_len - term.input_cursor));
            term.input_len--;
            term.input_cursor--;
        }

    } else if (key == KEY_DELETE) {
        if (term.input_cursor < term.input_len) {
            memmove(&term.input[term.input_cursor],
                    &term.input[term.input_cursor + 1],
                    (size_t)(term.input_len - term.input_cursor - 1));
            term.input_len--;
        }

    } else if (key == KEY_LEFT) {
        if (term.input_cursor > 0) term.input_cursor--;

    } else if (key == KEY_RIGHT) {
        if (term.input_cursor < term.input_len) term.input_cursor++;

    } else if (key == KEY_HOME) {
        term.input_cursor = 0;

    } else if (key == KEY_END) {
        term.input_cursor = term.input_len;

    } else if (key == KEY_UP) {
        /* Command history: go back */
        if (term.history_count > 0) {
            if (term.history_browse < 0) {
                memcpy(term.saved_input, term.input, (size_t)term.input_len);
                term.saved_input_len = term.input_len;
                term.history_browse = term.history_count - 1;
            } else if (term.history_browse > 0) {
                term.history_browse--;
            }
            int idx = (term.history_write - term.history_count
                       + term.history_browse + TERM_HISTORY_SIZE)
                      % TERM_HISTORY_SIZE;
            int len = (int)strlen(term.history[idx]);
            memcpy(term.input, term.history[idx], (size_t)len);
            term.input_len = len;
            term.input_cursor = len;
        }

    } else if (key == KEY_DOWN) {
        /* Command history: go forward */
        if (term.history_browse >= 0) {
            if (term.history_browse < term.history_count - 1) {
                term.history_browse++;
                int idx = (term.history_write - term.history_count
                           + term.history_browse + TERM_HISTORY_SIZE)
                          % TERM_HISTORY_SIZE;
                int len = (int)strlen(term.history[idx]);
                memcpy(term.input, term.history[idx], (size_t)len);
                term.input_len = len;
                term.input_cursor = len;
            } else {
                memcpy(term.input, term.saved_input,
                       (size_t)term.saved_input_len);
                term.input_len = term.saved_input_len;
                term.input_cursor = term.input_len;
                term.history_browse = -1;
            }
        }

    } else if (key == KEY_PAGEUP) {
        term.scroll_lines += 5;

    } else if (key == KEY_PAGEDOWN) {
        term.scroll_lines -= 5;
        if (term.scroll_lines < 0) term.scroll_lines = 0;

    } else if (key >= 32 && key < 127) {
        if (term.input_len < TERM_INPUT_MAX - 1) {
            memmove(&term.input[term.input_cursor + 1],
                    &term.input[term.input_cursor],
                    (size_t)(term.input_len - term.input_cursor));
            term.input[term.input_cursor] = (char)key;
            term.input_len++;
            term.input_cursor++;
        }
        term.history_browse = -1;
    }
}

/*============================================================================
 * Settings Window
 *============================================================================*/

static void settings_paint(struct wm_window *win)
{
    int y = 8;
    widget_label(win, 8, y, "SETTINGS", COLOR_HIGHLIGHT);
    y += 24;

    widget_label(win, 8, y, "Display:", COLOR_TEXT_DIM);
    y += 18;
    {
        char dbuf[32];
        int dp = 0;
        uint32_t rw = fb_get_width(), rh = fb_get_height();
        if (rw >= 1000) dbuf[dp++] = '0' + (char)(rw / 1000);
        if (rw >= 100) dbuf[dp++] = '0' + (char)((rw / 100) % 10);
        if (rw >= 10) dbuf[dp++] = '0' + (char)((rw / 10) % 10);
        dbuf[dp++] = '0' + (char)(rw % 10);
        dbuf[dp++] = 'x';
        if (rh >= 1000) dbuf[dp++] = '0' + (char)(rh / 1000);
        if (rh >= 100) dbuf[dp++] = '0' + (char)((rh / 100) % 10);
        if (rh >= 10) dbuf[dp++] = '0' + (char)((rh / 10) % 10);
        dbuf[dp++] = '0' + (char)(rh % 10);
        dbuf[dp++] = ' '; dbuf[dp++] = '3'; dbuf[dp++] = '2';
        dbuf[dp++] = 'b'; dbuf[dp++] = 'p'; dbuf[dp++] = 'p';
        dbuf[dp] = '\0';
        widget_label(win, 16, y, dbuf, COLOR_TEXT);
    }
    y += 24;

    widget_label(win, 8, y, "Governor:", COLOR_TEXT_DIM);
    y += 18;
    widget_label(win, 16, y, "Mode: Autonomous", COLOR_TEXT);
    y += 18;
    widget_label(win, 16, y, "Prime Directive: ACTIVE", COLOR_GREEN_ACTIVE);
    y += 24;

    widget_label(win, 8, y, "Filesystem:", COLOR_TEXT_DIM);
    y += 18;
    widget_label(win, 16, y, "GeoFS (append-only)", COLOR_TEXT);
    y += 18;
    widget_label(win, 16, y, "No deletion possible", COLOR_TEXT_DIM);
}

/*============================================================================
 * Security Window
 *============================================================================*/

static void security_paint(struct wm_window *win)
{
    int y = 8;
    widget_label(win, 8, y, "SECURITY STATUS", COLOR_HIGHLIGHT);
    y += 24;

    widget_label(win, 8, y, "Governor:", COLOR_TEXT_DIM);
    y += 18;
    widget_label(win, 16, y, "Protection ACTIVE", COLOR_GREEN_ACTIVE);
    y += 24;

    widget_label(win, 8, y, "Threat Level:", COLOR_TEXT_DIM);
    y += 18;
    widget_label(win, 16, y, "Low", COLOR_GREEN_ACTIVE);
    y += 24;

    widget_label(win, 8, y, "Security Features:", COLOR_TEXT_DIM);
    y += 18;
    widget_label(win, 16, y, "* Append-only filesystem", COLOR_TEXT);
    y += 16;
    widget_label(win, 16, y, "* No delete operations", COLOR_TEXT);
    y += 16;
    widget_label(win, 16, y, "* Immutable history", COLOR_TEXT);
    y += 16;
    widget_label(win, 16, y, "* Time-travel recovery", COLOR_TEXT);
    y += 16;
    widget_label(win, 16, y, "* DNAuth (DNA-based auth)", COLOR_TEXT);
    y += 16;
    widget_label(win, 16, y, "* MusiKey (musical auth)", COLOR_TEXT);
    y += 16;
    widget_label(win, 16, y, "* LifeAuth (plasma auth)", COLOR_TEXT);
    y += 16;
    widget_label(win, 16, y, "* BioSense (vein auth)", COLOR_TEXT);
}

/*============================================================================
 * Processes Window (matches simulation process_viewer)
 *============================================================================*/

static void processes_paint(struct wm_window *win)
{
    int y = 8;
    widget_label(win, 8, y, "PROCESS VIEWER", COLOR_HIGHLIGHT);
    y += 24;

    struct scheduler_stats stats;
    sched_get_stats(&stats);

    widget_label(win, 8, y, "Active:", COLOR_TEXT_DIM);
    char buf[32];
    int pos = 0;
    uint32_t val = stats.active_processes;
    if (val >= 10) buf[pos++] = '0' + (char)(val / 10);
    buf[pos++] = '0' + (char)(val % 10);
    buf[pos++] = ' '; buf[pos++] = 'p'; buf[pos++] = 'r';
    buf[pos++] = 'o'; buf[pos++] = 'c'; buf[pos++] = 'e';
    buf[pos++] = 's'; buf[pos++] = 's'; buf[pos++] = 'e';
    buf[pos++] = 's';
    buf[pos] = '\0';
    widget_label(win, 80, y, buf, COLOR_TEXT);
    y += 20;

    widget_label(win, 8, y, "Peak:", COLOR_TEXT_DIM);
    pos = 0;
    val = stats.peak_processes;
    if (val >= 10) buf[pos++] = '0' + (char)(val / 10);
    buf[pos++] = '0' + (char)(val % 10);
    buf[pos] = '\0';
    widget_label(win, 80, y, buf, COLOR_TEXT);
    y += 20;

    widget_label(win, 8, y, "Switches:", COLOR_TEXT_DIM);
    pos = 0;
    uint64_t sw = stats.total_context_switches;
    if (sw >= 10000) buf[pos++] = '0' + (char)((sw / 10000) % 10);
    if (sw >= 1000) buf[pos++] = '0' + (char)((sw / 1000) % 10);
    if (sw >= 100) buf[pos++] = '0' + (char)((sw / 100) % 10);
    if (sw >= 10) buf[pos++] = '0' + (char)((sw / 10) % 10);
    buf[pos++] = '0' + (char)(sw % 10);
    buf[pos] = '\0';
    widget_label(win, 80, y, buf, COLOR_TEXT);
    y += 24;

    /* Note about Phantom philosophy */
    widget_label(win, 8, y, "Note: Processes can be", COLOR_TEXT_DIM);
    y += 16;
    widget_label(win, 8, y, "suspended, not killed.", COLOR_TEXT_DIM);
    y += 16;
    widget_label(win, 8, y, "\"To Create, Not Destroy\"", COLOR_ICON_PURPLE);
}

/*============================================================================
 * Governor Helpers & Shared State
 *============================================================================*/

/* Convert uint64_t to decimal string */
static void gov_u64_to_str(uint64_t val, char *buf)
{
    if (val == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    char tmp[24];
    int i = 0;
    while (val > 0) { tmp[i++] = '0' + (char)(val % 10); val /= 10; }
    int j = 0;
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = '\0';
}

/* Simple strcat for governor strings */
static char *gov_strcat(char *dest, const char *src)
{
    char *d = dest;
    while (*d) d++;
    while ((*d++ = *src++));
    return dest;
}

/* Case-insensitive substring search */
static int str_icontains(const char *haystack, const char *needle)
{
    if (!needle[0]) return 1;
    for (int i = 0; haystack[i]; i++) {
        int match = 1;
        for (int j = 0; needle[j]; j++) {
            char h = haystack[i + j];
            char n = needle[j];
            if (h >= 'A' && h <= 'Z') h += 32;
            if (n >= 'A' && n <= 'Z') n += 32;
            if (h != n) { match = 0; break; }
        }
        if (match) return 1;
    }
    return 0;
}

/*============================================================================
 * Governor Threat Level Computation
 *============================================================================*/

/* Compute dynamic threat level from violation rate */
static int gov_compute_threat_level(void)
{
    struct gov_stats st;
    governor_get_stats(&st);
    if (st.total_checks == 0) return 0; /* Low */
    uint64_t violations = st.total_denied + st.total_transformed;
    uint64_t pct = (violations * 100) / st.total_checks;
    if (pct >= 10) return 2; /* High */
    if (pct >= 3) return 1;  /* Medium */
    return 0;                 /* Low */
}

static const char *gov_threat_str(int level)
{
    if (level >= 2) return "High";
    if (level == 1) return "Medium";
    return "Low";
}

static uint32_t gov_threat_color(int level)
{
    if (level >= 2) return COLOR_HIGHLIGHT;     /* red */
    if (level == 1) return COLOR_ICON_YELLOW;   /* yellow */
    return COLOR_GREEN_ACTIVE;                   /* green */
}

/* Periodic scan counter (incremented in event loop) */
static uint64_t gov_last_scan_ticks = 0;
static uint64_t gov_scan_count = 0;

/* Predictive threat trend tracking (1-minute window, 12 slots at 5s intervals) */
#define GOV_TREND_SLOTS 12
static struct {
    uint64_t violations[GOV_TREND_SLOTS];
    int head;
    int filled;
} gov_trend;

static const char *gov_trend_str(void)
{
    if (gov_trend.filled < 3) return "Analyzing...";
    int oldest = (gov_trend.head - gov_trend.filled + GOV_TREND_SLOTS) % GOV_TREND_SLOTS;
    int newest = (gov_trend.head - 1 + GOV_TREND_SLOTS) % GOV_TREND_SLOTS;
    int64_t diff = (int64_t)gov_trend.violations[newest] - (int64_t)gov_trend.violations[oldest];
    if (diff > 2) return "Rising";
    if (diff < -2) return "Falling";
    return "Stable";
}

/* Anomaly Detection */
#define GOV_MAX_ALERTS 6
#define GOV_ALERT_MSG_LEN 64

struct gov_alert {
    int active;
    int severity;          /* 0=info, 1=warning, 2=critical */
    char msg[GOV_ALERT_MSG_LEN];
    uint64_t timestamp;    /* tick when detected */
};

static struct {
    struct gov_alert alerts[GOV_MAX_ALERTS];
    int count;
    /* Previous scan snapshot for delta detection */
    uint64_t prev_mem_used_pct;
    uint64_t prev_violations;
    uint32_t prev_processes;
    uint64_t prev_denied;
    int initialized;
} gov_anomaly;

static void gov_add_alert(int severity, const char *msg)
{
    /* Find a free slot or overwrite oldest */
    int slot = -1;
    for (int i = 0; i < GOV_MAX_ALERTS; i++) {
        if (!gov_anomaly.alerts[i].active) { slot = i; break; }
    }
    if (slot < 0) {
        /* Overwrite oldest */
        uint64_t oldest_ts = ~(uint64_t)0;
        for (int i = 0; i < GOV_MAX_ALERTS; i++) {
            if (gov_anomaly.alerts[i].timestamp < oldest_ts) {
                oldest_ts = gov_anomaly.alerts[i].timestamp;
                slot = i;
            }
        }
    }
    if (slot < 0) slot = 0;
    gov_anomaly.alerts[slot].active = 1;
    gov_anomaly.alerts[slot].severity = severity;
    strncpy(gov_anomaly.alerts[slot].msg, msg, GOV_ALERT_MSG_LEN - 1);
    gov_anomaly.alerts[slot].msg[GOV_ALERT_MSG_LEN - 1] = '\0';
    gov_anomaly.alerts[slot].timestamp = timer_get_ticks();
    gov_anomaly.count++;
    if (gov_anomaly.count > GOV_MAX_ALERTS) gov_anomaly.count = GOV_MAX_ALERTS;
}

static void gov_expire_alerts(void)
{
    uint64_t now = timer_get_ticks();
    for (int i = 0; i < GOV_MAX_ALERTS; i++) {
        if (gov_anomaly.alerts[i].active &&
            (now - gov_anomaly.alerts[i].timestamp) > 3000) { /* 30 seconds */
            gov_anomaly.alerts[i].active = 0;
        }
    }
    /* Recount */
    int c = 0;
    for (int i = 0; i < GOV_MAX_ALERTS; i++)
        if (gov_anomaly.alerts[i].active) c++;
    gov_anomaly.count = c;
}

static void gov_detect_anomalies(void)
{
    /* Get current state */
    uint64_t free_pg = pmm_get_free_pages();
    uint64_t total_pg = pmm_get_total_pages();
    uint64_t used_pct = (total_pg > 0) ? ((total_pg - free_pg) * 100) / total_pg : 0;

    struct scheduler_stats ss;
    sched_get_stats(&ss);

    struct gov_stats gs;
    governor_get_stats(&gs);

    uint64_t violations = gs.total_denied + gs.total_transformed;

    if (!gov_anomaly.initialized) {
        gov_anomaly.prev_mem_used_pct = used_pct;
        gov_anomaly.prev_violations = violations;
        gov_anomaly.prev_processes = ss.active_processes;
        gov_anomaly.prev_denied = gs.total_denied;
        gov_anomaly.initialized = 1;
        return;
    }

    /* Memory spike: >20% increase */
    if (used_pct > gov_anomaly.prev_mem_used_pct + 20) {
        char abuf[GOV_ALERT_MSG_LEN];
        abuf[0] = '\0';
        gov_strcat(abuf, "Memory spike: +");
        char n[16];
        gov_u64_to_str(used_pct - gov_anomaly.prev_mem_used_pct, n);
        gov_strcat(abuf, n);
        gov_strcat(abuf, "%");
        gov_add_alert(1, abuf);
    }

    /* Process surge: >5 new */
    if (ss.active_processes > gov_anomaly.prev_processes + 5) {
        char abuf[GOV_ALERT_MSG_LEN];
        abuf[0] = '\0';
        gov_strcat(abuf, "Process surge: ");
        char n[16];
        gov_u64_to_str(ss.active_processes - gov_anomaly.prev_processes, n);
        gov_strcat(abuf, n);
        gov_strcat(abuf, " new");
        gov_add_alert(1, abuf);
    }

    /* Violation burst: >3 new in 5s */
    if (violations > gov_anomaly.prev_violations + 3) {
        char abuf[GOV_ALERT_MSG_LEN];
        abuf[0] = '\0';
        gov_strcat(abuf, "Violation burst: ");
        char n[16];
        gov_u64_to_str(violations - gov_anomaly.prev_violations, n);
        gov_strcat(abuf, n);
        gov_strcat(abuf, " new in 5s");
        gov_add_alert(2, abuf);
    }

    /* Rapid denials: >2 new */
    if (gs.total_denied > gov_anomaly.prev_denied + 2) {
        gov_add_alert(1, "Rapid denial pattern detected");
    }

    /* Malicious pattern detection via audit entries */
    {
        int mem_denials = 0, kill_denials = 0, del_transforms = 0, exhaust_denials = 0;
        int audit_n = governor_audit_count();
        if (audit_n > 10) audit_n = 10;
        for (int i = 0; i < audit_n; i++) {
            struct gov_audit_entry ae;
            if (governor_audit_get(i, &ae) != 0) break;
            /* Only look at recent entries (last 10 seconds) */
            uint64_t age = timer_get_ticks() - ae.timestamp;
            if (age > 1000) continue;
            if (ae.policy == POLICY_MEM_FREE && ae.verdict == GOV_DENY)
                mem_denials++;
            if (ae.policy == POLICY_PROC_KILL && ae.verdict == GOV_DENY)
                kill_denials++;
            if (ae.policy == POLICY_FS_DELETE && ae.verdict == GOV_TRANSFORM)
                del_transforms++;
            if (ae.policy == POLICY_RES_EXHAUST && ae.verdict == GOV_DENY)
                exhaust_denials++;
        }
        if (mem_denials >= 3)
            gov_add_alert(2, "Memory bomb pattern detected");
        if (kill_denials >= 3)
            gov_add_alert(2, "Fork bomb/kill storm pattern");
        if (del_transforms >= 3)
            gov_add_alert(2, "Mass deletion attempt blocked");
        if (exhaust_denials >= 1)
            gov_add_alert(2, "Resource exhaustion attempt");
    }

    /* Update snapshot */
    gov_anomaly.prev_mem_used_pct = used_pct;
    gov_anomaly.prev_violations = violations;
    gov_anomaly.prev_processes = ss.active_processes;
    gov_anomaly.prev_denied = gs.total_denied;
}

/* Behavioral Learning: per-policy normalcy profile */
#define GOV_BEHAVIOR_BASELINE 100

struct gov_policy_counters {
    uint64_t allow_count;
    uint64_t deny_count;
    uint64_t transform_count;
};

static struct {
    struct gov_policy_counters current[POLICY_COUNT];
    struct gov_policy_counters baseline[POLICY_COUNT];
    int      baseline_set;
    int      deviation_count;
} gov_behavior;

/* Threat Timeline: 24-slot history for mini bar chart */
#define GOV_TIMELINE_SLOTS 24

static struct {
    int threat_level[GOV_TIMELINE_SLOTS];
    int health_score[GOV_TIMELINE_SLOTS];
    int head;
    int filled;
} gov_timeline;

/* Smart Recommendations */
#define GOV_MAX_RECS 4
#define GOV_REC_MSG_LEN 64

static struct {
    struct {
        char msg[GOV_REC_MSG_LEN];
        int  priority;
        int  active;
    } items[GOV_MAX_RECS];
    int count;
} gov_recommendations;

/* Quarantine System */
#define GOV_QUARANTINE_MAX 8
#define GOV_QUARANTINE_REASON_LEN 48

struct gov_quarantine_item {
    int active;
    gov_policy_t policy;
    gov_verdict_t verdict;
    uint32_t pid;
    uint64_t timestamp;
    char reason[GOV_QUARANTINE_REASON_LEN];
    int reviewed;
};

static struct {
    struct gov_quarantine_item items[GOV_QUARANTINE_MAX];
    int write_head;
    int capturing;
    int capture_count;
} gov_quarantine;

static void gov_quarantine_add(gov_policy_t policy, gov_verdict_t verdict,
                                uint32_t pid, const char *reason)
{
    int slot = gov_quarantine.write_head;
    gov_quarantine.items[slot].active = 1;
    gov_quarantine.items[slot].policy = policy;
    gov_quarantine.items[slot].verdict = verdict;
    gov_quarantine.items[slot].pid = pid;
    gov_quarantine.items[slot].timestamp = timer_get_ticks();
    strncpy(gov_quarantine.items[slot].reason, reason, GOV_QUARANTINE_REASON_LEN - 1);
    gov_quarantine.items[slot].reason[GOV_QUARANTINE_REASON_LEN - 1] = '\0';
    gov_quarantine.items[slot].reviewed = 0;
    gov_quarantine.write_head = (gov_quarantine.write_head + 1) % GOV_QUARANTINE_MAX;
}

/* ── PVE: Planck Variable Encryption ────────────────────────────── */
#define PVE_KEY_LEN        16
#define PVE_MSG_MAX        64
#define PVE_CIPHER_MAX     80   /* room for PKCS#7 CBC padding */
#define PVE_HISTORY_SLOTS  32

static struct {
    /* Evolving key state */
    uint8_t  current_key[PVE_KEY_LEN];
    uint64_t evolution_count;
    uint64_t planck_clock;          /* internal tick counter */

    /* Encryption workspace */
    char     plaintext[PVE_MSG_MAX];
    uint8_t  ciphertext[PVE_CIPHER_MAX];
    int      cipher_len;
    uint8_t  snapshot_key[PVE_KEY_LEN]; /* key at encryption time */
    int      has_cipher;
    char     decrypted[PVE_MSG_MAX];
    int      has_decrypted;
    uint8_t  iv[PVE_KEY_LEN];          /* CBC initialization vector */
    int      padded_len;               /* ciphertext length after padding */

    /* Key evolution history (for bar chart) */
    uint8_t  history[PVE_HISTORY_SLOTS]; /* first byte of key at each snapshot */
    int      hist_head;
    int      hist_filled;

    /* Stats */
    uint64_t total_encryptions;
    uint64_t total_decryptions;

    /* UI state */
    struct widget_textinput text_input;
    int      initialized;
} pve_state;

/* ── PVE S-box (AES Rijndael) ──────────────────────────────────── */

static const uint8_t pve_sbox[256] = {
    0x63,0x7C,0x77,0x7B,0xF2,0x6B,0x6F,0xC5,0x30,0x01,0x67,0x2B,0xFE,0xD7,0xAB,0x76,
    0xCA,0x82,0xC9,0x7D,0xFA,0x59,0x47,0xF0,0xAD,0xD4,0xA2,0xAF,0x9C,0xA4,0x72,0xC0,
    0xB7,0xFD,0x93,0x26,0x36,0x3F,0xF7,0xCC,0x34,0xA5,0xE5,0xF1,0x71,0xD8,0x31,0x15,
    0x04,0xC7,0x23,0xC3,0x18,0x96,0x05,0x9A,0x07,0x12,0x80,0xE2,0xEB,0x27,0xB2,0x75,
    0x09,0x83,0x2C,0x1A,0x1B,0x6E,0x5A,0xA0,0x52,0x3B,0xD6,0xB3,0x29,0xE3,0x2F,0x84,
    0x53,0xD1,0x00,0xED,0x20,0xFC,0xB1,0x5B,0x6A,0xCB,0xBE,0x39,0x4A,0x4C,0x58,0xCF,
    0xD0,0xEF,0xAA,0xFB,0x43,0x4D,0x33,0x85,0x45,0xF9,0x02,0x7F,0x50,0x3C,0x9F,0xA8,
    0x51,0xA3,0x40,0x8F,0x92,0x9D,0x38,0xF5,0xBC,0xB6,0xDA,0x21,0x10,0xFF,0xF3,0xD2,
    0xCD,0x0C,0x13,0xEC,0x5F,0x97,0x44,0x17,0xC4,0xA7,0x7E,0x3D,0x64,0x5D,0x19,0x73,
    0x60,0x81,0x4F,0xDC,0x22,0x2A,0x90,0x88,0x46,0xEE,0xB8,0x14,0xDE,0x5E,0x0B,0xDB,
    0xE0,0x32,0x3A,0x0A,0x49,0x06,0x24,0x5C,0xC2,0xD3,0xAC,0x62,0x91,0x95,0xE4,0x79,
    0xE7,0xC8,0x37,0x6D,0x8D,0xD5,0x4E,0xA9,0x6C,0x56,0xF4,0xEA,0x65,0x7A,0xAE,0x08,
    0xBA,0x78,0x25,0x2E,0x1C,0xA6,0xB4,0xC6,0xE8,0xDD,0x74,0x1F,0x4B,0xBD,0x8B,0x8A,
    0x70,0x3E,0xB5,0x66,0x48,0x03,0xF6,0x0E,0x61,0x35,0x57,0xB9,0x86,0xC1,0x1D,0x9E,
    0xE1,0xF8,0x98,0x11,0x69,0xD9,0x8E,0x94,0x9B,0x1E,0x87,0xE9,0xCE,0x55,0x28,0xDF,
    0x8C,0xA1,0x89,0x0D,0xBF,0xE6,0x42,0x68,0x41,0x99,0x2D,0x0F,0xB0,0x54,0xBB,0x16
};

static const uint8_t pve_inv_sbox[256] = {
    0x52,0x09,0x6A,0xD5,0x30,0x36,0xA5,0x38,0xBF,0x40,0xA3,0x9E,0x81,0xF3,0xD7,0xFB,
    0x7C,0xE3,0x39,0x82,0x9B,0x2F,0xFF,0x87,0x34,0x8E,0x43,0x44,0xC4,0xDE,0xE9,0xCB,
    0x54,0x7B,0x94,0x32,0xA6,0xC2,0x23,0x3D,0xEE,0x4C,0x95,0x0B,0x42,0xFA,0xC3,0x4E,
    0x08,0x2E,0xA1,0x66,0x28,0xD9,0x24,0xB2,0x76,0x5B,0xA2,0x49,0x6D,0x8B,0xD1,0x25,
    0x72,0xF8,0xF6,0x64,0x86,0x68,0x98,0x16,0xD4,0xA4,0x5C,0xCC,0x5D,0x65,0xB6,0x92,
    0x6C,0x70,0x48,0x50,0xFD,0xED,0xB9,0xDA,0x5E,0x15,0x46,0x57,0xA7,0x8D,0x9D,0x84,
    0x90,0xD8,0xAB,0x00,0x8C,0xBC,0xD3,0x0A,0xF7,0xE4,0x58,0x05,0xB8,0xB3,0x45,0x06,
    0xD0,0x2C,0x1E,0x8F,0xCA,0x3F,0x0F,0x02,0xC1,0xAF,0xBD,0x03,0x01,0x13,0x8A,0x6B,
    0x3A,0x91,0x11,0x41,0x4F,0x67,0xDC,0xEA,0x97,0xF2,0xCF,0xCE,0xF0,0xB4,0xE6,0x73,
    0x96,0xAC,0x74,0x22,0xE7,0xAD,0x35,0x85,0xE2,0xF9,0x37,0xE8,0x1C,0x75,0xDF,0x6E,
    0x47,0xF1,0x1A,0x71,0x1D,0x29,0xC5,0x89,0x6F,0xB7,0x62,0x0E,0xAA,0x18,0xBE,0x1B,
    0xFC,0x56,0x3E,0x4B,0xC6,0xD2,0x79,0x20,0x9A,0xDB,0xC0,0xFE,0x78,0xCD,0x5A,0xF4,
    0x1F,0xDD,0xA8,0x33,0x88,0x07,0xC7,0x31,0xB1,0x12,0x10,0x59,0x27,0x80,0xEC,0x5F,
    0x60,0x51,0x7F,0xA9,0x19,0xB5,0x4A,0x0D,0x2D,0xE5,0x7A,0x9F,0x93,0xC9,0x9C,0xEF,
    0xA0,0xE0,0x3B,0x4D,0xAE,0x2A,0xF5,0xB0,0xC8,0xEB,0xBB,0x3C,0x83,0x53,0x99,0x61,
    0x17,0x2B,0x04,0x7E,0xBA,0x77,0xD6,0x26,0xE1,0x69,0x14,0x63,0x55,0x21,0x0C,0x7D
};

/* ── PVE Helper Functions ─────────────────────────────────────── */

static void pve_byte_to_hex(uint8_t b, char *out)
{
    const char *hex = "0123456789ABCDEF";
    out[0] = hex[(b >> 4) & 0xF];
    out[1] = hex[b & 0xF];
}

static void pve_evolve_key(void)
{
    uint64_t tick = timer_get_ticks();
    pve_state.planck_clock++;
    for (int i = 0; i < PVE_KEY_LEN; i++) {
        uint64_t mix = pve_state.current_key[i];
        mix = mix * 6364136223846793005ULL + 1442695040888963407ULL;
        mix ^= tick >> (i & 7);
        mix ^= pve_state.planck_clock;
        mix ^= (uint64_t)(uintptr_t)&pve_state + i;
        pve_state.current_key[i] = (uint8_t)(mix >> 32);
    }
    pve_state.evolution_count++;
    /* Record history */
    pve_state.history[pve_state.hist_head] = pve_state.current_key[0];
    pve_state.hist_head = (pve_state.hist_head + 1) % PVE_HISTORY_SLOTS;
    if (pve_state.hist_filled < PVE_HISTORY_SLOTS) pve_state.hist_filled++;
}

/* Key stream generator: unique byte per position from seed key */
static void pve_generate_keystream(const uint8_t *seed_key, uint8_t *stream, int len)
{
    uint64_t state = 0;
    for (int i = 0; i < PVE_KEY_LEN; i++)
        state = (state << 4) ^ (state >> 3) ^ seed_key[i];
    if (state == 0) state = 0xDEADBEEFCAFE1234ULL;
    for (int i = 0; i < len; i++) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        stream[i] = (uint8_t)(state >> 33);
    }
}

/* Derive IV deterministically from key (different LCG constant) */
static void pve_derive_iv(const uint8_t *key, uint8_t *iv)
{
    uint64_t state = 0xA5A5A5A5A5A5A5A5ULL;
    for (int i = 0; i < PVE_KEY_LEN; i++)
        state ^= ((uint64_t)key[i]) << ((i * 5) & 0x3F);
    for (int i = 0; i < PVE_KEY_LEN; i++) {
        state = state * 6364136223846793005ULL + 7046029254386353131ULL;
        iv[i] = (uint8_t)(state >> 35);
    }
}

static void pve_format_key_hex(const uint8_t *key, char *buf, int buflen)
{
    int pos = 0;
    for (int i = 0; i < PVE_KEY_LEN && pos + 3 < buflen; i++) {
        if (i > 0 && pos + 1 < buflen) buf[pos++] = ' ';
        pve_byte_to_hex(key[i], &buf[pos]);
        pos += 2;
    }
    buf[pos] = '\0';
}

static void pve_format_cipher_hex(char *buf, int buflen)
{
    int pos = 0;
    int show = pve_state.padded_len;
    if (show > 16) show = 16;
    for (int i = 0; i < show && pos + 3 < buflen; i++) {
        if (i > 0 && pos + 1 < buflen) buf[pos++] = ' ';
        pve_byte_to_hex(pve_state.ciphertext[i], &buf[pos]);
        pos += 2;
    }
    if (pve_state.padded_len > 16 && pos + 4 < buflen) {
        buf[pos++] = '.'; buf[pos++] = '.'; buf[pos++] = '.';
    }
    buf[pos] = '\0';
}

/* ── PVE Init ─────────────────────────────────────────────────── */

static void pve_init_state(void)
{
    if (pve_state.initialized) return;
    /* Seed key from timer + address entropy */
    uint64_t seed = timer_get_ticks();
    for (int i = 0; i < PVE_KEY_LEN; i++) {
        seed = seed * 6364136223846793005ULL + i + 1;
        pve_state.current_key[i] = (uint8_t)(seed >> 33);
    }
    pve_state.evolution_count = 0;
    pve_state.planck_clock = 0;
    pve_state.cipher_len = 0;
    pve_state.has_cipher = 0;
    pve_state.has_decrypted = 0;
    pve_state.padded_len = 0;
    memset(pve_state.iv, 0, PVE_KEY_LEN);
    pve_state.hist_head = 0;
    pve_state.hist_filled = 0;
    pve_state.total_encryptions = 0;
    pve_state.total_decryptions = 0;
    pve_state.plaintext[0] = '\0';
    pve_state.decrypted[0] = '\0';
    /* Text input widget */
    widget_textinput_init(&pve_state.text_input, 10, 200, 260, 20);
    pve_state.text_input.max_length = PVE_MSG_MAX - 1;
    pve_state.initialized = 1;
}

/* ── PVE Encrypt/Decrypt ──────────────────────────────────────── */

static void pve_do_encrypt(void)
{
    const char *msg = widget_textinput_text(&pve_state.text_input);
    int len = strlen(msg);
    if (len == 0) return;
    if (len > PVE_MSG_MAX - 1) len = PVE_MSG_MAX - 1;

    /* Governor audit */
    governor_audit_record(POLICY_RES_EXHAUST, GOV_ALLOW,
                          GOVERNOR_DOMAIN_RESOURCE, 0, (uint64_t)len,
                          "PVE-SBC encrypt");

    /* Evolve and snapshot key */
    pve_evolve_key();
    memcpy(pve_state.snapshot_key, pve_state.current_key, PVE_KEY_LEN);

    /* Save plaintext */
    strncpy(pve_state.plaintext, msg, PVE_MSG_MAX - 1);
    pve_state.plaintext[PVE_MSG_MAX - 1] = '\0';
    pve_state.cipher_len = len;

    /* PKCS#7 padding to next block boundary */
    int pad_amt = PVE_KEY_LEN - (len % PVE_KEY_LEN);
    int padded_len = len + pad_amt;
    if (padded_len > PVE_CIPHER_MAX) padded_len = PVE_CIPHER_MAX;
    pve_state.padded_len = padded_len;

    /* Build padded plaintext */
    uint8_t padded[PVE_CIPHER_MAX];
    memcpy(padded, msg, len);
    for (int i = len; i < padded_len; i++)
        padded[i] = (uint8_t)pad_amt;

    /* Derive IV from snapshot key */
    pve_derive_iv(pve_state.snapshot_key, pve_state.iv);

    /* Generate key stream */
    uint8_t keystream[PVE_CIPHER_MAX];
    pve_generate_keystream(pve_state.snapshot_key, keystream, padded_len);

    /* CBC encrypt with S-box: per 16-byte block */
    int num_blocks = padded_len / PVE_KEY_LEN;
    for (int b = 0; b < num_blocks; b++) {
        int off = b * PVE_KEY_LEN;
        /* Step 1: CBC chain — XOR with prev ciphertext block (or IV) */
        const uint8_t *prev = (b == 0) ? pve_state.iv
                                        : &pve_state.ciphertext[off - PVE_KEY_LEN];
        for (int i = 0; i < PVE_KEY_LEN; i++)
            padded[off + i] ^= prev[i];
        /* Step 2: S-box substitution (nonlinear) */
        for (int i = 0; i < PVE_KEY_LEN; i++)
            padded[off + i] = pve_sbox[padded[off + i]];
        /* Step 3: XOR with key stream */
        for (int i = 0; i < PVE_KEY_LEN; i++)
            pve_state.ciphertext[off + i] = padded[off + i] ^ keystream[off + i];
    }

    pve_state.has_cipher = 1;
    pve_state.has_decrypted = 0;
    pve_state.total_encryptions++;
}

static void pve_do_decrypt(void)
{
    if (!pve_state.has_cipher) return;

    /* Governor audit */
    governor_audit_record(POLICY_RES_EXHAUST, GOV_AUDIT,
                          GOVERNOR_DOMAIN_RESOURCE, 0,
                          (uint64_t)pve_state.padded_len, "PVE-SBC decrypt");

    int padded_len = pve_state.padded_len;

    /* Regenerate key stream from snapshot key */
    uint8_t keystream[PVE_CIPHER_MAX];
    pve_generate_keystream(pve_state.snapshot_key, keystream, padded_len);

    /* Rederive IV */
    uint8_t iv[PVE_KEY_LEN];
    pve_derive_iv(pve_state.snapshot_key, iv);

    /* Copy ciphertext to working buffer */
    uint8_t plain[PVE_CIPHER_MAX];
    memcpy(plain, pve_state.ciphertext, padded_len);

    /* CBC decrypt: forward order (prev reads from untouched ciphertext) */
    int num_blocks = padded_len / PVE_KEY_LEN;
    for (int b = 0; b < num_blocks; b++) {
        int off = b * PVE_KEY_LEN;
        /* Step 1: undo key stream XOR */
        for (int i = 0; i < PVE_KEY_LEN; i++)
            plain[off + i] ^= keystream[off + i];
        /* Step 2: inverse S-box */
        for (int i = 0; i < PVE_KEY_LEN; i++)
            plain[off + i] = pve_inv_sbox[plain[off + i]];
        /* Step 3: undo CBC chain */
        const uint8_t *prev = (b == 0) ? iv
                                        : &pve_state.ciphertext[off - PVE_KEY_LEN];
        for (int i = 0; i < PVE_KEY_LEN; i++)
            plain[off + i] ^= prev[i];
    }

    /* Strip PKCS#7 padding — use original length */
    int original_len = pve_state.cipher_len;
    for (int i = 0; i < original_len && i < PVE_MSG_MAX - 1; i++)
        pve_state.decrypted[i] = (char)plain[i];
    pve_state.decrypted[original_len] = '\0';

    pve_state.has_decrypted = 1;
    pve_state.total_decryptions++;
}

/* AI Health Score (0-100, composite of 4 metrics, 25 pts each) */
static int gov_compute_health_score(void)
{
    /* Memory component (25 pts): high free = high score */
    uint64_t free_pg = pmm_get_free_pages();
    uint64_t total_pg = pmm_get_total_pages();
    int used_pct = (total_pg > 0) ? (int)(((total_pg - free_pg) * 100) / total_pg) : 0;
    int mem_score = 25 - (used_pct / 4);
    if (mem_score < 0) mem_score = 0;

    /* Process component (25 pts): 25 if <10 active, scales down */
    struct scheduler_stats ss;
    sched_get_stats(&ss);
    int proc_score = 25;
    if (ss.active_processes > 10)
        proc_score = 25 - (int)(ss.active_processes - 10);
    if (proc_score < 0) proc_score = 0;

    /* Violation component (25 pts): -5 per denial */
    struct gov_stats gs;
    governor_get_stats(&gs);
    int viol_score = 25 - (int)(gs.total_denied * 5);
    if (viol_score < 0) viol_score = 0;

    /* Uptime component (25 pts): linear ramp over 10 minutes */
    uint64_t secs = timer_get_ticks() / 100;
    int up_score = (int)((secs * 25) / 600);
    if (up_score > 25) up_score = 25;

    int total = mem_score + proc_score + viol_score + up_score;
    if (total > 100) total = 100;
    if (total < 0) total = 0;
    return total;
}

/* Accessor functions for desktop_panels.c */
const char *desktop_gov_threat_str(void)
{
    return gov_threat_str(gov_compute_threat_level());
}

uint32_t desktop_gov_threat_color(void)
{
    return gov_threat_color(gov_compute_threat_level());
}

uint64_t desktop_gov_last_scan_ticks(void)
{
    return gov_last_scan_ticks;
}

const char *desktop_gov_trend_str(void)
{
    return gov_trend_str();
}

int desktop_gov_health_score(void)
{
    return gov_compute_health_score();
}

const char *desktop_gov_alert_str(void)
{
    /* Return highest severity alert message or "None" */
    int best_sev = -1;
    int best_idx = -1;
    for (int i = 0; i < GOV_MAX_ALERTS; i++) {
        if (gov_anomaly.alerts[i].active && gov_anomaly.alerts[i].severity > best_sev) {
            best_sev = gov_anomaly.alerts[i].severity;
            best_idx = i;
        }
    }
    if (best_idx >= 0) return gov_anomaly.alerts[best_idx].msg;
    return "None";
}

int desktop_gov_alert_severity(void)
{
    int best_sev = -1;
    for (int i = 0; i < GOV_MAX_ALERTS; i++) {
        if (gov_anomaly.alerts[i].active && gov_anomaly.alerts[i].severity > best_sev)
            best_sev = gov_anomaly.alerts[i].severity;
    }
    return best_sev;
}

const char *desktop_gov_recommendation(void)
{
    for (int i = 0; i < gov_recommendations.count; i++) {
        if (gov_recommendations.items[i].active)
            return gov_recommendations.items[i].msg;
    }
    return (const char *)0;
}

/* Context-aware response: append warning tag to dynamic AI responses */
static void gov_append_context(char *buf)
{
    if (gov_anomaly.count > 0) {
        for (int i = 0; i < GOV_MAX_ALERTS; i++) {
            if (gov_anomaly.alerts[i].active && gov_anomaly.alerts[i].severity >= 2) {
                gov_strcat(buf, " [!ALERT]");
                return;
            }
        }
    }
    uint64_t free_pg = pmm_get_free_pages();
    uint64_t total_pg = pmm_get_total_pages();
    int used_pct = (total_pg > 0) ? (int)(((total_pg - free_pg) * 100) / total_pg) : 0;
    if (used_pct > 80) { gov_strcat(buf, " [MEM HIGH]"); return; }
    if (gov_compute_health_score() < 40) { gov_strcat(buf, " [HEALTH LOW]"); return; }
}

/*============================================================================
 * Governor Window (Tabbed Interactive UI)
 *============================================================================*/

/* Governor UI state */
static struct {
    struct widget_tabbar    tabbar;
    int                     active_tab;   /* 0=Overview, 1=Audit, 2=Config, 3=Quarantine */
    struct gov_stats        cached_stats;
    struct gov_audit_entry  audit_entries[20];
    int                     audit_count;
    int                     audit_scroll;
    int                     selected_audit; /* -1 = none */
    struct widget_scrollbar audit_sb;
    struct widget_checkbox  cb_strict;
    struct widget_checkbox  cb_audit_all;
    struct widget_checkbox  cb_verbose;
    struct widget_button    apply_btn;
    int                     quarantine_selected; /* -1 = none */
} gov_ui;

static void gov_ui_init(void)
{
    gov_ui.active_tab = 0;
    gov_ui.audit_scroll = 0;
    gov_ui.selected_audit = -1;
    gov_ui.quarantine_selected = -1;

    /* Tab bar */
    widget_tabbar_init(&gov_ui.tabbar, 0, 0, 450);
    gov_ui.tabbar.tabs[0] = "Overview";
    gov_ui.tabbar.tabs[1] = "Audit Log";
    gov_ui.tabbar.tabs[2] = "Config";
    gov_ui.tabbar.tabs[3] = "Quarantine";
    gov_ui.tabbar.tab_count = 4;
    gov_ui.tabbar.selected = 0;

    /* Audit scrollbar */
    widget_scrollbar_init(&gov_ui.audit_sb, 450 - WIDGET_SCROLLBAR_WIDTH - 4, 30, 450);

    /* Config checkboxes */
    uint32_t flags = governor_get_flags();
    gov_ui.cb_strict.x = 16;
    gov_ui.cb_strict.y = 50;
    gov_ui.cb_strict.label = "Strict Mode";
    gov_ui.cb_strict.checked = (flags & GOV_FLAG_STRICT) ? 1 : 0;
    gov_ui.cb_strict.text_color = COLOR_TEXT;

    gov_ui.cb_audit_all.x = 16;
    gov_ui.cb_audit_all.y = 80;
    gov_ui.cb_audit_all.label = "Audit All Operations";
    gov_ui.cb_audit_all.checked = (flags & GOV_FLAG_AUDIT_ALL) ? 1 : 0;
    gov_ui.cb_audit_all.text_color = COLOR_TEXT;

    gov_ui.cb_verbose.x = 16;
    gov_ui.cb_verbose.y = 110;
    gov_ui.cb_verbose.label = "Verbose Logging";
    gov_ui.cb_verbose.checked = (flags & GOV_FLAG_VERBOSE) ? 1 : 0;
    gov_ui.cb_verbose.text_color = COLOR_TEXT;

    /* Apply button */
    gov_ui.apply_btn.x = 16;
    gov_ui.apply_btn.y = 160;
    gov_ui.apply_btn.w = 100;
    gov_ui.apply_btn.h = 24;
    gov_ui.apply_btn.text = "Apply";
    gov_ui.apply_btn.bg_color = COLOR_BUTTON_PRIMARY;
    gov_ui.apply_btn.text_color = COLOR_TEXT;
    gov_ui.apply_btn.hovered = 0;
}

static void gov_refresh_data(void)
{
    governor_get_stats(&gov_ui.cached_stats);
    int n = governor_audit_count();
    if (n > 20) n = 20;
    gov_ui.audit_count = n;
    for (int i = 0; i < n; i++)
        governor_audit_get(i, &gov_ui.audit_entries[i]);
}

/* Decision Explanation: human-readable reasoning for audit entries */
static void gov_explain_decision(const struct gov_audit_entry *e, char *buf)
{
    buf[0] = '\0';
    if (e->verdict == GOV_ALLOW) gov_strcat(buf, "ALLOWED: ");
    else if (e->verdict == GOV_DENY) gov_strcat(buf, "DENIED: ");
    else if (e->verdict == GOV_TRANSFORM) gov_strcat(buf, "TRANSFORMED: ");
    else gov_strcat(buf, "AUDITED: ");

    switch (e->policy) {
    case POLICY_MEM_FREE:
        if (e->verdict == GOV_DENY)
            gov_strcat(buf, "Memory free blocked. Prime Directive preserves all allocated memory.");
        else if (e->verdict == GOV_ALLOW)
            gov_strcat(buf, "Memory free permitted. Kernel context holds CAP_MEM_FREE.");
        else
            gov_strcat(buf, "Memory operation logged for audit trail.");
        break;
    case POLICY_MEM_OVERWRITE:
        if (e->verdict == GOV_DENY)
            gov_strcat(buf, "Overwrite blocked. Existing data must be preserved.");
        else
            gov_strcat(buf, "Memory overwrite permitted under capability.");
        break;
    case POLICY_PROC_KILL:
        if (e->verdict == GOV_DENY)
            gov_strcat(buf, "Kill blocked. Processes may only be suspended, never destroyed.");
        else if (e->verdict == GOV_TRANSFORM)
            gov_strcat(buf, "Kill transformed to suspend. Process preserved per Constitution.");
        else
            gov_strcat(buf, "Process signal permitted.");
        break;
    case POLICY_PROC_EXIT:
        gov_strcat(buf, "Self-termination is always allowed under Article III.");
        break;
    case POLICY_FS_DELETE:
        if (e->verdict == GOV_TRANSFORM)
            gov_strcat(buf, "Delete transformed to hide. File data preserved in GeoFS strata.");
        else if (e->verdict == GOV_DENY)
            gov_strcat(buf, "File deletion denied. No hide capability in this context.");
        else
            gov_strcat(buf, "File operation permitted.");
        break;
    case POLICY_FS_TRUNCATE:
        if (e->verdict == GOV_DENY)
            gov_strcat(buf, "Truncation blocked. File content is immutable once written.");
        else
            gov_strcat(buf, "Truncation logged. Original data preserved in layer.");
        break;
    case POLICY_FS_OVERWRITE:
        if (e->verdict == GOV_TRANSFORM)
            gov_strcat(buf, "Overwrite transformed to new layer. Original data preserved.");
        else if (e->verdict == GOV_DENY)
            gov_strcat(buf, "File overwrite blocked. Append-only policy in effect.");
        else
            gov_strcat(buf, "File write permitted as new layer.");
        break;
    case POLICY_FS_HIDE:
        gov_strcat(buf, "Hide is the approved alternative to delete. Always allowed.");
        break;
    case POLICY_FS_PERM_DENIED:
        gov_strcat(buf, "Permission check failed for requested filesystem operation.");
        break;
    case POLICY_FS_QUOTA_EXCEEDED:
        gov_strcat(buf, "Storage quota exceeded. Request more capacity.");
        break;
    case POLICY_RES_EXHAUST:
        gov_strcat(buf, "Resource exhaustion attempt detected and blocked.");
        break;
    default:
        gov_strcat(buf, governor_verdict_name(e->verdict));
        gov_strcat(buf, " for ");
        gov_strcat(buf, governor_policy_name(e->policy));
        gov_strcat(buf, ".");
        break;
    }
}

/* Paint: Overview tab */
static void gov_paint_overview(struct wm_window *win, int y0)
{
    struct gov_stats *s = &gov_ui.cached_stats;
    char buf[64];
    int y = y0 + 8;

    widget_label(win, 8, y, "AI GOVERNOR", COLOR_HIGHLIGHT);
    y += 18;
    widget_label(win, 8, y, "Code Safety Evaluator", COLOR_TEXT_DIM);
    y += 28;

    /* Total checks */
    widget_label(win, 8, y, "Total Checks:", COLOR_TEXT_DIM);
    gov_u64_to_str(s->total_checks, buf);
    widget_label(win, 140, y, buf, COLOR_TEXT);
    y += 22;

    /* Allowed */
    widget_label(win, 8, y, "Allowed:", COLOR_TEXT_DIM);
    gov_u64_to_str(s->total_allowed, buf);
    widget_label(win, 140, y, buf, COLOR_GREEN_ACTIVE);
    y += 22;

    /* Denied */
    widget_label(win, 8, y, "Denied:", COLOR_TEXT_DIM);
    gov_u64_to_str(s->total_denied, buf);
    widget_label(win, 140, y, buf, COLOR_HIGHLIGHT);
    y += 22;

    /* Transformed */
    widget_label(win, 8, y, "Transformed:", COLOR_TEXT_DIM);
    gov_u64_to_str(s->total_transformed, buf);
    widget_label(win, 140, y, buf, COLOR_ICON_ORANGE);
    y += 28;

    /* Threat level */
    int threat = gov_compute_threat_level();
    widget_label(win, 8, y, "Threat Level:", COLOR_TEXT_DIM);
    widget_label(win, 140, y, gov_threat_str(threat), gov_threat_color(threat));
    y += 22;

    /* Threat trend */
    widget_label(win, 8, y, "Trend:", COLOR_TEXT_DIM);
    {
        const char *trend = gov_trend_str();
        uint32_t tc = COLOR_TEXT;
        if (trend[0] == 'R') tc = COLOR_HIGHLIGHT;
        else if (trend[0] == 'F') tc = COLOR_GREEN_ACTIVE;
        else if (trend[0] == 'S') tc = COLOR_ICON_YELLOW;
        widget_label(win, 140, y, trend, tc);
    }
    y += 24;

    /* Violation breakdown */
    widget_label(win, 8, y, "Violations Blocked:", COLOR_TEXT_DIM);
    y += 20;

    widget_label(win, 16, y, "Memory:", COLOR_TEXT_DIM);
    gov_u64_to_str(s->violations_memory, buf);
    widget_label(win, 140, y, buf, COLOR_TEXT);
    y += 18;

    widget_label(win, 16, y, "Process:", COLOR_TEXT_DIM);
    gov_u64_to_str(s->violations_process, buf);
    widget_label(win, 140, y, buf, COLOR_TEXT);
    y += 18;

    widget_label(win, 16, y, "Filesystem:", COLOR_TEXT_DIM);
    gov_u64_to_str(s->violations_fs, buf);
    widget_label(win, 140, y, buf, COLOR_TEXT);
    y += 28;

    /* Scan count */
    widget_label(win, 8, y, "Scans:", COLOR_TEXT_DIM);
    gov_u64_to_str(gov_scan_count, buf);
    widget_label(win, 140, y, buf, COLOR_TEXT);
    y += 22;

    /* Health score */
    {
        int health = gov_compute_health_score();
        char hbuf[8];
        gov_u64_to_str((uint64_t)health, hbuf);
        widget_label(win, 8, y, "Health:", COLOR_TEXT_DIM);
        char hdisp[16];
        hdisp[0] = '\0';
        gov_strcat(hdisp, hbuf);
        gov_strcat(hdisp, "/100");
        uint32_t hc = COLOR_GREEN_ACTIVE;
        if (health < 40) hc = COLOR_HIGHLIGHT;
        else if (health <= 70) hc = COLOR_ICON_YELLOW;
        widget_label(win, 140, y, hdisp, hc);
        y += 18;
        uint32_t bar_fg = COLOR_GREEN_ACTIVE;
        if (health < 40) bar_fg = COLOR_HIGHLIGHT;
        else if (health <= 70) bar_fg = COLOR_ICON_YELLOW;
        widget_progress(win, 8, y, 430, 12, health, bar_fg, 0xFF0D0D1A);
        y += 22;
    }

    /* Active alerts */
    if (gov_anomaly.count > 0) {
        widget_label(win, 8, y, "Active Alerts:", COLOR_HIGHLIGHT);
        y += 18;
        for (int i = 0; i < GOV_MAX_ALERTS; i++) {
            if (!gov_anomaly.alerts[i].active) continue;
            uint32_t ac = COLOR_BUTTON_PRIMARY; /* info = blue */
            if (gov_anomaly.alerts[i].severity == 1) ac = COLOR_ICON_YELLOW;
            if (gov_anomaly.alerts[i].severity >= 2) ac = COLOR_HIGHLIGHT;
            char aline[72];
            aline[0] = '['; aline[1] = '!'; aline[2] = ']'; aline[3] = ' ';
            aline[4] = '\0';
            gov_strcat(aline, gov_anomaly.alerts[i].msg);
            widget_label(win, 16, y, aline, ac);
            y += 16;
        }
        y += 6;
    } else {
        widget_label(win, 8, y, "Alerts:", COLOR_TEXT_DIM);
        widget_label(win, 80, y, "None", COLOR_GREEN_ACTIVE);
        y += 20;
    }

    /* Behavioral Learning Status */
    widget_label(win, 8, y, "Learning:", COLOR_TEXT_DIM);
    if (!gov_behavior.baseline_set) {
        struct gov_stats _ls;
        governor_get_stats(&_ls);
        char lbuf[48];
        lbuf[0] = '\0';
        gov_strcat(lbuf, "Collecting... ");
        char cn[16];
        gov_u64_to_str(_ls.total_checks, cn);
        gov_strcat(lbuf, cn);
        gov_strcat(lbuf, "/100");
        widget_label(win, 100, y, lbuf, COLOR_ICON_YELLOW);
    } else {
        if (gov_behavior.deviation_count == 0) {
            widget_label(win, 100, y, "Nominal", COLOR_GREEN_ACTIVE);
        } else {
            char dbuf[32];
            dbuf[0] = '\0';
            char dn[8];
            gov_u64_to_str((uint64_t)gov_behavior.deviation_count, dn);
            gov_strcat(dbuf, dn);
            gov_strcat(dbuf, " deviations");
            widget_label(win, 100, y, dbuf, COLOR_HIGHLIGHT);
        }
    }
    y += 20;

    /* Threat Timeline (mini bar chart) */
    if (gov_timeline.filled > 0) {
        widget_label(win, 8, y, "Timeline:", COLOR_TEXT_DIM);
        y += 16;

        int chart_x = 8;
        int chart_h = 20;
        int bar_w = 16;
        int bar_gap = 1;
        int ox = win->x;
        int oy = win->y + WM_TITLE_HEIGHT;

        fb_fill_rect((uint32_t)(ox + chart_x), (uint32_t)(oy + y),
                     (uint32_t)(GOV_TIMELINE_SLOTS * (bar_w + bar_gap)),
                     (uint32_t)chart_h, 0xFF0D0D1A);

        for (int i = 0; i < gov_timeline.filled; i++) {
            int idx = (gov_timeline.head - gov_timeline.filled + i + GOV_TIMELINE_SLOTS) % GOV_TIMELINE_SLOTS;
            int hs = gov_timeline.health_score[idx];
            int tl = gov_timeline.threat_level[idx];

            int bh = (hs * chart_h) / 100;
            if (bh < 1) bh = 1;

            uint32_t bc = COLOR_GREEN_ACTIVE;
            if (tl == 1) bc = COLOR_ICON_YELLOW;
            if (tl >= 2) bc = COLOR_HIGHLIGHT;

            int bx = ox + chart_x + i * (bar_w + bar_gap);
            int by = oy + y + (chart_h - bh);

            fb_fill_rect((uint32_t)bx, (uint32_t)by, (uint32_t)bar_w, (uint32_t)bh, bc);
        }
        y += chart_h + 8;
    }

    /* Philosophy */
    widget_label(win, 8, y, "\"To Create, Not Destroy\"", COLOR_ICON_PURPLE);
}

/* Paint: Audit Log tab */
static void gov_paint_audit(struct wm_window *win, int y0)
{
    int y = y0 + 8;
    widget_label(win, 8, y, "Recent Audit Entries", COLOR_TEXT_DIM);
    y += 22;

    if (gov_ui.audit_count == 0) {
        widget_label(win, 16, y, "No audit entries yet.", COLOR_TEXT_DIM);
        return;
    }

    int visible = 10;
    int scroll = gov_ui.audit_scroll;
    if (scroll > gov_ui.audit_count - visible)
        scroll = gov_ui.audit_count - visible;
    if (scroll < 0) scroll = 0;

    for (int i = 0; i < visible && (scroll + i) < gov_ui.audit_count; i++) {
        struct gov_audit_entry *e = &gov_ui.audit_entries[scroll + i];
        int idx = scroll + i;

        /* Highlight selected row */
        if (idx == gov_ui.selected_audit) {
            /* Draw selection background */
            widget_label(win, 2, y, ">", COLOR_ICON_PURPLE);
        }

        /* Verdict color */
        uint32_t vc = COLOR_TEXT;
        if (e->verdict == GOV_ALLOW) vc = COLOR_GREEN_ACTIVE;
        else if (e->verdict == GOV_DENY) vc = COLOR_HIGHLIGHT;
        else if (e->verdict == GOV_TRANSFORM) vc = COLOR_ICON_ORANGE;
        else if (e->verdict == GOV_AUDIT) vc = COLOR_ICON_PURPLE;

        /* Policy name */
        const char *pname = governor_policy_name(e->policy);
        widget_label(win, 12, y, pname, COLOR_TEXT);

        /* Verdict */
        const char *vname = governor_verdict_name(e->verdict);
        widget_label(win, 160, y, vname, vc);

        /* Reason (truncated) */
        char reason[32];
        str_copy(reason, e->reason, 30);
        widget_label(win, 240, y, reason, COLOR_TEXT_DIM);

        y += 18;
    }

    /* Scrollbar */
    widget_scrollbar_update(&gov_ui.audit_sb,
                            gov_ui.audit_count, visible, scroll);
    widget_scrollbar_draw(win, &gov_ui.audit_sb);

    /* Explanation area for selected audit entry */
    if (gov_ui.selected_audit >= 0 && gov_ui.selected_audit < gov_ui.audit_count) {
        y += 8;
        widget_label(win, 8, y, "________________________________", 0xFF1E293B);
        y += 14;
        widget_label(win, 8, y, "Explanation:", COLOR_ICON_PURPLE);
        y += 18;

        char explain_buf[256];
        gov_explain_decision(&gov_ui.audit_entries[gov_ui.selected_audit], explain_buf);

        /* Word-wrap explanation (up to 4 lines, ~46 chars each) */
        int ei = 0;
        int lines = 0;
        while (explain_buf[ei] && lines < 4) {
            int li = 0;
            int last_space = 0;
            while (explain_buf[ei + li] && li < 46) {
                if (explain_buf[ei + li] == ' ') last_space = li;
                li++;
            }
            if (explain_buf[ei + li] && last_space > 0) li = last_space + 1;
            char draw_line[48];
            int di;
            for (di = 0; di < li && di < 46; di++) draw_line[di] = explain_buf[ei + di];
            while (di > 0 && draw_line[di - 1] == ' ') di--;
            draw_line[di] = '\0';
            widget_label(win, 16, y, draw_line, COLOR_TEXT);
            y += 16;
            ei += li;
            while (explain_buf[ei] == ' ') ei++;
            lines++;
        }
    }
}

/* Paint: Config tab */
static void gov_paint_config(struct wm_window *win, int y0)
{
    int y = y0 + 8;
    widget_label(win, 8, y, "Governor Configuration", COLOR_TEXT_DIM);
    y += 24;

    /* Current flags display */
    uint32_t flags = governor_get_flags();
    char flag_str[32];
    flag_str[0] = '0'; flag_str[1] = 'x'; flag_str[2] = '\0';
    char hexbuf[12];
    gov_u64_to_str((uint64_t)flags, hexbuf);
    gov_strcat(flag_str, hexbuf);
    widget_label(win, 8, y, "Current Flags:", COLOR_TEXT_DIM);
    widget_label(win, 140, y, flag_str, COLOR_TEXT);
    y += 28;

    /* Checkboxes */
    gov_ui.cb_strict.y = y;
    widget_checkbox_draw(win, &gov_ui.cb_strict);
    y += 30;

    gov_ui.cb_audit_all.y = y;
    widget_checkbox_draw(win, &gov_ui.cb_audit_all);
    y += 30;

    gov_ui.cb_verbose.y = y;
    widget_checkbox_draw(win, &gov_ui.cb_verbose);
    y += 40;

    /* Apply button */
    gov_ui.apply_btn.y = y;
    widget_button_draw(win, &gov_ui.apply_btn);
    y += 40;

    widget_label(win, 8, y, "Changes take effect", COLOR_TEXT_DIM);
    y += 16;
    widget_label(win, 8, y, "immediately when Applied.", COLOR_TEXT_DIM);
}

/* Paint: Quarantine tab */
static void gov_paint_quarantine(struct wm_window *win, int y0)
{
    int y = y0 + 8;
    widget_label(win, 8, y, "Quarantined Operations", COLOR_TEXT_DIM);
    y += 22;

    int active = 0;
    for (int i = 0; i < GOV_QUARANTINE_MAX; i++)
        if (gov_quarantine.items[i].active) active++;

    if (active == 0) {
        widget_label(win, 16, y, "No quarantined items.", COLOR_TEXT_DIM);
        y += 18;
        widget_label(win, 16, y, "Items appear when critical", COLOR_TEXT_DIM);
        y += 16;
        widget_label(win, 16, y, "alerts trigger captures.", COLOR_TEXT_DIM);
        return;
    }

    int shown = 0;
    for (int i = 0; i < GOV_QUARANTINE_MAX && shown < 8; i++) {
        struct gov_quarantine_item *qi = &gov_quarantine.items[i];
        if (!qi->active) continue;

        if (i == gov_ui.quarantine_selected)
            widget_label(win, 2, y, ">", COLOR_ICON_PURPLE);

        const char *pname = governor_policy_name(qi->policy);
        widget_label(win, 12, y, pname, COLOR_TEXT);

        uint32_t vc = (qi->verdict == GOV_DENY) ? COLOR_HIGHLIGHT : COLOR_ICON_ORANGE;
        const char *vname = governor_verdict_name(qi->verdict);
        widget_label(win, 160, y, vname, vc);

        uint32_t rc = qi->reviewed ? COLOR_TEXT_DIM : COLOR_HIGHLIGHT;
        widget_label(win, 260, y, qi->reviewed ? "Reviewed" : "PENDING", rc);

        {
            uint64_t age = timer_get_ticks() - qi->timestamp;
            uint64_t secs = age / 100;
            char tbuf[16];
            tbuf[0] = '\0';
            char ns[8];
            gov_u64_to_str(secs, ns);
            gov_strcat(tbuf, ns);
            gov_strcat(tbuf, "s ago");
            widget_label(win, 360, y, tbuf, COLOR_TEXT_DIM);
        }

        y += 20;
        shown++;
    }

    if (gov_ui.quarantine_selected >= 0 &&
        gov_ui.quarantine_selected < GOV_QUARANTINE_MAX &&
        gov_quarantine.items[gov_ui.quarantine_selected].active) {
        y += 8;
        widget_label(win, 8, y, "Reason:", COLOR_ICON_PURPLE);
        y += 18;
        widget_label(win, 16, y,
                     gov_quarantine.items[gov_ui.quarantine_selected].reason,
                     COLOR_TEXT);
        y += 18;
        if (!gov_quarantine.items[gov_ui.quarantine_selected].reviewed)
            widget_label(win, 16, y, "Click again to mark reviewed", COLOR_TEXT_DIM);
        else
            widget_label(win, 16, y, "Marked as reviewed", COLOR_GREEN_ACTIVE);
    }
}

static void governor_paint(struct wm_window *win)
{
    gov_refresh_data();

    /* Tab bar */
    gov_ui.tabbar.selected = gov_ui.active_tab;
    widget_tabbar_draw(win, &gov_ui.tabbar);

    int tab_y = WIDGET_TAB_HEIGHT + 4;

    switch (gov_ui.active_tab) {
    case 0: gov_paint_overview(win, tab_y); break;
    case 1: gov_paint_audit(win, tab_y);    break;
    case 2: gov_paint_config(win, tab_y);   break;
    case 3: gov_paint_quarantine(win, tab_y); break;
    }
}

static void governor_click(struct wm_window *win, int cx, int cy, int btn)
{
    (void)win; (void)btn;

    /* Tab bar click */
    int tab = widget_tabbar_click(&gov_ui.tabbar, cx, cy);
    if (tab >= 0) {
        gov_ui.active_tab = tab;
        return;
    }

    if (gov_ui.active_tab == 1) {
        /* Audit entry row click */
        int entry_y0 = WIDGET_TAB_HEIGHT + 4 + 8 + 22; /* tab_y + header */
        if (cy >= entry_y0 && cy < entry_y0 + 10 * 18) {
            int row = (cy - entry_y0) / 18;
            int idx = gov_ui.audit_scroll + row;
            if (idx >= 0 && idx < gov_ui.audit_count)
                gov_ui.selected_audit = idx;
        }
        /* Audit log scrollbar */
        int sc = widget_scrollbar_click(&gov_ui.audit_sb, cx, cy);
        if (sc >= 0) gov_ui.audit_scroll = sc;
    } else if (gov_ui.active_tab == 2) {
        /* Config checkboxes */
        widget_checkbox_click(&gov_ui.cb_strict, cx, cy);
        widget_checkbox_click(&gov_ui.cb_audit_all, cx, cy);
        widget_checkbox_click(&gov_ui.cb_verbose, cx, cy);

        /* Apply button */
        if (widget_button_hit(&gov_ui.apply_btn, cx, cy)) {
            uint32_t flags = 0;
            if (gov_ui.cb_strict.checked)    flags |= GOV_FLAG_STRICT;
            if (gov_ui.cb_audit_all.checked) flags |= GOV_FLAG_AUDIT_ALL;
            if (gov_ui.cb_verbose.checked)   flags |= GOV_FLAG_VERBOSE;
            governor_set_flags(flags);
        }
    } else if (gov_ui.active_tab == 3) {
        /* Quarantine item click */
        int entry_y0 = WIDGET_TAB_HEIGHT + 4 + 8 + 22;
        if (cy >= entry_y0) {
            int row = (cy - entry_y0) / 20;
            int shown = 0;
            for (int i = 0; i < GOV_QUARANTINE_MAX; i++) {
                if (!gov_quarantine.items[i].active) continue;
                if (shown == row) {
                    if (gov_ui.quarantine_selected == i) {
                        gov_quarantine.items[i].reviewed = 1;
                    } else {
                        gov_ui.quarantine_selected = i;
                    }
                    break;
                }
                shown++;
            }
        }
    }
}

static void governor_key(struct wm_window *win, int key)
{
    (void)win;
    /* Left/Right arrows to switch tabs */
    if (key == KEY_LEFT && gov_ui.active_tab > 0) {
        gov_ui.active_tab--;
    } else if (key == KEY_RIGHT && gov_ui.active_tab < 3) {
        gov_ui.active_tab++;
    }
    /* Up/Down for audit scroll */
    if (gov_ui.active_tab == 1) {
        if (key == KEY_UP && gov_ui.audit_scroll > 0)
            gov_ui.audit_scroll--;
        else if (key == KEY_DOWN && gov_ui.audit_scroll < gov_ui.audit_count - 1)
            gov_ui.audit_scroll++;
    }
}

/*============================================================================
 * Geology Viewer (GeoFS v3 Interactive Explorer)
 * "Every stratum tells a story"
 *============================================================================*/

/* Branch color palette (indexed by branch_id % 8) */
#define GEO_BRANCH_COLORS   8
static const uint32_t geo_branch_palette[GEO_BRANCH_COLORS] = {
    0xFF3B82F6,  /* Blue    - main */
    0xFF22C55E,  /* Green   */
    0xFFF97316,  /* Orange  */
    0xFF8B5CF6,  /* Purple  */
    0xFFEAB308,  /* Yellow  */
    0xFFE94560,  /* Red     */
    0xFF06B6D4,  /* Cyan    */
    0xFFEC4899,  /* Pink    */
};

#define GEO_MAX_VIEWS       48
#define GEO_MAX_BRANCHES    16
#define GEO_DIFF_BUF_SIZE   1024
#define GEO_BAND_H          22
#define GEO_DETAIL_H        64

static struct {
    /* Tab bar */
    struct widget_tabbar    tabbar;
    int                     active_tab;     /* 0=Strata, 1=Branches, 2=Dashboard */

    /* Refresh button */
    struct widget_button    refresh_btn;

    /* === Strata tab === */
    struct {
        kgeofs_view_t   id;
        kgeofs_view_t   parent_id;
        kgeofs_branch_t branch_id;
        char            label[64];
    } views[GEO_MAX_VIEWS];
    int                     view_count;
    int                     strata_selected;    /* index, -1=none */
    int                     strata_scroll;      /* scroll offset (items) */
    struct widget_scrollbar strata_sb;
    struct widget_button    strata_switch_btn;
    int                     band_area_top;      /* cached for click */

    /* === Branches tab === */
    struct {
        kgeofs_branch_t id;
        char            name[KGEOFS_BRANCH_NAME_MAX];
        kgeofs_view_t   base_view;
        kgeofs_view_t   head_view;
    } branches[GEO_MAX_BRANCHES];
    int                     branch_count;
    struct widget_list      branch_list;
    char                    branch_names[GEO_MAX_BRANCHES][80];
    struct widget_button    branch_switch_btn;
    struct widget_button    branch_diff_btn;
    char                    diff_buf[GEO_DIFF_BUF_SIZE];
    int                     diff_count;
    int                     diff_visible;

    /* === Dashboard tab === */
    struct kgeofs_stats     stats;
    kgeofs_view_t           current_view;
    kgeofs_branch_t         current_branch;
    char                    current_branch_name[KGEOFS_BRANCH_NAME_MAX];
    uint64_t                quota_content_used;
    struct kgeofs_quota     quota_limits;
    int                     quota_valid;
    struct kgeofs_access_ctx access_ctx;
} geo;

/* --- Helpers --- */

static void geo_u64_to_str(uint64_t v, char *buf, int buf_size)
{
    char tmp[20];
    int len = 0;
    if (v == 0) { tmp[len++] = '0'; }
    else { while (v && len < 19) { tmp[len++] = '0' + (char)(v % 10); v /= 10; } }
    int i;
    for (i = 0; i < len && i < buf_size - 1; i++)
        buf[i] = tmp[len - 1 - i];
    buf[i] = '\0';
}

static const char *geo_branch_name(kgeofs_branch_t bid)
{
    for (int i = 0; i < geo.branch_count; i++)
        if (geo.branches[i].id == bid) return geo.branches[i].name;
    return "main";
}

/* --- Callbacks --- */

static void geo_branch_cb(kgeofs_branch_t id, const char *name,
                            kgeofs_view_t base_view, kgeofs_view_t head_view,
                            kgeofs_time_t created, void *ctx)
{
    (void)created; (void)ctx;
    if (geo.branch_count >= GEO_MAX_BRANCHES) return;
    int i = geo.branch_count++;
    geo.branches[i].id = id;
    str_copy(geo.branches[i].name, name, KGEOFS_BRANCH_NAME_MAX);
    geo.branches[i].base_view = base_view;
    geo.branches[i].head_view = head_view;
}

static int geo_diff_cb(const struct kgeofs_diff_entry *entry, void *ctx)
{
    (void)ctx;
    int len = (int)strlen(geo.diff_buf);
    if (len > GEO_DIFF_BUF_SIZE - 80) return 1;
    const char *tag = (entry->change_type == 0) ? "+ADD " :
                      (entry->change_type == 1) ? "~MOD " : "-HID ";
    int tlen = (int)strlen(tag);
    int plen = (int)strlen(entry->path);
    if (len + tlen + plen + 2 < GEO_DIFF_BUF_SIZE) {
        memcpy(geo.diff_buf + len, tag, (size_t)tlen);
        memcpy(geo.diff_buf + len + tlen, entry->path, (size_t)plen);
        geo.diff_buf[len + tlen + plen] = '\n';
        geo.diff_buf[len + tlen + plen + 1] = '\0';
    }
    geo.diff_count++;
    return 0;
}

/* --- Data refresh --- */

static void geo_refresh(void)
{
    if (!fs_vol) return;

    kgeofs_volume_stats(fs_vol, &geo.stats);
    geo.current_view = kgeofs_view_current(fs_vol);
    geo.current_branch = kgeofs_branch_current(fs_vol);

    /* Collect views from index (includes branch_id) */
    geo.view_count = 0;
    struct kgeofs_view_entry *ve = fs_vol->view_index;
    while (ve && geo.view_count < GEO_MAX_VIEWS) {
        int i = geo.view_count++;
        geo.views[i].id = ve->id;
        geo.views[i].parent_id = ve->parent_id;
        geo.views[i].branch_id = ve->branch_id;
        str_copy(geo.views[i].label, ve->label, 64);
        ve = ve->next;
    }

    /* Collect branches */
    geo.branch_count = 0;
    kgeofs_branch_list(fs_vol, geo_branch_cb, NULL);

    /* Build branch list widget */
    geo.branch_list.count = 0;
    for (int i = 0; i < geo.branch_count; i++) {
        int p = 0;
        if (geo.branches[i].id == geo.current_branch) {
            geo.branch_names[i][p++] = '*';
            geo.branch_names[i][p++] = ' ';
        }
        str_copy(geo.branch_names[i] + p, geo.branches[i].name, 80 - p);
        geo.branch_list.items[i] = geo.branch_names[i];
        geo.branch_list.count++;
    }

    /* Current branch name */
    geo.current_branch_name[0] = '\0';
    for (int i = 0; i < geo.branch_count; i++) {
        if (geo.branches[i].id == geo.current_branch) {
            str_copy(geo.current_branch_name, geo.branches[i].name,
                     KGEOFS_BRANCH_NAME_MAX);
            break;
        }
    }

    /* Quota */
    geo.quota_valid = 0;
    if (kgeofs_quota_get(fs_vol, KGEOFS_QUOTA_VOLUME, &geo.quota_limits) == KGEOFS_OK) {
        uint64_t r, v;
        kgeofs_quota_usage(fs_vol, KGEOFS_QUOTA_VOLUME,
                           &geo.quota_content_used, &r, &v);
        geo.quota_valid = 1;
    }

    /* Access context */
    const struct kgeofs_access_ctx *ctx = kgeofs_get_context(fs_vol);
    if (ctx) geo.access_ctx = *ctx;
}

static void geo_init_state(void)
{
    memset(&geo, 0, sizeof(geo));

    widget_tabbar_init(&geo.tabbar, 4, 4, 460);
    geo.tabbar.tabs[0] = "Strata";
    geo.tabbar.tabs[1] = "Branches";
    geo.tabbar.tabs[2] = "Dashboard";
    geo.tabbar.tab_count = 3;
    geo.tabbar.selected = 0;
    geo.active_tab = 0;

    geo.refresh_btn.x = 500; geo.refresh_btn.y = 4;
    geo.refresh_btn.w = 68; geo.refresh_btn.h = 22;
    geo.refresh_btn.text = "Refresh";
    geo.refresh_btn.bg_color = COLOR_BUTTON_PRIMARY;
    geo.refresh_btn.text_color = COLOR_WHITE;
    geo.refresh_btn.hovered = 0;

    geo.strata_selected = -1;
    geo.strata_scroll = 0;
    widget_scrollbar_init(&geo.strata_sb, 0, 0, 0);

    geo.strata_switch_btn.x = 8; geo.strata_switch_btn.y = 0;
    geo.strata_switch_btn.w = 72; geo.strata_switch_btn.h = 20;
    geo.strata_switch_btn.text = "Switch";
    geo.strata_switch_btn.bg_color = COLOR_GREEN_ACTIVE;
    geo.strata_switch_btn.text_color = COLOR_WHITE;
    geo.strata_switch_btn.hovered = 0;

    memset(&geo.branch_list, 0, sizeof(geo.branch_list));
    geo.branch_list.selected = -1;

    geo.branch_switch_btn.x = 0; geo.branch_switch_btn.y = 0;
    geo.branch_switch_btn.w = 68; geo.branch_switch_btn.h = 20;
    geo.branch_switch_btn.text = "Switch";
    geo.branch_switch_btn.bg_color = COLOR_GREEN_ACTIVE;
    geo.branch_switch_btn.text_color = COLOR_WHITE;
    geo.branch_switch_btn.hovered = 0;

    geo.branch_diff_btn.x = 0; geo.branch_diff_btn.y = 0;
    geo.branch_diff_btn.w = 52; geo.branch_diff_btn.h = 20;
    geo.branch_diff_btn.text = "Diff";
    geo.branch_diff_btn.bg_color = COLOR_ICON_PURPLE;
    geo.branch_diff_btn.text_color = COLOR_WHITE;
    geo.branch_diff_btn.hovered = 0;

    geo.diff_visible = 0;
    geo.diff_buf[0] = '\0';

    geo_refresh();
}

/* --- Paint: Strata tab --- */

static void geo_paint_strata(struct wm_window *win, int cw, int ch, int top)
{
    int y = top + 2;

    /* Branch legend */
    widget_label(win, 8, y, "Legend:", COLOR_TEXT_DIM);
    int lx = 60;
    for (int i = 0; i < geo.branch_count && i < GEO_BRANCH_COLORS; i++) {
        uint32_t col = geo_branch_palette[geo.branches[i].id % GEO_BRANCH_COLORS];
        gfx_fill_rounded_rect(win->x + WM_BORDER_WIDTH + lx,
                               win->y + WM_TITLE_HEIGHT + y, 10, 10, 2, col);
        int nlen = (int)strlen(geo.branches[i].name);
        widget_label(win, lx + 14, y, geo.branches[i].name, COLOR_TEXT);
        lx += 14 + nlen * FONT_WIDTH + 8;
        if (lx > cw - 60) { lx = 60; y += 14; }
    }
    y += 16;

    /* Band area */
    int sb_w = WIDGET_SCROLLBAR_WIDTH;
    int band_area_top = y;
    int band_area_h = ch - band_area_top - GEO_DETAIL_H - 4;
    int visible = band_area_h / GEO_BAND_H;
    if (visible < 1) visible = 1;
    int band_w = cw - 8 - sb_w - 4;

    geo.band_area_top = band_area_top;  /* cache for click */

    /* Scrollbar */
    geo.strata_sb.x = cw - sb_w - 4;
    geo.strata_sb.y = band_area_top;
    geo.strata_sb.h = band_area_h;
    widget_scrollbar_update(&geo.strata_sb, geo.view_count, visible,
                            geo.strata_scroll);
    widget_scrollbar_draw(win, &geo.strata_sb);

    /* Draw bands bottom-to-top: index 0 (genesis) at bottom */
    int drawn = 0;
    for (int vi = geo.strata_scroll;
         vi < geo.view_count && drawn < visible; vi++, drawn++)
    {
        int by = band_area_top + band_area_h - (drawn + 1) * GEO_BAND_H;
        if (by < band_area_top) break;

        uint32_t bcol = geo_branch_palette[geo.views[vi].branch_id % GEO_BRANCH_COLORS];
        int is_cur = (geo.views[vi].id == geo.current_view);
        int is_sel = (vi == geo.strata_selected);

        /* Band fill */
        uint32_t fill = is_sel ? gfx_alpha_blend(bcol, COLOR_WHITE, 180)
                               : gfx_alpha_blend(bcol, COLOR_BG_DARK, 200);
        gfx_fill_rounded_rect(win->x + WM_BORDER_WIDTH + 4,
                               win->y + WM_TITLE_HEIGHT + by,
                               band_w, GEO_BAND_H - 2, 4, fill);

        /* Current view accent border */
        if (is_cur) {
            gfx_draw_rounded_rect(win->x + WM_BORDER_WIDTH + 4,
                                   win->y + WM_TITLE_HEIGHT + by,
                                   band_w, GEO_BAND_H - 2, 4, COLOR_HIGHLIGHT);
            widget_label(win, 8, by + 3, ">", COLOR_HIGHLIGHT);
        }

        /* View ID */
        char id_buf[16];
        geo_u64_to_str(geo.views[vi].id, id_buf, 16);
        widget_label(win, is_cur ? 20 : 10, by + 3, id_buf, COLOR_WHITE);

        /* Label (truncated) */
        int lbl_x = is_cur ? 52 : 42;
        char trunc[28];
        int max_lbl = (band_w - 160) / FONT_WIDTH;
        if (max_lbl > 27) max_lbl = 27;
        if (max_lbl < 4) max_lbl = 4;
        str_copy(trunc, geo.views[vi].label, max_lbl + 1);
        widget_label(win, lbl_x, by + 3, trunc, COLOR_WHITE);

        /* Branch name on right */
        const char *bname = geo_branch_name(geo.views[vi].branch_id);
        int bn_x = 4 + band_w - (int)strlen(bname) * FONT_WIDTH - 8;
        widget_label(win, bn_x, by + 3, bname, 0xFFCCCCCC);
    }

    if (geo.view_count == 0)
        widget_label(win, 20, band_area_top + 20, "No views", COLOR_TEXT_DIM);

    /* Detail panel */
    int dy = ch - GEO_DETAIL_H;
    gfx_draw_hline(win->x + WM_BORDER_WIDTH + 4,
                   win->y + WM_TITLE_HEIGHT + dy, cw - 8, COLOR_PANEL_BORDER);
    dy += 4;

    if (geo.strata_selected >= 0 && geo.strata_selected < geo.view_count) {
        int si = geo.strata_selected;
        char buf[32];

        widget_label(win, 8, dy, "Label:", COLOR_TEXT_DIM);
        widget_label(win, 56, dy, geo.views[si].label, COLOR_TEXT);
        dy += 16;

        widget_label(win, 8, dy, "Parent:", COLOR_TEXT_DIM);
        geo_u64_to_str(geo.views[si].parent_id, buf, 32);
        widget_label(win, 64, dy, buf, COLOR_TEXT);

        widget_label(win, 140, dy, "Branch:", COLOR_TEXT_DIM);
        widget_label(win, 200, dy,
                     geo_branch_name(geo.views[si].branch_id), COLOR_TEXT);
        dy += 16;

        geo.strata_switch_btn.y = dy;
        widget_button_draw(win, &geo.strata_switch_btn);

        if (geo.views[si].id == geo.current_view) {
            widget_label(win, 88, dy + 2, "(current)", COLOR_GREEN_ACTIVE);
        }
    } else {
        widget_label(win, 8, dy + 8, "Click a stratum to inspect", COLOR_TEXT_DIM);
    }
}

/* --- Paint: Branches tab --- */

static void geo_paint_branches(struct wm_window *win, int cw, int ch, int top)
{
    int split = 180;

    /* Left: branch list */
    geo.branch_list.x = 4;
    geo.branch_list.y = top;
    geo.branch_list.w = split - 8;
    geo.branch_list.h = ch - top - 8;
    widget_list_draw(win, &geo.branch_list);

    gfx_draw_vline(win->x + WM_BORDER_WIDTH + split,
                   win->y + WM_TITLE_HEIGHT + top, ch - top - 8, COLOR_BORDER);

    /* Right: details */
    int rx = split + 8;
    int ry = top + 4;
    int sel = geo.branch_list.selected;

    if (sel >= 0 && sel < geo.branch_count) {
        char buf[32];

        widget_label(win, rx, ry, "Branch Details", COLOR_HIGHLIGHT);
        ry += 22;

        widget_label(win, rx, ry, "Name:", COLOR_TEXT_DIM);
        widget_label(win, rx + 56, ry, geo.branches[sel].name, COLOR_TEXT);
        ry += 18;

        widget_label(win, rx, ry, "ID:", COLOR_TEXT_DIM);
        geo_u64_to_str(geo.branches[sel].id, buf, 32);
        widget_label(win, rx + 56, ry, buf, COLOR_TEXT);
        ry += 18;

        widget_label(win, rx, ry, "Base:", COLOR_TEXT_DIM);
        geo_u64_to_str(geo.branches[sel].base_view, buf, 32);
        widget_label(win, rx + 56, ry, buf, COLOR_TEXT);
        ry += 18;

        widget_label(win, rx, ry, "Head:", COLOR_TEXT_DIM);
        geo_u64_to_str(geo.branches[sel].head_view, buf, 32);
        widget_label(win, rx + 56, ry, buf, COLOR_TEXT);
        ry += 18;

        /* Count views on branch */
        int bv = 0;
        for (int i = 0; i < geo.view_count; i++)
            if (geo.views[i].branch_id == geo.branches[sel].id) bv++;
        widget_label(win, rx, ry, "Views:", COLOR_TEXT_DIM);
        geo_u64_to_str((uint64_t)bv, buf, 32);
        widget_label(win, rx + 56, ry, buf, COLOR_TEXT);
        ry += 18;

        widget_label(win, rx, ry, "Status:", COLOR_TEXT_DIM);
        if (geo.branches[sel].id == geo.current_branch)
            widget_label(win, rx + 56, ry, "CURRENT", COLOR_GREEN_ACTIVE);
        else
            widget_label(win, rx + 56, ry, "inactive", COLOR_TEXT_DIM);
        ry += 24;

        /* Buttons */
        geo.branch_switch_btn.x = rx;
        geo.branch_switch_btn.y = ry;
        widget_button_draw(win, &geo.branch_switch_btn);

        geo.branch_diff_btn.x = rx + 76;
        geo.branch_diff_btn.y = ry;
        widget_button_draw(win, &geo.branch_diff_btn);
        ry += 28;

        /* Diff results */
        if (geo.diff_visible && geo.diff_buf[0]) {
            widget_label(win, rx, ry, "Diff vs current:", COLOR_TEXT_DIM);
            ry += 16;
            int dh = ch - ry - 8;
            if (dh > 16)
                widget_textbox(win, rx, ry, cw - rx - 8, dh,
                               geo.diff_buf, COLOR_TEXT, 0xFF0A0E1A);
        }
    } else {
        widget_label(win, rx, top + 40, "Select a branch", COLOR_TEXT_DIM);
    }
}

/* --- Paint: Dashboard tab --- */

static void geo_paint_dashboard(struct wm_window *win, int cw, int ch, int top)
{
    (void)ch;
    int y = top + 4;
    char buf[32];

    widget_label(win, 8, y, "STORAGE USAGE", COLOR_HIGHLIGHT);
    y += 20;

    /* Content gauge */
    int pct = 0;
    widget_label(win, 8, y, "Content:", COLOR_TEXT_DIM);
    if (geo.stats.content_region_size > 0)
        pct = (int)((geo.stats.content_region_used * 100) /
                     geo.stats.content_region_size);
    widget_progress(win, 80, y, cw - 180, 14, pct, 0xFF3B82F6, 0xFF0D0D1A);
    geo_u64_to_str(geo.stats.content_region_used, buf, 32);
    widget_label(win, cw - 90, y, buf, COLOR_TEXT);
    y += 22;

    /* Refs gauge */
    pct = 0;
    widget_label(win, 8, y, "Refs:", COLOR_TEXT_DIM);
    if (geo.stats.ref_region_size > 0)
        pct = (int)((geo.stats.ref_region_used * 100) /
                     geo.stats.ref_region_size);
    widget_progress(win, 80, y, cw - 180, 14, pct, 0xFF22C55E, 0xFF0D0D1A);
    geo_u64_to_str(geo.stats.ref_region_used, buf, 32);
    widget_label(win, cw - 90, y, buf, COLOR_TEXT);
    y += 22;

    /* Views gauge */
    pct = 0;
    widget_label(win, 8, y, "Views:", COLOR_TEXT_DIM);
    if (geo.stats.view_region_size > 0)
        pct = (int)((geo.stats.view_region_used * 100) /
                     geo.stats.view_region_size);
    widget_progress(win, 80, y, cw - 180, 14, pct, 0xFFF97316, 0xFF0D0D1A);
    geo_u64_to_str(geo.stats.view_region_used, buf, 32);
    widget_label(win, cw - 90, y, buf, COLOR_TEXT);
    y += 28;

    /* Counters */
    widget_label(win, 8, y, "COUNTERS", COLOR_HIGHLIGHT);
    y += 18;

    int cw4 = (cw - 16) / 4;

    widget_label(win, 8, y, "Files:", COLOR_TEXT_DIM);
    geo_u64_to_str(geo.stats.ref_count, buf, 32);
    widget_label(win, 52, y, buf, COLOR_TEXT);

    widget_label(win, 8 + cw4, y, "Views:", COLOR_TEXT_DIM);
    geo_u64_to_str(geo.stats.view_count, buf, 32);
    widget_label(win, 52 + cw4, y, buf, COLOR_TEXT);

    widget_label(win, 8 + cw4 * 2, y, "Branches:", COLOR_TEXT_DIM);
    geo_u64_to_str((uint64_t)geo.branch_count, buf, 32);
    widget_label(win, 76 + cw4 * 2, y, buf, COLOR_TEXT);

    widget_label(win, 8 + cw4 * 3, y, "Dedup:", COLOR_TEXT_DIM);
    geo_u64_to_str(geo.stats.dedup_hits, buf, 32);
    widget_label(win, 56 + cw4 * 3, y, buf, COLOR_TEXT);
    y += 28;

    /* Quota */
    widget_label(win, 8, y, "QUOTA", COLOR_HIGHLIGHT);
    y += 18;
    if (geo.quota_valid && geo.quota_limits.max_content_bytes > 0) {
        pct = (int)((geo.quota_content_used * 100) /
                     geo.quota_limits.max_content_bytes);
        widget_label(win, 8, y, "Volume:", COLOR_TEXT_DIM);
        widget_progress(win, 80, y, cw - 100, 14, pct,
                        COLOR_ICON_ORANGE, 0xFF0D0D1A);
        y += 22;
    } else {
        widget_label(win, 8, y, "No quota set", COLOR_TEXT_DIM);
        y += 18;
    }
    y += 10;

    /* Access context */
    widget_label(win, 8, y, "ACCESS CONTEXT", COLOR_HIGHLIGHT);
    y += 18;

    widget_label(win, 8, y, "UID:", COLOR_TEXT_DIM);
    geo_u64_to_str(geo.access_ctx.uid, buf, 32);
    widget_label(win, 40, y, buf, COLOR_TEXT);

    widget_label(win, 100, y, "GID:", COLOR_TEXT_DIM);
    geo_u64_to_str(geo.access_ctx.gid, buf, 32);
    widget_label(win, 132, y, buf, COLOR_TEXT);

    widget_label(win, 200, y, "Caps:", COLOR_TEXT_DIM);
    if (geo.access_ctx.caps & 0x80000000)
        widget_label(win, 244, y, "KERNEL", COLOR_GREEN_ACTIVE);
    else {
        geo_u64_to_str(geo.access_ctx.caps, buf, 32);
        widget_label(win, 244, y, buf, COLOR_TEXT);
    }
    y += 24;

    /* Current state */
    widget_label(win, 8, y, "CURRENT STATE", COLOR_HIGHLIGHT);
    y += 18;

    widget_label(win, 8, y, "View:", COLOR_TEXT_DIM);
    geo_u64_to_str(geo.current_view, buf, 32);
    widget_label(win, 56, y, buf, COLOR_GREEN_ACTIVE);

    widget_label(win, 160, y, "Branch:", COLOR_TEXT_DIM);
    widget_label(win, 224, y,
                 geo.current_branch_name[0] ? geo.current_branch_name : "main",
                 COLOR_GREEN_ACTIVE);
}

/* --- Paint dispatcher --- */

static void geology_paint(struct wm_window *win)
{
    int cw = wm_content_width(win);
    int ch = wm_content_height(win);

    widget_tabbar_draw(win, &geo.tabbar);
    geo.refresh_btn.x = cw - 76;
    geo.refresh_btn.y = 4;
    widget_button_draw(win, &geo.refresh_btn);

    int content_top = WIDGET_TAB_HEIGHT + 8;

    if (!fs_vol) {
        widget_label(win, 8, content_top + 20,
                     "No GeoFS volume mounted", COLOR_TEXT_DIM);
        return;
    }

    switch (geo.active_tab) {
    case 0: geo_paint_strata(win, cw, ch, content_top); break;
    case 1: geo_paint_branches(win, cw, ch, content_top); break;
    case 2: geo_paint_dashboard(win, cw, ch, content_top); break;
    }
}

/* --- Click handler --- */

static void geology_click(struct wm_window *win, int x, int y, int button)
{
    (void)button;
    int cw = wm_content_width(win);
    int ch = wm_content_height(win);

    /* Tab bar */
    int tab = widget_tabbar_click(&geo.tabbar, x, y);
    if (tab >= 0) {
        geo.active_tab = tab;
        geo.tabbar.selected = tab;
        return;
    }

    /* Refresh */
    if (widget_button_hit(&geo.refresh_btn, x, y)) {
        geo_refresh();
        return;
    }

    if (!fs_vol) return;

    switch (geo.active_tab) {
    case 0: /* Strata */
    {
        /* Scrollbar */
        int sb_w = WIDGET_SCROLLBAR_WIDTH;
        if (x >= cw - sb_w - 4) {
            int noff = widget_scrollbar_click(&geo.strata_sb, x, y);
            geo.strata_scroll = noff;
            return;
        }

        /* Band hit-test */
        int bat = geo.band_area_top;
        int bah = ch - bat - GEO_DETAIL_H - 4;
        if (y >= bat && y < bat + bah) {
            int from_bottom = (bat + bah - y) / GEO_BAND_H;
            int vi = geo.strata_scroll + from_bottom;
            if (vi >= 0 && vi < geo.view_count)
                geo.strata_selected = vi;
            return;
        }

        /* Switch button */
        if (geo.strata_selected >= 0 &&
            widget_button_hit(&geo.strata_switch_btn, x, y)) {
            kgeofs_view_switch(fs_vol, geo.views[geo.strata_selected].id);
            geo_refresh();
        }
        break;
    }
    case 1: /* Branches */
    {
        /* Branch list */
        int sel = widget_list_click(&geo.branch_list, x, y);
        if (sel >= 0) {
            geo.diff_visible = 0;
            geo.diff_buf[0] = '\0';
            return;
        }

        /* Switch */
        if (geo.branch_list.selected >= 0 &&
            widget_button_hit(&geo.branch_switch_btn, x, y)) {
            int si = geo.branch_list.selected;
            kgeofs_branch_switch_name(fs_vol, geo.branches[si].name);
            geo_refresh();
            return;
        }

        /* Diff */
        if (geo.branch_list.selected >= 0 &&
            widget_button_hit(&geo.branch_diff_btn, x, y)) {
            int si = geo.branch_list.selected;
            geo.diff_buf[0] = '\0';
            geo.diff_count = 0;
            kgeofs_branch_diff(fs_vol, geo.current_branch,
                               geo.branches[si].id, geo_diff_cb, NULL);
            if (geo.diff_count == 0)
                str_copy(geo.diff_buf, "(no differences)", GEO_DIFF_BUF_SIZE);
            geo.diff_visible = 1;
        }
        break;
    }
    case 2: /* Dashboard - no interactive elements */
        break;
    }
}

/* --- Key handler --- */

static void geology_key(struct wm_window *win, int key)
{
    (void)win;
    if (!fs_vol) return;

    /* Tab switching with Left/Right */
    if (key == KEY_LEFT && geo.active_tab > 0) {
        geo.active_tab--;
        geo.tabbar.selected = geo.active_tab;
        return;
    }
    if (key == KEY_RIGHT && geo.active_tab < 2) {
        geo.active_tab++;
        geo.tabbar.selected = geo.active_tab;
        return;
    }

    switch (geo.active_tab) {
    case 0: /* Strata: Up/Down select, Enter switch */
        if (key == KEY_UP) {
            if (geo.strata_selected < geo.view_count - 1)
                geo.strata_selected++;
        } else if (key == KEY_DOWN) {
            if (geo.strata_selected > 0)
                geo.strata_selected--;
            else if (geo.strata_selected < 0 && geo.view_count > 0)
                geo.strata_selected = 0;
        } else if (key == '\n' && geo.strata_selected >= 0) {
            kgeofs_view_switch(fs_vol, geo.views[geo.strata_selected].id);
            geo_refresh();
        }
        break;

    case 1: /* Branches: Up/Down select, Enter switch */
        if (key == KEY_UP) {
            if (geo.branch_list.selected > 0)
                geo.branch_list.selected--;
        } else if (key == KEY_DOWN) {
            if (geo.branch_list.selected < geo.branch_list.count - 1)
                geo.branch_list.selected++;
            else if (geo.branch_list.selected < 0 && geo.branch_list.count > 0)
                geo.branch_list.selected = 0;
        } else if (key == '\n' && geo.branch_list.selected >= 0) {
            int si = geo.branch_list.selected;
            kgeofs_branch_switch_name(fs_vol, geo.branches[si].name);
            geo_refresh();
        }
        break;

    case 2: /* Dashboard: no keys */
        break;
    }
}

/*============================================================================
 * Constitution Window (matches simulation constitution_view)
 *============================================================================*/

static void constitution_paint(struct wm_window *win)
{
    int y = 8;
    widget_label(win, 8, y, "PHANTOM CONSTITUTION", COLOR_HIGHLIGHT);
    y += 24;

    widget_label(win, 8, y, "Article I: Prime Directive", COLOR_ICON_PURPLE);
    y += 18;
    widget_label(win, 16, y, "To Create, Not To Destroy.", COLOR_TEXT);
    y += 16;
    widget_label(win, 16, y, "No data shall be deleted.", COLOR_TEXT);
    y += 24;

    widget_label(win, 8, y, "Article II: Preservation", COLOR_ICON_PURPLE);
    y += 18;
    widget_label(win, 16, y, "All operations are append-", COLOR_TEXT);
    y += 16;
    widget_label(win, 16, y, "only. History is immutable.", COLOR_TEXT);
    y += 24;

    widget_label(win, 8, y, "Article III: Protection", COLOR_ICON_PURPLE);
    y += 18;
    widget_label(win, 16, y, "The Governor shall evaluate", COLOR_TEXT);
    y += 16;
    widget_label(win, 16, y, "all code for safety before", COLOR_TEXT);
    y += 16;
    widget_label(win, 16, y, "execution is permitted.", COLOR_TEXT);
    y += 24;

    widget_label(win, 8, y, "Article IV: Alternatives", COLOR_ICON_PURPLE);
    y += 18;
    widget_label(win, 16, y, "Hide, not delete.", COLOR_TEXT);
    y += 16;
    widget_label(win, 16, y, "Suspend, not kill.", COLOR_TEXT);
    y += 16;
    widget_label(win, 16, y, "Transform, not destroy.", COLOR_TEXT);
}

/*============================================================================
 * Network Window (placeholder - matches simulation network_panel)
 *============================================================================*/

static void net_u64_to_str(uint64_t v, char *buf, int buf_size)
{
    char tmp[20];
    int len = 0;
    if (v == 0) { tmp[len++] = '0'; }
    else { while (v && len < 19) { tmp[len++] = '0' + (char)(v % 10); v /= 10; } }
    int i;
    for (i = 0; i < len && i < buf_size - 1; i++)
        buf[i] = tmp[len - 1 - i];
    buf[i] = '\0';
}

static void network_paint(struct wm_window *win)
{
    int y = 8;
    widget_label(win, 8, y, "NETWORK", COLOR_HIGHLIGHT);
    y += 24;

    if (!virtio_net_available()) {
        widget_label(win, 8, y, "Status:", COLOR_TEXT_DIM);
        widget_label(win, 80, y, "No NIC detected", COLOR_TEXT);
        y += 20;
        widget_label(win, 8, y, "Add -device virtio-net-pci", COLOR_TEXT_DIM);
        y += 16;
        widget_label(win, 8, y, "to QEMU command line.", COLOR_TEXT_DIM);
        y += 24;
        widget_label(win, 8, y, "Protected by AI Governor", COLOR_TEXT_DIM);
        return;
    }

    /* Link status */
    widget_label(win, 8, y, "Link:", COLOR_TEXT_DIM);
    widget_label(win, 80, y,
                 virtio_net_link_up() ? "Up" : "Down",
                 virtio_net_link_up() ? COLOR_GREEN_ACTIVE : COLOR_HIGHLIGHT);
    y += 20;

    /* MAC address */
    const uint8_t *mac = virtio_net_get_mac();
    char mac_str[18];
    if (mac) {
        static const char hex[] = "0123456789abcdef";
        for (int i = 0; i < 6; i++) {
            mac_str[i*3]   = hex[mac[i] >> 4];
            mac_str[i*3+1] = hex[mac[i] & 0xF];
            mac_str[i*3+2] = (i < 5) ? ':' : '\0';
        }
    } else {
        mac_str[0] = '-'; mac_str[1] = '\0';
    }
    widget_label(win, 8, y, "MAC:", COLOR_TEXT_DIM);
    widget_label(win, 80, y, mac_str, COLOR_TEXT);
    y += 20;

    /* IP / Gateway */
    widget_label(win, 8, y, "IP:", COLOR_TEXT_DIM);
    widget_label(win, 80, y, virtio_net_get_ip(), COLOR_TEXT);
    y += 20;
    widget_label(win, 8, y, "Gateway:", COLOR_TEXT_DIM);
    widget_label(win, 80, y, "10.0.2.2", COLOR_TEXT);
    y += 24;

    /* Statistics */
    const struct net_stats *ns = virtio_net_get_stats();
    char buf[32];

    widget_label(win, 8, y, "Packets TX:", COLOR_TEXT_DIM);
    net_u64_to_str(ns->tx_packets, buf, sizeof(buf));
    widget_label(win, 120, y, buf, COLOR_TEXT);
    y += 18;

    widget_label(win, 8, y, "Packets RX:", COLOR_TEXT_DIM);
    net_u64_to_str(ns->rx_packets, buf, sizeof(buf));
    widget_label(win, 120, y, buf, COLOR_TEXT);
    y += 18;

    widget_label(win, 8, y, "Bytes TX:", COLOR_TEXT_DIM);
    net_u64_to_str(ns->tx_bytes, buf, sizeof(buf));
    widget_label(win, 120, y, buf, COLOR_TEXT);
    y += 18;

    widget_label(win, 8, y, "Bytes RX:", COLOR_TEXT_DIM);
    net_u64_to_str(ns->rx_bytes, buf, sizeof(buf));
    widget_label(win, 120, y, buf, COLOR_TEXT);
    y += 24;

    widget_label(win, 8, y, "Protected by AI Governor", COLOR_TEXT_DIM);
}

/*============================================================================
 * DNAuth - DNA-Based Authentication
 * "Your genetic signature, immutably preserved"
 *============================================================================*/

static struct {
    int         enrolled;
    int         scanning;
    int         scan_progress;
    int         scan_tick;
    int         match_pct;
    char        sequence[33];   /* 32-char DNA display sequence */
    char        status_msg[64];
    uint32_t    status_color;
    struct widget_button enroll_btn;
    struct widget_button verify_btn;
} dna;

static void dna_init_state(void)
{
    memset(&dna, 0, sizeof(dna));
    /* Generate a pseudo-DNA sequence from timer ticks */
    const char bases[] = "ACGT";
    uint32_t seed = (uint32_t)timer_get_ticks();
    for (int i = 0; i < 32; i++) {
        seed = seed * 1103515245 + 12345;
        dna.sequence[i] = bases[(seed >> 16) & 3];
    }
    dna.sequence[32] = '\0';
    str_copy(dna.status_msg, "Ready to enroll", 64);
    dna.status_color = COLOR_TEXT_DIM;
}

static void dnauth_paint(struct wm_window *win)
{
    int cw = wm_content_width(win);
    int y = 8;

    widget_label(win, 8, y, "DNA AUTHENTICATION", COLOR_HIGHLIGHT);
    y += 20;
    widget_label(win, 8, y, "Genetic Signature Verification", COLOR_TEXT_DIM);
    y += 28;

    /* DNA helix visualization - two interleaved strands */
    widget_label(win, 8, y, "DNA Sequence:", COLOR_TEXT_DIM);
    y += 18;

    /* Draw sequence in colored base pairs */
    int sx = 12;
    for (int i = 0; i < 32; i++) {
        char ch[2] = { dna.sequence[i], '\0' };
        uint32_t col;
        if (ch[0] == 'A') col = 0xFF22C55E;      /* Green */
        else if (ch[0] == 'T') col = 0xFFE94560;  /* Red */
        else if (ch[0] == 'C') col = 0xFF3B82F6;  /* Blue */
        else col = 0xFFEAB308;                      /* Yellow - G */
        widget_label(win, sx, y, ch, col);
        sx += 8;
        if (i == 15) { y += 16; sx = 12; }
    }
    y += 24;

    /* Base pair legend */
    widget_label(win, 12, y, "A", 0xFF22C55E);
    widget_label(win, 20, y, "=Adenine", COLOR_TEXT_DIM);
    widget_label(win, 88, y, "T", 0xFFE94560);
    widget_label(win, 96, y, "=Thymine", COLOR_TEXT_DIM);
    y += 16;
    widget_label(win, 12, y, "C", 0xFF3B82F6);
    widget_label(win, 20, y, "=Cytosine", COLOR_TEXT_DIM);
    widget_label(win, 96, y, "G", 0xFFEAB308);
    widget_label(win, 104, y, "=Guanine", COLOR_TEXT_DIM);
    y += 24;

    /* Scan progress */
    if (dna.scanning) {
        widget_label(win, 8, y, "Scanning...", COLOR_ICON_PURPLE);
        y += 18;
        widget_progress(win, 8, y, cw - 16, 14, dna.scan_progress,
                        0xFF8B5CF6, 0xFF0D0D1A);
        y += 22;
    } else if (dna.enrolled) {
        widget_label(win, 8, y, "Match Confidence:", COLOR_TEXT_DIM);
        y += 18;
        widget_progress(win, 8, y, cw - 16, 14, dna.match_pct,
                        dna.match_pct > 90 ? COLOR_GREEN_ACTIVE : COLOR_ICON_ORANGE,
                        0xFF0D0D1A);
        y += 22;
    } else {
        y += 40;
    }

    /* Status */
    widget_label(win, 8, y, dna.status_msg, dna.status_color);
    y += 28;

    /* Buttons */
    dna.enroll_btn = (struct widget_button){8, y, 110, 28,
        dna.enrolled ? "Re-Enroll" : "Enroll DNA",
        COLOR_BUTTON_PRIMARY, COLOR_WHITE};
    widget_button_draw(win, &dna.enroll_btn);

    dna.verify_btn = (struct widget_button){126, y, 110, 28,
        "Verify", dna.enrolled ? 0xFF22C55E : 0xFF333355,
        dna.enrolled ? COLOR_WHITE : COLOR_TEXT_DIM};
    widget_button_draw(win, &dna.verify_btn);

    y += 40;
    widget_label(win, 8, y, "Protected by AI Governor", COLOR_TEXT_DIM);
    widget_label(win, 8, y + 16, "Sequence stored in GeoFS", COLOR_TEXT_DIM);
}

static void dnauth_click(struct wm_window *win, int x, int y, int btn)
{
    (void)win; (void)btn;
    if (widget_button_hit(&dna.enroll_btn, x, y)) {
        dna.scanning = 1;
        dna.scan_progress = 0;
        dna.scan_tick = 0;
        str_copy(dna.status_msg, "Enrolling DNA sequence...", 64);
        dna.status_color = COLOR_ICON_PURPLE;
    } else if (widget_button_hit(&dna.verify_btn, x, y) && dna.enrolled) {
        dna.scanning = 1;
        dna.scan_progress = 0;
        dna.scan_tick = 0;
        str_copy(dna.status_msg, "Verifying DNA match...", 64);
        dna.status_color = COLOR_ICON_PURPLE;
    }
}

/*============================================================================
 * LifeAuth - Plasma-Based Life Sign Authentication
 * "Living proof of identity"
 *============================================================================*/

static struct {
    int         enrolled;
    int         scanning;
    int         scan_progress;
    int         scan_tick;
    int         heart_rate;
    int         plasma_level;
    int         oxygen_sat;
    int         body_temp;     /* x10, e.g. 369 = 36.9C */
    char        status_msg[64];
    uint32_t    status_color;
    struct widget_button enroll_btn;
    struct widget_button verify_btn;
} life;

static void life_init_state(void)
{
    memset(&life, 0, sizeof(life));
    life.heart_rate = 72;
    life.plasma_level = 94;
    life.oxygen_sat = 98;
    life.body_temp = 369;
    str_copy(life.status_msg, "Ready for life sign scan", 64);
    life.status_color = COLOR_TEXT_DIM;
}

static void lifeauth_paint(struct wm_window *win)
{
    int cw = wm_content_width(win);
    int y = 8;

    widget_label(win, 8, y, "LIFEAUTH", COLOR_HIGHLIGHT);
    y += 20;
    widget_label(win, 8, y, "Plasma Life Sign Verification", COLOR_TEXT_DIM);
    y += 28;

    /* Vital signs display */
    widget_label(win, 8, y, "VITAL SIGNS", 0xFF22C55E);
    y += 22;

    char buf[32];
    int pos;

    /* Heart rate */
    widget_label(win, 8, y, "Heart Rate:", COLOR_TEXT_DIM);
    pos = 0;
    if (life.heart_rate >= 100) buf[pos++] = '0' + (char)(life.heart_rate / 100);
    if (life.heart_rate >= 10) buf[pos++] = '0' + (char)((life.heart_rate / 10) % 10);
    buf[pos++] = '0' + (char)(life.heart_rate % 10);
    buf[pos++] = ' '; buf[pos++] = 'b'; buf[pos++] = 'p'; buf[pos++] = 'm';
    buf[pos] = '\0';
    widget_label(win, 140, y, buf, COLOR_GREEN_ACTIVE);
    y += 18;
    widget_progress(win, 8, y, cw - 16, 10, life.heart_rate,
                    0xFFE94560, 0xFF0D0D1A);
    y += 18;

    /* Oxygen */
    widget_label(win, 8, y, "SpO2:", COLOR_TEXT_DIM);
    pos = 0;
    buf[pos++] = '0' + (char)(life.oxygen_sat / 10);
    buf[pos++] = '0' + (char)(life.oxygen_sat % 10);
    buf[pos++] = '%';
    buf[pos] = '\0';
    widget_label(win, 140, y, buf, COLOR_GREEN_ACTIVE);
    y += 18;
    widget_progress(win, 8, y, cw - 16, 10, life.oxygen_sat,
                    0xFF3B82F6, 0xFF0D0D1A);
    y += 18;

    /* Plasma level */
    widget_label(win, 8, y, "Plasma:", COLOR_TEXT_DIM);
    pos = 0;
    buf[pos++] = '0' + (char)(life.plasma_level / 10);
    buf[pos++] = '0' + (char)(life.plasma_level % 10);
    buf[pos++] = '%';
    buf[pos] = '\0';
    widget_label(win, 140, y, buf, 0xFF8B5CF6);
    y += 18;
    widget_progress(win, 8, y, cw - 16, 10, life.plasma_level,
                    0xFF8B5CF6, 0xFF0D0D1A);
    y += 18;

    /* Temperature */
    widget_label(win, 8, y, "Body Temp:", COLOR_TEXT_DIM);
    pos = 0;
    buf[pos++] = '0' + (char)(life.body_temp / 100);
    buf[pos++] = '0' + (char)((life.body_temp / 10) % 10);
    buf[pos++] = '.';
    buf[pos++] = '0' + (char)(life.body_temp % 10);
    buf[pos++] = 'C';
    buf[pos] = '\0';
    widget_label(win, 140, y, buf, COLOR_GREEN_ACTIVE);
    y += 24;

    /* Scan progress or status */
    if (life.scanning) {
        widget_label(win, 8, y, "Scanning vitals...", COLOR_ICON_PURPLE);
        y += 18;
        widget_progress(win, 8, y, cw - 16, 14, life.scan_progress,
                        0xFFE94560, 0xFF0D0D1A);
        y += 22;
    } else {
        widget_label(win, 8, y, life.status_msg, life.status_color);
        y += 28;
    }

    /* Buttons */
    life.enroll_btn = (struct widget_button){8, y, 110, 28,
        life.enrolled ? "Re-Scan" : "Enroll",
        COLOR_BUTTON_PRIMARY, COLOR_WHITE};
    widget_button_draw(win, &life.enroll_btn);

    life.verify_btn = (struct widget_button){126, y, 110, 28,
        "Authenticate", life.enrolled ? 0xFF22C55E : 0xFF333355,
        life.enrolled ? COLOR_WHITE : COLOR_TEXT_DIM};
    widget_button_draw(win, &life.verify_btn);
}

static void lifeauth_click(struct wm_window *win, int x, int y, int btn)
{
    (void)win; (void)btn;
    if (widget_button_hit(&life.enroll_btn, x, y)) {
        life.scanning = 1;
        life.scan_progress = 0;
        life.scan_tick = 0;
        str_copy(life.status_msg, "Recording life signs...", 64);
        life.status_color = COLOR_ICON_PURPLE;
    } else if (widget_button_hit(&life.verify_btn, x, y) && life.enrolled) {
        life.scanning = 1;
        life.scan_progress = 0;
        life.scan_tick = 0;
        str_copy(life.status_msg, "Authenticating...", 64);
        life.status_color = COLOR_ICON_PURPLE;
    }
}

/*============================================================================
 * BioSense - Vein Pattern Biometric Scanner
 * "The patterns within, mapped forever"
 *============================================================================*/

static struct {
    int         enrolled;
    int         scanning;
    int         scan_progress;
    int         scan_tick;
    int         vein_map[8];   /* 8 simplified vein density values 0-100 */
    int         match_pct;
    char        status_msg[64];
    uint32_t    status_color;
    struct widget_button scan_btn;
    struct widget_button auth_btn;
} bio;

static void bio_init_state(void)
{
    memset(&bio, 0, sizeof(bio));
    uint32_t seed = (uint32_t)timer_get_ticks() ^ 0xDEADBEEF;
    for (int i = 0; i < 8; i++) {
        seed = seed * 1103515245 + 12345;
        bio.vein_map[i] = 40 + (int)((seed >> 16) % 50);
    }
    str_copy(bio.status_msg, "Place hand on scanner", 64);
    bio.status_color = COLOR_TEXT_DIM;
}

static void biosense_paint(struct wm_window *win)
{
    int cw = wm_content_width(win);
    int y = 8;

    widget_label(win, 8, y, "BIOSENSE", COLOR_HIGHLIGHT);
    y += 20;
    widget_label(win, 8, y, "Vein Pattern Biometric Scanner", COLOR_TEXT_DIM);
    y += 28;

    /* Vein pattern visualization - bar chart */
    widget_label(win, 8, y, "VEIN DENSITY MAP", 0xFF3B82F6);
    y += 20;

    int bar_w = (cw - 32) / 8;
    for (int i = 0; i < 8; i++) {
        int bx = 12 + i * (bar_w + 2);
        int bar_h = bio.vein_map[i] * 60 / 100;
        int by = y + 60 - bar_h;

        /* Draw bar background */
        gfx_fill_rounded_rect(
            win->x + WM_BORDER_WIDTH + bx,
            win->y + WM_TITLE_HEIGHT + by,
            bar_w - 2, bar_h, 2, 0xFF1E3A5F);

        /* Draw bar fill */
        uint32_t bar_col = bio.scanning ? 0xFF3B82F6 : 0xFF1E5A8F;
        gfx_fill_rounded_rect(
            win->x + WM_BORDER_WIDTH + bx,
            win->y + WM_TITLE_HEIGHT + by,
            bar_w - 2, bar_h, 2, bar_col);

        /* Label */
        char lbl[3];
        lbl[0] = 'R'; lbl[1] = '1' + (char)i; lbl[2] = '\0';
        widget_label(win, bx + 2, y + 64, lbl, COLOR_TEXT_DIM);
    }
    y += 82;

    /* Scan metrics */
    widget_label(win, 8, y, "Sensor:", COLOR_TEXT_DIM);
    widget_label(win, 80, y, bio.scanning ? "ACTIVE" : "STANDBY",
                 bio.scanning ? COLOR_GREEN_ACTIVE : COLOR_TEXT_DIM);
    y += 18;

    widget_label(win, 8, y, "Points:", COLOR_TEXT_DIM);
    widget_label(win, 80, y, bio.enrolled ? "2,048" : "0", COLOR_TEXT);
    y += 18;

    if (bio.enrolled && !bio.scanning) {
        char buf[16];
        int pos = 0;
        if (bio.match_pct >= 100) buf[pos++] = '1';
        if (bio.match_pct >= 100) { buf[pos++] = '0'; buf[pos++] = '0'; }
        else {
            if (bio.match_pct >= 10) buf[pos++] = '0' + (char)(bio.match_pct / 10);
            buf[pos++] = '0' + (char)(bio.match_pct % 10);
        }
        buf[pos++] = '%'; buf[pos] = '\0';
        widget_label(win, 8, y, "Match:", COLOR_TEXT_DIM);
        widget_label(win, 80, y, buf,
                     bio.match_pct > 90 ? COLOR_GREEN_ACTIVE : COLOR_ICON_ORANGE);
    }
    y += 22;

    /* Progress */
    if (bio.scanning) {
        widget_progress(win, 8, y, cw - 16, 14, bio.scan_progress,
                        0xFF3B82F6, 0xFF0D0D1A);
        y += 22;
    } else {
        y += 22;
    }

    /* Status */
    widget_label(win, 8, y, bio.status_msg, bio.status_color);
    y += 28;

    /* Buttons */
    bio.scan_btn = (struct widget_button){8, y, 110, 28,
        bio.enrolled ? "Re-Scan" : "Scan Hand",
        COLOR_BUTTON_PRIMARY, COLOR_WHITE};
    widget_button_draw(win, &bio.scan_btn);

    bio.auth_btn = (struct widget_button){126, y, 110, 28,
        "Authenticate", bio.enrolled ? 0xFF22C55E : 0xFF333355,
        bio.enrolled ? COLOR_WHITE : COLOR_TEXT_DIM};
    widget_button_draw(win, &bio.auth_btn);
}

static void biosense_click(struct wm_window *win, int x, int y, int btn)
{
    (void)win; (void)btn;
    if (widget_button_hit(&bio.scan_btn, x, y)) {
        bio.scanning = 1;
        bio.scan_progress = 0;
        bio.scan_tick = 0;
        str_copy(bio.status_msg, "Scanning vein patterns...", 64);
        bio.status_color = 0xFF3B82F6;
    } else if (widget_button_hit(&bio.auth_btn, x, y) && bio.enrolled) {
        bio.scanning = 1;
        bio.scan_progress = 0;
        bio.scan_tick = 0;
        str_copy(bio.status_msg, "Verifying vein pattern...", 64);
        bio.status_color = 0xFF3B82F6;
    }
}

/*============================================================================
 * PVE - Planck Variable Encryption
 * "Keys evolving at the speed of time itself"
 *============================================================================*/

static void pve_paint(struct wm_window *win)
{
    int cx = win->x;
    int cy = win->y + WM_TITLE_HEIGHT;
    int cw = win->width;
    uint32_t bg = 0xFF1A1A2E;

    /* Background */
    fb_fill_rect(cx, cy, cw, win->height - WM_TITLE_HEIGHT, bg);

    /* Title */
    font_draw_string(cx + 10, cy + 4, "Planck Variable Encryption", 0xFFFFFFFF, bg);

    /* Planck Clock */
    {
        char clk[48];
        str_copy(clk, "Planck Clock: ", 48);
        char tmp[24];
        gov_u64_to_str(pve_state.planck_clock, tmp);
        gov_strcat(clk, tmp);
        font_draw_string(cx + 10, cy + 22, clk, 0xFF00E5FF, bg);
    }

    /* Evolution count */
    {
        char evo[48];
        str_copy(evo, "Evolutions: ", 48);
        char tmp[24];
        gov_u64_to_str(pve_state.evolution_count, tmp);
        gov_strcat(evo, tmp);
        font_draw_string(cx + 10, cy + 38, evo, 0xFF4ADE80, bg);
    }

    /* Separator */
    fb_fill_rect(cx + 8, cy + 56, cw - 16, 1, 0xFF444466);

    /* Current Key */
    font_draw_string(cx + 10, cy + 62, "Current Key:", 0xFFCCCCCC, bg);
    fb_fill_rect(cx + 8, cy + 76, cw - 16, 18, 0xFF0D0D1A);
    {
        char keyhex[80];
        pve_format_key_hex(pve_state.current_key, keyhex, 80);
        font_draw_string(cx + 12, cy + 78, keyhex, 0xFFFFD700, 0xFF0D0D1A);
    }

    /* Key Evolution bar chart */
    font_draw_string(cx + 10, cy + 100, "Key Evolution:", 0xFFCCCCCC, bg);
    fb_fill_rect(cx + 8, cy + 116, cw - 16, 34, 0xFF0D0D1A);
    {
        int bars = pve_state.hist_filled;
        int bw = 8;
        for (int i = 0; i < bars && i < PVE_HISTORY_SLOTS; i++) {
            int idx = (pve_state.hist_head - bars + i + PVE_HISTORY_SLOTS) % PVE_HISTORY_SLOTS;
            int val = pve_state.history[idx];
            int bh = (val * 30) / 255;
            if (bh < 1) bh = 1;
            /* Color gradient: blue to green based on value */
            uint8_t g = (uint8_t)((val * 200) / 255 + 55);
            uint8_t b = (uint8_t)(255 - (val * 200) / 255);
            uint32_t col = 0xFF000000 | ((uint32_t)g << 8) | (uint32_t)b;
            fb_fill_rect(cx + 10 + i * bw, cy + 116 + 32 - bh, bw - 1, bh, col);
        }
    }

    /* Cipher mode + block info */
    {
        char ent[48];
        str_copy(ent, "Mode: PVE-SBC", 48);
        if (pve_state.has_cipher) {
            gov_strcat(ent, " [");
            char tmp[8];
            gov_u64_to_str((uint64_t)(pve_state.padded_len / PVE_KEY_LEN), tmp);
            gov_strcat(ent, tmp);
            gov_strcat(ent, " blocks]");
        }
        font_draw_string(cx + 10, cy + 156, ent, 0xFF4ADE80, bg);
    }

    /* Separator */
    fb_fill_rect(cx + 8, cy + 174, cw - 16, 1, 0xFF444466);

    /* Message label */
    font_draw_string(cx + 10, cy + 180, "Message:", 0xFFCCCCCC, bg);

    /* Text input widget */
    widget_textinput_draw(win, &pve_state.text_input);

    /* Buttons */
    fb_fill_rect(cx + 10, cy + 224, 90, 22, 0xFF22C55E);
    font_draw_string(cx + 22, cy + 227, "Encrypt", 0xFFFFFFFF, 0xFF22C55E);
    fb_fill_rect(cx + 110, cy + 224, 90, 22, 0xFF3B82F6);
    font_draw_string(cx + 122, cy + 227, "Decrypt", 0xFFFFFFFF, 0xFF3B82F6);

    /* Ciphertext display */
    font_draw_string(cx + 10, cy + 252, "Ciphertext:", 0xFFCCCCCC, bg);
    if (pve_state.has_cipher) {
        char chex[80];
        pve_format_cipher_hex(chex, 80);
        font_draw_string(cx + 10, cy + 268, chex, 0xFFFF6B6B, bg);
    } else {
        font_draw_string(cx + 10, cy + 268, "No data", 0xFF666666, bg);
    }

    /* Decrypted display */
    font_draw_string(cx + 10, cy + 288, "Decrypted:", 0xFFCCCCCC, bg);
    if (pve_state.has_decrypted) {
        font_draw_string(cx + 10, cy + 304, pve_state.decrypted, 0xFF4ADE80, bg);
    } else {
        font_draw_string(cx + 10, cy + 304, "--", 0xFF666666, bg);
    }
}

static void pve_click(struct wm_window *win, int x, int y, int btn)
{
    (void)win; (void)btn;
    int cy_off = y;  /* y is already content-relative */
    /* Encrypt button: y=224..246, x=10..100 */
    if (cy_off >= 224 && cy_off < 246 && x >= 10 && x < 100) {
        pve_do_encrypt();
        return;
    }
    /* Decrypt button: y=224..246, x=110..200 */
    if (cy_off >= 224 && cy_off < 246 && x >= 110 && x < 200) {
        pve_do_decrypt();
        return;
    }
    /* Text input click */
    widget_textinput_click(&pve_state.text_input, x, y);
}

static void pve_key(struct wm_window *win, int key)
{
    (void)win;
    if (key == '\n' || key == '\r') {
        pve_do_encrypt();
        return;
    }
    widget_textinput_key(&pve_state.text_input, key);
}

/*============================================================================
 * QRNet - QR Code Based Networking
 * "Visual data transfer, cryptographically sealed"
 *============================================================================*/

static struct {
    int         connected;
    int         generating;
    int         gen_progress;
    int         gen_tick;
    int         packets_sent;
    int         packets_recv;
    uint8_t     qr_grid[16][16]; /* Simple 16x16 QR-like pattern */
    char        peer_id[17];
    char        status_msg[64];
    uint32_t    status_color;
    struct widget_button gen_btn;
    struct widget_button connect_btn;
} qr;

static void qr_init_state(void)
{
    memset(&qr, 0, sizeof(qr));
    /* Generate a pseudo-random QR pattern */
    uint32_t seed = (uint32_t)timer_get_ticks() ^ 0xCAFEBABE;
    for (int r = 0; r < 16; r++)
        for (int c = 0; c < 16; c++) {
            seed = seed * 1103515245 + 12345;
            qr.qr_grid[r][c] = ((seed >> 16) & 7) < 3 ? 1 : 0;
        }
    /* Finder patterns in corners */
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            qr.qr_grid[i][j] = (i == 0 || i == 3 || j == 0 || j == 3) ? 1 : 0;
            qr.qr_grid[i][12+j] = (i == 0 || i == 3 || j == 0 || j == 3) ? 1 : 0;
            qr.qr_grid[12+i][j] = (i == 0 || i == 3 || j == 0 || j == 3) ? 1 : 0;
        }
    qr.qr_grid[1][1] = 1; qr.qr_grid[1][13] = 1; qr.qr_grid[13][1] = 1;

    const char hex[] = "0123456789ABCDEF";
    seed = (uint32_t)timer_get_ticks();
    for (int i = 0; i < 16; i++) {
        seed = seed * 1103515245 + 12345;
        qr.peer_id[i] = hex[(seed >> 16) & 0xF];
    }
    qr.peer_id[16] = '\0';
    str_copy(qr.status_msg, "Ready to generate QR code", 64);
    qr.status_color = COLOR_TEXT_DIM;
}

static void qrnet_paint(struct wm_window *win)
{
    int cw = wm_content_width(win);
    int y = 8;

    widget_label(win, 8, y, "QRNET", COLOR_HIGHLIGHT);
    y += 20;
    widget_label(win, 8, y, "QR Code Networking Protocol", COLOR_TEXT_DIM);
    y += 28;

    /* Draw QR code grid */
    int cell = 6;
    int qr_w = 16 * cell;
    int qr_x = (cw - qr_w) / 2;
    int qr_y = y;

    /* QR background */
    gfx_fill_rounded_rect(
        win->x + WM_BORDER_WIDTH + qr_x - 4,
        win->y + WM_TITLE_HEIGHT + qr_y - 4,
        qr_w + 8, qr_w + 8, 4, COLOR_WHITE);

    for (int r = 0; r < 16; r++)
        for (int c = 0; c < 16; c++) {
            uint32_t col = qr.qr_grid[r][c] ? COLOR_BLACK : COLOR_WHITE;
            int px = win->x + WM_BORDER_WIDTH + qr_x + c * cell;
            int py = win->y + WM_TITLE_HEIGHT + qr_y + r * cell;
            fb_fill_rect((uint32_t)px, (uint32_t)py,
                        (uint32_t)cell, (uint32_t)cell, col);
        }
    y += qr_w + 12;

    /* Peer ID */
    widget_label(win, 8, y, "Peer ID:", COLOR_TEXT_DIM);
    y += 16;
    widget_label(win, 16, y, qr.peer_id, 0xFF3B82F6);
    y += 22;

    /* Stats */
    char buf[32];
    int pos;
    widget_label(win, 8, y, "Sent:", COLOR_TEXT_DIM);
    pos = 0;
    int v = qr.packets_sent;
    if (v >= 100) buf[pos++] = '0' + (char)(v / 100);
    if (v >= 10) buf[pos++] = '0' + (char)((v / 10) % 10);
    buf[pos++] = '0' + (char)(v % 10);
    buf[pos++] = ' '; buf[pos++] = 'p'; buf[pos++] = 'k'; buf[pos++] = 't';
    buf[pos] = '\0';
    widget_label(win, 80, y, buf, COLOR_TEXT);
    y += 16;

    widget_label(win, 8, y, "Recv:", COLOR_TEXT_DIM);
    pos = 0;
    v = qr.packets_recv;
    if (v >= 100) buf[pos++] = '0' + (char)(v / 100);
    if (v >= 10) buf[pos++] = '0' + (char)((v / 10) % 10);
    buf[pos++] = '0' + (char)(v % 10);
    buf[pos++] = ' '; buf[pos++] = 'p'; buf[pos++] = 'k'; buf[pos++] = 't';
    buf[pos] = '\0';
    widget_label(win, 80, y, buf, COLOR_TEXT);
    y += 22;

    /* Progress */
    if (qr.generating) {
        widget_progress(win, 8, y, cw - 16, 12, qr.gen_progress,
                        0xFF3B82F6, 0xFF0D0D1A);
        y += 20;
    } else {
        y += 20;
    }

    /* Status */
    widget_label(win, 8, y, qr.status_msg, qr.status_color);
    y += 24;

    /* Buttons */
    qr.gen_btn = (struct widget_button){8, y, 120, 28,
        "Generate QR", COLOR_BUTTON_PRIMARY, COLOR_WHITE};
    widget_button_draw(win, &qr.gen_btn);

    qr.connect_btn = (struct widget_button){136, y, 120, 28,
        qr.connected ? "Disconnect" : "Connect",
        qr.connected ? COLOR_ICON_ORANGE : 0xFF22C55E, COLOR_WHITE};
    widget_button_draw(win, &qr.connect_btn);
}

static void qrnet_click(struct wm_window *win, int x, int y, int btn)
{
    (void)win; (void)btn;
    if (widget_button_hit(&qr.gen_btn, x, y)) {
        qr.generating = 1;
        qr.gen_progress = 0;
        qr.gen_tick = 0;
        /* Regenerate pattern */
        uint32_t seed = (uint32_t)timer_get_ticks();
        for (int r = 0; r < 16; r++)
            for (int c = 0; c < 16; c++) {
                seed = seed * 1103515245 + 12345;
                qr.qr_grid[r][c] = ((seed >> 16) & 7) < 3 ? 1 : 0;
            }
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++) {
                qr.qr_grid[i][j] = (i == 0 || i == 3 || j == 0 || j == 3) ? 1 : 0;
                qr.qr_grid[i][12+j] = (i == 0 || i == 3 || j == 0 || j == 3) ? 1 : 0;
                qr.qr_grid[12+i][j] = (i == 0 || i == 3 || j == 0 || j == 3) ? 1 : 0;
            }
        qr.qr_grid[1][1] = 1; qr.qr_grid[1][13] = 1; qr.qr_grid[13][1] = 1;
        str_copy(qr.status_msg, "Generating new QR code...", 64);
        qr.status_color = 0xFF3B82F6;
    } else if (widget_button_hit(&qr.connect_btn, x, y)) {
        qr.connected = !qr.connected;
        if (qr.connected) {
            str_copy(qr.status_msg, "Connected to PhantomNet", 64);
            qr.status_color = COLOR_GREEN_ACTIVE;
        } else {
            str_copy(qr.status_msg, "Disconnected", 64);
            qr.status_color = COLOR_TEXT_DIM;
            qr.packets_sent = 0;
            qr.packets_recv = 0;
        }
    }
}

/*============================================================================
 * Notes - Append-Only Note Editor
 * "Words preserved in geological strata"
 *============================================================================*/

#define NOTES_MAX       8
#define NOTE_TEXT_MAX   256
#define NOTE_TITLE_MAX  32

static struct {
    struct {
        char    title[NOTE_TITLE_MAX];
        char    text[NOTE_TEXT_MAX];
        int     text_len;
    } notes[NOTES_MAX];
    int         count;
    int         selected;       /* -1 = none */
    int         editing;        /* 1 = editing selected note text */
    int         cursor;
    struct widget_button new_btn;
    struct widget_button save_btn;
    struct widget_textinput title_input;
    char        list_names[NOTES_MAX][NOTE_TITLE_MAX];
    struct widget_list note_list;
} notes;

static void notes_init_state(void)
{
    memset(&notes, 0, sizeof(notes));
    notes.selected = -1;
    widget_textinput_init(&notes.title_input, 60, 0, 180, 20);

    /* Pre-populate with a welcome note */
    str_copy(notes.notes[0].title, "Welcome", NOTE_TITLE_MAX);
    str_copy(notes.notes[0].text,
        "PhantomOS Notes\n"
        "All notes are preserved\n"
        "in geological layers.\n"
        "Nothing is ever lost.", NOTE_TEXT_MAX);
    notes.notes[0].text_len = (int)strlen(notes.notes[0].text);
    notes.count = 1;
}

static void notes_refresh_list(void)
{
    notes.note_list.count = 0;
    for (int i = 0; i < notes.count; i++) {
        str_copy(notes.list_names[i], notes.notes[i].title, NOTE_TITLE_MAX);
        notes.note_list.items[i] = notes.list_names[i];
        notes.note_list.count++;
    }
}

static void notes_paint(struct wm_window *win)
{
    int cw = wm_content_width(win);
    int ch = wm_content_height(win);
    int y = 8;

    widget_label(win, 8, y, "NOTES", COLOR_HIGHLIGHT);
    y += 24;

    /* New note button */
    notes.new_btn = (struct widget_button){8, y, 80, 24,
        "New Note", COLOR_BUTTON_PRIMARY, COLOR_WHITE};
    widget_button_draw(win, &notes.new_btn);

    if (notes.selected >= 0) {
        notes.save_btn = (struct widget_button){96, y, 60, 24,
            "Save", COLOR_GREEN_ACTIVE, COLOR_WHITE};
        widget_button_draw(win, &notes.save_btn);
    }
    y += 32;

    /* Note list on left */
    int list_w = 120;
    notes_refresh_list();
    notes.note_list.x = 8;
    notes.note_list.y = y;
    notes.note_list.w = list_w;
    notes.note_list.h = ch - y - 8;
    notes.note_list.selected = notes.selected;
    widget_list_draw(win, &notes.note_list);

    /* Editor area on right */
    int ex = list_w + 16;
    int ew = cw - ex - 8;
    if (ew < 40) ew = 40;

    if (notes.selected >= 0 && notes.selected < notes.count) {
        /* Title */
        widget_label(win, ex, y, "Title:", COLOR_TEXT_DIM);
        widget_label(win, ex + 48, y, notes.notes[notes.selected].title,
                     COLOR_TEXT);
        y += 20;

        /* Separator */
        gfx_draw_hline(win->x + WM_BORDER_WIDTH + ex, win->y + WM_TITLE_HEIGHT + y,
                       ew, COLOR_PANEL_BORDER);
        y += 6;

        /* Note text */
        widget_textbox(win, ex, y, ew, ch - y - 8,
                      notes.notes[notes.selected].text,
                      COLOR_TEXT, 0xFF0A0E1A);
    } else {
        widget_label(win, ex, y + 40, "Select or create", COLOR_TEXT_DIM);
        widget_label(win, ex, y + 56, "a note", COLOR_TEXT_DIM);
    }
}

static void notes_click(struct wm_window *win, int x, int y, int btn)
{
    (void)win; (void)btn;
    if (widget_button_hit(&notes.new_btn, x, y)) {
        if (notes.count < NOTES_MAX) {
            int idx = notes.count++;
            str_copy(notes.notes[idx].title, "New Note", NOTE_TITLE_MAX);
            notes.notes[idx].text[0] = '\0';
            notes.notes[idx].text_len = 0;
            notes.selected = idx;
            notes.editing = 1;
            notes.cursor = 0;
        }
    } else if (notes.selected >= 0 && widget_button_hit(&notes.save_btn, x, y)) {
        /* Save confirmed (data already in struct, GeoFS would persist) */
    } else {
        int sel = widget_list_click(&notes.note_list, x, y);
        if (sel >= 0 && sel < notes.count) {
            notes.selected = sel;
            notes.editing = 1;
            notes.cursor = notes.notes[sel].text_len;
        }
    }
}

static void notes_key(struct wm_window *win, int key)
{
    (void)win;
    if (!notes.editing || notes.selected < 0) return;
    int idx = notes.selected;

    if (key == '\b' || key == 127) {
        if (notes.cursor > 0 && notes.notes[idx].text_len > 0) {
            notes.cursor--;
            memmove(&notes.notes[idx].text[notes.cursor],
                    &notes.notes[idx].text[notes.cursor + 1],
                    (size_t)(notes.notes[idx].text_len - notes.cursor));
            notes.notes[idx].text_len--;
        }
    } else if (key == '\n') {
        if (notes.notes[idx].text_len < NOTE_TEXT_MAX - 1) {
            memmove(&notes.notes[idx].text[notes.cursor + 1],
                    &notes.notes[idx].text[notes.cursor],
                    (size_t)(notes.notes[idx].text_len - notes.cursor));
            notes.notes[idx].text[notes.cursor] = '\n';
            notes.notes[idx].text_len++;
            notes.cursor++;
            notes.notes[idx].text[notes.notes[idx].text_len] = '\0';
        }
    } else if (key >= 32 && key < 127) {
        if (notes.notes[idx].text_len < NOTE_TEXT_MAX - 1) {
            memmove(&notes.notes[idx].text[notes.cursor + 1],
                    &notes.notes[idx].text[notes.cursor],
                    (size_t)(notes.notes[idx].text_len - notes.cursor));
            notes.notes[idx].text[notes.cursor] = (char)key;
            notes.notes[idx].text_len++;
            notes.cursor++;
            notes.notes[idx].text[notes.notes[idx].text_len] = '\0';
        }
    }
}

/*============================================================================
 * Media Player
 * "Every frequency preserved in geological time"
 *============================================================================*/

#define MEDIA_TRACKS    6
#define MEDIA_VIS_BARS  24

static struct {
    int         playing;
    int         current_track;
    int         progress;       /* 0-100 */
    int         volume;         /* 0-100 */
    int         tick;
    int         vis_bars[MEDIA_VIS_BARS];
    struct widget_button play_btn;
    struct widget_button prev_btn;
    struct widget_button next_btn;
    struct widget_button stop_btn;
} media;

static const char *media_tracks[MEDIA_TRACKS] = {
    "Phantom Overture",
    "Digital Strata",
    "Geology of Sound",
    "Append Only Dreams",
    "Governor's Theme",
    "Creation Hymn",
};

static void media_init_state(void)
{
    memset(&media, 0, sizeof(media));
    media.volume = 75;
}

static void media_paint(struct wm_window *win)
{
    int cw = wm_content_width(win);
    int y = 8;

    widget_label(win, 8, y, "MEDIA PLAYER", COLOR_HIGHLIGHT);
    y += 28;

    /* Now playing */
    widget_label(win, 8, y, "Now Playing:", COLOR_TEXT_DIM);
    y += 18;
    widget_label(win, 16, y, media_tracks[media.current_track],
                 media.playing ? COLOR_GREEN_ACTIVE : COLOR_TEXT);
    y += 24;

    /* Audio visualizer */
    if (media.playing) {
        int bar_w = (cw - 20) / MEDIA_VIS_BARS;
        for (int i = 0; i < MEDIA_VIS_BARS; i++) {
            int bx = 8 + i * bar_w;
            int bar_h = media.vis_bars[i] * 40 / 15;
            int by = y + 40 - bar_h;
            uint32_t col = 0xFF22C55E;
            if (bar_h > 30) col = 0xFFE94560;
            else if (bar_h > 20) col = 0xFFEAB308;
            gfx_fill_rounded_rect(
                win->x + WM_BORDER_WIDTH + bx,
                win->y + WM_TITLE_HEIGHT + by,
                bar_w - 2, bar_h, 1, col);
        }
    }
    y += 48;

    /* Progress bar */
    widget_progress(win, 8, y, cw - 16, 10, media.progress,
                    COLOR_BUTTON_PRIMARY, 0xFF0D0D1A);
    y += 16;

    /* Time display */
    int secs = media.progress * 240 / 100;
    int mins = secs / 60;
    secs %= 60;
    char tbuf[16];
    int tp = 0;
    tbuf[tp++] = '0' + (char)(mins / 10);
    tbuf[tp++] = '0' + (char)(mins % 10);
    tbuf[tp++] = ':';
    tbuf[tp++] = '0' + (char)(secs / 10);
    tbuf[tp++] = '0' + (char)(secs % 10);
    tbuf[tp++] = ' '; tbuf[tp++] = '/'; tbuf[tp++] = ' ';
    tbuf[tp++] = '0'; tbuf[tp++] = '4'; tbuf[tp++] = ':';
    tbuf[tp++] = '0'; tbuf[tp++] = '0';
    tbuf[tp] = '\0';
    widget_label(win, 8, y, tbuf, COLOR_TEXT_DIM);
    y += 22;

    /* Controls */
    media.prev_btn = (struct widget_button){8, y, 50, 28,
        "|<", COLOR_BUTTON, COLOR_WHITE};
    widget_button_draw(win, &media.prev_btn);

    media.play_btn = (struct widget_button){64, y, 70, 28,
        media.playing ? "Pause" : "Play",
        media.playing ? COLOR_ICON_ORANGE : COLOR_GREEN_ACTIVE, COLOR_WHITE};
    widget_button_draw(win, &media.play_btn);

    media.stop_btn = (struct widget_button){140, y, 50, 28,
        "Stop", COLOR_HIGHLIGHT, COLOR_WHITE};
    widget_button_draw(win, &media.stop_btn);

    media.next_btn = (struct widget_button){196, y, 50, 28,
        ">|", COLOR_BUTTON, COLOR_WHITE};
    widget_button_draw(win, &media.next_btn);
    y += 36;

    /* Volume */
    widget_label(win, 8, y, "Volume:", COLOR_TEXT_DIM);
    widget_progress(win, 72, y + 2, cw - 80, 10, media.volume,
                    0xFF3B82F6, 0xFF0D0D1A);
    y += 24;

    /* Track list */
    widget_label(win, 8, y, "PLAYLIST", COLOR_TEXT_DIM);
    y += 18;
    for (int i = 0; i < MEDIA_TRACKS; i++) {
        uint32_t col = (i == media.current_track) ?
            (media.playing ? COLOR_GREEN_ACTIVE : COLOR_HIGHLIGHT) : COLOR_TEXT;
        char prefix[4];
        prefix[0] = (i == media.current_track && media.playing) ? '>' : ' ';
        prefix[1] = ' ';
        prefix[2] = '1' + (char)i;
        prefix[3] = '\0';
        widget_label(win, 8, y, prefix, col);
        widget_label(win, 32, y, media_tracks[i], col);
        y += 16;
    }
}

static void media_click(struct wm_window *win, int x, int y, int btn)
{
    (void)win; (void)btn;
    if (widget_button_hit(&media.play_btn, x, y)) {
        media.playing = !media.playing;
    } else if (widget_button_hit(&media.stop_btn, x, y)) {
        media.playing = 0;
        media.progress = 0;
    } else if (widget_button_hit(&media.prev_btn, x, y)) {
        if (media.current_track > 0) media.current_track--;
        else media.current_track = MEDIA_TRACKS - 1;
        media.progress = 0;
    } else if (widget_button_hit(&media.next_btn, x, y)) {
        media.current_track = (media.current_track + 1) % MEDIA_TRACKS;
        media.progress = 0;
    }
}

/*============================================================================
 * Users - User Management
 * "Every identity preserved, none destroyed"
 *============================================================================*/

#define USERS_MAX   6

static struct {
    struct {
        char    name[32];
        char    role[16];
        int     active;
    } users[USERS_MAX];
    int         count;
    int         selected;
    struct widget_list user_list;
    char        list_names[USERS_MAX][32];
} usr;

static void usr_init_state(void)
{
    memset(&usr, 0, sizeof(usr));
    str_copy(usr.users[0].name, "admin", 32);
    str_copy(usr.users[0].role, "Root", 16);
    usr.users[0].active = 1;

    str_copy(usr.users[1].name, "phantom", 32);
    str_copy(usr.users[1].role, "User", 16);
    usr.users[1].active = 1;

    str_copy(usr.users[2].name, "governor", 32);
    str_copy(usr.users[2].role, "System", 16);
    usr.users[2].active = 1;

    str_copy(usr.users[3].name, "guest", 32);
    str_copy(usr.users[3].role, "Guest", 16);
    usr.users[3].active = 0;

    usr.count = 4;
    usr.selected = -1;
}

static void users_paint(struct wm_window *win)
{
    int cw = wm_content_width(win);
    int y = 8;

    widget_label(win, 8, y, "USER MANAGEMENT", COLOR_HIGHLIGHT);
    y += 20;
    widget_label(win, 8, y, "Identities are never deleted", COLOR_TEXT_DIM);
    y += 28;

    /* User list */
    usr.user_list.count = 0;
    for (int i = 0; i < usr.count; i++) {
        str_copy(usr.list_names[i], usr.users[i].name, 32);
        usr.user_list.items[i] = usr.list_names[i];
        usr.user_list.count++;
    }
    usr.user_list.x = 8;
    usr.user_list.y = y;
    usr.user_list.w = 120;
    usr.user_list.h = 100;
    usr.user_list.selected = usr.selected;
    widget_list_draw(win, &usr.user_list);

    /* Detail panel */
    int dx = 140;
    if (usr.selected >= 0 && usr.selected < usr.count) {
        widget_label(win, dx, y, "Details:", COLOR_TEXT_DIM);
        y += 20;
        widget_label(win, dx, y, "Name:", COLOR_TEXT_DIM);
        widget_label(win, dx + 56, y, usr.users[usr.selected].name, COLOR_TEXT);
        y += 18;
        widget_label(win, dx, y, "Role:", COLOR_TEXT_DIM);
        widget_label(win, dx + 56, y, usr.users[usr.selected].role,
                     COLOR_ICON_PURPLE);
        y += 18;
        widget_label(win, dx, y, "State:", COLOR_TEXT_DIM);
        widget_label(win, dx + 56, y,
                     usr.users[usr.selected].active ? "Active" : "Suspended",
                     usr.users[usr.selected].active ? COLOR_GREEN_ACTIVE : COLOR_ICON_ORANGE);
        y += 24;

        /* Permissions */
        widget_label(win, dx, y, "Auth Methods:", COLOR_TEXT_DIM);
        y += 18;
        widget_label(win, dx + 8, y, "* DNAuth", COLOR_TEXT);
        y += 14;
        widget_label(win, dx + 8, y, "* MusiKey", COLOR_TEXT);
        y += 14;
        widget_label(win, dx + 8, y, "* LifeAuth", COLOR_TEXT);
        y += 14;
        widget_label(win, dx + 8, y, "* BioSense", COLOR_TEXT);
    } else {
        widget_label(win, dx, y + 30, "Select a user", COLOR_TEXT_DIM);
    }

    /* Footer */
    y = wm_content_height(win) - 40;
    (void)cw;
    widget_label(win, 8, y, "Users can be suspended,", COLOR_TEXT_DIM);
    y += 16;
    widget_label(win, 8, y, "never deleted.", COLOR_TEXT_DIM);
}

static void users_click(struct wm_window *win, int x, int y, int btn)
{
    (void)win; (void)btn;
    int sel = widget_list_click(&usr.user_list, x, y);
    if (sel >= 0 && sel < usr.count)
        usr.selected = sel;
}

/*============================================================================
 * PhantomPods - Container Management
 * "Isolated environments, eternally preserved"
 *============================================================================*/

#define PODS_MAX    5

static struct {
    struct {
        char    name[32];
        int     cpu_pct;
        int     mem_pct;
        int     running;
    } pods[PODS_MAX];
    int         count;
} pod;

static void pod_init_state(void)
{
    memset(&pod, 0, sizeof(pod));
    str_copy(pod.pods[0].name, "phantom-core", 32);
    pod.pods[0].cpu_pct = 12; pod.pods[0].mem_pct = 34; pod.pods[0].running = 1;

    str_copy(pod.pods[1].name, "governor-svc", 32);
    pod.pods[1].cpu_pct = 5; pod.pods[1].mem_pct = 18; pod.pods[1].running = 1;

    str_copy(pod.pods[2].name, "geofs-worker", 32);
    pod.pods[2].cpu_pct = 8; pod.pods[2].mem_pct = 22; pod.pods[2].running = 1;

    str_copy(pod.pods[3].name, "auth-service", 32);
    pod.pods[3].cpu_pct = 3; pod.pods[3].mem_pct = 12; pod.pods[3].running = 1;

    str_copy(pod.pods[4].name, "sandbox-test", 32);
    pod.pods[4].cpu_pct = 0; pod.pods[4].mem_pct = 8; pod.pods[4].running = 0;

    pod.count = 5;
}

static void pods_paint(struct wm_window *win)
{
    int cw = wm_content_width(win);
    int y = 8;

    widget_label(win, 8, y, "PHANTOMPODS", COLOR_HIGHLIGHT);
    y += 20;
    widget_label(win, 8, y, "Container Management", COLOR_TEXT_DIM);
    y += 28;

    /* Header row */
    widget_label(win, 8, y, "NAME", COLOR_TEXT_DIM);
    widget_label(win, 140, y, "CPU", COLOR_TEXT_DIM);
    widget_label(win, 200, y, "MEM", COLOR_TEXT_DIM);
    widget_label(win, 260, y, "STATE", COLOR_TEXT_DIM);
    y += 18;
    gfx_draw_hline(win->x + WM_BORDER_WIDTH + 8, win->y + WM_TITLE_HEIGHT + y,
                   cw - 16, COLOR_PANEL_BORDER);
    y += 6;

    /* Pod rows */
    for (int i = 0; i < pod.count; i++) {
        widget_label(win, 8, y, pod.pods[i].name,
                     pod.pods[i].running ? COLOR_TEXT : COLOR_TEXT_DIM);

        /* CPU bar */
        widget_progress(win, 140, y + 2, 50, 10, pod.pods[i].cpu_pct,
                        0xFF3B82F6, 0xFF0D0D1A);

        /* Mem bar */
        widget_progress(win, 200, y + 2, 50, 10, pod.pods[i].mem_pct,
                        0xFF8B5CF6, 0xFF0D0D1A);

        /* Status */
        widget_label(win, 260, y,
                     pod.pods[i].running ? "RUN" : "STOP",
                     pod.pods[i].running ? COLOR_GREEN_ACTIVE : COLOR_TEXT_DIM);
        y += 22;
    }

    y += 10;

    /* Summary */
    char buf[32];
    int running = 0;
    for (int i = 0; i < pod.count; i++)
        if (pod.pods[i].running) running++;
    int pos = 0;
    buf[pos++] = '0' + (char)running;
    buf[pos++] = '/';
    buf[pos++] = '0' + (char)pod.count;
    buf[pos++] = ' '; buf[pos++] = 'r'; buf[pos++] = 'u';
    buf[pos++] = 'n'; buf[pos++] = 'n'; buf[pos++] = 'i';
    buf[pos++] = 'n'; buf[pos++] = 'g';
    buf[pos] = '\0';
    widget_label(win, 8, y, "Pods:", COLOR_TEXT_DIM);
    widget_label(win, 56, y, buf, COLOR_GREEN_ACTIVE);
    y += 24;
    (void)cw;

    widget_label(win, 8, y, "Pods are suspended, not", COLOR_TEXT_DIM);
    y += 16;
    widget_label(win, 8, y, "destroyed. All state is", COLOR_TEXT_DIM);
    y += 16;
    widget_label(win, 8, y, "preserved in GeoFS.", COLOR_TEXT_DIM);
}

/*============================================================================
 * Backup - Backup Utility
 * "Every version preserved, time travel enabled"
 *============================================================================*/

#define BACKUP_HISTORY_MAX  5

static struct {
    struct {
        char    name[32];
        char    date[16];
        int     size_kb;
        int     complete;
    } history[BACKUP_HISTORY_MAX];
    int         count;
    int         backing_up;
    int         backup_progress;
    int         backup_tick;
    struct widget_button backup_btn;
    struct widget_button restore_btn;
} bkp;

static void bkp_init_state(void)
{
    memset(&bkp, 0, sizeof(bkp));
    str_copy(bkp.history[0].name, "System Snapshot", 32);
    str_copy(bkp.history[0].date, "Layer 42", 16);
    bkp.history[0].size_kb = 512;
    bkp.history[0].complete = 1;

    str_copy(bkp.history[1].name, "User Data", 32);
    str_copy(bkp.history[1].date, "Layer 38", 16);
    bkp.history[1].size_kb = 256;
    bkp.history[1].complete = 1;

    str_copy(bkp.history[2].name, "Config Backup", 32);
    str_copy(bkp.history[2].date, "Layer 35", 16);
    bkp.history[2].size_kb = 64;
    bkp.history[2].complete = 1;

    bkp.count = 3;
}

static void backup_paint(struct wm_window *win)
{
    int cw = wm_content_width(win);
    int y = 8;

    widget_label(win, 8, y, "BACKUP MANAGER", COLOR_HIGHLIGHT);
    y += 20;
    widget_label(win, 8, y, "GeoFS Geological Snapshots", COLOR_TEXT_DIM);
    y += 28;

    /* Backup history */
    widget_label(win, 8, y, "SNAPSHOT HISTORY", COLOR_TEXT_DIM);
    y += 18;
    gfx_draw_hline(win->x + WM_BORDER_WIDTH + 8, win->y + WM_TITLE_HEIGHT + y,
                   cw - 16, COLOR_PANEL_BORDER);
    y += 6;

    for (int i = 0; i < bkp.count; i++) {
        widget_label(win, 8, y, bkp.history[i].name, COLOR_TEXT);
        widget_label(win, 160, y, bkp.history[i].date, COLOR_TEXT_DIM);
        y += 16;

        char buf[16];
        int pos = 0;
        int kb = bkp.history[i].size_kb;
        if (kb >= 100) buf[pos++] = '0' + (char)(kb / 100);
        if (kb >= 10) buf[pos++] = '0' + (char)((kb / 10) % 10);
        buf[pos++] = '0' + (char)(kb % 10);
        buf[pos++] = ' '; buf[pos++] = 'K'; buf[pos++] = 'B';
        buf[pos] = '\0';
        widget_label(win, 16, y, buf, COLOR_TEXT_DIM);
        widget_label(win, 160, y,
                     bkp.history[i].complete ? "Complete" : "Partial",
                     bkp.history[i].complete ? COLOR_GREEN_ACTIVE : COLOR_ICON_ORANGE);
        y += 22;
    }

    /* Backup progress */
    if (bkp.backing_up) {
        y += 4;
        widget_label(win, 8, y, "Creating snapshot...", 0xFF3B82F6);
        y += 18;
        widget_progress(win, 8, y, cw - 16, 14, bkp.backup_progress,
                        0xFF3B82F6, 0xFF0D0D1A);
        y += 22;
    } else {
        y += 12;
    }

    /* Buttons */
    bkp.backup_btn = (struct widget_button){8, y, 120, 28,
        "New Snapshot", COLOR_BUTTON_PRIMARY, COLOR_WHITE};
    widget_button_draw(win, &bkp.backup_btn);

    bkp.restore_btn = (struct widget_button){136, y, 110, 28,
        "Time Travel", 0xFF8B5CF6, COLOR_WHITE};
    widget_button_draw(win, &bkp.restore_btn);

    y += 40;
    widget_label(win, 8, y, "All snapshots preserved", COLOR_TEXT_DIM);
    y += 16;
    widget_label(win, 8, y, "in geological layers.", COLOR_TEXT_DIM);
    (void)cw;
}

static void backup_click(struct wm_window *win, int x, int y, int btn)
{
    (void)win; (void)btn;
    if (widget_button_hit(&bkp.backup_btn, x, y) && !bkp.backing_up) {
        bkp.backing_up = 1;
        bkp.backup_progress = 0;
        bkp.backup_tick = 0;
    } else if (widget_button_hit(&bkp.restore_btn, x, y)) {
        /* Time travel - conceptual in kernel */
    }
}

/*============================================================================
 * Desktop Lab - Theme & Layout Experimentation
 * "Experiment freely, every design preserved"
 *============================================================================*/

static struct {
    int         theme;          /* 0=Dark, 1=Midnight, 2=Ocean */
    int         accent_idx;     /* 0-3 accent color selection */
    int         font_scale;     /* 1-3 */
    struct widget_button theme_btn;
    struct widget_button accent_btn;
    struct widget_button scale_btn;
    struct widget_button reset_btn;
} lab;

static const char *lab_themes[] = { "Dark", "Midnight", "Ocean" };
static const char *lab_accents[] = { "Red", "Blue", "Green", "Purple" };
static const uint32_t lab_accent_colors[] = { 0xFFE94560, 0xFF3B82F6, 0xFF22C55E, 0xFF8B5CF6 };

static void lab_init_state(void)
{
    memset(&lab, 0, sizeof(lab));
    lab.font_scale = 1;
}

static void desktoplab_paint(struct wm_window *win)
{
    int cw = wm_content_width(win);
    int y = 8;

    widget_label(win, 8, y, "DESKTOP LAB", COLOR_HIGHLIGHT);
    y += 20;
    widget_label(win, 8, y, "Theme & Layout Playground", COLOR_TEXT_DIM);
    y += 28;

    /* Theme selector */
    widget_label(win, 8, y, "Theme:", COLOR_TEXT_DIM);
    lab.theme_btn = (struct widget_button){80, y - 2, 100, 24,
        lab_themes[lab.theme], COLOR_BUTTON, COLOR_WHITE};
    widget_button_draw(win, &lab.theme_btn);
    y += 30;

    /* Accent color */
    widget_label(win, 8, y, "Accent:", COLOR_TEXT_DIM);
    lab.accent_btn = (struct widget_button){80, y - 2, 100, 24,
        lab_accents[lab.accent_idx], lab_accent_colors[lab.accent_idx], COLOR_WHITE};
    widget_button_draw(win, &lab.accent_btn);
    y += 30;

    /* Font scale */
    widget_label(win, 8, y, "Scale:", COLOR_TEXT_DIM);
    char sbuf[4];
    sbuf[0] = '0' + (char)lab.font_scale;
    sbuf[1] = 'x'; sbuf[2] = '\0';
    lab.scale_btn = (struct widget_button){80, y - 2, 100, 24,
        sbuf, COLOR_BUTTON, COLOR_WHITE};
    widget_button_draw(win, &lab.scale_btn);
    y += 36;

    /* Preview area */
    widget_label(win, 8, y, "PREVIEW", COLOR_TEXT_DIM);
    y += 18;

    /* Color swatches */
    uint32_t accent = lab_accent_colors[lab.accent_idx];
    gfx_fill_rounded_rect(
        win->x + WM_BORDER_WIDTH + 8,
        win->y + WM_TITLE_HEIGHT + y,
        cw - 16, 50, 6, 0xFF111827);

    /* Sample UI elements */
    gfx_fill_rounded_rect(
        win->x + WM_BORDER_WIDTH + 16,
        win->y + WM_TITLE_HEIGHT + y + 8,
        80, 14, 3, accent);
    widget_label(win, 20, y + 10, "Button", COLOR_WHITE);

    widget_progress(win, 16, y + 30, cw - 40, 10,
                    65, accent, 0xFF0D0D1A);
    y += 58;

    /* Scaled text demo */
    if (lab.font_scale > 1) {
        gfx_draw_text_scaled(
            win->x + WM_BORDER_WIDTH + 8,
            win->y + WM_TITLE_HEIGHT + y,
            "PhantomOS", accent, 0xFF0A0E1A, lab.font_scale);
        y += 16 * lab.font_scale + 4;
    } else {
        widget_label(win, 8, y, "PhantomOS", accent);
        y += 20;
    }

    /* Reset */
    lab.reset_btn = (struct widget_button){8, y, 80, 24,
        "Reset", COLOR_BUTTON, COLOR_TEXT};
    widget_button_draw(win, &lab.reset_btn);
    (void)cw;
}

static void desktoplab_click(struct wm_window *win, int x, int y, int btn)
{
    (void)win; (void)btn;
    if (widget_button_hit(&lab.theme_btn, x, y)) {
        lab.theme = (lab.theme + 1) % 3;
    } else if (widget_button_hit(&lab.accent_btn, x, y)) {
        lab.accent_idx = (lab.accent_idx + 1) % 4;
    } else if (widget_button_hit(&lab.scale_btn, x, y)) {
        lab.font_scale = (lab.font_scale % 3) + 1;
    } else if (widget_button_hit(&lab.reset_btn, x, y)) {
        lab.theme = 0;
        lab.accent_idx = 0;
        lab.font_scale = 1;
    }
}

/*============================================================================
 * ArtOS - Digital Art Studio
 * "Every stroke preserved in geological layers"
 *============================================================================*/

/* --- Math utilities (integer only, no FPU) --- */

/* Integer square root via Newton's method */
static int isqrt(int n)
{
    if (n <= 0) return 0;
    int x = n;
    int y = (x + 1) / 2;
    while (y < x) { x = y; y = (x + n / x) / 2; }
    return x;
}

/* Sine lookup table: isin_table[d] = sin(d°) * 1024, for d=0..90 */
static const int isin_table[91] = {
       0,   18,   36,   54,   71,   89,  107,  125,  143,  160,
     178,  195,  213,  230,  248,  265,  282,  299,  316,  333,
     350,  367,  384,  400,  416,  433,  449,  465,  481,  496,
     512,  527,  543,  558,  573,  588,  602,  617,  631,  645,
     659,  673,  687,  700,  714,  727,  740,  752,  765,  777,
     789,  801,  813,  824,  836,  847,  857,  868,  878,  888,
     898,  908,  917,  926,  935,  944,  953,  961,  969,  977,
     985,  992,  999, 1005, 1012, 1018, 1024, 1023, 1022, 1021,
    1020, 1019, 1018, 1016, 1014, 1012, 1009, 1007, 1004, 1001,
    1024
};

/* Fixed-point sine: returns sin(deg) * 1024, range [-1024, 1024] */
static int isin(int deg)
{
    deg = deg % 360;
    if (deg < 0) deg += 360;
    if (deg <= 90) return isin_table[deg];
    if (deg <= 180) return isin_table[180 - deg];
    if (deg <= 270) return -isin_table[deg - 180];
    return -isin_table[360 - deg];
}

/* Fixed-point cosine: returns cos(deg) * 1024 */
static int icos(int deg) { return isin(deg + 90); }

/* Grid snap helper */
static void artos_snap(int *cx, int *cy)
{
    if (!art.grid_snap) return;
    int g = art.grid_size;
    *cx = ((*cx + g / 2) / g) * g;
    *cy = ((*cy + g / 2) / g) * g;
    if (*cx >= ARTOS_CANVAS_W) *cx = ARTOS_CANVAS_W - 1;
    if (*cy >= ARTOS_CANVAS_H) *cy = ARTOS_CANVAS_H - 1;
}

/* --- HSV color conversion (integer math only) --- */
static uint32_t hsv_to_rgb(int h, int s, int v)
{
    if (s == 0) return 0xFF000000 | ((uint32_t)v << 16) | ((uint32_t)v << 8) | (uint32_t)v;
    while (h < 0) h += 360;
    while (h >= 360) h -= 360;
    int sector = h / 60;
    int rem = h - sector * 60;
    int p = (v * (255 - s)) / 255;
    int q = (v * (255 * 60 - s * rem)) / (255 * 60);
    int t = (v * (255 * 60 - s * (60 - rem))) / (255 * 60);
    int r, g, b;
    switch (sector) {
        case 0: r=v; g=t; b=p; break;
        case 1: r=q; g=v; b=p; break;
        case 2: r=p; g=v; b=t; break;
        case 3: r=p; g=q; b=v; break;
        case 4: r=t; g=p; b=v; break;
        default: r=v; g=p; b=q; break;
    }
    if (r<0) r=0; if (r>255) r=255;
    if (g<0) g=0; if (g>255) g=255;
    if (b<0) b=0; if (b>255) b=255;
    return 0xFF000000 | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static void rgb_to_hsv(uint32_t color, int *oh, int *os, int *ov)
{
    int r = (color >> 16) & 0xFF, g = (color >> 8) & 0xFF, b = color & 0xFF;
    int mx = r; if (g > mx) mx = g; if (b > mx) mx = b;
    int mn = r; if (g < mn) mn = g; if (b < mn) mn = b;
    int delta = mx - mn;
    *ov = mx;
    *os = (mx == 0) ? 0 : (delta * 255) / mx;
    if (delta == 0) { *oh = 0; return; }
    if (mx == r) { *oh = 60 * (g - b) / delta; if (*oh < 0) *oh += 360; }
    else if (mx == g) { *oh = 120 + 60 * (b - r) / delta; }
    else { *oh = 240 + 60 * (r - g) / delta; }
}

/* --- Layer compositing --- */
static void artos_composite_layers(void)
{
    int npix = ARTOS_CANVAS_W * ARTOS_CANVAS_H;
    for (int i = 0; i < npix; i++) art.composite[i] = 0xFFFFFFFF; /* white bg */
    for (int l = 0; l < art.layer_count; l++) {
        if (!art.layers[l].visible) continue;
        uint8_t lop = art.layers[l].opacity;
        for (int i = 0; i < npix; i++) {
            uint32_t src = art.layers[l].pixels[i];
            uint8_t sa = (src >> 24) & 0xFF;
            int ea = (sa * lop) / 255;
            if (ea >= 255) art.composite[i] = src | 0xFF000000;
            else if (ea > 0) art.composite[i] = gfx_alpha_blend(src | 0xFF000000, art.composite[i], (uint8_t)ea);
        }
    }
}

/* --- Canvas access (operates on active layer) --- */
static void artos_canvas_set(int cx, int cy, uint32_t color)
{
    if (cx >= 0 && cx < ARTOS_CANVAS_W && cy >= 0 && cy < ARTOS_CANVAS_H)
        art.layers[art.active_layer].pixels[cy * ARTOS_CANVAS_W + cx] = color;
}

static void artos_canvas_set_opacity(int cx, int cy, uint32_t color, int opacity)
{
    if (cx < 0 || cx >= ARTOS_CANVAS_W || cy < 0 || cy >= ARTOS_CANVAS_H) return;
    if (opacity >= 255) {
        art.layers[art.active_layer].pixels[cy * ARTOS_CANVAS_W + cx] = color;
    } else if (opacity > 0) {
        uint32_t ex = art.layers[art.active_layer].pixels[cy * ARTOS_CANVAS_W + cx];
        art.layers[art.active_layer].pixels[cy * ARTOS_CANVAS_W + cx] =
            gfx_alpha_blend(color, ex, (uint8_t)opacity);
    }
}

static uint32_t artos_canvas_get(int cx, int cy)
{
    if (cx >= 0 && cx < ARTOS_CANVAS_W && cy >= 0 && cy < ARTOS_CANVAS_H)
        return art.layers[art.active_layer].pixels[cy * ARTOS_CANVAS_W + cx];
    return 0;
}

/* --- Undo (operates on active layer only) --- */
static void artos_undo_push(void)
{
    memcpy(art.undo[art.undo_pos], art.layers[art.active_layer].pixels,
           sizeof(uint32_t) * ARTOS_CANVAS_W * ARTOS_CANVAS_H);
    art.undo_pos = (art.undo_pos + 1) % ARTOS_MAX_UNDO;
    if (art.undo_count < ARTOS_MAX_UNDO) art.undo_count++;
}

static void artos_undo(void)
{
    if (art.undo_count <= 0) return;
    art.undo_pos = (art.undo_pos - 1 + ARTOS_MAX_UNDO) % ARTOS_MAX_UNDO;
    art.undo_count--;
    memcpy(art.layers[art.active_layer].pixels, art.undo[art.undo_pos],
           sizeof(uint32_t) * ARTOS_CANVAS_W * ARTOS_CANVAS_H);
}

static void artos_switch_layer(int n)
{
    if (n < 0 || n >= art.layer_count || n == art.active_layer) return;
    art.active_layer = n;
    art.undo_count = 0;
    art.undo_pos = 0;
}

static void artos_flatten_layers(void)
{
    artos_composite_layers();
    memcpy(art.layers[0].pixels, art.composite,
           sizeof(uint32_t) * ARTOS_CANVAS_W * ARTOS_CANVAS_H);
    art.layers[0].visible = 1;
    art.layers[0].opacity = 255;
    art.layer_count = 1;
    art.active_layer = 0;
    art.undo_count = 0;
    art.undo_pos = 0;
}

/* --- Drawing primitives --- */
static void artos_plot(int cx, int cy, uint32_t color, int size)
{
    int r = size / 2;
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++) {
            if (art.brush_opacity >= 255)
                artos_canvas_set(cx + dx, cy + dy, color);
            else
                artos_canvas_set_opacity(cx + dx, cy + dy, color, art.brush_opacity);
        }
}

/* Bresenham line on canvas */
static void artos_line(int x0, int y0, int x1, int y1, uint32_t color, int size)
{
    int dx = x1 - x0;
    int dy = y1 - y0;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;

    while (1) {
        artos_plot(x0, y0, color, size);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx) { err += dx; y0 += sy; }
    }
}

/* Rectangle outline on canvas */
static void artos_rect(int x0, int y0, int x1, int y1, uint32_t color)
{
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    for (int x = x0; x <= x1; x++) {
        artos_canvas_set(x, y0, color);
        artos_canvas_set(x, y1, color);
    }
    for (int y = y0; y <= y1; y++) {
        artos_canvas_set(x0, y, color);
        artos_canvas_set(x1, y, color);
    }
}

/* Filled rectangle on canvas */
static void artos_fill_rect(int x0, int y0, int x1, int y1, uint32_t color)
{
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            artos_canvas_set(x, y, color);
}

/* Ellipse outline (midpoint algorithm) on canvas */
static void artos_ellipse(int cx, int cy, int rx, int ry, uint32_t color)
{
    if (rx < 0) rx = -rx;
    if (ry < 0) ry = -ry;
    if (rx == 0 || ry == 0) { artos_line(cx - rx, cy - ry, cx + rx, cy + ry, color, 1); return; }

    int x = 0, y = ry;
    long rx2 = (long)rx * rx, ry2 = (long)ry * ry;
    long px = 0, py = 2 * rx2 * y;
    long p = ry2 - rx2 * ry + rx2 / 4;

    while (px < py) {
        artos_canvas_set(cx + x, cy + y, color);
        artos_canvas_set(cx - x, cy + y, color);
        artos_canvas_set(cx + x, cy - y, color);
        artos_canvas_set(cx - x, cy - y, color);
        x++;
        px += 2 * ry2;
        if (p < 0) {
            p += ry2 + px;
        } else {
            y--;
            py -= 2 * rx2;
            p += ry2 + px - py;
        }
    }

    p = ry2 * (long)(x * 2 + 1) * (x * 2 + 1) / 4 + rx2 * ((long)y - 1) * (y - 1) - rx2 * ry2;
    while (y >= 0) {
        artos_canvas_set(cx + x, cy + y, color);
        artos_canvas_set(cx - x, cy + y, color);
        artos_canvas_set(cx + x, cy - y, color);
        artos_canvas_set(cx - x, cy - y, color);
        y--;
        py -= 2 * rx2;
        if (p > 0) {
            p += rx2 - py;
        } else {
            x++;
            px += 2 * ry2;
            p += rx2 - py + px;
        }
    }
}

/*============================================================================
 * AI Art Generator - Keyword Parser & Procedural Generation
 *============================================================================*/

/* AI pattern types */
#define AI_PATTERN_SOLID     0
#define AI_PATTERN_GRADIENT  1
#define AI_PATTERN_CIRCLES   2
#define AI_PATTERN_SQUARES   3
#define AI_PATTERN_LINES     4
#define AI_PATTERN_DOTS      5
#define AI_PATTERN_WAVES     6

/* AI direction flags */
#define AI_DIR_VERTICAL      0
#define AI_DIR_HORIZONTAL    1

/* Parsed AI prompt result */
struct ai_keywords {
    uint32_t base_color;     /* Primary color */
    uint32_t accent_color;   /* Secondary color for gradients */
    int      pattern_type;   /* AI_PATTERN_* */
    int      density;        /* Number of shapes (5=few, 20=many) */
    int      direction;      /* AI_DIR_* */
    int      brightness;     /* 0=dark, 1=normal, 2=bright */
};

/* Simple string comparison helper */
static int ai_strstr(const char *haystack, const char *needle)
{
    int i, j;
    for (i = 0; haystack[i]; i++) {
        for (j = 0; needle[j] && haystack[i + j] == needle[j]; j++);
        if (!needle[j]) return 1;
    }
    return 0;
}

/* Parse AI prompt keywords */
static struct ai_keywords parse_ai_keywords(const char *prompt)
{
    struct ai_keywords kw;

    /* Defaults */
    kw.base_color = 0xFF808080;     /* Gray */
    kw.accent_color = 0xFFC0C0C0;   /* Light gray */
    kw.pattern_type = AI_PATTERN_SOLID;
    kw.density = 10;
    kw.direction = AI_DIR_VERTICAL;
    kw.brightness = 1;

    /* Color keywords */
    if (ai_strstr(prompt, "sunset")) {
        kw.base_color = 0xFFFF6B00;      /* Orange */
        kw.accent_color = 0xFFFF0000;    /* Red */
    } else if (ai_strstr(prompt, "ocean") || ai_strstr(prompt, "sea") || ai_strstr(prompt, "water")) {
        kw.base_color = 0xFF0080FF;      /* Blue */
        kw.accent_color = 0xFF00FFFF;    /* Cyan */
    } else if (ai_strstr(prompt, "forest") || ai_strstr(prompt, "tree") || ai_strstr(prompt, "green")) {
        kw.base_color = 0xFF00A000;      /* Green */
        kw.accent_color = 0xFF80FF80;    /* Light green */
    } else if (ai_strstr(prompt, "night") || ai_strstr(prompt, "dark")) {
        kw.base_color = 0xFF000040;      /* Dark blue */
        kw.accent_color = 0xFF8080FF;    /* Light blue */
    } else if (ai_strstr(prompt, "desert") || ai_strstr(prompt, "sand")) {
        kw.base_color = 0xFFE0C040;      /* Yellow-brown */
        kw.accent_color = 0xFFFFFF80;    /* Light yellow */
    } else if (ai_strstr(prompt, "fire") || ai_strstr(prompt, "flame")) {
        kw.base_color = 0xFFFF0000;      /* Red */
        kw.accent_color = 0xFFFFFF00;    /* Yellow */
    } else if (ai_strstr(prompt, "sky") || ai_strstr(prompt, "cloud")) {
        kw.base_color = 0xFF87CEEB;      /* Sky blue */
        kw.accent_color = 0xFFFFFFFF;    /* White */
    }

    /* Pattern keywords */
    if (ai_strstr(prompt, "gradient")) {
        kw.pattern_type = AI_PATTERN_GRADIENT;
    } else if (ai_strstr(prompt, "circle")) {
        kw.pattern_type = AI_PATTERN_CIRCLES;
    } else if (ai_strstr(prompt, "square") || ai_strstr(prompt, "rect") || ai_strstr(prompt, "box")) {
        kw.pattern_type = AI_PATTERN_SQUARES;
    } else if (ai_strstr(prompt, "line") || ai_strstr(prompt, "stripe")) {
        kw.pattern_type = AI_PATTERN_LINES;
    } else if (ai_strstr(prompt, "dot") || ai_strstr(prompt, "spot")) {
        kw.pattern_type = AI_PATTERN_DOTS;
    } else if (ai_strstr(prompt, "wave") || ai_strstr(prompt, "sine")) {
        kw.pattern_type = AI_PATTERN_WAVES;
    }

    /* Density modifiers */
    if (ai_strstr(prompt, "many") || ai_strstr(prompt, "lots")) {
        kw.density = 30;
    } else if (ai_strstr(prompt, "few") || ai_strstr(prompt, "sparse")) {
        kw.density = 5;
    }

    /* Direction modifiers */
    if (ai_strstr(prompt, "horizontal")) {
        kw.direction = AI_DIR_HORIZONTAL;
    } else if (ai_strstr(prompt, "vertical")) {
        kw.direction = AI_DIR_VERTICAL;
    }

    /* Brightness modifiers */
    if (ai_strstr(prompt, "bright") || ai_strstr(prompt, "light")) {
        kw.brightness = 2;
        /* Brighten colors */
        uint8_t r = ((kw.base_color >> 16) & 0xFF);
        uint8_t g = ((kw.base_color >> 8) & 0xFF);
        uint8_t b = (kw.base_color & 0xFF);
        r = (r > 200) ? 255 : r + 55;
        g = (g > 200) ? 255 : g + 55;
        b = (b > 200) ? 255 : b + 55;
        kw.base_color = 0xFF000000 | (r << 16) | (g << 8) | b;
    } else if (ai_strstr(prompt, "dark") || ai_strstr(prompt, "dim")) {
        kw.brightness = 0;
        /* Darken colors */
        uint8_t r = ((kw.base_color >> 16) & 0xFF) / 2;
        uint8_t g = ((kw.base_color >> 8) & 0xFF) / 2;
        uint8_t b = (kw.base_color & 0xFF) / 2;
        kw.base_color = 0xFF000000 | (r << 16) | (g << 8) | b;
    }

    return kw;
}

/* PRNG for AI art generation (Linear Congruential Generator) */
static uint32_t ai_rand(void)
{
    art.ai_rand_seed = art.ai_rand_seed * 1103515245 + 12345;
    return (art.ai_rand_seed >> 16) & 0x7FFF;
}

/* Vertical gradient fill on canvas */
static void artos_fill_gradient_v(uint32_t top_color, uint32_t bottom_color)
{
    uint8_t r1 = (top_color >> 16) & 0xFF;
    uint8_t g1 = (top_color >> 8) & 0xFF;
    uint8_t b1 = top_color & 0xFF;
    uint8_t r2 = (bottom_color >> 16) & 0xFF;
    uint8_t g2 = (bottom_color >> 8) & 0xFF;
    uint8_t b2 = bottom_color & 0xFF;

    for (int y = 0; y < ARTOS_CANVAS_H; y++) {
        uint8_t r = r1 + ((r2 - r1) * y) / ARTOS_CANVAS_H;
        uint8_t g = g1 + ((g2 - g1) * y) / ARTOS_CANVAS_H;
        uint8_t b = b1 + ((b2 - b1) * y) / ARTOS_CANVAS_H;
        uint32_t color = 0xFF000000 | (r << 16) | (g << 8) | b;
        for (int x = 0; x < ARTOS_CANVAS_W; x++) {
            artos_canvas_set(x, y, color);
        }
    }
}

/* Generate AI art from text prompt */
static void generate_ai_art(void)
{
    if (art.ai_prompt[0] == '\0') return;  /* Empty prompt */

    /* Push current canvas to undo stack */
    artos_undo_push();

    /* Seed PRNG with simple hash of prompt */
    art.ai_rand_seed = 12345;
    for (int i = 0; art.ai_prompt[i]; i++) {
        art.ai_rand_seed = art.ai_rand_seed * 31 + (uint32_t)art.ai_prompt[i];
    }

    /* Parse keywords */
    struct ai_keywords kw = parse_ai_keywords(art.ai_prompt);

    /* Apply background */
    if (kw.pattern_type == AI_PATTERN_GRADIENT) {
        if (kw.direction == AI_DIR_VERTICAL) {
            artos_fill_gradient_v(kw.base_color, kw.accent_color);
        } else {
            /* Horizontal gradient - rotate logic */
            for (int x = 0; x < ARTOS_CANVAS_W; x++) {
                uint8_t r1 = (kw.base_color >> 16) & 0xFF;
                uint8_t g1 = (kw.base_color >> 8) & 0xFF;
                uint8_t b1 = kw.base_color & 0xFF;
                uint8_t r2 = (kw.accent_color >> 16) & 0xFF;
                uint8_t g2 = (kw.accent_color >> 8) & 0xFF;
                uint8_t b2 = kw.accent_color & 0xFF;
                uint8_t r = r1 + ((r2 - r1) * x) / ARTOS_CANVAS_W;
                uint8_t g = g1 + ((g2 - g1) * x) / ARTOS_CANVAS_W;
                uint8_t b = b1 + ((b2 - b1) * x) / ARTOS_CANVAS_W;
                uint32_t color = 0xFF000000 | (r << 16) | (g << 8) | b;
                for (int y = 0; y < ARTOS_CANVAS_H; y++) {
                    artos_canvas_set(x, y, color);
                }
            }
        }
    } else {
        /* Solid background */
        artos_fill_rect(0, 0, ARTOS_CANVAS_W - 1, ARTOS_CANVAS_H - 1, kw.base_color);
    }

    /* Apply pattern overlay */
    switch (kw.pattern_type) {
        case AI_PATTERN_CIRCLES:
            for (int i = 0; i < kw.density; i++) {
                int cx = ai_rand() % ARTOS_CANVAS_W;
                int cy = ai_rand() % ARTOS_CANVAS_H;
                int r = 5 + (ai_rand() % 16);  /* Radius 5-20 */
                artos_ellipse(cx, cy, r, r, kw.accent_color);
            }
            break;

        case AI_PATTERN_SQUARES:
            for (int i = 0; i < kw.density; i++) {
                int x = ai_rand() % ARTOS_CANVAS_W;
                int y = ai_rand() % ARTOS_CANVAS_H;
                int size = 5 + (ai_rand() % 16);
                int filled = (ai_rand() % 2);
                if (filled) {
                    artos_fill_rect(x, y, x + size, y + size, kw.accent_color);
                } else {
                    artos_rect(x, y, x + size, y + size, kw.accent_color);
                }
            }
            break;

        case AI_PATTERN_LINES:
            for (int i = 0; i < kw.density; i++) {
                int x0, y0, x1, y1;
                if (kw.direction == AI_DIR_VERTICAL) {
                    x0 = ai_rand() % ARTOS_CANVAS_W;
                    x1 = x0;
                    y0 = 0;
                    y1 = ARTOS_CANVAS_H - 1;
                } else {
                    y0 = ai_rand() % ARTOS_CANVAS_H;
                    y1 = y0;
                    x0 = 0;
                    x1 = ARTOS_CANVAS_W - 1;
                }
                artos_line(x0, y0, x1, y1, kw.accent_color, 1);
            }
            break;

        case AI_PATTERN_DOTS:
            for (int i = 0; i < kw.density * 2; i++) {
                int x = ai_rand() % ARTOS_CANVAS_W;
                int y = ai_rand() % ARTOS_CANVAS_H;
                int size = 1 + (ai_rand() % 3);
                artos_plot(x, y, kw.accent_color, size);
            }
            break;

        case AI_PATTERN_WAVES:
            /* Sine wave approximation using line segments */
            for (int w = 0; w < kw.density / 5 + 1; w++) {
                int amplitude = 10 + (ai_rand() % 20);
                int y_offset = ai_rand() % ARTOS_CANVAS_H;
                for (int x = 0; x < ARTOS_CANVAS_W - 1; x++) {
                    /* Approximate sine with simple periodic function */
                    int y1 = y_offset + (amplitude * ((x * 360 / ARTOS_CANVAS_W) % 180 < 90 ? 1 : -1) * (x % (ARTOS_CANVAS_W / 4))) / (ARTOS_CANVAS_W / 4);
                    int y2 = y_offset + (amplitude * (((x + 1) * 360 / ARTOS_CANVAS_W) % 180 < 90 ? 1 : -1) * ((x + 1) % (ARTOS_CANVAS_W / 4))) / (ARTOS_CANVAS_W / 4);
                    artos_line(x, y1, x + 1, y2, kw.accent_color, 1);
                }
            }
            break;

        default:
            /* No pattern overlay for solid/gradient */
            break;
    }

    art.modified = 1;
}

/*============================================================================
 * DrawNet - Shared Canvas Collaboration (File-Based)
 *============================================================================*/

/* Helper: simple strcat (not in freestanding lib) */
static char *dn_strcat(char *dest, const char *src)
{
    char *d = dest;
    while (*d) d++;
    while ((*d++ = *src++));
    return dest;
}

/* Helper: convert uint64_t to string */
static void dn_u64_to_str(uint64_t val, char *buf)
{
    if (val == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    char tmp[32];
    int i = 0;
    while (val > 0) {
        tmp[i++] = '0' + (val % 10);
        val /= 10;
    }
    int j = 0;
    while (i > 0) {
        buf[j++] = tmp[--i];
    }
    buf[j] = '\0';
}

/* Helper: convert uint32_t to hex string (8 chars) */
static void dn_u32_to_hex(uint32_t val, char *buf)
{
    const char *hex = "0123456789abcdef";
    for (int i = 7; i >= 0; i--) {
        buf[i] = hex[val & 0xF];
        val >>= 4;
    }
    buf[8] = '\0';
}

/* Helper: convert int to string */
static void dn_int_to_str(int val, char *buf)
{
    if (val < 0) {
        buf[0] = '-';
        dn_u64_to_str((uint64_t)(-val), buf + 1);
    } else {
        dn_u64_to_str((uint64_t)val, buf);
    }
}

/* Helper: build path "/drawnet/SESSION_ID/filename" */
static void dn_build_path(const char *session_id, const char *filename, char *out)
{
    strcpy(out, "/drawnet/");
    dn_strcat(out, session_id);
    if (filename) {
        dn_strcat(out, "/");
        dn_strcat(out, filename);
    }
}

/* Initialize DrawNet session */
static void drawnet_init_session(const char *session_id)
{
    /* Copy session ID */
    strncpy(art.drawnet_session_id, session_id, 15);
    art.drawnet_session_id[15] = '\0';

    /* Create session directory */
    char path[64];

    /* Initialize state */
    art.drawnet_enabled = 1;
    art.drawnet_peer_count = 0;
    art.drawnet_stroke_seq = 0;
    art.drawnet_last_sync_ms = timer_get_ms();

    /* Note: File-based sync requires GeoFS volume access */
    /* For now, session runs in local-only mode */

    /* Create empty strokes log */
}

/* Stop DrawNet session */
static void drawnet_stop_session(void)
{
    art.drawnet_enabled = 0;
    art.drawnet_peer_count = 0;
}

/* Sync peer cursors and presence */
static void drawnet_sync_peers(void)
{
    /* Simplified: no file sync, local-only mode */
    (void)art.drawnet_enabled;
}

/* Push stroke to shared log */
static void drawnet_push_stroke(int tool, int x1, int y1, int x2, int y2,
                                 uint32_t color, int size)
{
    /* Simplified: no file sync, local-only mode */
    (void)tool; (void)x1; (void)y1; (void)x2; (void)y2; (void)color; (void)size;
}

/* Pull and replay strokes from shared log */
static void drawnet_pull_strokes(void)
{
    /* Simplified: no file sync, local-only mode */
    (void)art.drawnet_enabled;
}

/* Paint peer cursors overlay */
static void drawnet_paint_cursors(struct wm_window *win)
{
    if (!art.drawnet_enabled) return;

    int ox = win->x + WM_BORDER_WIDTH;
    int oy = win->y + WM_TITLE_HEIGHT;

    for (int i = 0; i < art.drawnet_peer_count; i++) {
        int cx = art.drawnet_peers[i].cursor_x;
        int cy = art.drawnet_peers[i].cursor_y;
        uint32_t col = art.drawnet_peers[i].color;

        /* Convert canvas coords to screen coords */
        int sx = ox + art.canvas_ox + cx * art.pixel_scale;
        int sy = oy + art.canvas_oy + cy * art.pixel_scale;

        /* Draw small cross */
        gfx_draw_hline(sx - 4, sy, 9, col);
        gfx_draw_vline(sx, sy - 4, 9, col);

        /* Draw peer name */
        font_draw_string((uint32_t)(sx + 6), (uint32_t)(sy + 6),
                         art.drawnet_peers[i].name, col, 0xFF000000);
    }
}

/* Flood fill on canvas (stack-based, bounded) */
#define ARTOS_FILL_STACK 16384

static void artos_flood_fill(int sx, int sy, uint32_t new_color)
{
    if (sx < 0 || sx >= ARTOS_CANVAS_W || sy < 0 || sy >= ARTOS_CANVAS_H) return;
    uint32_t old_color = artos_canvas_get(sx, sy);
    if (old_color == new_color) return;

    /* Simple stack-based flood fill */
    static int stack_x[ARTOS_FILL_STACK];
    static int stack_y[ARTOS_FILL_STACK];
    int sp = 0;
    stack_x[sp] = sx;
    stack_y[sp] = sy;
    sp++;

    while (sp > 0) {
        sp--;
        int x = stack_x[sp];
        int y = stack_y[sp];
        if (x < 0 || x >= ARTOS_CANVAS_W || y < 0 || y >= ARTOS_CANVAS_H) continue;
        if (artos_canvas_get(x, y) != old_color) continue;
        artos_canvas_set(x, y, new_color);

        if (sp + 4 <= ARTOS_FILL_STACK) {
            stack_x[sp] = x + 1; stack_y[sp] = y; sp++;
            stack_x[sp] = x - 1; stack_y[sp] = y; sp++;
            stack_x[sp] = x; stack_y[sp] = y + 1; sp++;
            stack_x[sp] = x; stack_y[sp] = y - 1; sp++;
        }
    }
}

/* Initialize ArtOS state */
static void artos_init_state(void)
{
    memset(&art, 0, sizeof(art));
    art.tool = ARTOS_TOOL_PENCIL;
    art.fg_color = 0xFF000000;
    art.bg_color = 0xFFFFFFFF;
    art.brush_size = 1;
    art.brush_opacity = 255;
    art.zoom = 1;
    art.drawing = 0;
    art.toolbar_h = ARTOS_TOOLBAR_H;
    art.palette_h = ARTOS_PALETTE_H;
    art.modified = 0;
    art.hsv_h = 0; art.hsv_s = 0; art.hsv_v = 0;
    art.star_sides = 5;
    art.grid_size = 8;
    art.mirror_mode = 0;
    art.grid_snap = 0;
    art.bezier_count = 0;
    art.clone_src_set = 0;

    /* Init layer 0 */
    art.layer_count = 1;
    art.active_layer = 0;
    art.layers[0].visible = 1;
    art.layers[0].opacity = 255;
    strcpy(art.layers[0].name, "Layer 1");
    for (int i = 0; i < ARTOS_CANVAS_W * ARTOS_CANVAS_H; i++)
        art.layers[0].pixels[i] = 0xFFFFFFFF;

    /* Pre-init other layers */
    for (int l = 1; l < ARTOS_MAX_LAYERS; l++) {
        art.layers[l].visible = 1;
        art.layers[l].opacity = 255;
        char nm[8]; nm[0] = 'L'; nm[1] = 'a'; nm[2] = 'y'; nm[3] = 'e';
        nm[4] = 'r'; nm[5] = ' '; nm[6] = '1' + (char)l; nm[7] = '\0';
        strcpy(art.layers[l].name, nm);
    }

    artos_composite_layers();
}

/* Convert content-area coords to canvas coords; returns 1 if on canvas */
static int artos_screen_to_canvas(int x, int y, int *cx, int *cy)
{
    if (art.pixel_scale <= 0) return 0;
    int rx = x - art.canvas_ox;
    int ry = y - art.canvas_oy;
    *cx = rx / art.pixel_scale + art.scroll_x;
    *cy = ry / art.pixel_scale + art.scroll_y;
    return (*cx >= 0 && *cx < ARTOS_CANVAS_W && *cy >= 0 && *cy < ARTOS_CANVAS_H);
}

/* --- New tool functions --- */

/* Spray tool: scatter random dots in radius */
static void artos_spray(int cx, int cy, uint32_t color, int radius)
{
    if (radius < 1) radius = 3;
    int count = radius * 2;
    for (int i = 0; i < count; i++) {
        int dx = (int)(ai_rand() % (uint32_t)(2 * radius + 1)) - radius;
        int dy = (int)(ai_rand() % (uint32_t)(2 * radius + 1)) - radius;
        if (dx * dx + dy * dy <= radius * radius)
            artos_canvas_set_opacity(cx + dx, cy + dy, color, art.brush_opacity);
    }
}

/* Text tool: render one 8x16 font glyph onto canvas */
extern const uint8_t font_data[95][16];
static void artos_render_text_char(int cx, int cy, char ch, uint32_t color)
{
    if (ch < 32 || ch > 126) return;
    const uint8_t *glyph = font_data[ch - 32];
    for (int row = 0; row < 16 && (cy + row) < ARTOS_CANVAS_H; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8 && (cx + col) < ARTOS_CANVAS_W; col++) {
            if (bits & (0x80 >> col))
                artos_canvas_set(cx + col, cy + row, color);
        }
    }
}

/* Polygon tool: close and draw all edges */
static void artos_close_polygon(void)
{
    if (art.poly_count < 2) { art.poly_count = 0; return; }
    artos_undo_push();
    for (int i = 0; i < art.poly_count - 1; i++)
        artos_line(art.poly_verts[i][0], art.poly_verts[i][1],
                   art.poly_verts[i+1][0], art.poly_verts[i+1][1],
                   art.fg_color, art.brush_size);
    artos_line(art.poly_verts[art.poly_count-1][0], art.poly_verts[art.poly_count-1][1],
               art.poly_verts[0][0], art.poly_verts[0][1],
               art.fg_color, art.brush_size);
    art.poly_count = 0;
    art.modified = 1;
}

/* --- New drawing tools (ArtOS v3) --- */

/* Rounded rectangle: 4 quarter-circle arcs at corners + straight edges */
static void artos_round_rect(int x0, int y0, int x1, int y1, uint32_t color, int r)
{
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    int w = x1 - x0, h = y1 - y0;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    if (r < 1) r = 1;
    /* Straight edges */
    artos_line(x0 + r, y0, x1 - r, y0, color, 1);
    artos_line(x0 + r, y1, x1 - r, y1, color, 1);
    artos_line(x0, y0 + r, x0, y1 - r, color, 1);
    artos_line(x1, y0 + r, x1, y1 - r, color, 1);
    /* Quarter arcs using midpoint circle algorithm */
    int cx, cy, px = 0, py = r, d = 1 - r;
    while (px <= py) {
        /* Top-left arc */
        cx = x0 + r; cy = y0 + r;
        artos_canvas_set(cx - px, cy - py, color);
        artos_canvas_set(cx - py, cy - px, color);
        /* Top-right arc */
        cx = x1 - r; cy = y0 + r;
        artos_canvas_set(cx + px, cy - py, color);
        artos_canvas_set(cx + py, cy - px, color);
        /* Bottom-left arc */
        cx = x0 + r; cy = y1 - r;
        artos_canvas_set(cx - px, cy + py, color);
        artos_canvas_set(cx - py, cy + px, color);
        /* Bottom-right arc */
        cx = x1 - r; cy = y1 - r;
        artos_canvas_set(cx + px, cy + py, color);
        artos_canvas_set(cx + py, cy + px, color);
        px++;
        if (d < 0) { d += 2 * px + 1; }
        else { py--; d += 2 * (px - py) + 1; }
    }
}

/* Star: regular star polygon with n outer and n inner vertices */
static void artos_star(int cx, int cy, int radius, int sides, uint32_t color)
{
    if (sides < 3) sides = 3;
    if (sides > 8) sides = 8;
    if (radius < 2) return;
    int inner = radius * 2 / 5; /* inner radius ~40% of outer */
    int total = sides * 2;
    int prevx = cx + (icos(270) * radius) / 1024;
    int prevy = cy + (isin(270) * radius) / 1024;
    for (int i = 1; i <= total; i++) {
        int angle = 270 + (i * 360) / total;
        int r = (i % 2 == 0) ? radius : inner;
        int nx = cx + (icos(angle) * r) / 1024;
        int ny = cy + (isin(angle) * r) / 1024;
        artos_line(prevx, prevy, nx, ny, color, 1);
        prevx = nx; prevy = ny;
    }
}

/* Arrow: line with triangular arrowhead */
static void artos_arrow(int x0, int y0, int x1, int y1, uint32_t color, int size)
{
    artos_line(x0, y0, x1, y1, color, size);
    /* Arrowhead: triangle at endpoint */
    int dx = x1 - x0, dy = y1 - y0;
    int len = isqrt(dx * dx + dy * dy);
    if (len < 1) return;
    int head = 8 + size * 2;
    /* Unit vector * 1024 scaled */
    int ux = (dx * 1024) / len, uy = (dy * 1024) / len;
    /* Perpendicular */
    int px = -uy, py = ux;
    /* Base of arrowhead */
    int bx = x1 - (ux * head) / 1024;
    int by = y1 - (uy * head) / 1024;
    int hw = head / 2;
    int ax = bx + (px * hw) / 1024, ay = by + (py * hw) / 1024;
    int bx2 = bx - (px * hw) / 1024, by2 = by - (py * hw) / 1024;
    artos_line(x1, y1, ax, ay, color, 1);
    artos_line(x1, y1, bx2, by2, color, 1);
    artos_line(ax, ay, bx2, by2, color, 1);
}

/* Cubic bezier via De Casteljau, 64 line segments, integer math */
static void artos_bezier(int x0, int y0, int x1, int y1,
                          int x2, int y2, int x3, int y3,
                          uint32_t color, int size)
{
    int steps = 64;
    int prevx = x0, prevy = y0;
    for (int i = 1; i <= steps; i++) {
        /* t = i/steps, use fixed-point t*1024 */
        int t = (i * 1024) / steps;
        int t1 = 1024 - t;
        /* De Casteljau: B(t) = (1-t)^3*P0 + 3*(1-t)^2*t*P1 + 3*(1-t)*t^2*P2 + t^3*P3 */
        /* Compute in stages to avoid overflow: max intermediate ~1024^3 = 1G, risky */
        /* Use nested lerp instead for safety */
        int ax = (t1 * x0 + t * x1) / 1024;
        int ay = (t1 * y0 + t * y1) / 1024;
        int bxx = (t1 * x1 + t * x2) / 1024;
        int by = (t1 * y1 + t * y2) / 1024;
        int cxx = (t1 * x2 + t * x3) / 1024;
        int cy = (t1 * y2 + t * y3) / 1024;
        int dx = (t1 * ax + t * bxx) / 1024;
        int dy = (t1 * ay + t * by) / 1024;
        int ex = (t1 * bxx + t * cxx) / 1024;
        int ey = (t1 * by + t * cy) / 1024;
        int fx = (t1 * dx + t * ex) / 1024;
        int fy = (t1 * dy + t * ey) / 1024;
        artos_line(prevx, prevy, fx, fy, color, size);
        prevx = fx; prevy = fy;
    }
}

/* Linear gradient fill: fills entire canvas with FG→BG gradient along the drag direction */
static void artos_grad_fill(int x0, int y0, int x1, int y1)
{
    int dx = x1 - x0, dy = y1 - y0;
    int len2 = dx * dx + dy * dy;
    if (len2 < 1) len2 = 1;
    uint8_t r0 = (art.fg_color >> 16) & 0xFF, g0 = (art.fg_color >> 8) & 0xFF, b0 = art.fg_color & 0xFF;
    uint8_t r1 = (art.bg_color >> 16) & 0xFF, g1 = (art.bg_color >> 8) & 0xFF, b1 = art.bg_color & 0xFF;
    for (int py = 0; py < ARTOS_CANVAS_H; py++) {
        for (int px = 0; px < ARTOS_CANVAS_W; px++) {
            /* Project (px-x0, py-y0) onto (dx, dy), get t in 0..256 */
            int dot = (px - x0) * dx + (py - y0) * dy;
            int t = (dot * 256) / len2;
            if (t < 0) t = 0; if (t > 256) t = 256;
            int r = r0 + ((r1 - r0) * t) / 256;
            int g = g0 + ((g1 - g0) * t) / 256;
            int b = b0 + ((b1 - b0) * t) / 256;
            artos_canvas_set(px, py, 0xFF000000 | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b);
        }
    }
}

/* Dither flood fill: like flood_fill but uses 2x2 ordered dither of FG+BG */
static void artos_dither_fill(int sx, int sy)
{
    uint32_t target = artos_canvas_get(sx, sy);
    if (target == art.fg_color || target == art.bg_color) return;
    /* Simple iterative flood fill with dither */
    static int stack_x2[4096], stack_y2[4096];
    int sp = 0;
    stack_x2[sp] = sx; stack_y2[sp] = sy; sp++;
    while (sp > 0) {
        sp--;
        int cx = stack_x2[sp], cy = stack_y2[sp];
        if (cx < 0 || cx >= ARTOS_CANVAS_W || cy < 0 || cy >= ARTOS_CANVAS_H) continue;
        if (artos_canvas_get(cx, cy) != target) continue;
        /* 2x2 ordered dither: (0,0)=FG, (1,0)=BG, (0,1)=BG, (1,1)=FG */
        uint32_t dc = ((cx + cy) % 2 == 0) ? art.fg_color : art.bg_color;
        artos_canvas_set(cx, cy, dc);
        if (sp < 4092) {
            stack_x2[sp] = cx + 1; stack_y2[sp] = cy; sp++;
            stack_x2[sp] = cx - 1; stack_y2[sp] = cy; sp++;
            stack_x2[sp] = cx; stack_y2[sp] = cy + 1; sp++;
            stack_x2[sp] = cx; stack_y2[sp] = cy - 1; sp++;
        }
    }
}

/* Calligraphy: angled rectangular nib at 45 degrees */
static void artos_callig_plot(int cx, int cy, uint32_t color, int size)
{
    /* Draw a diagonal line from (cx-size/2, cy+size/2) to (cx+size/2, cy-size/2) */
    int half = size / 2;
    for (int i = -half; i <= half; i++)
        artos_canvas_set(cx + i, cy - i, color);
}

static void artos_callig_line(int x0, int y0, int x1, int y1, uint32_t color, int size)
{
    /* Bresenham between points, applying callig_plot at each step */
    int dx = x1 - x0, dy = y1 - y0;
    int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    int sx = (dx > 0) ? 1 : -1, sy = (dy > 0) ? 1 : -1;
    int err = adx - ady, cx = x0, cy = y0;
    while (1) {
        artos_callig_plot(cx, cy, color, size);
        if (cx == x1 && cy == y1) break;
        int e2 = err * 2;
        if (e2 > -ady) { err -= ady; cx += sx; }
        if (e2 < adx) { err += adx; cy += sy; }
    }
}

/* Soft brush: circle with opacity falloff from center */
static void artos_soft_plot(int cx, int cy, uint32_t color, int size)
{
    int r = size;
    if (r < 1) r = 1;
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            int d2 = dx * dx + dy * dy;
            if (d2 > r * r) continue;
            /* Opacity falloff: 255 at center, 0 at edge */
            int dist = isqrt(d2);
            int alpha = 255 - (dist * 255) / r;
            if (alpha < 0) alpha = 0;
            /* Apply brush_opacity scaling */
            alpha = (alpha * art.brush_opacity) / 255;
            artos_canvas_set_opacity(cx + dx, cy + dy, color, alpha);
        }
    }
}

/* Pattern brush: 4x4 checkerboard of FG and BG */
static void artos_pattern_plot(int cx, int cy, int size)
{
    int r = size / 2;
    if (r < 1) r = 1;
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            if (dx * dx + dy * dy > r * r) continue;
            /* 4x4 checkerboard pattern */
            int px = cx + dx, py = cy + dy;
            uint32_t color = (((px / 4) + (py / 4)) % 2 == 0) ? art.fg_color : art.bg_color;
            artos_canvas_set(px, py, color);
        }
    }
}

/* Clone stamp: paint pixels from source offset */
static void artos_clone_plot(int cx, int cy, int size)
{
    int r = size / 2;
    if (r < 1) r = 1;
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            if (dx * dx + dy * dy > r * r) continue;
            int sx = cx + dx + art.clone_off_x;
            int sy = cy + dy + art.clone_off_y;
            if (sx >= 0 && sx < ARTOS_CANVAS_W && sy >= 0 && sy < ARTOS_CANVAS_H) {
                uint32_t src = artos_canvas_get(sx, sy);
                artos_canvas_set(cx + dx, cy + dy, src);
            }
        }
    }
}

/* Smudge: pick up pixel buffer at click point */
static void artos_smudge_pickup(int cx, int cy, int size)
{
    int r = size / 2;
    if (r < 1) r = 1;
    int diam = r * 2 + 1;
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            int idx = (dy + r) * diam + (dx + r);
            if (idx < 441)
                art.smudge_buf[idx] = artos_canvas_get(cx + dx, cy + dy);
        }
    }
}

/* Smudge: blend buffer with destination pixels */
static void artos_smudge_apply(int cx, int cy, int size)
{
    int r = size / 2;
    if (r < 1) r = 1;
    int diam = r * 2 + 1;
    int strength = 160; /* ~62% carry-forward */
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            if (dx * dx + dy * dy > r * r) continue;
            int idx = (dy + r) * diam + (dx + r);
            if (idx >= 441) continue;
            uint32_t dst = artos_canvas_get(cx + dx, cy + dy);
            uint32_t src = art.smudge_buf[idx];
            uint32_t blended = gfx_alpha_blend(src, dst, (uint8_t)strength);
            artos_canvas_set(cx + dx, cy + dy, blended);
            art.smudge_buf[idx] = blended;
        }
    }
}

/* --- Edit operations --- */

static void artos_flip_h(void)
{
    artos_undo_push();
    uint32_t *px = art.layers[art.active_layer].pixels;
    int x0 = 0, y0 = 0, x1 = ARTOS_CANVAS_W, y1 = ARTOS_CANVAS_H;
    if (art.sel_active) { x0 = art.sel_x1; y0 = art.sel_y1; x1 = art.sel_x2; y1 = art.sel_y2; }
    int w = x1 - x0;
    for (int y = y0; y < y1; y++)
        for (int i = 0; i < w / 2; i++) {
            int a = y * ARTOS_CANVAS_W + x0 + i;
            int b = y * ARTOS_CANVAS_W + x1 - 1 - i;
            uint32_t tmp = px[a]; px[a] = px[b]; px[b] = tmp;
        }
}

static void artos_flip_v(void)
{
    artos_undo_push();
    uint32_t *px = art.layers[art.active_layer].pixels;
    int x0 = 0, y0 = 0, x1 = ARTOS_CANVAS_W, y1 = ARTOS_CANVAS_H;
    if (art.sel_active) { x0 = art.sel_x1; y0 = art.sel_y1; x1 = art.sel_x2; y1 = art.sel_y2; }
    int h = y1 - y0;
    for (int i = 0; i < h / 2; i++)
        for (int x = x0; x < x1; x++) {
            int a = (y0 + i) * ARTOS_CANVAS_W + x;
            int b = (y1 - 1 - i) * ARTOS_CANVAS_W + x;
            uint32_t tmp = px[a]; px[a] = px[b]; px[b] = tmp;
        }
}

static void artos_invert(void)
{
    artos_undo_push();
    uint32_t *px = art.layers[art.active_layer].pixels;
    int x0 = 0, y0 = 0, x1 = ARTOS_CANVAS_W, y1 = ARTOS_CANVAS_H;
    if (art.sel_active) { x0 = art.sel_x1; y0 = art.sel_y1; x1 = art.sel_x2; y1 = art.sel_y2; }
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
            px[y * ARTOS_CANVAS_W + x] ^= 0x00FFFFFF;
}

static void artos_brightness(int delta)
{
    artos_undo_push();
    uint32_t *px = art.layers[art.active_layer].pixels;
    int x0 = 0, y0 = 0, x1 = ARTOS_CANVAS_W, y1 = ARTOS_CANVAS_H;
    if (art.sel_active) { x0 = art.sel_x1; y0 = art.sel_y1; x1 = art.sel_x2; y1 = art.sel_y2; }
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            int idx = y * ARTOS_CANVAS_W + x;
            uint32_t c = px[idx];
            int r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
            r += delta; if (r < 0) r = 0; if (r > 255) r = 255;
            g += delta; if (g < 0) g = 0; if (g > 255) g = 255;
            b += delta; if (b < 0) b = 0; if (b > 255) b = 255;
            px[idx] = (c & 0xFF000000) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
}

static void artos_posterize(void)
{
    artos_undo_push();
    uint32_t *px = art.layers[art.active_layer].pixels;
    int x0 = 0, y0 = 0, x1 = ARTOS_CANVAS_W, y1 = ARTOS_CANVAS_H;
    if (art.sel_active) { x0 = art.sel_x1; y0 = art.sel_y1; x1 = art.sel_x2; y1 = art.sel_y2; }
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            int idx = y * ARTOS_CANVAS_W + x;
            uint32_t c = px[idx];
            int r = ((c >> 16) & 0xFF) / 85 * 85;
            int g = ((c >> 8) & 0xFF) / 85 * 85;
            int b = (c & 0xFF) / 85 * 85;
            px[idx] = (c & 0xFF000000) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
}

/* Paint callback — ArtOS v3 */
static void artos_paint(struct wm_window *win)
{
    int cw = wm_content_width(win);
    int ch = wm_content_height(win);
    int ox = win->x;
    int oy = win->y + WM_TITLE_HEIGHT;
    uint32_t tb_bg = 0xFF111827;

    /* Composite layers for display */
    artos_composite_layers();

    /* === TOOLBAR (7 rows, 132px) === */
    fb_fill_rect((uint32_t)ox, (uint32_t)oy, (uint32_t)cw, (uint32_t)ARTOS_TOOLBAR_H, tb_bg);

    /* Row A (y=2): tools 0-5 */
    for (int i = 0; i < 6; i++) {
        uint32_t bg = (i == art.tool) ? COLOR_HIGHLIGHT : COLOR_BUTTON_PRIMARY;
        int bx = 4 + i * (ARTOS_BTN_W + ARTOS_BTN_GAP);
        fb_fill_rect((uint32_t)(ox + bx), (uint32_t)(oy + 2), (uint32_t)ARTOS_BTN_W, (uint32_t)ARTOS_BTN_H, bg);
        font_draw_string((uint32_t)(ox + bx + 2), (uint32_t)(oy + 5), artos_tool_names[i], COLOR_WHITE, bg);
    }
    /* Row B (y=22): tools 6-11 */
    for (int i = 6; i < 12; i++) {
        uint32_t bg = (i == art.tool) ? COLOR_HIGHLIGHT : COLOR_BUTTON_PRIMARY;
        int bx = 4 + (i - 6) * (ARTOS_BTN_W + ARTOS_BTN_GAP);
        fb_fill_rect((uint32_t)(ox + bx), (uint32_t)(oy + 22), (uint32_t)ARTOS_BTN_W, (uint32_t)ARTOS_BTN_H, bg);
        font_draw_string((uint32_t)(ox + bx + 2), (uint32_t)(oy + 25), artos_tool_names[i], COLOR_WHITE, bg);
    }
    /* Row C (y=42): tools 12-17 */
    for (int i = 12; i < 18; i++) {
        uint32_t bg = (i == art.tool) ? COLOR_HIGHLIGHT : COLOR_BUTTON_PRIMARY;
        int bx = 4 + (i - 12) * (ARTOS_BTN_W + ARTOS_BTN_GAP);
        fb_fill_rect((uint32_t)(ox + bx), (uint32_t)(oy + 42), (uint32_t)ARTOS_BTN_W, (uint32_t)ARTOS_BTN_H, bg);
        font_draw_string((uint32_t)(ox + bx + 2), (uint32_t)(oy + 45), artos_tool_names[i], COLOR_WHITE, bg);
    }
    /* Row D (y=62): tools 18-23 */
    for (int i = 18; i < ARTOS_TOOL_COUNT; i++) {
        uint32_t bg = (i == art.tool) ? COLOR_HIGHLIGHT : COLOR_BUTTON_PRIMARY;
        int bx = 4 + (i - 18) * (ARTOS_BTN_W + ARTOS_BTN_GAP);
        fb_fill_rect((uint32_t)(ox + bx), (uint32_t)(oy + 62), (uint32_t)ARTOS_BTN_W, (uint32_t)ARTOS_BTN_H, bg);
        font_draw_string((uint32_t)(ox + bx + 2), (uint32_t)(oy + 65), artos_tool_names[i], COLOR_WHITE, bg);
    }

    /* Row E (y=82): Undo Clear | Size | Opac | FG BG Swap | Zoom | Mir Grd */
    int ry = 82;
    fb_fill_rect((uint32_t)(ox + 4), (uint32_t)(oy + ry), 36, ARTOS_BTN_H, COLOR_BUTTON_PRIMARY);
    font_draw_string((uint32_t)(ox + 6), (uint32_t)(oy + ry + 3), "Undo", COLOR_WHITE, COLOR_BUTTON_PRIMARY);
    fb_fill_rect((uint32_t)(ox + 44), (uint32_t)(oy + ry), 36, ARTOS_BTN_H, COLOR_BUTTON_PRIMARY);
    font_draw_string((uint32_t)(ox + 46), (uint32_t)(oy + ry + 3), "Clr", COLOR_WHITE, COLOR_BUTTON_PRIMARY);

    /* Size -[n]+ */
    font_draw_string((uint32_t)(ox + 86), (uint32_t)(oy + ry + 3), "Sz", COLOR_TEXT_DIM, tb_bg);
    fb_fill_rect((uint32_t)(ox + 104), (uint32_t)(oy + ry), 16, ARTOS_BTN_H, COLOR_BUTTON_PRIMARY);
    font_draw_char((uint32_t)(ox + 108), (uint32_t)(oy + ry + 3), '-', COLOR_WHITE, COLOR_BUTTON_PRIMARY);
    { char sc[4]; sc[0] = '0' + (char)(art.brush_size % 10);
      if (art.brush_size >= 10) { sc[0] = '1'; sc[1] = '0'; sc[2] = '\0'; }
      else { sc[1] = '\0'; }
      font_draw_string((uint32_t)(ox + 122), (uint32_t)(oy + ry + 3), sc, COLOR_TEXT, tb_bg); }
    fb_fill_rect((uint32_t)(ox + 136), (uint32_t)(oy + ry), 16, ARTOS_BTN_H, COLOR_BUTTON_PRIMARY);
    font_draw_char((uint32_t)(ox + 140), (uint32_t)(oy + ry + 3), '+', COLOR_WHITE, COLOR_BUTTON_PRIMARY);

    /* Opacity -[n]+ */
    font_draw_string((uint32_t)(ox + 158), (uint32_t)(oy + ry + 3), "Op", COLOR_TEXT_DIM, tb_bg);
    fb_fill_rect((uint32_t)(ox + 176), (uint32_t)(oy + ry), 16, ARTOS_BTN_H, COLOR_BUTTON_PRIMARY);
    font_draw_char((uint32_t)(ox + 180), (uint32_t)(oy + ry + 3), '-', COLOR_WHITE, COLOR_BUTTON_PRIMARY);
    { char ob[4]; int ov = (art.brush_opacity * 100) / 255;
      ob[0] = '0' + (char)(ov / 10); ob[1] = '0' + (char)(ov % 10); ob[2] = '\0';
      font_draw_string((uint32_t)(ox + 194), (uint32_t)(oy + ry + 3), ob, COLOR_TEXT, tb_bg); }
    fb_fill_rect((uint32_t)(ox + 212), (uint32_t)(oy + ry), 16, ARTOS_BTN_H, COLOR_BUTTON_PRIMARY);
    font_draw_char((uint32_t)(ox + 216), (uint32_t)(oy + ry + 3), '+', COLOR_WHITE, COLOR_BUTTON_PRIMARY);

    /* FG/BG swatches */
    fb_fill_rect((uint32_t)(ox + 236), (uint32_t)(oy + ry), 18, ARTOS_BTN_H, art.fg_color);
    fb_draw_rect((uint32_t)(ox + 236), (uint32_t)(oy + ry), 18, ARTOS_BTN_H, COLOR_TEXT_DIM);
    fb_fill_rect((uint32_t)(ox + 258), (uint32_t)(oy + ry), 18, ARTOS_BTN_H, art.bg_color);
    fb_draw_rect((uint32_t)(ox + 258), (uint32_t)(oy + ry), 18, ARTOS_BTN_H, COLOR_TEXT_DIM);
    fb_fill_rect((uint32_t)(ox + 280), (uint32_t)(oy + ry), 28, ARTOS_BTN_H, COLOR_BUTTON_PRIMARY);
    font_draw_string((uint32_t)(ox + 282), (uint32_t)(oy + ry + 3), "Swp", COLOR_WHITE, COLOR_BUTTON_PRIMARY);

    /* Zoom -[n]+ */
    font_draw_string((uint32_t)(ox + 316), (uint32_t)(oy + ry + 3), "Zm", COLOR_TEXT_DIM, tb_bg);
    fb_fill_rect((uint32_t)(ox + 334), (uint32_t)(oy + ry), 16, ARTOS_BTN_H, COLOR_BUTTON_PRIMARY);
    font_draw_char((uint32_t)(ox + 338), (uint32_t)(oy + ry + 3), '-', COLOR_WHITE, COLOR_BUTTON_PRIMARY);
    { char zc = '0' + (char)art.zoom;
      font_draw_char((uint32_t)(ox + 354), (uint32_t)(oy + ry + 3), zc, COLOR_TEXT, tb_bg); }
    fb_fill_rect((uint32_t)(ox + 364), (uint32_t)(oy + ry), 16, ARTOS_BTN_H, COLOR_BUTTON_PRIMARY);
    font_draw_char((uint32_t)(ox + 368), (uint32_t)(oy + ry + 3), '+', COLOR_WHITE, COLOR_BUTTON_PRIMARY);

    /* Mirror mode toggle */
    { uint32_t mir_bg = art.mirror_mode ? COLOR_HIGHLIGHT : COLOR_BUTTON_PRIMARY;
      fb_fill_rect((uint32_t)(ox + 388), (uint32_t)(oy + ry), 28, ARTOS_BTN_H, mir_bg);
      font_draw_string((uint32_t)(ox + 390), (uint32_t)(oy + ry + 3), "Mir", COLOR_WHITE, mir_bg); }
    /* Grid snap toggle */
    { uint32_t grd_bg = art.grid_snap ? COLOR_HIGHLIGHT : COLOR_BUTTON_PRIMARY;
      fb_fill_rect((uint32_t)(ox + 420), (uint32_t)(oy + ry), 28, ARTOS_BTN_H, grd_bg);
      font_draw_string((uint32_t)(ox + 422), (uint32_t)(oy + ry + 3), "Grd", COLOR_WHITE, grd_bg); }

    /* Row F (y=102): AI prompt */
    int ai_y = 102;
    font_draw_string((uint32_t)(ox + 4), (uint32_t)(oy + ai_y + 3), "AI:", COLOR_TEXT_DIM, tb_bg);
    uint32_t pbg = art.ai_input_active ? 0xFF1F2937 : 0xFF0F1419;
    fb_fill_rect((uint32_t)(ox + 26), (uint32_t)(oy + ai_y), 280, ARTOS_BTN_H, pbg);
    fb_draw_rect((uint32_t)(ox + 26), (uint32_t)(oy + ai_y), 280, ARTOS_BTN_H, COLOR_TEXT_DIM);
    if (art.ai_prompt[0])
        font_draw_string((uint32_t)(ox + 30), (uint32_t)(oy + ai_y + 3), art.ai_prompt, COLOR_TEXT, pbg);
    else
        font_draw_string((uint32_t)(ox + 30), (uint32_t)(oy + ai_y + 3), "(prompt...)", COLOR_TEXT_DIM, pbg);
    if (art.ai_input_active)
        gfx_draw_vline(ox + 30 + art.ai_prompt_cursor * 8, oy + ai_y + 2, 14, COLOR_HIGHLIGHT);
    fb_fill_rect((uint32_t)(ox + 312), (uint32_t)(oy + ai_y), 56, ARTOS_BTN_H, COLOR_BUTTON_PRIMARY);
    font_draw_string((uint32_t)(ox + 314), (uint32_t)(oy + ai_y + 3), "Gen", COLOR_WHITE, COLOR_BUTTON_PRIMARY);

    /* Row G (y=120): DrawNet */
    int dn_y = 120;
    font_draw_string((uint32_t)(ox + 4), (uint32_t)(oy + dn_y + 3), "Net:", COLOR_TEXT_DIM, tb_bg);
    uint32_t sbg = art.drawnet_input_active ? 0xFF1F2937 : 0xFF0F1419;
    fb_fill_rect((uint32_t)(ox + 32), (uint32_t)(oy + dn_y), 80, ARTOS_BTN_H, sbg);
    fb_draw_rect((uint32_t)(ox + 32), (uint32_t)(oy + dn_y), 80, ARTOS_BTN_H, COLOR_TEXT_DIM);
    if (art.drawnet_input[0])
        font_draw_string((uint32_t)(ox + 36), (uint32_t)(oy + dn_y + 3), art.drawnet_input, COLOR_TEXT, sbg);
    if (art.drawnet_input_active)
        gfx_draw_vline(ox + 36 + (int)strlen(art.drawnet_input) * 8, oy + dn_y + 2, 14, COLOR_HIGHLIGHT);
    if (art.drawnet_enabled) {
        fb_fill_rect((uint32_t)(ox + 118), (uint32_t)(oy + dn_y), 36, ARTOS_BTN_H, COLOR_HIGHLIGHT);
        font_draw_string((uint32_t)(ox + 120), (uint32_t)(oy + dn_y + 3), "Stop", COLOR_WHITE, COLOR_HIGHLIGHT);
    } else {
        fb_fill_rect((uint32_t)(ox + 118), (uint32_t)(oy + dn_y), 36, ARTOS_BTN_H, COLOR_GREEN_ACTIVE);
        font_draw_string((uint32_t)(ox + 120), (uint32_t)(oy + dn_y + 3), "Go", COLOR_WHITE, COLOR_GREEN_ACTIVE);
    }

    /* Separator */
    gfx_draw_hline(ox, oy + ARTOS_TOOLBAR_H - 1, cw, COLOR_PANEL_BORDER);

    /* === CANVAS AREA + LAYER PANEL === */
    int ca_y = ARTOS_TOOLBAR_H;
    int ca_h = ch - ARTOS_TOOLBAR_H - ARTOS_PALETTE_H;
    int cp_w = cw - ARTOS_LAYER_PANEL_W; /* canvas panel width */

    /* Canvas dark bg */
    fb_fill_rect((uint32_t)ox, (uint32_t)(oy + ca_y), (uint32_t)cp_w, (uint32_t)ca_h, 0xFF202030);

    /* Layer panel bg */
    fb_fill_rect((uint32_t)(ox + cp_w), (uint32_t)(oy + ca_y),
                 (uint32_t)ARTOS_LAYER_PANEL_W, (uint32_t)ca_h, 0xFF0F1218);
    gfx_draw_vline(ox + cp_w, oy + ca_y, ca_h, COLOR_PANEL_BORDER);

    /* Layer panel content */
    font_draw_string((uint32_t)(ox + cp_w + 4), (uint32_t)(oy + ca_y + 4), "Layers", COLOR_TEXT_DIM, 0xFF0F1218);
    for (int l = 0; l < art.layer_count; l++) {
        int ly = ca_y + 22 + l * 24;
        uint32_t lbg = (l == art.active_layer) ? 0xFF1E3A5F : 0xFF0F1218;
        fb_fill_rect((uint32_t)(ox + cp_w + 2), (uint32_t)(oy + ly), (uint32_t)(ARTOS_LAYER_PANEL_W - 4), 22, lbg);
        /* Eye icon (visibility) */
        uint32_t ec = art.layers[l].visible ? COLOR_GREEN_ACTIVE : COLOR_TEXT_DIM;
        fb_fill_rect((uint32_t)(ox + cp_w + 4), (uint32_t)(oy + ly + 6), 8, 8, ec);
        /* Layer name */
        font_draw_string((uint32_t)(ox + cp_w + 16), (uint32_t)(oy + ly + 5),
                         art.layers[l].name, COLOR_TEXT, lbg);
    }

    /* Add layer button */
    if (art.layer_count < ARTOS_MAX_LAYERS) {
        int aby = ca_y + 22 + art.layer_count * 24 + 4;
        fb_fill_rect((uint32_t)(ox + cp_w + 4), (uint32_t)(oy + aby), 52, 16, COLOR_BUTTON_PRIMARY);
        font_draw_string((uint32_t)(ox + cp_w + 8), (uint32_t)(oy + aby + 2), "+Layer", COLOR_WHITE, COLOR_BUTTON_PRIMARY);
    }

    /* Flatten button */
    if (art.layer_count > 1) {
        int fby = ca_y + 22 + ARTOS_MAX_LAYERS * 24 + 8;
        fb_fill_rect((uint32_t)(ox + cp_w + 4), (uint32_t)(oy + fby), 52, 16, COLOR_BUTTON_PRIMARY);
        font_draw_string((uint32_t)(ox + cp_w + 6), (uint32_t)(oy + fby + 2), "Flatten", COLOR_WHITE, COLOR_BUTTON_PRIMARY);
    }

    /* Layer opacity */
    { int loy = ca_y + ca_h - 40;
      font_draw_string((uint32_t)(ox + cp_w + 4), (uint32_t)(oy + loy), "Opac", COLOR_TEXT_DIM, 0xFF0F1218);
      fb_fill_rect((uint32_t)(ox + cp_w + 4), (uint32_t)(oy + loy + 14), 16, 14, COLOR_BUTTON_PRIMARY);
      font_draw_char((uint32_t)(ox + cp_w + 8), (uint32_t)(oy + loy + 16), '-', COLOR_WHITE, COLOR_BUTTON_PRIMARY);
      fb_fill_rect((uint32_t)(ox + cp_w + 40), (uint32_t)(oy + loy + 14), 16, 14, COLOR_BUTTON_PRIMARY);
      font_draw_char((uint32_t)(ox + cp_w + 44), (uint32_t)(oy + loy + 16), '+', COLOR_WHITE, COLOR_BUTTON_PRIMARY);
      char lop[4]; int lov = (art.layers[art.active_layer].opacity * 100) / 255;
      lop[0] = '0' + (char)(lov / 10); lop[1] = '0' + (char)(lov % 10); lop[2] = '\0';
      font_draw_string((uint32_t)(ox + cp_w + 22), (uint32_t)(oy + loy + 16), lop, COLOR_TEXT, 0xFF0F1218);
    }

    /* Calculate zoom/pan for canvas rendering */
    int avail_w = cp_w - ARTOS_MARGIN * 2;
    int avail_h = ca_h - ARTOS_MARGIN * 2;
    art.pixel_scale = art.zoom;

    int vp_cw = avail_w / art.pixel_scale;
    int vp_ch = avail_h / art.pixel_scale;
    if (vp_cw > ARTOS_CANVAS_W) vp_cw = ARTOS_CANVAS_W;
    if (vp_ch > ARTOS_CANVAS_H) vp_ch = ARTOS_CANVAS_H;

    /* Clamp scroll */
    int msx = ARTOS_CANVAS_W - vp_cw; if (msx < 0) msx = 0;
    int msy = ARTOS_CANVAS_H - vp_ch; if (msy < 0) msy = 0;
    if (art.scroll_x > msx) art.scroll_x = msx;
    if (art.scroll_y > msy) art.scroll_y = msy;
    if (art.scroll_x < 0) art.scroll_x = 0;
    if (art.scroll_y < 0) art.scroll_y = 0;

    int disp_w = vp_cw * art.pixel_scale;
    int disp_h = vp_ch * art.pixel_scale;
    int off_x = (disp_w <= avail_w) ? (avail_w - disp_w) / 2 : 0;
    int off_y = (disp_h <= avail_h) ? (avail_h - disp_h) / 2 : 0;
    art.canvas_ox = ARTOS_MARGIN + off_x;
    art.canvas_oy = ca_y + ARTOS_MARGIN + off_y;

    /* Draw composite canvas (zoomed + scrolled) */
    for (int vy = 0; vy < vp_ch; vy++) {
        for (int vx = 0; vx < vp_cw; vx++) {
            int ccx = art.scroll_x + vx;
            int ccy = art.scroll_y + vy;
            uint32_t color = art.composite[ccy * ARTOS_CANVAS_W + ccx];
            int sx = ox + art.canvas_ox + vx * art.pixel_scale;
            int sy = oy + art.canvas_oy + vy * art.pixel_scale;
            fb_fill_rect((uint32_t)sx, (uint32_t)sy,
                         (uint32_t)art.pixel_scale, (uint32_t)art.pixel_scale, color);
        }
    }

    /* Canvas border */
    fb_draw_rect((uint32_t)(ox + art.canvas_ox - 1), (uint32_t)(oy + art.canvas_oy - 1),
                 (uint32_t)(disp_w + 2), (uint32_t)(disp_h + 2), COLOR_TEXT_DIM);

    /* Selection overlay (dashed rect) */
    if (art.sel_active) {
        int sx1 = (art.sel_x1 - art.scroll_x) * art.pixel_scale + art.canvas_ox;
        int sy1 = (art.sel_y1 - art.scroll_y) * art.pixel_scale + art.canvas_oy;
        int sx2 = (art.sel_x2 - art.scroll_x) * art.pixel_scale + art.canvas_ox;
        int sy2 = (art.sel_y2 - art.scroll_y) * art.pixel_scale + art.canvas_oy;
        fb_draw_rect((uint32_t)(ox + sx1), (uint32_t)(oy + sy1),
                     (uint32_t)(sx2 - sx1), (uint32_t)(sy2 - sy1), COLOR_HIGHLIGHT);
    }

    /* Bezier control point markers */
    if (art.tool == ARTOS_TOOL_BEZIER && art.bezier_count > 0) {
        for (int bi = 0; bi < art.bezier_count; bi++) {
            int bpx = (art.bezier_pts[bi][0] - art.scroll_x) * art.pixel_scale + art.canvas_ox;
            int bpy = (art.bezier_pts[bi][1] - art.scroll_y) * art.pixel_scale + art.canvas_oy;
            fb_fill_rect((uint32_t)(ox + bpx - 2), (uint32_t)(oy + bpy - 2), 5, 5, 0x00FF4444);
            fb_draw_rect((uint32_t)(ox + bpx - 3), (uint32_t)(oy + bpy - 3), 7, 7, COLOR_WHITE);
        }
    }

    /* Clone source crosshair */
    if (art.tool == ARTOS_TOOL_CLONE && art.clone_src_set) {
        int csx = (art.clone_src_x - art.scroll_x) * art.pixel_scale + art.canvas_ox;
        int csy = (art.clone_src_y - art.scroll_y) * art.pixel_scale + art.canvas_oy;
        /* Horizontal line */
        for (int ci = -4; ci <= 4; ci++)
            if (ci != 0)
                fb_fill_rect((uint32_t)(ox + csx + ci), (uint32_t)(oy + csy), 1, 1, 0x0000FF00);
        /* Vertical line */
        for (int ci = -4; ci <= 4; ci++)
            if (ci != 0)
                fb_fill_rect((uint32_t)(ox + csx), (uint32_t)(oy + csy + ci), 1, 1, 0x0000FF00);
    }

    /* Grid overlay when snap enabled */
    if (art.grid_snap && art.zoom >= 2) {
        int gs = art.grid_size * art.pixel_scale;
        int gsx = art.canvas_ox - (art.scroll_x % art.grid_size) * art.pixel_scale;
        int gsy = ARTOS_TOOLBAR_H - (art.scroll_y % art.grid_size) * art.pixel_scale;
        int canvas_px_h = ch - ARTOS_TOOLBAR_H - ARTOS_PALETTE_H;
        for (int gx = gsx; gx < art.canvas_ox + ARTOS_CANVAS_W * art.pixel_scale; gx += gs)
            for (int gy = 0; gy < canvas_px_h; gy += 4)
                fb_fill_rect((uint32_t)(ox + gx), (uint32_t)(oy + ARTOS_TOOLBAR_H + gy), 1, 1, 0x40808080);
        for (int gy = gsy; gy < canvas_px_h; gy += gs)
            for (int gx = 0; gx < art.canvas_ox + ARTOS_CANVAS_W * art.pixel_scale - art.canvas_ox; gx += 4)
                fb_fill_rect((uint32_t)(ox + art.canvas_ox + gx), (uint32_t)(oy + ARTOS_TOOLBAR_H + gy), 1, 1, 0x40808080);
    }

    /* Mirror mode center line indicator */
    if (art.mirror_mode) {
        int mcx = (ARTOS_CANVAS_W / 2 - art.scroll_x) * art.pixel_scale + art.canvas_ox;
        int canvas_px_h = ch - ARTOS_TOOLBAR_H - ARTOS_PALETTE_H;
        for (int my = 0; my < canvas_px_h; my += 2)
            fb_fill_rect((uint32_t)(ox + mcx), (uint32_t)(oy + ARTOS_TOOLBAR_H + my), 1, 1, 0x00FF00FF);
    }

    /* Star sides indicator */
    if (art.tool == ARTOS_TOOL_STAR) {
        char stxt[8]; stxt[0] = '0' + (char)art.star_sides; stxt[1] = 'p'; stxt[2] = 't'; stxt[3] = '\0';
        font_draw_string((uint32_t)(ox + cw - 30), (uint32_t)(oy + ARTOS_TOOLBAR_H + 2), stxt, 0x00FFFF00, 0x00333333);
    }

    /* === BOTTOM PALETTE (44px) === */
    int pal_y = ch - ARTOS_PALETTE_H;
    fb_fill_rect((uint32_t)ox, (uint32_t)(oy + pal_y), (uint32_t)cw, (uint32_t)ARTOS_PALETTE_H, tb_bg);
    gfx_draw_hline(ox, oy + pal_y, cw, COLOR_PANEL_BORDER);

    /* 16 quick swatches (left side, 14x14) */
    int ssz = 14, sgap = 2;
    int srow_y = pal_y + 4;
    for (int i = 0; i < ARTOS_PALETTE_COUNT; i++) {
        int sx = 4 + i * (ssz + sgap);
        fb_fill_rect((uint32_t)(ox + sx), (uint32_t)(oy + srow_y), (uint32_t)ssz, (uint32_t)ssz, artos_palette[i]);
        uint32_t bd = (artos_palette[i] == art.fg_color) ? COLOR_HIGHLIGHT : COLOR_TEXT_DIM;
        fb_draw_rect((uint32_t)(ox + sx), (uint32_t)(oy + srow_y), (uint32_t)ssz, (uint32_t)ssz, bd);
    }

    /* HSV Hue bar (right side) */
    int hue_x = 280;
    for (int hx = 0; hx < ARTOS_HUE_BAR_W; hx++) {
        uint32_t hc = hsv_to_rgb(hx * 360 / ARTOS_HUE_BAR_W, 255, 255);
        fb_fill_rect((uint32_t)(ox + hue_x + hx), (uint32_t)(oy + pal_y + 4), 1, (uint32_t)ARTOS_HUE_BAR_H, hc);
    }
    /* Hue marker */
    { int hm = hue_x + art.hsv_h * ARTOS_HUE_BAR_W / 360;
      fb_draw_rect((uint32_t)(ox + hm - 1), (uint32_t)(oy + pal_y + 3), 3, (uint32_t)(ARTOS_HUE_BAR_H + 2), COLOR_WHITE); }

    /* SV box */
    int sv_x = 416;
    for (int sy2 = 0; sy2 < ARTOS_SV_BOX_SIZE; sy2++) {
        for (int sx2 = 0; sx2 < ARTOS_SV_BOX_SIZE; sx2++) {
            int s = sx2 * 255 / (ARTOS_SV_BOX_SIZE - 1);
            int v = (ARTOS_SV_BOX_SIZE - 1 - sy2) * 255 / (ARTOS_SV_BOX_SIZE - 1);
            uint32_t pc = hsv_to_rgb(art.hsv_h, s, v);
            fb_fill_rect((uint32_t)(ox + sv_x + sx2), (uint32_t)(oy + pal_y + 4 + sy2), 1, 1, pc);
        }
    }
    fb_draw_rect((uint32_t)(ox + sv_x), (uint32_t)(oy + pal_y + 4),
                 (uint32_t)ARTOS_SV_BOX_SIZE, (uint32_t)ARTOS_SV_BOX_SIZE, COLOR_TEXT_DIM);

    /* Color preview */
    fb_fill_rect((uint32_t)(ox + 454), (uint32_t)(oy + pal_y + 4), 20, 20, art.fg_color);
    fb_draw_rect((uint32_t)(ox + 454), (uint32_t)(oy + pal_y + 4), 20, 20, COLOR_TEXT_DIM);

    /* Status line */
    font_draw_string((uint32_t)(ox + 4), (uint32_t)(oy + pal_y + 24), "ArtOS", COLOR_ICON_PURPLE, tb_bg);
    if (art.modified)
        font_draw_string((uint32_t)(ox + 52), (uint32_t)(oy + pal_y + 24), "*", COLOR_HIGHLIGHT, tb_bg);

    /* Canvas info */
    { char info[32]; strcpy(info, "256x192 z:");
      info[10] = '0' + (char)art.zoom; info[11] = 'x'; info[12] = ' '; info[13] = 'L';
      info[14] = '1' + (char)art.active_layer; info[15] = '\0';
      font_draw_string((uint32_t)(ox + cw - 128), (uint32_t)(oy + pal_y + 24), info, COLOR_TEXT_DIM, tb_bg); }

    /* DrawNet peer cursors */
    drawnet_paint_cursors(win);
}

/* Click handler
 * button flags: bit0=left, bit6(0x40)=release, bit7(0x80)=drag motion */
static void artos_click(struct wm_window *win, int x, int y, int button)
{
    int cw = wm_content_width(win);
    int ch = wm_content_height(win);
    int cp_w = cw - ARTOS_LAYER_PANEL_W;
    int is_drag = (button & 0x80) != 0;
    int is_release = (button & 0x40) != 0;
    uint32_t *layer_px = art.layers[art.active_layer].pixels;

    /* === Handle drag (continuous drawing, shape preview, selection move) === */
    if (is_drag && art.drawing) {
        int cx_coord, cy_coord;
        if (artos_screen_to_canvas(x, y, &cx_coord, &cy_coord)) {
            artos_snap(&cx_coord, &cy_coord);
            uint32_t draw_color = (art.tool == ARTOS_TOOL_ERASER) ? art.bg_color : art.fg_color;

            if (art.tool == ARTOS_TOOL_PENCIL || art.tool == ARTOS_TOOL_ERASER) {
                artos_line(art.last_cx, art.last_cy, cx_coord, cy_coord,
                           draw_color, art.brush_size);
                if (art.mirror_mode)
                    artos_line(ARTOS_CANVAS_W-1-art.last_cx, art.last_cy,
                               ARTOS_CANVAS_W-1-cx_coord, cy_coord,
                               draw_color, art.brush_size);
                drawnet_push_stroke(art.tool, art.last_cx, art.last_cy,
                                    cx_coord, cy_coord, draw_color, art.brush_size);
                art.last_cx = cx_coord;
                art.last_cy = cy_coord;
            } else if (art.tool == ARTOS_TOOL_SPRAY) {
                artos_spray(cx_coord, cy_coord, art.fg_color, art.brush_size * 3);
                if (art.mirror_mode)
                    artos_spray(ARTOS_CANVAS_W-1-cx_coord, cy_coord, art.fg_color, art.brush_size * 3);
            } else if (art.tool == ARTOS_TOOL_CALLIG) {
                artos_callig_line(art.last_cx, art.last_cy, cx_coord, cy_coord,
                                  draw_color, art.brush_size);
                if (art.mirror_mode)
                    artos_callig_line(ARTOS_CANVAS_W-1-art.last_cx, art.last_cy,
                                      ARTOS_CANVAS_W-1-cx_coord, cy_coord,
                                      draw_color, art.brush_size);
                art.last_cx = cx_coord; art.last_cy = cy_coord;
            } else if (art.tool == ARTOS_TOOL_SOFTBRUSH) {
                artos_soft_plot(cx_coord, cy_coord, draw_color, art.brush_size);
                if (art.mirror_mode)
                    artos_soft_plot(ARTOS_CANVAS_W-1-cx_coord, cy_coord, draw_color, art.brush_size);
                art.last_cx = cx_coord; art.last_cy = cy_coord;
            } else if (art.tool == ARTOS_TOOL_PATBRUSH) {
                artos_pattern_plot(cx_coord, cy_coord, art.brush_size);
                if (art.mirror_mode)
                    artos_pattern_plot(ARTOS_CANVAS_W-1-cx_coord, cy_coord, art.brush_size);
                art.last_cx = cx_coord; art.last_cy = cy_coord;
            } else if (art.tool == ARTOS_TOOL_CLONE && art.clone_src_set) {
                artos_clone_plot(cx_coord, cy_coord, art.brush_size);
                art.last_cx = cx_coord; art.last_cy = cy_coord;
            } else if (art.tool == ARTOS_TOOL_SMUDGE) {
                artos_smudge_apply(cx_coord, cy_coord, art.brush_size);
                art.last_cx = cx_coord; art.last_cy = cy_coord;
            } else if (art.tool == ARTOS_TOOL_SELECT) {
                if (art.sel_moving) {
                    int ddx = cx_coord - art.sel_move_ox;
                    int ddy = cy_coord - art.sel_move_oy;
                    if (ddx != 0 || ddy != 0) {
                        int sw = art.sel_x2 - art.sel_x1;
                        int sh = art.sel_y2 - art.sel_y1;
                        for (int sy = art.sel_y1; sy < art.sel_y2; sy++)
                            for (int sx = art.sel_x1; sx < art.sel_x2; sx++)
                                if (sx >= 0 && sx < ARTOS_CANVAS_W && sy >= 0 && sy < ARTOS_CANVAS_H)
                                    artos_canvas_set(sx, sy, art.bg_color);
                        art.sel_x1 += ddx; art.sel_y1 += ddy;
                        art.sel_x2 += ddx; art.sel_y2 += ddy;
                        for (int sy = 0; sy < sh; sy++)
                            for (int sx = 0; sx < sw; sx++) {
                                int nx = art.sel_x1 + sx, ny = art.sel_y1 + sy;
                                if (nx >= 0 && nx < ARTOS_CANVAS_W && ny >= 0 && ny < ARTOS_CANVAS_H)
                                    artos_canvas_set(nx, ny, art.sel_buf[sy * ARTOS_CANVAS_W + sx]);
                            }
                        art.sel_move_ox = cx_coord;
                        art.sel_move_oy = cy_coord;
                    }
                } else {
                    art.sel_x2 = cx_coord;
                    art.sel_y2 = cy_coord;
                }
            } else if (art.tool == ARTOS_TOOL_LINE || art.tool == ARTOS_TOOL_RECT ||
                       art.tool == ARTOS_TOOL_FILLRECT || art.tool == ARTOS_TOOL_ELLIPSE ||
                       art.tool == ARTOS_TOOL_RNDRECT || art.tool == ARTOS_TOOL_CIRCLE ||
                       art.tool == ARTOS_TOOL_STAR || art.tool == ARTOS_TOOL_ARROW ||
                       art.tool == ARTOS_TOOL_GRADFILL) {
                /* Restore saved layer and draw shape preview */
                memcpy(layer_px, art.shape_save,
                       sizeof(uint32_t) * ARTOS_CANVAS_W * ARTOS_CANVAS_H);
                if (art.tool == ARTOS_TOOL_LINE) {
                    artos_line(art.start_cx, art.start_cy, cx_coord, cy_coord,
                               art.fg_color, art.brush_size);
                } else if (art.tool == ARTOS_TOOL_RECT) {
                    artos_rect(art.start_cx, art.start_cy, cx_coord, cy_coord,
                               art.fg_color);
                } else if (art.tool == ARTOS_TOOL_FILLRECT) {
                    artos_fill_rect(art.start_cx, art.start_cy, cx_coord, cy_coord,
                                    art.fg_color);
                } else if (art.tool == ARTOS_TOOL_ELLIPSE) {
                    int ecx = (art.start_cx + cx_coord) / 2;
                    int ecy = (art.start_cy + cy_coord) / 2;
                    int erx = (cx_coord - art.start_cx) / 2;
                    int ery = (cy_coord - art.start_cy) / 2;
                    if (erx < 0) erx = -erx;
                    if (ery < 0) ery = -ery;
                    artos_ellipse(ecx, ecy, erx, ery, art.fg_color);
                } else if (art.tool == ARTOS_TOOL_RNDRECT) {
                    artos_round_rect(art.start_cx, art.start_cy, cx_coord, cy_coord,
                                     art.fg_color, art.brush_size * 2);
                } else if (art.tool == ARTOS_TOOL_CIRCLE) {
                    int ddx = cx_coord - art.start_cx;
                    int ddy = cy_coord - art.start_cy;
                    int r = isqrt(ddx * ddx + ddy * ddy);
                    artos_ellipse(art.start_cx, art.start_cy, r, r, art.fg_color);
                } else if (art.tool == ARTOS_TOOL_STAR) {
                    int ddx = cx_coord - art.start_cx;
                    int ddy = cy_coord - art.start_cy;
                    int r = isqrt(ddx * ddx + ddy * ddy);
                    artos_star(art.start_cx, art.start_cy, r, art.star_sides, art.fg_color);
                } else if (art.tool == ARTOS_TOOL_ARROW) {
                    artos_arrow(art.start_cx, art.start_cy, cx_coord, cy_coord,
                                art.fg_color, art.brush_size);
                } else if (art.tool == ARTOS_TOOL_GRADFILL) {
                    artos_grad_fill(art.start_cx, art.start_cy, cx_coord, cy_coord);
                }
            }
        }
        return;
    }

    /* === Handle release (finalize shape / selection) === */
    if (is_release) {
        if (art.tool == ARTOS_TOOL_SELECT && art.drawing && !art.sel_moving) {
            /* Normalize selection rectangle */
            if (art.sel_x1 > art.sel_x2) { int t = art.sel_x1; art.sel_x1 = art.sel_x2; art.sel_x2 = t; }
            if (art.sel_y1 > art.sel_y2) { int t = art.sel_y1; art.sel_y1 = art.sel_y2; art.sel_y2 = t; }
            /* Clamp to canvas */
            if (art.sel_x1 < 0) art.sel_x1 = 0;
            if (art.sel_y1 < 0) art.sel_y1 = 0;
            if (art.sel_x2 > ARTOS_CANVAS_W) art.sel_x2 = ARTOS_CANVAS_W;
            if (art.sel_y2 > ARTOS_CANVAS_H) art.sel_y2 = ARTOS_CANVAS_H;
            /* Save selection contents */
            int sw = art.sel_x2 - art.sel_x1;
            int sh = art.sel_y2 - art.sel_y1;
            if (sw > 0 && sh > 0) {
                art.sel_active = 1;
                for (int sy = 0; sy < sh; sy++)
                    for (int sx = 0; sx < sw; sx++)
                        art.sel_buf[sy * ARTOS_CANVAS_W + sx] =
                            artos_canvas_get(art.sel_x1 + sx, art.sel_y1 + sy);
            }
        }
        art.drawing = 0;
        art.sel_moving = 0;
        return;
    }

    /* === Normal click (initial press) === */

    /* Deactivate text input fields when clicking elsewhere */
    int clicked_ai = 0, clicked_dn = 0;

    /* Row A (y=2..19): tools 0-5 */
    if (y >= 2 && y < 20) {
        for (int i = 0; i < 6; i++) {
            int bx = 4 + i * (ARTOS_BTN_W + ARTOS_BTN_GAP);
            if (x >= bx && x < bx + ARTOS_BTN_W) {
                art.tool = i;
                art.text_active = 0;
                return;
            }
        }
    }

    /* Row B (y=22..39): tools 6-11 */
    if (y >= 22 && y < 40) {
        for (int i = 6; i < 12; i++) {
            int bx = 4 + (i - 6) * (ARTOS_BTN_W + ARTOS_BTN_GAP);
            if (x >= bx && x < bx + ARTOS_BTN_W) {
                art.tool = i;
                art.text_active = 0;
                art.sel_active = 0;
                art.poly_count = 0;
                return;
            }
        }
    }

    /* Row C (y=42..59): tools 12-17 */
    if (y >= 42 && y < 60) {
        for (int i = 12; i < 18; i++) {
            int bx = 4 + (i - 12) * (ARTOS_BTN_W + ARTOS_BTN_GAP);
            if (x >= bx && x < bx + ARTOS_BTN_W) {
                art.tool = i;
                art.text_active = 0; art.sel_active = 0;
                art.poly_count = 0; art.bezier_count = 0;
                if (i == ARTOS_TOOL_CLONE) art.clone_src_set = 0;
                return;
            }
        }
    }

    /* Row D (y=62..79): tools 18-23 */
    if (y >= 62 && y < 80) {
        for (int i = 18; i < ARTOS_TOOL_COUNT; i++) {
            int bx = 4 + (i - 18) * (ARTOS_BTN_W + ARTOS_BTN_GAP);
            if (x >= bx && x < bx + ARTOS_BTN_W) {
                art.tool = i;
                art.text_active = 0; art.sel_active = 0;
                art.poly_count = 0; art.bezier_count = 0;
                if (i == ARTOS_TOOL_CLONE) art.clone_src_set = 0;
                return;
            }
        }
    }

    /* Row E (y=82..99): Undo, Clear, Size, Opacity, FG, BG, Swap, Zoom, Mirror, Grid */
    if (y >= 82 && y < 100) {
        /* Undo (x=4..39) */
        if (x >= 4 && x < 40) { artos_undo(); return; }
        /* Clear (x=44..79) */
        if (x >= 44 && x < 80) {
            artos_undo_push();
            for (int i = 0; i < ARTOS_CANVAS_W * ARTOS_CANVAS_H; i++)
                layer_px[i] = art.bg_color;
            art.modified = 1;
            return;
        }
        /* Size - (x=104..119) */
        if (x >= 104 && x < 120) { if (art.brush_size > 1) art.brush_size--; return; }
        /* Size + (x=136..151) */
        if (x >= 136 && x < 152) { if (art.brush_size < ARTOS_MAX_BRUSH) art.brush_size++; return; }
        /* Opacity - (x=176..191) */
        if (x >= 176 && x < 192) {
            if (art.brush_opacity > ARTOS_OPACITY_STEP) art.brush_opacity -= ARTOS_OPACITY_STEP;
            else art.brush_opacity = 1;
            return;
        }
        /* Opacity + (x=212..227) */
        if (x >= 212 && x < 228) {
            art.brush_opacity += ARTOS_OPACITY_STEP;
            if (art.brush_opacity > ARTOS_MAX_OPACITY) art.brush_opacity = ARTOS_MAX_OPACITY;
            return;
        }
        /* FG swatch (x=236..253) */
        if (x >= 236 && x < 254) return; /* Just visual indicator */
        /* BG swatch (x=258..275) */
        if (x >= 258 && x < 276) return;
        /* Swap (x=280..307) */
        if (x >= 280 && x < 308) {
            uint32_t tmp = art.fg_color;
            art.fg_color = art.bg_color;
            art.bg_color = tmp;
            return;
        }
        /* Zoom - (x=334..349) */
        if (x >= 334 && x < 350) { if (art.zoom > 1) art.zoom--; return; }
        /* Zoom + (x=364..379) */
        if (x >= 364 && x < 380) { if (art.zoom < 3) art.zoom++; return; }
        /* Mirror toggle (x=388..415) */
        if (x >= 388 && x < 416) { art.mirror_mode = !art.mirror_mode; return; }
        /* Grid snap toggle (x=420..447) */
        if (x >= 420 && x < 448) { art.grid_snap = !art.grid_snap; return; }
    }

    /* Row F (y=102..119): AI prompt + Generate */
    if (y >= 102 && y < 120) {
        if (x >= 26 && x < 306) {
            art.ai_input_active = 1;
            art.drawnet_input_active = 0;
            art.text_active = 0;
            clicked_ai = 1;
            return;
        }
        if (x >= 312 && x < 368) {
            generate_ai_art();
            art.ai_input_active = 0;
            return;
        }
    }

    /* Row G (y=120..131): DrawNet session + Go/Stop */
    if (y >= 120 && y < ARTOS_TOOLBAR_H) {
        if (x >= 32 && x < 112) {
            art.drawnet_input_active = 1;
            art.ai_input_active = 0;
            art.text_active = 0;
            clicked_dn = 1;
            return;
        }
        if (x >= 118 && x < 154) {
            if (art.drawnet_enabled) {
                drawnet_stop_session();
            } else {
                if (art.drawnet_input[0])
                    drawnet_init_session(art.drawnet_input);
            }
            art.drawnet_input_active = 0;
            return;
        }
    }

    /* Deactivate text fields if not clicked */
    if (!clicked_ai) art.ai_input_active = 0;
    if (!clicked_dn) art.drawnet_input_active = 0;

    /* === Layer panel clicks (right side) === */
    int ca_y = ARTOS_TOOLBAR_H;
    int ca_h = ch - ARTOS_TOOLBAR_H - ARTOS_PALETTE_H;
    if (x >= cp_w && y >= ca_y && y < ca_y + ca_h) {
        int lx = x - cp_w; /* local x within panel */
        int ly = y - ca_y; /* local y within panel */

        /* Layer entries: each 24px tall starting at ly=22 */
        for (int l = 0; l < art.layer_count; l++) {
            int ey = 22 + l * 24;
            if (ly >= ey && ly < ey + 22) {
                /* Eye icon (x 4..11 within panel) */
                if (lx >= 4 && lx < 12) {
                    art.layers[l].visible = !art.layers[l].visible;
                    return;
                }
                /* Click on layer name = switch to it */
                artos_switch_layer(l);
                return;
            }
        }

        /* Add layer button */
        if (art.layer_count < ARTOS_MAX_LAYERS) {
            int aby = 22 + art.layer_count * 24 + 4;
            if (ly >= aby && ly < aby + 16 && lx >= 4 && lx < 56) {
                int nl = art.layer_count;
                art.layer_count++;
                art.layers[nl].visible = 1;
                art.layers[nl].opacity = 255;
                art.layers[nl].name[0] = 'L'; art.layers[nl].name[1] = 'y';
                art.layers[nl].name[2] = 'r'; art.layers[nl].name[3] = ' ';
                art.layers[nl].name[4] = '1' + (char)nl; art.layers[nl].name[5] = '\0';
                for (int i = 0; i < ARTOS_CANVAS_W * ARTOS_CANVAS_H; i++)
                    art.layers[nl].pixels[i] = 0x00000000; /* transparent */
                artos_switch_layer(nl);
                return;
            }
        }

        /* Flatten button */
        if (art.layer_count > 1) {
            int fby = 22 + ARTOS_MAX_LAYERS * 24 + 8;
            if (ly >= fby && ly < fby + 16 && lx >= 4 && lx < 56) {
                artos_flatten_layers();
                return;
            }
        }

        /* Layer opacity controls (bottom of panel) */
        int loy = ca_h - 40;
        if (ly >= loy + 14 && ly < loy + 28) {
            /* Opacity - */
            if (lx >= 4 && lx < 20) {
                int o = art.layers[art.active_layer].opacity;
                o -= ARTOS_OPACITY_STEP;
                if (o < 0) o = 0;
                art.layers[art.active_layer].opacity = (uint8_t)o;
                return;
            }
            /* Opacity + */
            if (lx >= 40 && lx < 56) {
                int o = art.layers[art.active_layer].opacity;
                o += ARTOS_OPACITY_STEP;
                if (o > 255) o = 255;
                art.layers[art.active_layer].opacity = (uint8_t)o;
                return;
            }
        }
        return;
    }

    /* === Canvas click (begin drawing) === */
    int cx_coord, cy_coord;
    if (artos_screen_to_canvas(x, y, &cx_coord, &cy_coord)) {
        artos_snap(&cx_coord, &cy_coord);
        uint32_t draw_color = (art.tool == ARTOS_TOOL_ERASER) ? art.bg_color : art.fg_color;

        if (art.tool == ARTOS_TOOL_PENCIL || art.tool == ARTOS_TOOL_ERASER) {
            artos_undo_push();
            artos_plot(cx_coord, cy_coord, draw_color, art.brush_size);
            if (art.mirror_mode)
                artos_plot(ARTOS_CANVAS_W-1-cx_coord, cy_coord, draw_color, art.brush_size);
            art.drawing = 1;
            art.last_cx = cx_coord;
            art.last_cy = cy_coord;
            art.modified = 1;
        } else if (art.tool == ARTOS_TOOL_LINE || art.tool == ARTOS_TOOL_RECT ||
                   art.tool == ARTOS_TOOL_FILLRECT || art.tool == ARTOS_TOOL_ELLIPSE ||
                   art.tool == ARTOS_TOOL_RNDRECT || art.tool == ARTOS_TOOL_CIRCLE ||
                   art.tool == ARTOS_TOOL_STAR || art.tool == ARTOS_TOOL_ARROW ||
                   art.tool == ARTOS_TOOL_GRADFILL) {
            artos_undo_push();
            memcpy(art.shape_save, layer_px,
                   sizeof(uint32_t) * ARTOS_CANVAS_W * ARTOS_CANVAS_H);
            art.drawing = 1;
            art.start_cx = cx_coord;
            art.start_cy = cy_coord;
            art.modified = 1;
        } else if (art.tool == ARTOS_TOOL_FILL) {
            artos_undo_push();
            artos_flood_fill(cx_coord, cy_coord, art.fg_color);
            art.modified = 1;
        } else if (art.tool == ARTOS_TOOL_DITHFILL) {
            artos_undo_push();
            artos_dither_fill(cx_coord, cy_coord);
            art.modified = 1;
        } else if (art.tool == ARTOS_TOOL_EYEDROP) {
            art.fg_color = artos_canvas_get(cx_coord, cy_coord);
            rgb_to_hsv(art.fg_color, &art.hsv_h, &art.hsv_s, &art.hsv_v);
        } else if (art.tool == ARTOS_TOOL_TEXT) {
            art.text_cx = cx_coord;
            art.text_cy = cy_coord;
            art.text_active = 1;
            art.text_cursor = 0;
            art.text_buf[0] = '\0';
            artos_undo_push();
            art.modified = 1;
        } else if (art.tool == ARTOS_TOOL_POLYGON) {
            if (art.poly_count > 2) {
                int pdx = cx_coord - art.poly_verts[0][0];
                int pdy = cy_coord - art.poly_verts[0][1];
                if (pdx < 0) pdx = -pdx;
                if (pdy < 0) pdy = -pdy;
                if (pdx < 5 && pdy < 5) {
                    artos_undo_push();
                    artos_close_polygon();
                    art.poly_count = 0;
                    art.modified = 1;
                    return;
                }
            }
            if (art.poly_count < ARTOS_MAX_POLY_VERTS) {
                if (art.poly_count == 0) artos_undo_push();
                art.poly_verts[art.poly_count][0] = cx_coord;
                art.poly_verts[art.poly_count][1] = cy_coord;
                art.poly_count++;
                if (art.poly_count > 1) {
                    int pi = art.poly_count - 2;
                    artos_line(art.poly_verts[pi][0], art.poly_verts[pi][1],
                               cx_coord, cy_coord, art.fg_color, art.brush_size);
                    art.modified = 1;
                }
            }
        } else if (art.tool == ARTOS_TOOL_BEZIER) {
            art.bezier_pts[art.bezier_count][0] = cx_coord;
            art.bezier_pts[art.bezier_count][1] = cy_coord;
            art.bezier_count++;
            if (art.bezier_count >= 4) {
                artos_undo_push();
                artos_bezier(art.bezier_pts[0][0], art.bezier_pts[0][1],
                             art.bezier_pts[1][0], art.bezier_pts[1][1],
                             art.bezier_pts[2][0], art.bezier_pts[2][1],
                             art.bezier_pts[3][0], art.bezier_pts[3][1],
                             art.fg_color, art.brush_size);
                art.bezier_count = 0;
                art.modified = 1;
            }
        } else if (art.tool == ARTOS_TOOL_SPRAY) {
            artos_undo_push();
            artos_spray(cx_coord, cy_coord, art.fg_color, art.brush_size * 3);
            if (art.mirror_mode)
                artos_spray(ARTOS_CANVAS_W-1-cx_coord, cy_coord, art.fg_color, art.brush_size * 3);
            art.drawing = 1;
            art.modified = 1;
        } else if (art.tool == ARTOS_TOOL_CALLIG) {
            artos_undo_push();
            artos_callig_plot(cx_coord, cy_coord, draw_color, art.brush_size);
            if (art.mirror_mode)
                artos_callig_plot(ARTOS_CANVAS_W-1-cx_coord, cy_coord, draw_color, art.brush_size);
            art.drawing = 1;
            art.last_cx = cx_coord; art.last_cy = cy_coord;
            art.modified = 1;
        } else if (art.tool == ARTOS_TOOL_SOFTBRUSH) {
            artos_undo_push();
            artos_soft_plot(cx_coord, cy_coord, draw_color, art.brush_size);
            if (art.mirror_mode)
                artos_soft_plot(ARTOS_CANVAS_W-1-cx_coord, cy_coord, draw_color, art.brush_size);
            art.drawing = 1;
            art.last_cx = cx_coord; art.last_cy = cy_coord;
            art.modified = 1;
        } else if (art.tool == ARTOS_TOOL_PATBRUSH) {
            artos_undo_push();
            artos_pattern_plot(cx_coord, cy_coord, art.brush_size);
            if (art.mirror_mode)
                artos_pattern_plot(ARTOS_CANVAS_W-1-cx_coord, cy_coord, art.brush_size);
            art.drawing = 1;
            art.last_cx = cx_coord; art.last_cy = cy_coord;
            art.modified = 1;
        } else if (art.tool == ARTOS_TOOL_CLONE) {
            if (!art.clone_src_set) {
                art.clone_src_x = cx_coord;
                art.clone_src_y = cy_coord;
                art.clone_src_set = 1;
            } else {
                art.clone_off_x = art.clone_src_x - cx_coord;
                art.clone_off_y = art.clone_src_y - cy_coord;
                artos_undo_push();
                artos_clone_plot(cx_coord, cy_coord, art.brush_size);
                art.drawing = 1;
                art.last_cx = cx_coord; art.last_cy = cy_coord;
                art.modified = 1;
            }
        } else if (art.tool == ARTOS_TOOL_SMUDGE) {
            artos_undo_push();
            artos_smudge_pickup(cx_coord, cy_coord, art.brush_size);
            art.drawing = 1;
            art.last_cx = cx_coord; art.last_cy = cy_coord;
            art.modified = 1;
        } else if (art.tool == ARTOS_TOOL_SELECT) {
            if (art.sel_active &&
                cx_coord >= art.sel_x1 && cx_coord < art.sel_x2 &&
                cy_coord >= art.sel_y1 && cy_coord < art.sel_y2) {
                art.sel_moving = 1;
                art.sel_move_ox = cx_coord;
                art.sel_move_oy = cy_coord;
                art.drawing = 1;
                artos_undo_push();
            } else {
                art.sel_active = 0;
                art.sel_moving = 0;
                art.sel_x1 = cx_coord; art.sel_y1 = cy_coord;
                art.sel_x2 = cx_coord; art.sel_y2 = cy_coord;
                art.drawing = 1;
            }
        }
        return;
    }

    /* === Bottom palette clicks === */
    int pal_y = ch - ARTOS_PALETTE_H;
    if (y >= pal_y) {
        int py = y - pal_y;

        /* 16 quick swatches: x=4.., 14px wide, 2px gap, y offset 4 */
        if (py >= 4 && py < 18) {
            for (int i = 0; i < ARTOS_PALETTE_COUNT; i++) {
                int sx = 4 + i * (14 + 2);
                if (x >= sx && x < sx + 14) {
                    art.fg_color = artos_palette[i];
                    rgb_to_hsv(art.fg_color, &art.hsv_h, &art.hsv_s, &art.hsv_v);
                    return;
                }
            }
        }

        /* HSV Hue bar: x=280..407, y=pal_y+4..pal_y+15 */
        if (py >= 4 && py < 4 + ARTOS_HUE_BAR_H && x >= 280 && x < 280 + ARTOS_HUE_BAR_W) {
            art.hsv_h = (x - 280) * 360 / ARTOS_HUE_BAR_W;
            if (art.hsv_h > 359) art.hsv_h = 359;
            art.fg_color = hsv_to_rgb(art.hsv_h, art.hsv_s, art.hsv_v);
            return;
        }

        /* SV box: x=416..447, y=pal_y+4..pal_y+35 */
        if (py >= 4 && py < 4 + ARTOS_SV_BOX_SIZE && x >= 416 && x < 416 + ARTOS_SV_BOX_SIZE) {
            art.hsv_s = (x - 416) * 255 / (ARTOS_SV_BOX_SIZE - 1);
            art.hsv_v = (ARTOS_SV_BOX_SIZE - 1 - (py - 4)) * 255 / (ARTOS_SV_BOX_SIZE - 1);
            if (art.hsv_s > 255) art.hsv_s = 255;
            if (art.hsv_v > 255) art.hsv_v = 255;
            art.fg_color = hsv_to_rgb(art.hsv_h, art.hsv_s, art.hsv_v);
            return;
        }
    }
}

/* Key handler */
static void artos_key(struct wm_window *win, int key)
{
    (void)win;

    /* Text tool input mode — captures all printable keys */
    if (art.text_active) {
        if (key == 27) {
            /* Escape: exit text mode */
            art.text_active = 0;
            return;
        } else if (key == '\n' || key == '\r') {
            /* Enter: newline */
            art.text_cy += 16;
            art.text_cx = art.poly_verts[0][0]; /* reuse for line start X (not great, but saves a field) */
            return;
        } else if (key == '\b' || key == 127) {
            /* Backspace: crude — can't truly un-draw, just move cursor back */
            if (art.text_cx >= 8) art.text_cx -= 8;
            return;
        } else if (key >= 32 && key < 127) {
            /* Printable character: render onto canvas */
            artos_render_text_char(art.text_cx, art.text_cy, (char)key, art.fg_color);
            art.text_cx += 8;
            art.modified = 1;
            return;
        }
        return;
    }

    /* AI prompt input mode */
    if (art.ai_input_active) {
        if (key == '\n' || key == '\r') {
            generate_ai_art();
            art.ai_input_active = 0;
            return;
        } else if (key == '\b' || key == 127) {
            int len = 0;
            while (art.ai_prompt[len]) len++;
            if (len > 0) {
                art.ai_prompt[len - 1] = '\0';
                art.ai_prompt_cursor = len - 1;
            }
            return;
        } else if (key == 27) {
            art.ai_input_active = 0;
            return;
        } else if (key >= 32 && key < 127) {
            int len = 0;
            while (art.ai_prompt[len]) len++;
            if (len < 63) {
                art.ai_prompt[len] = (char)key;
                art.ai_prompt[len + 1] = '\0';
                art.ai_prompt_cursor = len + 1;
            }
            return;
        }
        return;
    }

    /* DrawNet session ID input mode */
    if (art.drawnet_input_active) {
        if (key == '\n' || key == '\r') {
            if (art.drawnet_input[0])
                drawnet_init_session(art.drawnet_input);
            art.drawnet_input_active = 0;
            return;
        } else if (key == '\b' || key == 127) {
            int len = (int)strlen(art.drawnet_input);
            if (len > 0) art.drawnet_input[len - 1] = '\0';
            return;
        } else if (key == 27) {
            art.drawnet_input_active = 0;
            return;
        } else if (key >= 32 && key < 127) {
            int len = (int)strlen(art.drawnet_input);
            if (len < 15) {
                art.drawnet_input[len] = (char)key;
                art.drawnet_input[len + 1] = '\0';
            }
            return;
        }
        return;
    }

    /* === Normal keyboard shortcuts === */

    /* Undo */
    if (key == 'z' || key == 'u') { artos_undo(); return; }

    /* Tool shortcuts */
    if (key == 'p') { art.tool = ARTOS_TOOL_PENCIL; return; }
    if (key == 'l') { art.tool = ARTOS_TOOL_LINE; return; }
    if (key == 'r') { art.tool = ARTOS_TOOL_RECT; return; }
    if (key == 'f') { art.tool = ARTOS_TOOL_FILLRECT; return; }
    if (key == 'e') { art.tool = ARTOS_TOOL_ELLIPSE; return; }
    if (key == 'g') { art.tool = ARTOS_TOOL_FILL; return; }
    if (key == 't') { art.tool = ARTOS_TOOL_TEXT; return; }
    if (key == 'n') { art.tool = ARTOS_TOOL_POLYGON; art.poly_count = 0; return; }
    if (key == 's') { art.tool = ARTOS_TOOL_SPRAY; return; }
    if (key == 'm') { art.tool = ARTOS_TOOL_SELECT; return; }
    /* New v3 tool shortcuts */
    if (key == 'o') { art.tool = ARTOS_TOOL_RNDRECT; return; }
    if (key == 'c') { art.tool = ARTOS_TOOL_CIRCLE; return; }
    if (key == 'w') { art.tool = ARTOS_TOOL_STAR; return; }
    if (key == 'a') { art.tool = ARTOS_TOOL_ARROW; return; }
    if (key == 'b') { art.tool = ARTOS_TOOL_BEZIER; art.bezier_count = 0; return; }
    if (key == 'd') { art.tool = ARTOS_TOOL_SOFTBRUSH; return; }
    if (key == 'k') { art.tool = ARTOS_TOOL_CLONE; art.clone_src_set = 0; return; }
    if (key == 'j') { art.tool = ARTOS_TOOL_SMUDGE; return; }
    if (key == 'i') { art.tool = ARTOS_TOOL_CALLIG; return; }
    if (key == 'h') { art.tool = ARTOS_TOOL_DITHFILL; return; }

    /* Star sides: number keys 3-8 when Star tool active */
    if (art.tool == ARTOS_TOOL_STAR && key >= '3' && key <= '8') {
        art.star_sides = key - '0';
        return;
    }

    /* Grid size toggle: q key */
    if (key == 'q') {
        art.grid_size = (art.grid_size == 4) ? 8 : 4;
        return;
    }

    /* Edit operations (Shift+key = uppercase) */
    if (key == 'H') { artos_undo_push(); artos_flip_h(); art.modified = 1; return; }
    if (key == 'V') { artos_undo_push(); artos_flip_v(); art.modified = 1; return; }
    if (key == 'I') { artos_undo_push(); artos_invert(); art.modified = 1; return; }
    if (key == 'B') { artos_undo_push(); artos_brightness(16); art.modified = 1; return; }
    if (key == 'D') { artos_undo_push(); artos_brightness(-16); art.modified = 1; return; }
    if (key == 'P') { artos_undo_push(); artos_posterize(); art.modified = 1; return; }
    if (key == 'M') { art.mirror_mode = !art.mirror_mode; return; }
    if (key == 'G') { art.grid_snap = !art.grid_snap; return; }

    /* Swap FG/BG */
    if (key == 'x') {
        uint32_t tmp = art.fg_color;
        art.fg_color = art.bg_color;
        art.bg_color = tmp;
        return;
    }

    /* Brush size */
    if (key == '[') { if (art.brush_size > 1) art.brush_size--; return; }
    if (key == ']') { if (art.brush_size < ARTOS_MAX_BRUSH) art.brush_size++; return; }

    /* Brush opacity */
    if (key == '{') {
        if (art.brush_opacity > ARTOS_OPACITY_STEP) art.brush_opacity -= ARTOS_OPACITY_STEP;
        else art.brush_opacity = 1;
        return;
    }
    if (key == '}') {
        art.brush_opacity += ARTOS_OPACITY_STEP;
        if (art.brush_opacity > ARTOS_MAX_OPACITY) art.brush_opacity = ARTOS_MAX_OPACITY;
        return;
    }

    /* Zoom */
    if (key == '+' || key == '=') { if (art.zoom < 3) art.zoom++; return; }
    if (key == '-') { if (art.zoom > 1) art.zoom--; return; }

    /* Pan (arrow keys — typically sent as escape sequences, but handle raw) */
    /* Arrow keys from PS/2 come as scan codes mapped by our keyboard driver */
    /* Using WASD-style for pan when zoomed: h/j/k/l or arrow surrogates */
    if (key == 0x100) { art.scroll_x -= 8; if (art.scroll_x < 0) art.scroll_x = 0; return; } /* Left */
    if (key == 0x101) { art.scroll_x += 8; return; } /* Right */
    if (key == 0x102) { art.scroll_y -= 8; if (art.scroll_y < 0) art.scroll_y = 0; return; } /* Up */
    if (key == 0x103) { art.scroll_y += 8; return; } /* Down */

    /* Selection: Escape to deselect, Delete/Backspace to clear */
    if (key == 27) {
        art.sel_active = 0;
        art.poly_count = 0;
        art.text_active = 0;
        art.bezier_count = 0;
        art.clone_src_set = 0;
        return;
    }
    if ((key == '\b' || key == 127) && art.sel_active) {
        /* Clear selection area to bg color */
        artos_undo_push();
        for (int sy = art.sel_y1; sy < art.sel_y2; sy++)
            for (int sx = art.sel_x1; sx < art.sel_x2; sx++)
                if (sx >= 0 && sx < ARTOS_CANVAS_W && sy >= 0 && sy < ARTOS_CANVAS_H)
                    artos_canvas_set(sx, sy, art.bg_color);
        art.sel_active = 0;
        art.modified = 1;
        return;
    }
}

/*============================================================================
 * MusiKey - Musical Authentication
 * "Secure authentication through unique musical compositions"
 *============================================================================*/

/* Generate pentatonic composition with melodic contour and rhythm variation */
static void mk_generate_composition(const char *user, const char *pass,
                                     char *comp, char *dur, uint16_t *freqs,
                                     int *entropy, int *scale_key,
                                     int scores[MK_NUM_SCORES])
{
    /* Mix username and passphrase into a seed */
    unsigned int seed = 5381;
    for (const char *p = user; *p; p++)
        seed = seed * 33 + (unsigned char)*p;
    for (const char *p = pass; *p; p++)
        seed = seed * 37 + (unsigned char)*p;

    /* Derive scale key from seed (0-11) */
    unsigned int ks = seed;
    ks = ks * 1103515245 + 12345;
    *scale_key = (int)((ks >> 8) % 12);

    /* First note: random pentatonic index */
    seed = seed * 1103515245 + 12345;
    int prev_idx = (int)((seed >> 8) % 15);
    comp[0] = (char)mk_pentatonic[prev_idx];
    freqs[0] = mk_penta_freq[prev_idx];

    /* First duration */
    seed = seed * 1103515245 + 12345;
    dur[0] = (char)(1 + (int)((seed >> 8) % 3));

    int bits = 4;
    int step_count = 0;

    /* Generate remaining notes with stepwise motion preference */
    for (int i = 1; i < MK_COMPOSITION_LEN; i++) {
        seed = seed * 1103515245 + 12345;
        int r = (int)((seed >> 8) % 100);
        int new_idx;

        if (r < 50) {
            /* 50%: stepwise motion (+/-1 in pentatonic) */
            seed = seed * 1103515245 + 12345;
            int dir = ((seed >> 8) & 1) ? 1 : -1;
            new_idx = prev_idx + dir;
            if (new_idx < 0) new_idx = 0;
            if (new_idx > 14) new_idx = 14;
            step_count++;
        } else if (r < 80) {
            /* 30%: small leap (+/-2 or 3) */
            seed = seed * 1103515245 + 12345;
            int leap = 2 + (int)((seed >> 8) % 2);
            seed = seed * 1103515245 + 12345;
            int dir = ((seed >> 8) & 1) ? 1 : -1;
            new_idx = prev_idx + dir * leap;
            if (new_idx < 0) new_idx = 0;
            if (new_idx > 14) new_idx = 14;
        } else {
            /* 20%: random jump */
            seed = seed * 1103515245 + 12345;
            new_idx = (int)((seed >> 8) % 15);
        }

        comp[i] = (char)mk_pentatonic[new_idx];
        freqs[i] = mk_penta_freq[new_idx];
        prev_idx = new_idx;

        /* Duration: 30% short, 50% normal, 20% long */
        seed = seed * 1103515245 + 12345;
        int dr = (int)((seed >> 8) % 100);
        if (dr < 30) dur[i] = MK_DUR_SHORT;
        else if (dr < 80) dur[i] = MK_DUR_NORMAL;
        else dur[i] = MK_DUR_LONG;

        bits += 4;
    }
    *entropy = bits;

    /* Compute musicality scores (integer 0-100) */
    /* Harmonic: consonant intervals score higher */
    int interval_sum = 0;
    for (int i = 1; i < MK_COMPOSITION_LEN; i++) {
        int diff = (int)(unsigned char)comp[i] - (int)(unsigned char)comp[i - 1];
        if (diff < 0) diff = -diff;
        if (diff <= 5 || diff == 7) interval_sum += 3;
        else interval_sum += 1;
    }
    scores[MK_SCORE_HARMONIC] = interval_sum * 100 / (3 * (MK_COMPOSITION_LEN - 1));
    if (scores[MK_SCORE_HARMONIC] > 100) scores[MK_SCORE_HARMONIC] = 100;

    /* Melodic: percentage of stepwise motion */
    scores[MK_SCORE_MELODIC] = step_count * 100 / (MK_COMPOSITION_LEN - 1);

    /* Rhythm: variety of durations (balanced = higher score) */
    int dur_counts[4] = {0, 0, 0, 0};
    for (int i = 0; i < MK_COMPOSITION_LEN; i++)
        dur_counts[(int)(unsigned char)dur[i]]++;
    int min_d = dur_counts[1];
    if (dur_counts[2] < min_d) min_d = dur_counts[2];
    if (dur_counts[3] < min_d) min_d = dur_counts[3];
    scores[MK_SCORE_RHYTHM] = min_d * 300 / MK_COMPOSITION_LEN;
    if (scores[MK_SCORE_RHYTHM] > 100) scores[MK_SCORE_RHYTHM] = 100;

    /* Scale adherence: always 100% (generated from pentatonic) */
    scores[MK_SCORE_SCALE] = 100;
}

/* Update visualizer bars based on composition */
static void mk_update_visualizer(const char *comp, int len)
{
    for (int i = 0; i < MK_VIS_BARS; i++) {
        if (i < len) {
            mk.vis_target[i] = (int)(unsigned char)comp[i] * 15 / MK_PIANO_KEYS + 1;
        } else {
            mk.vis_target[i] = 1;
        }
        mk.vis_bars[i] = mk.vis_target[i]; /* Snap initially */
    }
    mk.vis_active = 1;
    mk.vis_tick = 0;
}

/* Tick visualizer bar decay */
static void mk_tick_visualizer(void)
{
    if (!mk.vis_active) return;
    mk.vis_tick++;
    if (mk.vis_tick % 2 == 0) {
        int any = 0;
        for (int i = 0; i < MK_VIS_BARS; i++) {
            if (mk.vis_bars[i] > mk.vis_target[i]) {
                mk.vis_bars[i]--;
                any = 1;
            } else if (mk.vis_bars[i] < mk.vis_target[i]) {
                mk.vis_bars[i]++;
                any = 1;
            }
        }
        if (!any) mk.vis_active = 0;
    }
}

/* Update melody contour points */
static void mk_update_contour(const char *comp, int len)
{
    mk.contour_len = len;
    for (int i = 0; i < len && i < MK_COMPOSITION_LEN; i++)
        mk.contour_notes[i] = (int)(unsigned char)comp[i];
}

/* Build key name string (e.g., "C Penta") */
static void mk_build_key_name(int key_idx)
{
    const char *kn = mk_key_names[key_idx % 12];
    int p = 0;
    while (*kn && p < 3) mk.analysis_key_name[p++] = *kn++;
    const char *suf = " Penta";
    while (*suf && p < 14) mk.analysis_key_name[p++] = *suf++;
    mk.analysis_key_name[p] = '\0';
}

/* Initialize MusiKey state */
static void mk_init_state(void)
{
    memset(&mk, 0, sizeof(mk));
    mk.key_pressed = -1;
    mk.black_pressed = -1;
    mk.preview_playing = 0;
    mk.preview_pos = 0;
    mk.authenticated = 0;
    mk.anim_phase = MK_ANIM_NONE;
    mk.show_analysis = 0;
    mk.contour_len = 0;
    mk.tone_playing = 0;
    mk.tone_error = 0;
    speaker_stop();

    str_copy(mk.status_msg, "MusiKey System Ready", 128);
    mk.status_color = COLOR_GREEN_ACTIVE;

    /* Positions match musikey_paint() layout: form_x=134, inputs at form_x+80=214 */
    widget_textinput_init(&mk.username_input, 214, 44, 240, 18);
    widget_textinput_init(&mk.passphrase_input, 214, 68, 240, 18);
    mk.active_field = 0;

    mk.enroll_btn.x = 214; mk.enroll_btn.y = 92;
    mk.enroll_btn.w = 60; mk.enroll_btn.h = 20;
    mk.enroll_btn.text = "Enroll";
    mk.enroll_btn.bg_color = COLOR_BUTTON_PRIMARY;
    mk.enroll_btn.text_color = COLOR_WHITE;

    mk.auth_btn.x = 280; mk.auth_btn.y = 92;
    mk.auth_btn.w = 84; mk.auth_btn.h = 20;
    mk.auth_btn.text = "Authenticate";
    mk.auth_btn.bg_color = COLOR_BUTTON_PRIMARY;
    mk.auth_btn.text_color = COLOR_WHITE;

    mk.play_btn.x = 370; mk.play_btn.y = 92;
    mk.play_btn.w = 84; mk.play_btn.h = 20;
    mk.play_btn.text = "Play Preview";
    mk.play_btn.bg_color = COLOR_ACCENT;
    mk.play_btn.text_color = COLOR_WHITE;

    /* Flat visualizer */
    for (int i = 0; i < MK_VIS_BARS; i++) {
        mk.vis_bars[i] = 1;
        mk.vis_target[i] = 1;
    }
    mk.analysis_key_name[0] = '\0';
}

static void mk_do_enroll(void)
{
    const char *user = widget_textinput_text(&mk.username_input);
    const char *pass = widget_textinput_text(&mk.passphrase_input);

    if (!user[0] || !pass[0]) {
        str_copy(mk.status_msg, "Enter username and passphrase", 128);
        mk.status_color = COLOR_HIGHLIGHT;
        return;
    }

    /* Check if already enrolled */
    for (int i = 0; i < mk.user_count; i++) {
        if (mk.users[i].enrolled && strcmp(mk.users[i].username, user) == 0) {
            str_copy(mk.status_msg, "User already enrolled", 128);
            mk.status_color = COLOR_ICON_ORANGE;
            return;
        }
    }

    if (mk.user_count >= MK_MAX_USERS) {
        str_copy(mk.status_msg, "User limit reached", 128);
        mk.status_color = COLOR_HIGHLIGHT;
        return;
    }

    /* Generate composition + frequencies */
    struct mk_user *u = &mk.users[mk.user_count];
    str_copy(u->username, user, MK_USERNAME_MAX);
    char comp[MK_COMPOSITION_LEN];
    char dur[MK_COMPOSITION_LEN];
    uint16_t freqs[MK_COMPOSITION_LEN];
    mk_generate_composition(user, pass, comp, dur, freqs,
                            &u->entropy_bits, &u->scale_key, u->scores);

    /* Pack, hash, scramble, and store */
    uint8_t raw[MK_TONE_DATA_LEN];
    mk_pack_tone_data(freqs, dur, raw);
    u->verify_hash = mk_compute_hash(raw, MK_TONE_DATA_LEN);
    u->salt = (uint32_t)timer_get_ticks() ^ 0x5A5A5A5A;
    memcpy(u->scrambled_data, raw, MK_TONE_DATA_LEN);
    mk_scramble(u->scrambled_data, MK_TONE_DATA_LEN, pass, u->salt);

    u->enrolled = 1;
    mk.user_count++;

    mk_update_visualizer(comp, MK_COMPOSITION_LEN);
    mk_update_contour(comp, MK_COMPOSITION_LEN);

    /* Start tone playback as enrollment confirmation */
    speaker_stop();
    memcpy(mk.tone_freqs, freqs, sizeof(freqs));
    memcpy(mk.tone_durs, dur, MK_COMPOSITION_LEN);
    mk.tone_len = MK_COMPOSITION_LEN;
    mk.tone_index = 0;
    mk.tone_tick = 0;
    mk.tone_playing = 1;
    mk.tone_error = 0;
    speaker_play_tone(mk.tone_freqs[0]);

    /* Show analysis panel */
    for (int i = 0; i < MK_NUM_SCORES; i++)
        mk.analysis_scores[i] = u->scores[i];
    mk.analysis_key = u->scale_key;
    mk_build_key_name(u->scale_key);
    mk.show_analysis = 1;
    mk.anim_phase = MK_ANIM_NONE;

    str_copy(mk.status_msg, "Enrolled - Playing key tones...", 128);
    mk.status_color = COLOR_GREEN_ACTIVE;
}

static void mk_do_authenticate(void)
{
    const char *user = widget_textinput_text(&mk.username_input);
    const char *pass = widget_textinput_text(&mk.passphrase_input);

    if (!user[0] || !pass[0]) {
        str_copy(mk.status_msg, "Enter username and passphrase", 128);
        mk.status_color = COLOR_HIGHLIGHT;
        mk.authenticated = 0;
        return;
    }

    /* Find user */
    struct mk_user *found = NULL;
    for (int i = 0; i < mk.user_count; i++) {
        if (mk.users[i].enrolled && strcmp(mk.users[i].username, user) == 0) {
            found = &mk.users[i];
            break;
        }
    }

    if (!found) {
        str_copy(mk.status_msg, "User not found", 128);
        mk.status_color = COLOR_HIGHLIGHT;
        mk.authenticated = 0;
        return;
    }

    /* Descramble attempt: copy scrambled data, XOR with passphrase keystream */
    uint8_t candidate[MK_TONE_DATA_LEN];
    memcpy(candidate, found->scrambled_data, MK_TONE_DATA_LEN);
    mk_scramble(candidate, MK_TONE_DATA_LEN, pass, found->salt);

    /* Verify: hash descrambled data, compare with stored hash */
    uint32_t candidate_hash = mk_compute_hash(candidate, MK_TONE_DATA_LEN);
    int match = (candidate_hash == found->verify_hash) ? 1 : 0;
    mk.anim_result = match;

    if (match) {
        /* Descramble succeeded: unpack tones for playback after animation */
        mk_unpack_tone_data(candidate, mk.tone_freqs, mk.tone_durs);
        mk.tone_len = MK_COMPOSITION_LEN;
        mk.tone_index = 0;
        mk.tone_tick = 0;
        mk.tone_playing = 0;  /* Will start at RESULT phase */
        mk.tone_error = 0;

        /* Generate comp[] for visualizer display */
        mk_generate_composition(user, pass, mk.anim_comp, mk.anim_dur,
                                mk.tone_freqs, /* reuse, already filled */
                                &mk.anim_entropy, &mk.analysis_key,
                                mk.analysis_scores);
    } else {
        /* Wrong passphrase: prepare error buzz */
        mk.tone_error = 1;
        mk.tone_playing = 0;  /* Will start at RESULT phase */
        mk.tone_len = 4;
        mk.tone_index = 0;
        mk.tone_tick = 0;

        /* Generate a composition from the entered creds for visualizer */
        uint16_t dummy_freqs[MK_COMPOSITION_LEN];
        mk_generate_composition(user, pass, mk.anim_comp, mk.anim_dur,
                                dummy_freqs, &mk.anim_entropy,
                                &mk.analysis_key, mk.analysis_scores);
    }

    mk_build_key_name(mk.analysis_key);

    /* Start animation */
    speaker_stop();
    mk.anim_phase = MK_ANIM_GENERATING;
    mk.anim_tick = 0;
    mk.anim_progress = 0;
    mk.show_analysis = 0;
    str_copy(mk.status_msg, "Descrambling tone data...", 128);
    mk.status_color = COLOR_ICON_PURPLE;

    mk_update_visualizer(mk.anim_comp, MK_COMPOSITION_LEN);
    mk_update_contour(mk.anim_comp, MK_COMPOSITION_LEN);
}

static void mk_do_preview(void)
{
    const char *user = widget_textinput_text(&mk.username_input);
    const char *pass = widget_textinput_text(&mk.passphrase_input);

    if (!user[0]) {
        str_copy(mk.status_msg, "Enter username for preview", 128);
        mk.status_color = COLOR_HIGHLIGHT;
        return;
    }

    int ent, skey, scores[MK_NUM_SCORES];
    uint16_t preview_freqs[MK_COMPOSITION_LEN];
    mk_generate_composition(user, pass[0] ? pass : "preview",
                            mk.preview_comp, mk.preview_dur, preview_freqs,
                            &ent, &skey, scores);
    mk.preview_len = MK_COMPOSITION_LEN;
    mk.preview_playing = 1;
    mk.preview_pos = 0;
    mk.preview_tick = 0;
    mk_update_visualizer(mk.preview_comp, MK_COMPOSITION_LEN);
    mk_update_contour(mk.preview_comp, MK_COMPOSITION_LEN);

    /* Start speaker playback alongside visual preview */
    speaker_stop();
    memcpy(mk.tone_freqs, preview_freqs, sizeof(preview_freqs));
    memcpy(mk.tone_durs, mk.preview_dur, MK_COMPOSITION_LEN);
    mk.tone_len = MK_COMPOSITION_LEN;
    mk.tone_index = 0;
    mk.tone_tick = 0;
    mk.tone_playing = 1;
    mk.tone_error = 0;
    speaker_play_tone(mk.tone_freqs[0]);

    /* Show analysis for preview too */
    for (int i = 0; i < MK_NUM_SCORES; i++)
        mk.analysis_scores[i] = scores[i];
    mk.analysis_key = skey;
    mk_build_key_name(skey);
    mk.show_analysis = 1;

    str_copy(mk.status_msg, "Playing key tones...", 128);
    mk.status_color = COLOR_ICON_PURPLE;
}

/* Paint callback */
static void musikey_paint(struct wm_window *win)
{
    int cw = wm_content_width(win);
    int ch = wm_content_height(win);
    int ox = win->x;
    int oy = win->y + WM_TITLE_HEIGHT;

    /* === HEADER (gradient) === */
    gfx_fill_gradient_v(ox, oy, cw, 30, 0xFF0D1117, 0xFF162040);
    gfx_draw_text_scaled(ox + 8, oy + 4, "MusiKey", COLOR_HIGHLIGHT, 0, 2);
    widget_label(win, 130, 8, "Musical Authentication", COLOR_TEXT_DIM);

    /* === STATUS INDICATOR === */
    gfx_fill_rounded_rect(ox + 6, oy + 32, 10, 10, 3, mk.status_color);
    widget_label(win, 20, 32, mk.status_msg, mk.status_color);

    /* === LEFT PANEL: Enrolled Users === */
    int left_w = 130;
    gfx_fill_rounded_rect(ox + 2, oy + 44, left_w - 4, 74, 4, 0xFF0F1218);
    gfx_draw_vline(ox + left_w, oy + 44, 74, COLOR_PANEL_BORDER);

    widget_label(win, 4, 46, "Enrolled Users", COLOR_TEXT);
    widget_label(win, 4, 62, "User", COLOR_TEXT_DIM);
    widget_label(win, 70, 62, "Bits", COLOR_TEXT_DIM);
    gfx_draw_hline(ox + 2, oy + 74, left_w - 4, COLOR_PANEL_BORDER);

    int uy = 78;
    for (int i = 0; i < mk.user_count && i < MK_MAX_USERS; i++) {
        if (!mk.users[i].enrolled) continue;
        char uname[10];
        strncpy(uname, mk.users[i].username, 8);
        uname[8] = '\0';
        widget_label(win, 4, uy, uname, COLOR_TEXT);

        char ebuf[8];
        int e = mk.users[i].entropy_bits;
        int ep = 0;
        if (e >= 100) ebuf[ep++] = '0' + (char)(e / 100);
        if (e >= 10) ebuf[ep++] = '0' + (char)((e / 10) % 10);
        ebuf[ep++] = '0' + (char)(e % 10);
        ebuf[ep] = '\0';
        widget_label(win, 74, uy, ebuf, COLOR_GREEN_ACTIVE);
        uy += 16;
    }

    /* === CENTER FORM AREA === */
    int form_x = left_w + 4;

    widget_label(win, form_x, 46, "Username:", COLOR_TEXT_DIM);
    mk.username_input.x = form_x + 80;
    mk.username_input.y = 44;
    widget_textinput_draw(win, &mk.username_input);

    widget_label(win, form_x, 70, "Passphrase:", COLOR_TEXT_DIM);
    mk.passphrase_input.x = form_x + 80;
    mk.passphrase_input.y = 68;
    widget_textinput_draw(win, &mk.passphrase_input);

    /* Buttons */
    mk.enroll_btn.x = form_x + 80;
    mk.enroll_btn.y = 92;
    mk.auth_btn.x = form_x + 146;
    mk.auth_btn.y = 92;
    mk.play_btn.x = form_x + 236;
    mk.play_btn.y = 92;
    widget_button_draw(win, &mk.enroll_btn);
    widget_button_draw(win, &mk.auth_btn);
    widget_button_draw(win, &mk.play_btn);

    /* Entropy display */
    widget_label(win, cw - 110, 94, "Entropy:", COLOR_TEXT_DIM);
    if (mk.user_count > 0) {
        char ebuf[16];
        int last = mk.user_count - 1;
        int e = mk.users[last].entropy_bits;
        int ep = 0;
        if (e >= 100) ebuf[ep++] = '0' + (char)(e / 100);
        if (e >= 10) ebuf[ep++] = '0' + (char)((e / 10) % 10);
        ebuf[ep++] = '0' + (char)(e % 10);
        ebuf[ep++] = ' '; ebuf[ep++] = 'b'; ebuf[ep] = '\0';
        widget_label(win, cw - 40, 94, ebuf, COLOR_TEXT);
    } else {
        widget_label(win, cw - 40, 94, "-- b", COLOR_TEXT_DIM);
    }

    /* === SCALE / KEY INFO === */
    int info_y = 122;
    gfx_draw_hline(ox + 2, oy + info_y - 2, cw - 4, COLOR_PANEL_BORDER);
    widget_label(win, 4, info_y, "Key:", COLOR_TEXT_DIM);
    if (mk.show_analysis) {
        widget_label(win, 36, info_y, mk.analysis_key_name, COLOR_ICON_PURPLE);
    } else {
        widget_label(win, 36, info_y, "-- --", COLOR_TEXT_DIM);
    }
    widget_label(win, 160, info_y, "Scale: Pentatonic", COLOR_TEXT_DIM);
    widget_label(win, 380, info_y, "Notes: 32", COLOR_TEXT_DIM);

    /* === MELODY CONTOUR WAVEFORM === */
    int contour_y = 140;
    int contour_h = 36;
    gfx_fill_rounded_rect(ox + 2, oy + contour_y, cw - 4, contour_h, 4, 0xFF0A0E1A);
    widget_label(win, 4, contour_y + 2, "Contour", COLOR_TEXT_DIM);

    if (mk.contour_len > 0) {
        int cx_start = 60;
        int cx_range = cw - cx_start - 8;
        int cy_base = oy + contour_y + contour_h - 4;
        int cy_range = contour_h - 10;
        int prev_px = -1, prev_py = -1;

        for (int i = 0; i < mk.contour_len; i++) {
            int px = ox + cx_start + i * cx_range / mk.contour_len;
            int note_val = mk.contour_notes[i];
            int py = cy_base - note_val * cy_range / MK_PIANO_KEYS;

            /* Draw dot */
            fb_fill_rect((uint32_t)(px - 1), (uint32_t)(py - 1), 3, 3,
                         COLOR_ICON_PURPLE);

            /* Connect to previous */
            if (prev_px >= 0)
                gfx_draw_line(prev_px, prev_py, px, py, COLOR_ACCENT);
            prev_px = px;
            prev_py = py;
        }
    }

    /* === AUDIO VISUALIZER (gradient bars) === */
    int vis_y = contour_y + contour_h + 4;
    int vis_h = 36;
    gfx_fill_rounded_rect(ox + 2, oy + vis_y, cw - 4, vis_h, 4, 0xFF0A0E1A);
    widget_label(win, 4, vis_y + 2, "Visualizer", COLOR_TEXT_DIM);

    int bar_area_x = ox + 4;
    int bar_area_w = cw - 8;
    int bar_w = bar_area_w / MK_VIS_BARS;
    if (bar_w < 2) bar_w = 2;
    int bar_max_h = vis_h - 14;

    for (int i = 0; i < MK_VIS_BARS; i++) {
        int bh = mk.vis_bars[i] * bar_max_h / 15;
        if (bh < 1) bh = 1;
        int bx = bar_area_x + i * bar_w;
        int by = oy + vis_y + vis_h - 2 - bh;
        uint32_t color_top = COLOR_GREEN_ACTIVE;
        uint32_t color_bot = 0xFF0A6630;

        if (mk.preview_playing && i == mk.preview_pos * MK_VIS_BARS / MK_COMPOSITION_LEN) {
            color_top = COLOR_HIGHLIGHT;
            color_bot = 0xFF991133;
        }

        if (bh > 2)
            gfx_fill_gradient_v(bx, by, bar_w - 1, bh, color_top, color_bot);
        else
            fb_fill_rect((uint32_t)bx, (uint32_t)by, (uint32_t)(bar_w - 1),
                         (uint32_t)bh, color_top);
    }

    /* === PIANO KEYBOARD (with note labels) === */
    int piano_y = vis_y + vis_h + 4;
    int piano_h = 100;

    gfx_fill_rounded_rect(ox + 2, oy + piano_y - 2, cw - 4, piano_h + 6, 4, 0xFF0A0E1A);
    widget_label(win, 4, piano_y, "Piano", COLOR_TEXT_DIM);

    int key_area_y = piano_y + 14;
    int key_area_h = piano_h - 16;
    int white_w = (cw - 8) / MK_PIANO_KEYS;
    if (white_w < 10) white_w = 10;
    int black_w = white_w * 6 / 10;
    int black_h = key_area_h * 6 / 10;
    int key_start_x = 4;

    /* Draw white keys with note labels */
    for (int i = 0; i < MK_PIANO_KEYS; i++) {
        int kx = ox + key_start_x + i * white_w;
        int ky = oy + key_area_y;
        uint32_t color = COLOR_WHITE;

        if (i == mk.key_pressed)
            color = COLOR_GREEN_ACTIVE;
        if (mk.preview_playing && mk.preview_pos < mk.preview_len) {
            int note = (int)(unsigned char)mk.preview_comp[mk.preview_pos];
            if (note == i) color = COLOR_ICON_PURPLE;
        }

        gfx_fill_rounded_rect(kx, ky, white_w - 1, key_area_h, 2, color);
        gfx_draw_rounded_rect(kx, ky, white_w - 1, key_area_h, 2, 0xFF333333);

        /* Note label at bottom */
        uint32_t label_col = (color == COLOR_WHITE) ? 0xFF555555 : 0xFF222222;
        int lx = kx + (white_w - 1) / 2 - 3;
        int ly = ky + key_area_h - 14;
        font_draw_char((uint32_t)lx, (uint32_t)ly, mk_white_labels[i],
                       label_col, color);
    }

    /* Draw black keys on top */
    for (int i = 0; i < MK_PIANO_KEYS - 1; i++) {
        if (!mk_has_black(i)) continue;
        int kx = ox + key_start_x + i * white_w + white_w - black_w / 2;
        int ky = oy + key_area_y;
        uint32_t color = 0xFF1A1A1A;
        if (mk.black_pressed >= 0) {
            int black_idx = 0;
            for (int j = 0; j < i; j++)
                if (mk_has_black(j)) black_idx++;
            if (black_idx == mk.black_pressed)
                color = COLOR_ICON_PURPLE;
        }
        gfx_fill_rounded_rect(kx, ky, black_w, black_h, 2, color);
        gfx_draw_rounded_rect(kx, ky, black_w, black_h, 2, 0xFF444444);
    }

    /* === MUSICALITY ANALYSIS PANEL (conditional) === */
    int analysis_y = piano_y + piano_h + 8;
    if (mk.show_analysis) {
        gfx_fill_rounded_rect(ox + 2, oy + analysis_y, cw - 4, 90, 4, 0xFF111827);
        gfx_draw_rounded_rect(ox + 2, oy + analysis_y, cw - 4, 90, 4,
                              COLOR_PANEL_BORDER);
        widget_label(win, 8, analysis_y + 4, "MUSICALITY ANALYSIS", COLOR_ICON_PURPLE);

        widget_label(win, cw - 160, analysis_y + 4, "Key:", COLOR_TEXT_DIM);
        widget_label(win, cw - 128, analysis_y + 4, mk.analysis_key_name, COLOR_TEXT);

        int ay = analysis_y + 22;
        const char *slabels[MK_NUM_SCORES] = {
            "Harmonic:", "Melodic:", "Rhythm:", "Scale:"
        };
        uint32_t scolors[MK_NUM_SCORES] = {
            0xFF3B82F6, COLOR_GREEN_ACTIVE, COLOR_ICON_ORANGE, COLOR_ICON_PURPLE
        };
        for (int s = 0; s < MK_NUM_SCORES; s++) {
            widget_label(win, 8, ay, slabels[s], COLOR_TEXT_DIM);
            widget_progress(win, 80, ay + 2, cw / 2 - 100, 10,
                            mk.analysis_scores[s], scolors[s], 0xFF0D0D1A);
            /* Score number */
            char sbuf[8];
            int sv = mk.analysis_scores[s];
            int sp = 0;
            if (sv >= 100) sbuf[sp++] = '1';
            if (sv >= 10) sbuf[sp++] = '0' + (char)((sv / 10) % 10);
            sbuf[sp++] = '0' + (char)(sv % 10);
            sbuf[sp++] = '%';
            sbuf[sp] = '\0';
            widget_label(win, cw / 2 - 10, ay, sbuf, COLOR_TEXT);
            ay += 16;
        }
    }

    /* === AUTH ANIMATION PROGRESS BAR (conditional) === */
    int anim_bar_y = mk.show_analysis ? analysis_y + 94 : analysis_y;
    if (mk.anim_phase != MK_ANIM_NONE && mk.anim_phase != MK_ANIM_RESULT) {
        gfx_fill_rounded_rect(ox + 2, oy + anim_bar_y, cw - 4, 20, 4, 0xFF111827);
        const char *plabels[] = { "", "Descrambling...", "Analyzing tones...",
                                  "Verifying...", "" };
        widget_label(win, 8, anim_bar_y + 3, plabels[mk.anim_phase], COLOR_TEXT_DIM);
        widget_progress(win, 120, anim_bar_y + 5, cw - 140, 10,
                        mk.anim_progress, COLOR_ICON_PURPLE, 0xFF0D0D1A);
    }

    /* === SPEAKER INDICATOR === */
    if (mk.tone_playing) {
        uint32_t tcol = mk.tone_error ? COLOR_HIGHLIGHT : COLOR_GREEN_ACTIVE;
        int sx = cw - 100;
        gfx_fill_rounded_rect(ox + sx, oy + 32, 92, 14, 4, 0xFF1A2233);
        widget_label(win, sx + 4, 33, mk.tone_error ? "Error Tone" : "Speaker ON", tcol);
    }

    /* === FOOTER === */
    int footer_y = ch - 24;
    widget_label(win, 4, footer_y,
                 "MusiKey: tone-based key with scramble/descramble.", COLOR_TEXT_DIM);
    widget_label(win, 4, footer_y + 12,
                 "PC speaker plays descrambled musical key on auth.", COLOR_TEXT_DIM);

    /* === ADVANCE PREVIEW ANIMATION === */
    if (mk.preview_playing) {
        mk.preview_tick++;
        /* Variable tick rate based on duration */
        int tick_limit = 8;
        if (mk.preview_pos < mk.preview_len) {
            int d = (int)(unsigned char)mk.preview_dur[mk.preview_pos];
            if (d >= 1 && d <= 3)
                tick_limit = 4 + d * 3; /* SHORT=7, NORMAL=10, LONG=13 */
        }
        if (mk.preview_tick >= tick_limit) {
            mk.preview_tick = 0;
            mk.preview_pos++;
            mk.key_pressed = (mk.preview_pos < mk.preview_len) ?
                             (int)(unsigned char)mk.preview_comp[mk.preview_pos] : -1;
            if (mk.preview_pos >= mk.preview_len) {
                mk.preview_playing = 0;
                mk.key_pressed = -1;
                str_copy(mk.status_msg, "Playback complete", 128);
                mk.status_color = COLOR_GREEN_ACTIVE;
            }
        }
    }
}

/* Click handler */
static void musikey_click(struct wm_window *win, int x, int y, int button)
{
    (void)button;
    int cw = wm_content_width(win);
    (void)cw;

    /* Block clicks during animation */
    if (mk.anim_phase != MK_ANIM_NONE && mk.anim_phase != MK_ANIM_RESULT)
        return;

    /* Username input */
    if (x >= mk.username_input.x && x < mk.username_input.x + mk.username_input.w &&
        y >= mk.username_input.y && y < mk.username_input.y + mk.username_input.h) {
        mk.active_field = 0;
        widget_textinput_click(&mk.username_input, x, y);
        return;
    }

    /* Passphrase input */
    if (x >= mk.passphrase_input.x &&
        x < mk.passphrase_input.x + mk.passphrase_input.w &&
        y >= mk.passphrase_input.y &&
        y < mk.passphrase_input.y + mk.passphrase_input.h) {
        mk.active_field = 1;
        widget_textinput_click(&mk.passphrase_input, x, y);
        return;
    }

    /* Enroll button */
    if (widget_button_hit(&mk.enroll_btn, x, y)) {
        mk.anim_phase = MK_ANIM_NONE;
        speaker_stop(); mk.tone_playing = 0;
        mk_do_enroll();
        return;
    }

    /* Authenticate button */
    if (widget_button_hit(&mk.auth_btn, x, y)) {
        mk.anim_phase = MK_ANIM_NONE;
        speaker_stop(); mk.tone_playing = 0;
        mk_do_authenticate();
        return;
    }

    /* Play Preview button */
    if (widget_button_hit(&mk.play_btn, x, y)) {
        mk.anim_phase = MK_ANIM_NONE;
        speaker_stop(); mk.tone_playing = 0;
        mk_do_preview();
        return;
    }

    /* Piano key click (updated Y offsets for new layout) */
    int contour_y = 140;
    int contour_h = 36;
    int vis_y_loc = contour_y + contour_h + 4;
    int vis_h_loc = 36;
    int piano_y = vis_y_loc + vis_h_loc + 4;
    int piano_h = 100;
    int key_area_y = piano_y + 14;
    int key_area_h = piano_h - 16;
    int white_w = (wm_content_width(win) - 8) / MK_PIANO_KEYS;
    if (white_w < 10) white_w = 10;
    int key_start_x = 4;

    if (y >= key_area_y && y < key_area_y + key_area_h) {
        int black_w = white_w * 6 / 10;
        int black_kh = key_area_h * 6 / 10;
        if (y < key_area_y + black_kh) {
            for (int i = 0; i < MK_PIANO_KEYS - 1; i++) {
                if (!mk_has_black(i)) continue;
                int kx = key_start_x + i * white_w + white_w - black_w / 2;
                if (x >= kx && x < kx + black_w) {
                    mk.key_pressed = -1;
                    int bidx = 0;
                    for (int j = 0; j < i; j++)
                        if (mk_has_black(j)) bidx++;
                    mk.black_pressed = bidx;
                    return;
                }
            }
        }

        int key_idx = (x - key_start_x) / white_w;
        if (key_idx >= 0 && key_idx < MK_PIANO_KEYS) {
            mk.key_pressed = key_idx;
            mk.black_pressed = -1;
            return;
        }
    }

    mk.key_pressed = -1;
    mk.black_pressed = -1;
}

/* Key handler */
static void musikey_key(struct wm_window *win, int key)
{
    (void)win;

    /* Block keys during animation */
    if (mk.anim_phase != MK_ANIM_NONE && mk.anim_phase != MK_ANIM_RESULT) {
        if (key == '\n') return; /* Ignore enter during animation */
    }

    /* Tab switches between fields */
    if (key == '\t' || key == KEY_TAB) {
        mk.active_field = 1 - mk.active_field;
        return;
    }

    /* Enter triggers authenticate */
    if (key == '\n') {
        mk.anim_phase = MK_ANIM_NONE;
        speaker_stop(); mk.tone_playing = 0;
        mk_do_authenticate();
        return;
    }

    /* Route keys to active field */
    if (mk.active_field == 0) {
        widget_textinput_key(&mk.username_input, key);
    } else {
        widget_textinput_key(&mk.passphrase_input, key);
    }
}

/*============================================================================
 * App Launch Callbacks
 *============================================================================*/

static void launch_files(void)
{
    if (filebrowser_win > 0) return;
    filebrowser_win = wm_create_window(160, 60, 400, 420, "File Browser");
    if (filebrowser_win > 0) {
        wm_set_on_close(filebrowser_win, desktop_on_close);
        wm_set_on_paint(filebrowser_win, filebrowser_paint);
        wm_set_on_click(filebrowser_win, filebrowser_click);
        wm_set_on_key(filebrowser_win, filebrowser_key);
        fb_init_state();
    }
}

static void launch_terminal(void)
{
    if (terminal_win > 0) return;
    terminal_win = wm_create_window(140, 80, 560, 360, "Terminal");
    if (terminal_win > 0) {
        wm_set_on_close(terminal_win, desktop_on_close);
        wm_set_on_paint(terminal_win, terminal_paint);
        wm_set_on_key(terminal_win, terminal_key);
        wm_set_on_click(terminal_win, terminal_click);
    }
}

static void launch_ai(void)
{
    active_input = 1;
}

static void launch_settings(void)
{
    if (settings_win > 0) return;
    settings_win = wm_create_window(250, 100, 280, 280, "Settings");
    if (settings_win > 0) {
        wm_set_on_close(settings_win, desktop_on_close);
        wm_set_on_paint(settings_win, settings_paint);
    }
}

static void launch_security(void)
{
    if (security_win > 0) return;
    security_win = wm_create_window(220, 80, 300, 360, "Security");
    if (security_win > 0) {
        wm_set_on_close(security_win, desktop_on_close);
        wm_set_on_paint(security_win, security_paint);
    }
}

static void launch_sysinfo(void)
{
    if (sysinfo_win > 0) return;
    sysinfo_win = wm_create_window(160, 60, 260, 300, "System Monitor");
    if (sysinfo_win > 0) {
        wm_set_on_close(sysinfo_win, desktop_on_close);
        wm_set_on_paint(sysinfo_win, sysinfo_paint);
    }
}

static void launch_processes(void)
{
    if (processes_win > 0) return;
    processes_win = wm_create_window(200, 90, 280, 280, "Processes");
    if (processes_win > 0) {
        wm_set_on_close(processes_win, desktop_on_close);
        wm_set_on_paint(processes_win, processes_paint);
    }
}

static void launch_governor(void)
{
    if (governor_win > 0) return;
    governor_win = wm_create_window(150, 50, 450, 520, "AI Governor");
    if (governor_win > 0) {
        wm_set_on_close(governor_win, desktop_on_close);
        wm_set_on_paint(governor_win, governor_paint);
        wm_set_on_click(governor_win, governor_click);
        wm_set_on_key(governor_win, governor_key);
        gov_ui_init();
    }
}

static void launch_geology(void)
{
    if (geology_win > 0) return;
    geology_win = wm_create_window(120, 50, 580, 440, "Geology Viewer");
    if (geology_win > 0) {
        wm_set_on_close(geology_win, desktop_on_close);
        wm_set_on_paint(geology_win, geology_paint);
        wm_set_on_click(geology_win, geology_click);
        wm_set_on_key(geology_win, geology_key);
        geo_init_state();
    }
}

static void launch_constitution(void)
{
    if (constitution_win > 0) return;
    constitution_win = wm_create_window(170, 60, 320, 400, "Constitution");
    if (constitution_win > 0) {
        wm_set_on_close(constitution_win, desktop_on_close);
        wm_set_on_paint(constitution_win, constitution_paint);
    }
}

static void launch_network(void)
{
    if (network_win > 0) return;
    network_win = wm_create_window(230, 100, 280, 340, "Network");
    if (network_win > 0) {
        wm_set_on_close(network_win, desktop_on_close);
        wm_set_on_paint(network_win, network_paint);
    }
}

static void launch_artos(void)
{
    if (artos_win > 0) return;
    artos_win = wm_create_window(60, 20, 680, 580, "ArtOS - Digital Art Studio v2");
    if (artos_win > 0) {
        wm_set_on_close(artos_win, desktop_on_close);
        wm_set_on_paint(artos_win, artos_paint);
        wm_set_on_click(artos_win, artos_click);
        wm_set_on_key(artos_win, artos_key);
        artos_init_state();
    }
}

static void launch_musikey(void)
{
    if (musikey_win > 0) return;
    musikey_win = wm_create_window(60, 30, 600, 520, "MusiKey - Musical Authentication");
    if (musikey_win > 0) {
        wm_set_on_close(musikey_win, desktop_on_close);
        wm_set_on_paint(musikey_win, musikey_paint);
        wm_set_on_click(musikey_win, musikey_click);
        wm_set_on_key(musikey_win, musikey_key);
        mk_init_state();
    }
}

static void launch_dnauth(void)
{
    if (dnauth_win > 0) return;
    dnauth_win = wm_create_window(160, 60, 300, 440, "DNAuth - DNA Authentication");
    if (dnauth_win > 0) {
        wm_set_on_close(dnauth_win, desktop_on_close);
        wm_set_on_paint(dnauth_win, dnauth_paint);
        wm_set_on_click(dnauth_win, dnauth_click);
        dna_init_state();
    }
}

static void launch_lifeauth(void)
{
    if (lifeauth_win > 0) return;
    lifeauth_win = wm_create_window(180, 50, 300, 420, "LifeAuth - Life Sign Auth");
    if (lifeauth_win > 0) {
        wm_set_on_close(lifeauth_win, desktop_on_close);
        wm_set_on_paint(lifeauth_win, lifeauth_paint);
        wm_set_on_click(lifeauth_win, lifeauth_click);
        life_init_state();
    }
}

static void launch_biosense(void)
{
    if (biosense_win > 0) return;
    biosense_win = wm_create_window(200, 70, 320, 460, "BioSense - Vein Scanner");
    if (biosense_win > 0) {
        wm_set_on_close(biosense_win, desktop_on_close);
        wm_set_on_paint(biosense_win, biosense_paint);
        wm_set_on_click(biosense_win, biosense_click);
        bio_init_state();
    }
}

static void launch_pve(void)
{
    if (pve_win > 0) return;
    pve_init_state();
    pve_win = wm_create_window(140, 100, 300, 330, "PVE Encryption");
    if (pve_win > 0) {
        wm_set_on_close(pve_win, desktop_on_close);
        wm_set_on_paint(pve_win, pve_paint);
        wm_set_on_click(pve_win, pve_click);
        wm_set_on_key(pve_win, pve_key);
    }
}

static void launch_qrnet(void)
{
    if (qrnet_win > 0) return;
    qrnet_win = wm_create_window(170, 50, 300, 460, "QRNet - QR Networking");
    if (qrnet_win > 0) {
        wm_set_on_close(qrnet_win, desktop_on_close);
        wm_set_on_paint(qrnet_win, qrnet_paint);
        wm_set_on_click(qrnet_win, qrnet_click);
        qr_init_state();
    }
}

static void launch_notes(void)
{
    if (notes_win > 0) return;
    notes_win = wm_create_window(120, 40, 400, 360, "Notes");
    if (notes_win > 0) {
        wm_set_on_close(notes_win, desktop_on_close);
        wm_set_on_paint(notes_win, notes_paint);
        wm_set_on_click(notes_win, notes_click);
        wm_set_on_key(notes_win, notes_key);
        notes_init_state();
    }
}

static void launch_media(void)
{
    if (media_win > 0) return;
    media_win = wm_create_window(150, 60, 300, 440, "Media Player");
    if (media_win > 0) {
        wm_set_on_close(media_win, desktop_on_close);
        wm_set_on_paint(media_win, media_paint);
        wm_set_on_click(media_win, media_click);
        media_init_state();
    }
}

static void launch_users(void)
{
    if (users_win > 0) return;
    users_win = wm_create_window(190, 80, 340, 340, "User Management");
    if (users_win > 0) {
        wm_set_on_close(users_win, desktop_on_close);
        wm_set_on_paint(users_win, users_paint);
        wm_set_on_click(users_win, users_click);
        usr_init_state();
    }
}

static void launch_pods(void)
{
    if (pods_win > 0) return;
    pods_win = wm_create_window(130, 50, 360, 380, "PhantomPods");
    if (pods_win > 0) {
        wm_set_on_close(pods_win, desktop_on_close);
        wm_set_on_paint(pods_win, pods_paint);
        pod_init_state();
    }
}

static void launch_backup(void)
{
    if (backup_win > 0) return;
    backup_win = wm_create_window(200, 70, 320, 400, "Backup Manager");
    if (backup_win > 0) {
        wm_set_on_close(backup_win, desktop_on_close);
        wm_set_on_paint(backup_win, backup_paint);
        wm_set_on_click(backup_win, backup_click);
        bkp_init_state();
    }
}

static void launch_desktoplab(void)
{
    if (desktoplab_win > 0) return;
    desktoplab_win = wm_create_window(160, 60, 300, 380, "Desktop Lab");
    if (desktoplab_win > 0) {
        wm_set_on_close(desktoplab_win, desktop_on_close);
        wm_set_on_paint(desktoplab_win, desktoplab_paint);
        wm_set_on_click(desktoplab_win, desktoplab_click);
        lab_init_state();
    }
}

/*============================================================================
 * GPU Monitor App
 * Real-time GPU acceleration statistics and ring buffer state
 *============================================================================*/

static void gpu_monitor_paint(struct wm_window *win)
{
    int cw = wm_content_width(win);
    int y = 8;
    char buf[80];
    int pos;

    widget_label(win, 8, y, "GPU MONITOR", COLOR_HIGHLIGHT);
    y += 24;

    /* Active backend name */
    const char *backend_name = gpu_hal_get_active_name();
    enum gpu_backend_type btype = gpu_hal_get_active_type();

    widget_label(win, 8, y, "Backend:", COLOR_TEXT_DIM);
    y += 18;
    pos = 0;
    buf[pos++] = ' '; buf[pos++] = ' ';
    const char *bn = backend_name;
    while (*bn && pos < 70) buf[pos++] = *bn++;
    buf[pos] = '\0';

    /* Color code by type */
    uint32_t type_color;
    switch (btype) {
    case GPU_BACKEND_INTEL:    type_color = 0xFF4488FF; break;
    case GPU_BACKEND_VIRTIO:   type_color = 0xFF44CC88; break;
    case GPU_BACKEND_VMWARE:   type_color = 0xFF88CC44; break;
    case GPU_BACKEND_BOCHS:    type_color = 0xFFCC8844; break;
    default:                   type_color = 0xFF888888; break;
    }
    widget_label(win, 8, y, buf, type_color);
    y += 18;

    /* Status indicator */
    widget_label(win, 8, y, "Status:", COLOR_TEXT_DIM);
    y += 18;
    widget_label(win, 8, y, gpu_hal_available() ?
        "  Active" : "  Inactive", gpu_hal_available() ?
        0xFF00CC66 : 0xFFCC3333);
    y += 24;

    /* Show PCI VGA device info */
    const struct pci_device *vga = pci_find_device(0x03, 0x00);
    if (vga) {
        widget_label(win, 8, y, "PCI Device:", COLOR_TEXT_DIM);
        y += 18;
        pos = 0;
        buf[pos++] = ' '; buf[pos++] = ' ';
        buf[pos++] = '0'; buf[pos++] = 'x';
        buf[pos++] = "0123456789ABCDEF"[(vga->vendor_id >> 12) & 0xF];
        buf[pos++] = "0123456789ABCDEF"[(vga->vendor_id >> 8) & 0xF];
        buf[pos++] = "0123456789ABCDEF"[(vga->vendor_id >> 4) & 0xF];
        buf[pos++] = "0123456789ABCDEF"[vga->vendor_id & 0xF];
        buf[pos++] = ':';
        buf[pos++] = '0'; buf[pos++] = 'x';
        buf[pos++] = "0123456789ABCDEF"[(vga->device_id >> 12) & 0xF];
        buf[pos++] = "0123456789ABCDEF"[(vga->device_id >> 8) & 0xF];
        buf[pos++] = "0123456789ABCDEF"[(vga->device_id >> 4) & 0xF];
        buf[pos++] = "0123456789ABCDEF"[vga->device_id & 0xF];
        buf[pos] = '\0';
        widget_label(win, 8, y, buf, COLOR_TEXT);
        y += 22;
    }

    /* Separator */
    gfx_fill_rounded_rect(
        win->x + WM_BORDER_WIDTH + 8,
        win->y + WM_TITLE_HEIGHT + y,
        cw - 16, 1, 0, 0xFF333333);
    y += 8;

    /* HAL Statistics */
    struct gpu_stats stats;
    gpu_hal_get_stats(&stats);

    widget_label(win, 8, y, "GPU STATISTICS", COLOR_HIGHLIGHT);
    y += 22;

    /* Fill ops */
    {
        uint64_t v = stats.fills;
        pos = 0;
        buf[pos++] = 'F'; buf[pos++] = 'i'; buf[pos++] = 'l';
        buf[pos++] = 'l'; buf[pos++] = 's'; buf[pos++] = ':';
        buf[pos++] = ' ';
        if (v >= 10000) { buf[pos++] = '0' + (char)((v / 10000) % 10); }
        if (v >= 1000) { buf[pos++] = '0' + (char)((v / 1000) % 10); }
        if (v >= 100) { buf[pos++] = '0' + (char)((v / 100) % 10); }
        if (v >= 10) { buf[pos++] = '0' + (char)((v / 10) % 10); }
        buf[pos++] = '0' + (char)(v % 10);
        buf[pos] = '\0';
        widget_label(win, 8, y, buf, COLOR_TEXT);
    }
    y += 16;

    /* Flip ops */
    {
        uint64_t v = stats.flips;
        pos = 0;
        buf[pos++] = 'F'; buf[pos++] = 'l'; buf[pos++] = 'i';
        buf[pos++] = 'p'; buf[pos++] = 's'; buf[pos++] = ':';
        buf[pos++] = ' ';
        if (v >= 10000) { buf[pos++] = '0' + (char)((v / 10000) % 10); }
        if (v >= 1000) { buf[pos++] = '0' + (char)((v / 1000) % 10); }
        if (v >= 100) { buf[pos++] = '0' + (char)((v / 100) % 10); }
        if (v >= 10) { buf[pos++] = '0' + (char)((v / 10) % 10); }
        buf[pos++] = '0' + (char)(v % 10);
        buf[pos] = '\0';
        widget_label(win, 8, y, buf, COLOR_TEXT);
    }
    y += 16;

    /* Screen copies */
    {
        uint64_t v = stats.screen_copies;
        pos = 0;
        buf[pos++] = 'C'; buf[pos++] = 'o'; buf[pos++] = 'p';
        buf[pos++] = 'i'; buf[pos++] = 'e'; buf[pos++] = 's';
        buf[pos++] = ':'; buf[pos++] = ' ';
        if (v >= 10000) { buf[pos++] = '0' + (char)((v / 10000) % 10); }
        if (v >= 1000) { buf[pos++] = '0' + (char)((v / 1000) % 10); }
        if (v >= 100) { buf[pos++] = '0' + (char)((v / 100) % 10); }
        if (v >= 10) { buf[pos++] = '0' + (char)((v / 10) % 10); }
        buf[pos++] = '0' + (char)(v % 10);
        buf[pos] = '\0';
        widget_label(win, 8, y, buf, COLOR_TEXT);
    }
    y += 16;

    /* Batched ops */
    {
        uint64_t v = stats.batched_ops;
        pos = 0;
        buf[pos++] = 'B'; buf[pos++] = 'a'; buf[pos++] = 't';
        buf[pos++] = 'c'; buf[pos++] = 'h'; buf[pos++] = 'e';
        buf[pos++] = 'd'; buf[pos++] = ':'; buf[pos++] = ' ';
        if (v >= 10000) { buf[pos++] = '0' + (char)((v / 10000) % 10); }
        if (v >= 1000) { buf[pos++] = '0' + (char)((v / 1000) % 10); }
        if (v >= 100) { buf[pos++] = '0' + (char)((v / 100) % 10); }
        if (v >= 10) { buf[pos++] = '0' + (char)((v / 10) % 10); }
        buf[pos++] = '0' + (char)(v % 10);
        buf[pos] = '\0';
        widget_label(win, 8, y, buf, COLOR_TEXT);
    }
    y += 22;

    /* Bytes throughput */
    {
        uint64_t kb = stats.bytes_transferred / 1024;
        uint64_t mb = kb / 1024;
        pos = 0;
        buf[pos++] = 'T'; buf[pos++] = 'h'; buf[pos++] = 'r';
        buf[pos++] = 'o'; buf[pos++] = 'u'; buf[pos++] = 'g';
        buf[pos++] = 'h'; buf[pos++] = 'p'; buf[pos++] = 'u';
        buf[pos++] = 't'; buf[pos++] = ':'; buf[pos++] = ' ';
        if (mb > 0) {
            if (mb >= 10000) buf[pos++] = '0' + (char)((mb / 10000) % 10);
            if (mb >= 1000) buf[pos++] = '0' + (char)((mb / 1000) % 10);
            if (mb >= 100) buf[pos++] = '0' + (char)((mb / 100) % 10);
            if (mb >= 10) buf[pos++] = '0' + (char)((mb / 10) % 10);
            buf[pos++] = '0' + (char)(mb % 10);
            buf[pos++] = ' '; buf[pos++] = 'M'; buf[pos++] = 'B';
        } else {
            if (kb >= 1000) buf[pos++] = '0' + (char)((kb / 1000) % 10);
            if (kb >= 100) buf[pos++] = '0' + (char)((kb / 100) % 10);
            if (kb >= 10) buf[pos++] = '0' + (char)((kb / 10) % 10);
            buf[pos++] = '0' + (char)(kb % 10);
            buf[pos++] = ' '; buf[pos++] = 'K'; buf[pos++] = 'B';
        }
        buf[pos] = '\0';
        widget_label(win, 8, y, buf, COLOR_TEXT);
    }
    y += 16;

    /* SW fallbacks */
    {
        uint64_t v = stats.sw_fallbacks;
        pos = 0;
        buf[pos++] = 'F'; buf[pos++] = 'a'; buf[pos++] = 'l';
        buf[pos++] = 'l'; buf[pos++] = 'b'; buf[pos++] = 'a';
        buf[pos++] = 'c'; buf[pos++] = 'k'; buf[pos++] = 's';
        buf[pos++] = ':'; buf[pos++] = ' ';
        if (v >= 100) buf[pos++] = '0' + (char)((v / 100) % 10);
        if (v >= 10) buf[pos++] = '0' + (char)((v / 10) % 10);
        buf[pos++] = '0' + (char)(v % 10);
        buf[pos] = '\0';
        widget_label(win, 8, y, buf, v > 0 ? 0xFFCC6633 : 0xFF00CC66);
    }

    (void)cw;
}

static void launch_gpumon(void)
{
    if (gpumon_win > 0) return;
    gpumon_win = wm_create_window(180, 50, 280, 420, "GPU Monitor");
    if (gpumon_win > 0) {
        wm_set_on_close(gpumon_win, desktop_on_close);
        wm_set_on_paint(gpumon_win, gpu_monitor_paint);
    }
}

/*============================================================================
 * VM System Info Window
 *============================================================================*/

/* Resolution button geometry (y positions filled in during paint) */
static struct widget_button res_buttons[4];
static int res_button_count = 0;

static void vminfo_paint(struct wm_window *win)
{
    int y = 8;
    int cw = wm_content_width(win);
    char buf[64];
    int pos;

    widget_label(win, 8, y, "VM SYSTEM INFO", COLOR_HIGHLIGHT);
    y += 24;

    /* Virtualization status */
    widget_label(win, 8, y, "Virtualization:", COLOR_TEXT_DIM);
    y += 16;
    if (vm_is_virtualized()) {
        const char *name = vm_get_type_name();
        pos = 0;
        buf[pos++] = ' '; buf[pos++] = ' ';
        for (int i = 0; name[i] && pos < 50; i++)
            buf[pos++] = name[i];
        buf[pos] = '\0';
        widget_label(win, 8, y, buf, 0xFF00CC66);
    } else {
        widget_label(win, 8, y, "  Bare Metal", 0xFFCCCCCC);
    }
    y += 22;

    /* VM Optimizations */
    widget_label(win, 8, y, "VM Optimizations:", COLOR_TEXT_DIM);
    y += 16;
    if (vm_is_virtualized()) {
        widget_label(win, 8, y, "  Dirty tracking: ON", 0xFF00CC66);
        y += 14;
        widget_label(win, 8, y, "  Frame limiting: ON", 0xFF00CC66);
    } else {
        widget_label(win, 8, y, "  Not active", 0xFF888888);
    }
    y += 22;

    /* GPU Backend */
    widget_label(win, 8, y, "GPU Backend:", COLOR_TEXT_DIM);
    y += 16;
    if (gpu_hal_available()) {
        const char *gname = gpu_hal_get_active_name();
        pos = 0;
        buf[pos++] = ' '; buf[pos++] = ' ';
        for (int i = 0; gname[i] && pos < 50; i++)
            buf[pos++] = gname[i];
        buf[pos] = '\0';
        widget_label(win, 8, y, buf, 0xFF3399FF);
    } else {
        widget_label(win, 8, y, "  Software", 0xFFCC6633);
    }
    y += 22;

    /* Display info */
    widget_label(win, 8, y, "Display:", COLOR_TEXT_DIM);
    y += 16;
    {
        char dbuf[32];
        int dp = 0;
        uint32_t rw = fb_get_width(), rh = fb_get_height();
        dbuf[dp++] = ' '; dbuf[dp++] = ' ';
        if (rw >= 1000) dbuf[dp++] = '0' + (char)(rw / 1000);
        if (rw >= 100) dbuf[dp++] = '0' + (char)((rw / 100) % 10);
        if (rw >= 10) dbuf[dp++] = '0' + (char)((rw / 10) % 10);
        dbuf[dp++] = '0' + (char)(rw % 10);
        dbuf[dp++] = 'x';
        if (rh >= 1000) dbuf[dp++] = '0' + (char)(rh / 1000);
        if (rh >= 100) dbuf[dp++] = '0' + (char)((rh / 100) % 10);
        if (rh >= 10) dbuf[dp++] = '0' + (char)((rh / 10) % 10);
        dbuf[dp++] = '0' + (char)(rh % 10);
        dbuf[dp++] = ' '; dbuf[dp++] = '3'; dbuf[dp++] = '2';
        dbuf[dp++] = 'b'; dbuf[dp++] = 'p'; dbuf[dp++] = 'p';
        dbuf[dp] = '\0';
        widget_label(win, 8, y, dbuf, COLOR_TEXT);
    }
    y += 18;

    /* Resolution picker buttons */
    res_button_count = fb_get_resolution_count();
    if (res_button_count > 4) res_button_count = 4;
    {
        uint32_t cur_w = fb_get_width(), cur_h = fb_get_height();
        int bx = 8;
        for (int i = 0; i < res_button_count; i++) {
            const struct fb_resolution *r = fb_get_resolution(i);
            if (!r) continue;
            int is_current = (r->width == cur_w && r->height == cur_h);
            res_buttons[i].x = bx;
            res_buttons[i].y = y;
            res_buttons[i].w = 62;
            res_buttons[i].h = 18;
            res_buttons[i].text = r->label;
            res_buttons[i].bg_color = is_current ? 0xFF2266AA : 0xFF1A1A2E;
            res_buttons[i].text_color = is_current ? 0xFFFFFFFF : 0xFFAAAAAA;
            widget_button_draw(win, &res_buttons[i]);
            bx += 66;
        }
    }
    y += 26;

    /* ACPI status */
    widget_label(win, 8, y, "ACPI:", COLOR_TEXT_DIM);
    y += 16;
    widget_label(win, 8, y, "  Active", 0xFF00CC66);
    y += 22;

    /* Memory */
    widget_label(win, 8, y, "Memory:", COLOR_TEXT_DIM);
    y += 16;
    {
        const struct pmm_stats *pmm = pmm_get_stats();
        uint64_t total_mb = (pmm->total_pages * 4) / 1024;
        uint64_t used_mb = ((pmm->total_pages - pmm->free_pages) * 4) / 1024;
        pos = 0;
        buf[pos++] = ' '; buf[pos++] = ' ';
        if (used_mb >= 100) buf[pos++] = '0' + (char)(used_mb / 100);
        if (used_mb >= 10) buf[pos++] = '0' + (char)((used_mb / 10) % 10);
        buf[pos++] = '0' + (char)(used_mb % 10);
        buf[pos++] = '/';
        if (total_mb >= 100) buf[pos++] = '0' + (char)(total_mb / 100);
        if (total_mb >= 10) buf[pos++] = '0' + (char)((total_mb / 10) % 10);
        buf[pos++] = '0' + (char)(total_mb % 10);
        buf[pos++] = ' '; buf[pos++] = 'M'; buf[pos++] = 'B';
        buf[pos] = '\0';
        widget_label(win, 8, y, buf, COLOR_TEXT);
        y += 16;
        int mem_pct = 0;
        if (pmm->total_pages > 0)
            mem_pct = (int)(((pmm->total_pages - pmm->free_pages) * 100) / pmm->total_pages);
        widget_progress(win, 8, y, cw - 16, 10,
                        mem_pct, COLOR_HIGHLIGHT, 0xFF0D0D1A);
    }
    y += 20;

    /* PCI Devices */
    widget_label(win, 8, y, "PCI Devices:", COLOR_TEXT_DIM);
    y += 16;
    {
        int count = pci_device_count();
        pos = 0;
        buf[pos++] = ' '; buf[pos++] = ' ';
        if (count >= 10) buf[pos++] = '0' + (char)(count / 10);
        buf[pos++] = '0' + (char)(count % 10);
        buf[pos++] = ' '; buf[pos++] = 'd'; buf[pos++] = 'e';
        buf[pos++] = 'v'; buf[pos++] = 'i'; buf[pos++] = 'c';
        buf[pos++] = 'e'; buf[pos++] = 's';
        buf[pos] = '\0';
        widget_label(win, 8, y, buf, COLOR_TEXT);
    }

    (void)cw;
}

static void vminfo_click(struct wm_window *win, int x, int y, int btn)
{
    (void)win; (void)btn;
    for (int i = 0; i < res_button_count; i++) {
        if (widget_button_hit(&res_buttons[i], x, y)) {
            const struct fb_resolution *r = fb_get_resolution(i);
            if (!r) return;
            if (r->width == fb_get_width() && r->height == fb_get_height())
                return;  /* Already at this resolution */
            if (fb_resize(r->width, r->height) == 0) {
                mouse_set_bounds((int)r->width, (int)r->height);
                fb_mark_all_dirty();
            }
            return;
        }
    }
}

static void launch_vminfo(void)
{
    if (vminfo_win > 0) return;
    vminfo_win = wm_create_window(200, 80, 280, 400, "VM System Info");
    if (vminfo_win > 0) {
        wm_set_on_close(vminfo_win, desktop_on_close);
        wm_set_on_paint(vminfo_win, vminfo_paint);
        wm_set_on_click(vminfo_win, vminfo_click);
    }
}

/*============================================================================
 * Sub-item Launch Dispatcher
 * Maps panel_id strings to launch functions (matching simulation)
 *============================================================================*/

static void launch_by_panel_id(const char *id)
{
    if (!id) return;

    if (strcmp(id, "desktop") == 0)       { /* Already on desktop */ }
    else if (strcmp(id, "files") == 0)    launch_files();
    else if (strcmp(id, "terminal") == 0) launch_terminal();
    else if (strcmp(id, "processes") == 0) launch_processes();
    else if (strcmp(id, "services") == 0) launch_sysinfo();
    else if (strcmp(id, "governor") == 0) launch_governor();
    else if (strcmp(id, "geology") == 0)  launch_geology();
    else if (strcmp(id, "security") == 0) launch_security();
    else if (strcmp(id, "dnauth") == 0)   launch_dnauth();
    else if (strcmp(id, "musikey") == 0)  launch_musikey();
    else if (strcmp(id, "lifeauth") == 0) launch_lifeauth();
    else if (strcmp(id, "biosense") == 0) launch_biosense();
    else if (strcmp(id, "pve") == 0)      launch_pve();
    else if (strcmp(id, "network") == 0)  launch_network();
    else if (strcmp(id, "qrnet") == 0)    launch_qrnet();
    else if (strcmp(id, "notes") == 0)    launch_notes();
    else if (strcmp(id, "media") == 0)    launch_media();
    else if (strcmp(id, "artos") == 0)    launch_artos();
    else if (strcmp(id, "users") == 0)    launch_users();
    else if (strcmp(id, "pods") == 0)     launch_pods();
    else if (strcmp(id, "backup") == 0)   launch_backup();
    else if (strcmp(id, "desktoplab") == 0) launch_desktoplab();
    else if (strcmp(id, "gpumon") == 0)   launch_gpumon();
    else if (strcmp(id, "vminfo") == 0)  launch_vminfo();
    else if (strcmp(id, "constitution") == 0) launch_constitution();
    else if (strcmp(id, "ai") == 0)       launch_ai();
    else if (strcmp(id, "settings") == 0) launch_settings();
}

/*============================================================================
 * AI Assistant Input Handler (enhanced responses matching simulation)
 *============================================================================*/

static void ai_set_response(struct ai_assistant_state *st, const char *text)
{
    st->has_response = 1;
    str_copy(st->response_buf, text, AI_RESPONSE_MAX);
}

/* Tutorial page content */
static const char * const tutorial_pages[] = {
    "[1/8] Welcome to PhantomOS! "
    "This tour covers key concepts. "
    "Type 'next' to continue, 'exit' to stop.",

    "[2/8] PRIME DIRECTIVE: "
    "To Create, Not To Destroy. "
    "Nothing is ever deleted - only "
    "hidden, transformed, or preserved.",

    "[3/8] AI GOVERNOR: Evaluates all "
    "code and operations. Blocks or "
    "transforms destructive actions. "
    "Open Governor window to see stats.",

    "[4/8] GeoFS: Geological File System. "
    "Append-only storage in immutable "
    "layers. Every version preserved. "
    "Time travel through file history!",

    "[5/8] SECURITY: DNAuth (DNA auth), "
    "MusiKey (music passwords), LifeAuth "
    "(biometric vitals), BioSense (bio "
    "signature). Multi-factor by design.",

    "[6/8] NETWORKING: VirtIO-net driver "
    "with ARP/ICMP stack. Ping support. "
    "Open Network window for live stats. "
    "All packets logged, never dropped.",

    "[7/8] ArtOS: AI Art Generator creates "
    "procedural art from text prompts. "
    "DrawNet enables collaborative "
    "drawing with peer sync.",

    "[8/8] Tour complete! You now know "
    "the core of PhantomOS. Type 'help' "
    "for all commands. Remember: To "
    "Create, Not To Destroy."
};
#define TUTORIAL_PAGE_COUNT 8

static void ai_set_tutorial_response(struct ai_assistant_state *st)
{
    if (ai_tutorial.page < 0) ai_tutorial.page = 0;
    if (ai_tutorial.page >= TUTORIAL_PAGE_COUNT) ai_tutorial.page = TUTORIAL_PAGE_COUNT - 1;
    ai_set_response(st, tutorial_pages[ai_tutorial.page]);
}

static void process_ai_query(struct ai_assistant_state *st)
{
    const char *q = st->input_buf;
    char buf[AI_RESPONSE_MAX];

    /* Tutorial mode intercepts all input */
    if (ai_tutorial.active) {
        if (str_icontains(q, "next")) {
            ai_tutorial.page++;
            if (ai_tutorial.page >= TUTORIAL_PAGE_COUNT)
                ai_tutorial.page = TUTORIAL_PAGE_COUNT - 1;
        } else if (str_icontains(q, "prev") || str_icontains(q, "back")) {
            if (ai_tutorial.page > 0) ai_tutorial.page--;
        } else if (str_icontains(q, "exit") || str_icontains(q, "quit") || str_icontains(q, "stop")) {
            ai_tutorial.active = 0;
            ai_set_response(st, "Tutorial ended. Type 'help' for all commands.");
            return;
        }
        ai_set_tutorial_response(st);
        return;
    }

    if (str_icontains(q, "scan")) {
        /* Real governor stats */
        struct gov_stats gs;
        governor_get_stats(&gs);
        char n1[16], n2[16], n3[16];
        gov_u64_to_str(gs.total_checks, n1);
        gov_u64_to_str(gs.total_denied, n2);
        gov_u64_to_str(gs.total_transformed, n3);
        buf[0] = '\0';
        gov_strcat(buf, "Scan complete. ");
        gov_strcat(buf, n1);
        gov_strcat(buf, " checks, ");
        gov_strcat(buf, n2);
        gov_strcat(buf, " denied, ");
        gov_strcat(buf, n3);
        gov_strcat(buf, " transformed. Governor active.");
        gov_append_context(buf);
        ai_set_response(st, buf);
    } else if (str_icontains(q, "status")) {
        int threat = gov_compute_threat_level();
        uint32_t flags = governor_get_flags();
        buf[0] = '\0';
        gov_strcat(buf, "Governor: ACTIVE. Threat: ");
        gov_strcat(buf, gov_threat_str(threat));
        gov_strcat(buf, ". Flags: ");
        if (flags & GOV_FLAG_STRICT) gov_strcat(buf, "strict ");
        if (flags & GOV_FLAG_AUDIT_ALL) gov_strcat(buf, "audit-all ");
        if (flags & GOV_FLAG_VERBOSE) gov_strcat(buf, "verbose ");
        if (flags == 0) gov_strcat(buf, "default ");
        gov_strcat(buf, "- All data preserved.");
        gov_append_context(buf);
        ai_set_response(st, buf);
    } else if (str_icontains(q, "memory") || str_icontains(q, "mem")) {
        uint64_t free_pg = pmm_get_free_pages();
        uint64_t total_pg = pmm_get_total_pages();
        uint64_t free_kb = free_pg * 4;
        uint64_t total_kb = total_pg * 4;
        uint64_t used_pct = total_kb > 0 ? ((total_kb - free_kb) * 100) / total_kb : 0;
        char s1[16], s2[16], s3[16];
        gov_u64_to_str(used_pct, s1);
        gov_u64_to_str(free_kb, s2);
        gov_u64_to_str(total_kb, s3);
        buf[0] = '\0';
        gov_strcat(buf, "Memory: ");
        gov_strcat(buf, s1);
        gov_strcat(buf, "% used. Free: ");
        gov_strcat(buf, s2);
        gov_strcat(buf, "KB / ");
        gov_strcat(buf, s3);
        gov_strcat(buf, "KB total.");
        struct gov_stats gs;
        governor_get_stats(&gs);
        if (gs.violations_memory > 0) {
            char vm[16];
            gov_u64_to_str(gs.violations_memory, vm);
            gov_strcat(buf, " ");
            gov_strcat(buf, vm);
            gov_strcat(buf, " mem violations blocked.");
        }
        gov_append_context(buf);
        ai_set_response(st, buf);
    } else if (str_icontains(q, "uptime") || str_icontains(q, "time")) {
        uint64_t ticks = timer_get_ticks();
        uint64_t secs = ticks / 100;
        uint64_t mins = secs / 60;
        secs = secs % 60;
        char sm[16], ss[16];
        gov_u64_to_str(mins, sm);
        gov_u64_to_str(secs, ss);
        buf[0] = '\0';
        gov_strcat(buf, "Uptime: ");
        gov_strcat(buf, sm);
        gov_strcat(buf, "m ");
        gov_strcat(buf, ss);
        gov_strcat(buf, "s. ");
        char sc[16];
        gov_u64_to_str(gov_scan_count, sc);
        gov_strcat(buf, sc);
        gov_strcat(buf, " governor scans completed.");
        gov_append_context(buf);
        ai_set_response(st, buf);
    } else if (str_icontains(q, "network") || str_icontains(q, "net")) {
        buf[0] = '\0';
        if (virtio_net_available()) {
            gov_strcat(buf, "Network: VirtIO-net online. "
                "ARP/ICMP stack active. "
                "Use Network window for details.");
        } else {
            gov_strcat(buf, "Network: No VirtIO-net device. "
                "Network features unavailable.");
        }
        ai_set_response(st, buf);
    } else if (str_icontains(q, "process") || str_icontains(q, "task")) {
        struct scheduler_stats ss;
        sched_get_stats(&ss);
        char s1[16], s2[16];
        gov_u64_to_str((uint64_t)ss.active_processes, s1);
        gov_u64_to_str((uint64_t)ss.peak_processes, s2);
        buf[0] = '\0';
        gov_strcat(buf, "Processes: ");
        gov_strcat(buf, s1);
        gov_strcat(buf, " active, ");
        gov_strcat(buf, s2);
        gov_strcat(buf, " peak. Processes can be suspended, not killed.");
        gov_append_context(buf);
        ai_set_response(st, buf);
    } else if (str_icontains(q, "config") || str_icontains(q, "flag")) {
        uint32_t flags = governor_get_flags();
        buf[0] = '\0';
        gov_strcat(buf, "Governor config: ");
        if (flags & GOV_FLAG_STRICT) gov_strcat(buf, "[Strict] ");
        if (flags & GOV_FLAG_AUDIT_ALL) gov_strcat(buf, "[Audit-All] ");
        if (flags & GOV_FLAG_VERBOSE) gov_strcat(buf, "[Verbose] ");
        if (flags == 0) gov_strcat(buf, "[Default] ");
        gov_strcat(buf, "Open Governor window Config tab to change.");
        ai_set_response(st, buf);
    } else if (str_icontains(q, "explain")) {
        /* Explain most recent audit entry */
        struct gov_audit_entry recent;
        if (governor_audit_count() > 0 && governor_audit_get(0, &recent) == 0) {
            char explain[256];
            gov_explain_decision(&recent, explain);
            ai_set_response(st, explain);
        } else {
            ai_set_response(st, "No audit entries to explain yet. "
                "Governor checks are logged as they occur.");
        }
    } else if (str_icontains(q, "tour") || str_icontains(q, "tutorial")) {
        ai_tutorial.active = 1;
        ai_tutorial.page = 0;
        ai_tutorial.total_pages = TUTORIAL_PAGE_COUNT;
        ai_set_tutorial_response(st);
    } else if (str_icontains(q, "health")) {
        int health = gov_compute_health_score();
        char hs[8];
        gov_u64_to_str((uint64_t)health, hs);
        buf[0] = '\0';
        gov_strcat(buf, "System Health: ");
        gov_strcat(buf, hs);
        gov_strcat(buf, "/100. ");
        if (health >= 80) gov_strcat(buf, "Excellent condition.");
        else if (health >= 60) gov_strcat(buf, "Good condition. Minor concerns.");
        else if (health >= 40) gov_strcat(buf, "Fair condition. Check violations.");
        else gov_strcat(buf, "Poor condition! Check memory and violations.");
        gov_append_context(buf);
        ai_set_response(st, buf);
    } else if (str_icontains(q, "alert")) {
        buf[0] = '\0';
        if (gov_anomaly.count == 0) {
            gov_strcat(buf, "No active alerts. System nominal.");
        } else {
            gov_strcat(buf, "Active alerts:");
            for (int i = 0; i < GOV_MAX_ALERTS; i++) {
                if (gov_anomaly.alerts[i].active) {
                    gov_strcat(buf, " [");
                    if (gov_anomaly.alerts[i].severity >= 2)
                        gov_strcat(buf, "CRIT");
                    else if (gov_anomaly.alerts[i].severity == 1)
                        gov_strcat(buf, "WARN");
                    else
                        gov_strcat(buf, "INFO");
                    gov_strcat(buf, "] ");
                    gov_strcat(buf, gov_anomaly.alerts[i].msg);
                }
            }
        }
        ai_set_response(st, buf);
    } else if (str_icontains(q, "learn") || str_icontains(q, "behavior")) {
        buf[0] = '\0';
        if (!gov_behavior.baseline_set) {
            struct gov_stats _lq;
            governor_get_stats(&_lq);
            gov_strcat(buf, "Learning: Collecting baseline. ");
            char cn[16];
            gov_u64_to_str(_lq.total_checks, cn);
            gov_strcat(buf, cn);
            gov_strcat(buf, "/100 checks gathered.");
        } else {
            gov_strcat(buf, "Baseline set. ");
            char dn[8];
            gov_u64_to_str((uint64_t)gov_behavior.deviation_count, dn);
            gov_strcat(buf, dn);
            gov_strcat(buf, " policy deviations. ");
            if (gov_behavior.deviation_count == 0)
                gov_strcat(buf, "System behaving normally.");
            else
                gov_strcat(buf, "Check Governor Overview.");
        }
        ai_set_response(st, buf);
    } else if (str_icontains(q, "timeline")) {
        buf[0] = '\0';
        if (gov_timeline.filled < 3) {
            gov_strcat(buf, "Timeline: Collecting... Need 3+ samples.");
        } else {
            int tg = 0, ty = 0, tr = 0;
            for (int i = 0; i < gov_timeline.filled; i++) {
                int idx = (gov_timeline.head - gov_timeline.filled + i + GOV_TIMELINE_SLOTS) % GOV_TIMELINE_SLOTS;
                if (gov_timeline.threat_level[idx] == 0) tg++;
                else if (gov_timeline.threat_level[idx] == 1) ty++;
                else tr++;
            }
            char ng[8], ny[8], nr[8];
            gov_u64_to_str((uint64_t)tg, ng);
            gov_u64_to_str((uint64_t)ty, ny);
            gov_u64_to_str((uint64_t)tr, nr);
            gov_strcat(buf, "Timeline (2min): ");
            gov_strcat(buf, ng);
            gov_strcat(buf, " low, ");
            gov_strcat(buf, ny);
            gov_strcat(buf, " med, ");
            gov_strcat(buf, nr);
            gov_strcat(buf, " high threat periods.");
        }
        ai_set_response(st, buf);
    } else if (str_icontains(q, "recommend")) {
        buf[0] = '\0';
        if (gov_recommendations.count == 0) {
            gov_strcat(buf, "No recommendations. System nominal.");
        } else {
            gov_strcat(buf, "Recommendations: ");
            for (int i = 0; i < gov_recommendations.count; i++) {
                if (!gov_recommendations.items[i].active) continue;
                gov_strcat(buf, "[");
                char pri[4];
                gov_u64_to_str((uint64_t)(i + 1), pri);
                gov_strcat(buf, pri);
                gov_strcat(buf, "] ");
                gov_strcat(buf, gov_recommendations.items[i].msg);
                gov_strcat(buf, " ");
            }
        }
        ai_set_response(st, buf);
    } else if (str_icontains(q, "quarantine")) {
        buf[0] = '\0';
        int qactive = 0, qpending = 0;
        for (int i = 0; i < GOV_QUARANTINE_MAX; i++) {
            if (gov_quarantine.items[i].active) {
                qactive++;
                if (!gov_quarantine.items[i].reviewed) qpending++;
            }
        }
        if (qactive == 0) {
            gov_strcat(buf, "Quarantine empty. No suspicious ops captured.");
        } else {
            char na[8], np[8];
            gov_u64_to_str((uint64_t)qactive, na);
            gov_u64_to_str((uint64_t)qpending, np);
            gov_strcat(buf, "Quarantine: ");
            gov_strcat(buf, na);
            gov_strcat(buf, " items, ");
            gov_strcat(buf, np);
            gov_strcat(buf, " pending. Open Governor Quarantine tab.");
        }
        ai_set_response(st, buf);
    } else if (str_icontains(q, "help")) {
        ai_set_response(st,
            "Commands: scan, status, memory, "
            "uptime, processes, config, explain, "
            "health, alerts, learn, timeline, "
            "recommend, quarantine, tour, version");
    } else if (str_icontains(q, "file")) {
        ai_set_response(st,
            "GeoFS file system: append-only. "
            "Use 'hide' instead of 'delete'. "
            "All versions are preserved forever.");
    } else if (str_icontains(q, "geol")) {
        ai_set_response(st,
            "Geology: immutable storage layer. "
            "Data is written in layers like "
            "geological strata. Time travel!");
    } else if (str_icontains(q, "secur")) {
        ai_set_response(st,
            "Security: Governor evaluates all "
            "code. Auth: DNAuth, MusiKey, "
            "LifeAuth, BioSense available.");
    } else if (str_icontains(q, "version") || str_icontains(q, "about")) {
        ai_set_response(st,
            "PhantomOS Kernel v1.0. GUI desktop, "
            "GeoFS, Governor, scheduler, VirtIO "
            "networking, GPU HAL, USB HID.");
    } else if (str_icontains(q, "constit")) {
        ai_set_response(st,
            "Article I: To Create, Not To "
            "Destroy. No data shall be deleted. "
            "All operations are append-only.");
    } else {
        ai_set_response(st,
            "I follow the Phantom Constitution. "
            "Try: help, scan, status, memory, "
            "explain, health, tour, config");
    }
}

static void handle_ai_input_key(int key)
{
    if (key == '\n') {
        ai_state.input_buf[ai_state.input_len] = '\0';
        if (ai_state.input_len > 0) {
            process_ai_query(&ai_state);
        }
        ai_state.input_len = 0;
    } else if (key == '\b' || key == KEY_BACKSPACE) {
        if (ai_state.input_len > 0) ai_state.input_len--;
    } else if (key >= 32 && key < 127) {
        if (ai_state.input_len < AI_INPUT_MAX - 1)
            ai_state.input_buf[ai_state.input_len++] = (char)key;
    }
}

static void handle_ai_button(int btn)
{
    /* Route buttons through process_ai_query for dynamic responses */
    if (btn == 0) {
        str_copy(ai_state.input_buf, "scan", AI_INPUT_MAX);
        ai_state.input_len = 4;
    } else if (btn == 1) {
        str_copy(ai_state.input_buf, "status", AI_INPUT_MAX);
        ai_state.input_len = 6;
    } else {
        str_copy(ai_state.input_buf, "help", AI_INPUT_MAX);
        ai_state.input_len = 4;
    }
    process_ai_query(&ai_state);
    ai_state.input_len = 0;
}

/*============================================================================
 * Desktop Initialization
 *============================================================================*/

void desktop_init(kgeofs_volume_t *vol)
{
    fs_vol = vol;

    /* Disable framebuffer console so desktop controls rendering */
    fbcon_disable();

    /* Initialize window manager (for popup windows) */
    wm_init();

    /* Initialize terminal */
    memset(&term, 0, sizeof(term));
    term.history_browse = -1;
    widget_scrollbar_init(&term.scrollbar, 0, 0, 0);
    term_append("PhantomOS Terminal\n");
    term_append("\"To Create, Not To Destroy\"\n");
    term_append("Type 'help' for commands.\n\n");

    /* Initialize AI assistant */
    memset(&ai_state, 0, sizeof(ai_state));

    /* Set up app grid entries (main desktop icons like simulation) */
    desktop_app_count = 6;

    desktop_apps[0].name = "Files";
    desktop_apps[0].icon = &icon_files;
    desktop_apps[0].dock_icon = &dock_files;
    desktop_apps[0].on_launch = launch_files;

    desktop_apps[1].name = "Terminal";
    desktop_apps[1].icon = &icon_terminal;
    desktop_apps[1].dock_icon = &dock_terminal;
    desktop_apps[1].on_launch = launch_terminal;

    desktop_apps[2].name = "AI Assistant";
    desktop_apps[2].icon = &icon_ai;
    desktop_apps[2].dock_icon = &dock_ai;
    desktop_apps[2].on_launch = launch_ai;

    desktop_apps[3].name = "Settings";
    desktop_apps[3].icon = &icon_settings;
    desktop_apps[3].dock_icon = &dock_settings;
    desktop_apps[3].on_launch = launch_settings;

    desktop_apps[4].name = "Security";
    desktop_apps[4].icon = &icon_security;
    desktop_apps[4].dock_icon = &dock_security;
    desktop_apps[4].on_launch = launch_security;

    desktop_apps[5].name = "ArtOS";
    desktop_apps[5].icon = &icon_artos;
    desktop_apps[5].dock_icon = &dock_artos;
    desktop_apps[5].on_launch = launch_artos;

    kprintf("Desktop initialized with panel layout.\n");
}

/*============================================================================
 * Main Event Loop
 *============================================================================*/

void desktop_run(void)
{
    struct mouse_state ms;
    int hover_sidebar_cat = -1;
    int hover_sidebar_sub = -1;
    int hover_app_grid = -1;
    int hover_dock = -1;

    while (1) {
        /* 1. Draw all panels (with hover state) */
        panel_draw_header();
        panel_draw_menubar();
        panel_draw_sidebar(selected_category, sidebar_cats,
                           hover_sidebar_cat, hover_sidebar_sub,
                           sidebar_anim_height);
        panel_draw_app_grid(desktop_apps, desktop_app_count, hover_app_grid);
        panel_draw_right_governor();
        panel_draw_right_assistant(&ai_state);
        panel_draw_dock(desktop_apps, desktop_app_count, hover_dock);
        panel_draw_statusbar();

        /* 2. Draw any open popup windows on top */
        wm_draw_all();

        /* 3. Poll USB HID devices (injects into kbd_buffer and mouse_state) */
        if (usb_is_initialized()) {
            usb_poll();
        }

        /* 3b. Poll VirtIO network (process received packets) */
        virtio_net_poll();

        /* 3c. Poll DrawNet collaboration (sync peers and strokes every 100ms) */
        if (art.drawnet_enabled) {
            uint64_t now_ms = timer_get_ms();
            if (now_ms - art.drawnet_last_sync_ms >= 100) {
                drawnet_sync_peers();
                drawnet_pull_strokes();
                art.drawnet_last_sync_ms = now_ms;
            }
        }

        /* 3d. Periodic Governor scan (every ~5 seconds = 500 ticks) */
        {
            uint64_t now_t = timer_get_ticks();
            if (now_t - gov_last_scan_ticks >= 500) {
                gov_last_scan_ticks = now_t;
                gov_scan_count++;
                /* Record violation count for trend analysis */
                {
                    struct gov_stats _gs;
                    governor_get_stats(&_gs);
                    gov_trend.violations[gov_trend.head] = _gs.total_denied + _gs.total_transformed;
                    gov_trend.head = (gov_trend.head + 1) % GOV_TREND_SLOTS;
                    if (gov_trend.filled < GOV_TREND_SLOTS) gov_trend.filled++;
                }
                /* Anomaly detection + alert expiry */
                gov_expire_alerts();
                gov_detect_anomalies();

                /* Timeline tracking */
                gov_timeline.threat_level[gov_timeline.head] = gov_compute_threat_level();
                gov_timeline.health_score[gov_timeline.head] = gov_compute_health_score();
                gov_timeline.head = (gov_timeline.head + 1) % GOV_TIMELINE_SLOTS;
                if (gov_timeline.filled < GOV_TIMELINE_SLOTS) gov_timeline.filled++;

                /* Behavioral learning update */
                {
                    int n = governor_audit_count();
                    if (n > 128) n = 128;
                    for (int p = 0; p < POLICY_COUNT; p++) {
                        gov_behavior.current[p].allow_count = 0;
                        gov_behavior.current[p].deny_count = 0;
                        gov_behavior.current[p].transform_count = 0;
                    }
                    for (int i = 0; i < n; i++) {
                        struct gov_audit_entry _ae;
                        if (governor_audit_get(i, &_ae) != 0) break;
                        int p = (int)_ae.policy;
                        if (p < 0 || p >= POLICY_COUNT) continue;
                        if (_ae.verdict == GOV_ALLOW) gov_behavior.current[p].allow_count++;
                        else if (_ae.verdict == GOV_DENY) gov_behavior.current[p].deny_count++;
                        else if (_ae.verdict == GOV_TRANSFORM) gov_behavior.current[p].transform_count++;
                    }
                    struct gov_stats _bs;
                    governor_get_stats(&_bs);
                    if (!gov_behavior.baseline_set && _bs.total_checks >= GOV_BEHAVIOR_BASELINE) {
                        for (int p = 0; p < POLICY_COUNT; p++)
                            gov_behavior.baseline[p] = gov_behavior.current[p];
                        gov_behavior.baseline_set = 1;
                    }
                    if (gov_behavior.baseline_set) {
                        int devs = 0;
                        for (int p = 0; p < POLICY_COUNT; p++) {
                            uint64_t bt = gov_behavior.baseline[p].allow_count +
                                          gov_behavior.baseline[p].deny_count +
                                          gov_behavior.baseline[p].transform_count;
                            uint64_t ct = gov_behavior.current[p].allow_count +
                                          gov_behavior.current[p].deny_count +
                                          gov_behavior.current[p].transform_count;
                            if (bt < 3 || ct < 3) continue;
                            uint64_t br = (gov_behavior.baseline[p].deny_count * 100) / bt;
                            uint64_t cr = (gov_behavior.current[p].deny_count * 100) / ct;
                            int64_t delta = (int64_t)cr - (int64_t)br;
                            if (delta > 30 || delta < -30) devs++;
                        }
                        gov_behavior.deviation_count = devs;
                    }
                }

                /* Smart recommendations refresh */
                {
                    struct gov_stats _rs;
                    governor_get_stats(&_rs);
                    uint32_t rflags = governor_get_flags();
                    gov_recommendations.count = 0;

                    if (_rs.violations_memory > 5 && !(rflags & GOV_FLAG_STRICT)) {
                        int ri = gov_recommendations.count;
                        if (ri < GOV_MAX_RECS) {
                            gov_recommendations.items[ri].msg[0] = '\0';
                            gov_strcat(gov_recommendations.items[ri].msg,
                                       "Enable Strict mode to block mem attacks");
                            gov_recommendations.items[ri].priority = 1;
                            gov_recommendations.items[ri].active = 1;
                            gov_recommendations.count++;
                        }
                    }
                    {
                        int h = gov_compute_health_score();
                        if (h < 40) {
                            int ri = gov_recommendations.count;
                            if (ri < GOV_MAX_RECS) {
                                gov_recommendations.items[ri].msg[0] = '\0';
                                gov_strcat(gov_recommendations.items[ri].msg,
                                           "Health low - investigate violations");
                                gov_recommendations.items[ri].priority = 2;
                                gov_recommendations.items[ri].active = 1;
                                gov_recommendations.count++;
                            }
                        }
                    }
                    {
                        const char *_tr = gov_trend_str();
                        if (_tr[0] == 'R' && !(rflags & GOV_FLAG_AUDIT_ALL)) {
                            int ri = gov_recommendations.count;
                            if (ri < GOV_MAX_RECS) {
                                gov_recommendations.items[ri].msg[0] = '\0';
                                gov_strcat(gov_recommendations.items[ri].msg,
                                           "Threat rising - enable Audit-All");
                                gov_recommendations.items[ri].priority = 1;
                                gov_recommendations.items[ri].active = 1;
                                gov_recommendations.count++;
                            }
                        }
                    }
                    if (gov_anomaly.count > 0) {
                        int ri = gov_recommendations.count;
                        if (ri < GOV_MAX_RECS) {
                            gov_recommendations.items[ri].msg[0] = '\0';
                            gov_strcat(gov_recommendations.items[ri].msg,
                                       "Active alerts - check Governor Overview");
                            gov_recommendations.items[ri].priority = 1;
                            gov_recommendations.items[ri].active = 1;
                            gov_recommendations.count++;
                        }
                    }
                }

                /* Quarantine capture trigger */
                {
                    int has_critical = 0;
                    for (int i = 0; i < GOV_MAX_ALERTS; i++) {
                        if (gov_anomaly.alerts[i].active && gov_anomaly.alerts[i].severity >= 2)
                            has_critical = 1;
                    }
                    if (has_critical && !gov_quarantine.capturing) {
                        gov_quarantine.capturing = 1;
                        gov_quarantine.capture_count = 0;
                    }
                    if (gov_quarantine.capturing && gov_quarantine.capture_count < 3) {
                        int _qn = governor_audit_count();
                        if (_qn > 5) _qn = 5;
                        for (int i = 0; i < _qn && gov_quarantine.capture_count < 3; i++) {
                            struct gov_audit_entry _qe;
                            if (governor_audit_get(i, &_qe) != 0) break;
                            uint64_t _qage = timer_get_ticks() - _qe.timestamp;
                            if (_qage > 600) continue;
                            if (_qe.verdict == GOV_DENY || _qe.verdict == GOV_TRANSFORM) {
                                gov_quarantine_add(_qe.policy, _qe.verdict, _qe.pid, _qe.reason);
                                gov_quarantine.capture_count++;
                            }
                        }
                        if (gov_quarantine.capture_count >= 3 || !has_critical)
                            gov_quarantine.capturing = 0;
                    }
                }

                /* PVE key evolution (continuous) */
                if (pve_state.initialized)
                    pve_evolve_key();
            }
        }

        /* 4. Handle mouse */
        mouse_get_state(&ms);

        /* Compute hover state from mouse position */
        hover_sidebar_cat = -1;
        hover_sidebar_sub = -1;
        hover_app_grid = -1;
        hover_dock = -1;
        {
            int hc = -1, hs = -1;
            if (sidebar_hit_test(ms.x, ms.y, selected_category,
                                 sidebar_cats, &hc, &hs)) {
                hover_sidebar_cat = hc;
                hover_sidebar_sub = hs;
            }
            hover_app_grid = app_grid_hit_test(ms.x, ms.y, desktop_app_count);
            hover_dock = dock_hit_test(ms.x, ms.y, desktop_app_count);
        }

        int left_pressed = (ms.buttons & MOUSE_LEFT) && !(prev_buttons & MOUSE_LEFT);
        prev_buttons = ms.buttons;

        /* Route to WM first if windows exist */
        if (wm_window_count() > 0) {
            wm_handle_mouse(ms.x, ms.y, ms.buttons);
        }

        /* Panel click handling (only on fresh left click) */
        if (left_pressed) {
            /* Sidebar hit test */
            int hit_cat = -1, hit_sub = -1;
            if (sidebar_hit_test(ms.x, ms.y, selected_category,
                                 sidebar_cats, &hit_cat, &hit_sub)) {
                if (hit_sub >= 0) {
                    /* Sub-item clicked: launch the panel */
                    launch_by_panel_id(
                        sidebar_cats[hit_cat].items[hit_sub].panel_id);
                } else if (hit_cat >= 0) {
                    /* Category header clicked: expand/collapse with animation */
                    selected_category = hit_cat;
                    sidebar_anim_height = 0;
                    sidebar_anim_target = sidebar_cats[hit_cat].sub_count * 18 + 4;
                }
            }

            /* App grid click (only if no WM windows are on top) */
            if (wm_window_count() == 0) {
                int app = app_grid_hit_test(ms.x, ms.y, desktop_app_count);
                if (app >= 0 && desktop_apps[app].on_launch) {
                    desktop_apps[app].on_launch();
                }

                int dock = dock_hit_test(ms.x, ms.y, desktop_app_count);
                if (dock >= 0 && desktop_apps[dock].on_launch) {
                    desktop_apps[dock].on_launch();
                }
            }

            /* AI input and buttons always clickable */
            if (ai_input_hit_test(ms.x, ms.y)) {
                active_input = 1;
            } else if (ms.x < RIGHT_PANEL_X) {
                active_input = 0;
            }

            int btn = ai_button_hit_test(ms.x, ms.y);
            if (btn >= 0) {
                handle_ai_button(btn);
            }

            /* Menubar click handling */
            if (ms.y >= 30 && ms.y < 54) {  /* HEADER_HEIGHT to HEADER_HEIGHT+MENUBAR_HEIGHT */
                /* "Activities" button (x: 12-96) */
                if (ms.x >= 12 && ms.x < 96) {
                    selected_category = 0;  /* Select "Core" category */
                    sidebar_anim_height = 0;
                    sidebar_anim_target = sidebar_cats[0].sub_count * 18 + 4;
                }
                /* "Applications" button (x: 108-204) */
                else if (ms.x >= 108 && ms.x < 204) {
                    selected_category = 4;  /* Select "Apps" category (5th category, index 4) */
                    sidebar_anim_height = 0;
                    sidebar_anim_target = sidebar_cats[4].sub_count * 18 + 4;
                }
            }

            /* Status bar power button */
            if (statusbar_power_hit_test(ms.x, ms.y)) {
                acpi_request_shutdown();
            }
        }

        /* 4. Tick animations */
        /* Sidebar expand animation (ease-out) */
        if (sidebar_anim_height >= 0 && sidebar_anim_height < sidebar_anim_target) {
            sidebar_anim_height += (sidebar_anim_target - sidebar_anim_height) / 3 + 1;
            if (sidebar_anim_height >= sidebar_anim_target)
                sidebar_anim_height = -1; /* done */
        }

        /* DNAuth scan animation */
        if (dna.scanning) {
            dna.scan_tick++;
            if (dna.scan_tick % 3 == 0) {
                dna.scan_progress += 2;
                if (dna.scan_progress >= 100) {
                    dna.scanning = 0;
                    dna.scan_progress = 100;
                    if (!dna.enrolled) {
                        dna.enrolled = 1;
                        str_copy(dna.status_msg, "DNA enrolled successfully", 64);
                        dna.status_color = COLOR_GREEN_ACTIVE;
                    } else {
                        dna.match_pct = 94 + (int)(timer_get_ticks() % 6);
                        str_copy(dna.status_msg, "DNA match verified!", 64);
                        dna.status_color = COLOR_GREEN_ACTIVE;
                    }
                }
            }
        }
        /* LifeAuth scan animation */
        if (life.scanning) {
            life.scan_tick++;
            if (life.scan_tick % 3 == 0) {
                life.scan_progress += 3;
                if (life.scan_progress >= 100) {
                    life.scanning = 0;
                    life.scan_progress = 100;
                    life.enrolled = 1;
                    str_copy(life.status_msg, "Life signs confirmed!", 64);
                    life.status_color = COLOR_GREEN_ACTIVE;
                    /* Slightly vary vitals */
                    life.heart_rate = 70 + (int)(timer_get_ticks() % 8);
                    life.oxygen_sat = 96 + (int)(timer_get_ticks() % 3);
                }
            }
        }
        /* BioSense scan animation */
        if (bio.scanning) {
            bio.scan_tick++;
            if (bio.scan_tick % 3 == 0) {
                bio.scan_progress += 2;
                if (bio.scan_progress >= 100) {
                    bio.scanning = 0;
                    bio.scan_progress = 100;
                    if (!bio.enrolled) {
                        bio.enrolled = 1;
                        str_copy(bio.status_msg, "Vein pattern enrolled", 64);
                        bio.status_color = COLOR_GREEN_ACTIVE;
                    } else {
                        bio.match_pct = 92 + (int)(timer_get_ticks() % 8);
                        str_copy(bio.status_msg, "Vein match confirmed!", 64);
                        bio.status_color = COLOR_GREEN_ACTIVE;
                    }
                }
            }
        }
        /* MusiKey authentication animation */
        if (mk.anim_phase != MK_ANIM_NONE && mk.anim_phase != MK_ANIM_RESULT) {
            mk.anim_tick++;
            if (mk.anim_tick % 2 == 0) {
                mk.anim_progress += 4;
                if (mk.anim_progress >= 100) {
                    mk.anim_progress = 100;
                    switch (mk.anim_phase) {
                    case MK_ANIM_GENERATING:
                        mk.anim_phase = MK_ANIM_ANALYZING;
                        mk.anim_tick = 0;
                        mk.anim_progress = 0;
                        str_copy(mk.status_msg, "Analyzing tone data...", 128);
                        mk.status_color = COLOR_ICON_PURPLE;
                        break;
                    case MK_ANIM_ANALYZING:
                        mk.anim_phase = MK_ANIM_VERIFYING;
                        mk.anim_tick = 0;
                        mk.anim_progress = 0;
                        str_copy(mk.status_msg, "Verifying tone signature...", 128);
                        mk.status_color = COLOR_ICON_ORANGE;
                        break;
                    case MK_ANIM_VERIFYING:
                        mk.anim_phase = MK_ANIM_RESULT;
                        mk.anim_tick = 0;
                        mk.anim_progress = 100;
                        mk.show_analysis = 1;
                        if (mk.anim_result) {
                            /* Play descrambled key tones */
                            mk.tone_playing = 1;
                            mk.tone_index = 0;
                            mk.tone_tick = 0;
                            speaker_play_tone(mk.tone_freqs[0]);
                            str_copy(mk.status_msg, "PASSED - Playing your key!", 128);
                            mk.status_color = COLOR_GREEN_ACTIVE;
                            mk.authenticated = 1;
                        } else {
                            /* Play error buzz */
                            mk.tone_playing = 1;
                            mk.tone_error = 1;
                            mk.tone_index = 0;
                            mk.tone_tick = 0;
                            mk.tone_len = 4;
                            speaker_play_tone(100);
                            str_copy(mk.status_msg, "FAILED - Wrong passphrase", 128);
                            mk.status_color = COLOR_HIGHLIGHT;
                            mk.authenticated = 0;
                        }
                        break;
                    default:
                        break;
                    }
                }
            }
        }
        /* MusiKey visualizer bar decay */
        mk_tick_visualizer();
        /* MusiKey PC speaker tone playback */
        if (mk.tone_playing) {
            mk.tone_tick++;
            int tick_limit;
            if (mk.tone_error) {
                tick_limit = 15;
            } else if (mk.tone_index < mk.tone_len) {
                int d = (int)(unsigned char)mk.tone_durs[mk.tone_index];
                tick_limit = 4 + d * 3; /* SHORT=7, NORMAL=10, LONG=13 */
            } else {
                tick_limit = 1;
            }
            if (mk.tone_tick >= tick_limit) {
                mk.tone_tick = 0;
                mk.tone_index++;
                if (mk.tone_error) {
                    if (mk.tone_index >= 4) {
                        speaker_stop();
                        mk.tone_playing = 0;
                    } else if (mk.tone_index % 2 == 0) {
                        speaker_play_tone(100);
                    } else {
                        speaker_stop();
                    }
                } else if (mk.tone_index < mk.tone_len) {
                    speaker_play_tone(mk.tone_freqs[mk.tone_index]);
                } else {
                    speaker_stop();
                    mk.tone_playing = 0;
                }
            }
        }
        /* QRNet generation animation */
        if (qr.generating) {
            qr.gen_tick++;
            if (qr.gen_tick % 2 == 0) {
                qr.gen_progress += 5;
                if (qr.gen_progress >= 100) {
                    qr.generating = 0;
                    qr.gen_progress = 100;
                    str_copy(qr.status_msg, "QR code ready", 64);
                    qr.status_color = COLOR_GREEN_ACTIVE;
                }
            }
        }
        /* QRNet packet counter when connected */
        if (qr.connected) {
            static int qr_pkt_tick = 0;
            qr_pkt_tick++;
            if (qr_pkt_tick % 50 == 0) {
                qr.packets_sent++;
                qr.packets_recv++;
            }
        }
        /* Media player animation */
        if (media.playing) {
            media.tick++;
            if (media.tick % 5 == 0) {
                media.progress++;
                if (media.progress > 100) {
                    media.progress = 0;
                    media.current_track = (media.current_track + 1) % MEDIA_TRACKS;
                }
            }
            /* Update visualizer bars */
            if (media.tick % 3 == 0) {
                uint32_t seed = (uint32_t)timer_get_ticks();
                for (int i = 0; i < MEDIA_VIS_BARS; i++) {
                    seed = seed * 1103515245 + 12345;
                    media.vis_bars[i] = (int)((seed >> 16) % 15);
                }
            }
        }
        /* Backup progress animation */
        if (bkp.backing_up) {
            bkp.backup_tick++;
            if (bkp.backup_tick % 3 == 0) {
                bkp.backup_progress += 2;
                if (bkp.backup_progress >= 100) {
                    bkp.backing_up = 0;
                    bkp.backup_progress = 100;
                    if (bkp.count < BACKUP_HISTORY_MAX) {
                        int idx = bkp.count++;
                        str_copy(bkp.history[idx].name, "New Snapshot", 32);
                        str_copy(bkp.history[idx].date, "Layer 99", 16);
                        bkp.history[idx].size_kb = 128;
                        bkp.history[idx].complete = 1;
                    }
                }
            }
        }

        /* 5. Draw cursor on top */
        gfx_draw_cursor(ms.x, ms.y);

        /* 6. Wait for frame timing then flip to screen */
        fb_frame_wait();
        fb_flip();

        /* 7. Handle keyboard */
        int key = keyboard_getchar_nonblock();
        if (key >= 0) {
            if (wm_window_count() > 0) {
                wm_handle_key(key);
            } else if (active_input == 1) {
                handle_ai_input_key(key);
            }
        }

        /* 8. Check for ACPI shutdown request */
        if (acpi_is_shutdown_requested())
            break;

        /* Yield until next interrupt */
        __asm__ volatile("hlt");
    }

    /* Shutdown screen */
    fb_clear(0xFF000000);
    fb_mark_all_dirty();

    /* Center "Shutting down..." message */
    const char *msg = "Shutting down...";
    int msg_len = 0;
    while (msg[msg_len]) msg_len++;
    int tx = ((int)fb_get_width() - msg_len * 8) / 2;
    int ty = (int)fb_get_height() / 2 - 4;
    gfx_draw_text(tx, ty, msg, 0xFFCCCCCC, 0xFF000000);

    /* "PhantomOS" subtitle */
    const char *sub = "PhantomOS - To Create, Not To Destroy";
    int sub_len = 0;
    while (sub[sub_len]) sub_len++;
    gfx_draw_text(((int)fb_get_width() - sub_len * 8) / 2, ty + 20, sub, 0xFF666666, 0xFF000000);

    fb_flip();

    /* Brief pause so user sees the message */
    timer_sleep_ms(1000);
}
