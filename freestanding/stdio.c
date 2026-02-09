/*
 * PhantomOS Freestanding I/O Library
 * "To Create, Not To Destroy"
 *
 * Provides basic console output via:
 * - VGA text mode (0xB8000)
 * - Serial port (COM1, 0x3F8)
 */

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

/*============================================================================
 * Hardware Constants
 *============================================================================*/

/* VGA text mode */
#define VGA_BUFFER      ((uint16_t *)0xB8000)
#define VGA_WIDTH       80
#define VGA_HEIGHT      25
#define VGA_ATTR_WHITE  0x0F    /* White on black */
#define VGA_ATTR_ERROR  0x4F    /* White on red */

/* Serial port (COM1) */
#define SERIAL_COM1     0x3F8
#define SERIAL_DATA     0       /* Data register (read/write) */
#define SERIAL_IER      1       /* Interrupt Enable Register */
#define SERIAL_FCR      2       /* FIFO Control Register */
#define SERIAL_LCR      3       /* Line Control Register */
#define SERIAL_MCR      4       /* Modem Control Register */
#define SERIAL_LSR      5       /* Line Status Register */
#define SERIAL_DLL      0       /* Divisor Latch Low (when DLAB=1) */
#define SERIAL_DLH      1       /* Divisor Latch High (when DLAB=1) */

#define SERIAL_LSR_EMPTY 0x20   /* Transmitter holding register empty */

/*============================================================================
 * Global State
 *============================================================================*/

static int vga_row = 0;
static int vga_col = 0;
static uint8_t vga_attr = VGA_ATTR_WHITE;
static int serial_initialized = 0;

/*============================================================================
 * Port I/O Helpers
 *============================================================================*/

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/*============================================================================
 * Serial Port Functions
 *============================================================================*/

/*
 * Initialize serial port (COM1) for debugging output
 * 115200 baud, 8N1
 */
void serial_init(void)
{
    /* Disable interrupts */
    outb(SERIAL_COM1 + SERIAL_IER, 0x00);

    /* Enable DLAB (Divisor Latch Access Bit) */
    outb(SERIAL_COM1 + SERIAL_LCR, 0x80);

    /* Set divisor for 115200 baud (divisor = 1) */
    outb(SERIAL_COM1 + SERIAL_DLL, 0x01);
    outb(SERIAL_COM1 + SERIAL_DLH, 0x00);

    /* 8 bits, no parity, 1 stop bit (8N1) */
    outb(SERIAL_COM1 + SERIAL_LCR, 0x03);

    /* Enable FIFO, clear buffers, 14-byte threshold */
    outb(SERIAL_COM1 + SERIAL_FCR, 0xC7);

    /* Enable IRQs, RTS/DSR set */
    outb(SERIAL_COM1 + SERIAL_MCR, 0x0B);

    /* Test serial chip (loopback mode) */
    outb(SERIAL_COM1 + SERIAL_MCR, 0x1E);
    outb(SERIAL_COM1 + SERIAL_DATA, 0xAE);
    if (inb(SERIAL_COM1 + SERIAL_DATA) != 0xAE) {
        /* Serial port failed loopback test - may still work */
    }

    /* Normal operation mode */
    outb(SERIAL_COM1 + SERIAL_MCR, 0x0F);

    serial_initialized = 1;
}

/*
 * Check if serial transmit buffer is empty
 */
static int serial_transmit_ready(void)
{
    return inb(SERIAL_COM1 + SERIAL_LSR) & SERIAL_LSR_EMPTY;
}

/*
 * Write a character to serial port
 */
static void serial_putchar(char c)
{
    if (!serial_initialized) {
        return;
    }

    /* Wait for transmit buffer to be empty */
    while (!serial_transmit_ready())
        ;

    outb(SERIAL_COM1 + SERIAL_DATA, c);
}

/*============================================================================
 * VGA Text Mode Functions
 *============================================================================*/

/*
 * Clear the VGA screen
 */
