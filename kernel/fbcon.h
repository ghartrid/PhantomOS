/*
 * PhantomOS Framebuffer Console
 * "To Create, Not To Destroy"
 *
 * Text console rendered on the framebuffer, replacing VGA text mode.
 */

#ifndef PHANTOMOS_FBCON_H
#define PHANTOMOS_FBCON_H

/*
 * Initialize framebuffer console
 * Sets up character grid based on framebuffer dimensions
 */
void fbcon_init(void);

/*
 * Write a single character to the framebuffer console
 * Handles cursor advancement, newlines, and scrolling.
 */
void fbcon_putchar(char c);

/*
 * Check if framebuffer console is active
 */
int fbcon_is_active(void);

/*
 * Clear the framebuffer console
 */
void fbcon_clear(void);

/*
 * Disable framebuffer console (when desktop takes over rendering)
 */
void fbcon_disable(void);

#endif /* PHANTOMOS_FBCON_H */
