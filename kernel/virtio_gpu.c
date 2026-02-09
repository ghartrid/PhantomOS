/*
 * PhantomOS VirtIO GPU Driver
 * "To Create, Not To Destroy"
 *
 * VirtIO GPU 2D driver using virtqueue command submission.
 * Provides DMA-based display update via TRANSFER_TO_HOST_2D + RESOURCE_FLUSH.
 *
 * Architecture:
 *   1. Detect VirtIO GPU on PCI bus (vendor 0x1AF4, device 0x1050)
 *   2. Walk PCI capabilities to find VirtIO config structures
 *   3. Negotiate features (2D only, no VirGL)
 *   4. Set up controlq virtqueue for command submission
 *   5. Create 2D resource, attach backbuffer backing, set scanout
 *   6. Flip = TRANSFER_TO_HOST_2D + RESOURCE_FLUSH
 */

#include "virtio_gpu.h"
#include "gpu_hal.h"
#include "pci.h"
#include "framebuffer.h"
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
 * VirtIO GPU Structures (matching VirtIO spec)
 *============================================================================*/

struct virtio_gpu_ctrl_hdr {
    uint32_t type;
    uint32_t flags;
    uint64_t fence_id;
    uint32_t ctx_id;
    uint32_t padding;
};

struct virtio_gpu_rect {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
};

struct virtio_gpu_resource_create_2d {
    struct virtio_gpu_ctrl_hdr hdr;
    uint32_t resource_id;
    uint32_t format;
    uint32_t width;
    uint32_t height;
};

struct virtio_gpu_set_scanout {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_gpu_rect r;
    uint32_t scanout_id;
    uint32_t resource_id;
};

struct virtio_gpu_resource_flush {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_gpu_rect r;
    uint32_t resource_id;
    uint32_t padding;
};

struct virtio_gpu_transfer_to_host_2d {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_gpu_rect r;
    uint64_t offset;
    uint32_t resource_id;
    uint32_t padding;
};

struct virtio_gpu_mem_entry {
    uint64_t addr;
    uint32_t length;
    uint32_t padding;
};

struct virtio_gpu_resource_attach_backing {
    struct virtio_gpu_ctrl_hdr hdr;
    uint32_t resource_id;
    uint32_t nr_entries;
    /* Followed by nr_entries virtio_gpu_mem_entry */
};

/*============================================================================
 * Virtqueue Structures
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
    uint16_t ring[VIRTQ_SIZE];
    uint16_t used_event;
} __attribute__((packed));

struct virtq_used_elem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed));

struct virtq_used {
    uint16_t flags;
    uint16_t idx;
    struct virtq_used_elem ring[VIRTQ_SIZE];
    uint16_t avail_event;
} __attribute__((packed));

/*============================================================================
 * VirtIO PCI Common Configuration (MMIO-mapped)
 *============================================================================*/

struct virtio_pci_common_cfg {
    /* About the whole device */
    uint32_t device_feature_select;
    uint32_t device_feature;
    uint32_t driver_feature_select;
    uint32_t driver_feature;
    uint16_t msix_config;
    uint16_t num_queues;
    uint8_t  device_status;
    uint8_t  config_generation;
    /* About a specific queue */
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

    /* Controlq (virtqueue 0) */
    struct virtq_desc      *desc;
    struct virtq_avail     *avail;
    struct virtq_used      *used;
    uint16_t                vq_free_head;   /* Next free descriptor */
    uint16_t                vq_last_used;   /* Last processed used idx */

    /* GPU resource */
    uint32_t                resource_id;
    uint32_t                width;
    uint32_t                height;

    /* Command buffers (pre-allocated, reused) */
    uint8_t                *cmd_buf;        /* Command buffer page */
    uint8_t                *resp_buf;       /* Response buffer page */

    /* Statistics */
    uint64_t                flip_count;
    uint64_t                cmd_count;
} vgpu;

/*============================================================================
 * PCI Capability Walking
 *============================================================================*/

