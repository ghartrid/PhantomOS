/*
 * PhantomOS Intel Integrated GPU Driver
 * "To Create, Not To Destroy"
 *
 * Hardware-accelerated 2D graphics via the Intel BLT (Block Transfer) engine.
 * Supports Intel integrated GPUs from Gen3 (i915) through Gen9 (Coffee Lake).
 *
 * Architecture:
 *   1. Detect Intel GPU on PCI bus 0
 *   2. Map BAR0 MMIO registers
 *   3. Program GTT to make backbuffer and framebuffer GPU-accessible
 *   4. Initialize BLT ring buffer for command submission
 *   5. Provide hardware-accelerated fill and copy operations
 */

#include "intel_gpu.h"
#include "gpu_hal.h"
#include "pci.h"
#include "framebuffer.h"
#include "vmm.h"
#include "pmm.h"
#include <stdint.h>
#include <stddef.h>

/*============================================================================
 * External Declarations
 *============================================================================*/

extern int kprintf(const char *fmt, ...);
extern void *memset(void *s, int c, size_t n);

/*============================================================================
 * Driver State
 *============================================================================*/

static struct intel_gpu gpu;

/*============================================================================
 * MMIO Register Access
 *============================================================================*/

static inline uint32_t gpu_read(uint32_t offset)
{
    return gpu.mmio_base[offset / 4];
}

static inline void gpu_write(uint32_t offset, uint32_t value)
{
    gpu.mmio_base[offset / 4] = value;
}

/*============================================================================
 * GPU Generation Detection
 *============================================================================*/

static intel_gpu_gen_t detect_gpu_gen(uint16_t dev_id)
{
    /* Gen 3: i915, i945 */
    if (dev_id == 0x2582 || dev_id == 0x2592 ||    /* i915G/GM */
        dev_id == 0x2772 || dev_id == 0x27A2 ||    /* i945G/GM */
        dev_id == 0x27AE ||                         /* i945GME */
        dev_id == 0x2972 || dev_id == 0x2982 ||    /* i946GZ/G35 */
        dev_id == 0x2992 || dev_id == 0x29A2 ||    /* i965Q/G */
        dev_id == 0x29B2 || dev_id == 0x29C2)      /* i965 variants */
        return INTEL_GPU_GEN3;

    /* Gen 4: i965, G45 */
    if ((dev_id >= 0x2A00 && dev_id <= 0x2A4F) ||  /* GM965/GL960 */
        (dev_id >= 0x2E00 && dev_id <= 0x2E4F) ||  /* G45/G43 */
        dev_id == 0x2E12 || dev_id == 0x2E22 ||
        dev_id == 0x2E32 || dev_id == 0x2E42)
        return INTEL_GPU_GEN4;

    /* Gen 5: Ironlake */
    if ((dev_id >= 0x0040 && dev_id <= 0x006F))     /* Ironlake */
        return INTEL_GPU_GEN5;

    /* Gen 6: Sandy Bridge */
    if ((dev_id >= 0x0100 && dev_id <= 0x013F))     /* SNB */
        return INTEL_GPU_GEN6;

    /* Gen 7: Ivy Bridge + Haswell */
    if ((dev_id >= 0x0150 && dev_id <= 0x017F) ||   /* IVB */
        (dev_id >= 0x0400 && dev_id <= 0x04FF) ||   /* HSW */
        (dev_id >= 0x0A00 && dev_id <= 0x0AFF) ||   /* HSW ULT */
        (dev_id >= 0x0C00 && dev_id <= 0x0CFF) ||   /* HSW */
        (dev_id >= 0x0D00 && dev_id <= 0x0DFF))     /* HSW CRW */
        return INTEL_GPU_GEN7;

    /* Gen 8: Broadwell */
    if ((dev_id >= 0x1600 && dev_id <= 0x16FF) ||   /* BDW */
        (dev_id >= 0x2200 && dev_id <= 0x22FF))     /* CHV */
        return INTEL_GPU_GEN8;

    /* Gen 9: Skylake, Kaby Lake, Coffee Lake, Comet Lake */
    if ((dev_id >= 0x1900 && dev_id <= 0x19FF) ||   /* SKL */
        (dev_id >= 0x5900 && dev_id <= 0x59FF) ||   /* KBL */
        (dev_id >= 0x3E00 && dev_id <= 0x3EFF) ||   /* CFL */
        (dev_id >= 0x9B00 && dev_id <= 0x9BFF) ||   /* CML */
        (dev_id >= 0x8A00 && dev_id <= 0x8AFF))     /* ICL (Gen11 but BLT compat) */
        return INTEL_GPU_GEN9;

    return INTEL_GPU_GEN_UNSUPPORTED;
}

