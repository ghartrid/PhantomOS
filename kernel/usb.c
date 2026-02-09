/*
 * PhantomOS USB / UHCI Host Controller Driver
 * "To Create, Not To Destroy"
 *
 * UHCI (Universal Host Controller Interface) driver for USB 1.1.
 * Detects UHCI controller via PCI, initializes frame list and TD/QH pools,
 * enumerates connected devices, and sets up HID boot protocol devices.
 */

#include "usb.h"
#include "usb_hid.h"
#include "pci.h"
#include "io.h"
#include "pmm.h"
#include "timer.h"
#include <stdint.h>
#include <stddef.h>

/*============================================================================
 * External Declarations
 *============================================================================*/

extern int kprintf(const char *fmt, ...);
extern void *memset(void *s, int c, size_t n);
extern void *memcpy(void *dest, const void *src, size_t n);

/*============================================================================
 * DMA Memory Layout
 *
 * 4 contiguous pages (16KB) from PMM, identity-mapped:
 *   Page 0 (0x0000): Frame list      (1024 x 4 = 4096 bytes)
 *   Page 1 (0x1000): TD pool         (64 x 32 = 2048 bytes)
 *   Page 2 (0x2000): QH pool         (16 x 16 = 256 bytes)
 *                     Setup buffer    (offset 0x100, 8 bytes)
 *                     Control buffer  (offset 0x200, 256 bytes)
 *   Page 3 (0x3000): HID poll buffers (4 x 64 = 256 bytes)
 *============================================================================*/

#define DMA_PAGES           4
#define DMA_FRAME_LIST_OFF  0x0000
#define DMA_TD_POOL_OFF     0x1000
#define DMA_QH_POOL_OFF     0x2000
#define DMA_SETUP_BUF_OFF   0x2100
#define DMA_CTRL_BUF_OFF    0x2200
#define DMA_POLL_BUF_OFF    0x3000
#define DMA_POLL_BUF_STRIDE 64

/*============================================================================
 * Controller State
 *============================================================================*/

static struct {
    int         initialized;
    uint16_t    io_base;            /* UHCI I/O base from BAR4 */
    uint8_t     irq;                /* PCI IRQ line */
    uint64_t    dma_base;           /* Physical address of DMA region */

    /* Pointers into DMA region */
    volatile uint32_t  *frame_list;
    struct uhci_td     *td_pool;
    struct uhci_qh     *qh_pool;
    uint8_t            *setup_buf;
    uint8_t            *ctrl_buf;
    uint8_t            *poll_bufs;

    /* Pool allocation tracking */
    uint8_t     td_used[UHCI_MAX_TD];
    uint8_t     qh_used[UHCI_MAX_QH];

    /* USB devices */
    struct usb_device devices[USB_MAX_DEVICES];
    uint8_t     next_address;       /* Next USB address to assign (1-127) */
    int         device_count;
} uhci;

/*============================================================================
 * UHCI Register I/O
 *============================================================================*/

static inline uint16_t uhci_read16(uint16_t reg)
{
    return inw(uhci.io_base + reg);
}

static inline void uhci_write16(uint16_t reg, uint16_t val)
{
    outw(uhci.io_base + reg, val);
}

static inline uint32_t uhci_read32(uint16_t reg)
{
    return inl(uhci.io_base + reg);
}

static inline void uhci_write32(uint16_t reg, uint32_t val)
{
    outl(uhci.io_base + reg, val);
}

static inline uint8_t uhci_read8(uint16_t reg)
{
    return inb(uhci.io_base + reg);
}

/*============================================================================
 * TD/QH Pool Management
 *============================================================================*/

struct uhci_td *usb_alloc_td(void)
{
    for (int i = 0; i < UHCI_MAX_TD; i++) {
        if (!uhci.td_used[i]) {
            uhci.td_used[i] = 1;
            struct uhci_td *td = &uhci.td_pool[i];
            memset(td, 0, sizeof(*td));
            td->link = UHCI_LP_TERMINATE;
            return td;
        }
    }
    return NULL;
}

void usb_free_td(struct uhci_td *td)
{
    if (!td) return;
    int idx = (int)(td - uhci.td_pool);
    if (idx >= 0 && idx < UHCI_MAX_TD) {
        uhci.td_used[idx] = 0;
    }
}

