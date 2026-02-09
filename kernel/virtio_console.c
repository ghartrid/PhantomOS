/*
 * PhantomOS VirtIO Console Driver
 * "To Create, Not To Destroy"
 *
 * VirtIO console (virtio-serial) for paravirtualized guest-host I/O.
 * Uses the same VirtIO PCI transport as virtio_gpu.c:
 *   1. Detect PCI device (0x1AF4/0x1003 transitional or 0x1AF4/0x1043 modern)
 *   2. Walk PCI capabilities for Common/Notify/ISR/Device config
 *   3. Set up receiveq (queue 0) and transmitq (queue 1)
 *   4. Pre-fill receive descriptors, transmit on demand
 *
 * Output is buffered per-character and flushed on newline or buffer full.
 */

#include "virtio_console.h"
#include "pci.h"
#include "vmm.h"
#include "pmm.h"
#include "io.h"
#include <stdint.h>
#include <stddef.h>

/*============================================================================
 * External Declarations
 *============================================================================*/

extern int kprintf(const char *fmt, ...);
extern void *memset(void *s, int c, size_t n);
extern void *memcpy(void *dest, const void *src, size_t n);

/*============================================================================
 * Constants
 *============================================================================*/

#define VIRTIO_CONSOLE_DEVICE_ID        0x1003  /* Transitional */
#define VIRTIO_CONSOLE_DEVICE_ID_V1     0x1043  /* Modern (0x1040+3) */
#define VIRTIO_VENDOR_ID                0x1AF4

#define VCON_QUEUE_SIZE     64      /* Virtqueue entries */
#define VCON_RX_BUF_SIZE    256     /* Per-descriptor receive buffer */
#define VCON_TX_BUF_SIZE    256     /* Transmit staging buffer */
#define VCON_WRITE_BUF_SIZE 256     /* Character write buffer */

/* VirtIO PCI capability types */
#define VIRTIO_PCI_CAP_COMMON_CFG   1
#define VIRTIO_PCI_CAP_NOTIFY_CFG   2
#define VIRTIO_PCI_CAP_ISR_CFG      3
#define VIRTIO_PCI_CAP_DEVICE_CFG   4

/* VirtIO device status bits */
#define VIRTIO_STATUS_ACKNOWLEDGE   1
#define VIRTIO_STATUS_DRIVER        2
#define VIRTIO_STATUS_FEATURES_OK   8
#define VIRTIO_STATUS_DRIVER_OK     4

/* Virtqueue descriptor flags */
#define VIRTQ_DESC_F_NEXT       1
#define VIRTQ_DESC_F_WRITE      2   /* Device writes (for receive) */

/* PCI capability list */
#define PCI_REG_CAP_PTR     0x34
#define PCI_REG_STATUS_CAP  0x10

/* VMM page flags */
#define PTE_PRESENT     (1ULL << 0)
#define PTE_WRITABLE    (1ULL << 1)
#define PTE_NOCACHE     (1ULL << 4)
#define PTE_WRITETHROUGH (1ULL << 3)

/*============================================================================
 * Virtqueue Structures (duplicated from virtio_gpu.c)
 *============================================================================*/

struct virtq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

struct virtq_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[VCON_QUEUE_SIZE];
    uint16_t used_event;
} __attribute__((packed));

struct virtq_used_elem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed));

struct virtq_used {
    uint16_t flags;
    uint16_t idx;
    struct virtq_used_elem ring[VCON_QUEUE_SIZE];
    uint16_t avail_event;
} __attribute__((packed));

/*============================================================================
 * VirtIO PCI Common Configuration (MMIO-mapped)
 *============================================================================*/

struct virtio_pci_common_cfg {
    uint32_t device_feature_select;
    uint32_t device_feature;
    uint32_t driver_feature_select;
    uint32_t driver_feature;
    uint16_t msix_config;
    uint16_t num_queues;
    uint8_t  device_status;
    uint8_t  config_generation;
    uint16_t queue_select;
    uint16_t queue_size;
    uint16_t queue_msix_vector;
    uint16_t queue_enable;
    uint16_t queue_notify_off;
    uint64_t queue_desc;
    uint64_t queue_avail;
    uint64_t queue_used;
} __attribute__((packed));