static const char *gen_name(intel_gpu_gen_t gen)
{
    switch (gen) {
    case INTEL_GPU_GEN3: return "Gen3 (i915/945)";
    case INTEL_GPU_GEN4: return "Gen4 (i965/G45)";
    case INTEL_GPU_GEN5: return "Gen5 (Ironlake)";
    case INTEL_GPU_GEN6: return "Gen6 (Sandy Bridge)";
    case INTEL_GPU_GEN7: return "Gen7 (Ivy/Haswell)";
    case INTEL_GPU_GEN8: return "Gen8 (Broadwell)";
    case INTEL_GPU_GEN9: return "Gen9 (Skylake+)";
    default: return "Unknown/Unsupported";
    }
}

/*============================================================================
 * MMIO Mapping
 *============================================================================*/

static int map_mmio(void)
{
    gpu.mmio_phys = gpu.pci_dev->bar_addr[0];
    gpu.mmio_size = gpu.pci_dev->bar_size[0];

    if (gpu.mmio_phys == 0 || gpu.mmio_size == 0) {
        kprintf("[GPU] BAR0 not valid\n");
        return -1;
    }

    /* Map MMIO pages with uncacheable attributes (same as framebuffer.c) */
    uint64_t num_pages = (gpu.mmio_size + 4095) / 4096;
    for (uint64_t i = 0; i < num_pages; i++) {
        uint64_t addr = gpu.mmio_phys + i * 4096;
        int ret = vmm_map_page(addr, addr,
                               PTE_PRESENT | PTE_WRITABLE |
                               PTE_NOCACHE | PTE_WRITETHROUGH);
        if (ret != 0) {
            kprintf("[GPU] Failed to map MMIO page 0x%lx\n",
                    (unsigned long)addr);
            return -1;
        }
    }

    gpu.mmio_base = (volatile uint32_t *)(uintptr_t)gpu.mmio_phys;

    kprintf("[GPU] MMIO: 0x%lx (%u KB)\n",
            (unsigned long)gpu.mmio_phys, gpu.mmio_size / 1024);
    return 0;
}

/*============================================================================
 * Stolen Memory Detection
 *============================================================================*/

static void detect_stolen_memory(void)
{
    if (gpu.gen >= INTEL_GPU_GEN6) {
        /* Gen6+: BDSM register in PCI config space */
        uint32_t bdsm = pci_config_read32(gpu.pci_dev->bus,
                                           gpu.pci_dev->device,
                                           gpu.pci_dev->function,
                                           INTEL_BSM);
        gpu.stolen_base = bdsm & 0xFFF00000ULL;
    }

    /* Read GMCH_CTL for stolen memory size */
    uint16_t gmch = pci_config_read16(gpu.pci_dev->bus,
                                       gpu.pci_dev->device,
                                       gpu.pci_dev->function,
                                       INTEL_GMCH_CTL);
    uint8_t gms = (gmch >> 8) & 0xFF;

    /* GMS encoding (Gen6+): value * 32MB */
    if (gpu.gen >= INTEL_GPU_GEN6) {
        if (gms <= 0x10)
            gpu.stolen_size = (uint32_t)gms * 32 * 1024 * 1024;
    } else {
        /* Gen3-5: smaller increments */
        static const uint32_t gen3_stolen[] = {
            0, 1*1024*1024, 4*1024*1024, 8*1024*1024,
            16*1024*1024, 32*1024*1024, 48*1024*1024, 64*1024*1024,
        };
        if (gms < 8)
            gpu.stolen_size = gen3_stolen[gms];
    }

    if (gpu.stolen_size > 0) {
        kprintf("[GPU] Stolen memory: %u MB at 0x%lx\n",
                gpu.stolen_size / 1024 / 1024,
                (unsigned long)gpu.stolen_base);
    }
}

