/*
 * PhantomOS Kernel Linker Script
 * "To Create, Not To Destroy"
 *
 * Memory Layout:
 *   0x00100000 (1MB) - Kernel start (standard x86 location)
 *
 *   .multiboot2  - Multiboot2 header (must be in first 32KB)
 *   .text        - Kernel code
 *   .rodata      - Read-only data
 *   .data        - Initialized data
 *   .bss         - Uninitialized data (zeroed at boot)
 *
 * The kernel is loaded at 1MB because:
 *   - 0x00000000 - 0x000FFFFF: Reserved (real mode IVT, BDA, legacy areas)
 *   - 0x00100000+: Free conventional memory for kernel
 */

ENTRY(_start)
OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)

SECTIONS
{
    /* Start at 1MB - standard location for x86 kernels */
    . = 1M;

    /* Kernel start marker */
    __kernel_start = .;

    /*
     * Multiboot2 header section
     * MUST be within first 32KB of kernel image
     * MUST be 8-byte aligned
     */
    .multiboot2 ALIGN(8) :
    {
        *(.multiboot2)
    }

    /*
     * Text section (executable code)
     * Page-aligned for memory protection
     */
    .text ALIGN(4K) :
    {
        __text_start = .;
        *(.text)
        *(.text.*)
        __text_end = .;
    }

    /*
     * Read-only data section
     * Includes string literals, const data, etc.
     */
    .rodata ALIGN(4K) :
    {
        __rodata_start = .;
        *(.rodata)
        *(.rodata.*)
        __rodata_end = .;
    }

    /*
     * Initialized data section
     * Variables with initial values
     */
    .data ALIGN(4K) :
    {
        __data_start = .;
        *(.data)
        *(.data.*)
        __data_end = .;
    }

    /*
     * Uninitialized data section (BSS)
     * Will be zeroed by boot code before calling kmain
     */
    .bss ALIGN(4K) :
    {
        __bss_start = .;
        *(COMMON)
        *(.bss)
        *(.bss.*)
        __bss_end = .;
    }

    /* Kernel end marker */
    __kernel_end = .;

    /*
     * Discard sections we don't need
     * These add bloat and aren't useful for a kernel
     */
    /DISCARD/ :
    {
        *(.comment)
        *(.note)
        *(.note.*)
        *(.eh_frame)
        *(.eh_frame_hdr)
        *(.gnu.hash)
    }
}

/*
 * Sanity checks
 */
ASSERT(__kernel_end - __kernel_start < 16M, "Kernel too large (>16MB)")
ASSERT(. - 1M < 32K || SIZEOF(.multiboot2) > 0, "Multiboot2 header must exist")