/*============================================================================
 * Driver State
 *============================================================================*/

static struct {
    int                     detected;
    int                     initialized;
    const struct pci_device *pci_dev;

    /* MMIO-mapped VirtIO config structures */
    volatile struct virtio_pci_common_cfg *common_cfg;
    volatile uint8_t       *isr_cfg;
    volatile uint8_t       *device_cfg;
    volatile uint16_t      *notify_base;
    uint32_t                notify_off_multiplier;

    /* Receiveq (virtqueue 0) */
    struct virtq_desc      *rx_desc;
    struct virtq_avail     *rx_avail;
    struct virtq_used      *rx_used;
    uint16_t                rx_last_used;
    uint16_t                rx_notify_off;

    /* Transmitq (virtqueue 1) */
    struct virtq_desc      *tx_desc;
    struct virtq_avail     *tx_avail;
    struct virtq_used      *tx_used;
    uint16_t                tx_free_head;
    uint16_t                tx_last_used;
    uint16_t                tx_notify_off;

    /* Receive buffers (pre-allocated) */
    uint8_t                *rx_bufs;    /* VCON_QUEUE_SIZE * VCON_RX_BUF_SIZE */

    /* Transmit buffer */
    uint8_t                *tx_buf;     /* Single page for transmit data */

    /* Character write buffer (for putchar batching) */
    uint8_t                 write_buf[VCON_WRITE_BUF_SIZE];
    int                     write_pos;
} vcon;

/*============================================================================
 * PCI Capability Walking (same pattern as virtio_gpu.c)
 *============================================================================*/

static int find_virtio_caps(void)
{
    uint8_t bus = vcon.pci_dev->bus;
    uint8_t dev = vcon.pci_dev->device;
    uint8_t func = vcon.pci_dev->function;

    uint16_t status = pci_config_read16(bus, dev, func, 0x06);
    if (!(status & PCI_REG_STATUS_CAP)) {
        kprintf("[VirtIO Con] No PCI capabilities\n");
        return -1;
    }

    uint8_t cap_ptr = pci_config_read8(bus, dev, func, PCI_REG_CAP_PTR);
    cap_ptr &= 0xFC;

    int found_common = 0, found_notify = 0;

    while (cap_ptr) {
        uint8_t cap_id   = pci_config_read8(bus, dev, func, cap_ptr);
        uint8_t cap_next = pci_config_read8(bus, dev, func, cap_ptr + 1);

        if (cap_id == 0x09) {  /* VirtIO vendor capability */
            uint8_t cfg_type = pci_config_read8(bus, dev, func, cap_ptr + 3);
            uint8_t bar_idx  = pci_config_read8(bus, dev, func, cap_ptr + 4);
            uint32_t offset  = pci_config_read32(bus, dev, func, cap_ptr + 8);
            uint32_t length  = pci_config_read32(bus, dev, func, cap_ptr + 12);

            uint64_t bar_base = vcon.pci_dev->bar_addr[bar_idx];
            if (bar_base == 0) {
                cap_ptr = cap_next;
                continue;
            }

            /* Map the BAR region */
            uint64_t map_addr = bar_base + offset;
            uint64_t map_pages = (length + 4095) / 4096;
            for (uint64_t p = 0; p < map_pages; p++) {
                uint64_t page = (map_addr + p * 4096) & ~0xFFFULL;
                vmm_map_page(page, page,
                             PTE_PRESENT | PTE_WRITABLE |
                             PTE_NOCACHE | PTE_WRITETHROUGH);
            }

            volatile void *mapped = (volatile void *)(uintptr_t)(bar_base + offset);

            switch (cfg_type) {
            case VIRTIO_PCI_CAP_COMMON_CFG:
                vcon.common_cfg = (volatile struct virtio_pci_common_cfg *)mapped;
                found_common = 1;
                break;
            case VIRTIO_PCI_CAP_NOTIFY_CFG:
                vcon.notify_base = (volatile uint16_t *)mapped;
                vcon.notify_off_multiplier =
                    pci_config_read32(bus, dev, func, cap_ptr + 16);
                found_notify = 1;
                break;
            case VIRTIO_PCI_CAP_ISR_CFG:
                vcon.isr_cfg = (volatile uint8_t *)mapped;
                break;
            case VIRTIO_PCI_CAP_DEVICE_CFG:
                vcon.device_cfg = (volatile uint8_t *)mapped;
                break;
            }
        }

        cap_ptr = cap_next;
    }

    if (!found_common || !found_notify) {
        kprintf("[VirtIO Con] Missing required capabilities\n");
        return -1;
    }
    return 0;
}

