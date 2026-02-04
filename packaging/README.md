# PhantomOS Packaging

Build distributable packages for PhantomOS.

## Package Types

### DEB Package (Debian/Ubuntu)
Best for users on Debian-based systems who want:
- System package manager integration (apt updates)
- Automatic dependency installation
- Standard system paths

### AppImage (Universal)
Best for users who want:
- Single portable executable
- No installation required
- Works on any Linux distribution
- Full system access (no sandbox)

## Building Packages

### Prerequisites (Ubuntu/Debian)
```bash
# Build dependencies
sudo apt install build-essential gcc make pkg-config \
    libgtk-3-dev libwebkit2gtk-4.1-dev \
    libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
    libssl-dev libqrencode-dev

# For DEB building
sudo apt install dpkg-dev debhelper fakeroot

# For AppImage (wget or curl needed)
sudo apt install wget
```

### Build DEB Package
```bash
cd packaging
./build-deb.sh
```
Output: `phantomos_1.0.0-1_amd64.deb`

### Build AppImage
```bash
cd packaging/appimage
./build-appimage.sh
```
Output: `PhantomOS-1.0.0-x86_64.AppImage`

## Installing

### DEB Package
```bash
sudo apt install ./phantomos_1.0.0-1_amd64.deb
```

### AppImage
```bash
chmod +x PhantomOS-1.0.0-x86_64.AppImage
./PhantomOS-1.0.0-x86_64.AppImage
```

## AppImage Usage

The AppImage includes all three PhantomOS components:

```bash
# Launch GUI (default)
./PhantomOS-1.0.0-x86_64.AppImage

# Launch CLI shell
./PhantomOS-1.0.0-x86_64.AppImage --cli

# Launch Anti-Malware scanner
./PhantomOS-1.0.0-x86_64.AppImage --antimalware
```

## Directory Structure

```
packaging/
├── README.md              # This file
├── build-deb.sh           # DEB package builder
├── phantomos.desktop      # Main desktop entry
├── phantomos-cli.desktop  # CLI desktop entry
├── phantom-antimalware.desktop
├── phantomos.svg          # Application icon
├── debian/                # Debian packaging files
│   ├── control            # Package metadata
│   ├── rules              # Build rules
│   ├── changelog          # Version history
│   ├── copyright          # License info
│   └── compat             # Debhelper compat level
└── appimage/              # AppImage building
    └── build-appimage.sh  # AppImage builder
```

## Notes

- AppImage has **full system access** - no sandbox restrictions
- DEB package integrates with system package manager
- Both formats support all PhantomOS features:
  - GeoFS (full filesystem access)
  - Anti-Malware scanning
  - QRNet network
  - WebKitGTK browser
  - GStreamer media player

---
*"To Create, Not To Destroy"*
