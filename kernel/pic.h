/*
 * PhantomOS PIC (8259) Driver
 * "To Create, Not To Destroy"
 *
 * Programmable Interrupt Controller for legacy IRQ handling.
 */

#ifndef _PIC_H
#define _PIC_H

#include <stdint.h>

/* PIC I/O ports */
#define PIC1_COMMAND    0x20
#define PIC1_DATA       0x21
#define PIC2_COMMAND    0xA0
#define PIC2_DATA       0xA1

/* PIC commands */
#define PIC_EOI         0x20    /* End of interrupt */

/* Initialize PICs and remap IRQs */
void pic_init(void);

/* Send End of Interrupt signal */
void pic_send_eoi(uint8_t irq);

/* Enable/disable specific IRQ */
void pic_enable_irq(uint8_t irq);
void pic_disable_irq(uint8_t irq);

/* Disable all IRQs (mask all) */
void pic_disable_all(void);

#endif /* _PIC_H */
