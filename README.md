# PhantomOS

> **"To Create, Not To Destroy"**

PhantomOS is an **experimental operating system simulation** exploring an alternative computing philosophy where data is never deleted—only preserved in immutable layers, like geological strata.

**Note:** This is a user-space simulation/proof-of-concept written in C, not a bootable operating system kernel.

![License](https://img.shields.io/badge/license-CC%20BY--NC--SA%204.0-blue)
![Platform](https://img.shields.io/badge/platform-Linux-green)
![Language](https://img.shields.io/badge/language-C-orange)

---

## Philosophy

The core principle: **destruction is architecturally impossible**.

- **No Deletion** — Data is versioned or hidden, never removed
- **Complete History** — Every change preserved in geological layers
- **AI Governance** — All code evaluated before execution
- **Transparency** — Full audit trail of all operations

---

## Components

| Component | Description |
|-----------|-------------|
| **GeoFS** | Append-only, content-addressed filesystem |
| **Governor** | AI code evaluator that blocks destructive operations |
| **VFS** | Virtual filesystem layer (procfs, devfs, geofs) |
| **DNAuth** | DNA sequence-based authentication system |
| **QRNet** | Secure code sharing via signed QR codes |
| **ARTOS** | Digital art studio with AI assistance |
| **DrawNet** | P2P collaborative drawing network |

---

## Quick Start

```bash
# Install dependencies (Ubuntu/Debian)
sudo apt install build-essential libgtk-3-dev libwebkit2gtk-4.1-dev \
    libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
    libgstreamer-plugins-good1.0-dev libssl-dev libqrencode-dev

# Build
cd kernel
make

# Run
./phantom-gui     # GUI mode
./phantom demo    # Demo mode
./phantom shell   # CLI mode
```

---

## Project Structure

```
phantomos/
├── kernel/                    # Core implementation
│   ├── phantom.c             # Kernel simulation core
│   ├── vfs.c                 # Virtual filesystem
│   ├── governor.c            # AI code evaluator
│   ├── phantom_artos.c       # ARTOS art system + DrawNet
│   ├── phantom_dnauth.c      # DNA authentication
│   ├── phantom_qrnet.c       # QR code networking
│   ├── gui.c                 # GTK3 interface
│   └── shell.c               # Interactive shell
├── geofs.c                   # GeoFS filesystem
├── docs/                     # Documentation
└── packaging/                # Distribution packaging
```

---

## Testing

The codebase has been verified with:

- AddressSanitizer — No memory errors
- UndefinedBehaviorSanitizer — No undefined behavior
- Valgrind — Zero memory leaks
- Fuzz testing — 36,000+ test cases
- Static analysis — cppcheck, scan-build

---

## Requirements

- Linux (64-bit)
- GCC
- GTK3, WebKitGTK, GStreamer, OpenSSL

---

## Contributing

Contributions welcome! Please ensure:

1. No features that enable data destruction
2. Code passes existing tests
3. Follows the project philosophy

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
