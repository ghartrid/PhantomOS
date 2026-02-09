/*
 * PhantomOS Kernel Entry Point
 * "To Create, Not To Destroy"
 *
 * This is the first C code executed after boot.S transitions to long mode.
 * It initializes core kernel systems and will eventually start the
 * Phantom kernel simulation in a bare-metal environment.
 */

#include <stdint.h>
#include <stddef.h>
#include "idt.h"
#include "pic.h"
#include "timer.h"
#include "pmm.h"
#include "vmm.h"
#include "heap.h"
#include "geofs.h"
#include "process.h"
#include "governor.h"
#include "keyboard.h"
#include "ata.h"
#include "shell.h"
#include "framebuffer.h"
#include "fbcon.h"
#include "mouse.h"
#include "pci.h"
#include "gpu_hal.h"
#include "intel_gpu.h"
#include "bochs_vga.h"
#include "virtio_gpu.h"
#include "vmware_svga.h"
#include "usb.h"
#include "usb_hid.h"
#include "vm_detect.h"
#include "kvm_clock.h"
#include "virtio_console.h"
#include "acpi.h"
#include "virtio_net.h"
#include "desktop.h"

/*============================================================================
 * Forward Declarations (from freestanding library)
 *============================================================================*/

extern void serial_init(void);
extern void vga_clear(void);
extern void vga_set_color(uint8_t attr);
extern int kprintf(const char *fmt, ...);
extern void kpanic(const char *msg);
extern void *memset(void *s, int c, size_t n);

/*============================================================================
 * Multiboot2 Definitions
 *============================================================================*/

#define MULTIBOOT2_MAGIC            0x36d76289

/* Tag types */
#define MULTIBOOT_TAG_END           0
#define MULTIBOOT_TAG_CMDLINE       1
#define MULTIBOOT_TAG_BOOTLOADER    2
#define MULTIBOOT_TAG_MODULE        3
#define MULTIBOOT_TAG_BASIC_MEMINFO 4
#define MULTIBOOT_TAG_BOOTDEV       5
#define MULTIBOOT_TAG_MMAP          6
#define MULTIBOOT_TAG_VBE           7
#define MULTIBOOT_TAG_FRAMEBUFFER   8
#define MULTIBOOT_TAG_ELF_SECTIONS  9
#define MULTIBOOT_TAG_APM           10
#define MULTIBOOT_TAG_EFI32         11
#define MULTIBOOT_TAG_EFI64         12
#define MULTIBOOT_TAG_SMBIOS        13
#define MULTIBOOT_TAG_ACPI_OLD      14
#define MULTIBOOT_TAG_ACPI_NEW      15
#define MULTIBOOT_TAG_NETWORK       16
#define MULTIBOOT_TAG_EFI_MMAP      17
#define MULTIBOOT_TAG_EFI_BS        18
#define MULTIBOOT_TAG_EFI32_IH      19
#define MULTIBOOT_TAG_EFI64_IH      20
#define MULTIBOOT_TAG_LOAD_BASE     21

/* Memory map entry types */
#define MULTIBOOT_MEMORY_AVAILABLE        1
#define MULTIBOOT_MEMORY_RESERVED         2
#define MULTIBOOT_MEMORY_ACPI_RECLAIMABLE 3
#define MULTIBOOT_MEMORY_NVS              4
#define MULTIBOOT_MEMORY_BADRAM           5

/* Multiboot tag header */
struct multiboot_tag {
    uint32_t type;
    uint32_t size;
};

/* Multiboot info header */
struct multiboot_info {
    uint32_t total_size;
    uint32_t reserved;
    /* Tags follow immediately after */
};

/* Memory map entry */
struct multiboot_mmap_entry {
    uint64_t addr;
    uint64_t len;
    uint32_t type;
    uint32_t reserved;
} __attribute__((packed));

/* Memory map tag */
struct multiboot_tag_mmap {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
    /* Entries follow */
};

/* String tag (cmdline, bootloader name) */
struct multiboot_tag_string {
    uint32_t type;
    uint32_t size;
    char string[];
};

/* Basic memory info tag */
struct multiboot_tag_basic_meminfo {
    uint32_t type;
    uint32_t size;
    uint32_t mem_lower;    /* KB below 1MB */
    uint32_t mem_upper;    /* KB above 1MB */
};

