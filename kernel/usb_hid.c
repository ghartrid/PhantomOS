/*
 * PhantomOS USB HID Boot Protocol Driver
 * "To Create, Not To Destroy"
 *
 * Handles USB HID keyboards and mice using boot protocol.
 * Keyboards inject into the existing PS/2 keyboard buffer.
 * Mice inject into the existing PS/2 mouse state structure.
 */

#include "usb_hid.h"
#include "usb.h"
#include "keyboard.h"
#include "mouse.h"
#include "io.h"
#include <stdint.h>
#include <stddef.h>

/*============================================================================
 * External Declarations
 *============================================================================*/

extern int kprintf(const char *fmt, ...);
extern void *memset(void *s, int c, size_t n);

/*============================================================================
 * HID Usage Code to ASCII Translation Tables (US QWERTY)
 *
 * USB HID keyboard usage codes:
 *   0x04 = 'a', 0x05 = 'b', ..., 0x1D = 'z'
 *   0x1E = '1', 0x1F = '2', ..., 0x27 = '0'
 *   0x28 = Enter, 0x29 = Escape, 0x2A = Backspace, 0x2B = Tab, 0x2C = Space
 *   0x2D = '-', 0x2E = '=', 0x2F = '[', 0x30 = ']', 0x31 = '\', etc.
 *============================================================================*/

static const char hid_to_ascii_normal[128] = {
    /*  0x00 */ 0,    0,    0,    0,    'a',  'b',  'c',  'd',
    /*  0x08 */ 'e',  'f',  'g',  'h',  'i',  'j',  'k',  'l',
    /*  0x10 */ 'm',  'n',  'o',  'p',  'q',  'r',  's',  't',
    /*  0x18 */ 'u',  'v',  'w',  'x',  'y',  'z',  '1',  '2',
    /*  0x20 */ '3',  '4',  '5',  '6',  '7',  '8',  '9',  '0',
    /*  0x28 */ '\n', 27,   '\b', '\t', ' ',  '-',  '=',  '[',
    /*  0x30 */ ']',  '\\', 0,    ';',  '\'', '`',  ',',  '.',
    /*  0x38 */ '/',  0,    0,    0,    0,    0,    0,    0,
    /*  0x40 */ 0,    0,    0,    0,    0,    0,    0,    0,
    /*  0x48 */ 0,    0,    0,    0,    0,    0,    0,    0,
    /*  0x50 */ 0,    0,    0,    0,    '/',  '*',  '-',  '+',
    /*  0x58 */ '\n', '1',  '2',  '3',  '4',  '5',  '6',  '7',
    /*  0x60 */ '8',  '9',  '0',  '.',  0,    0,    0,    0,
    /*  0x68 */ 0,    0,    0,    0,    0,    0,    0,    0,
    /*  0x70 */ 0,    0,    0,    0,    0,    0,    0,    0,
    /*  0x78 */ 0,    0,    0,    0,    0,    0,    0,    0,
};

static const char hid_to_ascii_shift[128] = {
    /*  0x00 */ 0,    0,    0,    0,    'A',  'B',  'C',  'D',
    /*  0x08 */ 'E',  'F',  'G',  'H',  'I',  'J',  'K',  'L',
    /*  0x10 */ 'M',  'N',  'O',  'P',  'Q',  'R',  'S',  'T',
    /*  0x18 */ 'U',  'V',  'W',  'X',  'Y',  'Z',  '!',  '@',
    /*  0x20 */ '#',  '$',  '%',  '^',  '&',  '*',  '(',  ')',
    /*  0x28 */ '\n', 27,   '\b', '\t', ' ',  '_',  '+',  '{',
    /*  0x30 */ '}',  '|',  0,    ':',  '"',  '~',  '<',  '>',
    /*  0x38 */ '?',  0,    0,    0,    0,    0,    0,    0,
    /*  0x40 */ 0,    0,    0,    0,    0,    0,    0,    0,
    /*  0x48 */ 0,    0,    0,    0,    0,    0,    0,    0,
    /*  0x50 */ 0,    0,    0,    0,    '/',  '*',  '-',  '+',
    /*  0x58 */ '\n', '1',  '2',  '3',  '4',  '5',  '6',  '7',
    /*  0x60 */ '8',  '9',  '0',  '.',  0,    0,    0,    0,
    /*  0x68 */ 0,    0,    0,    0,    0,    0,    0,    0,
    /*  0x70 */ 0,    0,    0,    0,    0,    0,    0,    0,
    /*  0x78 */ 0,    0,    0,    0,    0,    0,    0,    0,
};