/*============================================================================
 * GTT Programming
 *============================================================================*/

static void gtt_write_entry(uint32_t index, uint64_t phys_addr)
{
    if (gpu.gen >= INTEL_GPU_GEN8) {
        /* Gen8+: 64-bit GTT entries */
        uint64_t entry = (phys_addr & 0x7FFFFFF000ULL) | GTT_ENTRY_VALID;
        gpu.gtt_base[index * 2] = (uint32_t)entry;
        gpu.gtt_base[index * 2 + 1] = (uint32_t)(entry >> 32);
    } else {
        /* Gen3-7: 32-bit GTT entries */
        uint32_t entry = (uint32_t)(phys_addr & 0xFFFFF000UL) |
                         GTT_ENTRY_VALID;
        gpu.gtt_base[index] = entry;
    }
}

static int init_gtt(void)
{
    /* Locate GTT entries */
    if (gpu.gen >= INTEL_GPU_GEN6) {
        /* Gen6+: GTT is in the upper half of BAR0 MMIO */
        gpu.gtt_base = (volatile uint32_t *)
            ((uint8_t *)gpu.mmio_base + (gpu.mmio_size / 2));
    } else {
        /* Gen3-5: GTT at fixed offset in MMIO */
        gpu.gtt_base = (volatile uint32_t *)
            ((uint8_t *)gpu.mmio_base + INTEL_GTT_OFFSET_GEN3);
    }

    /* Get framebuffer info */
    const struct framebuffer_info *fb = fb_get_info();
    if (!fb || !fb->initialized || !fb->backbuffer) {
        kprintf("[GPU] Framebuffer not available for GTT mapping\n");
        return -1;
    }

    uint32_t fb_size = fb->width * fb->height * 4;
    uint32_t num_pages = (fb_size + 4095) / 4096;

    /*
     * Map backbuffer pages into GTT starting at entry 0.
     * The backbuffer is allocated via kmalloc() in the heap, which lives
     * in the first 1GB identity-mapped region (phys == virt).
     */
    uint64_t bb_virt = (uint64_t)(uintptr_t)fb->backbuffer;

    /* Verify identity mapping holds */
    uint64_t bb_phys = vmm_get_physical(bb_virt);
    if (bb_phys != bb_virt && bb_phys != 0) {
        kprintf("[GPU] Backbuffer not identity-mapped (virt=0x%lx phys=0x%lx)\n",
                (unsigned long)bb_virt, (unsigned long)bb_phys);
        return -1;
    }

    for (uint32_t i = 0; i < num_pages; i++) {
        gtt_write_entry(i, bb_virt + i * 4096);
    }
    gpu.gpu_bb_offset = 0;

    /* Map MMIO framebuffer pages after backbuffer in GTT */
    uint32_t fb_gtt_start = num_pages;
    for (uint32_t i = 0; i < num_pages; i++) {
        gtt_write_entry(fb_gtt_start + i, fb->phys_addr + i * 4096);
    }
    gpu.gpu_fb_offset = fb_gtt_start * 4096;

    kprintf("[GPU] GTT: backbuffer at 0x0, framebuffer at 0x%x (%u pages each)\n",
            gpu.gpu_fb_offset, num_pages);
    return 0;
}

/*============================================================================
 * Ring Buffer Management
 *============================================================================*/

static void ring_emit(uint32_t dword)
{
    gpu.ring_buffer[gpu.ring_tail / 4] = dword;
    gpu.ring_tail = (gpu.ring_tail + 4) & INTEL_RING_MASK;
}