#define PCI_REG_CAP_PTR     0x34
#define PCI_REG_STATUS_CAP  0x10    /* Capabilities List bit in Status */

struct virtio_pci_cap {
    uint8_t  cap_vndr;      /* 0x09 for VirtIO */
    uint8_t  cap_next;
    uint8_t  cap_len;
    uint8_t  cfg_type;      /* VIRTIO_PCI_CAP_* */
    uint8_t  bar;
    uint8_t  padding[3];
    uint32_t offset;
    uint32_t length;
};

static int find_virtio_caps(void)
{
    uint8_t bus = vgpu.pci_dev->bus;
    uint8_t dev = vgpu.pci_dev->device;
    uint8_t func = vgpu.pci_dev->function;

    /* Check if device has capabilities list */
    uint16_t status = pci_config_read16(bus, dev, func, PCI_REG_STATUS);
    if (!(status & PCI_REG_STATUS_CAP)) {
        kprintf("[VirtIO GPU] No PCI capabilities\n");
        return -1;
    }

    uint8_t cap_ptr = pci_config_read8(bus, dev, func, PCI_REG_CAP_PTR);
    cap_ptr &= 0xFC;  /* Must be DWORD-aligned */

    int found_common = 0, found_notify = 0, found_isr = 0;

    while (cap_ptr) {
        uint8_t cap_id   = pci_config_read8(bus, dev, func, cap_ptr);
        uint8_t cap_next = pci_config_read8(bus, dev, func, cap_ptr + 1);

        if (cap_id == 0x09) {  /* VirtIO vendor capability */
            uint8_t cfg_type = pci_config_read8(bus, dev, func, cap_ptr + 3);
            uint8_t bar_idx  = pci_config_read8(bus, dev, func, cap_ptr + 4);
            uint32_t offset  = pci_config_read32(bus, dev, func, cap_ptr + 8);
            uint32_t length  = pci_config_read32(bus, dev, func, cap_ptr + 12);

            uint64_t bar_base = vgpu.pci_dev->bar_addr[bar_idx];
            if (bar_base == 0) {
                cap_ptr = cap_next;
                continue;
            }

            /* Map the BAR region if needed */
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
                vgpu.common_cfg = (volatile struct virtio_pci_common_cfg *)mapped;
                found_common = 1;
                kprintf("[VirtIO GPU] Common cfg at BAR%u+0x%x\n", bar_idx, offset);
                break;
            case VIRTIO_PCI_CAP_NOTIFY_CFG:
                vgpu.notify_base = (volatile uint16_t *)mapped;
                /* Read notify_off_multiplier from cap_ptr+16 */
                vgpu.notify_off_multiplier =
                    pci_config_read32(bus, dev, func, cap_ptr + 16);
                found_notify = 1;
                kprintf("[VirtIO GPU] Notify cfg at BAR%u+0x%x (mult=%u)\n",
                        bar_idx, offset, vgpu.notify_off_multiplier);
                break;
            case VIRTIO_PCI_CAP_ISR_CFG:
                vgpu.isr_cfg = (volatile uint8_t *)mapped;
                found_isr = 1;
                break;
            case VIRTIO_PCI_CAP_DEVICE_CFG:
                vgpu.device_cfg = (volatile uint8_t *)mapped;
                break;
            }
        }

        cap_ptr = cap_next;
    }

    if (!found_common || !found_notify) {
        kprintf("[VirtIO GPU] Missing required capabilities\n");
        return -1;
    }
    return 0;
}

/*============================================================================
 * Virtqueue Setup
 *============================================================================*/

