/*
 * PhantomOS Icon Sprites
 * "To Create, Not To Destroy"
 *
 * 32x32 app icons and 16x16 dock icons as palette-indexed pixel art.
 */

#ifndef PHANTOMOS_ICONS_H
#define PHANTOMOS_ICONS_H

#include <stdint.h>

/*============================================================================
 * Icon Structures
 *============================================================================*/

/* Content area icons (32x32) */
#define ICON_SIZE           32
#define ICON_PALETTE_MAX    16

struct icon_sprite {
    const uint8_t   data[ICON_SIZE][ICON_SIZE];
    const uint32_t  palette[ICON_PALETTE_MAX];
};

/* Dock mini-icons (16x16) */
#define DOCK_ICON_SIZE      16

struct dock_icon_sprite {
    const uint8_t   data[DOCK_ICON_SIZE][DOCK_ICON_SIZE];
    const uint32_t  palette[ICON_PALETTE_MAX];
};

/* Sidebar category mini-icons (8x8) */
#define SIDEBAR_ICON_SIZE   8

struct sidebar_icon_sprite {
    const uint8_t   data[SIDEBAR_ICON_SIZE][SIDEBAR_ICON_SIZE];
    const uint32_t  palette[ICON_PALETTE_MAX];
};

/*============================================================================
 * Drawing Functions
 *============================================================================*/

void icon_draw(int x, int y, const struct icon_sprite *icon, uint32_t bg);
void dock_icon_draw(int x, int y, const struct dock_icon_sprite *icon, uint32_t bg);
void sidebar_icon_draw(int x, int y, const struct sidebar_icon_sprite *icon, uint32_t bg);

/*============================================================================
 * Icon Definitions
 *============================================================================*/

extern const struct icon_sprite icon_files;
extern const struct icon_sprite icon_terminal;
extern const struct icon_sprite icon_ai;
extern const struct icon_sprite icon_settings;
extern const struct icon_sprite icon_security;
extern const struct icon_sprite icon_artos;

extern const struct dock_icon_sprite dock_files;
extern const struct dock_icon_sprite dock_terminal;
extern const struct dock_icon_sprite dock_ai;
extern const struct dock_icon_sprite dock_settings;
extern const struct dock_icon_sprite dock_security;
extern const struct dock_icon_sprite dock_artos;

/* Sidebar category icons (8x8) - 7 categories */
extern const struct sidebar_icon_sprite sidebar_icon_core;
extern const struct sidebar_icon_sprite sidebar_icon_system;
extern const struct sidebar_icon_sprite sidebar_icon_security;
extern const struct sidebar_icon_sprite sidebar_icon_network;
extern const struct sidebar_icon_sprite sidebar_icon_apps;
extern const struct sidebar_icon_sprite sidebar_icon_utilities;
extern const struct sidebar_icon_sprite sidebar_icon_reference;

#endif /* PHANTOMOS_ICONS_H */