static void ring_advance(void)
{
    /* Memory barrier to ensure all ring writes are visible before tail update */
    __asm__ volatile("mfence" ::: "memory");
    gpu_write(gpu.reg_ring_tail, gpu.ring_tail);
}

static void ring_wait_idle(void)
{
    int timeout = 2000000;
    while (timeout-- > 0) {
        uint32_t head = gpu_read(gpu.reg_ring_head) & RING_HEAD_ADDR_MASK;
        if (head == gpu.ring_tail)
            return;
        /* Small delay */
        __asm__ volatile("pause" ::: "memory");
    }
    kprintf("[GPU] BLT ring timeout (head=0x%x tail=0x%x)\n",
            gpu_read(gpu.reg_ring_head) & RING_HEAD_ADDR_MASK,
            gpu.ring_tail);
    gpu.sw_fallbacks++;
}

static int ring_has_space(uint32_t dwords)
{
    uint32_t head = gpu_read(gpu.reg_ring_head) & RING_HEAD_ADDR_MASK;
    uint32_t bytes_needed = dwords * 4;
    uint32_t space;

    if (gpu.ring_tail >= head)
        space = gpu.ring_size - gpu.ring_tail + head;
    else
        space = head - gpu.ring_tail;

    /* Keep 16 bytes of slack to avoid head==tail ambiguity */
    return space > (bytes_needed + 16);
}

static int init_ring_buffer(void)
{
    /* Select ring registers based on generation */
    if (gpu.gen >= INTEL_GPU_GEN6) {
        /* Gen6+: dedicated BLT ring */
        gpu.reg_ring_tail  = INTEL_BCS_RING_TAIL;
        gpu.reg_ring_head  = INTEL_BCS_RING_HEAD;
        gpu.reg_ring_start = INTEL_BCS_RING_START;
        gpu.reg_ring_ctl   = INTEL_BCS_RING_CTL;
        gpu.reg_hws        = INTEL_BCS_HWS_PGA;
    } else {
        /* Gen3-5: BLT commands in main render ring */
        gpu.reg_ring_tail  = INTEL_RCS_RING_TAIL;
        gpu.reg_ring_head  = INTEL_RCS_RING_HEAD;
        gpu.reg_ring_start = INTEL_RCS_RING_START;
        gpu.reg_ring_ctl   = INTEL_RCS_RING_CTL;
        gpu.reg_hws        = INTEL_RCS_HWS_PGA;
    }

    /* Allocate ring buffer: 16KB physically contiguous */
    void *ring_mem = pmm_alloc_pages(INTEL_RING_SIZE_PAGES);
    if (!ring_mem) {
        kprintf("[GPU] Cannot allocate ring buffer\n");
        return -1;
    }
    gpu.ring_buffer = (uint32_t *)ring_mem;
    gpu.ring_phys = (uint64_t)(uintptr_t)ring_mem;
    gpu.ring_size = INTEL_RING_SIZE;
    gpu.ring_tail = 0;
    memset(gpu.ring_buffer, 0, gpu.ring_size);

    /* Allocate Hardware Status Page (4KB) */
    void *hws_mem = pmm_alloc_page();
    if (!hws_mem) {
        kprintf("[GPU] Cannot allocate HWS page\n");
        return -1;
    }
    gpu.hws_page = (uint32_t *)hws_mem;
    gpu.hws_phys = (uint64_t)(uintptr_t)hws_mem;
    memset(gpu.hws_page, 0, 4096);

    /*
     * Map ring buffer and HWS into GTT.
     * Place them after the framebuffer entries.
     */
    const struct framebuffer_info *fb = fb_get_info();
    uint32_t fb_pages = (fb->width * fb->height * 4 + 4095) / 4096;
    uint32_t ring_gtt_start = fb_pages * 2; /* After backbuffer + framebuffer */

    for (uint32_t i = 0; i < INTEL_RING_SIZE_PAGES; i++) {
        gtt_write_entry(ring_gtt_start + i, gpu.ring_phys + i * 4096);
    }
    gtt_write_entry(ring_gtt_start + INTEL_RING_SIZE_PAGES, gpu.hws_phys);

    /* Stop the ring first */
    gpu_write(gpu.reg_ring_ctl, 0);
    /* Wait for ring to stop */
    for (int i = 0; i < 10000; i++) {
        if (!(gpu_read(gpu.reg_ring_ctl) & RING_CTL_ENABLE))
            break;
        __asm__ volatile("pause" ::: "memory");
    }

    /* Set Hardware Status Page address */
    gpu_write(gpu.reg_hws, (uint32_t)gpu.hws_phys);

    /* Set ring buffer start address (physical, page-aligned) */
    gpu_write(gpu.reg_ring_start, (uint32_t)gpu.ring_phys);

    /* Reset head and tail */
    gpu_write(gpu.reg_ring_head, 0);
    gpu_write(gpu.reg_ring_tail, 0);

    /* Enable ring: set size (in pages - 1) and enable bit */
    uint32_t ctl = ((INTEL_RING_SIZE_PAGES - 1) << RING_CTL_SIZE_SHIFT) |
                   RING_CTL_ENABLE;
    gpu_write(gpu.reg_ring_ctl, ctl);

    /* Verify ring is running */
    uint32_t ring_ctl = gpu_read(gpu.reg_ring_ctl);
    if (!(ring_ctl & RING_CTL_ENABLE)) {
        kprintf("[GPU] Ring buffer failed to start (ctl=0x%x)\n", ring_ctl);
        return -1;
    }

    kprintf("[GPU] BLT ring: %u KB at 0x%lx\n",
            gpu.ring_size / 1024, (unsigned long)gpu.ring_phys);
    return 0;
}