/*============================================================================
 * Virtqueue Setup
 *============================================================================*/

static int setup_virtqueue(int queue_idx,
                           struct virtq_desc **out_desc,
                           struct virtq_avail **out_avail,
                           struct virtq_used **out_used,
                           uint16_t *out_notify_off)
{
    volatile struct virtio_pci_common_cfg *cfg = vcon.common_cfg;

    cfg->queue_select = (uint16_t)queue_idx;
    __asm__ volatile("mfence" ::: "memory");

    uint16_t max_size = cfg->queue_size;
    if (max_size == 0) return -1;
    if (max_size > VCON_QUEUE_SIZE)
        max_size = VCON_QUEUE_SIZE;
    cfg->queue_size = max_size;

    /* Allocate virtqueue memory (2 pages for descriptors + rings) */
    void *vq_mem = pmm_alloc_pages(2);
    if (!vq_mem) return -1;
    memset(vq_mem, 0, 8192);

    uint64_t vq_phys = (uint64_t)(uintptr_t)vq_mem;

    *out_desc = (struct virtq_desc *)vq_mem;
    *out_avail = (struct virtq_avail *)((uint8_t *)vq_mem +
                  max_size * sizeof(struct virtq_desc));

    uint64_t used_offset = max_size * sizeof(struct virtq_desc) +
                           sizeof(struct virtq_avail);
    used_offset = (used_offset + 0xFFF) & ~0xFFFULL;
    *out_used = (struct virtq_used *)((uint8_t *)vq_mem + used_offset);

    /* Initialize free descriptor chain */
    for (uint16_t i = 0; i < max_size - 1; i++)
        (*out_desc)[i].next = i + 1;
    (*out_desc)[max_size - 1].next = 0xFFFF;

    /* Save notify offset */
    *out_notify_off = cfg->queue_notify_off;

    /* Tell device where the queue structures are */
    cfg->queue_desc  = vq_phys;
    cfg->queue_avail = vq_phys +
                       (uint64_t)((uint8_t *)*out_avail - (uint8_t *)vq_mem);
    cfg->queue_used  = vq_phys + used_offset;
    __asm__ volatile("mfence" ::: "memory");

    cfg->queue_enable = 1;
    __asm__ volatile("mfence" ::: "memory");

    return 0;
}

/*============================================================================
 * Transmit / Receive Helpers
 *============================================================================*/

static void kick_queue(uint16_t notify_off, uint16_t queue_idx)
{
    __asm__ volatile("mfence" ::: "memory");
    volatile uint16_t *notify_addr = (volatile uint16_t *)
        ((uint8_t *)vcon.notify_base +
         (uint32_t)notify_off * vcon.notify_off_multiplier);
    *notify_addr = queue_idx;
}

static void flush_write_buf(void)
{
    if (vcon.write_pos == 0 || !vcon.initialized) return;

    /* Copy buffered data to transmit buffer */
    memcpy(vcon.tx_buf, vcon.write_buf, (size_t)vcon.write_pos);

    /* Allocate a descriptor */
    uint16_t idx = vcon.tx_free_head;
    if (idx == 0xFFFF) {
        /* No free descriptors - drop data */
        vcon.write_pos = 0;
        return;
    }
    vcon.tx_free_head = vcon.tx_desc[idx].next;

    /* Fill descriptor */
    vcon.tx_desc[idx].addr = (uint64_t)(uintptr_t)vcon.tx_buf;
    vcon.tx_desc[idx].len = (uint32_t)vcon.write_pos;
    vcon.tx_desc[idx].flags = 0;  /* Device reads */
    vcon.tx_desc[idx].next = 0xFFFF;

    /* Add to available ring */
    uint16_t avail_idx = vcon.tx_avail->idx;
    vcon.tx_avail->ring[avail_idx % VCON_QUEUE_SIZE] = idx;
    __asm__ volatile("mfence" ::: "memory");
    vcon.tx_avail->idx = avail_idx + 1;

    /* Kick transmitq */
    kick_queue(vcon.tx_notify_off, 1);

    /* Poll for completion (simple spin with timeout) */
    for (int i = 0; i < 1000000; i++) {
        if (vcon.tx_used->idx != vcon.tx_last_used) {
            /* Reclaim descriptor */
            vcon.tx_last_used = vcon.tx_used->idx;
            vcon.tx_desc[idx].next = vcon.tx_free_head;
            vcon.tx_free_head = idx;
            break;
        }
        __asm__ volatile("pause" ::: "memory");
    }

    vcon.write_pos = 0;
}

