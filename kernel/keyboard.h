/*
 * PhantomOS PS/2 Keyboard Driver
 * "To Create, Not To Destroy"
 *
 * Handles PS/2 keyboard input via the 8042 controller.
 * Converts scancodes to ASCII and buffers input for the shell.
 */

#ifndef PHANTOMOS_KEYBOARD_H
#define PHANTOMOS_KEYBOARD_H

#include <stdint.h>
#include <stddef.h>

/*============================================================================
 * Constants
 *============================================================================*/

/* 8042 PS/2 Controller Ports */
#define KBD_DATA_PORT       0x60    /* Data port (read/write) */
#define KBD_STATUS_PORT     0x64    /* Status register (read) */
#define KBD_COMMAND_PORT    0x64    /* Command register (write) */

/* Status Register Bits */
#define KBD_STATUS_OUTPUT   0x01    /* Output buffer full (data available) */
#define KBD_STATUS_INPUT    0x02    /* Input buffer full (don't write) */
#define KBD_STATUS_SYSTEM   0x04    /* System flag */
#define KBD_STATUS_COMMAND  0x08    /* Command/data (0=data, 1=command) */
#define KBD_STATUS_TIMEOUT  0x40    /* Timeout error */
#define KBD_STATUS_PARITY   0x80    /* Parity error */

/* Keyboard Commands */
#define KBD_CMD_SET_LEDS    0xED    /* Set keyboard LEDs */
#define KBD_CMD_ECHO        0xEE    /* Echo (diagnostic) */
#define KBD_CMD_SCANCODE    0xF0    /* Set/get scancode set */
#define KBD_CMD_IDENTIFY    0xF2    /* Identify keyboard */
#define KBD_CMD_TYPEMATIC   0xF3    /* Set typematic rate */
#define KBD_CMD_ENABLE      0xF4    /* Enable scanning */
#define KBD_CMD_DISABLE     0xF5    /* Disable scanning */
#define KBD_CMD_DEFAULT     0xF6    /* Set default parameters */
#define KBD_CMD_RESET       0xFF    /* Reset and self-test */

/* Keyboard Responses */
#define KBD_ACK             0xFA    /* Command acknowledged */
#define KBD_RESEND          0xFE    /* Resend last byte */
#define KBD_SELF_TEST_PASS  0xAA    /* Self-test passed */

/* Special Scancodes (Set 1) */
#define SC_ESCAPE           0x01
#define SC_BACKSPACE        0x0E
#define SC_TAB              0x0F
#define SC_ENTER            0x1C
#define SC_LCTRL            0x1D
#define SC_LSHIFT           0x2A
#define SC_RSHIFT           0x36
#define SC_LALT             0x38
#define SC_CAPSLOCK         0x3A
#define SC_F1               0x3B
#define SC_F2               0x3C
#define SC_F3               0x3D
#define SC_F4               0x3E
#define SC_F5               0x3F
#define SC_F6               0x40
#define SC_F7               0x41
#define SC_F8               0x42
#define SC_F9               0x43
#define SC_F10              0x44
#define SC_NUMLOCK          0x45
#define SC_SCROLLLOCK       0x46
#define SC_HOME             0x47
#define SC_UP               0x48
#define SC_PAGEUP           0x49
#define SC_LEFT             0x4B
#define SC_RIGHT            0x4D
#define SC_END              0x4F
#define SC_DOWN             0x50
#define SC_PAGEDOWN         0x51
#define SC_INSERT           0x52
#define SC_DELETE           0x53
#define SC_F11              0x57
#define SC_F12              0x58

/* Extended scancodes (preceded by 0xE0) */
#define SC_EXTENDED         0xE0

/* Key release bit */
#define SC_RELEASE          0x80

/* Input buffer size */
#define KBD_BUFFER_SIZE     256

/* Special key codes for non-printable keys */
#define KEY_NONE            0
#define KEY_ESCAPE          27
#define KEY_BACKSPACE       8
#define KEY_TAB             9
#define KEY_ENTER           '\n'
#define KEY_UP              0x100
#define KEY_DOWN            0x101
#define KEY_LEFT            0x102
#define KEY_RIGHT           0x103
#define KEY_HOME            0x104
#define KEY_END             0x105
#define KEY_PAGEUP          0x106
#define KEY_PAGEDOWN        0x107
#define KEY_INSERT          0x108
#define KEY_DELETE          0x109
#define KEY_F1              0x110
#define KEY_F2              0x111
#define KEY_F3              0x112
#define KEY_F4              0x113
#define KEY_F5              0x114
#define KEY_F6              0x115
#define KEY_F7              0x116
#define KEY_F8              0x117
#define KEY_F9              0x118
#define KEY_F10             0x119
#define KEY_F11             0x11A
#define KEY_F12             0x11B

/*============================================================================
 * Modifier State
 *============================================================================*/

/* Modifier flags */
#define MOD_SHIFT           0x01
#define MOD_CTRL            0x02
#define MOD_ALT             0x04
#define MOD_CAPSLOCK        0x08
#define MOD_NUMLOCK         0x10
#define MOD_SCROLLLOCK      0x20

/*============================================================================
 * API Functions
 *============================================================================*/

/*
 * Initialize the keyboard driver
 * Sets up IRQ1 handler and enables keyboard
 */
void keyboard_init(void);

/*
 * Check if a key is available in the buffer
 * Returns: 1 if key available, 0 if buffer empty
 */
int keyboard_has_key(void);

/*
 * Get a key from the buffer (blocking)
 * Returns: ASCII character or special key code
 */
int keyboard_getchar(void);

/*
 * Get a key from the buffer (non-blocking)
 * Returns: ASCII character, special key code, or -1 if no key
 */
int keyboard_getchar_nonblock(void);

/*
 * Read a line of input (blocking, with echo)
 * Returns: Number of characters read (excluding null terminator)
 */
int keyboard_readline(char *buf, size_t size);

/*
 * Get current modifier state
 */
uint8_t keyboard_get_modifiers(void);

/*
 * Set keyboard LEDs
 */
void keyboard_set_leds(int scroll, int num, int caps);

/*
 * Get keyboard statistics
 */
void keyboard_get_stats(uint64_t *keys_pressed, uint64_t *keys_released);

/*
 * Debug: dump keyboard state
 */
void keyboard_dump_state(void);

/*
 * Inject a character into the keyboard buffer (for USB HID keyboards)
 */
void keyboard_inject_char(char c);

#endif /* PHANTOMOS_KEYBOARD_H */
