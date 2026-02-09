/*
 * PhantomOS GPU Hardware Abstraction Layer
 * "To Create, Not To Destroy"
 *
 * Priority-based GPU backend selection and dispatch.
 * Backends register themselves; the highest-priority backend that
 * successfully initializes becomes the active renderer.
 */

#include "gpu_hal.h"
#include "vm_detect.h"
#include <stddef.h>

/*============================================================================
 * External Declarations
 *============================================================================*/

extern int kprintf(const char *fmt, ...);
extern void *memset(void *s, int c, size_t n);

/*============================================================================
 * Software Fallback Backend
 *============================================================================*/

static int  sw_init(void)            { return 0; }
static int  sw_available(void)       { return 1; }
static int  sw_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                          uint32_t color)
                                     { (void)x; (void)y; (void)w; (void)h;
                                       (void)color; return -1; }
static int  sw_clear(uint32_t color) { (void)color; return -1; }
static int  sw_copy_region(uint32_t dx, uint32_t dy, uint32_t sx,
                            uint32_t sy, uint32_t w, uint32_t h)
                                     { (void)dx; (void)dy; (void)sx;
                                       (void)sy; (void)w; (void)h; return -1; }
static int  sw_flip(void)            { return -1; }
static void sw_sync(void)            { }
static void sw_wait(void)            { }
static int  sw_pending_ops(void)     { return 0; }

static struct gpu_stats sw_stats;

static void sw_get_stats(struct gpu_stats *out) {
    if (out) *out = sw_stats;
}

static void sw_dump_info(void) {
    kprintf("\nGPU Backend: Software (CPU)\n");
    kprintf("  All rendering performed by CPU\n");
    kprintf("  No hardware acceleration available\n");
}

static struct gpu_ops software_backend = {
    .name        = "Software",
    .type        = GPU_BACKEND_SOFTWARE,
    .priority    = 0,
    .init        = sw_init,
    .available   = sw_available,
    .fill_rect   = sw_fill_rect,
    .clear       = sw_clear,
    .copy_region = sw_copy_region,
    .flip        = sw_flip,
    .sync        = sw_sync,
    .wait        = sw_wait,
    .pending_ops = sw_pending_ops,
    .get_stats   = sw_get_stats,
    .dump_info   = sw_dump_info,
};

/*============================================================================
 * HAL State
 *============================================================================*/

static struct gpu_ops *backends[GPU_HAL_MAX_BACKENDS];
static int             num_backends;
static struct gpu_ops *active_backend;
static int             hal_initialized;

/*============================================================================
 * Registration & Selection
 *============================================================================*/

void gpu_hal_init(void)
{
    num_backends = 0;
    active_backend = NULL;
    hal_initialized = 1;
    memset(&sw_stats, 0, sizeof(sw_stats));

    /* Software backend is always registered first */
    gpu_hal_register(&software_backend);

    kprintf("[GPU HAL] Initialized\n");
}

int gpu_hal_register(struct gpu_ops *ops)
{
    if (!ops || num_backends >= GPU_HAL_MAX_BACKENDS)
        return -1;

    backends[num_backends++] = ops;
    kprintf("[GPU HAL] Registered: %s (priority %d)\n", ops->name, ops->priority);
    return 0;
}

void gpu_hal_select_best(void)
{
    if (!hal_initialized || num_backends == 0)
        return;

    /* In VMs, deprioritize backends that need real hardware */
    if (vm_is_virtualized()) {
        kprintf("[GPU HAL] VM detected (%s) - adjusting backend priorities\n",
                vm_get_type_name());
        for (int i = 0; i < num_backends; i++) {
            if (backends[i]->type == GPU_BACKEND_INTEL) {
                backends[i]->priority = -1;
                kprintf("[GPU HAL]   %s: priority -> -1 (no real GPU in VM)\n",
                        backends[i]->name);
            }
        }
    }

    /* Sort by priority (simple selection: try highest first) */
    struct gpu_ops *best = NULL;
    int best_priority = -1;

    for (int i = 0; i < num_backends; i++) {
        if (backends[i]->priority > best_priority) {
            /* Try to initialize this backend */
            kprintf("[GPU HAL] Probing: %s...\n", backends[i]->name);
            if (backends[i]->init() == 0) {
                if (backends[i]->available()) {
                    best = backends[i];
                    best_priority = backends[i]->priority;
                }
            }
        }
    }

    active_backend = best;

    if (active_backend) {
        kprintf("[GPU HAL] Active backend: %s (priority %d)\n",
                active_backend->name, active_backend->priority);
    } else {
        kprintf("[GPU HAL] No backend available!\n");
    }
}

/*============================================================================
 * Query
 *============================================================================*/

int gpu_hal_available(void)
{
    return active_backend != NULL && active_backend->available();
}

const char *gpu_hal_get_active_name(void)
{
    if (active_backend)
        return active_backend->name;
    return "None";
}

enum gpu_backend_type gpu_hal_get_active_type(void)
{
    if (active_backend)
        return active_backend->type;
    return GPU_BACKEND_SOFTWARE;
}

/*============================================================================
 * 2D Operation Dispatch
 *============================================================================*/

int gpu_hal_set_resolution(uint32_t width, uint32_t height)
{
    if (active_backend && active_backend->set_resolution)
        return active_backend->set_resolution(width, height);
    return -1;
}

int gpu_hal_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                      uint32_t color)
{
    if (active_backend && active_backend->fill_rect)
        return active_backend->fill_rect(x, y, w, h, color);
    return -1;
}

int gpu_hal_clear(uint32_t color)
{
    if (active_backend && active_backend->clear)
        return active_backend->clear(color);
    return -1;
}

int gpu_hal_copy_region(uint32_t dst_x, uint32_t dst_y,
                        uint32_t src_x, uint32_t src_y,
                        uint32_t w, uint32_t h)
{
    if (active_backend && active_backend->copy_region)
        return active_backend->copy_region(dst_x, dst_y, src_x, src_y, w, h);
    return -1;
}

int gpu_hal_flip(void)
{
    if (active_backend && active_backend->flip)
        return active_backend->flip();
    return -1;
}

/*============================================================================
 * Synchronization Dispatch
 *============================================================================*/

void gpu_hal_sync(void)
{
    if (active_backend && active_backend->sync)
        active_backend->sync();
}

void gpu_hal_wait(void)
{
    if (active_backend && active_backend->wait)
        active_backend->wait();
}

int gpu_hal_pending_ops(void)
{
    if (active_backend && active_backend->pending_ops)
        return active_backend->pending_ops();
    return 0;
}

/*============================================================================
 * Diagnostics Dispatch
 *============================================================================*/

void gpu_hal_get_stats(struct gpu_stats *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (active_backend && active_backend->get_stats)
        active_backend->get_stats(out);
}

void gpu_hal_dump_info(void)
{
    kprintf("\n=== GPU HAL ===\n");
    kprintf("Registered backends: %d\n", num_backends);
    for (int i = 0; i < num_backends; i++) {
        kprintf("  [%d] %s (priority %d)%s\n", i,
                backends[i]->name, backends[i]->priority,
                backends[i] == active_backend ? " *ACTIVE*" : "");
    }

    if (active_backend && active_backend->dump_info) {
        active_backend->dump_info();
    }
}
