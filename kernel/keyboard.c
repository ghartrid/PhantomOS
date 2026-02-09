/*
 * PhantomOS PS/2 Keyboard Driver
 * "To Create, Not To Destroy"
 *
 * Implementation of PS/2 keyboard handling.
 */

#include "keyboard.h"
#include "idt.h"
#include "pic.h"
#include <stdint.h>
#include <stddef.h>

/*============================================================================
 * External Declarations
 *============================================================================*/

extern int kprintf(const char *fmt, ...);
extern void *memset(void *s, int c, size_t n);

/*============================================================================
 * Port I/O
 *============================================================================*/

static inline uint8_t inb(uint16_t port)
{
    uint8_t val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline void io_wait(void)
{
    outb(0x80, 0);  /* Write to unused port for delay */
}

/*============================================================================
 * Scancode to ASCII Tables (US QWERTY, Set 1)
 *============================================================================*/

/* Normal (no modifiers) */
static const char scancode_normal[128] = {
    0,    27,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0,    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*',  0,   ' ', 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,    0,   0,   0,   0,   0,   0,   0
};

/* Shifted */
static const char scancode_shift[128] = {
    0,    27,  '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0,    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0,    '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*',  0,   ' ', 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,    0,   0,   0,   0,   0,   0,   0
};

/*============================================================================
 * Driver State
 *============================================================================*/

/* Circular input buffer */
static volatile char kbd_buffer[KBD_BUFFER_SIZE];
static volatile int kbd_buffer_head = 0;
static volatile int kbd_buffer_tail = 0;

/* Modifier state */
static volatile uint8_t kbd_modifiers = 0;

/* Extended scancode flag */
static volatile int kbd_extended = 0;

/* Statistics */
static volatile uint64_t kbd_keys_pressed = 0;
static volatile uint64_t kbd_keys_released = 0;

/* Initialization flag */
static int kbd_initialized = 0;

/*============================================================================
 * Buffer Operations
 *============================================================================*/

static int buffer_put(char c)
{
    int next = (kbd_buffer_head + 1) % KBD_BUFFER_SIZE;
    if (next == kbd_buffer_tail) {
        return -1;  /* Buffer full */
    }
    kbd_buffer[kbd_buffer_head] = c;
    kbd_buffer_head = next;
    return 0;
}

static int buffer_get(void)
{
    if (kbd_buffer_tail == kbd_buffer_head) {
        return -1;  /* Buffer empty */
    }
    char c = kbd_buffer[kbd_buffer_tail];
    kbd_buffer_tail = (kbd_buffer_tail + 1) % KBD_BUFFER_SIZE;
    return (unsigned char)c;
}

static int buffer_empty(void)
{
    return kbd_buffer_tail == kbd_buffer_head;
}

/*============================================================================
 * Keyboard Controller Communication
 *============================================================================*/

static void kbd_wait_input(void)
{
    /* Wait until input buffer is empty */
    int timeout = 100000;
    while (timeout-- > 0) {
        if (!(inb(KBD_STATUS_PORT) & KBD_STATUS_INPUT)) {
            return;
        }
        io_wait();
    }
}

static void kbd_wait_output(void)
{
    /* Wait until output buffer is full */
    int timeout = 100000;
    while (timeout-- > 0) {
        if (inb(KBD_STATUS_PORT) & KBD_STATUS_OUTPUT) {
            return;
        }
        io_wait();
    }
}

static void kbd_send_command(uint8_t cmd)
{
    kbd_wait_input();
    outb(KBD_DATA_PORT, cmd);
}

static uint8_t kbd_read_data(void)
{
    kbd_wait_output();
    return inb(KBD_DATA_PORT);
}

/*============================================================================
 * Scancode Processing
 *============================================================================*/

static void process_scancode(uint8_t scancode)
{
    /* Check for extended scancode prefix */
    if (scancode == SC_EXTENDED) {
        kbd_extended = 1;
        return;
    }

    /* Check if this is a key release */
    int released = (scancode & SC_RELEASE) != 0;
    scancode &= ~SC_RELEASE;

    /* Handle extended scancodes */
    if (kbd_extended) {
        kbd_extended = 0;

        if (released) {
            kbd_keys_released++;
            return;
        }

        kbd_keys_pressed++;

        /* Extended keys - arrows, etc. */
        switch (scancode) {
        case 0x48: buffer_put(KEY_UP & 0xFF); break;      /* Up arrow */
        case 0x50: buffer_put(KEY_DOWN & 0xFF); break;    /* Down arrow */
        case 0x4B: buffer_put(KEY_LEFT & 0xFF); break;    /* Left arrow */
        case 0x4D: buffer_put(KEY_RIGHT & 0xFF); break;   /* Right arrow */
        case 0x47: buffer_put(KEY_HOME & 0xFF); break;    /* Home */
        case 0x4F: buffer_put(KEY_END & 0xFF); break;     /* End */
        case 0x49: buffer_put(KEY_PAGEUP & 0xFF); break;  /* Page Up */
        case 0x51: buffer_put(KEY_PAGEDOWN & 0xFF); break;/* Page Down */
        case 0x52: buffer_put(KEY_INSERT & 0xFF); break;  /* Insert */
        case 0x53: buffer_put(KEY_DELETE & 0xFF); break;  /* Delete */
        default: break;
        }
        return;
    }

    /* Handle modifier keys */
    if (released) {
        kbd_keys_released++;
        switch (scancode) {
        case SC_LSHIFT:
        case SC_RSHIFT:
            kbd_modifiers &= ~MOD_SHIFT;
            break;
        case SC_LCTRL:
            kbd_modifiers &= ~MOD_CTRL;
            break;
        case SC_LALT:
            kbd_modifiers &= ~MOD_ALT;
            break;
        }
        return;
    }

    kbd_keys_pressed++;

    /* Handle modifier key presses */
    switch (scancode) {
    case SC_LSHIFT:
    case SC_RSHIFT:
        kbd_modifiers |= MOD_SHIFT;
        return;
    case SC_LCTRL:
        kbd_modifiers |= MOD_CTRL;
        return;
    case SC_LALT:
        kbd_modifiers |= MOD_ALT;
        return;
    case SC_CAPSLOCK:
        kbd_modifiers ^= MOD_CAPSLOCK;
        /* Update LED */
        keyboard_set_leds(kbd_modifiers & MOD_SCROLLLOCK,
                          kbd_modifiers & MOD_NUMLOCK,
                          kbd_modifiers & MOD_CAPSLOCK);
        return;
    case SC_NUMLOCK:
        kbd_modifiers ^= MOD_NUMLOCK;
        keyboard_set_leds(kbd_modifiers & MOD_SCROLLLOCK,
                          kbd_modifiers & MOD_NUMLOCK,
                          kbd_modifiers & MOD_CAPSLOCK);
        return;
    case SC_SCROLLLOCK:
        kbd_modifiers ^= MOD_SCROLLLOCK;
        keyboard_set_leds(kbd_modifiers & MOD_SCROLLLOCK,
                          kbd_modifiers & MOD_NUMLOCK,
                          kbd_modifiers & MOD_CAPSLOCK);
        return;
    }

    /* Handle function keys */
    if (scancode >= SC_F1 && scancode <= SC_F10) {
        /* F1-F10: could add special handling here */
        return;
    }
    if (scancode == SC_F11 || scancode == SC_F12) {
        return;
    }

    /* Convert scancode to ASCII */
    char c;
    int shift = (kbd_modifiers & MOD_SHIFT) != 0;
    int caps = (kbd_modifiers & MOD_CAPSLOCK) != 0;

    if (shift) {
        c = scancode_shift[scancode];
    } else {
        c = scancode_normal[scancode];
    }

    /* Apply caps lock to letters */
    if (c >= 'a' && c <= 'z') {
        if (caps ^ shift) {
            c = c - 'a' + 'A';
        }
    } else if (c >= 'A' && c <= 'Z') {
        if (caps ^ shift) {
            c = c - 'A' + 'a';
        }
    }

    /* Handle Ctrl+key combinations */
    if (kbd_modifiers & MOD_CTRL) {
        if (c >= 'a' && c <= 'z') {
            c = c - 'a' + 1;  /* Ctrl+A = 1, Ctrl+B = 2, etc. */
        } else if (c >= 'A' && c <= 'Z') {
            c = c - 'A' + 1;
        }
    }

    /* Add to buffer if valid */
    if (c != 0) {
        buffer_put(c);
    }
}

/*============================================================================
 * Interrupt Handler
 *============================================================================*/

static void keyboard_handler(struct interrupt_frame *frame)
{
    (void)frame;

    /* Read scancode from keyboard */
    uint8_t scancode = inb(KBD_DATA_PORT);

    /* Process the scancode */
    process_scancode(scancode);

    /* Send EOI to PIC */
    pic_send_eoi(1);
}

/*============================================================================
 * Public API
 *============================================================================*/

void keyboard_init(void)
{
    if (kbd_initialized) {
        return;
    }

    /* Clear buffer */
    kbd_buffer_head = 0;
    kbd_buffer_tail = 0;
    kbd_modifiers = 0;
    kbd_extended = 0;

    /* Flush any pending data */
    while (inb(KBD_STATUS_PORT) & KBD_STATUS_OUTPUT) {
        inb(KBD_DATA_PORT);
        io_wait();
    }

    /* Register interrupt handler */
    register_interrupt_handler(IRQ_KEYBOARD, keyboard_handler);

    /* Enable keyboard IRQ */
    pic_enable_irq(1);

    kbd_initialized = 1;
    kprintf("  Keyboard: PS/2 driver initialized\n");
}

int keyboard_has_key(void)
{
    return !buffer_empty();
}

int keyboard_getchar(void)
{
    /* Wait for a key */
    while (buffer_empty()) {
        __asm__ volatile("hlt");  /* Wait for interrupt */
    }
    return buffer_get();
}

int keyboard_getchar_nonblock(void)
{
    return buffer_get();
}

int keyboard_readline(char *buf, size_t size)
{
    if (!buf || size == 0) return 0;

    size_t pos = 0;
    size_t max = size - 1;  /* Leave room for null terminator */

    while (pos < max) {
        int c = keyboard_getchar();

        if (c == '\n' || c == '\r') {
            /* Enter - end of line */
            kprintf("\n");
            break;
        } else if (c == '\b' || c == 127) {
            /* Backspace */
            if (pos > 0) {
                pos--;
                kprintf("\b \b");  /* Erase character on screen */
            }
        } else if (c == 3) {
            /* Ctrl+C - cancel */
            kprintf("^C\n");
            buf[0] = '\0';
            return -1;
        } else if (c == 4) {
            /* Ctrl+D - EOF */
            if (pos == 0) {
                buf[0] = '\0';
                return -2;  /* EOF */
            }
            break;
        } else if (c >= 32 && c < 127) {
            /* Printable character */
            buf[pos++] = c;
            kprintf("%c", c);  /* Echo */
        }
        /* Ignore other characters */
    }

    buf[pos] = '\0';
    return pos;
}

uint8_t keyboard_get_modifiers(void)
{
    return kbd_modifiers;
}

void keyboard_set_leds(int scroll, int num, int caps)
{
    uint8_t leds = 0;
    if (scroll) leds |= 1;
    if (num)    leds |= 2;
    if (caps)   leds |= 4;

    kbd_send_command(KBD_CMD_SET_LEDS);
    kbd_wait_input();
    outb(KBD_DATA_PORT, leds);
}

void keyboard_get_stats(uint64_t *keys_pressed, uint64_t *keys_released)
{
    if (keys_pressed)  *keys_pressed = kbd_keys_pressed;
    if (keys_released) *keys_released = kbd_keys_released;
}

void keyboard_inject_char(char c)
{
    buffer_put(c);
}

void keyboard_dump_state(void)
{
    kprintf("\nKeyboard State:\n");
    kprintf("  Keys pressed:  %lu\n", (unsigned long)kbd_keys_pressed);
    kprintf("  Keys released: %lu\n", (unsigned long)kbd_keys_released);
    kprintf("  Buffer: %d chars\n",
            (kbd_buffer_head - kbd_buffer_tail + KBD_BUFFER_SIZE) % KBD_BUFFER_SIZE);
    kprintf("  Modifiers: 0x%02x", kbd_modifiers);
    if (kbd_modifiers & MOD_SHIFT)      kprintf(" SHIFT");
    if (kbd_modifiers & MOD_CTRL)       kprintf(" CTRL");
    if (kbd_modifiers & MOD_ALT)        kprintf(" ALT");
    if (kbd_modifiers & MOD_CAPSLOCK)   kprintf(" CAPS");
    if (kbd_modifiers & MOD_NUMLOCK)    kprintf(" NUM");
    if (kbd_modifiers & MOD_SCROLLLOCK) kprintf(" SCROLL");
    kprintf("\n");
}
