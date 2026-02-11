/*
 * PhantomOS PS/2 Mouse Driver
 * "To Create, Not To Destroy"
 *
 * PS/2 mouse via 8042 controller auxiliary port.
 * Handles 3-byte standard PS/2 mouse packets on IRQ12.
 */

#include "mouse.h"
#include "idt.h"
#include "pic.h"
#include "framebuffer.h"
#include <stdint.h>

/*============================================================================
 * External Declarations
 *============================================================================*/

extern int kprintf(const char *fmt, ...);

/*============================================================================
 * 8042 Controller Ports
 *============================================================================*/

#define PS2_DATA_PORT       0x60
#define PS2_STATUS_PORT     0x64
#define PS2_COMMAND_PORT    0x64

/* Status register bits */
#define PS2_STATUS_OUTPUT   (1 << 0)    /* Output buffer full (data ready) */
#define PS2_STATUS_INPUT    (1 << 1)    /* Input buffer full (don't write) */

/* 8042 controller commands */
#define PS2_CMD_READ_CONFIG     0x20
#define PS2_CMD_WRITE_CONFIG    0x60
#define PS2_CMD_DISABLE_AUX     0xA7
#define PS2_CMD_ENABLE_AUX      0xA8
#define PS2_CMD_WRITE_AUX       0xD4    /* Send next byte to auxiliary device */

/* PS/2 mouse commands */
#define MOUSE_CMD_SET_DEFAULTS  0xF6
#define MOUSE_CMD_ENABLE_DATA   0xF4
#define MOUSE_CMD_RESET         0xFF

/* PS/2 mouse responses */
#define MOUSE_ACK               0xFA

/*============================================================================
 * Port I/O
 *============================================================================*/

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void io_wait(void)
{
    outb(0x80, 0);
}

/*============================================================================
 * Mouse State
 *============================================================================*/

static struct mouse_state state;
static uint8_t packet[3];
static int packet_idx = 0;
static int screen_w = 1024;
static int screen_h = 768;

/*============================================================================
 * 8042 Controller Helpers
 *============================================================================*/

/*
 * Wait until the 8042 input buffer is empty (ready to accept commands)
 */
static void ps2_wait_input(void)
{
    int timeout = 100000;
    while ((inb(PS2_STATUS_PORT) & PS2_STATUS_INPUT) && --timeout)
        ;
}

/*
 * Wait until the 8042 output buffer has data
 */
static void ps2_wait_output(void)
{
    int timeout = 100000;
    while (!(inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT) && --timeout)
        ;
}

/*
 * Send a command to the 8042 controller
 */
static void ps2_send_command(uint8_t cmd)
{
    ps2_wait_input();
    outb(PS2_COMMAND_PORT, cmd);
}

/*
 * Send a byte to the mouse (via 8042 auxiliary port)
 */
static void mouse_send(uint8_t data)
{
    ps2_wait_input();
    outb(PS2_COMMAND_PORT, PS2_CMD_WRITE_AUX);
    ps2_wait_input();
    outb(PS2_DATA_PORT, data);

    /* Wait for ACK */
    ps2_wait_output();
    inb(PS2_DATA_PORT);  /* Read and discard ACK */
}

/*============================================================================
 * IRQ12 Handler
 *============================================================================*/

static void mouse_handler(struct interrupt_frame *frame)
{
    (void)frame;

    uint8_t data = inb(PS2_DATA_PORT);

    /* Byte 0 must have bit 3 set (always-1 bit in PS/2 protocol) */
    if (packet_idx == 0 && !(data & 0x08)) {
        /* Out of sync - discard and resync */
        pic_send_eoi(12);
        return;
    }

    packet[packet_idx++] = data;

    if (packet_idx == 3) {
        packet_idx = 0;

        /* Parse packet:
         * Byte 0: Y_overflow | X_overflow | Y_sign | X_sign | 1 | Middle | Right | Left
         * Byte 1: X movement (unsigned, use X_sign for direction)
         * Byte 2: Y movement (unsigned, use Y_sign for direction)
         */
        uint8_t flags = packet[0];
        int dx = (int)packet[1];
        int dy = (int)packet[2];

        /* Apply sign extension */
        if (flags & 0x10) dx |= 0xFFFFFF00;  /* X sign bit */
        if (flags & 0x20) dy |= 0xFFFFFF00;  /* Y sign bit */

        /* Discard overflow packets */
        if (flags & 0xC0) {
            pic_send_eoi(12);
            return;
        }

        /* Update position (PS/2 Y is inverted: positive = up) */
        state.x += dx;
        state.y -= dy;

        /* Clamp to screen bounds */
        if (state.x < 0) state.x = 0;
        if (state.y < 0) state.y = 0;
        if (state.x >= screen_w) state.x = screen_w - 1;
        if (state.y >= screen_h) state.y = screen_h - 1;

        /* Update buttons */
        uint8_t new_buttons = flags & 0x07;
        if (new_buttons != state.buttons) {
            state.clicked = 1;
        }
        state.buttons = new_buttons;

        if (dx != 0 || dy != 0) {
            state.moved = 1;
        }
    }

    pic_send_eoi(12);
}

