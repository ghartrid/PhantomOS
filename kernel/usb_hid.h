/*
 * PhantomOS USB HID Boot Protocol Driver
 * "To Create, Not To Destroy"
 *
 * Handles USB HID keyboards and mice using boot protocol.
 * Injects input into existing PS/2 keyboard buffer and mouse state.
 */

#ifndef PHANTOMOS_USB_HID_H
#define PHANTOMOS_USB_HID_H

#include <stdint.h>
#include "usb.h"

/*============================================================================
 * HID Constants
 *============================================================================*/

#define USB_HID_KEYBOARD        1
#define USB_HID_MOUSE           2
#define USB_HID_MAX_DEVICES     4

/*============================================================================
 * HID Device State
 *============================================================================*/

struct usb_hid_device {
    int      active;                /* Device is active */
    int      type;                  /* USB_HID_KEYBOARD or USB_HID_MOUSE */
    int      usb_dev_index;         /* Index into usb_device array */
    uint8_t  address;               /* USB device address */
    uint8_t  endpoint;              /* Interrupt IN endpoint number */
    uint16_t max_packet;            /* Max packet size */
    uint8_t  interval;              /* Polling interval in ms */
    uint8_t  low_speed;             /* Low speed device flag */
    uint8_t  data_toggle;           /* Data toggle for interrupt IN */
    /* Polling hardware */
    struct uhci_qh *poll_qh;        /* QH in frame list */
    struct uhci_td *poll_td;        /* TD for interrupt IN */
    uint8_t *poll_buf;              /* DMA buffer for poll data */
    /* Keyboard previous state (for key press/release detection) */
    uint8_t  prev_report[8];
};

/*============================================================================
 * API
 *============================================================================*/

/* Initialize HID subsystem */
void usb_hid_init(void);

/* Register a newly-enumerated HID device */
int usb_hid_register(int usb_dev_index, int hid_type,
                     uint8_t address, uint8_t low_speed,
                     uint8_t ep_addr, uint16_t ep_mps, uint8_t interval);

/* Unregister a HID device (on disconnect) */
void usb_hid_unregister(int usb_dev_index);

/* Poll all HID devices for new data */
void usb_hid_poll(void);

/* Get number of active HID devices */
int usb_hid_device_count(void);

/* Print HID device info for shell */
void usb_hid_dump_status(void);

#endif /* PHANTOMOS_USB_HID_H */
