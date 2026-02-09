/*
 * PhantomOS ACPI Power Management
 * "To Create, Not To Destroy"
 *
 * Minimal ACPI support for graceful shutdown/reboot.
 * Uses PIIX4 PM device (QEMU i440fx) for power control.
 */

#ifndef PHANTOMOS_ACPI_H
#define PHANTOMOS_ACPI_H

#include <stdint.h>

/* Initialize ACPI power management (call after pci_init) */
int acpi_init(void);

/* Check if shutdown has been requested (power button or software) */
int acpi_is_shutdown_requested(void);

/* Request a graceful shutdown (e.g. from GUI power button) */
void acpi_request_shutdown(void);

/* Power off the system via ACPI S5 sleep state */
void acpi_poweroff(void);

/* Reboot the system via reset port */
void acpi_reboot(void);

#endif /* PHANTOMOS_ACPI_H */
