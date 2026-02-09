/*
 * PhantomOS Interrupt Descriptor Table (IDT)
 * "To Create, Not To Destroy"
 *
 * x86_64 IDT for handling exceptions and hardware interrupts.
 */

#ifndef _IDT_H
#define _IDT_H

#include <stdint.h>

/*============================================================================
 * IDT Entry Structure (x86_64)
 *
 * Each IDT entry is 16 bytes in 64-bit mode:
 *   - offset_low:  bits 0-15 of handler address
 *   - selector:    code segment selector
 *   - ist:         interrupt stack table index (0 = don't switch stacks)
 *   - type_attr:   gate type and attributes
 *   - offset_mid:  bits 16-31 of handler address
 *   - offset_high: bits 32-63 of handler address
 *   - reserved:    must be zero
 *============================================================================*/

/* IDT gate types */
#define IDT_GATE_INTERRUPT  0x8E    /* P=1, DPL=0, type=interrupt gate (0xE) */
#define IDT_GATE_TRAP       0x8F    /* P=1, DPL=0, type=trap gate (0xF) */
#define IDT_GATE_USER       0xEE    /* P=1, DPL=3, type=interrupt gate */

/* IDT entry structure */
struct idt_entry {
    uint16_t offset_low;        /* Handler address bits 0-15 */
    uint16_t selector;          /* Code segment selector */
    uint8_t  ist;               /* Interrupt Stack Table offset */
    uint8_t  type_attr;         /* Type and attributes */
    uint16_t offset_mid;        /* Handler address bits 16-31 */
    uint32_t offset_high;       /* Handler address bits 32-63 */
    uint32_t reserved;          /* Reserved, must be zero */
} __attribute__((packed));

/* IDT pointer structure for LIDT instruction */
struct idt_ptr {
    uint16_t limit;             /* Size of IDT minus 1 */
    uint64_t base;              /* Base address of IDT */
} __attribute__((packed));

/*============================================================================
 * Interrupt Numbers
 *============================================================================*/

/* CPU Exceptions (0-31) */
#define INT_DIVIDE_ERROR        0
#define INT_DEBUG               1
#define INT_NMI                 2
#define INT_BREAKPOINT          3
#define INT_OVERFLOW            4
#define INT_BOUND_RANGE         5
#define INT_INVALID_OPCODE      6
#define INT_DEVICE_NOT_AVAIL    7
#define INT_DOUBLE_FAULT        8
#define INT_COPROCESSOR_SEG     9   /* Reserved */
#define INT_INVALID_TSS         10
#define INT_SEGMENT_NOT_PRESENT 11
#define INT_STACK_SEGMENT       12
#define INT_GENERAL_PROTECTION  13
#define INT_PAGE_FAULT          14
#define INT_RESERVED_15         15
#define INT_X87_FPU             16
#define INT_ALIGNMENT_CHECK     17
#define INT_MACHINE_CHECK       18
#define INT_SIMD_FPU            19
#define INT_VIRTUALIZATION      20
#define INT_CONTROL_PROTECTION  21
/* 22-31 reserved */

/* Hardware IRQs (remapped to 32-47) */
#define IRQ_BASE                32
#define IRQ_TIMER               (IRQ_BASE + 0)   /* IRQ0: PIT timer */
#define IRQ_KEYBOARD            (IRQ_BASE + 1)   /* IRQ1: Keyboard */
#define IRQ_CASCADE             (IRQ_BASE + 2)   /* IRQ2: Cascade (never raised) */
#define IRQ_COM2                (IRQ_BASE + 3)   /* IRQ3: COM2 */
#define IRQ_COM1                (IRQ_BASE + 4)   /* IRQ4: COM1 */
#define IRQ_LPT2                (IRQ_BASE + 5)   /* IRQ5: LPT2 */
#define IRQ_FLOPPY              (IRQ_BASE + 6)   /* IRQ6: Floppy */
#define IRQ_LPT1                (IRQ_BASE + 7)   /* IRQ7: LPT1 / spurious */
#define IRQ_RTC                 (IRQ_BASE + 8)   /* IRQ8: RTC */
#define IRQ_FREE1               (IRQ_BASE + 9)   /* IRQ9: Free */
#define IRQ_FREE2               (IRQ_BASE + 10)  /* IRQ10: Free */
#define IRQ_FREE3               (IRQ_BASE + 11)  /* IRQ11: Free */
#define IRQ_MOUSE               (IRQ_BASE + 12)  /* IRQ12: PS/2 Mouse */
#define IRQ_FPU                 (IRQ_BASE + 13)  /* IRQ13: FPU */
#define IRQ_PRIMARY_ATA         (IRQ_BASE + 14)  /* IRQ14: Primary ATA */
#define IRQ_SECONDARY_ATA       (IRQ_BASE + 15)  /* IRQ15: Secondary ATA */

/* Software interrupts */
#define INT_SYSCALL             128              /* System call interrupt */

/*============================================================================
 * Interrupt Stack Frame
 *
 * Pushed by CPU on interrupt/exception:
 *   - SS, RSP (if privilege change)
 *   - RFLAGS
 *   - CS, RIP
 *   - Error code (for some exceptions)
 *
 * Our stub pushes additional registers for the C handler.
 *============================================================================*/

struct interrupt_frame {
    /* Pushed by our stub */
    uint64_t r15, r14, r13, r12;
    uint64_t r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;

    /* Interrupt number and error code */
    uint64_t int_no;
    uint64_t error_code;

    /* Pushed by CPU */
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} __attribute__((packed));

/*============================================================================
 * Function Declarations
 *============================================================================*/

/* Initialize IDT */
void idt_init(void);

/* Set an IDT entry */
void idt_set_gate(uint8_t num, uint64_t handler, uint16_t selector, uint8_t type_attr);

/* Enable/disable interrupts */
static inline void sti(void) { __asm__ volatile ("sti"); }
static inline void cli(void) { __asm__ volatile ("cli"); }

/* Get/set interrupt flag */
static inline int interrupts_enabled(void) {
    uint64_t flags;
    __asm__ volatile ("pushfq; pop %0" : "=r"(flags));
    return (flags >> 9) & 1;
}

/* Register an interrupt handler */
typedef void (*interrupt_handler_t)(struct interrupt_frame *frame);
void register_interrupt_handler(uint8_t num, interrupt_handler_t handler);

#endif /* _IDT_H */