/*============================================================================
 * Initialization
 *============================================================================*/

void mouse_init(void)
{
    /* Use framebuffer dimensions if available */
    if (fb_is_initialized()) {
        screen_w = (int)fb_get_width();
        screen_h = (int)fb_get_height();
    }

    /* Start cursor at center of screen */
    state.x = screen_w / 2;
    state.y = screen_h / 2;
    state.buttons = 0;
    state.moved = 0;
    state.clicked = 0;

    /* Enable auxiliary (mouse) port on 8042 controller */
    ps2_send_command(PS2_CMD_ENABLE_AUX);

    /* Read controller configuration byte */
    ps2_send_command(PS2_CMD_READ_CONFIG);
    ps2_wait_output();
    uint8_t config = inb(PS2_DATA_PORT);

    /* Enable IRQ12 (auxiliary port interrupt) and disable auxiliary clock disable */
    config |= (1 << 1);    /* Enable auxiliary port interrupt */
    config &= ~(1 << 5);   /* Enable auxiliary clock */

    /* Write back configuration */
    ps2_send_command(PS2_CMD_WRITE_CONFIG);
    ps2_wait_input();
    outb(PS2_DATA_PORT, config);

    /* Set defaults on mouse */
    mouse_send(MOUSE_CMD_SET_DEFAULTS);

    /* Enable data reporting */
    mouse_send(MOUSE_CMD_ENABLE_DATA);

    /* Register IRQ12 handler */
    register_interrupt_handler(IRQ_MOUSE, mouse_handler);

    /* Unmask IRQ12 on slave PIC */
    pic_enable_irq(12);

    kprintf("[MOUSE] PS/2 mouse initialized (IRQ12)\n");
}

/*============================================================================
 * State Query Functions
 *============================================================================*/

void mouse_get_state(struct mouse_state *out)
{
    out->x = state.x;
    out->y = state.y;
    out->buttons = state.buttons;
    out->moved = state.moved;
    out->clicked = state.clicked;
    state.moved = 0;
    state.clicked = 0;
}

int mouse_has_moved(void)
{
    return state.moved;
}

int mouse_has_clicked(void)
{
    return state.clicked;
}

void mouse_set_bounds(int w, int h)
{
    screen_w = w;
    screen_h = h;
    /* Clamp current position to new bounds */
    if (state.x >= screen_w) state.x = screen_w - 1;
    if (state.y >= screen_h) state.y = screen_h - 1;
}

void mouse_inject_movement(int dx, int dy, uint8_t buttons)
{
    /* Update position (USB HID: positive Y = down, matching screen coords) */
    state.x += dx;
    state.y += dy;

    /* Clamp to screen bounds */
    if (state.x < 0) state.x = 0;
    if (state.y < 0) state.y = 0;
    if (state.x >= screen_w) state.x = screen_w - 1;
    if (state.y >= screen_h) state.y = screen_h - 1;

    /* Update buttons */
    if (buttons != state.buttons) {
        state.clicked = 1;
    }
    state.buttons = buttons;

    if (dx != 0 || dy != 0) {
        state.moved = 1;
    }
}

void mouse_set_absolute(int abs_x, int abs_y, uint8_t buttons)
{
    /* Map from USB tablet range [0, 32767] to screen coordinates */
    int new_x = (abs_x * (screen_w - 1)) / 32767;
    int new_y = (abs_y * (screen_h - 1)) / 32767;

    /* Clamp to screen bounds */
    if (new_x < 0) new_x = 0;
    if (new_y < 0) new_y = 0;
    if (new_x >= screen_w) new_x = screen_w - 1;
    if (new_y >= screen_h) new_y = screen_h - 1;

    if (new_x != state.x || new_y != state.y) {
        state.moved = 1;
    }

    state.x = new_x;
    state.y = new_y;

    /* Update buttons */
    if (buttons != state.buttons) {
        state.clicked = 1;
    }
    state.buttons = buttons;
}
