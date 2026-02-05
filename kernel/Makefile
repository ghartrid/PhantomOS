# ══════════════════════════════════════════════════════════════════════════════
#                            PHANTOM KERNEL
#                     "To Create, Not To Destroy"
# ══════════════════════════════════════════════════════════════════════════════

CC = gcc
CFLAGS = -Wall -Wextra -O2 -g -I..
LDFLAGS = -lpthread

# Optional mbedTLS support for TLS/SSL
# Set HAVE_MBEDTLS=1 to enable: make HAVE_MBEDTLS=1
ifdef HAVE_MBEDTLS
    CFLAGS += -DHAVE_MBEDTLS
    LDFLAGS += -lmbedtls -lmbedx509 -lmbedcrypto
endif

# GTK3 flags for GUI
GTK_CFLAGS = $(shell pkg-config --cflags gtk+-3.0)
GTK_LIBS = $(shell pkg-config --libs gtk+-3.0)

# WebKitGTK flags for web browser
WEBKIT_CFLAGS = $(shell pkg-config --cflags webkit2gtk-4.1 2>/dev/null || pkg-config --cflags webkit2gtk-4.0)
WEBKIT_LIBS = $(shell pkg-config --libs webkit2gtk-4.1 2>/dev/null || pkg-config --libs webkit2gtk-4.0)

# GStreamer flags for media player (optional)
HAVE_GSTREAMER = $(shell pkg-config --exists gstreamer-1.0 gstreamer-pbutils-1.0 2>/dev/null && echo 1 || echo 0)
ifeq ($(HAVE_GSTREAMER),1)
    GSTREAMER_CFLAGS = $(shell pkg-config --cflags gstreamer-1.0 gstreamer-pbutils-1.0)
    GSTREAMER_LIBS = $(shell pkg-config --libs gstreamer-1.0 gstreamer-pbutils-1.0)
    MEDIAPLAYER_OBJ = phantom_mediaplayer.o
    MEDIAPLAYER_CFLAG = -DHAVE_GSTREAMER
else
    GSTREAMER_CFLAGS =
    GSTREAMER_LIBS =
    MEDIAPLAYER_OBJ =
    MEDIAPLAYER_CFLAG =
endif

# libqrencode for QR code rendering (optional)
HAVE_QRENCODE = $(shell pkg-config --exists libqrencode 2>/dev/null && echo 1 || echo 0)
ifeq ($(HAVE_QRENCODE),1)
    QRENCODE_CFLAGS = $(shell pkg-config --cflags libqrencode) -DHAVE_QRENCODE
    QRENCODE_LIBS = $(shell pkg-config --libs libqrencode)
else
    QRENCODE_CFLAGS =
    QRENCODE_LIBS =
endif

# GeoFS is the geology layer for all storage
GEOFS_SRC = ../geofs.c
GEOFS_OBJ = geofs.o

# Kernel sources (shared between CLI and GUI)
KERNEL_SRCS = phantom.c vfs.c procfs.c devfs.c shell.c geofs_vfs.c init.c governor.c phantom_ai.c phantom_ai_builtin.c phantom_net.c phantom_tls.c phantom_user.c phantom_pkg.c phantom_time.c phantom_browser.c phantom_webbrowser.c phantom_apps.c phantom_storage.c phantom_dnauth.c
KERNEL_OBJS = $(KERNEL_SRCS:.c=.o)
KERNEL_BIN = phantom

# GUI sources
GUI_SRCS = gui.c phantom_gui.c phantom_artos.c
GUI_OBJS = $(GUI_SRCS:.c=.o)
GUI_BIN = phantom-gui

# Shared objects (kernel without main)
SHARED_OBJS = vfs.o procfs.o devfs.o geofs_vfs.o init.o governor.o phantom_ai.o phantom_ai_builtin.o phantom_net.o phantom_tls.o phantom_user.o phantom_pkg.o phantom_time.o phantom_browser.o phantom_webbrowser.o phantom_apps.o phantom_storage.o phantom_urlscan.o phantom_antimalware.o phantom_pods.o phantom_backup.o phantom_dnauth.o phantom_qrnet.o phantom_qrnet_transport.o phantom_musikey.o phantom_biosense.o phantom_lifeauth.o $(MEDIAPLAYER_OBJ) $(GEOFS_OBJ)

# OpenSSL for DNAuth hashing
OPENSSL_LIBS = -lssl -lcrypto

# Anti-Malware Scanner
ANTIMALWARE_BIN = phantom-antimalware

.PHONY: all clean demo shell gui antimalware

all: $(KERNEL_BIN) $(GUI_BIN) $(ANTIMALWARE_BIN)

