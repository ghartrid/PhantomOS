#!/bin/bash
# ==============================================================================
#                            PHANTOM OS INSTALLER
#                        "To Create, Not To Destroy"
# ==============================================================================
#
# This script installs PhantomOS and all required dependencies.
# Supports: Ubuntu/Debian, Fedora/RHEL, Arch Linux, openSUSE
#
# Usage: ./install.sh [OPTIONS]
#
# Options:
#   --prefix=PATH    Install to PATH (default: /opt/phantomos)
#   --user           Install to ~/.local instead of system-wide
#   --no-desktop     Skip desktop integration (menu entries, icons)
#   --uninstall      Remove PhantomOS from the system
#   --help           Show this help message
#
# ==============================================================================

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Default settings
PREFIX="/opt/phantomos"
USER_INSTALL=0
DESKTOP_INTEGRATION=1
UNINSTALL=0
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# ==============================================================================
# Helper Functions
# ==============================================================================

print_banner() {
    echo -e "${CYAN}"
    echo "╔══════════════════════════════════════════════════════════════════════════════╗"
    echo "║                            PHANTOM OS INSTALLER                              ║"
    echo "║                        \"To Create, Not To Destroy\"                           ║"
    echo "╚══════════════════════════════════════════════════════════════════════════════╝"
    echo -e "${NC}"
}

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

check_root() {
    if [ "$USER_INSTALL" -eq 0 ] && [ "$EUID" -ne 0 ]; then
        log_error "System-wide installation requires root privileges."
        log_info "Run with sudo or use --user for local installation."
        exit 1
    fi
}

detect_distro() {
    if [ -f /etc/os-release ]; then
        . /etc/os-release
        DISTRO=$ID
        DISTRO_FAMILY=$ID_LIKE
    elif [ -f /etc/debian_version ]; then
        DISTRO="debian"
    elif [ -f /etc/redhat-release ]; then
        DISTRO="rhel"
    else
        DISTRO="unknown"
    fi

    log_info "Detected distribution: $DISTRO"
}

# ==============================================================================
# Dependency Installation
# ==============================================================================

install_dependencies_debian() {
    log_info "Installing dependencies for Debian/Ubuntu..."
    apt-get update
    apt-get install -y \
        build-essential \
        gcc \
        make \
        pkg-config \
        libgtk-3-dev \
        libwebkit2gtk-4.1-dev || apt-get install -y libwebkit2gtk-4.0-dev \
        libgstreamer1.0-dev \
        libgstreamer-plugins-base1.0-dev \
        gstreamer1.0-plugins-good \
        gstreamer1.0-plugins-bad \
        gstreamer1.0-plugins-ugly \
        gstreamer1.0-libav \
        libssl-dev \
        libqrencode-dev
}

install_dependencies_fedora() {
    log_info "Installing dependencies for Fedora/RHEL..."
    dnf install -y \
        gcc \
        make \
        pkgconfig \
        gtk3-devel \
        webkit2gtk4.1-devel || dnf install -y webkit2gtk3-devel \
        gstreamer1-devel \
        gstreamer1-plugins-base-devel \
        gstreamer1-plugins-good \
        gstreamer1-plugins-bad-free \
        gstreamer1-plugins-ugly-free \
        gstreamer1-libav \
        openssl-devel \
        qrencode-devel
}

install_dependencies_arch() {
    log_info "Installing dependencies for Arch Linux..."
    pacman -Sy --needed --noconfirm \
        base-devel \
        gcc \
        make \
        pkgconf \
        gtk3 \
        webkit2gtk \
        gstreamer \
        gst-plugins-base \
        gst-plugins-good \
        gst-plugins-bad \
        gst-plugins-ugly \
        gst-libav \
        openssl \
        qrencode
}

install_dependencies_opensuse() {
    log_info "Installing dependencies for openSUSE..."
    zypper install -y \
        gcc \
        make \
        pkg-config \
        gtk3-devel \
        webkit2gtk3-devel \
        gstreamer-devel \
        gstreamer-plugins-base-devel \
        gstreamer-plugins-good \
        gstreamer-plugins-bad \
        gstreamer-plugins-ugly \
        gstreamer-plugins-libav \
        libopenssl-devel \
        libqrencode-devel
}

