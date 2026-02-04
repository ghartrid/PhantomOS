#!/bin/bash
# ==============================================================================
#                        PHANTOMOS APPIMAGE BUILDER
#                       "To Create, Not To Destroy"
# ==============================================================================
#
# This script builds a portable AppImage for PhantomOS.
# The resulting AppImage runs on any Linux distribution.
#
# Requirements:
#   - Build dependencies (GTK3, WebKitGTK, GStreamer dev packages)
#   - wget or curl (for downloading appimagetool)
#
# Usage: ./build-appimage.sh
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
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
APPDIR="$BUILD_DIR/PhantomOS.AppDir"
VERSION="1.0.0"
ARCH=$(uname -m)

log_info() { echo -e "${BLUE}[INFO]${NC} $1"; }
log_success() { echo -e "${GREEN}[SUCCESS]${NC} $1"; }
log_warning() { echo -e "${YELLOW}[WARNING]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }

print_banner() {
    echo -e "${CYAN}"
    echo "╔══════════════════════════════════════════════════════════════════════════════╗"
    echo "║                        PHANTOMOS APPIMAGE BUILDER                            ║"
    echo "║                       \"To Create, Not To Destroy\"                            ║"
    echo "╚══════════════════════════════════════════════════════════════════════════════╝"
    echo -e "${NC}"
}

# ==============================================================================
# Download appimagetool if not present
# ==============================================================================
get_appimagetool() {
    if [ -x "$SCRIPT_DIR/appimagetool" ]; then
        log_info "appimagetool already present"
        return
    fi

    log_info "Downloading appimagetool..."

    local TOOL_ARCH="$ARCH"
    if [ "$ARCH" = "x86_64" ]; then
        TOOL_ARCH="x86_64"
    elif [ "$ARCH" = "aarch64" ]; then
        TOOL_ARCH="aarch64"
    else
        log_error "Unsupported architecture: $ARCH"
        exit 1
    fi

    local URL="https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-${TOOL_ARCH}.AppImage"

    if command -v wget &> /dev/null; then
        wget -q "$URL" -O "$SCRIPT_DIR/appimagetool"
    elif command -v curl &> /dev/null; then
        curl -sL "$URL" -o "$SCRIPT_DIR/appimagetool"
    else
        log_error "Neither wget nor curl found. Please install one."
        exit 1
    fi

    chmod +x "$SCRIPT_DIR/appimagetool"
    log_success "appimagetool downloaded"
}

# ==============================================================================
# Build PhantomOS
# ==============================================================================
build_phantomos() {
    log_info "Building PhantomOS..."

    cd "$PROJECT_ROOT/kernel"
    make clean 2>/dev/null || true

    if make -j$(nproc); then
        log_success "Build completed"
    else
        log_error "Build failed"
        exit 1
    fi

    cd "$SCRIPT_DIR"
}

# ==============================================================================
# Create AppDir structure
# ==============================================================================
create_appdir() {
    log_info "Creating AppDir structure..."

    # Clean previous build
    rm -rf "$BUILD_DIR"
    mkdir -p "$APPDIR"

    # Create directory structure
    mkdir -p "$APPDIR/usr/bin"
    mkdir -p "$APPDIR/usr/lib"
    mkdir -p "$APPDIR/usr/share/applications"
    mkdir -p "$APPDIR/usr/share/icons/hicolor/256x256/apps"
    mkdir -p "$APPDIR/usr/share/icons/hicolor/scalable/apps"
    mkdir -p "$APPDIR/usr/share/phantomos"

    # Copy binaries
    cp "$PROJECT_ROOT/kernel/phantom-gui" "$APPDIR/usr/bin/"
    cp "$PROJECT_ROOT/kernel/phantom" "$APPDIR/usr/bin/"
    cp "$PROJECT_ROOT/kernel/phantom-antimalware" "$APPDIR/usr/bin/"

    # Copy desktop file and icon
    cp "$PROJECT_ROOT/packaging/phantomos.desktop" "$APPDIR/"
    cp "$PROJECT_ROOT/packaging/phantomos.desktop" "$APPDIR/usr/share/applications/"
    cp "$PROJECT_ROOT/packaging/phantomos.svg" "$APPDIR/phantomos.svg"
    cp "$PROJECT_ROOT/packaging/phantomos.svg" "$APPDIR/usr/share/icons/hicolor/scalable/apps/"

    # Copy documentation
    cp "$PROJECT_ROOT/README.md" "$APPDIR/usr/share/phantomos/" 2>/dev/null || true

    log_success "AppDir structure created"
}

# ==============================================================================
# Bundle libraries
# ==============================================================================
bundle_libraries() {
    log_info "Bundling required libraries..."

    # Get list of required libraries for each binary
    local BINARIES="$APPDIR/usr/bin/phantom-gui $APPDIR/usr/bin/phantom $APPDIR/usr/bin/phantom-antimalware"

    # Libraries to bundle (excluding system basics like libc, libpthread)
    # We bundle GTK, WebKit, GStreamer and their dependencies
    local EXCLUDE_LIBS="libc.so libpthread.so libdl.so librt.so libm.so libstdc++ libgcc_s"

    for binary in $BINARIES; do
        if [ ! -f "$binary" ]; then
            continue
        fi

        ldd "$binary" 2>/dev/null | while read line; do
            local lib=$(echo "$line" | awk '{print $3}')

            if [ -z "$lib" ] || [ ! -f "$lib" ]; then
                continue
            fi

            # Skip excluded libraries
            local skip=0
            for excl in $EXCLUDE_LIBS; do
                if echo "$lib" | grep -q "$excl"; then
                    skip=1
                    break
                fi
            done

            if [ $skip -eq 1 ]; then
                continue
            fi

            # Copy library if not already present
            local libname=$(basename "$lib")
            if [ ! -f "$APPDIR/usr/lib/$libname" ]; then
                cp -L "$lib" "$APPDIR/usr/lib/" 2>/dev/null || true
            fi
        done
    done

    # Bundle GStreamer plugins
    log_info "Bundling GStreamer plugins..."
    local GST_PLUGIN_PATH=$(pkg-config --variable=pluginsdir gstreamer-1.0 2>/dev/null || echo "/usr/lib/x86_64-linux-gnu/gstreamer-1.0")

    if [ -d "$GST_PLUGIN_PATH" ]; then
        mkdir -p "$APPDIR/usr/lib/gstreamer-1.0"

        # Essential plugins for media playback
        local GST_PLUGINS="libgstcoreelements libgstplayback libgstaudioconvert libgstvideoconvert libgstaudioresample libgstvideoscale libgstautodetect libgstalsa libgstpulseaudio libgstvolume libgsttypefindfunctions"

        for plugin in $GST_PLUGINS; do
            if ls "$GST_PLUGIN_PATH/${plugin}"*.so 2>/dev/null; then
                cp "$GST_PLUGIN_PATH/${plugin}"*.so "$APPDIR/usr/lib/gstreamer-1.0/" 2>/dev/null || true
            fi
        done
    fi

    log_success "Libraries bundled"
}

# ==============================================================================
# Create AppRun script
# ==============================================================================
create_apprun() {
    log_info "Creating AppRun script..."

    cat > "$APPDIR/AppRun" << 'APPRUNEOF'
#!/bin/bash
# PhantomOS AppImage launcher

SELF=$(readlink -f "$0")
HERE=${SELF%/*}

# Set up environment
export PATH="${HERE}/usr/bin:${PATH}"
export LD_LIBRARY_PATH="${HERE}/usr/lib:${LD_LIBRARY_PATH}"
export XDG_DATA_DIRS="${HERE}/usr/share:${XDG_DATA_DIRS}"

# GStreamer plugin path
export GST_PLUGIN_PATH="${HERE}/usr/lib/gstreamer-1.0"
export GST_PLUGIN_SYSTEM_PATH="${HERE}/usr/lib/gstreamer-1.0"

# GTK settings
export GTK_USE_PORTAL=0

# Determine which binary to run
APPIMAGE_BINARY="${APPIMAGE_BINARY:-phantom-gui}"

case "$1" in
    --cli|cli|shell)
        shift
        exec "${HERE}/usr/bin/phantom" shell "$@"
        ;;
    --antimalware|antimalware|scan)
        shift
        exec "${HERE}/usr/bin/phantom-antimalware" "$@"
        ;;
    --help|-h)
        echo "PhantomOS AppImage"
        echo ""
        echo "Usage: $0 [OPTION]"
        echo ""
        echo "Options:"
        echo "  (default)        Launch PhantomOS GUI"
        echo "  --cli, shell     Launch PhantomOS CLI shell"
        echo "  --antimalware    Launch Anti-Malware scanner"
        echo "  --help           Show this help"
        echo ""
        echo "\"To Create, Not To Destroy\""
        exit 0
        ;;
    *)
        exec "${HERE}/usr/bin/phantom-gui" "$@"
        ;;
esac
APPRUNEOF

    chmod +x "$APPDIR/AppRun"
    log_success "AppRun script created"
}

# ==============================================================================
# Build the AppImage
# ==============================================================================
build_appimage() {
    log_info "Building AppImage..."

    cd "$BUILD_DIR"

    # Set ARCH for appimagetool
    export ARCH

    local OUTPUT_NAME="PhantomOS-${VERSION}-${ARCH}.AppImage"

    # Run appimagetool
    "$SCRIPT_DIR/appimagetool" --no-appstream "$APPDIR" "$OUTPUT_NAME"

    if [ -f "$OUTPUT_NAME" ]; then
        mv "$OUTPUT_NAME" "$SCRIPT_DIR/"
        log_success "AppImage created: $SCRIPT_DIR/$OUTPUT_NAME"
    else
        log_error "AppImage creation failed"
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

log_info "Building PhantomOS AppImage for $ARCH"
echo ""

# Check for required tools
if ! command -v make &> /dev/null; then
    log_error "make not found. Please install build-essential."
    exit 1
fi

if ! command -v pkg-config &> /dev/null; then
    log_error "pkg-config not found. Please install pkg-config."
    exit 1
fi

# Check for GTK development files
if ! pkg-config --exists gtk+-3.0; then
    log_error "GTK3 development files not found. Please install libgtk-3-dev."
    exit 1
fi

get_appimagetool
build_phantomos
create_appdir
bundle_libraries
create_apprun
build_appimage
cleanup

echo ""
echo -e "${GREEN}╔══════════════════════════════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║                      APPIMAGE BUILD COMPLETE!                                ║${NC}"
echo -e "${GREEN}╚══════════════════════════════════════════════════════════════════════════════╝${NC}"
echo ""
echo "Your AppImage is ready:"
echo "  ${CYAN}$SCRIPT_DIR/PhantomOS-${VERSION}-${ARCH}.AppImage${NC}"
echo ""
echo "To run:"
echo "  ${CYAN}./PhantomOS-${VERSION}-${ARCH}.AppImage${NC}           # GUI"
echo "  ${CYAN}./PhantomOS-${VERSION}-${ARCH}.AppImage --cli${NC}     # CLI Shell"
echo "  ${CYAN}./PhantomOS-${VERSION}-${ARCH}.AppImage --antimalware${NC}  # Scanner"
echo ""
echo -e "${BLUE}\"To Create, Not To Destroy\"${NC}"
echo ""