static int setup_controlq(void)
{
    volatile struct virtio_pci_common_cfg *cfg = vgpu.common_cfg;

    /* Select queue 0 (controlq) */
    cfg->queue_select = 0;
    __asm__ volatile("mfence" ::: "memory");

    uint16_t max_size = cfg->queue_size;
    if (max_size == 0) {
        kprintf("[VirtIO GPU] Queue 0 not available\n");
        return -1;
    }
    if (max_size > VIRTQ_SIZE)
        max_size = VIRTQ_SIZE;
    cfg->queue_size = max_size;

    /*
     * Allocate virtqueue memory: descriptors + available ring + used ring
     * Must be physically contiguous and aligned.
     * Descriptors: max_size * 16 bytes
     * Available: 6 + 2*max_size bytes
     * Used: 6 + 8*max_size bytes
     * Total < 8KB for 128 entries, so 2 pages is plenty.
     */
    void *vq_mem = pmm_alloc_pages(2);
    if (!vq_mem) {
        kprintf("[VirtIO GPU] Cannot allocate virtqueue\n");
        return -1;
    }
    memset(vq_mem, 0, 8192);

    uint64_t vq_phys = (uint64_t)(uintptr_t)vq_mem;

    /* Layout within the allocated pages */
    vgpu.desc = (struct virtq_desc *)vq_mem;
    vgpu.avail = (struct virtq_avail *)((uint8_t *)vq_mem +
                  max_size * sizeof(struct virtq_desc));

    /* Used ring needs to be aligned to 4 bytes */
    uint64_t used_offset = max_size * sizeof(struct virtq_desc) +
                           sizeof(struct virtq_avail);
    used_offset = (used_offset + 0xFFF) & ~0xFFFULL;  /* Page-align used ring */
    vgpu.used = (struct virtq_used *)((uint8_t *)vq_mem + used_offset);

    /* Initialize free descriptor chain */
    for (uint16_t i = 0; i < max_size - 1; i++) {
        vgpu.desc[i].next = i + 1;
    }
    vgpu.desc[max_size - 1].next = 0xFFFF;
    vgpu.vq_free_head = 0;
    vgpu.vq_last_used = 0;

    /* Tell device where the queue structures are */
    cfg->queue_desc  = vq_phys;
    cfg->queue_avail = vq_phys +
                       (uint64_t)((uint8_t *)vgpu.avail - (uint8_t *)vq_mem);
    cfg->queue_used  = vq_phys + used_offset;

    __asm__ volatile("mfence" ::: "memory");

    /* Enable the queue */
    cfg->queue_enable = 1;
    __asm__ volatile("mfence" ::: "memory");

    kprintf("[VirtIO GPU] Controlq: %u descriptors\n", max_size);
    return 0;
}

/*============================================================================
 * Command Submission
 *============================================================================*/

static uint16_t alloc_desc(void)
{
    uint16_t idx = vgpu.vq_free_head;
    if (idx == 0xFFFF) return 0xFFFF;
    vgpu.vq_free_head = vgpu.desc[idx].next;
    return idx;
}

static void free_desc(uint16_t idx)
{
    vgpu.desc[idx].next = vgpu.vq_free_head;
    vgpu.vq_free_head = idx;
}

static void virtq_kick(void)
{
    __asm__ volatile("mfence" ::: "memory");

    /* Notify via MMIO: write queue index to notify address */
    volatile struct virtio_pci_common_cfg *cfg = vgpu.common_cfg;
    cfg->queue_select = 0;
    __asm__ volatile("mfence" ::: "memory");
    uint16_t notify_off = cfg->queue_notify_off;

    volatile uint16_t *notify_addr = (volatile uint16_t *)
        ((uint8_t *)vgpu.notify_base +
         (uint32_t)notify_off * vgpu.notify_off_multiplier);
    *notify_addr = 0;  /* Queue index 0 */
}