/* HID modifier bit positions (byte 0 of keyboard boot report) */
#define HID_MOD_LCTRL   (1 << 0)
#define HID_MOD_LSHIFT  (1 << 1)
#define HID_MOD_LALT    (1 << 2)
#define HID_MOD_LGUI    (1 << 3)
#define HID_MOD_RCTRL   (1 << 4)
#define HID_MOD_RSHIFT  (1 << 5)
#define HID_MOD_RALT    (1 << 6)
#define HID_MOD_RGUI    (1 << 7)

/*============================================================================
 * HID Device State
 *============================================================================*/

static struct usb_hid_device hid_devices[USB_HID_MAX_DEVICES];
static int hid_count = 0;

/*============================================================================
 * Keyboard Report Processing
 *============================================================================*/

static void hid_process_keyboard(struct usb_hid_device *hid,
                                  const uint8_t *report)
{
    uint8_t modifiers = report[0];
    int shift = (modifiers & (HID_MOD_LSHIFT | HID_MOD_RSHIFT)) != 0;
    int ctrl = (modifiers & (HID_MOD_LCTRL | HID_MOD_RCTRL)) != 0;

    /* Check each keycode slot (bytes 2-7) for newly pressed keys */
    for (int i = 2; i < 8; i++) {
        uint8_t code = report[i];
        if (code == 0 || code == 1) continue; /* No event / error rollover */

        /* Check if this key was already in the previous report */
        int already_pressed = 0;
        for (int j = 2; j < 8; j++) {
            if (hid->prev_report[j] == code) {
                already_pressed = 1;
                break;
            }
        }
        if (already_pressed) continue;

        /* New key press — translate to ASCII */
        char c = 0;
        if (code < 128) {
            if (shift) {
                c = hid_to_ascii_shift[code];
            } else {
                c = hid_to_ascii_normal[code];
            }
        }

        /* Apply Ctrl modifier */
        if (ctrl && c >= 'a' && c <= 'z') {
            c = c - 'a' + 1;
        } else if (ctrl && c >= 'A' && c <= 'Z') {
            c = c - 'A' + 1;
        }

        /* Inject into keyboard buffer */
        if (c != 0) {
            keyboard_inject_char(c);
        }
    }

    /* Save current report as previous */
    for (int i = 0; i < 8; i++) {
        hid->prev_report[i] = report[i];
    }
}

/*============================================================================
 * Mouse Report Processing
 *============================================================================*/

static void hid_process_mouse(struct usb_hid_device *hid,
                               const uint8_t *report, int len)
{
    (void)hid;
    if (len < 3) return;

    uint8_t buttons = report[0] & 0x07;

    /* USB tablet devices (e.g. QEMU -usbdevice tablet) send 6-byte reports
     * with 16-bit absolute X/Y coordinates in range [0, 32767]:
     *   Byte 0: buttons
     *   Bytes 1-2: X absolute (little-endian, 16-bit)
     *   Bytes 3-4: Y absolute (little-endian, 16-bit)
     *   Byte 5: wheel (optional)
     * Standard USB mice send 3-4 byte reports with 8-bit relative deltas. */
    if (len >= 6) {
        int abs_x = (int)(report[1] | ((uint16_t)report[2] << 8));
        int abs_y = (int)(report[3] | ((uint16_t)report[4] << 8));
        mouse_set_absolute(abs_x, abs_y, buttons);
    } else {
        int8_t dx = (int8_t)report[1];
        int8_t dy = (int8_t)report[2];
        mouse_inject_movement(dx, dy, buttons);
    }
}

/*============================================================================
 * Interrupt Polling Setup
 *============================================================================*/

