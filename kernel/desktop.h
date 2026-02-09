/*
 * PhantomOS Desktop Environment
 * "To Create, Not To Destroy"
 *
 * Desktop with taskbar, window management, and default applications.
 */

#ifndef PHANTOMOS_DESKTOP_H
#define PHANTOMOS_DESKTOP_H

#include "geofs.h"

/*
 * Initialize the desktop environment
 * Sets up taskbar, desktop background, and default windows
 */
void desktop_init(kgeofs_volume_t *vol);

/*
 * Run the desktop event loop
 * Handles mouse, keyboard, and repaints.
 * Returns when ACPI shutdown is requested.
 */
void desktop_run(void);

#endif /* PHANTOMOS_DESKTOP_H */