/*============================================================================
 * Initialization
 *============================================================================*/

void intel_gpu_init(void)
{
    memset(&gpu, 0, sizeof(gpu));

    /* Find VGA display controller */
    const struct pci_device *dev = pci_find_device(PCI_CLASS_DISPLAY,
                                                    PCI_SUBCLASS_VGA);
    if (!dev) {
        kprintf("  [--] No VGA display controller on PCI\n");
        return;
    }

    /* Check if it's Intel */
    if (dev->vendor_id != INTEL_VENDOR_ID) {
        kprintf("  [--] VGA is not Intel (vendor 0x%04x)\n", dev->vendor_id);
        return;
    }

    gpu.detected = 1;
    gpu.pci_dev = dev;
    gpu.device_id = dev->device_id;

    /* Detect generation */
    gpu.gen = detect_gpu_gen(dev->device_id);
    if (gpu.gen == INTEL_GPU_GEN_UNKNOWN ||
        gpu.gen == INTEL_GPU_GEN_UNSUPPORTED) {
        kprintf("  [--] Intel GPU 0x%04x: unsupported generation\n",
                dev->device_id);
        return;
    }

    kprintf("[GPU] Intel %s (device 0x%04x)\n",
            gen_name(gpu.gen), gpu.device_id);

    /* Enable memory space and bus mastering */
    pci_enable_memory_space(dev);
    pci_enable_bus_master(dev);

    /* Map BAR0 MMIO registers */
    if (map_mmio() != 0) {
        kprintf("[GPU] MMIO mapping failed\n");
        return;
    }

    /* Detect stolen memory */
    detect_stolen_memory();

    /* Get aperture info from BAR2 */
    if (dev->bar_addr[2]) {
        gpu.aperture_phys = dev->bar_addr[2];
        gpu.aperture_size = dev->bar_size[2];
        kprintf("[GPU] Aperture: 0x%lx (%u MB)\n",
                (unsigned long)gpu.aperture_phys,
                gpu.aperture_size / 1024 / 1024);
    }

    /* Initialize GTT mappings */
    if (init_gtt() != 0) {
        kprintf("[GPU] GTT initialization failed\n");
        return;
    }

    /* Initialize BLT ring buffer */
    if (init_ring_buffer() != 0) {
        kprintf("[GPU] Ring buffer initialization failed\n");
        return;
    }

    gpu.initialized = 1;
    kprintf("[GPU] BLT acceleration ready\n");
}

