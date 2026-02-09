/*
 * PhantomOS Boot Code - Multiboot2 Entry Point
 * "To Create, Not To Destroy"
 *
 * This file contains:
 * 1. Multiboot2 header for GRUB compatibility
 * 2. 32-bit protected mode entry point
 * 3. Long mode (64-bit) transition
 * 4. Initial page table setup
 * 5. Jump to C kernel (kmain)
 *
 * Boot sequence:
 *   GRUB -> _start (32-bit) -> setup paging -> enable long mode ->
 *   long_mode_entry (64-bit) -> kmain()
 */

/* Multiboot2 constants */
.set MULTIBOOT2_MAGIC,          0xE85250D6
.set MULTIBOOT2_ARCH_I386,      0
.set MULTIBOOT2_HEADER_LENGTH,  multiboot2_header_end - multiboot2_header_start
.set MULTIBOOT2_CHECKSUM,       -(MULTIBOOT2_MAGIC + MULTIBOOT2_ARCH_I386 + MULTIBOOT2_HEADER_LENGTH)

/* Page table constants */
.set PAGE_PRESENT,    (1 << 0)
.set PAGE_WRITABLE,   (1 << 1)
.set PAGE_HUGE,       (1 << 7)    /* 2MB pages */
.set PAGE_FLAGS,      (PAGE_PRESENT | PAGE_WRITABLE)
.set PAGE_FLAGS_HUGE, (PAGE_PRESENT | PAGE_WRITABLE | PAGE_HUGE)

/* Control register bits */
.set CR0_PE,          (1 << 0)    /* Protected Mode Enable */
.set CR0_PG,          (1 << 31)   /* Paging Enable */
.set CR4_PAE,         (1 << 5)    /* Physical Address Extension */
.set EFER_LME,        (1 << 8)    /* Long Mode Enable */
.set EFER_MSR,        0xC0000080  /* EFER MSR number */

/* CPUID feature bits */
.set CPUID_EXTENDED,  0x80000000
.set CPUID_LONG_MODE, 0x80000001
.set CPUID_LM_BIT,    (1 << 29)

/*============================================================================
 * MULTIBOOT2 HEADER - Must be in first 32KB of kernel image
 *============================================================================*/
.section .multiboot2, "a"
.align 8

multiboot2_header_start:
    .long MULTIBOOT2_MAGIC
    .long MULTIBOOT2_ARCH_I386
    .long MULTIBOOT2_HEADER_LENGTH
    .long MULTIBOOT2_CHECKSUM

    /* Framebuffer request tag (type 5) */
    .align 8
    .short 5                        /* Type: framebuffer */
    .short 0                        /* Flags: not optional */
    .long 20                        /* Size: 8 header + 12 payload */
    .long 1024                      /* Width */
    .long 768                       /* Height */
    .long 32                        /* Depth (bpp) */

    /* End tag - required */
    .align 8
    .short 0                        /* Type: end */
    .short 0                        /* Flags */
    .long 8                         /* Size */

multiboot2_header_end:

/*============================================================================
 * BSS SECTION - Uninitialized data
 *============================================================================*/
.section .bss
.align 4096

/* Page tables for initial identity mapping (first 1GB) */
.global pml4_table
pml4_table:
    .skip 4096                      /* Page Map Level 4 */
pdpt_table:
    .skip 4096                      /* Page Directory Pointer Table */
pd_table:
    .skip 4096                      /* Page Directory (512 * 2MB = 1GB) */

/* Kernel stack (16KB) */
.align 16
stack_bottom:
    .skip 16384
.global stack_top
stack_top:

/*============================================================================
 * DATA SECTION - Initialized data
 *============================================================================*/
.section .data

/* Saved multiboot info for passing to kmain */
.global multiboot_info_ptr
multiboot_info_ptr:
    .quad 0                         /* 64-bit pointer */

.global multiboot_magic
multiboot_magic:
    .long 0