struct uhci_qh *usb_alloc_qh(void)
{
    for (int i = 0; i < UHCI_MAX_QH; i++) {
        if (!uhci.qh_used[i]) {
            uhci.qh_used[i] = 1;
            struct uhci_qh *qh = &uhci.qh_pool[i];
            memset(qh, 0, sizeof(*qh));
            qh->head_link = UHCI_LP_TERMINATE;
            qh->element = UHCI_LP_TERMINATE;
            return qh;
        }
    }
    return NULL;
}

void usb_free_qh(struct uhci_qh *qh)
{
    if (!qh) return;
    int idx = (int)(qh - uhci.qh_pool);
    if (idx >= 0 && idx < UHCI_MAX_QH) {
        uhci.qh_used[idx] = 0;
    }
}

/* Get physical address of a TD (identity-mapped) */
static inline uint32_t td_phys(struct uhci_td *td)
{
    return (uint32_t)(uintptr_t)td;
}

/* Get physical address of a QH (identity-mapped) */
static inline uint32_t qh_phys(struct uhci_qh *qh)
{
    return (uint32_t)(uintptr_t)qh;
}

/*============================================================================
 * TD Token Builder
 *============================================================================*/

static uint32_t uhci_td_token(uint8_t pid, uint8_t dev_addr,
                               uint8_t endpoint, uint8_t data_toggle,
                               uint16_t max_len)
{
    /* MaxLen field: actual_len - 1 (0x7FF = zero-length) */
    uint32_t maxlen = (max_len > 0) ? (max_len - 1) : 0x7FF;
    return (maxlen << 21) |
           ((uint32_t)data_toggle << 19) |
           ((uint32_t)endpoint << 15) |
           ((uint32_t)dev_addr << 8) |
           pid;
}

/*============================================================================
 * Frame List Scheduling
 *============================================================================*/

void usb_schedule_qh(struct uhci_qh *qh, int interval)
{
    if (!qh || interval <= 0) return;
    if (interval > 128) interval = 128;

    uint32_t qh_addr = qh_phys(qh) | UHCI_LP_QH;

    for (int i = 0; i < UHCI_FRAME_COUNT; i += interval) {
        /* Chain: new QH → old entry */
        uint32_t old_entry = uhci.frame_list[i];
        qh->head_link = old_entry;
        uhci.frame_list[i] = qh_addr;
    }
}

void usb_unschedule_qh(struct uhci_qh *qh, int interval)
{
    if (!qh || interval <= 0) return;
    if (interval > 128) interval = 128;

    uint32_t qh_addr = qh_phys(qh) | UHCI_LP_QH;

    for (int i = 0; i < UHCI_FRAME_COUNT; i += interval) {
        if (uhci.frame_list[i] == qh_addr) {
            uhci.frame_list[i] = qh->head_link;
        }
    }
}

/*============================================================================
 * Control Transfer (Blocking)
 *============================================================================*/

