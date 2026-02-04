# PhantomOS Installation Guide

> **"To Create, Not To Destroy"**

PhantomOS is a revolutionary operating system environment built on the Phantom philosophy: data is never deleted, only new versions are created. All storage is managed through GeoFS, a geological file system that maintains complete history.

---

## Quick Install

### One-Line Install (Recommended)

```bash
# System-wide installation (requires sudo)
sudo ./install.sh

# OR user-local installation (no root required)
./install.sh --user
```

---

## System Requirements

### Minimum Requirements
- **OS:** Linux (64-bit)
- **RAM:** 512 MB
- **Disk:** 100 MB for installation + storage for GeoFS
- **Display:** X11 or Wayland with GTK3 support

### Supported Distributions
| Distribution | Version | Status |
|--------------|---------|--------|
| Ubuntu | 20.04+ | ✅ Fully Supported |
| Debian | 11+ | ✅ Fully Supported |
| Linux Mint | 20+ | ✅ Fully Supported |
| Fedora | 35+ | ✅ Fully Supported |
| Arch Linux | Rolling | ✅ Fully Supported |
| Manjaro | Rolling | ✅ Fully Supported |
| openSUSE | Leap 15+ | ✅ Fully Supported |
| Pop!_OS | 21.04+ | ✅ Fully Supported |

---

## Dependencies

The installer will automatically install these dependencies:

| Package | Purpose |
|---------|---------|
| GTK3 | Graphical user interface |
| WebKitGTK | Web browser engine |
| GStreamer | Media player backend |
| GCC/Make | Building from source |

### Manual Dependency Installation

<details>
<summary><b>Ubuntu/Debian</b></summary>

```bash
sudo apt update
sudo apt install -y \
    build-essential gcc make pkg-config \
    libgtk-3-dev \
    libwebkit2gtk-4.1-dev \
    libgstreamer1.0-dev \
    libgstreamer-plugins-base1.0-dev \
    gstreamer1.0-plugins-good \
    gstreamer1.0-plugins-bad \
    gstreamer1.0-plugins-ugly \
    gstreamer1.0-libav
```
</details>

<details>
<summary><b>Fedora</b></summary>

```bash
sudo dnf install -y \
    gcc make pkgconfig \
    gtk3-devel \
    webkit2gtk4.1-devel \
    gstreamer1-devel \
    gstreamer1-plugins-base-devel \
    gstreamer1-plugins-good \
    gstreamer1-plugins-bad-free \
    gstreamer1-plugins-ugly-free \
    gstreamer1-libav
```
</details>

<details>
<summary><b>Arch Linux</b></summary>

```bash
sudo pacman -Sy --needed \
    base-devel gcc make pkgconf \
    gtk3 webkit2gtk \
    gstreamer gst-plugins-base \
    gst-plugins-good gst-plugins-bad \
    gst-plugins-ugly gst-libav
```
</details>

<details>
<summary><b>openSUSE</b></summary>

```bash
sudo zypper install -y \
    gcc make pkg-config \
    gtk3-devel webkit2gtk3-devel \
    gstreamer-devel gstreamer-plugins-base-devel \
    gstreamer-plugins-good gstreamer-plugins-bad \
    gstreamer-plugins-ugly gstreamer-plugins-libav
```
</details>

---

## Installation Options

### System-Wide Installation (Recommended)

Installs to `/opt/phantomos` with symlinks in `/usr/local/bin`:

```bash
sudo ./install.sh
```

### User Installation

Installs to `~/.local/share/phantomos` (no root required):

```bash
./install.sh --user
```

### Custom Prefix

Install to a custom location:

```bash
sudo ./install.sh --prefix=/custom/path
```

### Without Desktop Integration

Skip creating menu entries and icons:

```bash
sudo ./install.sh --no-desktop
```

---

## Manual Build

If you prefer to build manually:

```bash
# Install dependencies first (see above)

# Build
cd kernel
make clean
make

# Run directly (without installing)
./phantom-gui      # Graphical interface
./phantom shell    # Command-line interface
./phantom-antimalware --gui  # Anti-malware scanner
```

---

## Post-Installation

### Running PhantomOS

After installation, you can run PhantomOS in several ways:

**From Terminal:**
```bash
phantom-gui           # Launch graphical interface
phantom shell         # Command-line shell
phantom-antimalware   # Anti-malware scanner (GUI)
phantom-antimalware --help  # Anti-malware CLI options
```

**From Application Menu:**
- Look for "PhantomOS" in your application menu
- "PhantomOS CLI" for terminal access
- "Phantom Anti-Malware" for the security scanner

### First Run

On first run, PhantomOS will:
1. Display a login screen (default user: `phantom`, password: `phantom`)
2. Create the GeoFS storage in `~/.phantomos/` or the kernel directory
3. Initialize the virtual file system

### Default Credentials

| Username | Password | Role |
|----------|----------|------|
| `phantom` | `phantom` | Administrator |
| `root` | `phantom` | Super User |

**Important:** Change these passwords after first login!

---

## Uninstallation

To remove PhantomOS:

```bash
sudo ./install.sh --uninstall
```

**Note:** User data in `~/.phantomos` is preserved (Phantom philosophy - we don't destroy data). Remove manually if desired:

```bash
rm -rf ~/.phantomos
```

---

## Troubleshooting

### WebKitGTK Not Found

If you see errors about WebKitGTK:

```bash
# Ubuntu/Debian - try alternative version
sudo apt install libwebkit2gtk-4.0-dev

# Or check what's available
apt-cache search webkit2gtk
```

### GStreamer Warnings

The warning `_dma_fmt_to_dma_drm_fmts: assertion failed` is harmless. Suppress with:

```bash
GST_DEBUG=0 phantom-gui
```

### Build Fails

Ensure all development packages are installed:

```bash
# Check pkg-config can find libraries
pkg-config --exists gtk+-3.0 && echo "GTK3: OK"
pkg-config --exists webkit2gtk-4.1 || pkg-config --exists webkit2gtk-4.0 && echo "WebKitGTK: OK"
pkg-config --exists gstreamer-1.0 && echo "GStreamer: OK"
```

### Permission Denied

For system-wide installation, use `sudo`:

```bash
sudo ./install.sh
```

For user installation without sudo:

```bash
./install.sh --user
```

---

## Features

### Core Features
- **GeoFS** - Geological file system with complete history
- **Governor** - Ethical code review system
- **No Delete** - Data is never destroyed, only hidden or versioned

### Applications
- **File Browser** - Navigate GeoFS with version history
- **Web Browser** - Full web rendering with security scanning
- **Media Player** - Audio/video playback with playlist support
- **Anti-Malware** - Real-time and on-demand scanning
- **Notes** - Simple note-taking application
- **Terminal** - Built-in command shell

### Security
- **URL Scanner** - Checks URLs against threat databases
- **File Import Scanner** - Scans imported files for malware
- **Governor Review** - All code changes reviewed for safety

---

## File Locations

| Location | Purpose |
|----------|---------|
| `/opt/phantomos/` | Installation directory (system-wide) |
| `~/.local/share/phantomos/` | Installation directory (user) |
| `~/.phantomos/` | User data and GeoFS storage |
| `./kernel/geo/` | Default GeoFS location when running from source |

---

## Support

- **Issues:** Report bugs and feature requests on GitHub
- **Philosophy:** Read about the Phantom philosophy in the Constitution panel

---

## License

PhantomOS is released under the Phantom License.

> *"To Create, Not To Destroy"*
