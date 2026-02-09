/*
 * PhantomOS Interrupt Descriptor Table Implementation
 * "To Create, Not To Destroy"
 */

#include "idt.h"
#include <stddef.h>

/* External functions */
extern int kprintf(const char *fmt, ...);
extern void kpanic(const char *msg);

/* IDT and IDT pointer */
static struct idt_entry idt[256];
static struct idt_ptr idtp;

/* Interrupt handlers table */
static interrupt_handler_t interrupt_handlers[256];

/* External assembly interrupt stubs (defined in interrupts.S) */
extern void isr0(void);
extern void isr1(void);
extern void isr2(void);
extern void isr3(void);
extern void isr4(void);
extern void isr5(void);
extern void isr6(void);
extern void isr7(void);
extern void isr8(void);
extern void isr9(void);
extern void isr10(void);
extern void isr11(void);
extern void isr12(void);
extern void isr13(void);
extern void isr14(void);
extern void isr15(void);
extern void isr16(void);
extern void isr17(void);
extern void isr18(void);
extern void isr19(void);
extern void isr20(void);
extern void isr21(void);
extern void isr22(void);
extern void isr23(void);
extern void isr24(void);
extern void isr25(void);
extern void isr26(void);
extern void isr27(void);
extern void isr28(void);
extern void isr29(void);
extern void isr30(void);
extern void isr31(void);

/* IRQ stubs */
extern void irq0(void);
extern void irq1(void);
extern void irq2(void);
extern void irq3(void);
extern void irq4(void);
extern void irq5(void);
extern void irq6(void);
extern void irq7(void);
extern void irq8(void);
extern void irq9(void);
extern void irq10(void);
extern void irq11(void);
extern void irq12(void);
extern void irq13(void);
extern void irq14(void);
extern void irq15(void);

/* Load IDT (defined in assembly) */
extern void idt_load(struct idt_ptr *ptr);

/* Exception names for debugging */
static const char *exception_names[] = {
    "Division Error",
    "Debug",
    "Non-Maskable Interrupt",
    "Breakpoint",
    "Overflow",
    "Bound Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack-Segment Fault",
    "General Protection Fault",
    "Page Fault",
    "Reserved",
    "x87 FPU Error",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating-Point",
    "Virtualization Exception",
    "Control Protection Exception",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved"
};

/*
 * Set an IDT gate
 */
void idt_set_gate(uint8_t num, uint64_t handler, uint16_t selector, uint8_t type_attr)
{
    idt[num].offset_low = handler & 0xFFFF;
    idt[num].offset_mid = (handler >> 16) & 0xFFFF;
    idt[num].offset_high = (handler >> 32) & 0xFFFFFFFF;
    idt[num].selector = selector;
    idt[num].ist = 0;           /* Don't use IST */
    idt[num].type_attr = type_attr;
    idt[num].reserved = 0;
}

/*
 * Register a C interrupt handler
 */
void register_interrupt_handler(uint8_t num, interrupt_handler_t handler)
{
    interrupt_handlers[num] = handler;
}

/*
 * Default exception handler
 */
static void default_exception_handler(struct interrupt_frame *frame)
{
    const char *name = "Unknown Exception";
    if (frame->int_no < 32) {
        name = exception_names[frame->int_no];
    }

    kprintf("\n");
    kprintf("=== EXCEPTION: %s (int %lu) ===\n", name, frame->int_no);
    kprintf("Error Code: 0x%016lx\n", frame->error_code);
    kprintf("\n");
    kprintf("Registers:\n");
    kprintf("  RAX=%016lx  RBX=%016lx  RCX=%016lx\n", frame->rax, frame->rbx, frame->rcx);
    kprintf("  RDX=%016lx  RSI=%016lx  RDI=%016lx\n", frame->rdx, frame->rsi, frame->rdi);
    kprintf("  RBP=%016lx  RSP=%016lx  RIP=%016lx\n", frame->rbp, frame->rsp, frame->rip);
    kprintf("  R8 =%016lx  R9 =%016lx  R10=%016lx\n", frame->r8, frame->r9, frame->r10);
    kprintf("  R11=%016lx  R12=%016lx  R13=%016lx\n", frame->r11, frame->r12, frame->r13);
    kprintf("  R14=%016lx  R15=%016lx\n", frame->r14, frame->r15);
    kprintf("  CS=%04lx  SS=%04lx  RFLAGS=%016lx\n", frame->cs, frame->ss, frame->rflags);

    /* Special handling for page faults */
    if (frame->int_no == INT_PAGE_FAULT) {
        uint64_t cr2;
        __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
        kprintf("\nPage Fault Details:\n");
        kprintf("  Faulting Address (CR2): 0x%016lx\n", cr2);
        kprintf("  Error: %s, %s, %s\n",
            (frame->error_code & 1) ? "Protection violation" : "Page not present",
            (frame->error_code & 2) ? "Write" : "Read",
            (frame->error_code & 4) ? "User mode" : "Kernel mode");
    }

    kpanic("Unhandled exception");
}

/*
 * C interrupt dispatcher - called from assembly stubs
 */
void interrupt_handler(struct interrupt_frame *frame)
{
    /* Call registered handler if present */
    if (interrupt_handlers[frame->int_no]) {
        interrupt_handlers[frame->int_no](frame);
    } else if (frame->int_no < 32) {
        /* CPU exception without handler */
        default_exception_handler(frame);
    }
    /* IRQs without handlers are silently ignored after EOI */
}

/*
 * Initialize the IDT
 */