static int uhci_control_transfer(uint8_t dev_addr, int low_speed,
                                  uint8_t max_packet,
                                  struct usb_setup_packet *setup,
                                  void *data, uint16_t data_len,
                                  int direction_in)
{
    /* Allocate QH */
    struct uhci_qh *qh = usb_alloc_qh();
    if (!qh) return -1;

    /* Copy setup packet to DMA buffer */
    memcpy(uhci.setup_buf, setup, 8);

    /* SETUP TD */
    struct uhci_td *td_setup = usb_alloc_td();
    if (!td_setup) { usb_free_qh(qh); return -1; }

    uint32_t ls_bit = low_speed ? UHCI_TD_CTRL_LS : 0;
    td_setup->ctrl_status = UHCI_TD_STATUS_ACTIVE | ls_bit |
                            (3 << UHCI_TD_CTRL_CERR_SHIFT);
    td_setup->token = uhci_td_token(USB_PID_SETUP, dev_addr, 0, 0, 8);
    td_setup->buffer = (uint32_t)(uintptr_t)uhci.setup_buf;

    struct uhci_td *td_prev = td_setup;
    uint8_t data_toggle = 1;

    /* DATA TD(s) */
    if (data_len > 0 && data) {
        uint8_t pid = direction_in ? USB_PID_IN : USB_PID_OUT;
        uint16_t offset = 0;

        /* Copy OUT data to DMA buffer */
        if (!direction_in) {
            memcpy(uhci.ctrl_buf, data, data_len);
        }

        while (offset < data_len) {
            uint16_t chunk = data_len - offset;
            if (chunk > max_packet) chunk = max_packet;

            struct uhci_td *td_data = usb_alloc_td();
            if (!td_data) goto cleanup;

            td_data->ctrl_status = UHCI_TD_STATUS_ACTIVE | ls_bit |
                                   (3 << UHCI_TD_CTRL_CERR_SHIFT);
            td_data->token = uhci_td_token(pid, dev_addr, 0,
                                            data_toggle, chunk);
            td_data->buffer = (uint32_t)(uintptr_t)(uhci.ctrl_buf + offset);

            /* Link previous TD to this one */
            td_prev->link = td_phys(td_data) | UHCI_LP_DEPTH;
            td_prev = td_data;


            data_toggle ^= 1;
            offset += chunk;
        }
    }

    /* STATUS TD */
    struct uhci_td *td_status = usb_alloc_td();
    if (!td_status) goto cleanup;

    uint8_t status_pid = (data_len > 0 && direction_in) ? USB_PID_OUT : USB_PID_IN;
    td_status->ctrl_status = UHCI_TD_STATUS_ACTIVE | ls_bit | UHCI_TD_CTRL_IOC |
                             (3 << UHCI_TD_CTRL_CERR_SHIFT);
    td_status->token = uhci_td_token(status_pid, dev_addr, 0, 1, 0);
    td_status->buffer = 0;

    td_prev->link = td_phys(td_status) | UHCI_LP_DEPTH;

    /* Set up QH */
    qh->element = td_phys(td_setup);
    qh->head_link = UHCI_LP_TERMINATE;

    /* Insert QH into every frame list entry for immediate execution.
     * UHCI processes frame_list[FRNUM % 1024] each frame (1000 frames/sec).
     * Since all original entries are Terminate (for control transfers before
     * any HID devices are scheduled), we can safely set all entries to the QH
     * and the QH's head_link can be Terminate. */
    qh->head_link = UHCI_LP_TERMINATE;
    uint32_t qh_entry = qh_phys(qh) | UHCI_LP_QH;
    for (int i = 0; i < UHCI_FRAME_COUNT; i++) {
        uhci.frame_list[i] = qh_entry;
    }

    /* Poll for completion */
    uint64_t start = timer_get_ticks();
    uint64_t timeout = 50; /* 500ms at 100Hz */
    int result = -1;

    while ((timer_get_ticks() - start) < timeout) {
        /* Check SETUP TD first */
        uint32_t setup_st = td_setup->ctrl_status;
        if (setup_st & UHCI_TD_STATUS_STALLED) {
            break;
        }
        if (setup_st & (UHCI_TD_STATUS_ERROR & ~UHCI_TD_STATUS_NAK)) {
            break;
        }

        /* Check status TD for completion */
        uint32_t status = td_status->ctrl_status;
        if (!(status & UHCI_TD_STATUS_ACTIVE)) {
            if (status & UHCI_TD_STATUS_ERROR) {
                result = -1;
            } else {
                /* Copy IN data from DMA buffer */
                if (direction_in && data_len > 0 && data) {
                    memcpy(data, uhci.ctrl_buf, data_len);
                }
                result = 0;
            }
            break;
        }
    }

    /* Remove QH from frame list — reset to Terminate */
    for (int i = 0; i < UHCI_FRAME_COUNT; i++) {
        uhci.frame_list[i] = UHCI_LP_TERMINATE;
    }

    /* Free all TDs */
    {
        struct uhci_td *td = td_setup;
        while (td) {
            struct uhci_td *next = NULL;
            if (!(td->link & UHCI_LP_TERMINATE)) {
                /* Calculate next TD from link pointer */
                uint32_t next_phys = td->link & ~0xF;
                if (next_phys >= (uint32_t)(uintptr_t)uhci.td_pool &&
                    next_phys < (uint32_t)(uintptr_t)(uhci.td_pool + UHCI_MAX_TD)) {
                    next = (struct uhci_td *)(uintptr_t)next_phys;
                }
            }
            usb_free_td(td);
            td = next;
        }
    }
    usb_free_qh(qh);
    return result;

cleanup:
    /* Free any TDs we allocated on error */
    {
        struct uhci_td *td = td_setup;
        while (td) {
            struct uhci_td *next = NULL;
            if (!(td->link & UHCI_LP_TERMINATE)) {
                uint32_t next_phys = td->link & ~0xF;
                if (next_phys >= (uint32_t)(uintptr_t)uhci.td_pool &&
                    next_phys < (uint32_t)(uintptr_t)(uhci.td_pool + UHCI_MAX_TD)) {
                    next = (struct uhci_td *)(uintptr_t)next_phys;
                }
            }
            usb_free_td(td);
            td = next;
        }
    }
    usb_free_qh(qh);
    return -1;
}