/*============================================================================
 * Initialization
 *============================================================================*/

int virtio_console_init(void)
{
    memset(&vcon, 0, sizeof(vcon));

    /* Find VirtIO console on PCI bus */
    const struct pci_device *dev = pci_find_by_id(VIRTIO_VENDOR_ID,
                                                   VIRTIO_CONSOLE_DEVICE_ID);
    if (!dev) {
        dev = pci_find_by_id(VIRTIO_VENDOR_ID, VIRTIO_CONSOLE_DEVICE_ID_V1);
    }
    if (!dev) {
        kprintf("[VirtIO Con] No device found\n");
        return -1;
    }

    vcon.pci_dev = dev;
    vcon.detected = 1;
    kprintf("[VirtIO Con] Found: vendor 0x%x device 0x%x\n",
            dev->vendor_id, dev->device_id);

    /* Enable PCI bus mastering and memory space */
    pci_enable_bus_master(dev);
    pci_enable_memory_space(dev);

    /* Walk PCI capabilities */
    if (find_virtio_caps() != 0) return -1;

    /* Device initialization sequence */
    volatile struct virtio_pci_common_cfg *cfg = vcon.common_cfg;

    /* Reset */
    cfg->device_status = 0;
    __asm__ volatile("mfence" ::: "memory");

    /* Acknowledge + Driver */
    cfg->device_status = VIRTIO_STATUS_ACKNOWLEDGE;
    __asm__ volatile("mfence" ::: "memory");
    cfg->device_status |= VIRTIO_STATUS_DRIVER;
    __asm__ volatile("mfence" ::: "memory");

    /* Feature negotiation: accept no optional features (no multiport) */
    cfg->driver_feature_select = 0;
    cfg->driver_feature = 0;
    __asm__ volatile("mfence" ::: "memory");

    cfg->device_status |= VIRTIO_STATUS_FEATURES_OK;
    __asm__ volatile("mfence" ::: "memory");

    /* Verify features accepted */
    if (!(cfg->device_status & VIRTIO_STATUS_FEATURES_OK)) {
        kprintf("[VirtIO Con] Feature negotiation failed\n");
        cfg->device_status = 0;
        return -1;
    }

    /* Set up receiveq (queue 0) */
    if (setup_virtqueue(0, &vcon.rx_desc, &vcon.rx_avail, &vcon.rx_used,
                        &vcon.rx_notify_off) != 0) {
        kprintf("[VirtIO Con] Failed to set up receiveq\n");
        cfg->device_status = 0;
        return -1;
    }
    vcon.rx_last_used = 0;

    /* Set up transmitq (queue 1) */
    if (setup_virtqueue(1, &vcon.tx_desc, &vcon.tx_avail, &vcon.tx_used,
                        &vcon.tx_notify_off) != 0) {
        kprintf("[VirtIO Con] Failed to set up transmitq\n");
        cfg->device_status = 0;
        return -1;
    }
    vcon.tx_free_head = 0;
    vcon.tx_last_used = 0;

    /* Allocate receive buffers */
    vcon.rx_bufs = (uint8_t *)pmm_alloc_pages(
        (VCON_QUEUE_SIZE * VCON_RX_BUF_SIZE + 4095) / 4096);
    if (!vcon.rx_bufs) {
        kprintf("[VirtIO Con] Cannot allocate rx buffers\n");
        cfg->device_status = 0;
        return -1;
    }
    memset(vcon.rx_bufs, 0, VCON_QUEUE_SIZE * VCON_RX_BUF_SIZE);

    /* Allocate transmit buffer */
    vcon.tx_buf = (uint8_t *)pmm_alloc_pages(1);
    if (!vcon.tx_buf) {
        kprintf("[VirtIO Con] Cannot allocate tx buffer\n");
        cfg->device_status = 0;
        return -1;
    }

    /* Pre-fill receive descriptors */
    for (int i = 0; i < VCON_QUEUE_SIZE; i++) {
        vcon.rx_desc[i].addr = (uint64_t)(uintptr_t)(vcon.rx_bufs +
                                i * VCON_RX_BUF_SIZE);
        vcon.rx_desc[i].len = VCON_RX_BUF_SIZE;
        vcon.rx_desc[i].flags = VIRTQ_DESC_F_WRITE;  /* Device writes */
        vcon.rx_desc[i].next = 0xFFFF;

        vcon.rx_avail->ring[i] = (uint16_t)i;
    }
    vcon.rx_avail->idx = VCON_QUEUE_SIZE;

    /* Driver ready */
    cfg->device_status |= VIRTIO_STATUS_DRIVER_OK;
    __asm__ volatile("mfence" ::: "memory");

    /* Kick receiveq to signal buffers available */
    kick_queue(vcon.rx_notify_off, 0);

    vcon.initialized = 1;
    kprintf("[VirtIO Con] Initialized (rx=%d, tx=%d)\n",
            VCON_QUEUE_SIZE, VCON_QUEUE_SIZE);
    return 0;
}