/* Buffer for copying multiboot info (max 4KB should be enough) */
.align 8
.global multiboot_info_copy
multiboot_info_copy:
    .skip 4096

/* Error messages for early boot failures */
error_no_cpuid:
    .asciz "ERROR: CPUID not supported"
error_no_long_mode:
    .asciz "ERROR: Long mode not supported"

/*============================================================================
 * TEXT SECTION - Code
 *============================================================================*/
.section .text

/*----------------------------------------------------------------------------
 * 32-bit Entry Point - Called by GRUB
 *
 * At entry:
 *   EAX = Multiboot2 magic (0x36d76289)
 *   EBX = Physical address of multiboot2 info structure
 *   CS  = 32-bit read/execute code segment (base 0)
 *   DS, ES, FS, GS, SS = 32-bit read/write data segment (base 0)
 *   A20 enabled, interrupts disabled, paging disabled
 *----------------------------------------------------------------------------*/
.code32
.global _start
_start:
    /* Disable interrupts */
    cli

    /* DEBUG: Write 'P' to VGA immediately to confirm we reached here */
    movl $0xB8000, %ecx
    movw $0x4F50, (%ecx)            /* 'P' in white on red */
    movw $0x4F48, 2(%ecx)           /* 'H' */
    movw $0x4F41, 4(%ecx)           /* 'A' */
    movw $0x4F4E, 6(%ecx)           /* 'N' */

    /*
     * CRITICAL: Copy multiboot info to safe location BEFORE using stack!
     * GRUB may have placed multiboot info where our stack will go.
     * Copy up to 4KB (enough for typical multiboot info).
     * We do this inline without using call/ret since we have no stack yet.
     *
     * At entry: EAX = magic, EBX = multiboot info pointer
     */

    /* Save magic to EBP (unused register) */
    movl %eax, %ebp                 /* EBP = multiboot magic */

    /* Set up source and destination for copy */
    movl %ebx, %esi                 /* ESI = source (multiboot info from GRUB) */
    movl $multiboot_info_copy, %edi /* EDI = destination (our safe buffer) */

    /* Read size from multiboot info header (first 4 bytes) */
    movl (%esi), %ecx               /* ECX = total_size */
    cmpl $4096, %ecx                /* Clamp to 4KB max */
    jbe 1f
    movl $4096, %ecx
1:
    /* Ensure at least 8 bytes (header size) */
    cmpl $8, %ecx
    jae 2f
    movl $8, %ecx
2:
    /* Copy ECX bytes from ESI to EDI */
    cld                             /* Clear direction flag */
    rep movsb                       /* Copy bytes */

    /* Restore registers for later use:
     * ESI = multiboot magic (from EBP)
     * EDI = pointer to our copied multiboot info */
    movl %ebp, %esi                 /* ESI = multiboot magic */
    movl $multiboot_info_copy, %edi /* EDI = pointer to our copied multiboot info */

    /* Set up 32-bit stack using absolute address
     * Stack is in BSS at a known location relative to kernel base (1MB) */
    movl $0x14f000, %esp            /* stack_top (actual address from linker) */

    /* Verify CPU capabilities */
    call check_cpuid
    /* DEBUG: '1' after CPUID */
    movw $0x2F31, 0xB800A

    call check_long_mode
    /* DEBUG: '2' after long mode check */
    movw $0x2F32, 0xB800C

    /* Set up paging for long mode */
    call setup_page_tables
    /* DEBUG: '3' after page tables */
    movw $0x2F33, 0xB800E

    call enable_paging
    /* DEBUG: '4' after paging enabled */
    movw $0x2F34, 0xB8010

    /* Restore ESI and EDI which were clobbered by the function calls above */
    movl %ebp, %esi                 /* ESI = multiboot magic (saved in EBP) */
    movl $multiboot_info_copy, %edi /* EDI = pointer to our copied multiboot info */

    /* Load 64-bit GDT using position-independent code
     * We use call/pop to get our current address and compute offsets */
    call 1f