/*============================================================================
 * USB Standard Requests
 *============================================================================*/

static int usb_get_descriptor(uint8_t addr, int low_speed, uint8_t mps,
                               uint8_t desc_type, uint8_t desc_index,
                               void *buf, uint16_t len)
{
    struct usb_setup_packet setup;
    setup.bmRequestType = USB_RT_DEV_TO_HOST;
    setup.bRequest = USB_REQ_GET_DESCRIPTOR;
    setup.wValue = ((uint16_t)desc_type << 8) | desc_index;
    setup.wIndex = 0;
    setup.wLength = len;

    return uhci_control_transfer(addr, low_speed, mps, &setup, buf, len, 1);
}

static int usb_set_address(int low_speed, uint8_t new_addr)
{
    struct usb_setup_packet setup;
    setup.bmRequestType = USB_RT_HOST_TO_DEV;
    setup.bRequest = USB_REQ_SET_ADDRESS;
    setup.wValue = new_addr;
    setup.wIndex = 0;
    setup.wLength = 0;

    return uhci_control_transfer(0, low_speed, 8, &setup, NULL, 0, 0);
}

static int usb_set_configuration(uint8_t addr, int low_speed, uint8_t mps,
                                  uint8_t config_value)
{
    struct usb_setup_packet setup;
    setup.bmRequestType = USB_RT_HOST_TO_DEV;
    setup.bRequest = USB_REQ_SET_CONFIGURATION;
    setup.wValue = config_value;
    setup.wIndex = 0;
    setup.wLength = 0;

    return uhci_control_transfer(addr, low_speed, mps, &setup, NULL, 0, 0);
}

static int usb_hid_set_protocol(uint8_t addr, int low_speed, uint8_t mps,
                                 uint8_t iface, uint8_t protocol)
{
    struct usb_setup_packet setup;
    setup.bmRequestType = USB_RT_HOST_TO_DEV | USB_RT_CLASS | USB_RT_INTERFACE;
    setup.bRequest = USB_REQ_HID_SET_PROTOCOL;
    setup.wValue = protocol;
    setup.wIndex = iface;
    setup.wLength = 0;

    return uhci_control_transfer(addr, low_speed, mps, &setup, NULL, 0, 0);
}

static int usb_hid_set_idle(uint8_t addr, int low_speed, uint8_t mps,
                             uint8_t iface)
{
    struct usb_setup_packet setup;
    setup.bmRequestType = USB_RT_HOST_TO_DEV | USB_RT_CLASS | USB_RT_INTERFACE;
    setup.bRequest = USB_REQ_HID_SET_IDLE;
    setup.wValue = 0; /* Indefinite idle (report only on change) */
    setup.wIndex = iface;
    setup.wLength = 0;

    return uhci_control_transfer(addr, low_speed, mps, &setup, NULL, 0, 0);
}

/*============================================================================
 * Configuration Descriptor Parsing
 *============================================================================*/

static int usb_parse_config(struct usb_device *dev)
{
    uint8_t *p = dev->config_data;
    uint8_t *end = p + dev->config_len;

    dev->iface_class = 0;
    dev->iface_subclass = 0;
    dev->iface_protocol = 0;
    dev->int_ep_addr = 0;

    while (p < end) {
        uint8_t len = p[0];
        uint8_t type = p[1];

        if (len == 0) break;
        if (p + len > end) break;

        if (type == USB_DESC_INTERFACE && len >= 9) {
            struct usb_interface_desc *iface = (struct usb_interface_desc *)p;
            dev->iface_class = iface->bInterfaceClass;
            dev->iface_subclass = iface->bInterfaceSubClass;
            dev->iface_protocol = iface->bInterfaceProtocol;
            dev->iface_number = iface->bInterfaceNumber;
        }

        if (type == USB_DESC_ENDPOINT && len >= 7) {
            struct usb_endpoint_desc *ep = (struct usb_endpoint_desc *)p;
            /* Look for Interrupt IN endpoint */
            if ((ep->bmAttributes & 0x03) == 0x03 &&  /* Interrupt */
                (ep->bEndpointAddress & 0x80)) {       /* IN direction */
                dev->int_ep_addr = ep->bEndpointAddress & 0x0F;
                dev->int_ep_mps = ep->wMaxPacketSize;
                dev->int_ep_interval = ep->bInterval;
                if (dev->int_ep_interval == 0)
                    dev->int_ep_interval = 10;
            }
        }

        p += len;
    }

    return 0;
}

