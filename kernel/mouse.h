/*
 * PhantomOS PS/2 Mouse Driver
 * "To Create, Not To Destroy"
 *
 * PS/2 mouse input via 8042 controller auxiliary port (IRQ12).
 */

#ifndef PHANTOMOS_MOUSE_H
#define PHANTOMOS_MOUSE_H

#include <stdint.h>

/*============================================================================
 * Mouse Button Bits
 *============================================================================*/

#define MOUSE_LEFT      (1 << 0)
#define MOUSE_RIGHT     (1 << 1)
#define MOUSE_MIDDLE    (1 << 2)

/*============================================================================
 * Mouse State
 *============================================================================*/

struct mouse_state {
    int         x;          /* Cursor X position */
    int         y;          /* Cursor Y position */
    uint8_t     buttons;    /* Button state (MOUSE_LEFT/RIGHT/MIDDLE) */
    int         moved;      /* Set when cursor moves, cleared by reader */
    int         clicked;    /* Set on button press, cleared by reader */
};

/*============================================================================
 * Mouse API
 *============================================================================*/

/*
 * Initialize the PS/2 mouse driver
 * Enables the auxiliary port on the 8042 controller and registers IRQ12
 */
void mouse_init(void);

/*
 * Get current mouse state
 * Copies state and clears moved/clicked flags
 */
void mouse_get_state(struct mouse_state *state);

/*
 * Check if mouse has moved since last check
 */
int mouse_has_moved(void);

/*
 * Check if a button was clicked since last check
 */
int mouse_has_clicked(void);

/*
 * Inject mouse movement/button data (for USB HID mice)
 */
void mouse_inject_movement(int dx, int dy, uint8_t buttons);

/*
 * Update mouse screen bounds (for dynamic resolution changes)
 */
void mouse_set_bounds(int w, int h);

#endif /* PHANTOMOS_MOUSE_H */