1:  popl %eax                       /* EAX = address of label 1 */

    /* Compute GDT base address */
    leal (gdt64_here - 1b)(%eax), %ebx   /* EBX = GDT base address */

    /* Build GDT pointer on stack */
    subl $8, %esp
    movl %ebx, 2(%esp)              /* GDT base address */
    movw $(gdt64_here_end - gdt64_here - 1), (%esp)  /* GDT limit */
    lgdt (%esp)
    addl $8, %esp
    /* DEBUG: '5' after GDT loaded */
    movw $0x2F35, 0xB8012

    /* Compute long_mode_entry address */
    leal (long_mode_entry - 1b)(%eax), %ebx  /* EBX = target address */

    /* Far jump to 64-bit code segment */
    pushl $0x08                     /* Code segment selector */
    pushl %ebx                      /* Target address */
    ljmpl *(%esp)

    /* Should never reach here */
.halt_early:
    hlt
    jmp .halt_early

/* GDT placed inline here for position-independent access */
.align 8
gdt64_here:
    .quad 0x0000000000000000        /* Null descriptor */
    .quad 0x00209A0000000000        /* 64-bit code segment */
    .quad 0x0000920000000000        /* 64-bit data segment */
gdt64_here_end:

/*----------------------------------------------------------------------------
 * Check if CPUID is available
 *----------------------------------------------------------------------------*/
check_cpuid:
    /* Try to flip ID bit (bit 21) in FLAGS register */
    pushfl
    popl %eax
    movl %eax, %ecx             /* Save original FLAGS */
    xorl $(1 << 21), %eax       /* Flip ID bit */
    pushl %eax
    popfl
    pushfl
    popl %eax
    pushl %ecx
    popfl                       /* Restore original FLAGS */

    cmpl %ecx, %eax             /* If they're equal, CPUID isn't supported */
    je .no_cpuid
    ret

.no_cpuid:
    /* Print error to VGA using absolute addresses */
    movl $0xB8000, %edi
    movl $error_msg_cpuid, %esi
    call print_error_32
    jmp .halt32

/*----------------------------------------------------------------------------
 * Check if Long Mode (64-bit) is supported
 *----------------------------------------------------------------------------*/
check_long_mode:
    /* Check if extended CPUID is available */
    movl $CPUID_EXTENDED, %eax
    cpuid
    cmpl $CPUID_LONG_MODE, %eax
    jb .no_long_mode

    /* Check for long mode support */
    movl $CPUID_LONG_MODE, %eax
    cpuid
    testl $CPUID_LM_BIT, %edx
    jz .no_long_mode
    ret

.no_long_mode:
    movl $0xB8000, %edi
    movl $error_msg_lm, %esi
    call print_error_32
    jmp .halt32

/*----------------------------------------------------------------------------
 * Print error message to VGA text buffer (32-bit mode)
 * Input: EDI = VGA buffer address, ESI = string pointer
 *----------------------------------------------------------------------------*/
print_error_32:
    movb $0x4F, %ah             /* White on red */
.print_loop32:
    lodsb                       /* Load byte from [ESI] into AL */
    testb %al, %al
    jz .print_done32
    stosw                       /* Store AX (char + attr) to [EDI] */
    jmp .print_loop32
.print_done32:
    ret

.halt32:
    cli
    hlt
    jmp .halt32

/* Error messages in .text section for 32-bit accessibility */
error_msg_cpuid:
    .asciz "BOOT ERROR: CPUID not supported"
error_msg_lm:
    .asciz "BOOT ERROR: CPU does not support 64-bit long mode"

/*----------------------------------------------------------------------------
 * Set up initial page tables for identity mapping
 * Maps first 1GB using 2MB pages (512 entries * 2MB = 1GB)
 *
 * Uses absolute addresses since kernel is loaded at 1MB
 *----------------------------------------------------------------------------*/