/*============================================================================
 * Port Management
 *============================================================================*/

static uint16_t uhci_port_reg(int port)
{
    return (port == 0) ? UHCI_REG_PORTSC1 : UHCI_REG_PORTSC2;
}

static int uhci_port_connected(int port)
{
    return (uhci_read16(uhci_port_reg(port)) & UHCI_PORT_CCS) != 0;
}

static int uhci_port_low_speed(int port)
{
    return (uhci_read16(uhci_port_reg(port)) & UHCI_PORT_LSDA) != 0;
}

static int uhci_port_reset(int port)
{
    uint16_t reg = uhci_port_reg(port);
    uint16_t val;

    /* Set Port Reset bit */
    val = uhci_read16(reg);
    val |= UHCI_PORT_RESET;
    /* Don't accidentally clear W1C bits */
    val &= ~UHCI_PORT_WC_BITS;
    uhci_write16(reg, val);

    /* Hold reset for 50ms */
    timer_sleep_ms(50);

    /* Clear Port Reset bit */
    val = uhci_read16(reg);
    val &= ~UHCI_PORT_RESET;
    val &= ~UHCI_PORT_WC_BITS;
    uhci_write16(reg, val);

    /* Wait for port to recover */
    timer_sleep_ms(10);

    /* Enable the port */
    val = uhci_read16(reg);
    val |= UHCI_PORT_PE;
    val &= ~UHCI_PORT_WC_BITS;
    uhci_write16(reg, val);

    timer_sleep_ms(10);

    /* Clear any status change bits */
    val = uhci_read16(reg);
    if (val & UHCI_PORT_CSC) {
        uhci_write16(reg, (val & ~UHCI_PORT_WC_BITS) | UHCI_PORT_CSC);
    }
    val = uhci_read16(reg);
    if (val & UHCI_PORT_PEC) {
        uhci_write16(reg, (val & ~UHCI_PORT_WC_BITS) | UHCI_PORT_PEC);
    }

    /* Check if port is enabled */
    val = uhci_read16(reg);
    if (!(val & UHCI_PORT_PE)) {
        kprintf("[USB] Port %d: reset failed, port not enabled\n", port);
        return -1;
    }

    return 0;
}

/*============================================================================
 * Device Enumeration
 *============================================================================*/