static int hid_setup_polling(struct usb_hid_device *hid)
{
    /* Allocate QH and TD for interrupt polling */
    hid->poll_qh = usb_alloc_qh();
    hid->poll_td = usb_alloc_td();
    if (!hid->poll_qh || !hid->poll_td) {
        if (hid->poll_qh) usb_free_qh(hid->poll_qh);
        if (hid->poll_td) usb_free_td(hid->poll_td);
        hid->poll_qh = NULL;
        hid->poll_td = NULL;
        return -1;
    }

    /* Get DMA buffer for this device */
    hid->poll_buf = usb_get_poll_buffer(hid->usb_dev_index);
    if (!hid->poll_buf) {
        usb_free_qh(hid->poll_qh);
        usb_free_td(hid->poll_td);
        hid->poll_qh = NULL;
        hid->poll_td = NULL;
        return -1;
    }

    memset(hid->poll_buf, 0, 64);

    /* Configure TD for interrupt IN transfer */
    uint32_t ls_bit = hid->low_speed ? UHCI_TD_CTRL_LS : 0;
    uint16_t pkt_size = hid->max_packet;
    if (pkt_size > 64) pkt_size = 64;

    hid->poll_td->ctrl_status = UHCI_TD_STATUS_ACTIVE | ls_bit |
                                 UHCI_TD_CTRL_IOC |
                                 (3 << UHCI_TD_CTRL_CERR_SHIFT);
    /* MaxLen field: actual_len - 1 (0x7FF for zero-length) */
    uint32_t maxlen = (pkt_size > 0) ? (pkt_size - 1) : 0x7FF;
    hid->poll_td->token = (maxlen << 21) |
                           ((uint32_t)hid->data_toggle << 19) |
                           ((uint32_t)hid->endpoint << 15) |
                           ((uint32_t)hid->address << 8) |
                           USB_PID_IN;
    hid->poll_td->buffer = (uint32_t)(uintptr_t)hid->poll_buf;
    hid->poll_td->link = UHCI_LP_TERMINATE;

    /* Set up QH pointing to this TD */
    hid->poll_qh->element = (uint32_t)(uintptr_t)hid->poll_td;
    hid->poll_qh->head_link = UHCI_LP_TERMINATE;

    /* Schedule QH in frame list at the device's polling interval */
    int interval = hid->interval;
    if (interval < 1) interval = 1;
    if (interval > 128) interval = 128;
    /* Round up to nearest power of 2 for clean scheduling */
    int sched_interval = 1;
    while (sched_interval < interval && sched_interval < 128) {
        sched_interval <<= 1;
    }

    usb_schedule_qh(hid->poll_qh, sched_interval);

    kprintf("[USB HID] Polling setup: addr %d ep %d interval %d ms\n",
            hid->address, hid->endpoint, sched_interval);
    return 0;
}

static void hid_stop_polling(struct usb_hid_device *hid)
{
    if (hid->poll_qh) {
        int interval = hid->interval;
        if (interval < 1) interval = 1;
        if (interval > 128) interval = 128;
        int sched_interval = 1;
        while (sched_interval < interval && sched_interval < 128) {
            sched_interval <<= 1;
        }
        usb_unschedule_qh(hid->poll_qh, sched_interval);
        usb_free_qh(hid->poll_qh);
        hid->poll_qh = NULL;
    }
    if (hid->poll_td) {
        usb_free_td(hid->poll_td);
        hid->poll_td = NULL;
    }
}

/*============================================================================
 * Public API
 *============================================================================*/

void usb_hid_init(void)
{
    memset(hid_devices, 0, sizeof(hid_devices));
    hid_count = 0;
}

