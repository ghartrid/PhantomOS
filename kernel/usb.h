/*
 * PhantomOS USB / UHCI Host Controller Driver
 * "To Create, Not To Destroy"
 *
 * UHCI (Universal Host Controller Interface) driver for USB 1.1.
 * Supports device enumeration, control transfers, and interrupt polling
 * for HID boot protocol devices (keyboards and mice).
 *
 * Targets older Intel chipsets (Celeron/Pentium era) which all include UHCI.
 */

#ifndef PHANTOMOS_USB_H
#define PHANTOMOS_USB_H

#include <stdint.h>
#include <stddef.h>

/*============================================================================
 * UHCI Register Offsets (I/O port relative to BAR4)
 *============================================================================*/

#define UHCI_REG_USBCMD         0x00    /* 16-bit: USB Command */
#define UHCI_REG_USBSTS         0x02    /* 16-bit: USB Status */
#define UHCI_REG_USBINTR        0x04    /* 16-bit: USB Interrupt Enable */
#define UHCI_REG_FRNUM          0x06    /* 16-bit: Frame Number */
#define UHCI_REG_FLBASEADD      0x08    /* 32-bit: Frame List Base Address */
#define UHCI_REG_SOFMOD         0x0C    /* 8-bit: Start of Frame Modify */
#define UHCI_REG_PORTSC1        0x10    /* 16-bit: Port 1 Status/Control */
#define UHCI_REG_PORTSC2        0x12    /* 16-bit: Port 2 Status/Control */

/*============================================================================
 * USBCMD Bits
 *============================================================================*/

#define UHCI_CMD_RS             (1 << 0)    /* Run/Stop */
#define UHCI_CMD_HCRESET        (1 << 1)    /* Host Controller Reset */
#define UHCI_CMD_GRESET         (1 << 2)    /* Global Reset */
#define UHCI_CMD_EGSM           (1 << 3)    /* Enter Global Suspend Mode */
#define UHCI_CMD_FGR            (1 << 4)    /* Force Global Resume */
#define UHCI_CMD_SWDBG          (1 << 5)    /* Software Debug */
#define UHCI_CMD_CF             (1 << 6)    /* Configure Flag */
#define UHCI_CMD_MAXP           (1 << 7)    /* Max Packet (1=64 bytes) */

/*============================================================================
 * USBSTS Bits
 *============================================================================*/

#define UHCI_STS_USBINT         (1 << 0)    /* USB Interrupt (IOC) */
#define UHCI_STS_ERROR          (1 << 1)    /* USB Error Interrupt */
#define UHCI_STS_RD             (1 << 2)    /* Resume Detect */
#define UHCI_STS_HSE            (1 << 3)    /* Host System Error */
#define UHCI_STS_HCPE           (1 << 4)    /* Host Controller Process Error */
#define UHCI_STS_HCH            (1 << 5)    /* HC Halted */

/*============================================================================
 * USBINTR Bits
 *============================================================================*/

#define UHCI_INTR_TIMEOUT_CRC   (1 << 0)
#define UHCI_INTR_RESUME        (1 << 1)
#define UHCI_INTR_IOC           (1 << 2)
#define UHCI_INTR_SHORT_PKT     (1 << 3)

/*============================================================================
 * Port Status/Control Bits
 *============================================================================*/

#define UHCI_PORT_CCS           (1 << 0)    /* Current Connect Status */
#define UHCI_PORT_CSC           (1 << 1)    /* Connect Status Change (W1C) */
#define UHCI_PORT_PE            (1 << 2)    /* Port Enabled */
#define UHCI_PORT_PEC           (1 << 3)    /* Port Enable Change (W1C) */
#define UHCI_PORT_LS_MASK       (3 << 4)    /* Line Status (D+/D-) */
#define UHCI_PORT_RD            (1 << 6)    /* Resume Detect */
#define UHCI_PORT_LSDA          (1 << 8)    /* Low Speed Device Attached */
#define UHCI_PORT_RESET         (1 << 9)    /* Port Reset */
#define UHCI_PORT_SUSPEND       (1 << 12)   /* Suspend */

/* Write-clear bits mask: writing 1 clears these status bits */
#define UHCI_PORT_WC_BITS       (UHCI_PORT_CSC | UHCI_PORT_PEC)

/*============================================================================
 * Transfer Descriptor Control/Status Bits
 *============================================================================*/

