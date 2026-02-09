/*
 * PhantomOS Intel Integrated GPU Driver
 * "To Create, Not To Destroy"
 *
 * Provides 2D hardware acceleration via the Intel BLT (Block Transfer) engine
 * for integrated graphics (Gen3-Gen9: i915 through Coffee Lake).
 *
 * Falls back gracefully to software rendering when no Intel GPU is detected
 * (e.g., in QEMU with -vga std).
 */

#ifndef PHANTOMOS_INTEL_GPU_H
#define PHANTOMOS_INTEL_GPU_H

#include <stdint.h>
#include "pci.h"

/*============================================================================
 * Intel PCI Identification
 *============================================================================*/

#define INTEL_VENDOR_ID         0x8086

/*============================================================================
 * GPU Generation
 *============================================================================*/

typedef enum {
    INTEL_GPU_GEN_UNKNOWN = 0,
    INTEL_GPU_GEN3,             /* i915, i945 (GMA 900/950) */
    INTEL_GPU_GEN4,             /* i965, G35 (GMA X3000/X3500) */
    INTEL_GPU_GEN5,             /* Ironlake (HD Graphics) */
    INTEL_GPU_GEN6,             /* Sandy Bridge (HD 2000/3000) */
    INTEL_GPU_GEN7,             /* Ivy Bridge, Haswell (HD 4000/4600) */
    INTEL_GPU_GEN8,             /* Broadwell (HD 5500/6000, Iris) */
    INTEL_GPU_GEN9,             /* Skylake-Coffee Lake (HD 530/630, UHD 620/630) */
    INTEL_GPU_GEN_UNSUPPORTED,  /* Gen 10+ (different architecture) */
} intel_gpu_gen_t;

/*============================================================================
 * MMIO Register Offsets (Intel Open Source PRM)
 *============================================================================*/

/* Render ring (Gen3-5 BLT goes here) */
#define INTEL_RCS_RING_TAIL     0x02030
#define INTEL_RCS_RING_HEAD     0x02034
#define INTEL_RCS_RING_START    0x02038
#define INTEL_RCS_RING_CTL      0x0203C
#define INTEL_RCS_HWS_PGA      0x02080

/* BLT ring (Gen6+ dedicated BLT engine) */
#define INTEL_BCS_RING_TAIL     0x22030
#define INTEL_BCS_RING_HEAD     0x22034
#define INTEL_BCS_RING_START    0x22038
#define INTEL_BCS_RING_CTL      0x2203C
#define INTEL_BCS_HWS_PGA      0x22080

/* Ring control bits */
#define RING_CTL_ENABLE         (1 << 0)
#define RING_CTL_SIZE_SHIFT     12      /* bits 20:12 = (pages-1) */
#define RING_HEAD_ADDR_MASK     0x001FFFFC

/* GTT (Graphics Translation Table) */
#define INTEL_GTT_OFFSET_GEN3   0x10000     /* GTT in MMIO (Gen3-5) */
/* Gen6+: GTT is at mmio_base + mmio_size/2 */

/* GTT entry flags */
#define GTT_ENTRY_VALID         (1 << 0)

/* Page Table Control */
#define INTEL_PGTBL_CTL         0x02020

/* Device reset */
#define INTEL_GDRST             0x0941C

/* Stolen memory (via PCI config space, not MMIO) */
#define INTEL_BSM               0x5C    /* Base of Stolen Memory (Gen6+) */
#define INTEL_GMCH_CTL          0x50    /* GMCH Control */

/*============================================================================
 * BLT Command Definitions (Intel Blitter Engine)
 *============================================================================*/

/* Command client field (bits 31:29) */
#define BLT_CLIENT              (2 << 29)

/* BLT opcodes (bits 28:22) */
#define XY_COLOR_BLT_CMD        (BLT_CLIENT | (0x50 << 22))
#define XY_SRC_COPY_BLT_CMD    (BLT_CLIENT | (0x53 << 22))

/* BLT modifier bits */
#define BLT_WRITE_ALPHA         (1 << 21)
#define BLT_WRITE_RGB           (1 << 20)

/* Color depth (bits 25:24) */
#define BLT_COLOR_DEPTH_32      (3 << 24)

/* Raster operation codes */
#define BLT_ROP_PAT_COPY        0xF0    /* Solid fill: Dest = Pattern */
#define BLT_ROP_SRC_COPY        0xCC    /* Copy: Dest = Source */

/* MI commands */
#define MI_NOOP                 0x00000000
#define MI_FLUSH                (0x04 << 23)
#define MI_BATCH_BUFFER_END     (0x0A << 23)

/*============================================================================
 * Ring Buffer Configuration
 *============================================================================*/

