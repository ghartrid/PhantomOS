/*
 * PhantomOS Interrupt Stubs (x86_64)
 * "To Create, Not To Destroy"
 *
 * Assembly stubs for interrupt/exception handling.
 * These save CPU state, call C handler, restore state, and return.
 */

.section .text
.code64

/*============================================================================
 * Common interrupt stub
 * Saves all registers, calls C handler, restores registers, returns
 *============================================================================*/
.extern interrupt_handler

interrupt_common_stub:
    /* Save all general-purpose registers */
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    /* Save segment registers (already correct for kernel) */
    /* Pass pointer to interrupt frame as first argument */
    movq %rsp, %rdi

    /* Align stack to 16 bytes (required by System V ABI) */
    movq %rsp, %rbp
    andq $-16, %rsp

    /* Call C interrupt handler */
    call interrupt_handler

    /* Restore stack */
    movq %rbp, %rsp

    /* Restore all general-purpose registers */
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax

    /* Remove interrupt number and error code from stack */
    addq $16, %rsp

    /* Return from interrupt */
    iretq

/*============================================================================
 * Load IDT
 *============================================================================*/
.global idt_load
idt_load:
    lidt (%rdi)
    ret

/*============================================================================
 * ISR Stubs (Exceptions 0-31)
 * Some exceptions push an error code, others don't.
 * For consistency, we push a dummy error code (0) for those that don't.
 *============================================================================*/

/* Macro for ISR without error code */
.macro ISR_NOERR num
.global isr\num
isr\num:
    pushq $0            /* Dummy error code */
    pushq $\num         /* Interrupt number */
    jmp interrupt_common_stub
.endm

/* Macro for ISR with error code (pushed by CPU) */
.macro ISR_ERR num
.global isr\num
isr\num:
    /* Error code already pushed by CPU */
    pushq $\num         /* Interrupt number */
    jmp interrupt_common_stub
.endm

/* Exception handlers */
ISR_NOERR 0     /* Divide Error */
ISR_NOERR 1     /* Debug */
ISR_NOERR 2     /* NMI */
ISR_NOERR 3     /* Breakpoint */
ISR_NOERR 4     /* Overflow */
ISR_NOERR 5     /* Bound Range */
ISR_NOERR 6     /* Invalid Opcode */
ISR_NOERR 7     /* Device Not Available */
ISR_ERR   8     /* Double Fault (has error code) */
ISR_NOERR 9     /* Coprocessor Segment Overrun */
ISR_ERR   10    /* Invalid TSS (has error code) */
ISR_ERR   11    /* Segment Not Present (has error code) */
ISR_ERR   12    /* Stack-Segment Fault (has error code) */
ISR_ERR   13    /* General Protection Fault (has error code) */
ISR_ERR   14    /* Page Fault (has error code) */
ISR_NOERR 15    /* Reserved */
ISR_NOERR 16    /* x87 FPU Error */
ISR_ERR   17    /* Alignment Check (has error code) */
ISR_NOERR 18    /* Machine Check */
ISR_NOERR 19    /* SIMD Floating-Point */
ISR_NOERR 20    /* Virtualization Exception */
ISR_ERR   21    /* Control Protection (has error code) */
ISR_NOERR 22    /* Reserved */
ISR_NOERR 23    /* Reserved */
ISR_NOERR 24    /* Reserved */
ISR_NOERR 25    /* Reserved */
ISR_NOERR 26    /* Reserved */
ISR_NOERR 27    /* Reserved */
ISR_NOERR 28    /* Reserved */
ISR_NOERR 29    /* Reserved */
ISR_NOERR 30    /* Reserved */
ISR_NOERR 31    /* Reserved */

/*============================================================================
 * IRQ Stubs (Hardware Interrupts 0-15, mapped to vectors 32-47)
 *============================================================================*/

/* Macro for IRQ handler */
.macro IRQ num, vector
.global irq\num
irq\num:
    pushq $0            /* Dummy error code */
    pushq $\vector      /* Interrupt number */
    jmp interrupt_common_stub
.endm

/* IRQ handlers (remapped to vectors 32-47) */
IRQ 0, 32       /* Timer */
IRQ 1, 33       /* Keyboard */
IRQ 2, 34       /* Cascade */
IRQ 3, 35       /* COM2 */
IRQ 4, 36       /* COM1 */
IRQ 5, 37       /* LPT2 */
IRQ 6, 38       /* Floppy */
IRQ 7, 39       /* LPT1 / Spurious */
IRQ 8, 40       /* RTC */
IRQ 9, 41       /* Free */
IRQ 10, 42      /* Free */
IRQ 11, 43      /* Free */
IRQ 12, 44      /* Mouse */
IRQ 13, 45      /* FPU */
IRQ 14, 46      /* Primary ATA */
IRQ 15, 47      /* Secondary ATA */