#define UHCI_TD_STATUS_BITSTUFF (1 << 17)
#define UHCI_TD_STATUS_CRC      (1 << 18)
#define UHCI_TD_STATUS_NAK      (1 << 19)
#define UHCI_TD_STATUS_BABBLE   (1 << 20)
#define UHCI_TD_STATUS_DBUFFER  (1 << 21)
#define UHCI_TD_STATUS_STALLED  (1 << 22)
#define UHCI_TD_STATUS_ACTIVE   (1 << 23)
#define UHCI_TD_CTRL_IOC        (1 << 24)
#define UHCI_TD_CTRL_ISO        (1 << 25)
#define UHCI_TD_CTRL_LS         (1 << 26)
#define UHCI_TD_CTRL_CERR_SHIFT 27
#define UHCI_TD_CTRL_SPD        (1 << 29)

/* Error mask: any of these bits means transfer error */
#define UHCI_TD_STATUS_ERROR    (UHCI_TD_STATUS_BITSTUFF | \
                                 UHCI_TD_STATUS_CRC      | \
                                 UHCI_TD_STATUS_BABBLE   | \
                                 UHCI_TD_STATUS_DBUFFER  | \
                                 UHCI_TD_STATUS_STALLED)

/*============================================================================
 * Link Pointer Bits
 *============================================================================*/

#define UHCI_LP_TERMINATE       (1 << 0)    /* Terminate (no valid pointer) */
#define UHCI_LP_QH              (1 << 1)    /* Pointer is to QH (not TD) */
#define UHCI_LP_DEPTH           (1 << 2)    /* Depth-first traversal (TD only) */

/*============================================================================
 * USB PID Values
 *============================================================================*/

#define USB_PID_SETUP           0x2D
#define USB_PID_IN              0x69
#define USB_PID_OUT             0xE1

/*============================================================================
 * USB Standard Request Codes
 *============================================================================*/

#define USB_REQ_GET_STATUS          0x00
#define USB_REQ_CLEAR_FEATURE       0x01
#define USB_REQ_SET_FEATURE         0x03
#define USB_REQ_SET_ADDRESS         0x05
#define USB_REQ_GET_DESCRIPTOR      0x06
#define USB_REQ_SET_CONFIGURATION   0x09

/* HID Class Requests */
#define USB_REQ_HID_SET_IDLE        0x0A
#define USB_REQ_HID_SET_PROTOCOL    0x0B

/* bmRequestType fields */
#define USB_RT_HOST_TO_DEV      0x00
#define USB_RT_DEV_TO_HOST      0x80
#define USB_RT_CLASS            0x20
#define USB_RT_INTERFACE        0x01

/*============================================================================
 * USB Descriptor Types
 *============================================================================*/

#define USB_DESC_DEVICE         0x01
#define USB_DESC_CONFIGURATION  0x02
#define USB_DESC_STRING         0x03
#define USB_DESC_INTERFACE      0x04
#define USB_DESC_ENDPOINT       0x05
#define USB_DESC_HID            0x21

/*============================================================================
 * USB Class Codes
 *============================================================================*/

#define USB_CLASS_HID           0x03
#define USB_SUBCLASS_BOOT       0x01
#define USB_PROTOCOL_KEYBOARD   0x01
#define USB_PROTOCOL_MOUSE      0x02

/* HID Boot Protocol ID */
#define USB_HID_PROTOCOL_BOOT   0

/*============================================================================
 * Pool Sizes and Limits
 *============================================================================*/

#define UHCI_MAX_TD             64
#define UHCI_MAX_QH             16
#define USB_MAX_DEVICES         4
#define UHCI_FRAME_COUNT        1024
#define UHCI_NUM_PORTS          2

/*============================================================================
 * Data Structures
 *============================================================================*/

/* UHCI Transfer Descriptor (32 bytes, 16-byte aligned) */
struct uhci_td {
    volatile uint32_t link;         /* Next TD/QH pointer, or Terminate */
    volatile uint32_t ctrl_status;  /* Control and Status */
    uint32_t token;                 /* Token (PID, device addr, endpoint, etc.) */
    uint32_t buffer;                /* Buffer Pointer (physical address) */
    /* Software-use fields (not read by hardware) */
    uint32_t _sw_reserved[4];
} __attribute__((packed, aligned(16)));

