/*
 * PhantomOS ACPI Power Management
 * "To Create, Not To Destroy"
 *
 * Provides graceful shutdown via PIIX4 PM (QEMU i440fx).
 * Handles ACPI SCI interrupt (IRQ9) for power button events.
 *
 * PIIX4 PM registers (at PMBA from PCI config offset 0x40):
 *   PM1a_STS (PMBA+0): Status - bit 8 = PWRBTN_STS
 *   PM1a_EN  (PMBA+2): Enable - bit 8 = PWRBTN_EN
 *   PM1a_CNT (PMBA+4): Control - bit 0 = SCI_EN, bits[12:10] = SLP_TYP, bit 13 = SLP_EN
 */

#include "acpi.h"
#include "pci.h"
#include "idt.h"
#include "pic.h"
#include "io.h"

/*============================================================================
 * External Declarations
 *============================================================================*/

extern int kprintf(const char *fmt, ...);

/*============================================================================
 * PIIX4 PM Constants
 *============================================================================*/

#define PIIX4_VENDOR_ID     0x8086
#define PIIX4_DEVICE_ID     0x7113

/* PCI config register for Power Management Base Address */
#define PIIX4_PMBA_REG      0x40

/* PM1a register offsets from PMBA */
#define PM1A_STS_OFF        0x00
#define PM1A_EN_OFF         0x02
#define PM1A_CNT_OFF        0x04

/* PM1a_STS bits */
#define PWRBTN_STS          (1 << 8)
#define TMR_STS             (1 << 0)

/* PM1a_EN bits */
#define PWRBTN_EN           (1 << 8)

/* PM1a_CNT bits */
#define SCI_EN              (1 << 0)
#define SLP_EN              (1 << 13)
#define SLP_TYP_S5_SHIFT    10      /* QEMU PIIX4 S5 SLP_TYP = 0 */

/* QEMU reset port */
#define RESET_PORT          0x0CF9
#define RESET_CMD           0x06

/* ACPI SCI is routed to IRQ9 */
#define ACPI_SCI_IRQ        9

/*============================================================================
 * State
 *============================================================================*/

static uint16_t pmba = 0;          /* PM I/O base address */
static volatile int shutdown_requested = 0;
static int acpi_initialized = 0;

/*============================================================================
 * SCI Interrupt Handler (IRQ9, vector 41)
 *============================================================================*/

static void acpi_sci_handler(struct interrupt_frame *frame)
{
    (void)frame;

    if (!pmba) goto eoi;

    uint16_t sts = inw(pmba + PM1A_STS_OFF);

    if (sts & PWRBTN_STS) {
        /* Power button pressed - signal shutdown */
        outw(pmba + PM1A_STS_OFF, PWRBTN_STS);  /* Clear by writing 1 */
        shutdown_requested = 1;
        kprintf("[ACPI] Power button event - shutdown requested\n");
    }

    /* Clear any other status bits */
    if (sts & ~PWRBTN_STS) {
        outw(pmba + PM1A_STS_OFF, sts);
    }

eoi:
    pic_send_eoi(ACPI_SCI_IRQ);
}

/*============================================================================
 * API
 *============================================================================*/

int acpi_init(void)
{
    /* Find PIIX4 PM device on PCI bus */
    const struct pci_device *dev = pci_find_by_id(PIIX4_VENDOR_ID, PIIX4_DEVICE_ID);
    if (!dev) {
        kprintf("[ACPI] PIIX4 PM not found on PCI bus\n");
        return -1;
    }

    kprintf("[ACPI] Found PIIX4 PM at PCI %u:%u.%u\n",
            dev->bus, dev->device, dev->function);

    /* Read Power Management Base Address from PCI config offset 0x40 */
    uint32_t pmba_raw = pci_config_read32(dev->bus, dev->device,
                                           dev->function, PIIX4_PMBA_REG);
    pmba = (uint16_t)(pmba_raw & 0xFFC0);  /* Bits [15:6] = I/O base, bit 0 = enable */

    if (pmba == 0) {
        kprintf("[ACPI] PMBA not configured (raw=0x%x)\n", pmba_raw);
        return -1;
    }

    kprintf("[ACPI] PM I/O base: 0x%x\n", pmba);

    /* Clear all pending status bits */
    outw(pmba + PM1A_STS_OFF, 0xFFFF);

    /* Enable ACPI mode (set SCI_EN) */
    uint16_t cnt = inw(pmba + PM1A_CNT_OFF);
    cnt |= SCI_EN;
    outw(pmba + PM1A_CNT_OFF, cnt);

    /* Enable power button event */
    outw(pmba + PM1A_EN_OFF, PWRBTN_EN);

    /* Register SCI interrupt handler on IRQ9 (vector 41) */
    register_interrupt_handler(IRQ_BASE + ACPI_SCI_IRQ, acpi_sci_handler);
    pic_enable_irq(ACPI_SCI_IRQ);

    acpi_initialized = 1;
    kprintf("[ACPI] Power management initialized (SCI on IRQ%d)\n", ACPI_SCI_IRQ);

    return 0;
}

int acpi_is_shutdown_requested(void)
{
    return shutdown_requested;
}

void acpi_request_shutdown(void)
{
    shutdown_requested = 1;
    kprintf("[ACPI] Shutdown requested from GUI\n");
}

void acpi_poweroff(void)
{
    kprintf("[ACPI] Powering off...\n");

    cli();

    if (pmba) {
        /* Enter S5 sleep state: SLP_TYP=0 (QEMU PIIX4) | SLP_EN */
        outw(pmba + PM1A_CNT_OFF, SLP_EN | (0 << SLP_TYP_S5_SHIFT));
    }

    /* If ACPI poweroff didn't work, halt forever */
    for (;;) {
        __asm__ volatile("hlt");
    }
}

void acpi_reboot(void)
{
    kprintf("[ACPI] Rebooting...\n");

    cli();

    /* Use QEMU/chipset reset port */
    outb(RESET_PORT, RESET_CMD);

    /* Fallback: triple fault by loading invalid IDT */
    struct idt_ptr null_idt = { 0, 0 };
    __asm__ volatile("lidt %0" :: "m"(null_idt));
    __asm__ volatile("int $3");

    for (;;) {
        __asm__ volatile("hlt");
    }
}