int usb_hid_register(int usb_dev_index, int hid_type,
                     uint8_t address, uint8_t low_speed,
                     uint8_t ep_addr, uint16_t ep_mps, uint8_t interval)
{
    /* Find a free HID slot */
    int slot = -1;
    for (int i = 0; i < USB_HID_MAX_DEVICES; i++) {
        if (!hid_devices[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        kprintf("[USB HID] No free HID device slots\n");
        return -1;
    }

    struct usb_hid_device *hid = &hid_devices[slot];
    memset(hid, 0, sizeof(*hid));
    hid->active = 1;
    hid->type = hid_type;
    hid->usb_dev_index = usb_dev_index;
    hid->address = address;
    hid->endpoint = ep_addr & 0x0F;
    hid->max_packet = ep_mps;
    hid->interval = interval;
    hid->low_speed = low_speed;
    hid->data_toggle = 0;

    /* Set up interrupt polling */
    if (hid_setup_polling(hid) < 0) {
        kprintf("[USB HID] Failed to set up polling for device %d\n", address);
        hid->active = 0;
        return -1;
    }

    hid_count++;
    kprintf("[USB HID] Registered %s at address %d endpoint %d\n",
            hid_type == USB_HID_KEYBOARD ? "keyboard" : "mouse",
            address, hid->endpoint);
    return slot;
}

void usb_hid_unregister(int usb_dev_index)
{
    for (int i = 0; i < USB_HID_MAX_DEVICES; i++) {
        if (hid_devices[i].active &&
            hid_devices[i].usb_dev_index == usb_dev_index) {
            hid_stop_polling(&hid_devices[i]);
            hid_devices[i].active = 0;
            hid_count--;
            kprintf("[USB HID] Unregistered device at address %d\n",
                    hid_devices[i].address);
        }
    }
}

void usb_hid_poll(void)
{
    for (int i = 0; i < USB_HID_MAX_DEVICES; i++) {
        struct usb_hid_device *hid = &hid_devices[i];
        if (!hid->active || !hid->poll_td) continue;

        uint32_t status = hid->poll_td->ctrl_status;

        /* Check if TD completed (Active bit cleared) */
        if (status & UHCI_TD_STATUS_ACTIVE) {
            continue; /* Still waiting for data */
        }

        /* Check for errors */
        if (status & UHCI_TD_STATUS_STALLED) {
            /* Endpoint stalled — re-arm with reset toggle */
            hid->data_toggle = 0;
            goto rearm;
        }

        if (status & UHCI_TD_STATUS_NAK) {
            /* NAK = no new data, normal for HID. Re-arm. */
            goto rearm;
        }

        if (status & UHCI_TD_STATUS_ERROR) {
            /* Other error — re-arm */
            goto rearm;
        }

        /* Successful transfer — process data */
        int actual_len = (status & 0x7FF) + 1;  /* ActualLength field */
        if (actual_len > 0 && actual_len <= 64) {
            if (hid->type == USB_HID_KEYBOARD) {
                if (actual_len >= 8) {
                    hid_process_keyboard(hid, hid->poll_buf);
                }
            } else if (hid->type == USB_HID_MOUSE) {
                hid_process_mouse(hid, hid->poll_buf, actual_len);
            }
        }

        /* Toggle data toggle */
        hid->data_toggle ^= 1;

    rearm:
        /* Re-arm TD for next poll */
        memset(hid->poll_buf, 0, 64);

        {
            uint32_t ls_bit = hid->low_speed ? UHCI_TD_CTRL_LS : 0;
            hid->poll_td->ctrl_status = UHCI_TD_STATUS_ACTIVE | ls_bit |
                                         UHCI_TD_CTRL_IOC |
                                         (3 << UHCI_TD_CTRL_CERR_SHIFT);
        }

        /* Update token with current data toggle */
        {
            uint16_t pkt_size = hid->max_packet;
            if (pkt_size > 64) pkt_size = 64;
            uint32_t maxlen = (pkt_size > 0) ? (pkt_size - 1) : 0x7FF;
            hid->poll_td->token = (maxlen << 21) |
                                   ((uint32_t)hid->data_toggle << 19) |
                                   ((uint32_t)hid->endpoint << 15) |
                                   ((uint32_t)hid->address << 8) |
                                   USB_PID_IN;
        }

        /* Re-point QH element to this TD */
        hid->poll_qh->element = (uint32_t)(uintptr_t)hid->poll_td;
    }
}

int usb_hid_device_count(void)
{
    return hid_count;
}

void usb_hid_dump_status(void)
{
    kprintf("\nUSB HID Devices\n");
    kprintf("================\n");

    if (hid_count == 0) {
        kprintf("  No HID devices active\n");
        return;
    }

    for (int i = 0; i < USB_HID_MAX_DEVICES; i++) {
        struct usb_hid_device *hid = &hid_devices[i];
        if (!hid->active) continue;

        kprintf("  [%d] %s: addr %d, EP%d IN, %d byte max, %d ms interval\n",
                i,
                hid->type == USB_HID_KEYBOARD ? "Keyboard" : "Mouse",
                hid->address,
                hid->endpoint,
                hid->max_packet,
                hid->interval);
        kprintf("       %s, data toggle %d\n",
                hid->low_speed ? "Low speed" : "Full speed",
                hid->data_toggle);
    }
}