# CLI kernel build
$(KERNEL_BIN): $(KERNEL_OBJS) $(GEOFS_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(OPENSSL_LIBS) -lm

# GUI build (with WebKitGTK for full web rendering, GStreamer for media player, qrencode for QR codes)
$(GUI_BIN): phantom_gui.o gui.o phantom_artos.o phantom_nogui.o $(SHARED_OBJS)
	$(CC) $(CFLAGS) $(GTK_CFLAGS) $(WEBKIT_CFLAGS) $(GSTREAMER_CFLAGS) $(QRENCODE_CFLAGS) -o $@ $^ $(LDFLAGS) $(GTK_LIBS) $(WEBKIT_LIBS) $(GSTREAMER_LIBS) $(QRENCODE_LIBS) $(OPENSSL_LIBS) -lm

# Special phantom.o for GUI (exclude main)
phantom_nogui.o: phantom.c phantom.h vfs.h governor.h phantom_user.h ../geofs.h
	$(CC) $(CFLAGS) -DPHANTOM_NO_MAIN -c -o $@ $<

$(GEOFS_OBJ): $(GEOFS_SRC) ../geofs.h
	$(CC) $(CFLAGS) -c -o $@ $<

phantom.o: phantom.c phantom.h vfs.h governor.h phantom_user.h ../geofs.h
	$(CC) $(CFLAGS) -c -o $@ $<

vfs.o: vfs.c vfs.h phantom.h ../geofs.h
	$(CC) $(CFLAGS) -c -o $@ $<

procfs.o: procfs.c vfs.h phantom.h
	$(CC) $(CFLAGS) -c -o $@ $<

devfs.o: devfs.c vfs.h phantom.h
	$(CC) $(CFLAGS) -c -o $@ $<

shell.o: shell.c shell.h vfs.h phantom.h init.h governor.h phantom_ai.h phantom_net.h phantom_user.h phantom_pkg.h phantom_time.h phantom_browser.h phantom_apps.h phantom_storage.h phantom_dnauth.h
	$(CC) $(CFLAGS) -c -o $@ $<

geofs_vfs.o: geofs_vfs.c vfs.h phantom.h ../geofs.h
	$(CC) $(CFLAGS) -c -o $@ $<

init.o: init.c init.h vfs.h phantom.h ../geofs.h
	$(CC) $(CFLAGS) -c -o $@ $<

governor.o: governor.c governor.h phantom.h ../geofs.h
	$(CC) $(CFLAGS) -c -o $@ $<

phantom_ai.o: phantom_ai.c phantom_ai.h phantom_ai_builtin.h phantom.h governor.h
	$(CC) $(CFLAGS) -c -o $@ $<

phantom_ai_builtin.o: phantom_ai_builtin.c phantom_ai_builtin.h phantom_ai.h phantom.h
	$(CC) $(CFLAGS) -c -o $@ $<

phantom_net.o: phantom_net.c phantom_net.h phantom.h governor.h ../geofs.h
	$(CC) $(CFLAGS) -c -o $@ $<

phantom_tls.o: phantom_tls.c phantom_tls.h phantom_net.h phantom.h governor.h ../geofs.h
	$(CC) $(CFLAGS) -c -o $@ $<

phantom_user.o: phantom_user.c phantom_user.h phantom.h governor.h phantom_dnauth.h
	$(CC) $(CFLAGS) -c -o $@ $<

phantom_pkg.o: phantom_pkg.c phantom_pkg.h phantom.h governor.h
	$(CC) $(CFLAGS) -c -o $@ $<

phantom_time.o: phantom_time.c phantom_time.h phantom.h ../geofs.h
	$(CC) $(CFLAGS) -c -o $@ $<

phantom_browser.o: phantom_browser.c phantom_browser.h phantom.h phantom_ai.h phantom_time.h ../geofs.h
	$(CC) $(CFLAGS) -c -o $@ $<

phantom_webbrowser.o: phantom_webbrowser.c phantom_webbrowser.h phantom.h governor.h phantom_net.h phantom_tls.h phantom_browser.h vfs.h ../geofs.h
	$(CC) $(CFLAGS) -c -o $@ $<

phantom_apps.o: phantom_apps.c phantom_apps.h phantom.h vfs.h governor.h ../geofs.h
	$(CC) $(CFLAGS) -c -o $@ $<

phantom_storage.o: phantom_storage.c phantom_storage.h phantom.h ../geofs.h
	$(CC) $(CFLAGS) -c -o $@ $<

phantom_urlscan.o: phantom_urlscan.c phantom_urlscan.h
	$(CC) $(CFLAGS) -c -o $@ $<

phantom_mediaplayer.o: phantom_mediaplayer.c phantom_mediaplayer.h
	$(CC) $(CFLAGS) $(GSTREAMER_CFLAGS) -c -o $@ $<

gui.o: gui.c gui.h phantom.h vfs.h init.h governor.h phantom_ai.h phantom_net.h phantom_user.h phantom_storage.h phantom_webbrowser.h phantom_urlscan.h phantom_pods.h phantom_dnauth.h phantom_qrnet.h phantom_musikey.h phantom_biosense.h
	$(CC) $(CFLAGS) $(MEDIAPLAYER_CFLAG) $(GTK_CFLAGS) $(WEBKIT_CFLAGS) $(GSTREAMER_CFLAGS) $(QRENCODE_CFLAGS) -c -o $@ $<

phantom_gui.o: phantom_gui.c gui.h phantom.h vfs.h init.h governor.h phantom_user.h
	$(CC) $(CFLAGS) $(GTK_CFLAGS) $(WEBKIT_CFLAGS) -c -o $@ $<

phantom_artos.o: phantom_artos.c phantom_artos.h
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -c -o $@ $<

phantom_dnauth.o: phantom_dnauth.c phantom_dnauth.h governor.h
	$(CC) $(CFLAGS) -c -o $@ $<

phantom_qrnet.o: phantom_qrnet.c phantom_qrnet.h governor.h phantom_dnauth.h
	$(CC) $(CFLAGS) -c -o $@ $<

phantom_qrnet_transport.o: phantom_qrnet_transport.c phantom_qrnet_transport.h phantom_qrnet.h
	$(CC) $(CFLAGS) -c -o $@ $<

phantom_musikey.o: phantom_musikey.c phantom_musikey.h
	$(CC) $(CFLAGS) -DHAVE_OPENSSL -c -o $@ $<

phantom_biosense.o: phantom_biosense.c phantom_biosense.h
	$(CC) $(CFLAGS) -DHAVE_OPENSSL -c -o $@ $<

phantom_lifeauth.o: phantom_lifeauth.c phantom_lifeauth.h
	$(CC) $(CFLAGS) -DHAVE_OPENSSL -c -o $@ $<

demo: $(KERNEL_BIN)
	./$(KERNEL_BIN) demo

shell: $(KERNEL_BIN)
	./$(KERNEL_BIN) shell

gui: $(GUI_BIN)
	./$(GUI_BIN)

# Anti-Malware Scanner build
$(ANTIMALWARE_BIN): phantom_antimalware_main.o phantom_antimalware.o phantom_antimalware_gui.o
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -o $@ $^ $(LDFLAGS) $(GTK_LIBS) -lm

phantom_antimalware.o: phantom_antimalware.c phantom_antimalware.h
	$(CC) $(CFLAGS) -c -o $@ $<

phantom_pods.o: phantom_pods.c phantom_pods.h
	$(CC) $(CFLAGS) -c -o $@ $<

phantom_backup.o: phantom_backup.c phantom_backup.h
	$(CC) $(CFLAGS) -c -o $@ $<

phantom_antimalware_gui.o: phantom_antimalware_gui.c phantom_antimalware.h
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -c -o $@ $<

phantom_antimalware_main.o: phantom_antimalware_main.c phantom_antimalware.h
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -c -o $@ $<

antimalware: $(ANTIMALWARE_BIN)
	./$(ANTIMALWARE_BIN)

# DNAuth test suite (uses governor stub for isolated testing)
test-dnauth: test_dnauth.o phantom_dnauth.o test_governor_stub.o
	$(CC) $(CFLAGS) -o test_dnauth $^ $(OPENSSL_LIBS) -lm
	./test_dnauth

test_dnauth.o: test_dnauth.c phantom_dnauth.h
	$(CC) $(CFLAGS) -c -o $@ $<

test_governor_stub.o: test_governor_stub.c governor.h
	$(CC) $(CFLAGS) -c -o $@ $<

# QRNet Transport test suite
test-qrnet-transport: test_qrnet_transport.o phantom_qrnet_transport.o phantom_qrnet.o phantom_dnauth.o test_governor_stub.o
	$(CC) $(CFLAGS) -o test_qrnet_transport $^ $(OPENSSL_LIBS) -lm
	./test_qrnet_transport

test_qrnet_transport.o: test_qrnet_transport.c phantom_qrnet_transport.h
	$(CC) $(CFLAGS) -c -o $@ $<

# BioSense test suite (blood/vein biometric authentication)
test-biosense: test_biosense.o phantom_biosense.o
	$(CC) $(CFLAGS) -o test_biosense $^ $(OPENSSL_LIBS) -lm
	./test_biosense

test_biosense.o: test_biosense.c phantom_biosense.h
	$(CC) $(CFLAGS) -DHAVE_OPENSSL -c -o $@ $<

# MusiKey test suite (musical authentication)
test-musikey: test_musikey.o phantom_musikey.o
	$(CC) $(CFLAGS) -o test_musikey $^ $(OPENSSL_LIBS) -lm
	./test_musikey

test_musikey.o: test_musikey.c phantom_musikey.h
	$(CC) $(CFLAGS) -DHAVE_OPENSSL -c -o $@ $<

# LifeAuth test suite (blood plasma authentication)
test-lifeauth: test_lifeauth.o phantom_lifeauth.o
	$(CC) $(CFLAGS) -o test_lifeauth $^ $(OPENSSL_LIBS) -lm
	./test_lifeauth

test_lifeauth.o: test_lifeauth.c phantom_lifeauth.h
	$(CC) $(CFLAGS) -DHAVE_OPENSSL -c -o $@ $<

clean:
	rm -f $(KERNEL_OBJS) $(GUI_OBJS) $(GEOFS_OBJ) $(KERNEL_BIN) $(GUI_BIN) phantom_nogui.o phantom.geo *.o

# Note: 'clean' removes build artifacts, not source.
# In true Phantom fashion, source code is never deleted,
# only new versions are created.