static int send_cmd(void *cmd, uint32_t cmd_len,
                    void *resp, uint32_t resp_len)
{
    /* Allocate 2 descriptors: cmd (read) + resp (write) */
    uint16_t d0 = alloc_desc();
    uint16_t d1 = alloc_desc();
    if (d0 == 0xFFFF || d1 == 0xFFFF) {
        if (d0 != 0xFFFF) free_desc(d0);
        return -1;
    }

    /* Descriptor 0: command (device reads) */
    vgpu.desc[d0].addr  = (uint64_t)(uintptr_t)cmd;
    vgpu.desc[d0].len   = cmd_len;
    vgpu.desc[d0].flags = VIRTQ_DESC_F_NEXT;
    vgpu.desc[d0].next  = d1;

    /* Descriptor 1: response (device writes) */
    vgpu.desc[d1].addr  = (uint64_t)(uintptr_t)resp;
    vgpu.desc[d1].len   = resp_len;
    vgpu.desc[d1].flags = VIRTQ_DESC_F_WRITE;
    vgpu.desc[d1].next  = 0;

    /* Add to available ring */
    uint16_t avail_idx = vgpu.avail->idx;
    vgpu.avail->ring[avail_idx % VIRTQ_SIZE] = d0;
    __asm__ volatile("mfence" ::: "memory");
    vgpu.avail->idx = avail_idx + 1;

    /* Kick the device */
    virtq_kick();

    /* Poll for completion */
    int timeout = 5000000;
    while (timeout-- > 0) {
        __asm__ volatile("" ::: "memory");  /* Compiler barrier */
        if (vgpu.used->idx != vgpu.vq_last_used) {
            vgpu.vq_last_used = vgpu.used->idx;
            free_desc(d0);
            free_desc(d1);
            vgpu.cmd_count++;
            return 0;
        }
        __asm__ volatile("pause" ::: "memory");
    }

    kprintf("[VirtIO GPU] Command timeout\n");
    free_desc(d0);
    free_desc(d1);
    return -1;
}

/*============================================================================
 * GPU Resource Management
 *============================================================================*/

static int create_resource(uint32_t id, uint32_t width, uint32_t height)
{
    struct virtio_gpu_resource_create_2d *cmd =
        (struct virtio_gpu_resource_create_2d *)vgpu.cmd_buf;
    struct virtio_gpu_ctrl_hdr *resp =
        (struct virtio_gpu_ctrl_hdr *)vgpu.resp_buf;

    memset(cmd, 0, sizeof(*cmd));
    memset(resp, 0, sizeof(*resp));

    cmd->hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
    cmd->resource_id = id;
    cmd->format = VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM;
    cmd->width = width;
    cmd->height = height;

    if (send_cmd(cmd, sizeof(*cmd), resp, sizeof(*resp)) != 0)
        return -1;

    if (resp->type != VIRTIO_GPU_RESP_OK_NODATA) {
        kprintf("[VirtIO GPU] RESOURCE_CREATE_2D failed: 0x%x\n", resp->type);
        return -1;
    }
    return 0;
}

static int attach_backing(uint32_t id, uint64_t phys_addr, uint32_t size)
{
    /* Command = attach_backing header + 1 mem_entry (packed together) */
    struct {
        struct virtio_gpu_resource_attach_backing hdr;
        struct virtio_gpu_mem_entry entry;
    } __attribute__((packed)) *cmd =
        (void *)vgpu.cmd_buf;
    struct virtio_gpu_ctrl_hdr *resp =
        (struct virtio_gpu_ctrl_hdr *)vgpu.resp_buf;

    memset(cmd, 0, sizeof(*cmd));
    memset(resp, 0, sizeof(*resp));

    cmd->hdr.hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
    cmd->hdr.resource_id = id;
    cmd->hdr.nr_entries = 1;
    cmd->entry.addr = phys_addr;
    cmd->entry.length = size;

    if (send_cmd(cmd, sizeof(*cmd), resp, sizeof(*resp)) != 0)
        return -1;

    if (resp->type != VIRTIO_GPU_RESP_OK_NODATA) {
        kprintf("[VirtIO GPU] ATTACH_BACKING failed: 0x%x\n", resp->type);
        return -1;
    }
    return 0;
}

