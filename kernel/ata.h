/*
 * PhantomOS ATA/IDE Disk Driver
 * "To Create, Not To Destroy"
 *
 * Simple PIO mode ATA driver for reading/writing disk sectors.
 * Supports LBA28 addressing (up to 128GB).
 */

#ifndef PHANTOMOS_ATA_H
#define PHANTOMOS_ATA_H

#include <stdint.h>
#include <stddef.h>

/*============================================================================
 * Constants
 *============================================================================*/

/* ATA I/O Ports (Primary Channel) */
#define ATA_PRIMARY_BASE        0x1F0
#define ATA_PRIMARY_CTRL        0x3F6

/* ATA I/O Ports (Secondary Channel) */
#define ATA_SECONDARY_BASE      0x170
#define ATA_SECONDARY_CTRL      0x376

/* Register Offsets (from base) */
#define ATA_REG_DATA            0x00    /* Data (R/W) */
#define ATA_REG_ERROR           0x01    /* Error (R) / Features (W) */
#define ATA_REG_FEATURES        0x01
#define ATA_REG_SECCOUNT        0x02    /* Sector Count */
#define ATA_REG_LBA_LO          0x03    /* LBA Low byte */
#define ATA_REG_LBA_MID         0x04    /* LBA Mid byte */
#define ATA_REG_LBA_HI          0x05    /* LBA High byte */
#define ATA_REG_DRIVE           0x06    /* Drive/Head */
#define ATA_REG_STATUS          0x07    /* Status (R) / Command (W) */
#define ATA_REG_COMMAND         0x07

/* Control Register Offsets (from ctrl base) */
#define ATA_REG_ALT_STATUS      0x00    /* Alt Status (R) */
#define ATA_REG_DEV_CTRL        0x00    /* Device Control (W) */

/* Status Register Bits */
#define ATA_SR_BSY              0x80    /* Busy */
#define ATA_SR_DRDY             0x40    /* Drive ready */
#define ATA_SR_DF               0x20    /* Drive fault */
#define ATA_SR_DSC              0x10    /* Drive seek complete */
#define ATA_SR_DRQ              0x08    /* Data request ready */
#define ATA_SR_CORR             0x04    /* Corrected data */
#define ATA_SR_IDX              0x02    /* Index */
#define ATA_SR_ERR              0x01    /* Error */

/* Error Register Bits */
#define ATA_ER_BBK              0x80    /* Bad block */
#define ATA_ER_UNC              0x40    /* Uncorrectable data */
#define ATA_ER_MC               0x20    /* Media changed */
#define ATA_ER_IDNF             0x10    /* ID not found */
#define ATA_ER_MCR              0x08    /* Media change request */
#define ATA_ER_ABRT             0x04    /* Command aborted */
#define ATA_ER_TK0NF            0x02    /* Track 0 not found */
#define ATA_ER_AMNF             0x01    /* Address mark not found */

/* Device Control Register Bits */
#define ATA_DC_nIEN             0x02    /* Disable interrupts */
#define ATA_DC_SRST             0x04    /* Software reset */
#define ATA_DC_HOB              0x80    /* High order byte (LBA48) */

/* ATA Commands */
#define ATA_CMD_READ_PIO        0x20    /* Read sectors (PIO) */
#define ATA_CMD_READ_PIO_EXT    0x24    /* Read sectors (PIO, LBA48) */
#define ATA_CMD_WRITE_PIO       0x30    /* Write sectors (PIO) */
#define ATA_CMD_WRITE_PIO_EXT   0x34    /* Write sectors (PIO, LBA48) */
#define ATA_CMD_CACHE_FLUSH     0xE7    /* Flush cache */
#define ATA_CMD_CACHE_FLUSH_EXT 0xEA    /* Flush cache (LBA48) */
#define ATA_CMD_IDENTIFY        0xEC    /* Identify drive */
#define ATA_CMD_IDENTIFY_PACKET 0xA1    /* Identify packet device */

/* Drive Selection */
#define ATA_DRIVE_MASTER        0xE0    /* Master drive (LBA mode) */
#define ATA_DRIVE_SLAVE         0xF0    /* Slave drive (LBA mode) */

/* Sector size */
#define ATA_SECTOR_SIZE         512

/* Maximum drives */
#define ATA_MAX_DRIVES          4

/*============================================================================
 * Types
 *============================================================================*/

/* Drive identification */
typedef enum {
    ATA_TYPE_NONE = 0,      /* No drive */
    ATA_TYPE_ATA,           /* ATA (hard disk) */
    ATA_TYPE_ATAPI,         /* ATAPI (CD-ROM, etc.) */
} ata_drive_type_t;

/* Drive structure */
typedef struct {
    ata_drive_type_t    type;
    uint16_t            base_port;      /* I/O base port */
    uint16_t            ctrl_port;      /* Control port */
    uint8_t             drive_sel;      /* Drive select value */
    int                 is_slave;       /* Is slave drive? */

    /* Drive info (from IDENTIFY) */
    char                model[41];      /* Model string */
    char                serial[21];     /* Serial number */
    uint64_t            sectors;        /* Total sectors */
    uint64_t            size_mb;        /* Size in MB */
    int                 lba48;          /* Supports LBA48? */
} ata_drive_t;

/* Error codes */
typedef enum {
    ATA_OK = 0,
    ATA_ERR_NO_DRIVE,       /* Drive not present */
    ATA_ERR_TIMEOUT,        /* Operation timed out */
    ATA_ERR_DRIVE_FAULT,    /* Drive fault */
    ATA_ERR_READ,           /* Read error */
    ATA_ERR_WRITE,          /* Write error */
    ATA_ERR_INVALID,        /* Invalid parameter */
} ata_error_t;

/*============================================================================
 * API Functions
 *============================================================================*/

/*
 * Initialize the ATA driver
 * Detects all drives on primary and secondary channels
 */
void ata_init(void);

/*
 * Get drive info
 * @index: Drive index (0-3)
 * Returns: Pointer to drive structure, or NULL if not present
 */
const ata_drive_t *ata_get_drive(int index);

/*
 * Get number of detected drives
 */
int ata_drive_count(void);

/*
 * Read sectors from drive
 * @drive:  Drive index
 * @lba:    Starting LBA
 * @count:  Number of sectors to read
 * @buffer: Output buffer (must be count * 512 bytes)
 * Returns: ATA_OK on success, error code on failure
 */
ata_error_t ata_read_sectors(int drive, uint64_t lba, uint32_t count, void *buffer);

/*
 * Write sectors to drive
 * @drive:  Drive index
 * @lba:    Starting LBA
 * @count:  Number of sectors to write
 * @buffer: Input buffer (must be count * 512 bytes)
 * Returns: ATA_OK on success, error code on failure
 */
ata_error_t ata_write_sectors(int drive, uint64_t lba, uint32_t count, const void *buffer);

/*
 * Flush drive cache
 * Ensures all written data is committed to disk
 */
ata_error_t ata_flush(int drive);

/*
 * Get error string
 */
const char *ata_strerror(ata_error_t err);

/*
 * Debug: dump drive info
 */
void ata_dump_drives(void);

#endif /* PHANTOMOS_ATA_H */