void vga_clear(void)
{
    uint16_t *vga = VGA_BUFFER;
    uint16_t blank = (uint16_t)' ' | ((uint16_t)vga_attr << 8);

    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga[i] = blank;
    }

    vga_row = 0;
    vga_col = 0;
}

/*
 * Scroll the VGA screen up one line
 */
static void vga_scroll(void)
{
    uint16_t *vga = VGA_BUFFER;
    uint16_t blank = (uint16_t)' ' | ((uint16_t)vga_attr << 8);

    /* Move lines up */
    for (int i = 0; i < VGA_WIDTH * (VGA_HEIGHT - 1); i++) {
        vga[i] = vga[i + VGA_WIDTH];
    }

    /* Clear bottom line */
    for (int i = 0; i < VGA_WIDTH; i++) {
        vga[(VGA_HEIGHT - 1) * VGA_WIDTH + i] = blank;
    }
}

/*
 * Write a character to VGA text buffer
 */
static void vga_putchar(char c)
{
    uint16_t *vga = VGA_BUFFER;

    switch (c) {
    case '\n':
        vga_col = 0;
        vga_row++;
        break;

    case '\r':
        vga_col = 0;
        break;

    case '\t':
        /* Tab to next 8-column boundary */
        vga_col = (vga_col + 8) & ~7;
        if (vga_col >= VGA_WIDTH) {
            vga_col = 0;
            vga_row++;
        }
        break;

    case '\b':
        /* Backspace */
        if (vga_col > 0) {
            vga_col--;
            vga[vga_row * VGA_WIDTH + vga_col] =
                (uint16_t)' ' | ((uint16_t)vga_attr << 8);
        }
        break;

    default:
        if (c >= ' ') {
            vga[vga_row * VGA_WIDTH + vga_col] =
                (uint16_t)c | ((uint16_t)vga_attr << 8);
            vga_col++;
            if (vga_col >= VGA_WIDTH) {
                vga_col = 0;
                vga_row++;
            }
        }
        break;
    }

    /* Scroll if needed */
    while (vga_row >= VGA_HEIGHT) {
        vga_scroll();
        vga_row--;
    }
}

/*
 * Set VGA text color attribute
 */
void vga_set_color(uint8_t attr)
{
    vga_attr = attr;
}

/*============================================================================
 * Framebuffer Console (conditional)
 *============================================================================*/

extern int fbcon_is_active(void);
extern void fbcon_putchar(char c);

/*============================================================================
 * kprintf Output Capture
 *
 * When kprintf_capture_buf is non-NULL, kputchar also copies output
 * into the capture buffer. Used by the GUI terminal to capture
 * shell command output.
 *============================================================================*/

char *kprintf_capture_buf = 0;
int  *kprintf_capture_len = 0;
int   kprintf_capture_max = 0;

/*============================================================================
 * Unified Output Functions
 *============================================================================*/

/*
 * Write a character to VGA (or framebuffer console) and serial
 */
static void kputchar(char c)
{
    if (fbcon_is_active()) {
        fbcon_putchar(c);
    } else {
        vga_putchar(c);
    }
    serial_putchar(c);

    /* Also send '\r' with '\n' to serial for proper line endings */
    if (c == '\n') {
        serial_putchar('\r');
    }

    /* VirtIO console output (if available) */
    extern int virtio_console_available(void);
    extern void virtio_console_putchar(char c);
    if (virtio_console_available()) {
        virtio_console_putchar(c);
    }

    /* Capture output if hook is active */
    if (kprintf_capture_buf && kprintf_capture_len &&
        *kprintf_capture_len < kprintf_capture_max - 1) {
        kprintf_capture_buf[*kprintf_capture_len] = c;
        (*kprintf_capture_len)++;
        kprintf_capture_buf[*kprintf_capture_len] = '\0';
    }
}

/*
 * Write a string to both VGA and serial
 */
static void kputs(const char *s)
{
    while (*s) {
        kputchar(*s++);
    }
}

/*============================================================================
 * Printf Implementation
 *============================================================================*/