static int set_scanout(uint32_t scanout, uint32_t resource_id,
                       uint32_t width, uint32_t height)
{
    struct virtio_gpu_set_scanout *cmd =
        (struct virtio_gpu_set_scanout *)vgpu.cmd_buf;
    struct virtio_gpu_ctrl_hdr *resp =
        (struct virtio_gpu_ctrl_hdr *)vgpu.resp_buf;

    memset(cmd, 0, sizeof(*cmd));
    memset(resp, 0, sizeof(*resp));

    cmd->hdr.type = VIRTIO_GPU_CMD_SET_SCANOUT;
    cmd->r.x = 0;
    cmd->r.y = 0;
    cmd->r.width = width;
    cmd->r.height = height;
    cmd->scanout_id = scanout;
    cmd->resource_id = resource_id;

    if (send_cmd(cmd, sizeof(*cmd), resp, sizeof(*resp)) != 0)
        return -1;

    if (resp->type != VIRTIO_GPU_RESP_OK_NODATA) {
        kprintf("[VirtIO GPU] SET_SCANOUT failed: 0x%x\n", resp->type);
        return -1;
    }
    return 0;
}

static int transfer_to_host_2d(uint32_t resource_id,
                                uint32_t x, uint32_t y,
                                uint32_t w, uint32_t h)
{
    struct virtio_gpu_transfer_to_host_2d *cmd =
        (struct virtio_gpu_transfer_to_host_2d *)vgpu.cmd_buf;
    struct virtio_gpu_ctrl_hdr *resp =
        (struct virtio_gpu_ctrl_hdr *)vgpu.resp_buf;

    memset(cmd, 0, sizeof(*cmd));
    memset(resp, 0, sizeof(*resp));

    cmd->hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
    cmd->r.x = x;
    cmd->r.y = y;
    cmd->r.width = w;
    cmd->r.height = h;
    cmd->offset = (uint64_t)y * w * 4;
    cmd->resource_id = resource_id;

    if (send_cmd(cmd, sizeof(*cmd), resp, sizeof(*resp)) != 0)
        return -1;

    if (resp->type != VIRTIO_GPU_RESP_OK_NODATA) {
        kprintf("[VirtIO GPU] TRANSFER_TO_HOST_2D failed: 0x%x\n", resp->type);
        return -1;
    }
    return 0;
}

static int resource_flush(uint32_t resource_id,
                           uint32_t x, uint32_t y,
                           uint32_t w, uint32_t h)
{
    struct virtio_gpu_resource_flush *cmd =
        (struct virtio_gpu_resource_flush *)vgpu.cmd_buf;
    struct virtio_gpu_ctrl_hdr *resp =
        (struct virtio_gpu_ctrl_hdr *)vgpu.resp_buf;

    memset(cmd, 0, sizeof(*cmd));
    memset(resp, 0, sizeof(*resp));

    cmd->hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
    cmd->r.x = x;
    cmd->r.y = y;
    cmd->r.width = w;
    cmd->r.height = h;
    cmd->resource_id = resource_id;

    if (send_cmd(cmd, sizeof(*cmd), resp, sizeof(*resp)) != 0)
        return -1;

    if (resp->type != VIRTIO_GPU_RESP_OK_NODATA) {
        kprintf("[VirtIO GPU] RESOURCE_FLUSH failed: 0x%x\n", resp->type);
        return -1;
    }
    return 0;
}

/*============================================================================
 * Initialization
 *============================================================================*/