static int usb_enumerate_device(int port)
{
    if (!uhci_port_connected(port)) {
        return -1;
    }

    int low_speed = uhci_port_low_speed(port);
    kprintf("[USB] Port %d: device detected (%s speed)\n",
            port, low_speed ? "low" : "full");

    /* Reset the port */
    if (uhci_port_reset(port) < 0) {
        return -1;
    }

    /* Find a free device slot */
    int dev_idx = -1;
    for (int i = 0; i < USB_MAX_DEVICES; i++) {
        if (uhci.devices[i].state == USB_DEV_NONE) {
            dev_idx = i;
            break;
        }
    }
    if (dev_idx < 0) {
        kprintf("[USB] No free device slots\n");
        return -1;
    }

    struct usb_device *dev = &uhci.devices[dev_idx];
    memset(dev, 0, sizeof(*dev));
    dev->state = USB_DEV_ATTACHED;
    dev->port = port;
    dev->low_speed = low_speed;
    dev->max_packet_ep0 = 8; /* Default for low-speed, safe for full-speed */

    /* GET_DESCRIPTOR (first 8 bytes) at address 0 to learn max packet size */
    uint8_t desc_buf[18];
    memset(desc_buf, 0, sizeof(desc_buf));

    if (usb_get_descriptor(0, low_speed, 8, USB_DESC_DEVICE, 0,
                            desc_buf, 8) < 0) {
        kprintf("[USB] Port %d: failed to get device descriptor (8 bytes)\n", port);
        dev->state = USB_DEV_NONE;
        return -1;
    }

    dev->max_packet_ep0 = desc_buf[7]; /* bMaxPacketSize0 */
    if (dev->max_packet_ep0 == 0) dev->max_packet_ep0 = 8;

    /* SET_ADDRESS */
    uint8_t new_addr = uhci.next_address++;
    if (new_addr > 127) {
        kprintf("[USB] No more USB addresses available\n");
        dev->state = USB_DEV_NONE;
        return -1;
    }

    if (usb_set_address(low_speed, new_addr) < 0) {
        kprintf("[USB] Port %d: SET_ADDRESS failed\n", port);
        dev->state = USB_DEV_NONE;
        return -1;
    }

    dev->address = new_addr;
    dev->state = USB_DEV_ADDRESSED;
    timer_sleep_ms(2); /* Device needs time to accept new address */

    /* GET_DESCRIPTOR (full 18-byte device descriptor at new address) */
    if (usb_get_descriptor(new_addr, low_speed, dev->max_packet_ep0,
                            USB_DESC_DEVICE, 0,
                            &dev->dev_desc, 18) < 0) {
        kprintf("[USB] Port %d: failed to get full device descriptor\n", port);
        dev->state = USB_DEV_NONE;
        return -1;
    }

    kprintf("[USB] Device %d: VID=%04x PID=%04x Class=%02x\n",
            new_addr, dev->dev_desc.idVendor, dev->dev_desc.idProduct,
            dev->dev_desc.bDeviceClass);

    /* GET_DESCRIPTOR (configuration descriptor header - 9 bytes) */
    struct usb_config_desc config_hdr;
    if (usb_get_descriptor(new_addr, low_speed, dev->max_packet_ep0,
                            USB_DESC_CONFIGURATION, 0,
                            &config_hdr, 9) < 0) {
        kprintf("[USB] Port %d: failed to get config descriptor\n", port);
        return -1; /* Device addressed but not configured */
    }

    /* Get full configuration descriptor */
    dev->config_len = config_hdr.wTotalLength;
    if (dev->config_len > sizeof(dev->config_data))
        dev->config_len = sizeof(dev->config_data);

    if (usb_get_descriptor(new_addr, low_speed, dev->max_packet_ep0,
                            USB_DESC_CONFIGURATION, 0,
                            dev->config_data, dev->config_len) < 0) {
        kprintf("[USB] Port %d: failed to get full config descriptor\n", port);
        return -1;
    }

    /* Parse configuration to find HID interfaces */
    usb_parse_config(dev);

    /* SET_CONFIGURATION */
    if (usb_set_configuration(new_addr, low_speed, dev->max_packet_ep0,
                               config_hdr.bConfigurationValue) < 0) {
        kprintf("[USB] Port %d: SET_CONFIGURATION failed\n", port);
        return -1;
    }

    dev->state = USB_DEV_CONFIGURED;
    uhci.device_count++;

    /* Check if this is a HID boot protocol device */
    if (dev->iface_class == USB_CLASS_HID &&
        dev->iface_subclass == USB_SUBCLASS_BOOT &&
        dev->int_ep_addr != 0) {

        int hid_type = 0;
        if (dev->iface_protocol == USB_PROTOCOL_KEYBOARD) {
            hid_type = USB_HID_KEYBOARD;
            kprintf("[USB] Device %d: HID Boot Keyboard\n", new_addr);
        } else if (dev->iface_protocol == USB_PROTOCOL_MOUSE) {
            hid_type = USB_HID_MOUSE;
            kprintf("[USB] Device %d: HID Boot Mouse\n", new_addr);
        }

        if (hid_type) {
            /* Set boot protocol */
            usb_hid_set_protocol(new_addr, low_speed, dev->max_packet_ep0,
                                  dev->iface_number, USB_HID_PROTOCOL_BOOT);
            /* Set idle (report only on change) */
            usb_hid_set_idle(new_addr, low_speed, dev->max_packet_ep0,
                              dev->iface_number);

            /* Register with HID layer */
            usb_hid_register(dev_idx, hid_type,
                             new_addr, low_speed,
                             dev->int_ep_addr,
                             dev->int_ep_mps,
                             dev->int_ep_interval);
        }
    } else {
        kprintf("[USB] Device %d: Class %02x/%02x (not HID boot)\n",
                new_addr, dev->iface_class, dev->iface_subclass);
    }

    return 0;
}

/*============================================================================
 * Public API
 *============================================================================*/

uint16_t usb_get_io_base(void)
{
    return uhci.io_base;
}

uint8_t *usb_get_poll_buffer(int device_index)
{
    if (device_index < 0 || device_index >= USB_MAX_DEVICES) return NULL;
    return uhci.poll_bufs + (device_index * DMA_POLL_BUF_STRIDE);
}