install_dependencies() {
    case "$DISTRO" in
        ubuntu|debian|linuxmint|pop)
            install_dependencies_debian
            ;;
        fedora|rhel|centos|rocky|alma)
            install_dependencies_fedora
            ;;
        arch|manjaro|endeavouros)
            install_dependencies_arch
            ;;
        opensuse*|suse)
            install_dependencies_opensuse
            ;;
        *)
            # Try to detect from family
            case "$DISTRO_FAMILY" in
                *debian*|*ubuntu*)
                    install_dependencies_debian
                    ;;
                *fedora*|*rhel*)
                    install_dependencies_fedora
                    ;;
                *arch*)
                    install_dependencies_arch
                    ;;
                *)
                    log_warning "Unknown distribution. Please install dependencies manually:"
                    echo "  - GTK3 development libraries"
                    echo "  - WebKitGTK development libraries"
                    echo "  - GStreamer development libraries and plugins"
                    echo ""
                    read -p "Continue anyway? [y/N] " -n 1 -r
                    echo
                    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
                        exit 1
                    fi
                    ;;
            esac
            ;;
    esac
}

# ==============================================================================
# Build
# ==============================================================================

build_phantomos() {
    log_info "Building PhantomOS..."

    cd "$SCRIPT_DIR/kernel"

    # Clean previous build
    make clean 2>/dev/null || true

    # Build
    if make -j$(nproc); then
        log_success "Build completed successfully!"
    else
        log_error "Build failed. Check the output above for errors."
        exit 1
    fi

    cd "$SCRIPT_DIR"
}

# ==============================================================================
# Installation
# ==============================================================================

install_files() {
    log_info "Installing PhantomOS to $PREFIX..."

    # Create directories
    mkdir -p "$PREFIX/bin"
    mkdir -p "$PREFIX/share/phantomos"
    mkdir -p "$PREFIX/share/icons/hicolor/256x256/apps"
    mkdir -p "$PREFIX/share/icons/hicolor/scalable/apps"

    # Install binaries
    cp "$SCRIPT_DIR/kernel/phantom-gui" "$PREFIX/bin/"
    cp "$SCRIPT_DIR/kernel/phantom" "$PREFIX/bin/"
    cp "$SCRIPT_DIR/kernel/phantom-antimalware" "$PREFIX/bin/"

    # Make executable
    chmod +x "$PREFIX/bin/phantom-gui"
    chmod +x "$PREFIX/bin/phantom"
    chmod +x "$PREFIX/bin/phantom-antimalware"

    # Install GeoFS data directory structure
    if [ -d "$SCRIPT_DIR/kernel/geo" ]; then
        cp -r "$SCRIPT_DIR/kernel/geo" "$PREFIX/share/phantomos/"
    fi

    # Install documentation
    if [ -f "$SCRIPT_DIR/README.md" ]; then
        cp "$SCRIPT_DIR/README.md" "$PREFIX/share/phantomos/"
    fi

    # Create symlinks for system-wide access
    if [ "$USER_INSTALL" -eq 0 ]; then
        ln -sf "$PREFIX/bin/phantom-gui" /usr/local/bin/phantom-gui
        ln -sf "$PREFIX/bin/phantom" /usr/local/bin/phantom
        ln -sf "$PREFIX/bin/phantom-antimalware" /usr/local/bin/phantom-antimalware
    else
        mkdir -p "$HOME/.local/bin"
        ln -sf "$PREFIX/bin/phantom-gui" "$HOME/.local/bin/phantom-gui"
        ln -sf "$PREFIX/bin/phantom" "$HOME/.local/bin/phantom"
        ln -sf "$PREFIX/bin/phantom-antimalware" "$HOME/.local/bin/phantom-antimalware"
    fi

    log_success "Files installed successfully!"
}