static int virtio_gpu_init(void)
{
    memset(&vgpu, 0, sizeof(vgpu));

    /* Find VirtIO GPU on PCI */
    const struct pci_device *dev = pci_find_by_id(VIRTIO_VENDOR_ID,
                                                    VIRTIO_GPU_DEVICE_ID);
    if (!dev) {
        /* No VirtIO GPU present */
        return -1;
    }

    vgpu.detected = 1;
    vgpu.pci_dev = dev;

    kprintf("[VirtIO GPU] Found: vendor 0x%04x device 0x%04x\n",
            dev->vendor_id, dev->device_id);

    /* Enable memory space and bus mastering */
    pci_enable_memory_space(dev);
    pci_enable_bus_master(dev);

    /* Walk PCI capabilities to find VirtIO structures */
    if (find_virtio_caps() != 0)
        return -1;

    /* === VirtIO device initialization sequence === */

    volatile struct virtio_pci_common_cfg *cfg = vgpu.common_cfg;

    /* 1. Reset device */
    cfg->device_status = 0;
    __asm__ volatile("mfence" ::: "memory");

    /* 2. Acknowledge + Driver */
    cfg->device_status = VIRTIO_STATUS_ACKNOWLEDGE;
    __asm__ volatile("mfence" ::: "memory");
    cfg->device_status |= VIRTIO_STATUS_DRIVER;
    __asm__ volatile("mfence" ::: "memory");

    /* 3. Negotiate features (we want basic 2D, no VirGL) */
    cfg->driver_feature_select = 0;
    cfg->driver_feature = 0;  /* No special features needed for 2D */
    __asm__ volatile("mfence" ::: "memory");

    /* 4. Set FEATURES_OK */
    cfg->device_status |= VIRTIO_STATUS_FEATURES_OK;
    __asm__ volatile("mfence" ::: "memory");

    /* Verify features were accepted */
    if (!(cfg->device_status & VIRTIO_STATUS_FEATURES_OK)) {
        kprintf("[VirtIO GPU] Feature negotiation failed\n");
        cfg->device_status |= VIRTIO_STATUS_FAILED;
        return -1;
    }

    /* 5. Allocate command/response buffers */
    vgpu.cmd_buf = (uint8_t *)pmm_alloc_page();
    vgpu.resp_buf = (uint8_t *)pmm_alloc_page();
    if (!vgpu.cmd_buf || !vgpu.resp_buf) {
        kprintf("[VirtIO GPU] Cannot allocate command buffers\n");
        return -1;
    }
    memset(vgpu.cmd_buf, 0, 4096);
    memset(vgpu.resp_buf, 0, 4096);

    /* 6. Set up controlq (virtqueue 0) */
    if (setup_controlq() != 0) {
        cfg->device_status |= VIRTIO_STATUS_FAILED;
        return -1;
    }

    /* 7. Driver OK */
    cfg->device_status |= VIRTIO_STATUS_DRIVER_OK;
    __asm__ volatile("mfence" ::: "memory");

    kprintf("[VirtIO GPU] Device initialized (status=0x%02x)\n",
            cfg->device_status);

    /* === Set up 2D display === */

    const struct framebuffer_info *fb = fb_get_info();
    if (!fb || !fb->initialized) {
        kprintf("[VirtIO GPU] Framebuffer not ready\n");
        return -1;
    }

    vgpu.width = fb->width;
    vgpu.height = fb->height;
    vgpu.resource_id = 1;

    /* Create 2D resource */
    if (create_resource(vgpu.resource_id, vgpu.width, vgpu.height) != 0) {
        kprintf("[VirtIO GPU] Failed to create 2D resource\n");
        return -1;
    }
    kprintf("[VirtIO GPU] Resource %u: %ux%u\n",
            vgpu.resource_id, vgpu.width, vgpu.height);

    /* Attach backbuffer as backing storage */
    uint64_t bb_phys = (uint64_t)(uintptr_t)fb->backbuffer;
    uint32_t bb_size = vgpu.width * vgpu.height * 4;
    if (attach_backing(vgpu.resource_id, bb_phys, bb_size) != 0) {
        kprintf("[VirtIO GPU] Failed to attach backing\n");
        return -1;
    }

    /* Set scanout (bind resource to display) */
    if (set_scanout(0, vgpu.resource_id, vgpu.width, vgpu.height) != 0) {
        kprintf("[VirtIO GPU] Failed to set scanout\n");
        return -1;
    }

    vgpu.initialized = 1;
    kprintf("[VirtIO GPU] 2D display ready (%ux%u)\n",
            vgpu.width, vgpu.height);
    return 0;
}

/*============================================================================
 * HAL Operations
 *============================================================================*/

static int vgpu_available(void)
{
    return vgpu.initialized;
}