int intel_gpu_available(void)
{
    return gpu.initialized;
}

const struct intel_gpu *intel_gpu_get_info(void)
{
    return &gpu;
}

/*============================================================================
 * BLT Operations
 *============================================================================*/

int intel_gpu_blt_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                       uint32_t color)
{
    if (!gpu.initialized || w == 0 || h == 0)
        return -1;

    const struct framebuffer_info *fb = fb_get_info();
    uint32_t pitch = fb->width * 4;

    /* Clip to screen bounds */
    if (x >= fb->width || y >= fb->height)
        return 0;
    if (x + w > fb->width)  w = fb->width - x;
    if (y + h > fb->height) h = fb->height - y;

    /* Ensure ring space: XY_COLOR_BLT = 6 DWORDs */
    if (!ring_has_space(6)) {
        ring_wait_idle();
        if (!ring_has_space(6)) {
            gpu.sw_fallbacks++;
            return -1;
        }
    }

    /*
     * XY_COLOR_BLT command:
     *   DW0: opcode | write_alpha | write_rgb | (dw_length - 2)
     *   DW1: (rop << 16) | color_depth | dst_pitch
     *   DW2: (y1 << 16) | x1
     *   DW3: (y2 << 16) | x2
     *   DW4: destination offset in GTT
     *   DW5: fill color
     */
    ring_emit(XY_COLOR_BLT_CMD | BLT_WRITE_ALPHA | BLT_WRITE_RGB | (6 - 2));
    ring_emit((BLT_ROP_PAT_COPY << 16) | BLT_COLOR_DEPTH_32 | pitch);
    ring_emit((y << 16) | x);
    ring_emit(((y + h) << 16) | (x + w));
    ring_emit(gpu.gpu_bb_offset);
    ring_emit(color);

    ring_advance();

    gpu.blt_fills++;
    gpu.blt_bytes += (uint64_t)w * h * 4;
    gpu.pending_ops++;
    gpu.batched_ops++;

    return 0;
}

int intel_gpu_blt_clear(uint32_t color)
{
    if (!gpu.initialized)
        return -1;

    const struct framebuffer_info *fb = fb_get_info();
    return intel_gpu_blt_fill(0, 0, fb->width, fb->height, color);
}

int intel_gpu_blt_screen_copy(uint32_t dst_x, uint32_t dst_y,
                               uint32_t src_x, uint32_t src_y,
                               uint32_t w, uint32_t h)
{
    if (!gpu.initialized || w == 0 || h == 0)
        return -1;

    const struct framebuffer_info *fb = fb_get_info();
    uint32_t pitch = fb->width * 4;

    /* Clip to screen bounds */
    if (dst_x >= fb->width || dst_y >= fb->height) return 0;
    if (src_x >= fb->width || src_y >= fb->height) return 0;
    if (dst_x + w > fb->width)  w = fb->width - dst_x;
    if (dst_y + h > fb->height) h = fb->height - dst_y;
    if (src_x + w > fb->width)  w = fb->width - src_x;
    if (src_y + h > fb->height) h = fb->height - src_y;

    /* Must sync pending ops before screen copy to avoid read-before-write */
    if (gpu.pending_ops > 0) {
        ring_wait_idle();
        gpu.pending_ops = 0;
    }

    /* Ensure ring space: XY_SRC_COPY_BLT = 8 DWORDs */
    if (!ring_has_space(8)) {
        ring_wait_idle();
        if (!ring_has_space(8)) {
            gpu.sw_fallbacks++;
            return -1;
        }
    }

    /* XY_SRC_COPY_BLT: backbuffer -> backbuffer */
    ring_emit(XY_SRC_COPY_BLT_CMD | BLT_WRITE_ALPHA | BLT_WRITE_RGB |
              (8 - 2));
    ring_emit((BLT_ROP_SRC_COPY << 16) | BLT_COLOR_DEPTH_32 | pitch);
    ring_emit((dst_y << 16) | dst_x);
    ring_emit(((dst_y + h) << 16) | (dst_x + w));
    ring_emit(gpu.gpu_bb_offset);           /* dst: backbuffer */
    ring_emit((src_y << 16) | src_x);
    ring_emit(pitch);
    ring_emit(gpu.gpu_bb_offset);           /* src: backbuffer */

    ring_advance();

    gpu.blt_screen_copies++;
    gpu.blt_bytes += (uint64_t)w * h * 4;
    gpu.pending_ops++;
    gpu.batched_ops++;

    return 0;
}

