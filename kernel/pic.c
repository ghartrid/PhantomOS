/*
 * PhantomOS PIC (8259) Driver Implementation
 * "To Create, Not To Destroy"
 */

#include "pic.h"

/* External functions */
extern int kprintf(const char *fmt, ...);

/* Port I/O helpers */
static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* Small delay for PIC initialization */
static inline void io_wait(void)
{
    outb(0x80, 0);
}

/*
 * Initialize the 8259 PICs and remap IRQs
 *
 * By default, IRQ 0-7 map to interrupts 8-15, which conflicts with
 * CPU exceptions. We remap them to 32-47.
 */
void pic_init(void)
{
    /* Start initialization sequence (ICW1) */
    outb(PIC1_COMMAND, 0x11);   /* ICW1: Initialize + ICW4 needed */
    io_wait();
    outb(PIC2_COMMAND, 0x11);
    io_wait();

    /* ICW2: Set vector offsets */
    outb(PIC1_DATA, 0x20);      /* PIC1: IRQ 0-7 -> INT 32-39 */
    io_wait();
    outb(PIC2_DATA, 0x28);      /* PIC2: IRQ 8-15 -> INT 40-47 */
    io_wait();

    /* ICW3: Tell PICs about each other */
    outb(PIC1_DATA, 0x04);      /* PIC1: Slave on IRQ2 (bit 2) */
    io_wait();
    outb(PIC2_DATA, 0x02);      /* PIC2: Cascade identity = 2 */
    io_wait();

    /* ICW4: 8086 mode */
    outb(PIC1_DATA, 0x01);
    io_wait();
    outb(PIC2_DATA, 0x01);
    io_wait();

    /* Restore saved masks (or mask all initially) */
    outb(PIC1_DATA, 0xFF);      /* Mask all IRQs initially */
    outb(PIC2_DATA, 0xFF);

    kprintf("  [OK] PIC initialized (IRQs remapped to 32-47)\n");
}

/*
 * Send End of Interrupt signal to PIC(s)
 */
void pic_send_eoi(uint8_t irq)
{
    /* If IRQ came from slave PIC (IRQ 8-15), send EOI to both */
    if (irq >= 8) {
        outb(PIC2_COMMAND, PIC_EOI);
    }
    outb(PIC1_COMMAND, PIC_EOI);
}

/*
 * Enable a specific IRQ by clearing its mask bit
 */
void pic_enable_irq(uint8_t irq)
{
    uint16_t port;
    uint8_t mask;

    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        /* Slave PIC: also ensure IRQ2 (cascade) is unmasked on master */
        mask = inb(PIC1_DATA);
        if (mask & (1 << 2)) {
            mask &= ~(1 << 2);
            outb(PIC1_DATA, mask);
        }
        port = PIC2_DATA;
        irq -= 8;
    }

    mask = inb(port);
    mask &= ~(1 << irq);
    outb(port, mask);
}

/*
 * Disable a specific IRQ by setting its mask bit
 */
void pic_disable_irq(uint8_t irq)
{
    uint16_t port;
    uint8_t mask;

    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }

    mask = inb(port);
    mask |= (1 << irq);
    outb(port, mask);
}

/*
 * Disable all IRQs
 */
void pic_disable_all(void)
{
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}