/* Framebuffer tag */
struct multiboot_tag_framebuffer {
    uint32_t type;
    uint32_t size;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t  framebuffer_bpp;
    uint8_t  framebuffer_type;     /* 0=indexed, 1=RGB, 2=text */
    uint8_t  reserved;
} __attribute__((packed));

/*============================================================================
 * Kernel Version and Build Info
 *============================================================================*/

#define PHANTOM_VERSION_MAJOR   0
#define PHANTOM_VERSION_MINOR   1
#define PHANTOM_VERSION_PATCH   0
#define PHANTOM_VERSION_STRING  "0.1.0-alpha"

/*============================================================================
 * Global Kernel State
 *============================================================================*/

static uint64_t total_memory_bytes = 0;
static uint64_t available_memory_bytes = 0;

/* Framebuffer info saved from multiboot parsing (used after heap init) */
static uint64_t saved_fb_addr = 0;
static uint32_t saved_fb_width = 0;
static uint32_t saved_fb_height = 0;
static uint32_t saved_fb_pitch = 0;
static uint32_t saved_fb_bpp = 0;
static int      saved_fb_found = 0;

/*============================================================================
 * Multiboot Info Parsing
 *============================================================================*/

/*
 * Get next multiboot tag (8-byte aligned)
 */
static struct multiboot_tag *next_tag(struct multiboot_tag *tag)
{
    /* Tags are 8-byte aligned */
    uintptr_t addr = (uintptr_t)tag + tag->size;
    addr = (addr + 7) & ~7;
    return (struct multiboot_tag *)addr;
}

/*
 * Get memory type string
 */
static const char *memory_type_string(uint32_t type)
{
    switch (type) {
    case MULTIBOOT_MEMORY_AVAILABLE:
        return "Available";
    case MULTIBOOT_MEMORY_RESERVED:
        return "Reserved";
    case MULTIBOOT_MEMORY_ACPI_RECLAIMABLE:
        return "ACPI Reclaimable";
    case MULTIBOOT_MEMORY_NVS:
        return "ACPI NVS";
    case MULTIBOOT_MEMORY_BADRAM:
        return "Bad RAM";
    default:
        return "Unknown";
    }
}

/*
 * Print bytes in human-readable format
 */
static void print_bytes(uint64_t bytes)
{
    const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit = 0;
    uint64_t whole = bytes;
    uint64_t frac = 0;

    while (whole >= 1024 && unit < 4) {
        frac = (whole % 1024) * 10 / 1024;
        whole /= 1024;
        unit++;
    }

    if (frac > 0) {
        kprintf("%lu.%lu %s", (unsigned long)whole, (unsigned long)frac, units[unit]);
    } else {
        kprintf("%lu %s", (unsigned long)whole, units[unit]);
    }
}

/*
 * Parse memory map from multiboot info
 */
static void parse_memory_map(struct multiboot_tag_mmap *mmap_tag)
{
    kprintf("\n");
    kprintf("Memory Map:\n");
    kprintf("  %s                  %s                  %s      %s\n", "Base Address", "Length", "Pages", "Type");
    kprintf("  %s                  %s                  %s      %s\n", "------------", "------", "-----", "----");

    struct multiboot_mmap_entry *entry = (struct multiboot_mmap_entry *)
        ((uint8_t *)mmap_tag + 16);  /* Skip header */
    uint8_t *end = (uint8_t *)mmap_tag + mmap_tag->size;

    while ((uint8_t *)entry < end) {
        uint64_t pages = entry->len / 4096;

        kprintf("  0x%016lx 0x%016lx %lu\t%s\n",
               (unsigned long)entry->addr,
               (unsigned long)entry->len,
               (unsigned long)pages,
               memory_type_string(entry->type));

        total_memory_bytes += entry->len;
        if (entry->type == MULTIBOOT_MEMORY_AVAILABLE) {
            available_memory_bytes += entry->len;
        }

        entry = (struct multiboot_mmap_entry *)
            ((uint8_t *)entry + mmap_tag->entry_size);
    }

    kprintf("\n");
    kprintf("Total Memory:     ");
    print_bytes(total_memory_bytes);
    kprintf("\n");
    kprintf("Available Memory: ");
    print_bytes(available_memory_bytes);
    kprintf("\n");
}

/*
 * Parse all multiboot tags
 */