int intel_gpu_blt_flip(void)
{
    if (!gpu.initialized)
        return -1;

    /* Drain all pending batched fills before copying to framebuffer */
    if (gpu.pending_ops > 0) {
        ring_wait_idle();
        gpu.pending_ops = 0;
    }

    const struct framebuffer_info *fb = fb_get_info();
    uint32_t w = fb->width;
    uint32_t h = fb->height;
    uint32_t pitch = w * 4;

    /* Ensure ring space: XY_SRC_COPY_BLT = 8 DWORDs */
    if (!ring_has_space(8)) {
        ring_wait_idle();
        if (!ring_has_space(8)) {
            gpu.sw_fallbacks++;
            return -1;
        }
    }

    /*
     * XY_SRC_COPY_BLT command:
     *   DW0: opcode | write_alpha | write_rgb | (dw_length - 2)
     *   DW1: (rop << 16) | color_depth | dst_pitch
     *   DW2: (dst_y1 << 16) | dst_x1
     *   DW3: (dst_y2 << 16) | dst_x2
     *   DW4: dst GTT offset
     *   DW5: (src_y1 << 16) | src_x1
     *   DW6: src_pitch
     *   DW7: src GTT offset
     */
    ring_emit(XY_SRC_COPY_BLT_CMD | BLT_WRITE_ALPHA | BLT_WRITE_RGB |
              (8 - 2));
    ring_emit((BLT_ROP_SRC_COPY << 16) | BLT_COLOR_DEPTH_32 | pitch);
    ring_emit(0);                           /* dst (0,0) */
    ring_emit((h << 16) | w);              /* dst (w,h) */
    ring_emit(gpu.gpu_fb_offset);           /* dst: framebuffer in GTT */
    ring_emit(0);                           /* src (0,0) */
    ring_emit(pitch);                       /* src pitch */
    ring_emit(gpu.gpu_bb_offset);           /* src: backbuffer in GTT */

    ring_advance();

    gpu.blt_copies++;
    gpu.blt_bytes += (uint64_t)w * h * 4;

    return 0;
}

void intel_gpu_blt_wait(void)
{
    if (!gpu.initialized) return;
    ring_wait_idle();
    gpu.pending_ops = 0;
}

void intel_gpu_blt_sync(void)
{
    if (!gpu.initialized || gpu.pending_ops == 0) return;
    ring_wait_idle();
    gpu.pending_ops = 0;
}

uint32_t intel_gpu_pending_ops(void)
{
    return gpu.pending_ops;
}

/*============================================================================
 * Diagnostics
 *============================================================================*/

