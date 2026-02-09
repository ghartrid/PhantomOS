/*
 * PhantomOS - Global Descriptor Table for 64-bit Long Mode
 * "To Create, Not To Destroy"
 *
 * The GDT defines memory segments for protected and long mode.
 * In 64-bit mode, segmentation is mostly disabled (flat memory model),
 * but we still need valid code and data segment descriptors.
 */

.section .rodata
.align 16

/*
 * 64-bit GDT structure:
 * - Entry 0: Null descriptor (required)
 * - Entry 1: 64-bit code segment (selector 0x08)
 * - Entry 2: 64-bit data segment (selector 0x10)
 */
.global gdt64
gdt64:
    /* Null descriptor - required first entry */
    .quad 0x0000000000000000

    /* 64-bit Code Segment (selector 0x08)
     * Base: 0, Limit: ignored in long mode
     * Access: Present, Ring 0, Code, Execute/Read
     * Flags: Long mode (L=1), not 32-bit (D=0)
     *
     * Byte breakdown:
     *   Limit 0:15  = 0x0000 (ignored)
     *   Base 0:15   = 0x0000
     *   Base 16:23  = 0x00
     *   Access      = 0x9A (Present=1, DPL=0, S=1, Type=1010 Execute/Read)
     *   Flags/Limit = 0x20 (G=0, D=0, L=1, AVL=0, Limit 16:19=0)
     *   Base 24:31  = 0x00
     */
gdt64_code:
    .quad 0x00209A0000000000

    /* 64-bit Data Segment (selector 0x10)
     * Base: 0, Limit: ignored in long mode
     * Access: Present, Ring 0, Data, Read/Write
     *
     * Byte breakdown:
     *   Limit 0:15  = 0x0000 (ignored)
     *   Base 0:15   = 0x0000
     *   Base 16:23  = 0x00
     *   Access      = 0x92 (Present=1, DPL=0, S=1, Type=0010 Read/Write)
     *   Flags/Limit = 0x00 (G=0, D=0, L=0, AVL=0, Limit 16:19=0)
     *   Base 24:31  = 0x00
     */
gdt64_data:
    .quad 0x0000920000000000

gdt64_end:

/*
 * GDT Pointer structure for LGDT instruction
 * - 16-bit limit (size - 1)
 * - 64-bit base address
 */
.global gdt64_pointer
gdt64_pointer:
    .word gdt64_end - gdt64 - 1     /* Limit: size of GDT minus 1 */
    .quad gdt64                      /* Base: address of GDT */

/*
 * Segment selector constants (for use in C code)
 */
.global GDT_KERNEL_CODE
.global GDT_KERNEL_DATA
.set GDT_KERNEL_CODE, 0x08
.set GDT_KERNEL_DATA, 0x10