void usb_init(void)
{
    memset(&uhci, 0, sizeof(uhci));
    uhci.next_address = 1;

    /* Initialize HID subsystem */
    usb_hid_init();

    /* Find UHCI controller on PCI bus */
    const struct pci_device *pci_dev = pci_find_device(PCI_CLASS_SERIAL,
                                                        PCI_SUBCLASS_USB);
    if (!pci_dev) {
        kprintf("[USB] No USB controller found on PCI bus\n");
        return;
    }

    /* Check prog_if for UHCI (0x00) */
    if (pci_dev->prog_if != 0x00) {
        kprintf("[USB] USB controller found but not UHCI (prog_if=0x%02x)\n",
                pci_dev->prog_if);
        return;
    }

    /* UHCI uses BAR4 for I/O space */
    if (!pci_dev->bar_is_io[4] || pci_dev->bar_addr[4] == 0) {
        kprintf("[USB] UHCI BAR4 not valid I/O space\n");
        return;
    }

    uhci.io_base = (uint16_t)pci_dev->bar_addr[4];
    uhci.irq = pci_dev->irq_line;

    kprintf("[USB] UHCI controller: I/O base 0x%04x, IRQ %d\n",
            uhci.io_base, uhci.irq);

    /* Enable bus mastering and I/O space */
    pci_enable_bus_master(pci_dev);
    pci_enable_memory_space(pci_dev);
    /* Also explicitly enable I/O space */
    uint16_t cmd = pci_config_read16(pci_dev->bus, pci_dev->device,
                                      pci_dev->function, PCI_REG_COMMAND);
    cmd |= PCI_CMD_IO_SPACE;
    pci_config_write16(pci_dev->bus, pci_dev->device,
                        pci_dev->function, PCI_REG_COMMAND, cmd);

    /* Allocate DMA memory (4 contiguous pages = 16KB) */
    void *dma_mem = pmm_alloc_pages(DMA_PAGES);
    if (!dma_mem) {
        kprintf("[USB] Failed to allocate DMA memory\n");
        return;
    }

    uhci.dma_base = (uint64_t)(uintptr_t)dma_mem;
    memset(dma_mem, 0, DMA_PAGES * 4096);

    /* Set up pointers into DMA region */
    uhci.frame_list = (volatile uint32_t *)((uintptr_t)dma_mem + DMA_FRAME_LIST_OFF);
    uhci.td_pool    = (struct uhci_td *)((uintptr_t)dma_mem + DMA_TD_POOL_OFF);
    uhci.qh_pool    = (struct uhci_qh *)((uintptr_t)dma_mem + DMA_QH_POOL_OFF);
    uhci.setup_buf  = (uint8_t *)((uintptr_t)dma_mem + DMA_SETUP_BUF_OFF);
    uhci.ctrl_buf   = (uint8_t *)((uintptr_t)dma_mem + DMA_CTRL_BUF_OFF);
    uhci.poll_bufs  = (uint8_t *)((uintptr_t)dma_mem + DMA_POLL_BUF_OFF);

    /* Global reset */
    uhci_write16(UHCI_REG_USBCMD, UHCI_CMD_GRESET);
    timer_sleep_ms(50);
    uhci_write16(UHCI_REG_USBCMD, 0);
    timer_sleep_ms(10);

    /* Host controller reset */
    uhci_write16(UHCI_REG_USBCMD, UHCI_CMD_HCRESET);
    {
        int timeout = 100;
        while ((uhci_read16(UHCI_REG_USBCMD) & UHCI_CMD_HCRESET) && --timeout) {
            timer_sleep_ms(1);
        }
        if (!timeout) {
            kprintf("[USB] UHCI reset timeout\n");
            return;
        }
    }

    /* Clear status */
    uhci_write16(UHCI_REG_USBSTS, 0x3F);

    /* Initialize frame list: all entries terminate */
    for (int i = 0; i < UHCI_FRAME_COUNT; i++) {
        uhci.frame_list[i] = UHCI_LP_TERMINATE;
    }

    /* Set frame list base address */
    uhci_write32(UHCI_REG_FLBASEADD, (uint32_t)(uintptr_t)uhci.frame_list);

    /* Set frame number to 0 */
    uhci_write16(UHCI_REG_FRNUM, 0);

    /* Set SOF timing to default */
    outb(uhci.io_base + UHCI_REG_SOFMOD, 0x40);

    /* Disable interrupts (polling mode) */
    uhci_write16(UHCI_REG_USBINTR, 0);

    /* Start the controller */
    uhci_write16(UHCI_REG_USBCMD, UHCI_CMD_RS | UHCI_CMD_CF | UHCI_CMD_MAXP);

    /* Wait and verify controller is running */
    timer_sleep_ms(10);
    if (uhci_read16(UHCI_REG_USBSTS) & UHCI_STS_HCH) {
        kprintf("[USB] UHCI failed to start (still halted)\n");
        return;
    }

    uhci.initialized = 1;
    kprintf("[USB] UHCI host controller started\n");

    /* Scan root hub ports */
    for (int port = 0; port < UHCI_NUM_PORTS; port++) {
        if (uhci_port_connected(port)) {
            usb_enumerate_device(port);
        } else {
            kprintf("[USB] Port %d: no device\n", port);
        }
    }

    kprintf("[USB] Enumeration complete: %d device(s)\n", uhci.device_count);
}

