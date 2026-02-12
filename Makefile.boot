# PhantomOS Bootable Kernel Makefile
# "To Create, Not To Destroy"
#
# This Makefile builds the freestanding bootable kernel.
# The original Makefile is preserved for the user-space simulation.
#
# Usage:
#   make -f Makefile.boot          # Build kernel ELF
#   make -f Makefile.boot iso      # Build bootable ISO
#   make -f Makefile.boot qemu     # Run in QEMU
#   make -f Makefile.boot clean    # Clean build artifacts

#============================================================================
# Toolchain Configuration
#============================================================================

# Try to use cross-compiler if available, otherwise use system gcc
CROSS := $(shell which x86_64-elf-gcc >/dev/null 2>&1 && echo "x86_64-elf-" || echo "")

CC      = $(CROSS)gcc
AS      = $(CROSS)as
LD      = $(CROSS)ld
OBJCOPY = $(CROSS)objcopy
OBJDUMP = $(CROSS)objdump

# If using system gcc, we need additional flags
ifeq ($(CROSS),)
    # System gcc - need to be explicit about no standard library
    EXTRA_CFLAGS = -fno-stack-protector -fno-pie -no-pie
    EXTRA_LDFLAGS = -no-pie
else
    EXTRA_CFLAGS =
    EXTRA_LDFLAGS =
endif

#============================================================================
# Compiler/Linker Flags
#============================================================================

# Freestanding C flags
CFLAGS = -ffreestanding \
         -fno-builtin \
         -nostdlib \
         -nostdinc \
         -mno-red-zone \
         -mcmodel=kernel \
         -fno-pic \
         -mno-sse \
         -mno-sse2 \
         -mno-mmx \
         -mno-80387 \
         -Wall \
         -Wextra \
         -Werror=implicit-function-declaration \
         -O2 \
         -g \
         -I freestanding/include \
         -I kernel \
         $(EXTRA_CFLAGS)

# Assembly flags
ASFLAGS = --64 -g

# Linker flags
LDFLAGS = -nostdlib \
          -z noexecstack \
          -T boot/linker.ld \
          $(EXTRA_LDFLAGS)

#============================================================================
# Source Files
#============================================================================

# Boot assembly sources
BOOT_ASM_SRCS = boot/arch/x86_64/boot.S \
                boot/arch/x86_64/gdt.S \
                boot/arch/x86_64/interrupts.S

# Freestanding library sources
FREESTANDING_SRCS = freestanding/string.c \
                    freestanding/stdio.c

# Kernel sources
KERNEL_SRCS = kernel/kmain.c \
              kernel/idt.c \
              kernel/pic.c \
              kernel/timer.c \
              kernel/pmm.c \
              kernel/vmm.c \
              kernel/heap.c \
              kernel/geofs.c \
              kernel/sched.c \
              kernel/governor.c \
              kernel/keyboard.c \
              kernel/ata.c \
              kernel/shell.c \
              kernel/framebuffer.c \
              kernel/font.c \
              kernel/fbcon.c \
              kernel/mouse.c \
              kernel/graphics.c \
              kernel/wm.c \
              kernel/widgets.c \
              kernel/desktop.c \
              kernel/pci.c \
              kernel/intel_gpu.c \
              kernel/gpu_hal.c \
              kernel/bochs_vga.c \
              kernel/virtio_gpu.c \
              kernel/vmware_svga.c \
              kernel/usb.c \
              kernel/usb_hid.c \
              kernel/icons.c \
              kernel/desktop_panels.c \
              kernel/vm_detect.c \
              kernel/kvm_clock.c \
              kernel/virtio_console.c \
              kernel/acpi.c \
              kernel/virtio_net.c \
              kernel/lz4.c

# Kernel assembly sources
KERNEL_ASM_SRCS = kernel/context_switch.S

# All sources
ALL_SRCS = $(BOOT_ASM_SRCS) $(FREESTANDING_SRCS) $(KERNEL_SRCS) $(KERNEL_ASM_SRCS)

#============================================================================
# Object Files
#============================================================================

BOOT_OBJS = $(BOOT_ASM_SRCS:.S=.o)
FREESTANDING_OBJS = $(FREESTANDING_SRCS:.c=.o)
KERNEL_OBJS = $(KERNEL_SRCS:.c=.o)
KERNEL_ASM_OBJS = $(KERNEL_ASM_SRCS:.S=.o)
ALL_OBJS = $(BOOT_OBJS) $(FREESTANDING_OBJS) $(KERNEL_OBJS) $(KERNEL_ASM_OBJS)

#============================================================================
# Output Files
#============================================================================

KERNEL_ELF = phantomos.elf
KERNEL_ISO = phantomos.iso
ISO_DIR    = iso_root

#============================================================================
# Targets
#============================================================================

.PHONY: all clean iso qemu qemu-debug disasm check-tools help

# Default target
all: $(KERNEL_ELF)

# Help
help:
	@echo "PhantomOS Bootable Kernel Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all        - Build kernel ELF (default)"
	@echo "  iso        - Build bootable ISO image"
	@echo "  qemu       - Run in QEMU with serial output"
	@echo "  qemu-debug - Run in QEMU with debugging"
	@echo "  disasm     - Generate disassembly listing"
	@echo "  clean      - Remove build artifacts"
	@echo "  check-tools- Verify required tools are installed"
	@echo ""
	@echo "Toolchain: $(if $(CROSS),Cross-compiler ($(CROSS)),System GCC)"

