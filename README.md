# PhantomOS

> **"To Create, Not To Destroy"**

PhantomOS is a **bare-metal x86_64 operating system** built from scratch with a graphical desktop, 25 interactive applications, and an AI governance engine. Data is never deleted — only preserved in immutable layers, like geological strata.

![Platform](https://img.shields.io/badge/platform-x86__64%20bare--metal-green)
![Language](https://img.shields.io/badge/language-C%20%2B%20x86__64%20ASM-orange)
![License](https://img.shields.io/badge/license-CC%20BY--NC--SA%204.0-blue)

---

## Philosophy

The core principle: **destruction is architecturally impossible**.

- **No Deletion** — Data is versioned or hidden, never removed
- **Complete History** — Every change preserved in geological layers
- **AI Governance** — The Governor monitors all operations and blocks destructive actions
- **Transparency** — Full audit trail of every policy decision

---

## Features

| Feature | Description |
|---------|-------------|
| **Bare-metal x86_64 kernel** | Multiboot2 boot, long mode, 64-bit paging, IDT/PIC/PIT |
| **Graphical desktop** | 32bpp framebuffer, window manager with draggable windows, sidebar, dock |
| **25 interactive apps** | File Browser, Terminal, ArtOS, MusiKey, Notes, PVE Encrypt, and more |
| **AI Governor v4** | Behavioral learning, anomaly detection, threat timeline, quarantine system |
| **PVE-SBC encryption** | AES Rijndael S-box + CBC block chaining + LCG key stream generation |
| **GPU HAL** | Intel BLT, VirtIO GPU, VMware SVGA II, Bochs VGA, software fallback |
| **USB HID stack** | UHCI host controller, boot protocol keyboard and mouse |
| **VirtIO networking** | VirtIO-net PCI driver, ARP/ICMP stack, live ping |
| **GeoFS filesystem** | Append-only, content-addressed, immutable-layered filesystem |
| **ACPI power management** | PIIX4 PM, graceful shutdown via SCI/IRQ9 |
| **KVM paravirt clock** | High-resolution timekeeping in KVM guests |
| **Dynamic resolution** | 800x600, 1024x768, 1280x720, 1280x1024 |

---

## Quick Start

```bash
# Install dependencies (Ubuntu/Debian)
sudo apt install build-essential nasm grub-pc-bin xorriso qemu-system-x86

# Build the ISO
make -f Makefile.boot clean && make -f Makefile.boot iso

# Run in QEMU
make -f Makefile.boot qemu
```

---

## Project Structure

```
phantomos/
├── Makefile.boot              # Build system
├── boot/
│   ├── arch/x86_64/
│   │   ├── boot.S             # Multiboot2 entry, long mode setup
│   │   ├── gdt.S              # Global Descriptor Table
│   │   └── interrupts.S       # ISR/IRQ stubs
│   └── linker.ld              # Linker script (kernel at 1MB)
├── freestanding/              # Minimal C library (string, stdio)
├── iso/grub/grub.cfg          # GRUB boot menu
├── kernel/
│   ├── kmain.c                # Kernel entry point
│   ├── desktop.c              # Desktop GUI + all 25 apps
│   ├── desktop_panels.c       # Desktop chrome (header, sidebar, dock)
│   ├── wm.c                   # Window manager
│   ├── governor.c             # AI Governor policy engine
│   ├── geofs.c                # GeoFS append-only filesystem
│   ├── gpu_hal.c              # GPU Hardware Abstraction Layer
│   ├── virtio_net.c           # VirtIO networking + ARP/ICMP
│   ├── usb.c / usb_hid.c     # USB UHCI + HID drivers
│   ├── acpi.c                 # ACPI power management
│   ├── pci.c                  # PCI bus enumeration
│   ├── vmm.c / pmm.c         # Virtual + physical memory managers
│   └── ...                    # 40+ kernel source files
└── phantomos.iso              # Bootable ISO (build output)
```

---

## Applications

25 fully implemented apps launched from the desktop sidebar:

| Category | Apps |
|----------|------|
| **System** | System Monitor, Processes, Settings |
| **Files** | File Browser |
| **Security** | DNAuth, LifeAuth, BioSense, Governor, Constitution, PVE Encrypt |
| **Network** | QRNet, Network |
| **Media** | ArtOS (paint + AI art + DrawNet), MusiKey, Media Player, PhantomPods |
| **Tools** | Terminal, Notes, Backup, Desktop Lab, GPU Monitor |
| **Apps** | Geology Viewer, Users |

---

## Documentation

See the [Wiki](https://github.com/ghartrid/PhantomOS/wiki) for detailed documentation:

- [Architecture](https://github.com/ghartrid/PhantomOS/wiki/Architecture) — Boot sequence, memory layout, kernel subsystems
- [GUI Desktop](https://github.com/ghartrid/PhantomOS/wiki/GUI-Desktop) — Window manager, panels, visual polish
- [Applications](https://github.com/ghartrid/PhantomOS/wiki/Applications) — All 25 apps documented
- [AI Governor](https://github.com/ghartrid/PhantomOS/wiki/AI-Governor) — Policy enforcement and behavioral learning
- [PVE-SBC Encryption](https://github.com/ghartrid/PhantomOS/wiki/PVE-SBC-Encryption) — Planck Variable Encryption cipher
- [Hardware Drivers](https://github.com/ghartrid/PhantomOS/wiki/Hardware-Drivers) — GPU HAL, USB HID, VirtIO, ACPI
- [GeoFS Filesystem](https://github.com/ghartrid/PhantomOS/wiki/GeoFS-Filesystem) — Append-only immutable filesystem
- [Building and Running](https://github.com/ghartrid/PhantomOS/wiki/Building-and-Running) — Build instructions and QEMU setup

---

## Requirements

- **Build**: GCC, NASM, GRUB (`grub-mkrescue`), `xorriso`
- **Run**: QEMU (`qemu-system-x86_64`) with `-vga std`
- **RAM**: 512MB minimum

---

## Contributing

Contributions welcome! Please ensure:

1. No features that enable data destruction
2. Code compiles with `-mno-sse -mno-sse2 -mno-mmx -mno-80387` (freestanding kernel)
3. Follows the "To Create, Not To Destroy" philosophy

---

## License

**CC BY-NC-SA 4.0** (Creative Commons Attribution-NonCommercial-ShareAlike)

- You can share and adapt this project
- You must give credit and indicate changes
- **You cannot sell or use commercially**
- Derivatives must use the same license

See [LICENSE](LICENSE) for full terms.

---

<p align="center"><i>"Nothing is ever truly lost."</i></p>