create_icon() {
    # Create a simple SVG icon for PhantomOS
    cat > "$PREFIX/share/icons/hicolor/scalable/apps/phantomos.svg" << 'ICONEOF'
<?xml version="1.0" encoding="UTF-8"?>
<svg width="256" height="256" viewBox="0 0 256 256" xmlns="http://www.w3.org/2000/svg">
  <defs>
    <linearGradient id="bg" x1="0%" y1="0%" x2="100%" y2="100%">
      <stop offset="0%" style="stop-color:#1a1a2e"/>
      <stop offset="100%" style="stop-color:#16213e"/>
    </linearGradient>
    <linearGradient id="glow" x1="0%" y1="0%" x2="100%" y2="100%">
      <stop offset="0%" style="stop-color:#00d4ff"/>
      <stop offset="100%" style="stop-color:#0099cc"/>
    </linearGradient>
  </defs>
  <!-- Background circle -->
  <circle cx="128" cy="128" r="120" fill="url(#bg)" stroke="#00d4ff" stroke-width="4"/>
  <!-- Ghost body -->
  <path d="M128 40 C70 40 50 90 50 130 C50 180 50 200 50 210
           L70 195 L90 210 L110 195 L128 210 L146 195 L166 210 L186 195 L206 210
           C206 200 206 180 206 130 C206 90 186 40 128 40 Z"
        fill="url(#glow)" opacity="0.9"/>
  <!-- Eyes -->
  <ellipse cx="100" cy="110" rx="15" ry="20" fill="#1a1a2e"/>
  <ellipse cx="156" cy="110" rx="15" ry="20" fill="#1a1a2e"/>
  <!-- Eye highlights -->
  <circle cx="105" cy="105" r="5" fill="#ffffff" opacity="0.8"/>
  <circle cx="161" cy="105" r="5" fill="#ffffff" opacity="0.8"/>
  <!-- Geology layers hint -->
  <path d="M70 160 Q128 170 186 160" stroke="#1a1a2e" stroke-width="3" fill="none" opacity="0.5"/>
  <path d="M75 175 Q128 185 181 175" stroke="#1a1a2e" stroke-width="2" fill="none" opacity="0.3"/>
</svg>
ICONEOF

    log_info "Icon created"
}

install_desktop_entry() {
    if [ "$DESKTOP_INTEGRATION" -eq 0 ]; then
        return
    fi

    log_info "Creating desktop entries..."

    # Create icon
    create_icon

    # Determine desktop entry location
    if [ "$USER_INSTALL" -eq 1 ]; then
        DESKTOP_DIR="$HOME/.local/share/applications"
    else
        DESKTOP_DIR="/usr/share/applications"
    fi

    mkdir -p "$DESKTOP_DIR"

    # Main PhantomOS entry
    cat > "$DESKTOP_DIR/phantomos.desktop" << DESKTOPEOF
[Desktop Entry]
Version=1.0
Type=Application
Name=PhantomOS
GenericName=Operating System Environment
Comment=A geological file system with append-only storage - To Create, Not To Destroy
Exec=$PREFIX/bin/phantom-gui
Icon=phantomos
Terminal=false
Categories=System;Utility;FileManager;
Keywords=phantom;geology;filesystem;version;history;
StartupNotify=true
DESKTOPEOF

    # PhantomOS CLI entry
    cat > "$DESKTOP_DIR/phantomos-cli.desktop" << DESKTOPEOF
[Desktop Entry]
Version=1.0
Type=Application
Name=PhantomOS CLI
GenericName=PhantomOS Command Line
Comment=PhantomOS command-line interface
Exec=$PREFIX/bin/phantom shell
Icon=phantomos
Terminal=true
Categories=System;Utility;TerminalEmulator;
Keywords=phantom;shell;cli;terminal;
DESKTOPEOF

    # Anti-Malware Scanner entry
    cat > "$DESKTOP_DIR/phantom-antimalware.desktop" << DESKTOPEOF
[Desktop Entry]
Version=1.0
Type=Application
Name=Phantom Anti-Malware
GenericName=Anti-Malware Scanner
Comment=Phantom Anti-Malware Scanner - Protect your system
Exec=$PREFIX/bin/phantom-antimalware --gui
Icon=phantomos
Terminal=false
Categories=System;Security;
Keywords=antivirus;malware;scanner;security;
DESKTOPEOF

    # Update desktop database
    if command -v update-desktop-database &> /dev/null; then
        update-desktop-database "$DESKTOP_DIR" 2>/dev/null || true
    fi

    # Update icon cache
    if command -v gtk-update-icon-cache &> /dev/null; then
        if [ "$USER_INSTALL" -eq 1 ]; then
            gtk-update-icon-cache -f "$HOME/.local/share/icons/hicolor" 2>/dev/null || true
        else
            gtk-update-icon-cache -f /usr/share/icons/hicolor 2>/dev/null || true
        fi
    fi

    log_success "Desktop entries created!"
}

# ==============================================================================
# Uninstallation
# ==============================================================================

