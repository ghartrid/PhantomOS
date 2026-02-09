/*
 * PhantomOS Context Switch
 * "To Create, Not To Destroy"
 *
 * Assembly routines for saving and restoring process context.
 */

.code64
.section .text

/*============================================================================
 * context_switch - Switch from one process to another
 *
 * void context_switch(struct cpu_context *old_ctx, struct cpu_context *new_ctx)
 *
 * Arguments:
 *   RDI = pointer to old process context (save here)
 *   RSI = pointer to new process context (load from here)
 *
 * struct cpu_context layout (offsets):
 *   0x00: r15    0x08: r14    0x10: r13    0x18: r12
 *   0x20: rbp    0x28: rbx    0x30: r11    0x38: r10
 *   0x40: r9     0x48: r8     0x50: rax    0x58: rcx
 *   0x60: rdx    0x68: rsi    0x70: rdi    0x78: rip
 *   0x80: rflags 0x88: rsp
 *============================================================================*/
.global context_switch
.type context_switch, @function
context_switch:
    /* Save callee-saved registers to old context */
    movq %r15, 0x00(%rdi)
    movq %r14, 0x08(%rdi)
    movq %r13, 0x10(%rdi)
    movq %r12, 0x18(%rdi)
    movq %rbp, 0x20(%rdi)
    movq %rbx, 0x28(%rdi)

    /* Save caller-saved registers (for complete state) */
    movq %r11, 0x30(%rdi)
    movq %r10, 0x38(%rdi)
    movq %r9,  0x40(%rdi)
    movq %r8,  0x48(%rdi)
    movq %rax, 0x50(%rdi)
    movq %rcx, 0x58(%rdi)
    movq %rdx, 0x60(%rdi)
    /* RSI is the new_ctx pointer, save it last */
    /* RDI is the old_ctx pointer, save it last */

    /* Save return address as RIP */
    movq (%rsp), %rax           /* Return address is on stack */
    movq %rax, 0x78(%rdi)

    /* Save RFLAGS */
    pushfq
    popq %rax
    movq %rax, 0x80(%rdi)

    /* Save RSP (after the return address) */
    leaq 8(%rsp), %rax          /* RSP after return */
    movq %rax, 0x88(%rdi)

    /* Now save RSI and RDI */
    movq %rsi, 0x68(%rdi)
    movq %rdi, 0x70(%rdi)

    /* ---- Load new context ---- */

    /* Load RSP first */
    movq 0x88(%rsi), %rsp

    /* Load RFLAGS */
    movq 0x80(%rsi), %rax
    pushq %rax
    popfq

    /* Load callee-saved registers */
    movq 0x00(%rsi), %r15
    movq 0x08(%rsi), %r14
    movq 0x10(%rsi), %r13
    movq 0x18(%rsi), %r12
    movq 0x20(%rsi), %rbp
    movq 0x28(%rsi), %rbx

    /* Load caller-saved registers */
    movq 0x30(%rsi), %r11
    movq 0x38(%rsi), %r10
    movq 0x40(%rsi), %r9
    movq 0x48(%rsi), %r8
    movq 0x50(%rsi), %rax
    movq 0x58(%rsi), %rcx
    movq 0x60(%rsi), %rdx

    /* Load RIP - push it so ret will jump there */
    movq 0x78(%rsi), %rdi       /* Temporarily use RDI */
    pushq %rdi                  /* Push return address */

    /* Load RDI and RSI last */
    movq 0x70(%rsi), %rdi
    movq 0x68(%rsi), %rsi

    /* Return to new process */
    ret

/*============================================================================
 * context_start - Start a new process (first time)
 *
 * void context_start(struct cpu_context *ctx)
 *
 * This is used to start a newly created process.
 * It loads the context and jumps to the entry point.
 * Never returns.
 *
 * The context should be set up so that:
 *   - RIP = entry point function
 *   - RDI = argument to entry function
 *   - RSP = top of stack
 *============================================================================*/
.global context_start
.type context_start, @function
context_start:
    /* RDI contains pointer to context to load */
    movq %rdi, %rsi             /* Move to RSI for consistency */

    /* Load RSP */
    movq 0x88(%rsi), %rsp

    /* Load RFLAGS (enable interrupts) */
    movq 0x80(%rsi), %rax
    pushq %rax
    popfq

    /* Load all registers */
    movq 0x00(%rsi), %r15
    movq 0x08(%rsi), %r14
    movq 0x10(%rsi), %r13
    movq 0x18(%rsi), %r12
    movq 0x20(%rsi), %rbp
    movq 0x28(%rsi), %rbx
    movq 0x30(%rsi), %r11
    movq 0x38(%rsi), %r10
    movq 0x40(%rsi), %r9
    movq 0x48(%rsi), %r8
    movq 0x50(%rsi), %rax
    movq 0x58(%rsi), %rcx
    movq 0x60(%rsi), %rdx

    /* Get entry point (RIP) */
    movq 0x78(%rsi), %r11       /* Use R11 temporarily */

    /* Load argument (RDI) */
    movq 0x70(%rsi), %rdi

    /* Load RSI last since we were using it */
    movq 0x68(%rsi), %rsi

    /* Jump to entry point */
    jmpq *%r11

/*============================================================================
 * process_entry_wrapper - Wrapper for process entry
 *
 * This is set as the initial RIP for new processes.
 * It calls the actual entry function and then calls process_exit.
 *
 * Stack layout when called:
 *   RSP+0: entry function pointer
 *   RSP+8: argument pointer
 *============================================================================*/
.global process_entry_wrapper
.type process_entry_wrapper, @function
process_entry_wrapper:
    /* RDI = argument (set by context_start) */
    /* The actual entry function address is in R12 (set up by process_create) */

    /* Call the entry function */
    callq *%r12

    /* Entry function returned - call process_exit with return value */
    movq %rax, %rdi             /* Pass return value as exit code */
    callq process_exit

    /* Should never reach here */
1:  cli
    hlt
    jmp 1b