void usb_poll(void)
{
    if (!uhci.initialized) return;

    /* Check for port status changes */
    for (int port = 0; port < UHCI_NUM_PORTS; port++) {
        uint16_t reg = uhci_port_reg(port);
        uint16_t val = uhci_read16(reg);

        if (val & UHCI_PORT_CSC) {
            /* Clear the change bit */
            uhci_write16(reg, (val & ~UHCI_PORT_WC_BITS) | UHCI_PORT_CSC);

            if (val & UHCI_PORT_CCS) {
                /* New device connected */
                kprintf("[USB] Port %d: device connected\n", port);
                timer_sleep_ms(100); /* Debounce */
                usb_enumerate_device(port);
            } else {
                /* Device disconnected */
                kprintf("[USB] Port %d: device disconnected\n", port);
                for (int i = 0; i < USB_MAX_DEVICES; i++) {
                    if (uhci.devices[i].port == port &&
                        uhci.devices[i].state != USB_DEV_NONE) {
                        usb_hid_unregister(i);
                        uhci.devices[i].state = USB_DEV_NONE;
                        uhci.device_count--;
                    }
                }
            }
        }
    }

    /* Poll HID devices */
    usb_hid_poll();
}

int usb_is_initialized(void)
{
    return uhci.initialized;
}

int usb_device_count(void)
{
    return uhci.device_count;
}

const struct usb_device *usb_get_device(int index)
{
    if (index < 0 || index >= USB_MAX_DEVICES) return NULL;
    if (uhci.devices[index].state == USB_DEV_NONE) return NULL;
    return &uhci.devices[index];
}

void usb_dump_status(void)
{
    kprintf("\nUSB UHCI Host Controller Status\n");
    kprintf("================================\n");

    if (!uhci.initialized) {
        kprintf("  Not initialized\n");
        return;
    }

    kprintf("  I/O Base:      0x%04x\n", uhci.io_base);
    kprintf("  IRQ:           %d\n", uhci.irq);
    kprintf("  USBCMD:        0x%04x\n", uhci_read16(UHCI_REG_USBCMD));
    kprintf("  USBSTS:        0x%04x\n", uhci_read16(UHCI_REG_USBSTS));
    kprintf("  Frame Number:  %d\n", uhci_read16(UHCI_REG_FRNUM));
    kprintf("  DMA Base:      0x%lx\n", (unsigned long)uhci.dma_base);

    for (int port = 0; port < UHCI_NUM_PORTS; port++) {
        uint16_t val = uhci_read16(uhci_port_reg(port));
        kprintf("  Port %d:        0x%04x", port, val);
        if (val & UHCI_PORT_CCS) kprintf(" CONNECTED");
        if (val & UHCI_PORT_PE) kprintf(" ENABLED");
        if (val & UHCI_PORT_LSDA) kprintf(" LOW-SPEED");
        if (val & UHCI_PORT_RESET) kprintf(" RESET");
        kprintf("\n");
    }

    kprintf("  Devices:       %d\n", uhci.device_count);
    for (int i = 0; i < USB_MAX_DEVICES; i++) {
        struct usb_device *dev = &uhci.devices[i];
        if (dev->state == USB_DEV_NONE) continue;
        kprintf("    [%d] Addr %d: VID=%04x PID=%04x %s",
                i, dev->address,
                dev->dev_desc.idVendor, dev->dev_desc.idProduct,
                dev->low_speed ? "LS" : "FS");
        if (dev->iface_class == USB_CLASS_HID) {
            if (dev->iface_protocol == USB_PROTOCOL_KEYBOARD)
                kprintf(" (Keyboard)");
            else if (dev->iface_protocol == USB_PROTOCOL_MOUSE)
                kprintf(" (Mouse)");
            else
                kprintf(" (HID)");
        }
        kprintf("\n");
    }
}