/* No 2D acceleration for drawing ops */
static int vgpu_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                            uint32_t color)
{
    (void)x; (void)y; (void)w; (void)h; (void)color;
    return -1;
}

static int vgpu_clear(uint32_t color)
{
    (void)color;
    return -1;
}

static int vgpu_copy_region(uint32_t dx, uint32_t dy, uint32_t sx,
                             uint32_t sy, uint32_t w, uint32_t h)
{
    (void)dx; (void)dy; (void)sx; (void)sy; (void)w; (void)h;
    return -1;
}

static int vgpu_flip(void)
{
    if (!vgpu.initialized) return -1;

    /* Transfer backbuffer contents to host resource */
    if (transfer_to_host_2d(vgpu.resource_id,
                             0, 0, vgpu.width, vgpu.height) != 0)
        return -1;

    /* Flush to display */
    if (resource_flush(vgpu.resource_id,
                        0, 0, vgpu.width, vgpu.height) != 0)
        return -1;

    vgpu.flip_count++;
    return 0;
}

static void vgpu_sync(void)  { }
static void vgpu_wait(void)  { }
static int  vgpu_pending(void) { return 0; }

static void vgpu_get_stats(struct gpu_stats *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->flips = vgpu.flip_count;
}

static void vgpu_dump_info(void)
{
    kprintf("\nGPU Backend: VirtIO GPU (2D)\n");
    if (!vgpu.detected) {
        kprintf("  Not detected\n");
        return;
    }
    kprintf("  PCI:          %u:%u.%u\n",
            vgpu.pci_dev->bus, vgpu.pci_dev->device, vgpu.pci_dev->function);
    kprintf("  Resolution:   %ux%u\n", vgpu.width, vgpu.height);
    kprintf("  Resource ID:  %u\n", vgpu.resource_id);
    kprintf("  Flip count:   %lu\n", (unsigned long)vgpu.flip_count);
    kprintf("  Commands:     %lu\n", (unsigned long)vgpu.cmd_count);
    kprintf("  2D Accel:     Flip only (TRANSFER + FLUSH)\n");
}

/*============================================================================
 * Resolution Change
 *============================================================================*/

static int vgpu_set_resolution(uint32_t width, uint32_t height)
{
    if (!vgpu.initialized) return -1;

    /* Create a new resource with the new dimensions */
    uint32_t new_id = vgpu.resource_id + 1;
    if (create_resource(new_id, width, height) != 0) return -1;

    /* Get backbuffer physical address for new backing */
    const struct framebuffer_info *fbi = fb_get_info();
    if (!fbi || !fbi->backbuffer) return -1;
    uint64_t bb_phys = (uint64_t)(uintptr_t)fbi->backbuffer;

    if (attach_backing(new_id, bb_phys, width * height * 4) != 0) return -1;
    if (set_scanout(0, new_id, width, height) != 0) return -1;

    vgpu.resource_id = new_id;
    vgpu.width = width;
    vgpu.height = height;

    kprintf("[VirtIO GPU] Resolution changed to %ux%u\n", width, height);
    return 0;
}

/*============================================================================
 * HAL Registration
 *============================================================================*/

static struct gpu_ops virtio_gpu_ops = {
    .name        = "VirtIO GPU",
    .type        = GPU_BACKEND_VIRTIO,
    .priority    = 80,
    .init        = virtio_gpu_init,
    .available   = vgpu_available,
    .fill_rect   = vgpu_fill_rect,
    .clear       = vgpu_clear,
    .copy_region = vgpu_copy_region,
    .flip        = vgpu_flip,
    .set_resolution = vgpu_set_resolution,
    .sync        = vgpu_sync,
    .wait        = vgpu_wait,
    .pending_ops = vgpu_pending,
    .get_stats   = vgpu_get_stats,
    .dump_info   = vgpu_dump_info,
};

void virtio_gpu_register_hal(void)
{
    gpu_hal_register(&virtio_gpu_ops);
}