setup_page_tables:
    /* Page table addresses (in BSS, actual addresses from linker):
     * pml4_table  = 0x148000
     * pdpt_table  = 0x149000
     * pd_table    = 0x14a000
     */

    /* Zero out page tables */
    movl $0x148000, %edi        /* pml4_table */
    xorl %eax, %eax
    movl $(3 * 4096 / 4), %ecx  /* 3 tables * 4096 bytes / 4 bytes */
    rep stosl

    /* PML4[0] -> PDPT */
    movl $0x149000, %eax        /* pdpt_table */
    orl $PAGE_FLAGS, %eax
    movl %eax, 0x148000         /* pml4_table[0] */

    /* PDPT[0] -> PD */
    movl $0x14a000, %eax        /* pd_table */
    orl $PAGE_FLAGS, %eax
    movl %eax, 0x149000         /* pdpt_table[0] */

    /* Fill PD with 2MB page entries (identity map first 1GB) */
    movl $0x14a000, %edi        /* pd_table */
    movl $PAGE_FLAGS_HUGE, %eax /* Start at physical address 0 */
    movl $512, %ecx             /* 512 entries */

.fill_pd_loop:
    movl %eax, (%edi)
    movl $0, 4(%edi)            /* High 32 bits = 0 */
    addl $0x200000, %eax        /* Next 2MB */
    addl $8, %edi               /* Next entry (8 bytes) */
    decl %ecx
    jnz .fill_pd_loop

    ret

/*----------------------------------------------------------------------------
 * Enable paging and switch to long mode
 *----------------------------------------------------------------------------*/
enable_paging:
    /* Load PML4 address into CR3 */
    movl $0x148000, %eax        /* pml4_table (actual address from linker) */
    movl %eax, %cr3

    /* Enable PAE (Physical Address Extension) in CR4 */
    movl %cr4, %eax
    orl $CR4_PAE, %eax
    movl %eax, %cr4

    /* Enable Long Mode in EFER MSR */
    movl $EFER_MSR, %ecx
    rdmsr
    orl $EFER_LME, %eax
    wrmsr

    /* Enable paging in CR0 (this activates long mode) */
    movl %cr0, %eax
    orl $(CR0_PG | CR0_PE), %eax
    movl %eax, %cr0

    ret

/*----------------------------------------------------------------------------
 * 64-bit Long Mode Entry Point
 *----------------------------------------------------------------------------*/
.code64
.global long_mode_entry
long_mode_entry:
    /* DEBUG: '6' - reached 64-bit mode! */
    movw $0x2F36, 0xB8014

    /* Reload all data segment registers with 64-bit data selector */
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss

    /* Set up 64-bit stack */
    movq $0x14f000, %rsp        /* stack_top (actual address from linker) */

    /* Now we can safely store multiboot info using RIP-relative addressing */
    /* ESI = magic (from 32-bit code), EDI = info pointer (from 32-bit code) */
    movl %esi, multiboot_magic(%rip)
    movq %rdi, multiboot_info_ptr(%rip)    /* Zero-extends EDI to RDI */

    /* Clear direction flag */
    cld

    /* Clear BSS section
     * BSS is from 0x105000 to ~0x10C000 (includes page tables and stack)
     * But we already set up page tables, so only clear non-page-table BSS
     * Actually, the page tables and stack are already initialized, so skip BSS clear
     * or be careful about what we clear */

    /* Prepare arguments for kmain */
    /* First argument (rdi): pointer to multiboot info */
    movq multiboot_info_ptr(%rip), %rdi

    /* Second argument (rsi): multiboot magic number */
    movl multiboot_magic(%rip), %esi

    /* Call the C kernel entry point */
    call kmain

    /* If kmain returns, halt the system */
.halt_loop:
    cli
    hlt
    jmp .halt_loop

/*----------------------------------------------------------------------------
 * External references
 *----------------------------------------------------------------------------*/
.extern kmain
