/*
 * PhantomOS VirtIO Console Driver
 * "To Create, Not To Destroy"
 *
 * Paravirtualized console via virtio-serial/virtio-console.
 * Provides guest-host communication channel alongside serial/VGA output.
 */

#ifndef PHANTOMOS_VIRTIO_CONSOLE_H
#define PHANTOMOS_VIRTIO_CONSOLE_H

#include <stdint.h>
#include <stddef.h>

/* Initialize virtio console (call during boot after PCI init) */
int virtio_console_init(void);

/* Check if virtio console is available */
int virtio_console_available(void);

/* Write data to console (host receives on chardev) */
int virtio_console_write(const void *buf, size_t len);

/* Read data from console (host sends on chardev), returns bytes read */
int virtio_console_read(void *buf, size_t max);

/* Check if receive data is available (non-blocking) */
int virtio_console_has_data(void);

/* Write a single character (for kputchar integration) */
void virtio_console_putchar(char c);

#endif /* PHANTOMOS_VIRTIO_CONSOLE_H */
