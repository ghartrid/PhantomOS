# ══════════════════════════════════════════════════════════════════════════════
#                           PHANTOM EXPLORER
#                     GUI/TUI for PhantomOS GeoFS
#                      "To Create, Not To Destroy"
# ══════════════════════════════════════════════════════════════════════════════

CC = gcc
BASE_CFLAGS = -Wall -Wextra -O2 -g -I..
BASE_LDFLAGS = -lpthread

# GeoFS library
GEOFS_SRC = ../geofs.c
GEOFS_OBJ = geofs.o

# TUI application (ncurses - lightweight, always available)
TUI_SRC = phantom-tui.c
TUI_OBJ = phantom-tui.o
TUI_BIN = phantom-tui
TUI_CFLAGS = $(BASE_CFLAGS)
TUI_LDFLAGS = $(BASE_LDFLAGS) -lncurses

# GTK4 GUI application (requires GTK4)
GUI_SRC = phantom-explorer.c
GUI_OBJ = phantom-explorer.o
GUI_BIN = phantom-explorer
GUI_CFLAGS = $(BASE_CFLAGS) $(shell pkg-config --cflags gtk4 2>/dev/null)
GUI_LDFLAGS = $(BASE_LDFLAGS) $(shell pkg-config --libs gtk4 2>/dev/null)

.PHONY: all tui gtk4 clean install help

# Default: build TUI (always works)
all: tui

# TUI version (ncurses)
tui: check-ncurses $(TUI_BIN)
	@echo ""
	@echo "Built: $(TUI_BIN) (Terminal UI)"
	@echo "Run with: ./$(TUI_BIN) [volume.geo]"

check-ncurses:
	@test -f /usr/include/ncurses.h || test -f /usr/include/curses.h || \
	 (echo "Error: ncurses development headers not found." && \
	  echo "Install with: sudo apt-get install libncurses-dev" && exit 1)

$(TUI_BIN): $(TUI_OBJ) $(GEOFS_OBJ)
	$(CC) $(TUI_CFLAGS) -o $@ $^ $(TUI_LDFLAGS)

$(TUI_OBJ): $(TUI_SRC) ../geofs.h
	$(CC) $(TUI_CFLAGS) -c -o $@ $<

# GTK4 version (requires: sudo apt-get install libgtk-4-dev)
gtk4: check-gtk4 $(GUI_BIN)
	@echo ""
	@echo "Built: $(GUI_BIN) (GTK4 GUI)"
	@echo "Run with: ./$(GUI_BIN) [volume.geo]"

check-gtk4:
	@pkg-config --exists gtk4 || (echo "Error: GTK4 not found." && \
	 echo "Install with: sudo apt-get install libgtk-4-dev" && exit 1)

$(GUI_BIN): $(GUI_OBJ) geofs-gtk.o
	$(CC) $(GUI_CFLAGS) -o $@ $^ $(GUI_LDFLAGS)

$(GUI_OBJ): $(GUI_SRC) ../geofs.h
	$(CC) $(GUI_CFLAGS) -c -o $@ $<

geofs-gtk.o: $(GEOFS_SRC) ../geofs.h
	$(CC) $(GUI_CFLAGS) -c -o $@ $<

# Shared GeoFS object (for TUI)
$(GEOFS_OBJ): $(GEOFS_SRC) ../geofs.h
	$(CC) $(TUI_CFLAGS) -c -o $@ $<

clean:
	rm -f $(TUI_OBJ) $(GUI_OBJ) $(GEOFS_OBJ) geofs-gtk.o $(TUI_BIN) $(GUI_BIN)

install: tui
	install -m 755 $(TUI_BIN) /usr/local/bin/

install-gtk4: gtk4
	install -m 755 $(GUI_BIN) /usr/local/bin/

# Desktop entry for GTK4 version
install-desktop: gtk4
	@mkdir -p ~/.local/share/applications
	@echo "[Desktop Entry]" > ~/.local/share/applications/phantom-explorer.desktop
	@echo "Name=Phantom Explorer" >> ~/.local/share/applications/phantom-explorer.desktop
	@echo "Comment=Browse GeoFS volumes" >> ~/.local/share/applications/phantom-explorer.desktop
	@echo "Exec=$(PWD)/$(GUI_BIN) %f" >> ~/.local/share/applications/phantom-explorer.desktop
	@echo "Icon=system-file-manager" >> ~/.local/share/applications/phantom-explorer.desktop
	@echo "Terminal=false" >> ~/.local/share/applications/phantom-explorer.desktop
	@echo "Type=Application" >> ~/.local/share/applications/phantom-explorer.desktop
	@echo "Categories=System;FileManager;" >> ~/.local/share/applications/phantom-explorer.desktop
	@echo "MimeType=application/x-geofs-volume;" >> ~/.local/share/applications/phantom-explorer.desktop
	@echo "Desktop entry installed to ~/.local/share/applications/"

help:
	@echo ""
	@echo "PHANTOM EXPLORER BUILD TARGETS"
	@echo "══════════════════════════════"
	@echo ""
	@echo "  make          - Build TUI version (ncurses, no dependencies)"
	@echo "  make tui      - Build TUI version (same as above)"
	@echo "  make gtk4     - Build GTK4 GUI (requires libgtk-4-dev)"
	@echo "  make clean    - Remove build artifacts"
	@echo "  make help     - Show this help"
	@echo ""
	@echo "The TUI version works in any terminal and requires only ncurses."
	@echo "The GTK4 version provides a modern graphical interface."
	@echo ""

# Note: 'clean' removes build artifacts, not source.
# In true Phantom fashion, source code is never deleted,
# only new versions are created.