void intel_gpu_dump_info(void)
{
    if (!gpu.detected) {
        kprintf("\nIntel GPU: Not detected\n");
        kprintf("Rendering: Software (CPU)\n");
        return;
    }

    kprintf("\nIntel GPU Information:\n");
    kprintf("  Device ID:    0x%04x\n", gpu.device_id);
    kprintf("  Generation:   %s\n", gen_name(gpu.gen));
    kprintf("  MMIO Base:    0x%lx (%u KB)\n",
            (unsigned long)gpu.mmio_phys, gpu.mmio_size / 1024);

    if (gpu.stolen_size > 0)
        kprintf("  Stolen Mem:   %u MB\n", gpu.stolen_size / 1024 / 1024);
    if (gpu.aperture_size > 0)
        kprintf("  GTT Aperture: %u MB\n", gpu.aperture_size / 1024 / 1024);

    kprintf("  BLT Engine:   %s\n",
            gpu.initialized ? "Active" : "Inactive");

    if (gpu.initialized) {
        kprintf("\nBLT Ring Buffer:\n");
        kprintf("  Address:  0x%lx (%u KB)\n",
                (unsigned long)gpu.ring_phys, gpu.ring_size / 1024);
        kprintf("  Tail:     0x%x\n", gpu.ring_tail);
        kprintf("  Head:     0x%x\n",
                gpu_read(gpu.reg_ring_head) & RING_HEAD_ADDR_MASK);

        kprintf("\nBLT Statistics:\n");
        kprintf("  Fill ops:       %lu\n", (unsigned long)gpu.blt_fills);
        kprintf("  Clear ops:      %lu\n", (unsigned long)gpu.blt_clears);
        kprintf("  Blit ops:       %lu\n", (unsigned long)gpu.blt_blits);
        kprintf("  Screen copies:  %lu\n", (unsigned long)gpu.blt_screen_copies);
        kprintf("  Flip ops:       %lu\n", (unsigned long)gpu.blt_copies);
        kprintf("  Batched ops:    %lu\n", (unsigned long)gpu.batched_ops);
        kprintf("  Bytes moved:    %lu KB\n",
                (unsigned long)(gpu.blt_bytes / 1024));
        kprintf("  SW fallbacks:   %lu\n", (unsigned long)gpu.sw_fallbacks);
    }
}

/*============================================================================
 * GPU HAL Backend Adapter
 *============================================================================*/

static int hal_intel_init(void)
{
    intel_gpu_init();
    return gpu.initialized ? 0 : -1;
}

static int hal_intel_available(void)
{
    return intel_gpu_available();
}

static int hal_intel_fill_rect(uint32_t x, uint32_t y, uint32_t w,
                                uint32_t h, uint32_t color)
{
    if (w < 16 || h < 16) return -1;
    return intel_gpu_blt_fill(x, y, w, h, color);
}

static int hal_intel_clear(uint32_t color)
{
    return intel_gpu_blt_clear(color);
}

static int hal_intel_copy_region(uint32_t dx, uint32_t dy,
                                  uint32_t sx, uint32_t sy,
                                  uint32_t w, uint32_t h)
{
    if (w < 16 || h < 16) return -1;
    return intel_gpu_blt_screen_copy(dx, dy, sx, sy, w, h);
}

static int hal_intel_flip(void)
{
    return intel_gpu_blt_flip();
}

static void hal_intel_sync(void)
{
    intel_gpu_blt_sync();
}

static void hal_intel_wait(void)
{
    intel_gpu_blt_wait();
}

static int hal_intel_pending(void)
{
    return (int)intel_gpu_pending_ops();
}

static void hal_intel_get_stats(struct gpu_stats *out)
{
    if (!out) return;
    out->fills           = gpu.blt_fills;
    out->clears          = gpu.blt_clears;
    out->copies          = gpu.blt_copies;
    out->screen_copies   = gpu.blt_screen_copies;
    out->flips           = gpu.blt_copies;
    out->batched_ops     = gpu.batched_ops;
    out->sw_fallbacks    = gpu.sw_fallbacks;
    out->bytes_transferred = gpu.blt_bytes;
}

static struct gpu_ops intel_blt_ops = {
    .name        = "Intel BLT",
    .type        = GPU_BACKEND_INTEL,
    .priority    = 100,
    .init        = hal_intel_init,
    .available   = hal_intel_available,
    .fill_rect   = hal_intel_fill_rect,
    .clear       = hal_intel_clear,
    .copy_region = hal_intel_copy_region,
    .flip        = hal_intel_flip,
    .sync        = hal_intel_sync,
    .wait        = hal_intel_wait,
    .pending_ops = hal_intel_pending,
    .get_stats   = hal_intel_get_stats,
    .dump_info   = intel_gpu_dump_info,
};

void intel_gpu_register_hal(void)
{
    gpu_hal_register(&intel_blt_ops);
}