static void parse_multiboot_info(struct multiboot_info *mb_info)
{
    struct multiboot_tag *tag = (struct multiboot_tag *)
        ((uint8_t *)mb_info + 8);  /* Skip header */

    while (tag->type != MULTIBOOT_TAG_END) {
        switch (tag->type) {
        case MULTIBOOT_TAG_CMDLINE: {
            struct multiboot_tag_string *str = (struct multiboot_tag_string *)tag;
            kprintf("Command Line: %s\n", str->string);
            break;
        }

        case MULTIBOOT_TAG_BOOTLOADER: {
            struct multiboot_tag_string *str = (struct multiboot_tag_string *)tag;
            kprintf("Bootloader:   %s\n", str->string);
            break;
        }

        case MULTIBOOT_TAG_BASIC_MEMINFO: {
            struct multiboot_tag_basic_meminfo *mem =
                (struct multiboot_tag_basic_meminfo *)tag;
            kprintf("Basic Memory: lower=%u KB, upper=%u KB\n",
                   mem->mem_lower, mem->mem_upper);
            break;
        }

        case MULTIBOOT_TAG_MMAP:
            parse_memory_map((struct multiboot_tag_mmap *)tag);
            break;

        case MULTIBOOT_TAG_FRAMEBUFFER: {
            struct multiboot_tag_framebuffer *fb =
                (struct multiboot_tag_framebuffer *)tag;
            kprintf("Framebuffer:  %ux%u %ubpp at 0x%lx (pitch=%u, type=%u)\n",
                   fb->framebuffer_width, fb->framebuffer_height,
                   fb->framebuffer_bpp,
                   (unsigned long)fb->framebuffer_addr,
                   fb->framebuffer_pitch, fb->framebuffer_type);
            /* Save for later init (after heap is ready) */
            saved_fb_addr = fb->framebuffer_addr;
            saved_fb_width = fb->framebuffer_width;
            saved_fb_height = fb->framebuffer_height;
            saved_fb_pitch = fb->framebuffer_pitch;
            saved_fb_bpp = fb->framebuffer_bpp;
            saved_fb_found = 1;
            break;
        }

        default:
            /* Ignore other tags for now */
            break;
        }

        tag = next_tag(tag);
    }
}

/*============================================================================
 * Kernel Banner
 *============================================================================*/

static void print_banner(void)
{
    kprintf("\n");
    kprintf("    ____  __  _____    _   ____________  __  ___\n");
    kprintf("   / __ \\/ / / /   |  / | / /_  __/ __ \\/  |/  /\n");
    kprintf("  / /_/ / /_/ / /| | /  |/ / / / / / / / /|_/ / \n");
    kprintf(" / ____/ __  / ___ |/ /|  / / / / /_/ / /  / /  \n");
    kprintf("/_/   /_/ /_/_/  |_/_/ |_/ /_/  \\____/_/  /_/   \n");
    kprintf("\n");
    kprintf("                 KERNEL v%s\n", PHANTOM_VERSION_STRING);
    kprintf("            \"To Create, Not To Destroy\"\n");
    kprintf("\n");
    kprintf("===========================================================\n");
}

/*============================================================================
 * Kernel Main Entry Point
 *============================================================================*/

/*
 * kmain - Called from boot.S after long mode transition
 *
 * @mb_info: Pointer to multiboot2 information structure
 * @magic:   Multiboot2 magic number (should be 0x36d76289)
 */
