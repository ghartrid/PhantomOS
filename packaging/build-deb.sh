#!/bin/bash
# ==============================================================================
#                        PHANTOMOS DEB PACKAGE BUILDER
#                       "To Create, Not To Destroy"
# ==============================================================================
#
# This script builds a .deb package for Debian/Ubuntu systems.
#
# Requirements:
#   - Build dependencies (GTK3, WebKitGTK, GStreamer dev packages)
#   - debhelper, dpkg-dev, fakeroot
#
# Usage: ./build-deb.sh
#
# ==============================================================================

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$SCRIPT_DIR/deb-build"
VERSION="1.0.0"
ARCH=$(dpkg --print-architecture)

log_info() { echo -e "${BLUE}[INFO]${NC} $1"; }
log_success() { echo -e "${GREEN}[SUCCESS]${NC} $1"; }
log_warning() { echo -e "${YELLOW}[WARNING]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }

print_banner() {
    echo -e "${CYAN}"
    echo "╔══════════════════════════════════════════════════════════════════════════════╗"
    echo "║                        PHANTOMOS DEB PACKAGE BUILDER                         ║"
    echo "║                       \"To Create, Not To Destroy\"                            ║"
    echo "╚══════════════════════════════════════════════════════════════════════════════╝"
    echo -e "${NC}"
}

# ==============================================================================
# Check build dependencies
# ==============================================================================
check_build_deps() {
    log_info "Checking build dependencies..."

    local MISSING=""

    for cmd in dpkg-buildpackage debhelper fakeroot; do
        if ! dpkg -l | grep -q "ii  $cmd"; then
            if ! command -v $cmd &> /dev/null; then
                MISSING="$MISSING $cmd"
            fi
        fi
    done

    # Check for debhelper specifically
    if ! dpkg -l debhelper &> /dev/null 2>&1; then
        MISSING="$MISSING debhelper"
    fi

    if [ -n "$MISSING" ]; then
        log_error "Missing build dependencies:$MISSING"
        log_info "Install with: sudo apt install dpkg-dev debhelper fakeroot"
        exit 1
    fi

    log_success "Build dependencies present"
}

# ==============================================================================
# Prepare source directory
# ==============================================================================
prepare_source() {
    log_info "Preparing source directory..."

    # Clean previous build
    rm -rf "$BUILD_DIR"
    mkdir -p "$BUILD_DIR"

    # Create source directory with version
    local SRC_DIR="$BUILD_DIR/phantomos-${VERSION}"
    mkdir -p "$SRC_DIR"

    # Copy source files
    cp -r "$PROJECT_ROOT/kernel" "$SRC_DIR/"
    cp -r "$PROJECT_ROOT/packaging" "$SRC_DIR/"
    cp "$PROJECT_ROOT/geofs.c" "$SRC_DIR/" 2>/dev/null || true
    cp "$PROJECT_ROOT/geofs.h" "$SRC_DIR/" 2>/dev/null || true
    cp "$PROJECT_ROOT/README.md" "$SRC_DIR/" 2>/dev/null || true

    # Copy debian directory
    cp -r "$SCRIPT_DIR/debian" "$SRC_DIR/"

    # Make rules executable
    chmod +x "$SRC_DIR/debian/rules"

    log_success "Source directory prepared"
}

# ==============================================================================
# Build the package
# ==============================================================================
build_package() {
    log_info "Building Debian package..."

    cd "$BUILD_DIR/phantomos-${VERSION}"

    # Build the package
    dpkg-buildpackage -us -uc -b

    cd "$BUILD_DIR"

    # Find the built package
    local DEB_FILE=$(ls phantomos_${VERSION}*.deb 2>/dev/null | head -1)

    if [ -n "$DEB_FILE" ] && [ -f "$DEB_FILE" ]; then
        mv "$DEB_FILE" "$SCRIPT_DIR/"
        log_success "Package built: $SCRIPT_DIR/$DEB_FILE"
    else
        log_error "Package build failed"
        exit 1
    fi

    cd "$SCRIPT_DIR"
}

# ==============================================================================
# Cleanup
# ==============================================================================
cleanup() {
    log_info "Cleaning up build directory..."
    rm -rf "$BUILD_DIR"
    log_success "Cleanup complete"
}

# ==============================================================================
# Main
# ==============================================================================

print_banner

log_info "Building PhantomOS DEB package for $ARCH"
echo ""

check_build_deps
prepare_source
build_package
cleanup

echo ""
echo -e "${GREEN}╔══════════════════════════════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║                       DEB PACKAGE BUILD COMPLETE!                            ║${NC}"
echo -e "${GREEN}╚══════════════════════════════════════════════════════════════════════════════╝${NC}"
echo ""

DEB_NAME="phantomos_${VERSION}-1_${ARCH}.deb"
echo "Your DEB package is ready:"
echo "  ${CYAN}$SCRIPT_DIR/$DEB_NAME${NC}"
echo ""
echo "To install:"
echo "  ${CYAN}sudo dpkg -i $DEB_NAME${NC}"
echo "  ${CYAN}sudo apt install -f${NC}  # Fix any missing dependencies"
echo ""
echo "Or use apt directly:"
echo "  ${CYAN}sudo apt install ./$DEB_NAME${NC}"
echo ""
echo -e "${BLUE}\"To Create, Not To Destroy\"${NC}"
echo ""