/* UHCI Queue Head (16 bytes, 16-byte aligned) */
struct uhci_qh {
    volatile uint32_t head_link;    /* Horizontal: next QH/TD pointer */
    volatile uint32_t element;      /* Vertical: first TD in this queue */
    /* Software-use fields */
    uint32_t _sw_reserved[2];
} __attribute__((aligned(16)));

/* USB Device Descriptor (18 bytes) */
struct usb_device_desc {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdUSB;
    uint8_t  bDeviceClass;
    uint8_t  bDeviceSubClass;
    uint8_t  bDeviceProtocol;
    uint8_t  bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t  iManufacturer;
    uint8_t  iProduct;
    uint8_t  iSerialNumber;
    uint8_t  bNumConfigurations;
} __attribute__((packed));

/* USB Configuration Descriptor (9 bytes) */
struct usb_config_desc {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t wTotalLength;
    uint8_t  bNumInterfaces;
    uint8_t  bConfigurationValue;
    uint8_t  iConfiguration;
    uint8_t  bmAttributes;
    uint8_t  bMaxPower;
} __attribute__((packed));

/* USB Interface Descriptor (9 bytes) */
struct usb_interface_desc {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bInterfaceNumber;
    uint8_t  bAlternateSetting;
    uint8_t  bNumEndpoints;
    uint8_t  bInterfaceClass;
    uint8_t  bInterfaceSubClass;
    uint8_t  bInterfaceProtocol;
    uint8_t  iInterface;
} __attribute__((packed));

/* USB Endpoint Descriptor (7 bytes) */
struct usb_endpoint_desc {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bEndpointAddress;      /* bit 7 = direction (1=IN) */
    uint8_t  bmAttributes;          /* bits 1:0 = transfer type */
    uint16_t wMaxPacketSize;
    uint8_t  bInterval;             /* Polling interval (ms for LS/FS) */
} __attribute__((packed));

/* USB Setup Packet (8 bytes) */
struct usb_setup_packet {
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} __attribute__((packed));

/* USB device state */
enum usb_device_state {
    USB_DEV_NONE = 0,
    USB_DEV_ATTACHED,
    USB_DEV_ADDRESSED,
    USB_DEV_CONFIGURED,
};

/* Per-device structure */
struct usb_device {
    enum usb_device_state state;
    uint8_t  address;               /* USB address (1-127) */
    uint8_t  port;                  /* Root hub port (0 or 1) */
    uint8_t  low_speed;             /* 1 = low speed device */
    uint8_t  max_packet_ep0;        /* Max packet size for EP0 */
    struct usb_device_desc dev_desc;
    uint8_t  config_data[256];      /* Raw configuration descriptor */
    uint16_t config_len;            /* Config descriptor total length */
    /* Parsed HID info */
    uint8_t  iface_class;
    uint8_t  iface_subclass;
    uint8_t  iface_protocol;
    uint8_t  iface_number;
    uint8_t  int_ep_addr;           /* Interrupt IN endpoint address */
    uint16_t int_ep_mps;            /* Interrupt endpoint max packet size */
    uint8_t  int_ep_interval;       /* Polling interval (ms) */
};

/*============================================================================
 * Public API
 *============================================================================*/

/* Initialize UHCI host controller and enumerate USB devices */
void usb_init(void);

/* Poll for USB device events and HID data */
void usb_poll(void);

/* Check if USB subsystem is initialized */
int usb_is_initialized(void);

/* Get number of connected USB devices */
int usb_device_count(void);

/* Get device info by index */
const struct usb_device *usb_get_device(int index);

/* Print USB status for shell */
void usb_dump_status(void);

/* Get UHCI I/O base (for HID polling TD setup) */
uint16_t usb_get_io_base(void);

/* Allocate/free TDs and QHs (for HID polling) */
struct uhci_td *usb_alloc_td(void);
void usb_free_td(struct uhci_td *td);
struct uhci_qh *usb_alloc_qh(void);
void usb_free_qh(struct uhci_qh *qh);

/* Get a DMA-safe buffer for HID poll data */
uint8_t *usb_get_poll_buffer(int device_index);

/* Insert/remove a QH into the frame list for periodic polling */
void usb_schedule_qh(struct uhci_qh *qh, int interval);
void usb_unschedule_qh(struct uhci_qh *qh, int interval);

#endif /* PHANTOMOS_USB_H */