uninstall_phantomos() {
    log_info "Uninstalling PhantomOS..."

    # Remove binaries and symlinks
    rm -f /usr/local/bin/phantom-gui
    rm -f /usr/local/bin/phantom
    rm -f /usr/local/bin/phantom-antimalware
    rm -f "$HOME/.local/bin/phantom-gui"
    rm -f "$HOME/.local/bin/phantom"
    rm -f "$HOME/.local/bin/phantom-antimalware"

    # Remove installation directory
    rm -rf /opt/phantomos
    rm -rf "$HOME/.local/share/phantomos"

    # Remove desktop entries
    rm -f /usr/share/applications/phantomos.desktop
    rm -f /usr/share/applications/phantomos-cli.desktop
    rm -f /usr/share/applications/phantom-antimalware.desktop
    rm -f "$HOME/.local/share/applications/phantomos.desktop"
    rm -f "$HOME/.local/share/applications/phantomos-cli.desktop"
    rm -f "$HOME/.local/share/applications/phantom-antimalware.desktop"

    # Remove icons
    rm -f /usr/share/icons/hicolor/scalable/apps/phantomos.svg
    rm -f "$HOME/.local/share/icons/hicolor/scalable/apps/phantomos.svg"

    # Note: User data in ~/.phantomos is preserved (geology never destroys!)
    log_warning "User data in ~/.phantomos has been preserved (Phantom philosophy)."
    log_warning "Remove manually if desired: rm -rf ~/.phantomos"

    log_success "PhantomOS uninstalled successfully!"
}

# ==============================================================================
# Main
# ==============================================================================

show_help() {
    echo "PhantomOS Installer"
    echo ""
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  --prefix=PATH    Install to PATH (default: /opt/phantomos)"
    echo "  --user           Install to ~/.local instead of system-wide"
    echo "  --no-desktop     Skip desktop integration (menu entries, icons)"
    echo "  --uninstall      Remove PhantomOS from the system"
    echo "  --help           Show this help message"
    echo ""
    echo "Examples:"
    echo "  sudo ./install.sh              # System-wide installation"
    echo "  ./install.sh --user            # User-local installation"
    echo "  sudo ./install.sh --uninstall  # Remove PhantomOS"
}

# Parse arguments
for arg in "$@"; do
    case $arg in
        --prefix=*)
            PREFIX="${arg#*=}"
            ;;
        --user)
            USER_INSTALL=1
            PREFIX="$HOME/.local/share/phantomos"
            ;;
        --no-desktop)
            DESKTOP_INTEGRATION=0
            ;;
        --uninstall)
            UNINSTALL=1
            ;;
        --help|-h)
            show_help
            exit 0
            ;;
        *)
            log_error "Unknown option: $arg"
            show_help
            exit 1
            ;;
    esac
done

# Main execution
print_banner

if [ "$UNINSTALL" -eq 1 ]; then
    check_root
    uninstall_phantomos
    exit 0
fi

# Check if we're in the right directory
if [ ! -f "$SCRIPT_DIR/kernel/Makefile" ]; then
    log_error "This script must be run from the PhantomOS source directory."
    exit 1
fi

detect_distro

if [ "$USER_INSTALL" -eq 0 ]; then
    check_root
    log_info "Performing system-wide installation to $PREFIX"
else
    log_info "Performing user installation to $PREFIX"
fi

echo ""
log_info "This installer will:"
echo "  1. Install required dependencies (GTK3, WebKitGTK, GStreamer)"
echo "  2. Build PhantomOS from source"
echo "  3. Install binaries to $PREFIX"
if [ "$DESKTOP_INTEGRATION" -eq 1 ]; then
    echo "  4. Create desktop menu entries and icons"
fi
echo ""

read -p "Continue with installation? [Y/n] " -n 1 -r
echo
if [[ $REPLY =~ ^[Nn]$ ]]; then
    log_info "Installation cancelled."
    exit 0
fi

echo ""

# Step 1: Install dependencies
if [ "$USER_INSTALL" -eq 0 ]; then
    install_dependencies
else
    log_warning "User installation - skipping system dependency installation."
    log_warning "Make sure you have installed: gtk3, webkit2gtk, gstreamer"
fi

# Step 2: Build
build_phantomos

# Step 3: Install files
install_files

# Step 4: Desktop integration
install_desktop_entry

echo ""
echo -e "${GREEN}╔══════════════════════════════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║                     PHANTOMOS INSTALLATION COMPLETE!                         ║${NC}"
echo -e "${GREEN}╚══════════════════════════════════════════════════════════════════════════════╝${NC}"
echo ""
echo "You can now run PhantomOS:"
echo ""
echo "  ${CYAN}phantom-gui${NC}           - Launch the graphical interface"
echo "  ${CYAN}phantom shell${NC}         - Start the command-line shell"
echo "  ${CYAN}phantom-antimalware${NC}   - Run the anti-malware scanner"
echo ""
echo "Or find PhantomOS in your application menu."
echo ""
echo -e "${BLUE}\"To Create, Not To Destroy\"${NC}"
echo ""
