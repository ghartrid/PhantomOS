/*
 * PhantomOS ATA/IDE Disk Driver
 * "To Create, Not To Destroy"
 *
 * Simple PIO mode ATA driver implementation.
 */

#include "ata.h"
#include <stdint.h>
#include <stddef.h>

/*============================================================================
 * External Declarations
 *============================================================================*/

extern int kprintf(const char *fmt, ...);
extern void *memset(void *s, int c, size_t n);
extern void *memcpy(void *dest, const void *src, size_t n);

/*============================================================================
 * Port I/O
 *============================================================================*/

static inline uint8_t inb(uint16_t port)
{
    uint8_t val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t inw(uint16_t port)
{
    uint16_t val;
    __asm__ volatile("inw %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void outw(uint16_t port, uint16_t val)
{
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline void insw(uint16_t port, void *addr, uint32_t count)
{
    __asm__ volatile("rep insw"
                     : "+D"(addr), "+c"(count)
                     : "d"(port)
                     : "memory");
}

static inline void outsw(uint16_t port, const void *addr, uint32_t count)
{
    __asm__ volatile("rep outsw"
                     : "+S"(addr), "+c"(count)
                     : "d"(port));
}

/*============================================================================
 * Driver State
 *============================================================================*/

static ata_drive_t ata_drives[ATA_MAX_DRIVES];
static int ata_num_drives = 0;
static int ata_initialized = 0;

/*============================================================================
 * Helper Functions
 *============================================================================*/

/* Wait for BSY to clear */
static int ata_wait_bsy(uint16_t base)
{
    int timeout = 100000;
    while (timeout-- > 0) {
        uint8_t status = inb(base + ATA_REG_STATUS);
        if (!(status & ATA_SR_BSY)) {
            return 0;
        }
    }
    return -1;  /* Timeout */
}

/* Wait for DRQ or error */
static int ata_wait_drq(uint16_t base)
{
    int timeout = 100000;
    while (timeout-- > 0) {
        uint8_t status = inb(base + ATA_REG_STATUS);
        if (status & ATA_SR_ERR) {
            return -1;  /* Error */
        }
        if (status & ATA_SR_DF) {
            return -2;  /* Drive fault */
        }
        if (status & ATA_SR_DRQ) {
            return 0;   /* Ready */
        }
    }
    return -3;  /* Timeout */
}

/* Wait for drive ready */
static int ata_wait_ready(uint16_t base)
{
    int timeout = 100000;
    while (timeout-- > 0) {
        uint8_t status = inb(base + ATA_REG_STATUS);
        if (status & ATA_SR_ERR) {
            return -1;
        }
        if (status & ATA_SR_DF) {
            return -2;
        }
        if ((status & (ATA_SR_BSY | ATA_SR_DRDY)) == ATA_SR_DRDY) {
            return 0;
        }
    }
    return -3;
}

/* 400ns delay by reading alt status */
static void ata_delay(uint16_t ctrl)
{
    for (int i = 0; i < 4; i++) {
        inb(ctrl);
    }
}

/* Select drive */
static void ata_select_drive(ata_drive_t *drive)
{
    outb(drive->base_port + ATA_REG_DRIVE, drive->drive_sel);
    ata_delay(drive->ctrl_port);
}

/* Software reset */
static void ata_software_reset(uint16_t ctrl)
{
    outb(ctrl, ATA_DC_SRST);
    ata_delay(ctrl);
    outb(ctrl, 0);
    ata_delay(ctrl);
}

/* Copy string from IDENTIFY data (swapped byte order) */
static void ata_copy_string(char *dest, uint16_t *src, int words)
{
    for (int i = 0; i < words; i++) {
        dest[i * 2] = (src[i] >> 8) & 0xFF;
        dest[i * 2 + 1] = src[i] & 0xFF;
    }
    dest[words * 2] = '\0';

    /* Trim trailing spaces */
    for (int i = words * 2 - 1; i >= 0 && dest[i] == ' '; i--) {
        dest[i] = '\0';
    }
}

/*============================================================================
 * Drive Detection
 *============================================================================*/

static int ata_identify_drive(ata_drive_t *drive)
{
    uint16_t base = drive->base_port;
    uint16_t ctrl = drive->ctrl_port;
    uint16_t identify_data[256];

    /* Select drive */
    ata_select_drive(drive);

    /* Disable interrupts */
    outb(ctrl, ATA_DC_nIEN);

    /* Clear sector count and LBA registers */
    outb(base + ATA_REG_SECCOUNT, 0);
    outb(base + ATA_REG_LBA_LO, 0);
    outb(base + ATA_REG_LBA_MID, 0);
    outb(base + ATA_REG_LBA_HI, 0);

    /* Send IDENTIFY command */
    outb(base + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    ata_delay(ctrl);

    /* Check if drive exists */
    uint8_t status = inb(base + ATA_REG_STATUS);
    if (status == 0) {
        return -1;  /* No drive */
    }

    /* Wait for BSY to clear */
    if (ata_wait_bsy(base) < 0) {
        return -1;
    }

    /* Check for ATAPI */
    uint8_t lba_mid = inb(base + ATA_REG_LBA_MID);
    uint8_t lba_hi = inb(base + ATA_REG_LBA_HI);

    if (lba_mid == 0x14 && lba_hi == 0xEB) {
        /* ATAPI device - try IDENTIFY PACKET */
        outb(base + ATA_REG_COMMAND, ATA_CMD_IDENTIFY_PACKET);
        ata_delay(ctrl);

        if (ata_wait_bsy(base) < 0) {
            return -1;
        }

        drive->type = ATA_TYPE_ATAPI;
    } else if (lba_mid == 0 && lba_hi == 0) {
        drive->type = ATA_TYPE_ATA;
    } else {
        return -1;  /* Unknown device */
    }

    /* Wait for data */
    if (ata_wait_drq(base) < 0) {
        return -1;
    }

    /* Read IDENTIFY data */
    insw(base + ATA_REG_DATA, identify_data, 256);

    /* Extract drive info */
    ata_copy_string(drive->serial, &identify_data[10], 10);
    ata_copy_string(drive->model, &identify_data[27], 20);

    /* Check for LBA48 support */
    if (identify_data[83] & (1 << 10)) {
        drive->lba48 = 1;
        /* LBA48 sector count is at words 100-103 */
        drive->sectors = (uint64_t)identify_data[100] |
                         ((uint64_t)identify_data[101] << 16) |
                         ((uint64_t)identify_data[102] << 32) |
                         ((uint64_t)identify_data[103] << 48);
    } else {
        drive->lba48 = 0;
        /* LBA28 sector count is at words 60-61 */
        drive->sectors = (uint64_t)identify_data[60] |
                         ((uint64_t)identify_data[61] << 16);
    }

    drive->size_mb = (drive->sectors * ATA_SECTOR_SIZE) / (1024 * 1024);

    return 0;
}

/*============================================================================
 * Initialization
 *============================================================================*/

void ata_init(void)
{
    if (ata_initialized) {
        return;
    }

    memset(ata_drives, 0, sizeof(ata_drives));
    ata_num_drives = 0;

    /* Configuration for all 4 possible drives */
    struct {
        uint16_t base;
        uint16_t ctrl;
        int is_slave;
    } configs[4] = {
        { ATA_PRIMARY_BASE,   ATA_PRIMARY_CTRL,   0 },  /* Primary Master */
        { ATA_PRIMARY_BASE,   ATA_PRIMARY_CTRL,   1 },  /* Primary Slave */
        { ATA_SECONDARY_BASE, ATA_SECONDARY_CTRL, 0 },  /* Secondary Master */
        { ATA_SECONDARY_BASE, ATA_SECONDARY_CTRL, 1 },  /* Secondary Slave */
    };

    /* Reset both channels */
    ata_software_reset(ATA_PRIMARY_CTRL);
    ata_software_reset(ATA_SECONDARY_CTRL);

    /* Detect drives */
    for (int i = 0; i < 4; i++) {
        ata_drive_t *drive = &ata_drives[i];
        drive->base_port = configs[i].base;
        drive->ctrl_port = configs[i].ctrl;
        drive->is_slave = configs[i].is_slave;
        drive->drive_sel = configs[i].is_slave ? ATA_DRIVE_SLAVE : ATA_DRIVE_MASTER;
        drive->type = ATA_TYPE_NONE;

        if (ata_identify_drive(drive) == 0) {
            ata_num_drives++;
        }
    }

    ata_initialized = 1;

    if (ata_num_drives > 0) {
        kprintf("  ATA: Found %d drive(s)\n", ata_num_drives);
    } else {
        kprintf("  ATA: No drives detected\n");
    }
}

/*============================================================================
 * Public API
 *============================================================================*/

const ata_drive_t *ata_get_drive(int index)
{
    if (index < 0 || index >= ATA_MAX_DRIVES) {
        return NULL;
    }
    if (ata_drives[index].type == ATA_TYPE_NONE) {
        return NULL;
    }
    return &ata_drives[index];
}

int ata_drive_count(void)
{
    return ata_num_drives;
}

ata_error_t ata_read_sectors(int drive_idx, uint64_t lba, uint32_t count, void *buffer)
{
    if (drive_idx < 0 || drive_idx >= ATA_MAX_DRIVES) {
        return ATA_ERR_INVALID;
    }

    ata_drive_t *drive = &ata_drives[drive_idx];
    if (drive->type == ATA_TYPE_NONE) {
        return ATA_ERR_NO_DRIVE;
    }

    if (count == 0 || !buffer) {
        return ATA_ERR_INVALID;
    }

    /* Check LBA range */
    if (lba + count > drive->sectors) {
        return ATA_ERR_INVALID;
    }

    uint16_t base = drive->base_port;
    uint16_t ctrl = drive->ctrl_port;
    uint8_t *buf = (uint8_t *)buffer;

    /* Select drive */
    ata_select_drive(drive);

    /* Disable interrupts */
    outb(ctrl, ATA_DC_nIEN);

    /* Wait for drive ready */
    if (ata_wait_ready(base) < 0) {
        return ATA_ERR_TIMEOUT;
    }

    /* Use LBA48 if needed or supported */
    int use_lba48 = drive->lba48 && (lba >= 0x10000000 || count > 255);

    if (use_lba48) {
        /* LBA48 */
        outb(base + ATA_REG_SECCOUNT, (count >> 8) & 0xFF);
        outb(base + ATA_REG_LBA_LO, (lba >> 24) & 0xFF);
        outb(base + ATA_REG_LBA_MID, (lba >> 32) & 0xFF);
        outb(base + ATA_REG_LBA_HI, (lba >> 40) & 0xFF);
        outb(base + ATA_REG_SECCOUNT, count & 0xFF);
        outb(base + ATA_REG_LBA_LO, lba & 0xFF);
        outb(base + ATA_REG_LBA_MID, (lba >> 8) & 0xFF);
        outb(base + ATA_REG_LBA_HI, (lba >> 16) & 0xFF);
        outb(base + ATA_REG_DRIVE, drive->drive_sel | 0x40);
        outb(base + ATA_REG_COMMAND, ATA_CMD_READ_PIO_EXT);
    } else {
        /* LBA28 */
        outb(base + ATA_REG_SECCOUNT, count & 0xFF);
        outb(base + ATA_REG_LBA_LO, lba & 0xFF);
        outb(base + ATA_REG_LBA_MID, (lba >> 8) & 0xFF);
        outb(base + ATA_REG_LBA_HI, (lba >> 16) & 0xFF);
        outb(base + ATA_REG_DRIVE, drive->drive_sel | ((lba >> 24) & 0x0F));
        outb(base + ATA_REG_COMMAND, ATA_CMD_READ_PIO);
    }

    /* Read sectors */
    for (uint32_t i = 0; i < count; i++) {
        /* Wait for data */
        int result = ata_wait_drq(base);
        if (result < 0) {
            if (result == -1) return ATA_ERR_READ;
            if (result == -2) return ATA_ERR_DRIVE_FAULT;
            return ATA_ERR_TIMEOUT;
        }

        /* Read sector */
        insw(base + ATA_REG_DATA, buf, 256);
        buf += ATA_SECTOR_SIZE;
    }

    return ATA_OK;
}

ata_error_t ata_write_sectors(int drive_idx, uint64_t lba, uint32_t count, const void *buffer)
{
    if (drive_idx < 0 || drive_idx >= ATA_MAX_DRIVES) {
        return ATA_ERR_INVALID;
    }

    ata_drive_t *drive = &ata_drives[drive_idx];
    if (drive->type == ATA_TYPE_NONE) {
        return ATA_ERR_NO_DRIVE;
    }

    if (count == 0 || !buffer) {
        return ATA_ERR_INVALID;
    }

    /* Check LBA range */
    if (lba + count > drive->sectors) {
        return ATA_ERR_INVALID;
    }

    uint16_t base = drive->base_port;
    uint16_t ctrl = drive->ctrl_port;
    const uint8_t *buf = (const uint8_t *)buffer;

    /* Select drive */
    ata_select_drive(drive);

    /* Disable interrupts */
    outb(ctrl, ATA_DC_nIEN);

    /* Wait for drive ready */
    if (ata_wait_ready(base) < 0) {
        return ATA_ERR_TIMEOUT;
    }

    /* Use LBA48 if needed */
    int use_lba48 = drive->lba48 && (lba >= 0x10000000 || count > 255);

    if (use_lba48) {
        /* LBA48 */
        outb(base + ATA_REG_SECCOUNT, (count >> 8) & 0xFF);
        outb(base + ATA_REG_LBA_LO, (lba >> 24) & 0xFF);
        outb(base + ATA_REG_LBA_MID, (lba >> 32) & 0xFF);
        outb(base + ATA_REG_LBA_HI, (lba >> 40) & 0xFF);
        outb(base + ATA_REG_SECCOUNT, count & 0xFF);
        outb(base + ATA_REG_LBA_LO, lba & 0xFF);
        outb(base + ATA_REG_LBA_MID, (lba >> 8) & 0xFF);
        outb(base + ATA_REG_LBA_HI, (lba >> 16) & 0xFF);
        outb(base + ATA_REG_DRIVE, drive->drive_sel | 0x40);
        outb(base + ATA_REG_COMMAND, ATA_CMD_WRITE_PIO_EXT);
    } else {
        /* LBA28 */
        outb(base + ATA_REG_SECCOUNT, count & 0xFF);
        outb(base + ATA_REG_LBA_LO, lba & 0xFF);
        outb(base + ATA_REG_LBA_MID, (lba >> 8) & 0xFF);
        outb(base + ATA_REG_LBA_HI, (lba >> 16) & 0xFF);
        outb(base + ATA_REG_DRIVE, drive->drive_sel | ((lba >> 24) & 0x0F));
        outb(base + ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);
    }

    /* Write sectors */
    for (uint32_t i = 0; i < count; i++) {
        /* Wait for DRQ */
        int result = ata_wait_drq(base);
        if (result < 0) {
            if (result == -1) return ATA_ERR_WRITE;
            if (result == -2) return ATA_ERR_DRIVE_FAULT;
            return ATA_ERR_TIMEOUT;
        }

        /* Write sector */
        outsw(base + ATA_REG_DATA, buf, 256);
        buf += ATA_SECTOR_SIZE;
    }

    /* Flush cache */
    ata_flush(drive_idx);

    return ATA_OK;
}

ata_error_t ata_flush(int drive_idx)
{
    if (drive_idx < 0 || drive_idx >= ATA_MAX_DRIVES) {
        return ATA_ERR_INVALID;
    }

    ata_drive_t *drive = &ata_drives[drive_idx];
    if (drive->type == ATA_TYPE_NONE) {
        return ATA_ERR_NO_DRIVE;
    }

    uint16_t base = drive->base_port;

    /* Select drive */
    ata_select_drive(drive);

    /* Send flush command */
    if (drive->lba48) {
        outb(base + ATA_REG_COMMAND, ATA_CMD_CACHE_FLUSH_EXT);
    } else {
        outb(base + ATA_REG_COMMAND, ATA_CMD_CACHE_FLUSH);
    }

    /* Wait for completion */
    if (ata_wait_bsy(base) < 0) {
        return ATA_ERR_TIMEOUT;
    }

    return ATA_OK;
}

const char *ata_strerror(ata_error_t err)
{
    switch (err) {
    case ATA_OK:            return "Success";
    case ATA_ERR_NO_DRIVE:  return "No drive present";
    case ATA_ERR_TIMEOUT:   return "Operation timed out";
    case ATA_ERR_DRIVE_FAULT: return "Drive fault";
    case ATA_ERR_READ:      return "Read error";
    case ATA_ERR_WRITE:     return "Write error";
    case ATA_ERR_INVALID:   return "Invalid parameter";
    default:                return "Unknown error";
    }
}

void ata_dump_drives(void)
{
    kprintf("\nATA Drives:\n");

    int found = 0;
    for (int i = 0; i < ATA_MAX_DRIVES; i++) {
        ata_drive_t *drive = &ata_drives[i];
        if (drive->type == ATA_TYPE_NONE) continue;

        found++;
        const char *type = (drive->type == ATA_TYPE_ATA) ? "ATA" : "ATAPI";
        const char *loc = drive->is_slave ? "Slave" : "Master";
        const char *chan = (drive->base_port == ATA_PRIMARY_BASE) ? "Primary" : "Secondary";

        kprintf("  [%d] %s %s %s\n", i, chan, loc, type);
        kprintf("      Model:  %s\n", drive->model);
        kprintf("      Serial: %s\n", drive->serial);
        kprintf("      Size:   %lu MB (%lu sectors)\n",
                (unsigned long)drive->size_mb,
                (unsigned long)drive->sectors);
        kprintf("      LBA48:  %s\n", drive->lba48 ? "Yes" : "No");
    }

    if (!found) {
        kprintf("  No drives detected\n");
    }
}
