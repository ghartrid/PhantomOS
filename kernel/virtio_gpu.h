/*
 * PhantomOS VirtIO GPU Driver
 * "To Create, Not To Destroy"
 *
 * VirtIO GPU 2D driver using virtqueue command submission.
 * Provides DMA-based flip via TRANSFER_TO_HOST_2D + RESOURCE_FLUSH,
 * which is faster than PIO memcpy to MMIO framebuffer.
 */

#ifndef PHANTOMOS_VIRTIO_GPU_H
#define PHANTOMOS_VIRTIO_GPU_H

#include <stdint.h>

/*============================================================================
 * VirtIO PCI Constants
 *============================================================================*/

#define VIRTIO_VENDOR_ID            0x1AF4
#define VIRTIO_GPU_DEVICE_ID        0x1050  /* Transitional: 0x1040+16 */

/*============================================================================
 * VirtIO Device Status Bits
 *============================================================================*/

#define VIRTIO_STATUS_ACKNOWLEDGE   0x01
#define VIRTIO_STATUS_DRIVER        0x02
#define VIRTIO_STATUS_FEATURES_OK   0x08
#define VIRTIO_STATUS_DRIVER_OK     0x04
#define VIRTIO_STATUS_FAILED        0x80

/*============================================================================
 * VirtIO PCI Capability Types
 *============================================================================*/

#define VIRTIO_PCI_CAP_COMMON_CFG   1
#define VIRTIO_PCI_CAP_NOTIFY_CFG   2
#define VIRTIO_PCI_CAP_ISR_CFG      3
#define VIRTIO_PCI_CAP_DEVICE_CFG   4

/*============================================================================
 * VirtIO GPU Command Types
 *============================================================================*/

/* 2D commands */
#define VIRTIO_GPU_CMD_GET_DISPLAY_INFO     0x0100
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_2D   0x0101
#define VIRTIO_GPU_CMD_RESOURCE_UNREF       0x0102
#define VIRTIO_GPU_CMD_SET_SCANOUT          0x0103
#define VIRTIO_GPU_CMD_RESOURCE_FLUSH       0x0104
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D  0x0105
#define VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING  0x0106

/* Responses */
#define VIRTIO_GPU_RESP_OK_NODATA           0x1100
#define VIRTIO_GPU_RESP_OK_DISPLAY_INFO     0x1101

/*============================================================================
 * VirtIO GPU Formats
 *============================================================================*/

#define VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM    1
#define VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM    2
#define VIRTIO_GPU_FORMAT_R8G8B8A8_UNORM    67
#define VIRTIO_GPU_FORMAT_X8R8G8B8_UNORM    68

/*============================================================================
 * Virtqueue Constants
 *============================================================================*/

#define VIRTQ_SIZE                  128     /* Number of descriptors */
#define VIRTQ_DESC_F_NEXT          0x01
#define VIRTQ_DESC_F_WRITE         0x02

/*============================================================================
 * API
 *============================================================================*/

/* Register VirtIO GPU as a GPU HAL backend */
void virtio_gpu_register_hal(void);

#endif /* PHANTOMOS_VIRTIO_GPU_H */
