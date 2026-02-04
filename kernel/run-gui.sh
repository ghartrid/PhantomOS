#!/bin/bash
# Run PhantomOS GUI outside of snap environment
# Works from any terminal including VS Code

cd "$(dirname "$0")"

echo "Starting PhantomOS GUI..."

# Use env -i to get a clean environment, preserving only essential vars
exec env -i \
    HOME="$HOME" \
    DISPLAY="$DISPLAY" \
    XAUTHORITY="$XAUTHORITY" \
    XDG_RUNTIME_DIR="$XDG_RUNTIME_DIR" \
    DBUS_SESSION_BUS_ADDRESS="$DBUS_SESSION_BUS_ADDRESS" \
    PATH="/usr/bin:/bin:/usr/local/bin" \
    ./phantom-gui "$@"