/*============================================================================
 * API
 *============================================================================*/

int virtio_console_available(void)
{
    return vcon.initialized;
}

int virtio_console_write(const void *buf, size_t len)
{
    if (!vcon.initialized || len == 0) return 0;

    const uint8_t *data = (const uint8_t *)buf;
    size_t written = 0;

    while (written < len) {
        size_t chunk = len - written;
        if (chunk > VCON_WRITE_BUF_SIZE - (size_t)vcon.write_pos)
            chunk = VCON_WRITE_BUF_SIZE - (size_t)vcon.write_pos;

        memcpy(vcon.write_buf + vcon.write_pos, data + written, chunk);
        vcon.write_pos += (int)chunk;
        written += chunk;

        if (vcon.write_pos >= VCON_WRITE_BUF_SIZE)
            flush_write_buf();
    }

    /* Flush remaining */
    if (vcon.write_pos > 0)
        flush_write_buf();

    return (int)written;
}

int virtio_console_read(void *buf, size_t max)
{
    if (!vcon.initialized || max == 0) return 0;

    uint8_t *out = (uint8_t *)buf;
    int total = 0;

    while (vcon.rx_used->idx != vcon.rx_last_used && (size_t)total < max) {
        uint16_t used_idx = vcon.rx_last_used % VCON_QUEUE_SIZE;
        uint32_t desc_id = vcon.rx_used->ring[used_idx].id;
        uint32_t data_len = vcon.rx_used->ring[used_idx].len;

        uint8_t *rx_data = vcon.rx_bufs + desc_id * VCON_RX_BUF_SIZE;
        size_t copy = data_len;
        if (copy > max - (size_t)total)
            copy = max - (size_t)total;

        memcpy(out + total, rx_data, copy);
        total += (int)copy;

        /* Re-queue the descriptor */
        vcon.rx_desc[desc_id].len = VCON_RX_BUF_SIZE;
        vcon.rx_desc[desc_id].flags = VIRTQ_DESC_F_WRITE;

        uint16_t avail_idx = vcon.rx_avail->idx;
        vcon.rx_avail->ring[avail_idx % VCON_QUEUE_SIZE] = (uint16_t)desc_id;
        __asm__ volatile("mfence" ::: "memory");
        vcon.rx_avail->idx = avail_idx + 1;

        vcon.rx_last_used++;
    }

    if (total > 0)
        kick_queue(vcon.rx_notify_off, 0);

    return total;
}

int virtio_console_has_data(void)
{
    if (!vcon.initialized) return 0;
    return vcon.rx_used->idx != vcon.rx_last_used;
}

void virtio_console_putchar(char c)
{
    if (!vcon.initialized) return;

    vcon.write_buf[vcon.write_pos++] = (uint8_t)c;

    /* Flush on newline or buffer full */
    if (c == '\n' || vcon.write_pos >= VCON_WRITE_BUF_SIZE)
        flush_write_buf();
}