void kmain(struct multiboot_info *mb_info, uint32_t magic)
{
    /* Initialize serial port for debugging */
    serial_init();

    /* Clear screen and print banner */
    vga_clear();
    print_banner();

    /* Verify multiboot2 magic */
    if (magic != MULTIBOOT2_MAGIC) {
        kprintf("ERROR: Invalid Multiboot2 magic number!\n");
        kprintf("  Expected: 0x%08x\n", MULTIBOOT2_MAGIC);
        kprintf("  Got:      0x%08x\n", magic);
        kpanic("Multiboot2 verification failed");
    }

    kprintf("Multiboot2 Info:\n");
    kprintf("  Magic:    0x%08x (valid)\n", magic);
    kprintf("  Info at:  0x%016lx\n", (unsigned long)(uintptr_t)mb_info);
    kprintf("  Size:     %u bytes\n", mb_info->total_size);

    /* Parse multiboot information */
    parse_multiboot_info(mb_info);

    /* Print initialization status */
    kprintf("\n");
    kprintf("===========================================================\n");
    kprintf("\n");
    kprintf("Kernel Initialization:\n");
    kprintf("  [OK] Serial port (COM1 @ 115200 baud)\n");
    kprintf("  [OK] VGA text mode (80x25)\n");
    kprintf("  [OK] Multiboot2 info parsed\n");

    /* Initialize interrupt handling */
    idt_init();
    pic_init();
    timer_init();

    /* Enable interrupts */
    kprintf("  [OK] Interrupts enabled\n");
    sti();

    /* Initialize memory management */
    pmm_init(mb_info);
    kprintf("  [OK] Physical memory manager\n");

    vmm_init();
    kprintf("  [OK] Virtual memory manager\n");

    heap_init();
    kprintf("  [OK] Kernel heap\n");

    /* Initialize PCI bus enumeration */
    pci_init();
    kprintf("  [OK] PCI bus enumeration\n");

    /* Detect hypervisor (before GPU HAL so backends can adjust) */
    vm_detect_init();

    /* Initialize KVM paravirtualized clock (after VM detection) */
    kvm_clock_init();

    /* Initialize VirtIO console (after PCI, before framebuffer) */
    virtio_console_init();

    /* Initialize VirtIO network (after PCI) */
    virtio_net_init();

    /* Initialize ACPI power management */
    acpi_init();

    /* Initialize GPU HAL and register backends */
    gpu_hal_init();
    intel_gpu_register_hal();
    virtio_gpu_register_hal();
    vmware_svga_register_hal();
    bochs_vga_register_hal();
    kprintf("  [OK] GPU HAL initialized\n");

    /* Initialize framebuffer (needs heap for backbuffer) */
    if (saved_fb_found) {
        if (fb_init(saved_fb_addr, saved_fb_width, saved_fb_height,
                    saved_fb_pitch, saved_fb_bpp) == 0) {
            kprintf("  [OK] Framebuffer initialized (%ux%u)\n",
                    saved_fb_width, saved_fb_height);
            /* Initialize framebuffer console so kprintf renders on screen */
            fbcon_init();
            kprintf("  [OK] Framebuffer console (128x48)\n");

            /* Probe GPU backends and activate best one */
            gpu_hal_select_best();
            kprintf("  [OK] GPU backend: %s\n", gpu_hal_get_active_name());

            /* Enable VM optimizations (dirty tracking + timer frame limiting) */
            fb_set_vm_mode(vm_is_virtualized());
        } else {
            kprintf("  [!!] Framebuffer initialization failed\n");
        }
    } else {
        kprintf("  [--] No framebuffer (text mode)\n");
    }

    /* Test memory allocation */
    void *test_ptr = kmalloc(1024);
    if (test_ptr) {
        kprintf("  [OK] Test allocation: 0x%lx\n", (unsigned long)test_ptr);
        kfree(test_ptr);
        kprintf("  [OK] Test free completed\n");
    } else {
        kprintf("  [!!] Test allocation failed\n");
    }

    /* Initialize kernel GeoFS */
    kgeofs_volume_t *geofs_vol = NULL;
    kgeofs_error_t gerr = kgeofs_volume_create(0, 0, 0, &geofs_vol);
    if (gerr == KGEOFS_OK) {
        kprintf("  [OK] GeoFS volume created\n");

        /* Test: Write a file */
        const char *test_content = "Hello from PhantomOS GeoFS!";
        gerr = kgeofs_file_write(geofs_vol, "/hello.txt", test_content, 27);
        if (gerr == KGEOFS_OK) {
            kprintf("  [OK] Test file written: /hello.txt\n");
        } else {
            kprintf("  [!!] File write failed: %s\n", kgeofs_strerror(gerr));
        }

        /* Test: Read the file back */
        char read_buf[64];
        size_t read_size;
        gerr = kgeofs_file_read(geofs_vol, "/hello.txt", read_buf, 64, &read_size);
        if (gerr == KGEOFS_OK) {
            read_buf[read_size] = '\0';
            kprintf("  [OK] Test file read: \"%s\"\n", read_buf);
        } else {
            kprintf("  [!!] File read failed: %s\n", kgeofs_strerror(gerr));
        }

        /* Test: Create a view (geological stratum) */
        kgeofs_view_t view2;
        gerr = kgeofs_view_create(geofs_vol, "Test Layer", &view2);
        if (gerr == KGEOFS_OK) {
            kprintf("  [OK] View created: \"Test Layer\" (id=%lu)\n",
                    (unsigned long)view2);
        }

        /* Test: Write another file in the new view */
        gerr = kgeofs_file_write(geofs_vol, "/test.txt", "GeoFS works!", 12);
        if (gerr == KGEOFS_OK) {
            kprintf("  [OK] Second file written: /test.txt\n");
        }

        /* Test: Hide a file (creates new view) */
        gerr = kgeofs_view_hide(geofs_vol, "/test.txt");
        if (gerr == KGEOFS_OK) {
            kprintf("  [OK] File hidden (preserved in history)\n");
        }

        /* Verify file is hidden */
        if (!kgeofs_exists(geofs_vol, "/test.txt")) {
            kprintf("  [OK] Hidden file not visible in current view\n");
        }

        /* Switch back to previous view to see hidden file */
        gerr = kgeofs_view_switch(geofs_vol, view2);
        if (gerr == KGEOFS_OK && kgeofs_exists(geofs_vol, "/test.txt")) {
            kprintf("  [OK] File visible after time travel to view %lu\n",
                    (unsigned long)view2);
        }

        /* Switch back to latest view */
        kgeofs_view_switch(geofs_vol, kgeofs_view_current(geofs_vol) + 1);

        kprintf("\n");
        kgeofs_dump_stats(geofs_vol);
        kprintf("\n");
        kgeofs_dump_views(geofs_vol);
    } else {
        kprintf("  [!!] GeoFS volume creation failed: %s\n", kgeofs_strerror(gerr));
    }
    kprintf("\n");

    /* Initialize scheduler */
    sched_init();
    kprintf("  [OK] Process scheduler\n");

    /* Initialize Governor (policy enforcement) */
    governor_init();
    kprintf("  [OK] Governor system\n");

    /* Initialize keyboard driver */
    keyboard_init();
    kprintf("  [OK] PS/2 keyboard driver\n");

    /* Initialize mouse driver */
    mouse_init();
    kprintf("  [OK] PS/2 mouse driver\n");

    /* Initialize ATA disk driver */
    ata_init();
    kprintf("  [OK] ATA disk driver\n");

    /* Initialize USB (UHCI) host controller and HID devices */
    usb_init();
    if (usb_is_initialized()) {
        kprintf("  [OK] USB UHCI host controller (%d device%s)\n",
                usb_device_count(), usb_device_count() == 1 ? "" : "s");
    } else {
        kprintf("  [--] USB: No UHCI controller found\n");
    }

    /* Print memory statistics */
    pmm_dump_stats();
    kprintf("\n");

    /* Print the Phantom Prime Directive */
    kprintf("===========================================================\n");
    kprintf("\n");
    kprintf("  THE PRIME DIRECTIVE IS ACTIVE\n");
    kprintf("\n");
    kprintf("  In this system, destruction is architecturally impossible.\n");
    kprintf("  Data is never deleted -- only preserved in immutable layers,\n");
    kprintf("  like geological strata.\n");
    kprintf("\n");
    kprintf("  Nothing is ever truly lost.\n");
    kprintf("\n");
    kprintf("===========================================================\n");
    kprintf("\n");

    /* Initialize shell (needed for terminal window too) */
    shell_init(geofs_vol);
    kprintf("  [OK] Shell initialized\n\n");

    /* Launch GUI desktop if framebuffer available, otherwise text shell */
    if (fb_is_initialized()) {
        kprintf("Launching graphical desktop...\n");
        kprintf("Press Ctrl+A, X to exit QEMU.\n\n");

        desktop_init(geofs_vol);
        desktop_run();  /* Returns on ACPI shutdown */
        acpi_poweroff();
    } else {
        kprintf("Starting interactive shell...\n");
        kprintf("Type 'help' for available commands.\n");
        kprintf("Press Ctrl+A, X to exit QEMU.\n\n");

        shell_run();

        kprintf("\nShell exited. System halted.\n");
        kprintf("Press Ctrl+A, X to exit QEMU.\n");
    }

    /* Halt the CPU */
    for (;;) {
        __asm__ volatile ("hlt");
    }
}