#define INTEL_RING_SIZE_PAGES   4               /* 16KB ring buffer */
#define INTEL_RING_SIZE         (INTEL_RING_SIZE_PAGES * 4096)
#define INTEL_RING_MASK         (INTEL_RING_SIZE - 1)

/*============================================================================
 * GPU State Structure
 *============================================================================*/

struct intel_gpu {
    /* Detection */
    int                     detected;       /* 1 = Intel GPU found on PCI */
    int                     initialized;    /* 1 = BLT engine ready */
    intel_gpu_gen_t         gen;
    uint16_t                device_id;
    const struct pci_device *pci_dev;

    /* MMIO registers (BAR0) */
    volatile uint32_t      *mmio_base;
    uint64_t                mmio_phys;
    uint32_t                mmio_size;

    /* GTT */
    volatile uint32_t      *gtt_base;       /* Mapped GTT entries */
    uint64_t                aperture_phys;  /* BAR2 (GTT aperture) */
    uint32_t                aperture_size;

    /* Stolen memory */
    uint64_t                stolen_base;
    uint32_t                stolen_size;

    /* Ring buffer */
    uint32_t               *ring_buffer;    /* Virtual address */
    uint64_t                ring_phys;      /* Physical address */
    uint32_t                ring_tail;      /* Write position (bytes) */
    uint32_t                ring_size;

    /* Hardware Status Page */
    uint32_t               *hws_page;
    uint64_t                hws_phys;

    /* Ring register offsets (set based on gen) */
    uint32_t                reg_ring_tail;
    uint32_t                reg_ring_head;
    uint32_t                reg_ring_start;
    uint32_t                reg_ring_ctl;
    uint32_t                reg_hws;

    /* GTT offsets for BLT operations */
    uint32_t                gpu_bb_offset;  /* Backbuffer in GTT */
    uint32_t                gpu_fb_offset;  /* Framebuffer in GTT */

    /* Batched BLT tracking */
    uint32_t                pending_ops;    /* Ops queued since last wait */

    /* Statistics */
    uint64_t                blt_fills;
    uint64_t                blt_copies;
    uint64_t                blt_clears;
    uint64_t                blt_blits;
    uint64_t                blt_screen_copies;
    uint64_t                blt_bytes;
    uint64_t                batched_ops;    /* Total ops batched without wait */
    uint64_t                sw_fallbacks;
};

/*============================================================================
 * API Functions
 *============================================================================*/

/* Initialize Intel GPU driver (call after pci_init and fb_init) */
void intel_gpu_init(void);

/* Check if GPU acceleration is available */
int intel_gpu_available(void);

/* Get GPU info (for diagnostics) */
const struct intel_gpu *intel_gpu_get_info(void);

/*
 * BLT rectangle fill (targets backbuffer)
 * Returns 0 on success, -1 on failure (caller should use software fallback)
 */
int intel_gpu_blt_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                       uint32_t color);

/*
 * BLT full-screen fill (accelerated fb_clear)
 * Returns 0 on success, -1 on failure
 */
int intel_gpu_blt_clear(uint32_t color);

/*
 * BLT copy: arbitrary source buffer -> backbuffer region
 * src_phys is physical address of source buffer, src_pitch in bytes
 * Returns 0 on success, -1 on failure
 */
int intel_gpu_blt_copy_to_bb(uint32_t dst_x, uint32_t dst_y,
                              uint32_t w, uint32_t h,
                              uint32_t src_x, uint32_t src_y,
                              uint32_t src_pitch, uint32_t src_gtt_offset);

/*
 * BLT screen-to-screen copy within backbuffer
 * Used for window manager operations (scroll, move)
 * Returns 0 on success, -1 on failure
 */
int intel_gpu_blt_screen_copy(uint32_t dst_x, uint32_t dst_y,
                               uint32_t src_x, uint32_t src_y,
                               uint32_t w, uint32_t h);

/*
 * BLT copy: backbuffer -> framebuffer (accelerated fb_flip)
 * Returns 0 on success, -1 on failure
 */
int intel_gpu_blt_flip(void);

/* Wait for all pending BLT operations to complete */
void intel_gpu_blt_wait(void);

/* Drain pending batched BLT operations (call before reading backbuffer) */
void intel_gpu_blt_sync(void);

/* Get count of pending (unsynced) BLT operations */
uint32_t intel_gpu_pending_ops(void);

/* Print GPU info and BLT statistics */
void intel_gpu_dump_info(void);

/* Register Intel BLT as a GPU HAL backend */
void intel_gpu_register_hal(void);

#endif /* PHANTOMOS_INTEL_GPU_H */
