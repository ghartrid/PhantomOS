/*
 * PhantomOS GPU Hardware Abstraction Layer
 * "To Create, Not To Destroy"
 *
 * Provides a unified interface for GPU backends (Intel BLT, VirtIO GPU,
 * Bochs VGA, software fallback). The highest-priority backend that
 * successfully initializes becomes the active renderer.
 */

#ifndef PHANTOMOS_GPU_HAL_H
#define PHANTOMOS_GPU_HAL_H

#include <stdint.h>

/*============================================================================
 * Constants
 *============================================================================*/

#define GPU_HAL_MAX_BACKENDS    8

/*============================================================================
 * Backend Types
 *============================================================================*/

enum gpu_backend_type {
    GPU_BACKEND_SOFTWARE = 0,
    GPU_BACKEND_BOCHS    = 1,
    GPU_BACKEND_VIRTIO   = 2,
    GPU_BACKEND_INTEL    = 3,
    GPU_BACKEND_VMWARE   = 4,
};

/*============================================================================
 * Unified Statistics
 *============================================================================*/

struct gpu_stats {
    uint64_t fills;
    uint64_t clears;
    uint64_t copies;
    uint64_t screen_copies;
    uint64_t flips;
    uint64_t batched_ops;
    uint64_t sw_fallbacks;
    uint64_t bytes_transferred;
};

/*============================================================================
 * Backend Operations (function pointer table)
 *============================================================================*/

struct gpu_ops {
    const char             *name;       /* "Intel BLT", "VirtIO GPU", etc. */
    enum gpu_backend_type   type;
    int                     priority;   /* Higher wins: Intel=100, VirtIO=80, Bochs=40, SW=0 */

    /* Lifecycle */
    int  (*init)(void);                 /* Probe + init. 0=success, -1=not present */
    int  (*available)(void);            /* Is this backend ready? */

    /* 2D operations: return 0=GPU handled, -1=not supported */
    int  (*fill_rect)(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                      uint32_t color);
    int  (*clear)(uint32_t color);
    int  (*copy_region)(uint32_t dst_x, uint32_t dst_y,
                        uint32_t src_x, uint32_t src_y,
                        uint32_t w, uint32_t h);
    int  (*flip)(void);                 /* Backbuffer -> frontbuffer */

    /* Synchronization */
    void (*sync)(void);                 /* Drain pending ops */
    void (*wait)(void);                 /* Wait for GPU idle */
    int  (*pending_ops)(void);          /* Queued op count */

    /* Resolution change */
    int  (*set_resolution)(uint32_t width, uint32_t height);  /* NULL = not supported */

    /* Diagnostics */
    void (*get_stats)(struct gpu_stats *out);
    void (*dump_info)(void);
};

/*============================================================================
 * HAL API
 *============================================================================*/

/* Initialize HAL and register built-in software backend */
void gpu_hal_init(void);

/* Register a backend (call before gpu_hal_select_best) */
int gpu_hal_register(struct gpu_ops *ops);

/* Probe all registered backends, activate highest-priority success */
void gpu_hal_select_best(void);

/* Check if any backend is active */
int gpu_hal_available(void);

/* Get active backend name (e.g. "Intel BLT") */
const char *gpu_hal_get_active_name(void);

/* Get active backend type */
enum gpu_backend_type gpu_hal_get_active_type(void);

/* 2D operations (dispatch to active backend) */
int  gpu_hal_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                       uint32_t color);
int  gpu_hal_clear(uint32_t color);
int  gpu_hal_copy_region(uint32_t dst_x, uint32_t dst_y,
                         uint32_t src_x, uint32_t src_y,
                         uint32_t w, uint32_t h);
int  gpu_hal_flip(void);

/* Resolution change (dispatches to active backend) */
int  gpu_hal_set_resolution(uint32_t width, uint32_t height);

/* Synchronization */
void gpu_hal_sync(void);
void gpu_hal_wait(void);
int  gpu_hal_pending_ops(void);

/* Diagnostics */
void gpu_hal_get_stats(struct gpu_stats *out);
void gpu_hal_dump_info(void);

#endif /* PHANTOMOS_GPU_HAL_H */