# Check for required tools
check-tools:
	@echo "Checking build tools..."
	@which $(CC) >/dev/null 2>&1 || (echo "ERROR: $(CC) not found" && exit 1)
	@which $(AS) >/dev/null 2>&1 || (echo "ERROR: $(AS) not found" && exit 1)
	@which $(LD) >/dev/null 2>&1 || (echo "ERROR: $(LD) not found" && exit 1)
	@which grub-mkrescue >/dev/null 2>&1 || (echo "ERROR: grub-mkrescue not found" && exit 1)
	@which xorriso >/dev/null 2>&1 || (echo "ERROR: xorriso not found" && exit 1)
	@echo "All required tools found."
	@echo "Using: $(if $(CROSS),Cross-compiler ($(CROSS)),System GCC)"

#============================================================================
# Build Rules
#============================================================================

# Link kernel ELF
$(KERNEL_ELF): $(ALL_OBJS) boot/linker.ld
	@echo "  LD      $@"
	@$(LD) $(LDFLAGS) -o $@ $(ALL_OBJS)
	@echo "Kernel built: $@ ($$(stat -c %s $@ 2>/dev/null || stat -f %z $@) bytes)"

# Compile assembly files
%.o: %.S
	@echo "  AS      $<"
	@$(AS) $(ASFLAGS) -o $@ $<

# Compile C files
%.o: %.c
	@echo "  CC      $<"
	@$(CC) $(CFLAGS) -c -o $@ $<

# Build ISO image
iso: $(KERNEL_ELF)
	@echo "Building ISO image..."
	@rm -rf $(ISO_DIR)
	@mkdir -p $(ISO_DIR)/boot/grub
	@cp $(KERNEL_ELF) $(ISO_DIR)/boot/
	@cp iso/grub/grub.cfg $(ISO_DIR)/boot/grub/
	@grub-mkrescue -o $(KERNEL_ISO) $(ISO_DIR) 2>/dev/null || \
	    grub-mkrescue -o $(KERNEL_ISO) $(ISO_DIR)
	@rm -rf $(ISO_DIR)
	@echo "ISO built: $(KERNEL_ISO) ($$(stat -c %s $(KERNEL_ISO) 2>/dev/null || stat -f %z $(KERNEL_ISO)) bytes)"

# UEFI firmware (required since system has only EFI GRUB modules)
OVMF_CODE = /usr/share/OVMF/OVMF_CODE_4M.fd

# Run in QEMU (UEFI mode)
qemu: iso
	@echo "Starting QEMU (UEFI mode)..."
	@echo "Serial output will appear below. Press Ctrl+A, X to exit."
	@echo "-----------------------------------------------------------"
	@qemu-system-x86_64 \
	    -drive if=pflash,format=raw,unit=0,file=$(OVMF_CODE),readonly=on \
	    -drive file=$(KERNEL_ISO),index=1,media=cdrom \
	    -serial stdio \
	    -vga std \
	    -m 512M \
	    -no-reboot \
	    -d guest_errors \
	    -netdev user,id=net0 \
	    -device virtio-net-pci,netdev=net0

# Run in QEMU with debugging (UEFI mode)
qemu-debug: iso
	@echo "Starting QEMU in debug mode (UEFI)..."
	@echo "Serial output will appear below."
	@echo "Connect GDB with: gdb $(KERNEL_ELF) -ex 'target remote :1234'"
	@echo "Press Ctrl+A, X to exit QEMU."
	@echo "-----------------------------------------------------------"
	@qemu-system-x86_64 \
	    -drive if=pflash,format=raw,unit=0,file=$(OVMF_CODE),readonly=on \
	    -drive file=$(KERNEL_ISO),index=1,media=cdrom \
	    -serial stdio \
	    -vga std \
	    -m 512M \
	    -no-reboot \
	    -no-shutdown \
	    -d int,cpu_reset \
	    -netdev user,id=net0 \
	    -device virtio-net-pci,netdev=net0 \
	    -s -S

# Generate disassembly
disasm: $(KERNEL_ELF)
	@echo "Generating disassembly..."
	@$(OBJDUMP) -d -S $(KERNEL_ELF) > phantomos.disasm
	@echo "Disassembly written to phantomos.disasm"

# Clean build artifacts
clean:
	@echo "Cleaning..."
	@rm -f $(ALL_OBJS)
	@rm -f $(KERNEL_ELF)
	@rm -f $(KERNEL_ISO)
	@rm -f phantomos.disasm
	@rm -rf $(ISO_DIR)
	@echo "Clean complete."

#============================================================================
# Dependencies
#============================================================================

# Header dependencies
freestanding/string.o: freestanding/include/stddef.h freestanding/include/stdint.h
freestanding/stdio.o: freestanding/include/stddef.h freestanding/include/stdint.h freestanding/include/stdarg.h
kernel/kmain.o: freestanding/include/stddef.h freestanding/include/stdint.h

# Boot assembly depends on GDT
boot/arch/x86_64/boot.o: boot/arch/x86_64/gdt.S