/*
 * Print unsigned integer in specified base
 */
static void print_uint(uint64_t val, int base, int width, char pad, int uppercase)
{
    char buf[64];
    int i = 0;
    const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";

    if (val == 0) {
        buf[i++] = '0';
    } else {
        while (val) {
            buf[i++] = digits[val % base];
            val /= base;
        }
    }

    /* Pad to width */
    while (i < width) {
        buf[i++] = pad;
    }

    /* Output in reverse order */
    while (i--) {
        kputchar(buf[i]);
    }
}

/*
 * Print signed integer
 */
static void print_int(int64_t val, int width, char pad)
{
    if (val < 0) {
        kputchar('-');
        if (width > 0) width--;
        val = -val;
    }
    print_uint((uint64_t)val, 10, width, pad, 0);
}

/*
 * Kernel printf - formatted output to VGA and serial
 *
 * Supported format specifiers:
 *   %d, %i  - signed decimal integer
 *   %u      - unsigned decimal integer
 *   %x, %X  - hexadecimal (lowercase/uppercase)
 *   %p      - pointer (always 16 hex digits with 0x prefix)
 *   %s      - string
 *   %c      - character
 *   %%      - literal percent sign
 *
 * Modifiers:
 *   l, ll   - long/long long
 *   0-9     - field width
 *   0       - zero padding
 */
int kprintf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    int count = 0;

    while (*fmt) {
        if (*fmt != '%') {
            kputchar(*fmt++);
            count++;
            continue;
        }

        fmt++;  /* Skip '%' */

        /* Field width and padding */
        int width = 0;
        char pad = ' ';

        if (*fmt == '0') {
            pad = '0';
            fmt++;
        }

        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt++ - '0');
        }

        /* Length modifiers */
        int is_long = 0;
        int is_longlong = 0;

        while (*fmt == 'l') {
            if (is_long) {
                is_longlong = 1;
            }
            is_long = 1;
            fmt++;
        }

        /* Format specifier */
        switch (*fmt) {
        case 'd':
        case 'i':
            if (is_longlong || is_long) {
                print_int(va_arg(args, int64_t), width, pad);
            } else {
                print_int(va_arg(args, int), width, pad);
            }
            break;

        case 'u':
            if (is_longlong || is_long) {
                print_uint(va_arg(args, uint64_t), 10, width, pad, 0);
            } else {
                print_uint(va_arg(args, unsigned int), 10, width, pad, 0);
            }
            break;

        case 'x':
            if (is_longlong || is_long) {
                print_uint(va_arg(args, uint64_t), 16, width, pad, 0);
            } else {
                print_uint(va_arg(args, unsigned int), 16, width, pad, 0);
            }
            break;

        case 'X':
            if (is_longlong || is_long) {
                print_uint(va_arg(args, uint64_t), 16, width, pad, 1);
            } else {
                print_uint(va_arg(args, unsigned int), 16, width, pad, 1);
            }
            break;

        case 'p':
            kputs("0x");
            print_uint(va_arg(args, uint64_t), 16, 16, '0', 0);
            break;

        case 's': {
            const char *s = va_arg(args, const char *);
            if (s) {
                kputs(s);
            } else {
                kputs("(null)");
            }
            break;
        }

        case 'c':
            kputchar((char)va_arg(args, int));
            break;

        case '%':
            kputchar('%');
            break;

        case '\0':
            /* Premature end of format string */
            goto done;

        default:
            /* Unknown format - print literally */
            kputchar('%');
            kputchar(*fmt);
            break;
        }

        fmt++;
    }

done:
    va_end(args);
    return count;
}

/*
 * Print a string followed by newline
 */
void kprintln(const char *s)
{
    kputs(s);
    kputchar('\n');
}

/*
 * Kernel panic - print message and halt
 */
void kpanic(const char *msg)
{
    vga_set_color(VGA_ATTR_ERROR);
    kprintf("\n\n*** KERNEL PANIC ***\n%s\n", msg);
    kprintf("System halted.\n");

    /* Halt forever */
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}