void idt_init(void)
{
    /* Clear IDT and handlers */
    for (int i = 0; i < 256; i++) {
        idt_set_gate(i, 0, 0, 0);
        interrupt_handlers[i] = NULL;
    }

    /* Set up exception handlers (ISRs 0-31) */
    idt_set_gate(0, (uint64_t)isr0, 0x08, IDT_GATE_INTERRUPT);
    idt_set_gate(1, (uint64_t)isr1, 0x08, IDT_GATE_INTERRUPT);
    idt_set_gate(2, (uint64_t)isr2, 0x08, IDT_GATE_INTERRUPT);
    idt_set_gate(3, (uint64_t)isr3, 0x08, IDT_GATE_TRAP);      /* Breakpoint - trap */
    idt_set_gate(4, (uint64_t)isr4, 0x08, IDT_GATE_INTERRUPT);
    idt_set_gate(5, (uint64_t)isr5, 0x08, IDT_GATE_INTERRUPT);
    idt_set_gate(6, (uint64_t)isr6, 0x08, IDT_GATE_INTERRUPT);
    idt_set_gate(7, (uint64_t)isr7, 0x08, IDT_GATE_INTERRUPT);
    idt_set_gate(8, (uint64_t)isr8, 0x08, IDT_GATE_INTERRUPT);
    idt_set_gate(9, (uint64_t)isr9, 0x08, IDT_GATE_INTERRUPT);
    idt_set_gate(10, (uint64_t)isr10, 0x08, IDT_GATE_INTERRUPT);
    idt_set_gate(11, (uint64_t)isr11, 0x08, IDT_GATE_INTERRUPT);
    idt_set_gate(12, (uint64_t)isr12, 0x08, IDT_GATE_INTERRUPT);
    idt_set_gate(13, (uint64_t)isr13, 0x08, IDT_GATE_INTERRUPT);
    idt_set_gate(14, (uint64_t)isr14, 0x08, IDT_GATE_INTERRUPT);
    idt_set_gate(15, (uint64_t)isr15, 0x08, IDT_GATE_INTERRUPT);
    idt_set_gate(16, (uint64_t)isr16, 0x08, IDT_GATE_INTERRUPT);
    idt_set_gate(17, (uint64_t)isr17, 0x08, IDT_GATE_INTERRUPT);
    idt_set_gate(18, (uint64_t)isr18, 0x08, IDT_GATE_INTERRUPT);
    idt_set_gate(19, (uint64_t)isr19, 0x08, IDT_GATE_INTERRUPT);
    idt_set_gate(20, (uint64_t)isr20, 0x08, IDT_GATE_INTERRUPT);
    idt_set_gate(21, (uint64_t)isr21, 0x08, IDT_GATE_INTERRUPT);
    idt_set_gate(22, (uint64_t)isr22, 0x08, IDT_GATE_INTERRUPT);
    idt_set_gate(23, (uint64_t)isr23, 0x08, IDT_GATE_INTERRUPT);
    idt_set_gate(24, (uint64_t)isr24, 0x08, IDT_GATE_INTERRUPT);
    idt_set_gate(25, (uint64_t)isr25, 0x08, IDT_GATE_INTERRUPT);
    idt_set_gate(26, (uint64_t)isr26, 0x08, IDT_GATE_INTERRUPT);
    idt_set_gate(27, (uint64_t)isr27, 0x08, IDT_GATE_INTERRUPT);
    idt_set_gate(28, (uint64_t)isr28, 0x08, IDT_GATE_INTERRUPT);
    idt_set_gate(29, (uint64_t)isr29, 0x08, IDT_GATE_INTERRUPT);
    idt_set_gate(30, (uint64_t)isr30, 0x08, IDT_GATE_INTERRUPT);
    idt_set_gate(31, (uint64_t)isr31, 0x08, IDT_GATE_INTERRUPT);

    /* Set up IRQ handlers (32-47) */
    idt_set_gate(32, (uint64_t)irq0, 0x08, IDT_GATE_INTERRUPT);
    idt_set_gate(33, (uint64_t)irq1, 0x08, IDT_GATE_INTERRUPT);
    idt_set_gate(34, (uint64_t)irq2, 0x08, IDT_GATE_INTERRUPT);
    idt_set_gate(35, (uint64_t)irq3, 0x08, IDT_GATE_INTERRUPT);
    idt_set_gate(36, (uint64_t)irq4, 0x08, IDT_GATE_INTERRUPT);
    idt_set_gate(37, (uint64_t)irq5, 0x08, IDT_GATE_INTERRUPT);
    idt_set_gate(38, (uint64_t)irq6, 0x08, IDT_GATE_INTERRUPT);
    idt_set_gate(39, (uint64_t)irq7, 0x08, IDT_GATE_INTERRUPT);
    idt_set_gate(40, (uint64_t)irq8, 0x08, IDT_GATE_INTERRUPT);
    idt_set_gate(41, (uint64_t)irq9, 0x08, IDT_GATE_INTERRUPT);
    idt_set_gate(42, (uint64_t)irq10, 0x08, IDT_GATE_INTERRUPT);
    idt_set_gate(43, (uint64_t)irq11, 0x08, IDT_GATE_INTERRUPT);
    idt_set_gate(44, (uint64_t)irq12, 0x08, IDT_GATE_INTERRUPT);
    idt_set_gate(45, (uint64_t)irq13, 0x08, IDT_GATE_INTERRUPT);
    idt_set_gate(46, (uint64_t)irq14, 0x08, IDT_GATE_INTERRUPT);
    idt_set_gate(47, (uint64_t)irq15, 0x08, IDT_GATE_INTERRUPT);

    /* Set up IDT pointer */
    idtp.limit = sizeof(idt) - 1;
    idtp.base = (uint64_t)&idt;

    /* Load IDT */
    idt_load(&idtp);

    kprintf("  [OK] IDT initialized (256 entries)\n");
}
