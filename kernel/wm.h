/*
 * PhantomOS Window Manager
 * "To Create, Not To Destroy"
 *
 * Manages windows with title bars, dragging, focus, and z-ordering.
 */

#ifndef PHANTOMOS_WM_H
#define PHANTOMOS_WM_H

#include <stdint.h>

/*============================================================================
 * Constants
 *============================================================================*/

#define WM_MAX_WINDOWS      32
#define WM_TITLE_HEIGHT     28      /* Title bar height in pixels */
#define WM_BORDER_WIDTH     1       /* Window border width */
#define WM_CLOSE_SIZE       18      /* Close button size */
#define WM_BTN_SIZE         14      /* Minimize/maximize button size */
#define WM_BTN_GAP          4       /* Gap between window buttons */
#define WM_TITLE_MAX        64      /* Max title string length */
#define WM_CORNER_RADIUS    8       /* Corner radius for windows */
#define WM_SHADOW_OFFSET    4       /* Drop shadow offset */
#define WM_SHADOW_ALPHA     100     /* Shadow opacity (0-255) */

/* Window flags */
#define WM_FLAG_VISIBLE     (1 << 0)
#define WM_FLAG_FOCUSED     (1 << 1)
#define WM_FLAG_DRAGGING    (1 << 2)
#define WM_FLAG_CLOSEABLE   (1 << 3)

/*============================================================================
 * Window Structure
 *============================================================================*/

struct wm_window {
    int         id;                         /* Window ID (0 = unused) */
    int         x, y;                       /* Position */
    int         width, height;              /* Total size including title bar */
    char        title[WM_TITLE_MAX];        /* Title text */
    uint32_t    flags;                      /* WM_FLAG_* */
    uint32_t   *content;                    /* Content pixel buffer */

    /* Drag state */
    int         drag_ox, drag_oy;           /* Offset from window origin to grab point */

    /* Fade transition */
    uint8_t     fade_alpha;                 /* 0=invisible, 255=opaque */
    int         fading_in;                  /* 1 = fading in */
    int         fading_out;                 /* 1 = fading out, destroy when done */

    /* Callbacks */
    void       (*on_paint)(struct wm_window *win);
    void       (*on_key)(struct wm_window *win, int key);
    void       (*on_click)(struct wm_window *win, int x, int y, int button);
    void       (*on_close)(struct wm_window *win);
};

/*============================================================================
 * Window Manager API
 *============================================================================*/

/*
 * Initialize the window manager
 */
void wm_init(void);

/*
 * Create a new window
 *
 * @x, @y:          Position on screen
 * @w, @h:          Content area size (title bar adds WM_TITLE_HEIGHT)
 * @title:          Window title text
 * @return:         Window ID (>0) on success, 0 on failure
 */
int wm_create_window(int x, int y, int w, int h, const char *title);

/*
 * Destroy a window
 */
void wm_destroy_window(int id);

/*
 * Get window by ID
 */
struct wm_window *wm_get_window(int id);

/*
 * Set window callbacks
 */
void wm_set_on_paint(int id, void (*callback)(struct wm_window *));
void wm_set_on_key(int id, void (*callback)(struct wm_window *, int));
void wm_set_on_click(int id, void (*callback)(struct wm_window *, int, int, int));
void wm_set_on_close(int id, void (*callback)(struct wm_window *));

/*
 * Draw all windows (desktop bg -> windows back-to-front -> cursor)
 */
void wm_draw_all(void);

/*
 * Handle mouse input (hit testing, dragging, focus)
 *
 * @x, @y:      Mouse position
 * @buttons:    Button state (bit 0=left, 1=right, 2=middle)
 */
void wm_handle_mouse(int x, int y, int buttons);

/*
 * Handle keyboard input (routes to focused window)
 */
void wm_handle_key(int key);

/*
 * Get content area dimensions for a window
 */
int wm_content_width(struct wm_window *win);
int wm_content_height(struct wm_window *win);

/*
 * Get content area pixel buffer for drawing into a window
 */
uint32_t *wm_content_buffer(struct wm_window *win);

/*
 * Get the number of open windows
 */
int wm_window_count(void);

#endif /* PHANTOMOS_WM_H */
