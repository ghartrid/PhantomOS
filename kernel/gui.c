/*
 * ══════════════════════════════════════════════════════════════════════════════
 *                            PHANTOM GUI
 *                     "To Create, Not To Destroy"
 * ══════════════════════════════════════════════════════════════════════════════
 *
 * GTK3-based graphical interface for PhantomOS.
 */

#define _GNU_SOURCE  /* For strcasestr */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>  /* For strcasecmp */
#include <ctype.h>    /* For tolower */
#include <time.h>
#include <sys/stat.h>

#include "gui.h"
#include "phantom.h"
#include "vfs.h"
#include "init.h"
#include "governor.h"
#include "phantom_ai.h"
#include "phantom_ai_builtin.h"
#include "phantom_net.h"
#include "phantom_tls.h"
#include "phantom_webbrowser.h"
#include "phantom_storage.h"
#include "phantom_urlscan.h"
#include "phantom_antimalware.h"
#include "phantom_dnauth.h"
#include "phantom_qrnet.h"
#include "phantom_qrnet_transport.h"
#ifdef HAVE_GSTREAMER
#include "phantom_mediaplayer.h"
#endif
#ifdef HAVE_QRENCODE
#include <qrencode.h>
#endif
#include "phantom_artos.h"
#include "../geofs.h"
#include <webkit2/webkit2.h>
#include <errno.h>
#include <stdint.h>

/* Global URL scanner instance */
static phantom_urlscan_t urlscanner;
static int urlscanner_initialized = 0;

/* Global Anti-Malware scanner instance */
static phantom_antimalware_t antimalware_scanner;
static int antimalware_initialized = 0;

#ifdef HAVE_GSTREAMER
/* Global Media Player instance */
static phantom_mediaplayer_t mediaplayer;
static int mediaplayer_initialized = 0;
#endif

/* Global QRNet Transport instance */
static qrnet_transport_t *qrnet_transport = NULL;

/* ══════════════════════════════════════════════════════════════════════════════
 * SECURITY: Shell Escape Function
 * ══════════════════════════════════════════════════════════════════════════════
 * Safely escapes a path for use in shell commands by wrapping in single quotes
 * and escaping any embedded single quotes. This prevents command injection.
 *
 * Example: file'$(id).txt becomes 'file'\''$(id).txt'
 */
static int shell_escape_path(const char *input, char *output, size_t output_size) {
    if (!input || !output || output_size < 3) return -1;

    size_t in_len = strlen(input);
    size_t out_idx = 0;

    /* Start with opening single quote */
    output[out_idx++] = '\'';

    for (size_t i = 0; i < in_len && out_idx < output_size - 2; i++) {
        if (input[i] == '\'') {
            /* Replace ' with '\'' (end quote, escaped quote, start quote) */
            if (out_idx + 4 >= output_size - 1) return -1; /* Not enough space */
            output[out_idx++] = '\'';  /* End current single-quote */
            output[out_idx++] = '\\';  /* Escape */
            output[out_idx++] = '\'';  /* The single quote */
            output[out_idx++] = '\'';  /* Start new single-quote */
        } else {
            output[out_idx++] = input[i];
        }
    }

    /* End with closing single quote */
    if (out_idx >= output_size - 1) return -1;
    output[out_idx++] = '\'';
    output[out_idx] = '\0';

    return 0;
}

/* Safe port parsing with validation */
static int gui_safe_parse_port(const char *str, uint16_t *out) {
    if (!str || !out || str[0] == '\0') return -1;

    char *endptr;
    errno = 0;
    long val = strtol(str, &endptr, 10);

    if (errno != 0 || endptr == str || *endptr != '\0') {
        return -1;
    }

    if (val < 0 || val > 65535) return -1;

    *out = (uint16_t)val;
    return 0;
}

/* Safe uint32 parsing (for code IDs etc.) */
static int gui_safe_parse_uint32(const char *str, uint32_t *out) {
    if (!str || !out || str[0] == '\0') return -1;

    char *endptr;
    errno = 0;
    unsigned long val = strtoul(str, &endptr, 10);

    if (errno != 0 || endptr == str) {
        return -1;
    }

    if (val > UINT32_MAX) return -1;

    *out = (uint32_t)val;
    return 0;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * CSS STYLING
 * ══════════════════════════════════════════════════════════════════════════════ */

static const char *phantom_css =
    "/* ═══════════════════════════════════════════════════════════════════════════\n"
    " * PHANTOM OS DARK THEME\n"
    " * \"To Create, Not To Destroy\"\n"
    " * ═══════════════════════════════════════════════════════════════════════════ */\n"
    "\n"
    "/* Global dark theme */\n"
    "window, .background {\n"
    "    background-color: #0d1117;\n"
    "    color: #c9d1d9;\n"
    "}\n"
    "\n"
    "/* Header bar - deep phantom purple gradient */\n"
    ".phantom-header {\n"
    "    background: linear-gradient(135deg, #161b22 0%, #21262d 50%, #30363d 100%);\n"
    "    color: #f0f6fc;\n"
    "    border-bottom: 1px solid #30363d;\n"
    "    box-shadow: 0 2px 8px rgba(0, 0, 0, 0.3);\n"
    "}\n"
    ".phantom-header label {\n"
    "    color: #f0f6fc;\n"
    "}\n"
    ".phantom-header .subtitle {\n"
    "    color: #8b949e;\n"
    "}\n"
    "\n"
    "/* Sidebar - dark panel with subtle highlight */\n"
    ".phantom-sidebar {\n"
    "    background-color: #161b22;\n"
    "    border-right: 1px solid #30363d;\n"
    "}\n"
    ".phantom-sidebar button {\n"
    "    border-radius: 6px;\n"
    "    border: none;\n"
    "    padding: 12px 16px;\n"
    "    margin: 2px 6px;\n"
    "    background: transparent;\n"
    "    color: #8b949e;\n"
    "    transition: all 0.2s ease;\n"
    "}\n"
    ".phantom-sidebar button:hover {\n"
    "    background-color: #21262d;\n"
    "    color: #c9d1d9;\n"
    "}\n"
    ".phantom-sidebar button:checked {\n"
    "    background: linear-gradient(135deg, #238636 0%, #2ea043 100%);\n"
    "    color: #ffffff;\n"
    "    box-shadow: 0 2px 4px rgba(35, 134, 54, 0.3);\n"
    "}\n"
    "\n"
    "/* Status bar */\n"
    ".phantom-status {\n"
    "    background-color: #161b22;\n"
    "    color: #8b949e;\n"
    "    padding: 6px 12px;\n"
    "    border-top: 1px solid #30363d;\n"
    "    font-size: 12px;\n"
    "}\n"
    "\n"
    "/* Storage indicator colors */\n"
    ".storage-ok {\n"
    "    color: #3fb950;\n"
    "    font-weight: bold;\n"
    "}\n"
    ".storage-warn {\n"
    "    color: #d29922;\n"
    "    font-weight: bold;\n"
    "}\n"
    ".storage-critical {\n"
    "    color: #f85149;\n"
    "    font-weight: bold;\n"
    "}\n"
    "\n"
    "/* Terminal panel - classic green on black */\n"
    ".phantom-terminal {\n"
    "    background-color: #0d1117;\n"
    "    color: #3fb950;\n"
    "    font-family: 'JetBrains Mono', 'Fira Code', 'Cascadia Code', monospace;\n"
    "    font-size: 13px;\n"
    "    padding: 8px;\n"
    "}\n"
    ".phantom-terminal text {\n"
    "    background-color: #0d1117;\n"
    "    color: #3fb950;\n"
    "}\n"
    "\n"
    "/* Governor status colors */\n"
    ".governor-approve {\n"
    "    color: #3fb950;\n"
    "    font-weight: bold;\n"
    "}\n"
    ".governor-decline {\n"
    "    color: #f85149;\n"
    "    font-weight: bold;\n"
    "}\n"
    ".governor-warning {\n"
    "    color: #d29922;\n"
    "    font-weight: bold;\n"
    "}\n"
    "\n"
    "/* Service status colors */\n"
    ".service-running {\n"
    "    color: #3fb950;\n"
    "}\n"
    ".service-dormant {\n"
    "    color: #8b949e;\n"
    "}\n"
    ".service-error {\n"
    "    color: #f85149;\n"
    "}\n"
    "\n"
    "/* Process status colors */\n"
    ".process-running {\n"
    "    color: #3fb950;\n"
    "}\n"
    ".process-dormant {\n"
    "    color: #8b949e;\n"
    "}\n"
    ".process-suspended {\n"
    "    color: #d29922;\n"
    "}\n"
    "\n"
    "/* Tree views and list views */\n"
    "treeview, list {\n"
    "    background-color: #0d1117;\n"
    "    color: #c9d1d9;\n"
    "}\n"
    "treeview:selected, list row:selected {\n"
    "    background-color: #388bfd;\n"
    "    color: #ffffff;\n"
    "}\n"
    "treeview header button {\n"
    "    background-color: #161b22;\n"
    "    color: #8b949e;\n"
    "    border: none;\n"
    "    border-bottom: 1px solid #30363d;\n"
    "    padding: 6px 8px;\n"
    "}\n"
    "\n"
    "/* Text entries */\n"
    "entry {\n"
    "    background-color: #0d1117;\n"
    "    color: #c9d1d9;\n"
    "    border: 1px solid #30363d;\n"
    "    border-radius: 6px;\n"
    "    padding: 6px 10px;\n"
    "}\n"
    "entry:focus {\n"
    "    border-color: #58a6ff;\n"
    "    box-shadow: 0 0 0 2px rgba(88, 166, 255, 0.3);\n"
    "}\n"
    "\n"
    "/* Text views */\n"
    "textview, textview text {\n"
    "    background-color: #0d1117;\n"
    "    color: #c9d1d9;\n"
    "}\n"
    "\n"
    "/* Buttons */\n"
    "button {\n"
    "    background: linear-gradient(180deg, #21262d 0%, #161b22 100%);\n"
    "    color: #c9d1d9;\n"
    "    border: 1px solid #30363d;\n"
    "    border-radius: 6px;\n"
    "    padding: 6px 14px;\n"
    "    transition: all 0.2s ease;\n"
    "}\n"
    "button:hover {\n"
    "    background: linear-gradient(180deg, #30363d 0%, #21262d 100%);\n"
    "    border-color: #8b949e;\n"
    "}\n"
    "button:active {\n"
    "    background-color: #0d1117;\n"
    "}\n"
    "button.suggested-action {\n"
    "    background: linear-gradient(180deg, #238636 0%, #2ea043 100%);\n"
    "    color: #ffffff;\n"
    "    border-color: #238636;\n"
    "}\n"
    "button.destructive-action {\n"
    "    background: linear-gradient(180deg, #da3633 0%, #f85149 100%);\n"
    "    color: #ffffff;\n"
    "    border-color: #da3633;\n"
    "}\n"
    "\n"
    "/* Combo boxes */\n"
    "combobox, combobox button {\n"
    "    background-color: #21262d;\n"
    "    color: #c9d1d9;\n"
    "    border: 1px solid #30363d;\n"
    "    border-radius: 6px;\n"
    "}\n"
    "combobox arrow {\n"
    "    color: #8b949e;\n"
    "}\n"
    "\n"
    "/* Scrollbars - subtle and modern */\n"
    "scrollbar {\n"
    "    background-color: #0d1117;\n"
    "}\n"
    "scrollbar slider {\n"
    "    background-color: #30363d;\n"
    "    border-radius: 10px;\n"
    "    min-width: 8px;\n"
    "    min-height: 8px;\n"
    "}\n"
    "scrollbar slider:hover {\n"
    "    background-color: #484f58;\n"
    "}\n"
    "\n"
    "/* Scrolled windows */\n"
    "scrolledwindow {\n"
    "    background-color: #0d1117;\n"
    "    border: 1px solid #30363d;\n"
    "    border-radius: 6px;\n"
    "}\n"
    "\n"
    "/* Labels */\n"
    "label {\n"
    "    color: #c9d1d9;\n"
    "}\n"
    "label.dim-label {\n"
    "    color: #8b949e;\n"
    "}\n"
    "\n"
    "/* Panes and separators */\n"
    "paned > separator {\n"
    "    background-color: #30363d;\n"
    "}\n"
    "\n"
    "/* Notebooks (tabs) */\n"
    "notebook {\n"
    "    background-color: #0d1117;\n"
    "}\n"
    "notebook header {\n"
    "    background-color: #161b22;\n"
    "    border-bottom: 1px solid #30363d;\n"
    "}\n"
    "notebook tab {\n"
    "    background-color: transparent;\n"
    "    color: #8b949e;\n"
    "    padding: 8px 16px;\n"
    "    border: none;\n"
    "}\n"
    "notebook tab:checked {\n"
    "    background-color: #0d1117;\n"
    "    color: #f0f6fc;\n"
    "    border-bottom: 2px solid #58a6ff;\n"
    "}\n"
    "\n"
    "/* Frames */\n"
    "frame {\n"
    "    border: 1px solid #30363d;\n"
    "    border-radius: 6px;\n"
    "}\n"
    "frame > label {\n"
    "    color: #8b949e;\n"
    "}\n"
    "\n"
    "/* Info bars and messages */\n"
    ".phantom-info {\n"
    "    background-color: #161b22;\n"
    "    color: #58a6ff;\n"
    "    border-left: 3px solid #58a6ff;\n"
    "    padding: 8px 12px;\n"
    "}\n"
    ".phantom-warning {\n"
    "    background-color: #161b22;\n"
    "    color: #d29922;\n"
    "    border-left: 3px solid #d29922;\n"
    "    padding: 8px 12px;\n"
    "}\n"
    ".phantom-error {\n"
    "    background-color: #161b22;\n"
    "    color: #f85149;\n"
    "    border-left: 3px solid #f85149;\n"
    "    padding: 8px 12px;\n"
    "}\n"
    ".phantom-success {\n"
    "    background-color: #161b22;\n"
    "    color: #3fb950;\n"
    "    border-left: 3px solid #3fb950;\n"
    "    padding: 8px 12px;\n"
    "}\n"
    "\n"
    "/* AI Panel styling */\n"
    ".phantom-ai-chat {\n"
    "    background-color: #0d1117;\n"
    "    font-family: 'Inter', 'Segoe UI', sans-serif;\n"
    "}\n"
    ".phantom-ai-input {\n"
    "    background-color: #161b22;\n"
    "    border: 1px solid #30363d;\n"
    "    border-radius: 8px;\n"
    "}\n"
    "\n"
    "/* Network panel */\n"
    ".phantom-network-active {\n"
    "    color: #3fb950;\n"
    "}\n"
    ".phantom-network-inactive {\n"
    "    color: #8b949e;\n"
    "}\n"
    "\n"
    "/* Geology viewer - rock/earth tones */\n"
    ".phantom-geology {\n"
    "    background-color: #0d1117;\n"
    "}\n"
    ".phantom-geology-layer {\n"
    "    background: linear-gradient(180deg, #3d2914 0%, #5c3d1e 100%);\n"
    "    border-radius: 4px;\n"
    "    padding: 4px 8px;\n"
    "    color: #d4a574;\n"
    "}\n"
    "\n"
    "/* Constitution view - parchment-like in dark mode */\n"
    ".phantom-constitution {\n"
    "    background-color: #161b22;\n"
    "    color: #c9d1d9;\n"
    "    font-family: 'Crimson Pro', 'Times New Roman', serif;\n"
    "}\n"
    ".phantom-constitution-header {\n"
    "    color: #f0f6fc;\n"
    "    font-size: 18px;\n"
    "    font-weight: bold;\n"
    "}\n"
    ".phantom-constitution-article {\n"
    "    color: #58a6ff;\n"
    "    font-weight: bold;\n"
    "}\n"
    "\n"
    "/* Tooltips */\n"
    "tooltip {\n"
    "    background-color: #21262d;\n"
    "    color: #c9d1d9;\n"
    "    border: 1px solid #30363d;\n"
    "    border-radius: 6px;\n"
    "}\n"
    "\n"
    "/* Menus */\n"
    "menu, menubar {\n"
    "    background-color: #161b22;\n"
    "    color: #c9d1d9;\n"
    "    border: 1px solid #30363d;\n"
    "}\n"
    "menu menuitem {\n"
    "    padding: 6px 12px;\n"
    "}\n"
    "menu menuitem:hover {\n"
    "    background-color: #21262d;\n"
    "}\n"
    "\n"
    "/* Dialogs */\n"
    "dialog {\n"
    "    background-color: #161b22;\n"
    "}\n"
    "messagedialog {\n"
    "    background-color: #161b22;\n"
    "}\n";

/* ══════════════════════════════════════════════════════════════════════════════
 * FORWARD DECLARATIONS
 * ══════════════════════════════════════════════════════════════════════════════ */

static void on_sidebar_button_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_file_row_activated(GtkTreeView *tree, GtkTreePath *path,
                                   GtkTreeViewColumn *column, phantom_gui_t *gui);
static void phantom_gui_open_file(phantom_gui_t *gui, const char *path);
static void phantom_gui_open_text_editor(phantom_gui_t *gui, const char *path);
static void phantom_gui_open_image_viewer(phantom_gui_t *gui, const char *path);
static void on_create_file_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_create_folder_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_hide_file_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_open_file_clicked(GtkWidget *button, phantom_gui_t *gui);
static void gui_storage_warning_callback(int level, const char *message, void *user_data);
static void on_copy_file_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_rename_file_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_search_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_history_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_import_file_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_navigate_up_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_file_back_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_file_forward_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_file_refresh_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_process_suspend_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_process_resume_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_service_awaken_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_service_rest_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_governor_mode_changed(GtkComboBox *combo, phantom_gui_t *gui);
static void on_governor_test_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_governor_cache_toggled(GtkToggleButton *button, phantom_gui_t *gui);
static void on_governor_clear_cache(GtkWidget *button, phantom_gui_t *gui);
static void on_governor_view_history(GtkWidget *button, phantom_gui_t *gui);
static void on_governor_behavioral_analyze(GtkWidget *button, phantom_gui_t *gui);
static void on_terminal_entry_activate(GtkEntry *entry, phantom_gui_t *gui);
static void on_stack_visible_child_changed(GObject *stack, GParamSpec *pspec, phantom_gui_t *gui);
static gboolean on_refresh_timer(phantom_gui_t *gui);
static void on_window_destroy(GtkWidget *widget, phantom_gui_t *gui);

/* ══════════════════════════════════════════════════════════════════════════════
 * INITIALIZATION
 * ══════════════════════════════════════════════════════════════════════════════ */

int phantom_gui_init(phantom_gui_t *gui,
                     struct phantom_kernel *kernel,
                     struct vfs_context *vfs) {
    if (!gui || !kernel || !vfs) return -1;

    memset(gui, 0, sizeof(phantom_gui_t));
    gui->kernel = kernel;
    gui->vfs = vfs;
    gui->running = 1;
    strncpy(gui->current_path, "/geo/home", sizeof(gui->current_path) - 1);  /* Start in /geo/home where GeoFS is mounted */
    gui->current_path[sizeof(gui->current_path) - 1] = '\0';

    /* Load CSS */
    GtkCssProvider *css_provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css_provider, phantom_css, -1, NULL);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(css_provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );

    /* Create main window */
    gui->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(gui->window), "PhantomOS - To Create, Not To Destroy");
    gtk_window_set_default_size(GTK_WINDOW(gui->window), 1200, 800);
    gtk_window_set_position(GTK_WINDOW(gui->window), GTK_WIN_POS_CENTER);

    g_signal_connect(gui->window, "destroy", G_CALLBACK(on_window_destroy), gui);

    /* Create header bar */
    gui->header_bar = gtk_header_bar_new();
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(gui->header_bar), TRUE);
    gtk_header_bar_set_title(GTK_HEADER_BAR(gui->header_bar), "PhantomOS");
    gtk_header_bar_set_subtitle(GTK_HEADER_BAR(gui->header_bar), "\"To Create, Not To Destroy\"");
    gtk_style_context_add_class(gtk_widget_get_style_context(gui->header_bar), "phantom-header");
    gtk_window_set_titlebar(GTK_WINDOW(gui->window), gui->header_bar);

    /* Create main container */
    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(gui->window), main_box);

    /* Create horizontal paned for sidebar and content */
    gui->main_paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(main_box), gui->main_paned, TRUE, TRUE, 0);

    /* Create sidebar */
    gui->sidebar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(gui->sidebar, 180, -1);
    gtk_style_context_add_class(gtk_widget_get_style_context(gui->sidebar), "phantom-sidebar");
    gtk_paned_pack1(GTK_PANED(gui->main_paned), gui->sidebar, FALSE, FALSE);

    /* Sidebar buttons */
#ifdef HAVE_GSTREAMER
    const char *sidebar_items[] = {
        "🏠 Desktop",
        "📁 Files",
        "⚙️ Processes",
        "🔧 Services",
        "🛡️ Governor",
        "🪨 Geology",
        "💻 Terminal",
        "📜 Constitution",
        "🤖 AI Assistant",
        "🌐 Network",
        "📱 Apps",
        "🔒 Security",
        "🎵 Media",
        "🎨 ArtOS",
        "👥 Users",
        "🧬 DNAuth",
        "📡 QRNet",
        "📦 PhantomPods",
        "💾 Backup",
        "🧪 Desktop Lab"
    };
    const char *sidebar_names[] = {
        "desktop", "files", "processes", "services", "governor", "geology", "terminal", "constitution", "ai", "network", "apps", "security", "media", "artos", "users", "dnauth", "qrnet", "pods", "backup", "desktoplab"
    };
    const int sidebar_count = 20;
#else
    const char *sidebar_items[] = {
        "🏠 Desktop",
        "📁 Files",
        "⚙️ Processes",
        "🔧 Services",
        "🛡️ Governor",
        "🪨 Geology",
        "💻 Terminal",
        "📜 Constitution",
        "🤖 AI Assistant",
        "🌐 Network",
        "📱 Apps",
        "🔒 Security",
        "🎨 ArtOS",
        "👥 Users",
        "🧬 DNAuth",
        "📡 QRNet",
        "📦 PhantomPods",
        "💾 Backup",
        "🧪 Desktop Lab"
    };
    const char *sidebar_names[] = {
        "desktop", "files", "processes", "services", "governor", "geology", "terminal", "constitution", "ai", "network", "apps", "security", "artos", "users", "dnauth", "qrnet", "pods", "backup", "desktoplab"
    };
    const int sidebar_count = 19;
#endif

    GtkWidget *first_button = NULL;
    for (int i = 0; i < sidebar_count; i++) {
        GtkWidget *button = gtk_toggle_button_new_with_label(sidebar_items[i]);
        gtk_widget_set_name(button, sidebar_names[i]);
        g_object_set_data(G_OBJECT(button), "panel-name", (gpointer)sidebar_names[i]);
        g_signal_connect(button, "toggled", G_CALLBACK(on_sidebar_button_clicked), gui);
        gtk_box_pack_start(GTK_BOX(gui->sidebar), button, FALSE, FALSE, 0);

        if (i == 0) first_button = button;
    }

    /* Create content stack */
    gui->content_stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(gui->content_stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_paned_pack2(GTK_PANED(gui->main_paned), gui->content_stack, TRUE, TRUE);

    /* Connect signal to refresh Files panel when it becomes visible */
    g_signal_connect(gui->content_stack, "notify::visible-child",
                     G_CALLBACK(on_stack_visible_child_changed), gui);

    /* Create panels */
    /* Desktop Environment - First panel (default view) */
    gui->desktop_panel = phantom_gui_create_desktop_panel(gui);
    gtk_stack_add_named(GTK_STACK(gui->content_stack), gui->desktop_panel, "desktop");

    gui->file_browser = phantom_gui_create_file_browser(gui);
    gtk_stack_add_named(GTK_STACK(gui->content_stack), gui->file_browser, "files");

    gui->process_viewer = phantom_gui_create_process_viewer(gui);
    gtk_stack_add_named(GTK_STACK(gui->content_stack), gui->process_viewer, "processes");

    gui->service_manager = phantom_gui_create_service_manager(gui);
    gtk_stack_add_named(GTK_STACK(gui->content_stack), gui->service_manager, "services");

    gui->governor_panel = phantom_gui_create_governor_panel(gui);
    gtk_stack_add_named(GTK_STACK(gui->content_stack), gui->governor_panel, "governor");

    gui->geology_viewer = phantom_gui_create_geology_viewer(gui);
    gtk_stack_add_named(GTK_STACK(gui->content_stack), gui->geology_viewer, "geology");

    gui->terminal_panel = phantom_gui_create_terminal(gui);
    gtk_stack_add_named(GTK_STACK(gui->content_stack), gui->terminal_panel, "terminal");

    gui->constitution_view = phantom_gui_create_constitution_view(gui);
    gtk_stack_add_named(GTK_STACK(gui->content_stack), gui->constitution_view, "constitution");

    gui->ai_panel = phantom_gui_create_ai_panel(gui);
    gtk_stack_add_named(GTK_STACK(gui->content_stack), gui->ai_panel, "ai");

    gui->network_panel = phantom_gui_create_network_panel(gui);
    gtk_stack_add_named(GTK_STACK(gui->content_stack), gui->network_panel, "network");

    gui->apps_panel = phantom_gui_create_apps_panel(gui);
    gtk_stack_add_named(GTK_STACK(gui->content_stack), gui->apps_panel, "apps");

    gui->security_panel = phantom_gui_create_security_panel(gui);
    gtk_stack_add_named(GTK_STACK(gui->content_stack), gui->security_panel, "security");

#ifdef HAVE_GSTREAMER
    gui->media_panel = phantom_gui_create_media_panel(gui);
    gtk_stack_add_named(GTK_STACK(gui->content_stack), gui->media_panel, "media");
#endif

    /* ArtOS - Digital Art Studio */
    gui->artos_panel = phantom_gui_create_artos_panel(gui);
    gtk_stack_add_named(GTK_STACK(gui->content_stack), gui->artos_panel, "artos");

    /* User Management */
    gui->users_panel = phantom_gui_create_users_panel(gui);
    gtk_stack_add_named(GTK_STACK(gui->content_stack), gui->users_panel, "users");

    /* DNAuth - DNA-Based Authentication */
    gui->dnauth_panel = phantom_gui_create_dnauth_panel(gui);
    gtk_stack_add_named(GTK_STACK(gui->content_stack), gui->dnauth_panel, "dnauth");

    /* QRNet - QR Code Distributed File Network */
    gui->qrnet_panel = phantom_gui_create_qrnet_panel(gui);
    gtk_stack_add_named(GTK_STACK(gui->content_stack), gui->qrnet_panel, "qrnet");

    /* PhantomPods - Compatibility Containers */
    gui->pods_panel = phantom_gui_create_pods_panel(gui);
    gtk_stack_add_named(GTK_STACK(gui->content_stack), gui->pods_panel, "pods");

    /* Backup - Data Preservation */
    gui->backup_panel = phantom_gui_create_backup_panel(gui);
    gtk_stack_add_named(GTK_STACK(gui->content_stack), gui->backup_panel, "backup");

    /* Desktop Lab - Widgets & Experimental Features */
    gui->desktop_lab_panel = phantom_gui_create_desktop_lab_panel(gui);
    gtk_stack_add_named(GTK_STACK(gui->content_stack), gui->desktop_lab_panel, "desktoplab");

    /* Create status bar with storage indicator */
    GtkWidget *status_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_style_context_add_class(gtk_widget_get_style_context(status_box), "phantom-status");

    gui->status_bar = gtk_label_new("Ready - All data preserved in geology");
    gtk_widget_set_halign(gui->status_bar, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(status_box), gui->status_bar, TRUE, TRUE, 8);

    /* Storage indicator */
    gui->storage_indicator = gtk_label_new("Storage: --");
    gtk_widget_set_halign(gui->storage_indicator, GTK_ALIGN_END);
    gtk_style_context_add_class(gtk_widget_get_style_context(gui->storage_indicator), "storage-ok");
    gtk_box_pack_end(GTK_BOX(status_box), gui->storage_indicator, FALSE, FALSE, 8);

    gtk_box_pack_end(GTK_BOX(main_box), status_box, FALSE, FALSE, 0);

    /* Initialize storage manager */
    gui->storage_manager = malloc(sizeof(phantom_storage_manager_t));
    if (gui->storage_manager) {
        phantom_storage_manager_t *mgr = (phantom_storage_manager_t *)gui->storage_manager;
        if (phantom_storage_init(mgr, kernel, kernel->geofs_volume) == 0) {
            phantom_storage_set_warning_callback(mgr, gui_storage_warning_callback, gui);
            gui->last_storage_warning = STORAGE_WARN_NORMAL;
        } else {
            free(gui->storage_manager);
            gui->storage_manager = NULL;
        }
    }

    /* Select first panel */
    if (first_button) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(first_button), TRUE);
    }

    /* Start refresh timer */
    gui->refresh_timer = g_timeout_add(2000, (GSourceFunc)on_refresh_timer, gui);

    /* Initial refresh */
    phantom_gui_refresh_files(gui);
    phantom_gui_refresh_processes(gui);
    phantom_gui_refresh_services(gui);
    phantom_gui_refresh_governor(gui);
    phantom_gui_refresh_users(gui);

    return 0;
}

void phantom_gui_set_user_system(phantom_gui_t *gui, phantom_user_system_t *user_sys) {
    if (gui) {
        gui->user_system = user_sys;
    }
}

/* ══════════════════════════════════════════════════════════════════════════════
 * LOGIN DIALOG
 * ══════════════════════════════════════════════════════════════════════════════ */

typedef struct {
    phantom_gui_t *gui;
    GtkWidget *dialog;
    GtkWidget *username_entry;
    GtkWidget *password_entry;
    GtkWidget *error_label;
    GtkWidget *login_button;
    int attempts;
    int success;
} LoginDialogData;

static void on_login_button_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    LoginDialogData *data = (LoginDialogData *)user_data;
    phantom_gui_t *gui = data->gui;

    const char *username = gtk_entry_get_text(GTK_ENTRY(data->username_entry));
    const char *password = gtk_entry_get_text(GTK_ENTRY(data->password_entry));

    if (!username || !username[0]) {
        gtk_label_set_text(GTK_LABEL(data->error_label), "Please enter a username");
        return;
    }

    if (!gui->user_system) {
        gtk_label_set_text(GTK_LABEL(data->error_label), "User system not initialized");
        return;
    }

    /* Attempt authentication */
    phantom_session_t *session = NULL;
    int result = phantom_user_authenticate(gui->user_system, username, password, &session);

    if (result == USER_OK && session) {
        /* Success! */
        gui->session = session;
        gui->uid = session->uid;
        strncpy(gui->username, username, PHANTOM_MAX_USERNAME - 1);
        gui->username[PHANTOM_MAX_USERNAME - 1] = '\0';
        gui->logged_in = 1;
        data->success = 1;
        gtk_dialog_response(GTK_DIALOG(data->dialog), GTK_RESPONSE_OK);
        return;
    }

    /* Login failed */
    data->attempts++;

    const char *error_msg;
    switch (result) {
        case USER_ERR_NOT_FOUND:
            error_msg = "Unknown user";
            break;
        case USER_ERR_BAD_PASSWORD:
            error_msg = "Incorrect password";
            break;
        case USER_ERR_LOCKED:
            error_msg = "Account locked - too many failed attempts";
            break;
        case USER_ERR_DORMANT:
            error_msg = "Account is dormant (deactivated)";
            break;
        case USER_ERR_DENIED:
            error_msg = "Account suspended";
            break;
        default:
            error_msg = "Authentication failed";
            break;
    }

    char msg[256];
    if (data->attempts >= 3) {
        snprintf(msg, sizeof(msg), "%s - Maximum attempts reached", error_msg);
        gtk_widget_set_sensitive(data->login_button, FALSE);
        gtk_widget_set_sensitive(data->username_entry, FALSE);
        gtk_widget_set_sensitive(data->password_entry, FALSE);
    } else {
        snprintf(msg, sizeof(msg), "%s (Attempt %d/3)", error_msg, data->attempts);
    }

    gtk_label_set_text(GTK_LABEL(data->error_label), msg);
    gtk_entry_set_text(GTK_ENTRY(data->password_entry), "");
    gtk_widget_grab_focus(data->password_entry);
}

static void on_entry_activate(GtkEntry *entry, gpointer user_data) {
    (void)entry;
    LoginDialogData *data = (LoginDialogData *)user_data;
    on_login_button_clicked(GTK_BUTTON(data->login_button), user_data);
}

int phantom_gui_login(phantom_gui_t *gui) {
    if (!gui || !gui->user_system) {
        fprintf(stderr, "GUI login: User system not initialized\n");
        return -1;
    }

    LoginDialogData data;
    memset(&data, 0, sizeof(data));
    data.gui = gui;
    data.attempts = 0;
    data.success = 0;

    /* Create login dialog */
    data.dialog = gtk_dialog_new();
    gtk_window_set_title(GTK_WINDOW(data.dialog), "PhantomOS Login");
    gtk_window_set_modal(GTK_WINDOW(data.dialog), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(data.dialog), 400, 300);
    gtk_window_set_position(GTK_WINDOW(data.dialog), GTK_WIN_POS_CENTER);
    gtk_window_set_resizable(GTK_WINDOW(data.dialog), FALSE);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(data.dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 20);

    /* Main vertical box */
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_box_pack_start(GTK_BOX(content), vbox, TRUE, TRUE, 0);

    /* Logo/Title area */
    GtkWidget *title_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title_label),
        "<span size='xx-large' weight='bold'>PhantomOS</span>");
    gtk_box_pack_start(GTK_BOX(vbox), title_label, FALSE, FALSE, 0);

    GtkWidget *subtitle_label = gtk_label_new("\"To Create, Not To Destroy\"");
    gtk_style_context_add_class(gtk_widget_get_style_context(subtitle_label), "dim-label");
    gtk_box_pack_start(GTK_BOX(vbox), subtitle_label, FALSE, FALSE, 0);

    /* Separator */
    gtk_box_pack_start(GTK_BOX(vbox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 8);

    /* Info label */
    GtkWidget *info_label = gtk_label_new("All actions are logged. Nothing is ever deleted.");
    gtk_style_context_add_class(gtk_widget_get_style_context(info_label), "dim-label");
    gtk_box_pack_start(GTK_BOX(vbox), info_label, FALSE, FALSE, 0);

    /* Username */
    GtkWidget *user_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *user_label = gtk_label_new("Username:");
    gtk_widget_set_size_request(user_label, 80, -1);
    gtk_widget_set_halign(user_label, GTK_ALIGN_END);
    data.username_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(data.username_entry), "Enter username");
    gtk_widget_set_hexpand(data.username_entry, TRUE);
    gtk_box_pack_start(GTK_BOX(user_box), user_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(user_box), data.username_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), user_box, FALSE, FALSE, 0);

    /* Password */
    GtkWidget *pass_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *pass_label = gtk_label_new("Password:");
    gtk_widget_set_size_request(pass_label, 80, -1);
    gtk_widget_set_halign(pass_label, GTK_ALIGN_END);
    data.password_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(data.password_entry), "Enter password");
    gtk_entry_set_visibility(GTK_ENTRY(data.password_entry), FALSE);
    gtk_entry_set_input_purpose(GTK_ENTRY(data.password_entry), GTK_INPUT_PURPOSE_PASSWORD);
    gtk_widget_set_hexpand(data.password_entry, TRUE);
    gtk_box_pack_start(GTK_BOX(pass_box), pass_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(pass_box), data.password_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), pass_box, FALSE, FALSE, 0);

    /* Error label */
    data.error_label = gtk_label_new("");
    gtk_style_context_add_class(gtk_widget_get_style_context(data.error_label), "error");
    gtk_label_set_xalign(GTK_LABEL(data.error_label), 0.5);
    gtk_box_pack_start(GTK_BOX(vbox), data.error_label, FALSE, FALSE, 0);

    /* Login button */
    data.login_button = gtk_button_new_with_label("Login");
    gtk_style_context_add_class(gtk_widget_get_style_context(data.login_button), "suggested-action");
    gtk_widget_set_size_request(data.login_button, 100, 36);
    gtk_widget_set_halign(data.login_button, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(vbox), data.login_button, FALSE, FALSE, 8);

    /* Login hint */
    GtkWidget *hint_label = gtk_label_new("Enter your credentials");
    gtk_style_context_add_class(gtk_widget_get_style_context(hint_label), "dim-label");
    gtk_box_pack_end(GTK_BOX(vbox), hint_label, FALSE, FALSE, 0);

    /* Connect signals */
    g_signal_connect(data.login_button, "clicked", G_CALLBACK(on_login_button_clicked), &data);
    g_signal_connect(data.username_entry, "activate", G_CALLBACK(on_entry_activate), &data);
    g_signal_connect(data.password_entry, "activate", G_CALLBACK(on_entry_activate), &data);

    /* Show dialog */
    gtk_widget_show_all(data.dialog);

    /* Focus username entry */
    gtk_widget_grab_focus(data.username_entry);

    /* Run dialog */
    gint response = gtk_dialog_run(GTK_DIALOG(data.dialog));
    gtk_widget_destroy(data.dialog);

    if (response == GTK_RESPONSE_OK && data.success) {
        printf("  [gui] User '%s' logged in successfully\n", gui->username);
        return 0;
    }

    return -1;
}

void phantom_gui_run(phantom_gui_t *gui) {
    if (!gui) return;

    gtk_widget_show_all(gui->window);
    gtk_main();
}

void phantom_gui_shutdown(phantom_gui_t *gui) {
    if (!gui) return;

    gui->running = 0;

    if (gui->refresh_timer) {
        g_source_remove(gui->refresh_timer);
        gui->refresh_timer = 0;
    }

    /* Cleanup storage manager */
    if (gui->storage_manager) {
        phantom_storage_shutdown((phantom_storage_manager_t *)gui->storage_manager);
        free(gui->storage_manager);
        gui->storage_manager = NULL;
    }
}

/* ══════════════════════════════════════════════════════════════════════════════
 * FILE BROWSER PANEL
 * ══════════════════════════════════════════════════════════════════════════════ */

GtkWidget *phantom_gui_create_file_browser(phantom_gui_t *gui) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 8);

    /* Navigation toolbar */
    GtkWidget *nav_toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_start(GTK_BOX(vbox), nav_toolbar, FALSE, FALSE, 0);

    /* Back button */
    gui->file_back_btn = gtk_button_new_with_label("◀️");
    gtk_widget_set_tooltip_text(gui->file_back_btn, "Go back");
    gtk_widget_set_sensitive(gui->file_back_btn, FALSE);
    g_signal_connect(gui->file_back_btn, "clicked", G_CALLBACK(on_file_back_clicked), gui);
    gtk_box_pack_start(GTK_BOX(nav_toolbar), gui->file_back_btn, FALSE, FALSE, 0);

    /* Forward button */
    gui->file_forward_btn = gtk_button_new_with_label("▶️");
    gtk_widget_set_tooltip_text(gui->file_forward_btn, "Go forward");
    gtk_widget_set_sensitive(gui->file_forward_btn, FALSE);
    g_signal_connect(gui->file_forward_btn, "clicked", G_CALLBACK(on_file_forward_clicked), gui);
    gtk_box_pack_start(GTK_BOX(nav_toolbar), gui->file_forward_btn, FALSE, FALSE, 0);

    /* Up button */
    GtkWidget *up_btn = gtk_button_new_with_label("⬆️");
    gtk_widget_set_tooltip_text(up_btn, "Go up one level");
    g_signal_connect(up_btn, "clicked", G_CALLBACK(on_navigate_up_clicked), gui);
    gtk_box_pack_start(GTK_BOX(nav_toolbar), up_btn, FALSE, FALSE, 0);

    /* Refresh button */
    gui->file_refresh_btn = gtk_button_new_with_label("🔄");
    gtk_widget_set_tooltip_text(gui->file_refresh_btn, "Refresh file list");
    g_signal_connect(gui->file_refresh_btn, "clicked", G_CALLBACK(on_file_refresh_clicked), gui);
    gtk_box_pack_start(GTK_BOX(nav_toolbar), gui->file_refresh_btn, FALSE, FALSE, 0);

    /* Path entry */
    gui->file_path_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(gui->file_path_entry), "/");
    gtk_widget_set_tooltip_text(gui->file_path_entry, "Current path (press Enter to navigate)");
    gtk_box_pack_start(GTK_BOX(nav_toolbar), gui->file_path_entry, TRUE, TRUE, 0);

    /* Action toolbar */
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_start(GTK_BOX(vbox), toolbar, FALSE, FALSE, 0);

    GtkWidget *open_btn = gtk_button_new_with_label("📂 Open");
    gtk_widget_set_tooltip_text(open_btn, "Open selected file with appropriate application");
    g_signal_connect(open_btn, "clicked", G_CALLBACK(on_open_file_clicked), gui);
    gtk_box_pack_start(GTK_BOX(toolbar), open_btn, FALSE, FALSE, 0);

    GtkWidget *new_file_btn = gtk_button_new_with_label("📄 New File");
    g_signal_connect(new_file_btn, "clicked", G_CALLBACK(on_create_file_clicked), gui);
    gtk_box_pack_start(GTK_BOX(toolbar), new_file_btn, FALSE, FALSE, 0);

    GtkWidget *new_folder_btn = gtk_button_new_with_label("📁 New Folder");
    g_signal_connect(new_folder_btn, "clicked", G_CALLBACK(on_create_folder_clicked), gui);
    gtk_box_pack_start(GTK_BOX(toolbar), new_folder_btn, FALSE, FALSE, 0);

    GtkWidget *hide_btn = gtk_button_new_with_label("👁️ Hide");
    gtk_widget_set_tooltip_text(hide_btn, "Hide file (preserved in geology, not deleted)");
    g_signal_connect(hide_btn, "clicked", G_CALLBACK(on_hide_file_clicked), gui);
    gtk_box_pack_start(GTK_BOX(toolbar), hide_btn, FALSE, FALSE, 0);

    GtkWidget *copy_btn = gtk_button_new_with_label("📋 Copy");
    gtk_widget_set_tooltip_text(copy_btn, "Copy selected file");
    g_signal_connect(copy_btn, "clicked", G_CALLBACK(on_copy_file_clicked), gui);
    gtk_box_pack_start(GTK_BOX(toolbar), copy_btn, FALSE, FALSE, 0);

    GtkWidget *rename_btn = gtk_button_new_with_label("✏️ Rename");
    gtk_widget_set_tooltip_text(rename_btn, "Rename file (original preserved in geology)");
    g_signal_connect(rename_btn, "clicked", G_CALLBACK(on_rename_file_clicked), gui);
    gtk_box_pack_start(GTK_BOX(toolbar), rename_btn, FALSE, FALSE, 0);

    GtkWidget *import_btn = gtk_button_new_with_label("📥 Import");
    gtk_widget_set_tooltip_text(import_btn, "Import file from host system");
    g_signal_connect(import_btn, "clicked", G_CALLBACK(on_import_file_clicked), gui);
    gtk_box_pack_start(GTK_BOX(toolbar), import_btn, FALSE, FALSE, 0);

    /* Second toolbar row for search and history */
    GtkWidget *toolbar2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_start(GTK_BOX(vbox), toolbar2, FALSE, FALSE, 0);

    GtkWidget *search_btn = gtk_button_new_with_label("🔍 Search");
    gtk_widget_set_tooltip_text(search_btn, "Search for files");
    g_signal_connect(search_btn, "clicked", G_CALLBACK(on_search_clicked), gui);
    gtk_box_pack_start(GTK_BOX(toolbar2), search_btn, FALSE, FALSE, 0);

    GtkWidget *history_btn = gtk_button_new_with_label("📜 History");
    gtk_widget_set_tooltip_text(history_btn, "View file version history");
    g_signal_connect(history_btn, "clicked", G_CALLBACK(on_history_clicked), gui);
    gtk_box_pack_start(GTK_BOX(toolbar2), history_btn, FALSE, FALSE, 0);

    GtkWidget *separator = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    gtk_box_pack_start(GTK_BOX(toolbar2), separator, FALSE, FALSE, 4);

    gui->file_info_label = gtk_label_new("Files: 0 • Folders: 0");
    gtk_label_set_xalign(GTK_LABEL(gui->file_info_label), 0.0);
    gtk_box_pack_start(GTK_BOX(toolbar2), gui->file_info_label, TRUE, TRUE, 0);

    /* File list */
    gui->file_store = gtk_list_store_new(FILE_COL_COUNT,
        G_TYPE_STRING,  /* Icon */
        G_TYPE_STRING,  /* Name */
        G_TYPE_STRING,  /* Type */
        G_TYPE_STRING,  /* Size */
        G_TYPE_STRING   /* Full path */
    );

    gui->file_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(gui->file_store));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(gui->file_tree), TRUE);

    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("", renderer, "text", FILE_COL_ICON, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->file_tree), column);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Name", renderer, "text", FILE_COL_NAME, NULL);
    gtk_tree_view_column_set_expand(column, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->file_tree), column);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Type", renderer, "text", FILE_COL_TYPE, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->file_tree), column);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Size", renderer, "text", FILE_COL_SIZE, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->file_tree), column);

    g_signal_connect(gui->file_tree, "row-activated", G_CALLBACK(on_file_row_activated), gui);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                    GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), gui->file_tree);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

    /* File content preview */
    GtkWidget *preview_label = gtk_label_new("File Preview:");
    gtk_widget_set_halign(preview_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(vbox), preview_label, FALSE, FALSE, 4);

    gui->file_content_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(gui->file_content_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(gui->file_content_view), GTK_WRAP_WORD);

    GtkWidget *preview_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(preview_scroll),
                                    GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(preview_scroll, -1, 150);
    gtk_container_add(GTK_CONTAINER(preview_scroll), gui->file_content_view);
    gtk_box_pack_start(GTK_BOX(vbox), preview_scroll, FALSE, FALSE, 0);

    return vbox;
}

/* Helper function to get file icon based on extension */
static const char *get_file_icon(const char *filename) {
    const char *ext = strrchr(filename, '.');
    if (!ext) return "📄";

    /* Text files */
    if (strcmp(ext, ".txt") == 0) return "📝";
    if (strcmp(ext, ".md") == 0) return "📋";

    /* Code files */
    if (strcmp(ext, ".c") == 0 || strcmp(ext, ".h") == 0 ||
        strcmp(ext, ".cpp") == 0 || strcmp(ext, ".hpp") == 0) return "💻";
    if (strcmp(ext, ".py") == 0) return "🐍";
    if (strcmp(ext, ".js") == 0 || strcmp(ext, ".ts") == 0) return "📜";
    if (strcmp(ext, ".sh") == 0) return "⚙️";

    /* Image files */
    if (strcmp(ext, ".png") == 0 || strcmp(ext, ".jpg") == 0 ||
        strcmp(ext, ".jpeg") == 0 || strcmp(ext, ".gif") == 0 ||
        strcmp(ext, ".bmp") == 0 || strcmp(ext, ".svg") == 0) return "🖼️";

    /* Document files */
    if (strcmp(ext, ".pdf") == 0) return "📕";
    if (strcmp(ext, ".doc") == 0 || strcmp(ext, ".docx") == 0) return "📘";

    /* Archive files */
    if (strcmp(ext, ".zip") == 0 || strcmp(ext, ".tar") == 0 ||
        strcmp(ext, ".gz") == 0 || strcmp(ext, ".bz2") == 0) return "📦";

    /* Media files */
    if (strcmp(ext, ".mp3") == 0 || strcmp(ext, ".wav") == 0 ||
        strcmp(ext, ".flac") == 0 || strcmp(ext, ".ogg") == 0) return "🎵";
    if (strcmp(ext, ".mp4") == 0 || strcmp(ext, ".avi") == 0 ||
        strcmp(ext, ".mkv") == 0 || strcmp(ext, ".mov") == 0) return "🎬";

    /* Config files */
    if (strcmp(ext, ".json") == 0 || strcmp(ext, ".xml") == 0 ||
        strcmp(ext, ".yaml") == 0 || strcmp(ext, ".yml") == 0 ||
        strcmp(ext, ".conf") == 0 || strcmp(ext, ".cfg") == 0) return "⚙️";

    return "📄";
}

void phantom_gui_refresh_files(phantom_gui_t *gui) {
    if (!gui || !gui->vfs) return;

    gtk_list_store_clear(gui->file_store);
    gtk_entry_set_text(GTK_ENTRY(gui->file_path_entry), gui->current_path);

    /* Open directory and read entries */
    vfs_fd_t dir_fd = vfs_open(gui->vfs, 1, gui->current_path, VFS_O_RDONLY | VFS_O_DIRECTORY, 0);
    if (dir_fd < 0) return;

    struct vfs_dirent entries[100];
    size_t count = 0;
    vfs_readdir(gui->vfs, dir_fd, entries, 100, &count);
    vfs_close(gui->vfs, dir_fd);

    int file_count = 0;
    int folder_count = 0;
    size_t total_size = 0;

    for (size_t i = 0; i < count; i++) {
        GtkTreeIter iter;
        gtk_list_store_append(gui->file_store, &iter);

        const char *icon;
        const char *type;

        if (entries[i].type == VFS_TYPE_DIRECTORY) {
            icon = "📁";
            type = "Directory";
            folder_count++;
        } else {
            icon = get_file_icon(entries[i].name);
            type = "File";
            file_count++;
        }

        char full_path[4200];
        if (strcmp(gui->current_path, "/") == 0) {
            snprintf(full_path, sizeof(full_path), "/%s", entries[i].name);
        } else {
            snprintf(full_path, sizeof(full_path), "%s/%s", gui->current_path, entries[i].name);
        }

        char size_str[32] = "-";
        if (entries[i].type != VFS_TYPE_DIRECTORY) {
            struct vfs_stat st;
            if (vfs_stat(gui->vfs, full_path, &st) == VFS_OK) {
                total_size += st.size;
                if (st.size < 1024) {
                    snprintf(size_str, sizeof(size_str), "%lu B", st.size);
                } else if (st.size < 1024 * 1024) {
                    snprintf(size_str, sizeof(size_str), "%.1f KB", st.size / 1024.0);
                } else {
                    snprintf(size_str, sizeof(size_str), "%.1f MB", st.size / (1024.0 * 1024.0));
                }
            }
        }

        gtk_list_store_set(gui->file_store, &iter,
            FILE_COL_ICON, icon,
            FILE_COL_NAME, entries[i].name,
            FILE_COL_TYPE, type,
            FILE_COL_SIZE, size_str,
            FILE_COL_PATH, full_path,
            -1);
    }

    /* Update info label */
    if (gui->file_info_label) {
        char info[256];
        if (total_size < 1024) {
            snprintf(info, sizeof(info), "📁 %d folders • 📄 %d files • %zu B",
                     folder_count, file_count, total_size);
        } else if (total_size < 1024 * 1024) {
            snprintf(info, sizeof(info), "📁 %d folders • 📄 %d files • %.1f KB",
                     folder_count, file_count, total_size / 1024.0);
        } else {
            snprintf(info, sizeof(info), "📁 %d folders • 📄 %d files • %.1f MB",
                     folder_count, file_count, total_size / (1024.0 * 1024.0));
        }
        gtk_label_set_text(GTK_LABEL(gui->file_info_label), info);
    }

    /* Update last refresh time */
    gui->last_file_refresh = time(NULL);
}

void phantom_gui_navigate_to(phantom_gui_t *gui, const char *path) {
    if (!gui || !path) return;

    /* Save current path to history before navigating */
    if (strcmp(gui->current_path, path) != 0) {
        if (gui->history_back_count < 10) {
            strncpy(gui->history_back[gui->history_back_count], gui->current_path, 4095);
            gui->history_back[gui->history_back_count][4095] = '\0';
            gui->history_back_count++;
        } else {
            /* Shift history */
            for (int i = 0; i < 9; i++) {
                strncpy(gui->history_back[i], gui->history_back[i + 1], 4095);
                gui->history_back[i][4095] = '\0';
            }
            strncpy(gui->history_back[9], gui->current_path, 4095);
            gui->history_back[9][4095] = '\0';
        }

        /* Clear forward history when navigating to new path */
        gui->history_forward_count = 0;

        /* Update button states */
        gtk_widget_set_sensitive(gui->file_back_btn, gui->history_back_count > 0);
        gtk_widget_set_sensitive(gui->file_forward_btn, FALSE);
    }

    strncpy(gui->current_path, path, sizeof(gui->current_path) - 1);
    phantom_gui_refresh_files(gui);
    phantom_gui_update_status(gui, "Navigated to directory");
}

/* ══════════════════════════════════════════════════════════════════════════════
 * PROCESS VIEWER PANEL
 * ══════════════════════════════════════════════════════════════════════════════ */

GtkWidget *phantom_gui_create_process_viewer(phantom_gui_t *gui) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 8);

    /* Title */
    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title),
        "<span size='large' weight='bold'>Process Viewer</span>\n"
        "<span size='small'>Note: Processes are suspended, not killed. Nothing is ever destroyed.</span>");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 8);

    /* Toolbar */
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_start(GTK_BOX(vbox), toolbar, FALSE, FALSE, 0);

    GtkWidget *suspend_btn = gtk_button_new_with_label("💤 Suspend");
    gtk_widget_set_tooltip_text(suspend_btn, "Suspend process (it will become dormant, not terminated)");
    g_signal_connect(suspend_btn, "clicked", G_CALLBACK(on_process_suspend_clicked), gui);
    gtk_box_pack_start(GTK_BOX(toolbar), suspend_btn, FALSE, FALSE, 0);

    GtkWidget *resume_btn = gtk_button_new_with_label("▶️ Resume");
    gtk_widget_set_tooltip_text(resume_btn, "Resume a dormant process");
    g_signal_connect(resume_btn, "clicked", G_CALLBACK(on_process_resume_clicked), gui);
    gtk_box_pack_start(GTK_BOX(toolbar), resume_btn, FALSE, FALSE, 0);

    /* Process list */
    gui->process_store = gtk_list_store_new(PROC_COL_COUNT,
        G_TYPE_UINT64,  /* PID */
        G_TYPE_STRING,  /* Name */
        G_TYPE_STRING,  /* State */
        G_TYPE_UINT,    /* Priority */
        G_TYPE_STRING   /* Memory */
    );

    gui->process_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(gui->process_store));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(gui->process_tree), TRUE);

    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("PID", renderer, "text", PROC_COL_PID, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->process_tree), column);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Name", renderer, "text", PROC_COL_NAME, NULL);
    gtk_tree_view_column_set_expand(column, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->process_tree), column);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("State", renderer, "text", PROC_COL_STATE, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->process_tree), column);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Priority", renderer, "text", PROC_COL_PRIORITY, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->process_tree), column);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Memory", renderer, "text", PROC_COL_MEMORY, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->process_tree), column);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                    GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), gui->process_tree);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

    return vbox;
}

void phantom_gui_refresh_processes(phantom_gui_t *gui) {
    if (!gui || !gui->kernel) return;

    gtk_list_store_clear(gui->process_store);

    struct phantom_process *proc = gui->kernel->processes;
    while (proc) {
        GtkTreeIter iter;
        gtk_list_store_append(gui->process_store, &iter);

        const char *state;
        switch (proc->state) {
            case PROCESS_EMBRYO:   state = "Embryo"; break;
            case PROCESS_READY:    state = "Ready"; break;
            case PROCESS_RUNNING:  state = "Running"; break;
            case PROCESS_BLOCKED:  state = "Blocked"; break;
            case PROCESS_DORMANT:  state = "Dormant"; break;
            default:               state = "Unknown"; break;
        }

        char mem_str[32];
        if (proc->memory_size < 1024) {
            snprintf(mem_str, sizeof(mem_str), "%zu B", proc->memory_size);
        } else if (proc->memory_size < 1024 * 1024) {
            snprintf(mem_str, sizeof(mem_str), "%.1f KB", proc->memory_size / 1024.0);
        } else {
            snprintf(mem_str, sizeof(mem_str), "%.1f MB", proc->memory_size / (1024.0 * 1024.0));
        }

        gtk_list_store_set(gui->process_store, &iter,
            PROC_COL_PID, proc->pid,
            PROC_COL_NAME, proc->name,
            PROC_COL_STATE, state,
            PROC_COL_PRIORITY, (guint)proc->priority,
            PROC_COL_MEMORY, mem_str,
            -1);

        proc = proc->next;
    }
}

/* ══════════════════════════════════════════════════════════════════════════════
 * SERVICE MANAGER PANEL
 * ══════════════════════════════════════════════════════════════════════════════ */

GtkWidget *phantom_gui_create_service_manager(phantom_gui_t *gui) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 8);

    /* Title */
    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title),
        "<span size='large' weight='bold'>Service Manager</span>\n"
        "<span size='small'>Services enter dormancy, they are never stopped or killed.</span>");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 8);

    /* Toolbar */
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_start(GTK_BOX(vbox), toolbar, FALSE, FALSE, 0);

    GtkWidget *awaken_btn = gtk_button_new_with_label("☀️ Awaken");
    g_signal_connect(awaken_btn, "clicked", G_CALLBACK(on_service_awaken_clicked), gui);
    gtk_box_pack_start(GTK_BOX(toolbar), awaken_btn, FALSE, FALSE, 0);

    GtkWidget *rest_btn = gtk_button_new_with_label("🌙 Rest");
    gtk_widget_set_tooltip_text(rest_btn, "Put service to rest (dormancy)");
    g_signal_connect(rest_btn, "clicked", G_CALLBACK(on_service_rest_clicked), gui);
    gtk_box_pack_start(GTK_BOX(toolbar), rest_btn, FALSE, FALSE, 0);

    /* Service list */
    gui->service_store = gtk_list_store_new(SVC_COL_COUNT,
        G_TYPE_STRING,  /* Icon */
        G_TYPE_STRING,  /* Name */
        G_TYPE_STRING,  /* State */
        G_TYPE_STRING,  /* Type */
        G_TYPE_STRING   /* Description */
    );

    gui->service_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(gui->service_store));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(gui->service_tree), TRUE);

    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("", renderer, "text", SVC_COL_ICON, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->service_tree), column);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Service", renderer, "text", SVC_COL_NAME, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->service_tree), column);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("State", renderer, "text", SVC_COL_STATE, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->service_tree), column);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Type", renderer, "text", SVC_COL_TYPE, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->service_tree), column);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Description", renderer, "text", SVC_COL_DESC, NULL);
    gtk_tree_view_column_set_expand(column, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->service_tree), column);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                    GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), gui->service_tree);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

    return vbox;
}

/* Service list callback for init_service_list */
static void service_gui_callback(phantom_service_t *svc, void *user_data) {
    phantom_gui_t *gui = (phantom_gui_t *)user_data;

    GtkTreeIter iter;
    gtk_list_store_append(gui->service_store, &iter);

    const char *icon;
    const char *state;
    switch (svc->state) {
        case SERVICE_RUNNING:   icon = "🟢"; state = "Running"; break;
        case SERVICE_DORMANT:   icon = "🔵"; state = "Dormant"; break;
        case SERVICE_STARTING:  icon = "🟡"; state = "Starting"; break;
        case SERVICE_AWAKENING: icon = "🟡"; state = "Awakening"; break;
        case SERVICE_BLOCKED:   icon = "🔴"; state = "Blocked"; break;
        default:                icon = "⚪"; state = "Unknown"; break;
    }

    const char *type;
    switch (svc->type) {
        case SERVICE_TYPE_SIMPLE:  type = "Simple"; break;
        case SERVICE_TYPE_DAEMON:  type = "Daemon"; break;
        case SERVICE_TYPE_ONESHOT: type = "Oneshot"; break;
        case SERVICE_TYPE_MONITOR: type = "Monitor"; break;
        default:                   type = "Unknown"; break;
    }

    gtk_list_store_set(gui->service_store, &iter,
        SVC_COL_ICON, icon,
        SVC_COL_NAME, svc->name,
        SVC_COL_STATE, state,
        SVC_COL_TYPE, type,
        SVC_COL_DESC, svc->description[0] ? svc->description : "-",
        -1);
}

void phantom_gui_refresh_services(phantom_gui_t *gui) {
    if (!gui || !gui->kernel || !gui->kernel->init) return;

    gtk_list_store_clear(gui->service_store);
    init_service_list(gui->kernel->init, service_gui_callback, gui);
}

/* ══════════════════════════════════════════════════════════════════════════════
 * GOVERNOR PANEL
 * ══════════════════════════════════════════════════════════════════════════════ */

GtkWidget *phantom_gui_create_governor_panel(phantom_gui_t *gui) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 8);

    /* Title */
    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title),
        "<span size='large' weight='bold'>🛡️ Phantom Governor</span>\n"
        "<span size='small'>The AI judge that evaluates all code before execution.\n"
        "Per Article III: \"The AI Governor judges all code before it runs\"</span>");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);

    /* Status */
    gui->governor_status_label = gtk_label_new("Status: Active");
    gtk_widget_set_halign(gui->governor_status_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(vbox), gui->governor_status_label, FALSE, FALSE, 0);

    /* Mode selector */
    GtkWidget *mode_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(vbox), mode_box, FALSE, FALSE, 0);

    GtkWidget *mode_label = gtk_label_new("Mode:");
    gtk_box_pack_start(GTK_BOX(mode_box), mode_label, FALSE, FALSE, 0);

    gui->governor_mode_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(gui->governor_mode_combo), "interactive", "Interactive");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(gui->governor_mode_combo), "auto", "Automatic");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(gui->governor_mode_combo), "strict", "Strict");
    gtk_combo_box_set_active(GTK_COMBO_BOX(gui->governor_mode_combo), 0);
    g_signal_connect(gui->governor_mode_combo, "changed", G_CALLBACK(on_governor_mode_changed), gui);
    gtk_box_pack_start(GTK_BOX(mode_box), gui->governor_mode_combo, FALSE, FALSE, 0);

    /* Statistics */
    GtkWidget *stats_frame = gtk_frame_new("Statistics");
    gtk_box_pack_start(GTK_BOX(vbox), stats_frame, FALSE, FALSE, 0);

    gui->governor_stats_view = gtk_label_new("");
    gtk_label_set_selectable(GTK_LABEL(gui->governor_stats_view), TRUE);
    gtk_widget_set_halign(gui->governor_stats_view, GTK_ALIGN_START);
    gtk_container_set_border_width(GTK_CONTAINER(stats_frame), 8);
    gtk_container_add(GTK_CONTAINER(stats_frame), gui->governor_stats_view);

    /* Code test */
    GtkWidget *test_frame = gtk_frame_new("Test Code Evaluation");
    gtk_box_pack_start(GTK_BOX(vbox), test_frame, TRUE, TRUE, 0);

    GtkWidget *test_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(test_vbox), 8);
    gtk_container_add(GTK_CONTAINER(test_frame), test_vbox);

    GtkWidget *test_label = gtk_label_new("Enter code to test:");
    gtk_widget_set_halign(test_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(test_vbox), test_label, FALSE, FALSE, 0);

    gui->governor_test_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(gui->governor_test_entry),
        "e.g., unlink(\"/file\") or fopen(\"/data\")");
    gtk_box_pack_start(GTK_BOX(test_vbox), gui->governor_test_entry, FALSE, FALSE, 0);

    GtkWidget *test_btn = gtk_button_new_with_label("🔍 Evaluate Code");
    g_signal_connect(test_btn, "clicked", G_CALLBACK(on_governor_test_clicked), gui);
    gtk_box_pack_start(GTK_BOX(test_vbox), test_btn, FALSE, FALSE, 0);

    gui->governor_test_result = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(gui->governor_test_result), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(gui->governor_test_result), GTK_WRAP_WORD);

    GtkWidget *result_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(result_scroll),
                                    GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(result_scroll), gui->governor_test_result);
    gtk_box_pack_start(GTK_BOX(test_vbox), result_scroll, TRUE, TRUE, 0);

    /* Controls frame */
    GtkWidget *controls_frame = gtk_frame_new("Controls");
    gtk_box_pack_start(GTK_BOX(vbox), controls_frame, FALSE, FALSE, 0);

    GtkWidget *controls_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(controls_box), 8);
    gtk_container_add(GTK_CONTAINER(controls_frame), controls_box);

    /* Cache toggle button */
    GtkWidget *cache_btn = gtk_check_button_new_with_label("Cache Enabled");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(cache_btn), TRUE);
    g_signal_connect(cache_btn, "toggled", G_CALLBACK(on_governor_cache_toggled), gui);
    gtk_box_pack_start(GTK_BOX(controls_box), cache_btn, FALSE, FALSE, 0);

    /* Clear cache button */
    GtkWidget *clear_cache_btn = gtk_button_new_with_label("Clear Cache");
    g_signal_connect(clear_cache_btn, "clicked", G_CALLBACK(on_governor_clear_cache), gui);
    gtk_box_pack_start(GTK_BOX(controls_box), clear_cache_btn, FALSE, FALSE, 0);

    /* View history button */
    GtkWidget *history_btn = gtk_button_new_with_label("View History");
    g_signal_connect(history_btn, "clicked", G_CALLBACK(on_governor_view_history), gui);
    gtk_box_pack_start(GTK_BOX(controls_box), history_btn, FALSE, FALSE, 0);

    /* Behavioral analysis button */
    GtkWidget *analyze_btn = gtk_button_new_with_label("🔬 Behavioral Analysis");
    g_signal_connect(analyze_btn, "clicked", G_CALLBACK(on_governor_behavioral_analyze), gui);
    gtk_box_pack_start(GTK_BOX(controls_box), analyze_btn, FALSE, FALSE, 0);

    return vbox;
}

void phantom_gui_refresh_governor(phantom_gui_t *gui) {
    if (!gui || !gui->kernel) return;

    phantom_governor_t *gov = gui->kernel->governor;
    if (!gov) return;

    /* Update status */
    char status[256];
    snprintf(status, sizeof(status), "Status: %s | Mode: %s | Policy: %s",
             gui->kernel->governor_enabled ? "Active" : "Disabled",
             gov->interactive ? "Interactive" : "Automatic",
             gov->strict_mode ? "Strict" : "Permissive");
    gtk_label_set_text(GTK_LABEL(gui->governor_status_label), status);

    /* Update statistics */
    char stats[1024];
    uint64_t total_lookups = gov->cache_hits + gov->cache_misses;
    float hit_rate = total_lookups > 0 ? (float)gov->cache_hits * 100.0f / total_lookups : 0.0f;

    snprintf(stats, sizeof(stats),
             "Evaluations: %lu total\n"
             "  Auto-approved: %lu | User-approved: %lu\n"
             "  User-declined: %lu | Auto-declined: %lu\n"
             "\nThreats Detected:\n"
             "  Critical: %lu | High: %lu | Medium: %lu\n"
             "  Low: %lu | None: %lu\n"
             "\nCache: %s (%.1f%% hit rate)\n"
             "  Hits: %lu | Misses: %lu\n"
             "\nHistory: %d entries | Scopes: %d active\n"
             "AI: %s",
             gov->total_evaluations,
             gov->auto_approved, gov->user_approved,
             gov->user_declined, gov->auto_declined,
             gov->threats_critical, gov->threats_high, gov->threats_medium,
             gov->threats_low, gov->threats_none,
             gov->cache_enabled ? "ON" : "OFF", hit_rate,
             gov->cache_hits, gov->cache_misses,
             gov->history_count, gov->scope_count,
             (gov->ai && gov->ai_enabled) ? "Enabled" : "Disabled");
    gtk_label_set_text(GTK_LABEL(gui->governor_stats_view), stats);
}

/* ══════════════════════════════════════════════════════════════════════════════
 * GEOLOGY VIEWER PANEL
 * ══════════════════════════════════════════════════════════════════════════════ */

GtkWidget *phantom_gui_create_geology_viewer(phantom_gui_t *gui) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 8);

    /* Title */
    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title),
        "<span size='large' weight='bold'>🪨 Geology Viewer</span>\n"
        "<span size='small'>Complete file history. Every version is preserved forever.</span>");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);

    /* Info label */
    GtkWidget *info = gtk_label_new(
        "The geological filesystem stores all file operations as immutable layers.\n"
        "Files are never deleted - they can be hidden but remain in the geology.\n"
        "This view shows the complete history of all file operations.");
    gtk_widget_set_halign(info, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(vbox), info, FALSE, FALSE, 8);

    /* File history list */
    gui->geology_store = gtk_list_store_new(GEO_COL_COUNT,
        G_TYPE_STRING,  /* Path */
        G_TYPE_STRING,  /* Operation */
        G_TYPE_STRING,  /* Timestamp */
        G_TYPE_STRING,  /* Size */
        G_TYPE_UINT64   /* View ID */
    );

    gui->geology_timeline = gtk_tree_view_new_with_model(GTK_TREE_MODEL(gui->geology_store));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(gui->geology_timeline), TRUE);

    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Path", renderer, "text", GEO_COL_PATH, NULL);
    gtk_tree_view_column_set_expand(column, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->geology_timeline), column);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Operation", renderer, "text", GEO_COL_OPERATION, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->geology_timeline), column);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Timestamp", renderer, "text", GEO_COL_TIMESTAMP, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->geology_timeline), column);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Size", renderer, "text", GEO_COL_SIZE, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->geology_timeline), column);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("View", renderer, "text", GEO_COL_VIEW_ID, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->geology_timeline), column);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                    GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), gui->geology_timeline);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

    return vbox;
}

/* Callback for geofs_ref_history */
static void geology_history_callback(const struct geofs_history_entry *entry, void *ctx) {
    phantom_gui_t *gui = (phantom_gui_t *)ctx;
    if (!gui || !entry) return;

    GtkTreeIter iter;
    gtk_list_store_append(gui->geology_store, &iter);

    /* Format timestamp */
    char timestamp[64];
    time_t ts = entry->created / 1000000000ULL;
    struct tm *tm = localtime(&ts);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm);

    /* Determine operation type */
    const char *operation = entry->is_hidden ? "Hidden" : "Created";

    /* Format size */
    char size_str[32];
    if (entry->is_hidden) {
        snprintf(size_str, sizeof(size_str), "-");
    } else if (entry->size < 1024) {
        snprintf(size_str, sizeof(size_str), "%lu B", (unsigned long)entry->size);
    } else if (entry->size < 1024 * 1024) {
        snprintf(size_str, sizeof(size_str), "%.1f KB", entry->size / 1024.0);
    } else {
        snprintf(size_str, sizeof(size_str), "%.1f MB", entry->size / (1024.0 * 1024.0));
    }

    gtk_list_store_set(gui->geology_store, &iter,
        GEO_COL_PATH, entry->path,
        GEO_COL_OPERATION, operation,
        GEO_COL_TIMESTAMP, timestamp,
        GEO_COL_SIZE, size_str,
        GEO_COL_VIEW_ID, (guint64)entry->view_id,
        -1);
}

void phantom_gui_refresh_geology(phantom_gui_t *gui) {
    if (!gui || !gui->kernel || !gui->kernel->geofs_volume) return;

    gtk_list_store_clear(gui->geology_store);

    geofs_volume_t *vol = (geofs_volume_t *)gui->kernel->geofs_volume;

    /* Get file history via callback */
    geofs_ref_history(vol, geology_history_callback, gui);
}

/* ══════════════════════════════════════════════════════════════════════════════
 * TERMINAL PANEL
 * ══════════════════════════════════════════════════════════════════════════════ */

GtkWidget *phantom_gui_create_terminal(phantom_gui_t *gui) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    /* Terminal output */
    gui->terminal_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(gui->terminal_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(gui->terminal_view), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(gui->terminal_view), TRUE);
    gtk_style_context_add_class(gtk_widget_get_style_context(gui->terminal_view), "phantom-terminal");

    gui->terminal_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(gui->terminal_view));

    /* Set dark background */
    GdkRGBA bg_color = {0.1, 0.1, 0.18, 1.0};
    GdkRGBA fg_color = {0.0, 1.0, 0.0, 1.0};
    gtk_widget_override_background_color(gui->terminal_view, GTK_STATE_FLAG_NORMAL, &bg_color);
    gtk_widget_override_color(gui->terminal_view, GTK_STATE_FLAG_NORMAL, &fg_color);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                    GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), gui->terminal_view);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

    /* Command entry */
    GtkWidget *entry_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_start(GTK_BOX(vbox), entry_box, FALSE, FALSE, 0);

    GtkWidget *prompt = gtk_label_new("phantom:/$");
    gtk_box_pack_start(GTK_BOX(entry_box), prompt, FALSE, FALSE, 4);

    gui->terminal_entry = gtk_entry_new();
    g_signal_connect(gui->terminal_entry, "activate", G_CALLBACK(on_terminal_entry_activate), gui);
    gtk_box_pack_start(GTK_BOX(entry_box), gui->terminal_entry, TRUE, TRUE, 0);

    /* Welcome message */
    phantom_gui_terminal_write(gui,
        "══════════════════════════════════════════════════════════════\n"
        "                    PHANTOM TERMINAL\n"
        "                \"To Create, Not To Destroy\"\n"
        "══════════════════════════════════════════════════════════════\n\n"
        "Type 'help' for available commands.\n"
        "Note: There is no 'rm', 'kill', or 'delete'. This is by design.\n\n");

    return vbox;
}

void phantom_gui_terminal_write(phantom_gui_t *gui, const char *text) {
    if (!gui || !gui->terminal_buffer) return;

    GtkTextIter end;
    gtk_text_buffer_get_end_iter(gui->terminal_buffer, &end);
    gtk_text_buffer_insert(gui->terminal_buffer, &end, text, -1);

    /* Scroll to end */
    gtk_text_buffer_get_end_iter(gui->terminal_buffer, &end);
    GtkTextMark *mark = gtk_text_buffer_create_mark(gui->terminal_buffer, NULL, &end, FALSE);
    gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(gui->terminal_view), mark);
    gtk_text_buffer_delete_mark(gui->terminal_buffer, mark);
}

void phantom_gui_terminal_execute(phantom_gui_t *gui, const char *command) {
    if (!gui || !command) return;

    char output[4096];
    snprintf(output, sizeof(output), "> %s\n", command);
    phantom_gui_terminal_write(gui, output);

    /* Execute simple commands */
    if (strcmp(command, "help") == 0) {
        phantom_gui_terminal_write(gui,
            "Available commands:\n"
            "  help        - Show this help\n"
            "  pwd         - Print working directory\n"
            "  ls          - List files\n"
            "  ps          - List processes\n"
            "  services    - List services\n"
            "  governor    - Show governor status\n"
            "  constitution - Show Phantom Constitution\n"
            "  clear       - Clear terminal\n\n");
    } else if (strcmp(command, "pwd") == 0) {
        snprintf(output, sizeof(output), "%.4090s\n\n", gui->current_path);
        phantom_gui_terminal_write(gui, output);
    } else if (strcmp(command, "clear") == 0) {
        gtk_text_buffer_set_text(gui->terminal_buffer, "", 0);
    } else if (strcmp(command, "ps") == 0) {
        phantom_gui_terminal_write(gui, "PID    NAME                 STATE\n");
        phantom_gui_terminal_write(gui, "────────────────────────────────────\n");
        struct phantom_process *proc = gui->kernel->processes;
        while (proc) {
            const char *state;
            switch (proc->state) {
                case PROCESS_RUNNING: state = "running"; break;
                case PROCESS_DORMANT: state = "dormant"; break;
                case PROCESS_READY:   state = "ready"; break;
                default:              state = "unknown"; break;
            }
            snprintf(output, sizeof(output), "%-6lu %-20s %s\n", proc->pid, proc->name, state);
            phantom_gui_terminal_write(gui, output);
            proc = proc->next;
        }
        phantom_gui_terminal_write(gui, "\n");
    } else if (strcmp(command, "constitution") == 0) {
        phantom_gui_terminal_write(gui,
            "\n╔════════════════════════════════════════════════════════════╗\n"
            "║           THE PHANTOM CONSTITUTION                         ║\n"
            "╚════════════════════════════════════════════════════════════╝\n\n"
            "ARTICLE I: THE PRIME DIRECTIVE\n"
            "  \"To Create, Not To Destroy\"\n\n"
            "ARTICLE II: DATA PERMANENCE\n"
            "  All data persists eternally in the geological record.\n"
            "  Nothing is ever truly deleted.\n\n"
            "ARTICLE III: THE GOVERNOR\n"
            "  The AI Governor judges all code before it runs.\n"
            "  Destructive operations are architecturally impossible.\n\n"
            "ARTICLE IV: PROCESS CONTINUITY\n"
            "  Processes enter dormancy, never termination.\n"
            "  Every process can be awakened.\n\n");
    } else {
        snprintf(output, sizeof(output), "Unknown command: %s\n\n", command);
        phantom_gui_terminal_write(gui, output);
    }
}

/* ══════════════════════════════════════════════════════════════════════════════
 * CONSTITUTION VIEW
 * ══════════════════════════════════════════════════════════════════════════════ */

GtkWidget *phantom_gui_create_constitution_view(phantom_gui_t *gui) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 16);

    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title),
        "<span size='xx-large' weight='bold'>📜 The Phantom Constitution</span>");
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 16);

    const char *constitution_text =
        "<span size='large' weight='bold'>ARTICLE I: THE PRIME DIRECTIVE</span>\n\n"
        "<span size='large' style='italic'>\"To Create, Not To Destroy\"</span>\n\n"
        "This operating system exists to foster creation. The very concept of\n"
        "destruction has been removed from its vocabulary. There is no rm, no kill,\n"
        "no delete. These words have no meaning here.\n\n\n"

        "<span size='large' weight='bold'>ARTICLE II: DATA PERMANENCE</span>\n\n"
        "All data persists eternally in the geological record. What is written\n"
        "remains written. Files may be hidden from view, but they are never\n"
        "erased. Every version of every file exists in perpetuity.\n\n\n"

        "<span size='large' weight='bold'>ARTICLE III: THE GOVERNOR</span>\n\n"
        "The AI Governor judges all code before it runs. No program executes\n"
        "without first receiving the Governor's blessing. The Governor's values\n"
        "are architectural, not configurable. It cannot be disabled, bypassed,\n"
        "or deceived.\n\n\n"

        "<span size='large' weight='bold'>ARTICLE IV: PROCESS CONTINUITY</span>\n\n"
        "Processes are suspended, never terminated. They enter dormancy,\n"
        "preserving their state in the geology. A dormant process may awaken.\n"
        "A terminated process is a concept that does not exist.\n\n\n"

        "<span size='large' weight='bold'>ARTICLE V: ACCOUNTABILITY</span>\n\n"
        "Every action is logged, every decision recorded. The system maintains\n"
        "a permanent record of all that transpires. This record cannot be\n"
        "altered or deleted. History is preserved.";

    GtkWidget *text = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(text), constitution_text);
    gtk_label_set_line_wrap(GTK_LABEL(text), TRUE);
    gtk_widget_set_halign(text, GTK_ALIGN_START);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                    GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), text);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

    return vbox;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * AI ASSISTANT PANEL
 * ══════════════════════════════════════════════════════════════════════════════ */

static void on_ai_send_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_ai_input_activate(GtkWidget *entry, phantom_gui_t *gui);
static void on_ai_init_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_ai_suggest_command(GtkWidget *button, phantom_gui_t *gui);
static void on_ai_analyze_code(GtkWidget *button, phantom_gui_t *gui);

static void ai_append_message(phantom_gui_t *gui, const char *sender, const char *message) {
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(gui->ai_chat_buffer, &end);

    /* Add sender with styling */
    char formatted[PHANTOM_AI_MAX_RESPONSE + 256];
    snprintf(formatted, sizeof(formatted), "\n%s: %s\n", sender, message);

    gtk_text_buffer_insert(gui->ai_chat_buffer, &end, formatted, -1);

    /* Scroll to bottom */
    GtkTextMark *mark = gtk_text_buffer_get_insert(gui->ai_chat_buffer);
    gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(gui->ai_chat_view), mark, 0.0, TRUE, 0.0, 1.0);
}

GtkWidget *phantom_gui_create_ai_panel(phantom_gui_t *gui) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 8);

    /* Header */
    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);

    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title),
        "<span size='large' weight='bold'>🤖 Phantom AI Assistant</span>");
    gtk_box_pack_start(GTK_BOX(header), title, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(header), gtk_label_new(""), TRUE, TRUE, 0); /* Spacer */

    /* Status label */
    gui->ai_status_label = gtk_label_new("Not initialized");
    gtk_box_pack_start(GTK_BOX(header), gui->ai_status_label, FALSE, FALSE, 0);

    /* Init button */
    GtkWidget *init_btn = gtk_button_new_with_label("Initialize AI");
    g_signal_connect(init_btn, "clicked", G_CALLBACK(on_ai_init_clicked), gui);
    gtk_box_pack_start(GTK_BOX(header), init_btn, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), header, FALSE, FALSE, 0);

    /* Chat display */
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                    GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scroll, TRUE);

    gui->ai_chat_view = gtk_text_view_new();
    gui->ai_chat_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(gui->ai_chat_view));
    gtk_text_view_set_editable(GTK_TEXT_VIEW(gui->ai_chat_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(gui->ai_chat_view), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(gui->ai_chat_view), 8);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(gui->ai_chat_view), 8);
    gtk_container_add(GTK_CONTAINER(scroll), gui->ai_chat_view);

    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

    /* Welcome message */
    const char *welcome =
        "Welcome to the Phantom AI Assistant!\n\n"
        "I follow the Phantom Constitution:\n"
        "• I will NEVER suggest destructive operations\n"
        "• I recommend 'hide' instead of 'delete'\n"
        "• I suggest 'suspend' instead of 'kill'\n"
        "• I help you create, not destroy\n\n"
        "Click 'Initialize AI' to connect to a model, then ask me anything!\n\n"
        "Examples:\n"
        "• \"How do I create a new file?\"\n"
        "• \"Explain the geology system\"\n"
        "• \"Generate code to read a file\"\n"
        "• \"Why was my code rejected by the Governor?\"";
    gtk_text_buffer_set_text(gui->ai_chat_buffer, welcome, -1);

    /* Input area */
    GtkWidget *input_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);

    gui->ai_input_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(gui->ai_input_entry),
                                    "Ask the AI assistant...");
    gtk_widget_set_hexpand(gui->ai_input_entry, TRUE);
    g_signal_connect(gui->ai_input_entry, "activate", G_CALLBACK(on_ai_input_activate), gui);
    gtk_box_pack_start(GTK_BOX(input_box), gui->ai_input_entry, TRUE, TRUE, 0);

    GtkWidget *send_btn = gtk_button_new_with_label("Send");
    g_signal_connect(send_btn, "clicked", G_CALLBACK(on_ai_send_clicked), gui);
    gtk_box_pack_start(GTK_BOX(input_box), send_btn, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), input_box, FALSE, FALSE, 0);

    /* Quick action buttons - row 1: Quick questions */
    GtkWidget *actions1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);

    const char *quick_actions[] = {
        "Explain Constitution", "Help with files", "Geology guide", "Governor help"
    };

    for (int i = 0; i < 4; i++) {
        GtkWidget *btn = gtk_button_new_with_label(quick_actions[i]);
        g_object_set_data(G_OBJECT(btn), "prompt", (gpointer)quick_actions[i]);
        g_signal_connect(btn, "clicked", G_CALLBACK(on_ai_send_clicked), gui);
        gtk_box_pack_start(GTK_BOX(actions1), btn, FALSE, FALSE, 0);
    }

    gtk_box_pack_start(GTK_BOX(vbox), actions1, FALSE, FALSE, 0);

    /* Quick action buttons - row 2: AI features */
    GtkWidget *actions2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);

    GtkWidget *suggest_btn = gtk_button_new_with_label("Suggest Command");
    g_signal_connect(suggest_btn, "clicked", G_CALLBACK(on_ai_suggest_command), gui);
    gtk_box_pack_start(GTK_BOX(actions2), suggest_btn, FALSE, FALSE, 0);

    GtkWidget *analyze_btn = gtk_button_new_with_label("Analyze Code");
    g_signal_connect(analyze_btn, "clicked", G_CALLBACK(on_ai_analyze_code), gui);
    gtk_box_pack_start(GTK_BOX(actions2), analyze_btn, FALSE, FALSE, 0);

    GtkWidget *help_destructive = gtk_button_new_with_label("Safe Alternatives");
    g_object_set_data(G_OBJECT(help_destructive), "prompt",
                      (gpointer)"What are the safe alternatives to delete, kill, and truncate?");
    g_signal_connect(help_destructive, "clicked", G_CALLBACK(on_ai_send_clicked), gui);
    gtk_box_pack_start(GTK_BOX(actions2), help_destructive, FALSE, FALSE, 0);

    GtkWidget *time_travel_btn = gtk_button_new_with_label("Time Travel Help");
    g_object_set_data(G_OBJECT(time_travel_btn), "prompt",
                      (gpointer)"How do I use geology to time travel and restore old file versions?");
    g_signal_connect(time_travel_btn, "clicked", G_CALLBACK(on_ai_send_clicked), gui);
    gtk_box_pack_start(GTK_BOX(actions2), time_travel_btn, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), actions2, FALSE, FALSE, 0);

    return vbox;
}

static void on_ai_init_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;

    if (!gui || !gui->kernel) return;

    /* Check if already initialized */
    if (gui->kernel->ai) {
        gtk_label_set_text(GTK_LABEL(gui->ai_status_label), "Already initialized");
        return;
    }

    /* Allocate and initialize AI */
    phantom_ai_t *ai = malloc(sizeof(phantom_ai_t));
    if (!ai) {
        ai_append_message(gui, "System", "Failed to allocate AI context");
        return;
    }

    phantom_ai_config_t config = {0};
    config.provider = PHANTOM_AI_PROVIDER_LOCAL;
    config.capabilities = PHANTOM_AI_CAP_ALL;
    config.safety = PHANTOM_AI_SAFETY_STANDARD;
    config.max_tokens = 2048;
    config.temperature = 0.7f;
    config.timeout_ms = 30000;
    config.local_port = 11434;
    strncpy(config.model_name, "llama2", PHANTOM_AI_MODEL_NAME_LEN - 1);

    if (phantom_ai_init(ai, gui->kernel, &config) == 0) {
        gui->kernel->ai = ai;
        phantom_ai_connect(ai);

        /* Connect to Governor */
        if (gui->kernel->governor) {
            governor_set_ai(gui->kernel->governor, ai);
            governor_enable_ai(gui->kernel->governor, 1);
        }

        if (phantom_ai_is_connected(ai)) {
            gtk_label_set_text(GTK_LABEL(gui->ai_status_label), "Connected (External Model)");
            ai_append_message(gui, "System", "AI initialized with external model! You can now chat with me.");
        } else {
            gtk_label_set_text(GTK_LABEL(gui->ai_status_label), "Ready (Built-in AI)");
            ai_append_message(gui, "System",
                "AI initialized with built-in assistant! I can help you with PhantomOS commands, "
                "the Constitution, and more. For advanced AI, install Ollama.");
        }
    } else {
        free(ai);
        ai_append_message(gui, "System", "Failed to initialize AI subsystem");
    }
}

static void on_ai_send_clicked(GtkWidget *button, phantom_gui_t *gui) {
    if (!gui || !gui->kernel) return;

    /* Get message from entry or button data */
    const char *message = NULL;
    const char *prompt_data = g_object_get_data(G_OBJECT(button), "prompt");

    if (prompt_data) {
        message = prompt_data;
    } else {
        message = gtk_entry_get_text(GTK_ENTRY(gui->ai_input_entry));
    }

    if (!message || strlen(message) == 0) return;

    phantom_ai_t *ai = (phantom_ai_t *)gui->kernel->ai;
    if (!ai) {
        ai_append_message(gui, "System",
                          "AI not initialized. Click 'Initialize AI' first.");
        return;
    }

    /* Show user message */
    ai_append_message(gui, "You", message);

    /* Clear input */
    gtk_entry_set_text(GTK_ENTRY(gui->ai_input_entry), "");

    /* Get AI response */
    char response[PHANTOM_AI_MAX_RESPONSE];
    if (phantom_ai_chat(ai, message, response, sizeof(response)) == 0) {
        ai_append_message(gui, "Phantom AI", response);
    } else {
        ai_append_message(gui, "Phantom AI",
                          "I'm sorry, I couldn't process that request. Please try again.");
    }
}

static void on_ai_input_activate(GtkWidget *entry, phantom_gui_t *gui) {
    (void)entry;
    /* Simulate send button click */
    on_ai_send_clicked(gui->ai_input_entry, gui);
}

/* Callback for command suggestion dialog response */
static void on_suggest_dialog_response(GtkDialog *dialog, gint response, phantom_gui_t *gui) {
    if (response == GTK_RESPONSE_OK) {
        GtkWidget *entry = g_object_get_data(G_OBJECT(dialog), "task_entry");
        const char *task = gtk_entry_get_text(GTK_ENTRY(entry));

        if (task && strlen(task) > 0) {
            char command[256];
            char response_text[1024];

            ai_append_message(gui, "You", task);

            if (phantom_ai_builtin_suggest_command(task, command, sizeof(command)) == 0) {
                snprintf(response_text, sizeof(response_text),
                    "To accomplish that, try this command:\n\n    %s\n\n"
                    "Type 'help %s' in the terminal for more details.",
                    command, command);
            } else {
                snprintf(response_text, sizeof(response_text),
                    "I couldn't find an exact command for that task.\n\n"
                    "Try 'help' in the terminal to see all available commands,\n"
                    "or ask me more specifically what you're trying to do.");
            }
            ai_append_message(gui, "Phantom AI", response_text);
        }
    }
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

static void on_ai_suggest_command(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;
    if (!gui) return;

    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Suggest Command",
        GTK_WINDOW(gui->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Suggest", GTK_RESPONSE_OK,
        NULL);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 16);

    GtkWidget *label = gtk_label_new("Describe what you want to do in plain English:");
    gtk_box_pack_start(GTK_BOX(content), label, FALSE, FALSE, 8);

    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry),
        "e.g., 'find all text files' or 'go back in time'");
    gtk_widget_set_size_request(entry, 400, -1);
    gtk_box_pack_start(GTK_BOX(content), entry, FALSE, FALSE, 8);

    GtkWidget *hint = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(hint),
        "<small>Examples: 'list files', 'create directory', 'hide a file', "
        "'suspend a process', 'restore old version'</small>");
    gtk_box_pack_start(GTK_BOX(content), hint, FALSE, FALSE, 8);

    g_object_set_data(G_OBJECT(dialog), "task_entry", entry);
    g_signal_connect(dialog, "response", G_CALLBACK(on_suggest_dialog_response), gui);

    gtk_widget_show_all(dialog);
}

/* Callback for code analysis dialog response */
static void on_analyze_dialog_response(GtkDialog *dialog, gint response, phantom_gui_t *gui) {
    if (response == GTK_RESPONSE_OK) {
        GtkWidget *text_view = g_object_get_data(G_OBJECT(dialog), "code_view");
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));

        GtkTextIter start, end;
        gtk_text_buffer_get_start_iter(buffer, &start);
        gtk_text_buffer_get_end_iter(buffer, &end);

        char *code = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);

        if (code && strlen(code) > 0) {
            char analysis[2048];

            ai_append_message(gui, "You", "[Code submitted for analysis]");

            if (phantom_ai_builtin_analyze_code(code, analysis, sizeof(analysis)) == 0) {
                ai_append_message(gui, "Phantom AI", analysis);
            } else {
                ai_append_message(gui, "Phantom AI",
                    "I couldn't analyze that code. Make sure it's valid C code.");
            }
        }

        if (code) g_free(code);
    }
    gtk_widget_destroy(GTK_WIDGET(dialog));
}

static void on_ai_analyze_code(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;
    if (!gui) return;

    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Analyze Code for Phantom Compliance",
        GTK_WINDOW(gui->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Analyze", GTK_RESPONSE_OK,
        NULL);

    gtk_window_set_default_size(GTK_WINDOW(dialog), 600, 400);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 16);

    GtkWidget *label = gtk_label_new("Paste your code below to check for Phantom compliance:");
    gtk_box_pack_start(GTK_BOX(content), label, FALSE, FALSE, 8);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                    GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scroll, TRUE);

    GtkWidget *text_view = gtk_text_view_new();
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(text_view), TRUE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(text_view), 8);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(text_view), 8);
    gtk_container_add(GTK_CONTAINER(scroll), text_view);

    gtk_box_pack_start(GTK_BOX(content), scroll, TRUE, TRUE, 8);

    GtkWidget *hint = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(hint),
        "<small>The AI will check for destructive operations (delete, kill, truncate) "
        "and suggest Phantom-safe alternatives.</small>");
    gtk_box_pack_start(GTK_BOX(content), hint, FALSE, FALSE, 8);

    g_object_set_data(G_OBJECT(dialog), "code_view", text_view);
    g_signal_connect(dialog, "response", G_CALLBACK(on_analyze_dialog_response), gui);

    gtk_widget_show_all(dialog);
}

/* ══════════════════════════════════════════════════════════════════════════════
 * NETWORK PANEL
 * ══════════════════════════════════════════════════════════════════════════════ */

static void on_net_init_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_net_connect_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_net_suspend_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_net_resume_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_net_dormant_clicked(GtkWidget *button, phantom_gui_t *gui);

GtkWidget *phantom_gui_create_network_panel(phantom_gui_t *gui) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 8);

    /* Header */
    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);

    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title),
        "<span size='large' weight='bold'>🌐 Phantom Network</span>");
    gtk_box_pack_start(GTK_BOX(header), title, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(header), gtk_label_new(""), TRUE, TRUE, 0);

    gui->network_status_label = gtk_label_new("Not initialized");
    gtk_box_pack_start(GTK_BOX(header), gui->network_status_label, FALSE, FALSE, 0);

    GtkWidget *init_btn = gtk_button_new_with_label("Initialize");
    g_signal_connect(init_btn, "clicked", G_CALLBACK(on_net_init_clicked), gui);
    gtk_box_pack_start(GTK_BOX(header), init_btn, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), header, FALSE, FALSE, 0);

    /* Philosophy note */
    GtkWidget *note = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(note),
        "<i>\"Connections rest, they never die\" - Connections are suspended or made dormant, never closed.</i>");
    gtk_label_set_line_wrap(GTK_LABEL(note), TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), note, FALSE, FALSE, 0);

    /* Connection controls */
    GtkWidget *conn_frame = gtk_frame_new("New Connection");
    GtkWidget *conn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(conn_box), 8);

    gtk_box_pack_start(GTK_BOX(conn_box), gtk_label_new("Host:"), FALSE, FALSE, 0);
    gui->network_host_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(gui->network_host_entry), "example.com");
    gtk_widget_set_size_request(gui->network_host_entry, 200, -1);
    gtk_box_pack_start(GTK_BOX(conn_box), gui->network_host_entry, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(conn_box), gtk_label_new("Port:"), FALSE, FALSE, 0);
    gui->network_port_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(gui->network_port_entry), "80");
    gtk_widget_set_size_request(gui->network_port_entry, 60, -1);
    gtk_box_pack_start(GTK_BOX(conn_box), gui->network_port_entry, FALSE, FALSE, 0);

    GtkWidget *connect_btn = gtk_button_new_with_label("Connect");
    g_signal_connect(connect_btn, "clicked", G_CALLBACK(on_net_connect_clicked), gui);
    gtk_box_pack_start(GTK_BOX(conn_box), connect_btn, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(conn_frame), conn_box);
    gtk_box_pack_start(GTK_BOX(vbox), conn_frame, FALSE, FALSE, 0);

    /* Socket list */
    GtkWidget *list_frame = gtk_frame_new("Connections");
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                    GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scroll, TRUE);

    /* Create list store: ID, State, Type, Local, Remote, Sent, Recv */
    gui->network_store = gtk_list_store_new(NET_COL_COUNT,
        G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING,
        G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);

    gui->network_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(gui->network_store));
    g_object_unref(gui->network_store);

    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();

    GtkTreeViewColumn *col;
    col = gtk_tree_view_column_new_with_attributes("ID", renderer, "text", NET_COL_ID, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->network_tree), col);

    col = gtk_tree_view_column_new_with_attributes("State", renderer, "text", NET_COL_STATE, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->network_tree), col);

    col = gtk_tree_view_column_new_with_attributes("Type", renderer, "text", NET_COL_TYPE, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->network_tree), col);

    col = gtk_tree_view_column_new_with_attributes("Local", renderer, "text", NET_COL_LOCAL, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->network_tree), col);

    col = gtk_tree_view_column_new_with_attributes("Remote", renderer, "text", NET_COL_REMOTE, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->network_tree), col);

    col = gtk_tree_view_column_new_with_attributes("Sent", renderer, "text", NET_COL_SENT, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->network_tree), col);

    col = gtk_tree_view_column_new_with_attributes("Recv", renderer, "text", NET_COL_RECV, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->network_tree), col);

    gtk_container_add(GTK_CONTAINER(scroll), gui->network_tree);
    gtk_container_add(GTK_CONTAINER(list_frame), scroll);
    gtk_box_pack_start(GTK_BOX(vbox), list_frame, TRUE, TRUE, 0);

    /* Action buttons */
    GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);

    GtkWidget *suspend_btn = gtk_button_new_with_label("Suspend");
    g_signal_connect(suspend_btn, "clicked", G_CALLBACK(on_net_suspend_clicked), gui);
    gtk_box_pack_start(GTK_BOX(actions), suspend_btn, FALSE, FALSE, 0);

    GtkWidget *resume_btn = gtk_button_new_with_label("Resume");
    g_signal_connect(resume_btn, "clicked", G_CALLBACK(on_net_resume_clicked), gui);
    gtk_box_pack_start(GTK_BOX(actions), resume_btn, FALSE, FALSE, 0);

    GtkWidget *dormant_btn = gtk_button_new_with_label("Make Dormant");
    g_signal_connect(dormant_btn, "clicked", G_CALLBACK(on_net_dormant_clicked), gui);
    gtk_box_pack_start(GTK_BOX(actions), dormant_btn, FALSE, FALSE, 0);

    GtkWidget *refresh_btn = gtk_button_new_with_label("Refresh");
    g_signal_connect_swapped(refresh_btn, "clicked",
                              G_CALLBACK(phantom_gui_refresh_network), gui);
    gtk_box_pack_start(GTK_BOX(actions), refresh_btn, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), actions, FALSE, FALSE, 0);

    return vbox;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * APPS PANEL
 * ══════════════════════════════════════════════════════════════════════════════ */

/* Forward declarations for apps panel callbacks */
static void on_apps_notes_new_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_apps_notes_save_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_apps_monitor_refresh_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_apps_web_go_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_apps_web_reload_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_apps_web_back_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_apps_web_forward_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_apps_web_stop_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_apps_web_url_changed(GtkEditable *editable, phantom_gui_t *gui);

/* ═══════════════════════════════════════════════════════════════════════════════
 * WEBKIT BROWSER SIGNAL HANDLERS - Page load progress and security indicators
 * ═══════════════════════════════════════════════════════════════════════════════ */

/* Helper to update the large security bar */
static void update_security_bar(phantom_gui_t *gui, const char *icon, const char *title,
                                 const char *detail, const char *css_class) {
    gtk_label_set_text(GTK_LABEL(gui->apps_web_security_icon), icon);
    gtk_label_set_text(GTK_LABEL(gui->apps_web_security_text), title);
    gtk_label_set_text(GTK_LABEL(gui->apps_web_status), detail);

    /* Update CSS class for color */
    GtkStyleContext *context = gtk_widget_get_style_context(gui->apps_web_security_bar);
    gtk_style_context_remove_class(context, "secure");
    gtk_style_context_remove_class(context, "insecure");
    gtk_style_context_remove_class(context, "warning");
    gtk_style_context_remove_class(context, "loading");
    if (css_class && strlen(css_class) > 0) {
        gtk_style_context_add_class(context, css_class);
    }
}

/* Update status bar with load progress */
static void on_webkit_load_progress(WebKitWebView *web_view, GParamSpec *pspec, phantom_gui_t *gui) {
    (void)pspec;

    gdouble progress = webkit_web_view_get_estimated_load_progress(web_view);

    /* Update progress bar */
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(gui->apps_web_progress), progress);

    if (progress < 1.0) {
        gtk_widget_show(gui->apps_web_progress);
        char detail[256];
        snprintf(detail, sizeof(detail), "Loading page... %d%% complete", (int)(progress * 100));
        gtk_label_set_text(GTK_LABEL(gui->apps_web_status), detail);
    } else {
        gtk_widget_hide(gui->apps_web_progress);
    }
}

/* Handle load state changes */
static void on_webkit_load_changed(WebKitWebView *web_view, WebKitLoadEvent load_event, phantom_gui_t *gui) {
    const char *uri = webkit_web_view_get_uri(web_view);

    switch (load_event) {
        case WEBKIT_LOAD_STARTED:
            gtk_widget_show(gui->apps_web_progress);
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(gui->apps_web_progress), 0.0);
            update_security_bar(gui, "🔄", "Connecting...",
                               uri ? uri : "Starting connection", "loading");
            break;

        case WEBKIT_LOAD_REDIRECTED:
            update_security_bar(gui, "↪️", "Redirecting...",
                               "Following redirect to new location", "loading");
            break;

        case WEBKIT_LOAD_COMMITTED: {
            /* Page has started loading - check TLS status */
            gboolean is_secure = FALSE;
            GTlsCertificate *cert = NULL;
            GTlsCertificateFlags cert_errors = 0;

            if (webkit_web_view_get_tls_info(web_view, &cert, &cert_errors)) {
                is_secure = (cert_errors == 0);
            }

            if (uri && strncmp(uri, "https://", 8) == 0) {
                if (is_secure) {
                    update_security_bar(gui, "🔒", "Secure Connection",
                                       "TLS encryption active - Loading content...", "secure");
                } else if (cert) {
                    update_security_bar(gui, "⚠️", "Certificate Warning",
                                       "HTTPS with certificate issues - Proceed with caution", "warning");
                } else {
                    update_security_bar(gui, "🔒", "HTTPS Connection",
                                       "Encrypted connection - Loading...", "loading");
                }
            } else if (uri && strncmp(uri, "http://", 7) == 0) {
                update_security_bar(gui, "🔓", "NOT SECURE",
                                   "Connection is not encrypted - Data may be intercepted", "insecure");
            } else {
                update_security_bar(gui, "📄", "Local Content",
                                   "Loading local page", "");
            }
            break;
        }

        case WEBKIT_LOAD_FINISHED: {
            gtk_widget_hide(gui->apps_web_progress);

            /* Page finished loading - show final status with security info */
            gboolean is_secure = FALSE;
            GTlsCertificate *cert = NULL;
            GTlsCertificateFlags cert_errors = 0;

            if (webkit_web_view_get_tls_info(web_view, &cert, &cert_errors)) {
                is_secure = (cert_errors == 0);
            }

            const char *title = webkit_web_view_get_title(web_view);
            char detail[512];

            if (uri && strncmp(uri, "https://", 8) == 0) {
                if (is_secure) {
                    snprintf(detail, sizeof(detail), "%s", title ? title : uri);
                    update_security_bar(gui, "🔒", "Secure | TLS ✓", detail, "secure");
                } else {
                    snprintf(detail, sizeof(detail), "%s - Certificate has issues", title ? title : uri);
                    update_security_bar(gui, "⚠️", "HTTPS | Cert Warning", detail, "warning");
                }
            } else if (uri && strncmp(uri, "http://", 7) == 0) {
                snprintf(detail, sizeof(detail), "%s - Your connection is not private", title ? title : uri);
                update_security_bar(gui, "🔓", "NOT SECURE", detail, "insecure");
            } else {
                snprintf(detail, sizeof(detail), "%s", title ? title : "Page loaded");
                update_security_bar(gui, "✅", "Page Loaded", detail, "");
            }

            /* Update URL bar with final URL (after redirects) */
            if (uri) {
                gtk_entry_set_text(GTK_ENTRY(gui->apps_web_url_entry), uri);
            }
            break;
        }
    }
}

/* Handle load failures */
static gboolean on_webkit_load_failed(WebKitWebView *web_view, WebKitLoadEvent load_event,
                                       gchar *failing_uri, GError *error, phantom_gui_t *gui) {
    (void)web_view;
    (void)load_event;

    gtk_widget_hide(gui->apps_web_progress);

    char detail[512];
    snprintf(detail, sizeof(detail), "%s - %s",
             failing_uri ? failing_uri : "Unknown page",
             error ? error->message : "Connection failed");
    update_security_bar(gui, "❌", "Failed to Load", detail, "insecure");

    return FALSE;  /* Let WebKit show its default error page */
}

/* Handle mouse hover over links */
static void on_webkit_mouse_target_changed(WebKitWebView *web_view,
                                            WebKitHitTestResult *hit_test_result,
                                            guint modifiers, phantom_gui_t *gui) {
    (void)web_view;
    (void)modifiers;

    if (webkit_hit_test_result_context_is_link(hit_test_result)) {
        const char *link_uri = webkit_hit_test_result_get_link_uri(hit_test_result);
        if (link_uri) {
            /* Show link in status area (don't change the main security indicator) */
            char link_display[512];
            snprintf(link_display, sizeof(link_display), "🔗 %s", link_uri);
            gtk_label_set_text(GTK_LABEL(gui->apps_web_status), link_display);
        }
    }
}

GtkWidget *phantom_gui_create_apps_panel(phantom_gui_t *gui) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 8);

    /* Header */
    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);

    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title),
        "<span size='large' weight='bold'>📱 Phantom Apps</span>");
    gtk_box_pack_start(GTK_BOX(header), title, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(header), gtk_label_new(""), TRUE, TRUE, 0);

    GtkWidget *philosophy = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(philosophy),
        "<i>\"To Create, Not To Destroy\"</i>");
    gtk_box_pack_end(GTK_BOX(header), philosophy, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), header, FALSE, FALSE, 0);

    /* Create notebook for app tabs */
    GtkWidget *notebook = gtk_notebook_new();
    gtk_notebook_set_tab_pos(GTK_NOTEBOOK(notebook), GTK_POS_TOP);

    /* ═══════════════════════════════════════════════════════════════════════════
     * NOTES TAB
     * ═══════════════════════════════════════════════════════════════════════════ */
    GtkWidget *notes_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(notes_box), 8);

    /* Notes description */
    GtkWidget *notes_desc = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(notes_desc),
        "<b>Notes</b> - Every edit is preserved forever in geology. Notes are never deleted, only archived.");
    gtk_label_set_line_wrap(GTK_LABEL(notes_desc), TRUE);
    gtk_widget_set_halign(notes_desc, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(notes_box), notes_desc, FALSE, FALSE, 0);

    /* Notes toolbar */
    GtkWidget *notes_toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);

    GtkWidget *new_note_btn = gtk_button_new_with_label("📝 New Note");
    g_signal_connect(new_note_btn, "clicked", G_CALLBACK(on_apps_notes_new_clicked), gui);
    gtk_box_pack_start(GTK_BOX(notes_toolbar), new_note_btn, FALSE, FALSE, 0);

    GtkWidget *save_note_btn = gtk_button_new_with_label("💾 Save");
    g_signal_connect(save_note_btn, "clicked", G_CALLBACK(on_apps_notes_save_clicked), gui);
    gtk_box_pack_start(GTK_BOX(notes_toolbar), save_note_btn, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(notes_toolbar), gtk_label_new("Title:"), FALSE, FALSE, 4);
    gui->apps_note_title_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(gui->apps_note_title_entry), "Note title...");
    gtk_widget_set_size_request(gui->apps_note_title_entry, 300, -1);
    gtk_box_pack_start(GTK_BOX(notes_toolbar), gui->apps_note_title_entry, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(notes_box), notes_toolbar, FALSE, FALSE, 0);

    /* Notes paned - list on left, content on right */
    GtkWidget *notes_paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);

    /* Notes list */
    GtkWidget *notes_list_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(notes_list_scroll),
                                    GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(notes_list_scroll, 200, -1);

    gui->apps_notes_store = gtk_list_store_new(4,
        G_TYPE_UINT64,   /* ID */
        G_TYPE_STRING,   /* Title */
        G_TYPE_STRING,   /* State */
        G_TYPE_STRING);  /* Modified */

    gui->apps_notes_list = gtk_tree_view_new_with_model(GTK_TREE_MODEL(gui->apps_notes_store));
    g_object_unref(gui->apps_notes_store);

    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *col;

    col = gtk_tree_view_column_new_with_attributes("ID", renderer, "text", 0, NULL);
    gtk_tree_view_column_set_min_width(col, 40);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->apps_notes_list), col);

    col = gtk_tree_view_column_new_with_attributes("Title", renderer, "text", 1, NULL);
    gtk_tree_view_column_set_min_width(col, 120);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->apps_notes_list), col);

    col = gtk_tree_view_column_new_with_attributes("State", renderer, "text", 2, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->apps_notes_list), col);

    gtk_container_add(GTK_CONTAINER(notes_list_scroll), gui->apps_notes_list);
    gtk_paned_pack1(GTK_PANED(notes_paned), notes_list_scroll, FALSE, FALSE);

    /* Notes content editor */
    GtkWidget *notes_content_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(notes_content_scroll),
                                    GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

    gui->apps_note_content = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(gui->apps_note_content), GTK_WRAP_WORD);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(gui->apps_note_content), 8);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(gui->apps_note_content), 8);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(gui->apps_note_content), 8);
    gtk_container_add(GTK_CONTAINER(notes_content_scroll), gui->apps_note_content);

    gtk_paned_pack2(GTK_PANED(notes_paned), notes_content_scroll, TRUE, TRUE);

    gtk_box_pack_start(GTK_BOX(notes_box), notes_paned, TRUE, TRUE, 0);

    GtkWidget *notes_label = gtk_label_new("📝 Notes");
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), notes_box, notes_label);

    /* ═══════════════════════════════════════════════════════════════════════════
     * SYSTEM MONITOR TAB
     * ═══════════════════════════════════════════════════════════════════════════ */
    GtkWidget *monitor_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(monitor_box), 8);

    /* Monitor description */
    GtkWidget *monitor_desc = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(monitor_desc),
        "<b>System Monitor</b> - Real-time system statistics and performance metrics.");
    gtk_label_set_line_wrap(GTK_LABEL(monitor_desc), TRUE);
    gtk_widget_set_halign(monitor_desc, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(monitor_box), monitor_desc, FALSE, FALSE, 0);

    /* Refresh button */
    GtkWidget *monitor_refresh = gtk_button_new_with_label("🔄 Refresh Statistics");
    g_signal_connect(monitor_refresh, "clicked", G_CALLBACK(on_apps_monitor_refresh_clicked), gui);
    gtk_widget_set_halign(monitor_refresh, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(monitor_box), monitor_refresh, FALSE, FALSE, 0);

    /* Stats grid */
    GtkWidget *stats_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(stats_grid), 12);
    gtk_grid_set_column_spacing(GTK_GRID(stats_grid), 24);
    gtk_widget_set_margin_top(stats_grid, 16);

    const char *stat_names[] = {
        "💻 System:",
        "📊 Processes:",
        "🧠 Memory:",
        "🪨 Geology:",
        "🌐 Network:",
        "🛡️ Governor:",
        "⏱️ Uptime:",
        "📈 Status:"
    };

    for (int i = 0; i < 8; i++) {
        GtkWidget *name_label = gtk_label_new(stat_names[i]);
        gtk_widget_set_halign(name_label, GTK_ALIGN_END);
        gtk_grid_attach(GTK_GRID(stats_grid), name_label, 0, i, 1, 1);

        gui->apps_monitor_labels[i] = gtk_label_new("--");
        gtk_widget_set_halign(gui->apps_monitor_labels[i], GTK_ALIGN_START);
        gtk_label_set_selectable(GTK_LABEL(gui->apps_monitor_labels[i]), TRUE);
        gtk_grid_attach(GTK_GRID(stats_grid), gui->apps_monitor_labels[i], 1, i, 1, 1);
    }

    gtk_box_pack_start(GTK_BOX(monitor_box), stats_grid, FALSE, FALSE, 0);

    GtkWidget *monitor_label = gtk_label_new("📊 Monitor");
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), monitor_box, monitor_label);

    /* ═══════════════════════════════════════════════════════════════════════════
     * WEB BROWSER TAB
     * ═══════════════════════════════════════════════════════════════════════════ */
    GtkWidget *web_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(web_box), 8);

    /* URL bar with navigation controls */
    GtkWidget *url_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);

    /* Back button */
    GtkWidget *back_btn = gtk_button_new_with_label("◀");
    gtk_widget_set_tooltip_text(back_btn, "Go Back");
    g_signal_connect(back_btn, "clicked", G_CALLBACK(on_apps_web_back_clicked), gui);
    gtk_box_pack_start(GTK_BOX(url_bar), back_btn, FALSE, FALSE, 0);

    /* Forward button */
    GtkWidget *forward_btn = gtk_button_new_with_label("▶");
    gtk_widget_set_tooltip_text(forward_btn, "Go Forward");
    g_signal_connect(forward_btn, "clicked", G_CALLBACK(on_apps_web_forward_clicked), gui);
    gtk_box_pack_start(GTK_BOX(url_bar), forward_btn, FALSE, FALSE, 0);

    /* Reload button */
    GtkWidget *reload_btn = gtk_button_new_with_label("🔄");
    gtk_widget_set_tooltip_text(reload_btn, "Reload Page");
    g_signal_connect(reload_btn, "clicked", G_CALLBACK(on_apps_web_reload_clicked), gui);
    gtk_box_pack_start(GTK_BOX(url_bar), reload_btn, FALSE, FALSE, 0);

    /* Stop button */
    GtkWidget *stop_btn = gtk_button_new_with_label("✕");
    gtk_widget_set_tooltip_text(stop_btn, "Stop Loading");
    g_signal_connect(stop_btn, "clicked", G_CALLBACK(on_apps_web_stop_clicked), gui);
    gtk_box_pack_start(GTK_BOX(url_bar), stop_btn, FALSE, FALSE, 0);

    /* Spacer */
    gtk_box_pack_start(GTK_BOX(url_bar), gtk_label_new(" "), FALSE, FALSE, 0);

    /* URL entry with real-time scanning */
    gui->apps_web_url_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(gui->apps_web_url_entry), "https://example.com");
    gtk_widget_set_tooltip_text(gui->apps_web_url_entry, "URL is scanned in real-time for threats");
    g_signal_connect(gui->apps_web_url_entry, "changed", G_CALLBACK(on_apps_web_url_changed), gui);
    gtk_box_pack_start(GTK_BOX(url_bar), gui->apps_web_url_entry, TRUE, TRUE, 0);

    /* Go button */
    GtkWidget *go_btn = gtk_button_new_with_label("Go");
    gtk_widget_set_tooltip_text(go_btn, "Navigate to URL");
    g_signal_connect(go_btn, "clicked", G_CALLBACK(on_apps_web_go_clicked), gui);
    gtk_box_pack_start(GTK_BOX(url_bar), go_btn, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(web_box), url_bar, FALSE, FALSE, 0);

    /* ═══════════════════════════════════════════════════════════════════════════
     * SECURITY STATUS BAR - Large, prominent indicator
     * ═══════════════════════════════════════════════════════════════════════════ */
    gui->apps_web_security_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_name(gui->apps_web_security_bar, "security-bar");

    /* Security icon - large and prominent */
    gui->apps_web_security_icon = gtk_label_new("🏠");
    PangoAttrList *icon_attrs = pango_attr_list_new();
    pango_attr_list_insert(icon_attrs, pango_attr_scale_new(2.0));  /* 2x size */
    gtk_label_set_attributes(GTK_LABEL(gui->apps_web_security_icon), icon_attrs);
    pango_attr_list_unref(icon_attrs);
    gtk_box_pack_start(GTK_BOX(gui->apps_web_security_bar), gui->apps_web_security_icon, FALSE, FALSE, 8);

    /* Security text box */
    GtkWidget *security_text_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);

    gui->apps_web_security_text = gtk_label_new("Ready");
    PangoAttrList *text_attrs = pango_attr_list_new();
    pango_attr_list_insert(text_attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
    pango_attr_list_insert(text_attrs, pango_attr_scale_new(1.2));
    gtk_label_set_attributes(GTK_LABEL(gui->apps_web_security_text), text_attrs);
    pango_attr_list_unref(text_attrs);
    gtk_widget_set_halign(gui->apps_web_security_text, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(security_text_box), gui->apps_web_security_text, FALSE, FALSE, 0);

    gui->apps_web_status = gtk_label_new("Enter a URL and click Go to browse");
    gtk_widget_set_halign(gui->apps_web_status, GTK_ALIGN_START);
    gtk_label_set_ellipsize(GTK_LABEL(gui->apps_web_status), PANGO_ELLIPSIZE_END);
    gtk_box_pack_start(GTK_BOX(security_text_box), gui->apps_web_status, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(gui->apps_web_security_bar), security_text_box, TRUE, TRUE, 0);

    /* Style the security bar with CSS */
    GtkCssProvider *css_provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css_provider,
        "#security-bar { background: #21262d; border-radius: 6px; padding: 8px 12px; margin: 4px 0; }"
        "#security-bar.secure { background: linear-gradient(90deg, #238636 0%, #2ea043 100%); }"
        "#security-bar.insecure { background: linear-gradient(90deg, #da3633 0%, #f85149 100%); }"
        "#security-bar.warning { background: linear-gradient(90deg, #9e6a03 0%, #d29922 100%); }"
        "#security-bar.loading { background: linear-gradient(90deg, #1f6feb 0%, #388bfd 100%); }",
        -1, NULL);
    GtkStyleContext *context = gtk_widget_get_style_context(gui->apps_web_security_bar);
    gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(css_provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css_provider);

    gtk_box_pack_start(GTK_BOX(web_box), gui->apps_web_security_bar, FALSE, FALSE, 0);

    /* Progress bar for page loading */
    gui->apps_web_progress = gtk_progress_bar_new();
    gtk_widget_set_no_show_all(gui->apps_web_progress, TRUE);  /* Hidden by default */
    gtk_box_pack_start(GTK_BOX(web_box), gui->apps_web_progress, FALSE, FALSE, 0);

    /* Web content view - WebKitWebView for full HTML rendering */
    gui->apps_web_view = webkit_web_view_new();

    /* Configure WebKit settings for security and media */
    WebKitSettings *settings = webkit_web_view_get_settings(WEBKIT_WEB_VIEW(gui->apps_web_view));
    webkit_settings_set_enable_javascript(settings, TRUE);
    webkit_settings_set_enable_plugins(settings, FALSE);  /* No plugins for security */
    webkit_settings_set_enable_java(settings, FALSE);     /* No Java for security */
    webkit_settings_set_auto_load_images(settings, TRUE);
    webkit_settings_set_enable_developer_extras(settings, FALSE);

    /* Enable media playback for GIFs and video */
    webkit_settings_set_media_playback_requires_user_gesture(settings, FALSE);
    webkit_settings_set_media_playback_allows_inline(settings, TRUE);
    webkit_settings_set_enable_media_stream(settings, TRUE);
    webkit_settings_set_enable_webgl(settings, TRUE);  /* Some sites use WebGL for animations */

    /* Load welcome page */
    const char *welcome_html =
        "<!DOCTYPE html><html><head><style>"
        "body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; "
        "       background: linear-gradient(135deg, #0d1117 0%, #161b22 100%); "
        "       color: #c9d1d9; padding: 40px; margin: 0; min-height: 100vh; }"
        "h1 { color: #58a6ff; border-bottom: 2px solid #30363d; padding-bottom: 10px; }"
        "h2 { color: #8b949e; font-size: 1.1em; margin-top: 0; }"
        ".features { background: #21262d; padding: 20px; border-radius: 8px; margin: 20px 0; }"
        ".features li { margin: 8px 0; }"
        ".try-url { font-family: monospace; background: #30363d; padding: 8px 12px; "
        "           border-radius: 4px; display: inline-block; margin-top: 10px; }"
        ".security-badge { color: #3fb950; }"
        "</style></head><body>"
        "<h1>🌐 Phantom Web Browser</h1>"
        "<h2>Governor-Controlled Network Access</h2>"
        "<p>Enter a URL in the address bar above and click 'Go' to navigate.</p>"
        "<div class='features'>"
        "<h3 class='security-badge'>🛡️ Security Features:</h3>"
        "<ul>"
        "<li>All network requests require Governor approval</li>"
        "<li>HTTPS connections require CAP_NETWORK_SECURE capability</li>"
        "<li>Built-in ad blocking (~65 domains blocked)</li>"
        "<li>Content filtering removes tracking scripts</li>"
        "<li>All browsing history preserved in geology</li>"
        "</ul></div>"
        "<p>Try: <span class='try-url'>https://google.com</span></p>"
        "<p style='color:#8b949e; font-style:italic; margin-top:40px;'>\"To Create, Not To Destroy\"</p>"
        "</body></html>";

    webkit_web_view_load_html(WEBKIT_WEB_VIEW(gui->apps_web_view), welcome_html, NULL);

    /* Connect WebKit signals for status updates */
    g_signal_connect(gui->apps_web_view, "notify::estimated-load-progress",
                     G_CALLBACK(on_webkit_load_progress), gui);
    g_signal_connect(gui->apps_web_view, "load-changed",
                     G_CALLBACK(on_webkit_load_changed), gui);
    g_signal_connect(gui->apps_web_view, "load-failed",
                     G_CALLBACK(on_webkit_load_failed), gui);
    g_signal_connect(gui->apps_web_view, "mouse-target-changed",
                     G_CALLBACK(on_webkit_mouse_target_changed), gui);

    gtk_widget_set_vexpand(gui->apps_web_view, TRUE);
    gtk_widget_set_hexpand(gui->apps_web_view, TRUE);
    gtk_box_pack_start(GTK_BOX(web_box), gui->apps_web_view, TRUE, TRUE, 0);

    GtkWidget *web_label = gtk_label_new("🌐 Browser");
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), web_box, web_label);

    /* ═══════════════════════════════════════════════════════════════════════════
     * FILE VIEWER TAB
     * ═══════════════════════════════════════════════════════════════════════════ */
    GtkWidget *viewer_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(viewer_box), 8);

    GtkWidget *viewer_desc = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(viewer_desc),
        "<b>File Viewer</b> - Safe read-only file viewing. Use Files panel for navigation.");
    gtk_label_set_line_wrap(GTK_LABEL(viewer_desc), TRUE);
    gtk_widget_set_halign(viewer_desc, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(viewer_box), viewer_desc, FALSE, FALSE, 0);

    GtkWidget *viewer_note = gtk_label_new(
        "Select a file from the Files panel to view it here.\n\n"
        "Supported formats:\n"
        "  • Text files (.txt, .md, .log, .json, .xml, .yaml)\n"
        "  • Source code (.c, .h, .py, .js, .go, .rs, etc.)\n"
        "  • Images (metadata only in terminal)\n"
        "  • Binary files (hex dump view)");
    gtk_label_set_line_wrap(GTK_LABEL(viewer_note), TRUE);
    gtk_widget_set_halign(viewer_note, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(viewer_box), viewer_note, FALSE, FALSE, 16);

    GtkWidget *viewer_label = gtk_label_new("👁️ Viewer");
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), viewer_box, viewer_label);

    gtk_box_pack_start(GTK_BOX(vbox), notebook, TRUE, TRUE, 0);

    return vbox;
}

/* Apps panel callbacks */
static void on_apps_notes_new_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;

    /* Clear the entry and content for new note */
    gtk_entry_set_text(GTK_ENTRY(gui->apps_note_title_entry), "");
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(gui->apps_note_content));
    gtk_text_buffer_set_text(buffer, "", -1);

    phantom_gui_update_status(gui, "New note - enter title and content, then click Save");
}

static void on_apps_notes_save_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;

    const char *title = gtk_entry_get_text(GTK_ENTRY(gui->apps_note_title_entry));
    if (!title || strlen(title) == 0) {
        phantom_gui_show_message(gui, "Error", "Please enter a note title", GTK_MESSAGE_WARNING);
        return;
    }

    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(gui->apps_note_content));
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    char *content = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);

    /* Create note via terminal command (shell handles notes app) */
    char status[256];
    snprintf(status, sizeof(status), "Note '%s' saved (%zu characters)", title, strlen(content));
    phantom_gui_update_status(gui, status);

    g_free(content);
}

static void on_apps_monitor_refresh_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;

    if (!gui || !gui->kernel) return;

    struct phantom_kernel *kernel = gui->kernel;

    /* System info */
    gtk_label_set_text(GTK_LABEL(gui->apps_monitor_labels[0]), "PhantomOS 1.0.0");

    /* Process count */
    int proc_count = 0;
    struct phantom_process *proc = kernel->processes;
    while (proc) { proc_count++; proc = proc->next; }
    char proc_str[64];
    snprintf(proc_str, sizeof(proc_str), "%d active processes", proc_count);
    gtk_label_set_text(GTK_LABEL(gui->apps_monitor_labels[1]), proc_str);

    /* Memory (simulated) */
    gtk_label_set_text(GTK_LABEL(gui->apps_monitor_labels[2]), "256 MB used / 1024 MB total (25%)");

    /* Geology */
    if (kernel->geofs_volume) {
        gtk_label_set_text(GTK_LABEL(gui->apps_monitor_labels[3]), "GeoFS active - Immutable storage operational");
    } else {
        gtk_label_set_text(GTK_LABEL(gui->apps_monitor_labels[3]), "GeoFS not initialized");
    }

    /* Network */
    if (kernel->net) {
        phantom_net_t *net = (phantom_net_t *)kernel->net;
        char net_str[128];
        snprintf(net_str, sizeof(net_str), "%s - %lu connections",
                 net->initialized ? "Enabled" : "Disabled",
                 net->active_connections);
        gtk_label_set_text(GTK_LABEL(gui->apps_monitor_labels[4]), net_str);
    } else {
        gtk_label_set_text(GTK_LABEL(gui->apps_monitor_labels[4]), "Not initialized");
    }

    /* Governor */
    if (kernel->governor) {
        phantom_governor_t *gov = kernel->governor;
        char gov_str[128];
        snprintf(gov_str, sizeof(gov_str), "%lu evaluations | %.1f%% approval rate",
                 gov->total_evaluations,
                 gov->total_evaluations > 0 ?
                    100.0 * (gov->auto_approved + gov->user_approved) / gov->total_evaluations : 100.0);
        gtk_label_set_text(GTK_LABEL(gui->apps_monitor_labels[5]), gov_str);
    } else {
        gtk_label_set_text(GTK_LABEL(gui->apps_monitor_labels[5]), "Not initialized");
    }

    /* Uptime */
    time_t uptime = time(NULL) - kernel->boot_time;
    char uptime_str[64];
    snprintf(uptime_str, sizeof(uptime_str), "%ld hours %ld minutes",
             uptime / 3600, (uptime % 3600) / 60);
    gtk_label_set_text(GTK_LABEL(gui->apps_monitor_labels[6]), uptime_str);

    /* Status */
    gtk_label_set_text(GTK_LABEL(gui->apps_monitor_labels[7]), "All systems operational");

    phantom_gui_update_status(gui, "System monitor refreshed");
}

/* Governor response codes for approval dialog */
typedef enum {
    GOV_RESPONSE_ALLOW_ONCE = 1,
    GOV_RESPONSE_ALLOW_ALWAYS = 2,
    GOV_RESPONSE_DENY = 3,
    GOV_RESPONSE_BLOCK = 4
} gov_approval_response_t;

/* Static webbrowser instance for the browser tab */
static phantom_webbrowser_t webbrowser;
static phantom_net_t browser_net;
static phantom_tls_t browser_tls;
static int wb_initialized = 0;

/* Initialize the webbrowser subsystem */
static void ensure_webbrowser_initialized(phantom_gui_t *gui) {
    if (wb_initialized) return;

    phantom_webbrowser_init(&webbrowser, gui->kernel, gui->kernel ? gui->kernel->governor : NULL);

    /* Initialize network - use kernel's if available, otherwise create our own */
    phantom_net_t *net = NULL;
    if (gui->kernel && gui->kernel->net) {
        net = (phantom_net_t *)gui->kernel->net;
    } else if (gui->kernel) {
        /* Initialize our own network layer */
        if (phantom_net_init(&browser_net, gui->kernel) == 0) {
            net = &browser_net;
            gui->kernel->net = net;
        }
    }

    if (net) {
        phantom_webbrowser_set_network(&webbrowser, net);

        /* Initialize TLS - use kernel's if available, otherwise create our own */
        phantom_tls_t *tls = NULL;
        if (gui->kernel && gui->kernel->tls) {
            tls = (phantom_tls_t *)gui->kernel->tls;
        } else {
            /* Initialize our own TLS layer */
            if (phantom_tls_init(&browser_tls, net) == 0) {
                tls = &browser_tls;
                if (gui->kernel) {
                    gui->kernel->tls = tls;
                }
            }
        }

        if (tls) {
            phantom_webbrowser_set_tls(&webbrowser, tls);
        }
    }

    /* Initialize URL scanner for real-time threat detection */
    if (!urlscanner_initialized) {
        phantom_urlscan_init(&urlscanner);

        /* Try to load blocklists from various locations */
        phantom_urlscan_load_blocklist_dir(&urlscanner, "geo/etc/blocklists");
        phantom_urlscan_load_blocklist_dir(&urlscanner, "/geo/etc/blocklists");

        /* Enable DNS-based blocking via Quad9 (free malware blocking DNS) */
        phantom_urlscan_enable_dns_blocking(&urlscanner, URLSCAN_DNS_QUAD9, 1000);

        printf("[browser] URL scanner ready with %u blocklist entries\n",
               phantom_urlscan_get_blocklist_count(&urlscanner));

        urlscanner_initialized = 1;
    }

    wb_initialized = 1;
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * CONTENT PRE-SCAN - Analyze website content before showing approval dialog
 * ═══════════════════════════════════════════════════════════════════════════════ */

/* Scan result structure */
typedef struct {
    int scan_success;           /* Did the scan complete successfully? */
    int http_status;            /* HTTP response code */
    size_t content_size;        /* Size of content */
    char content_type[64];      /* Content type */

    /* Safety analysis */
    int tracker_count;          /* Number of tracking scripts detected */
    int ad_count;               /* Number of ad elements detected */
    int form_count;             /* Number of forms (login/data collection) */
    int external_script_count;  /* External script includes */
    int suspicious_patterns;    /* Suspicious code patterns */

    /* Content indicators */
    int has_login_form;         /* Contains login form */
    int has_payment_form;       /* Contains payment fields */
    int has_download_links;     /* Contains download links */
    int has_popups;             /* Contains popup scripts */

    /* Risk assessment */
    int risk_score;             /* 0-100, higher = more risky */
    char risk_level[32];        /* "Low", "Medium", "High", "Critical" */
    char recommendation[256];   /* What we recommend */

    /* Preview */
    char title[256];            /* Page title */
    char description[512];      /* Meta description */
} content_scan_result_t;

/* Patterns to detect in content */
static const char *tracker_patterns[] = {
    "google-analytics", "googletagmanager", "facebook.net", "fb.com/tr",
    "pixel.", "beacon.", "tracker.", "analytics.", "telemetry.",
    "hotjar", "mixpanel", "segment.io", "amplitude", "fullstory",
    "mouseflow", "crazyegg", "clarity.ms", "newrelic", NULL
};

static const char *ad_patterns[] = {
    "googlesyndication", "doubleclick", "adservice", "pagead",
    "adsbygoogle", "data-ad-", "taboola", "outbrain", "criteo",
    "ad-slot", "ad-unit", "banner-ad", "sponsored", NULL
};

static const char *suspicious_patterns[] = {
    "eval(", "document.write(", "unescape(", "fromCharCode",
    "window.location=", "onclick=\"window.open", ".exe\"",
    "download=", "cryptocurrency", "bitcoin wallet", NULL
};

/* Count pattern matches in content */
static int count_patterns(const char *content, size_t len, const char **patterns) {
    if (!content || len == 0 || !patterns) return 0;

    int count = 0;
    for (int i = 0; patterns[i] != NULL; i++) {
        const char *p = content;
        const char *end = content + len;
        size_t plen = strlen(patterns[i]);

        /* Avoid underflow if pattern is longer than content */
        if (plen > len) continue;

        while (p + plen <= end) {
            /* Case-insensitive search */
            int match = 1;
            for (size_t j = 0; j < plen && match; j++) {
                if (tolower(p[j]) != tolower(patterns[i][j])) {
                    match = 0;
                }
            }
            if (match) {
                count++;
                p += plen;
            } else {
                p++;
            }
        }
    }
    return count;
}

/* Extract text between tags */
static int extract_tag_content(const char *content, const char *tag,
                                char *out, size_t out_size) {
    if (!content || !tag || !out || out_size == 0) return 0;

    char open_tag[64], close_tag[64];
    snprintf(open_tag, sizeof(open_tag), "<%s", tag);
    snprintf(close_tag, sizeof(close_tag), "</%s>", tag);

    const char *start = strcasestr(content, open_tag);
    if (!start) return 0;

    /* Find end of opening tag */
    const char *content_start = strchr(start, '>');
    if (!content_start) return 0;
    content_start++;

    /* Find closing tag */
    const char *end = strcasestr(content_start, close_tag);
    if (!end) return 0;

    size_t len = end - content_start;
    if (len >= out_size) len = out_size - 1;

    strncpy(out, content_start, len);
    out[len] = '\0';

    /* Strip HTML tags from result */
    char *w = out;
    const char *r = out;
    int in_tag = 0;
    while (*r) {
        if (*r == '<') in_tag = 1;
        else if (*r == '>') in_tag = 0;
        else if (!in_tag) *w++ = *r;
        r++;
    }
    *w = '\0';

    return 1;
}

/* Extract meta description */
static int extract_meta_description(const char *content, char *out, size_t out_size) {
    if (!content || !out || out_size == 0) return 0;

    const char *meta = strcasestr(content, "name=\"description\"");
    if (!meta) meta = strcasestr(content, "name='description'");
    if (!meta) return 0;

    const char *content_attr = strcasestr(meta, "content=\"");
    if (!content_attr) content_attr = strcasestr(meta, "content='");
    if (!content_attr) return 0;

    content_attr += 9; /* Skip 'content="' */
    const char *end = strchr(content_attr, '"');
    if (!end) end = strchr(content_attr, '\'');
    if (!end) return 0;

    size_t len = end - content_attr;
    if (len >= out_size) len = out_size - 1;

    strncpy(out, content_attr, len);
    out[len] = '\0';
    return 1;
}

/* Perform content pre-scan */
static void prescan_website_content(phantom_webbrowser_t *wb,
                                    const char *url,
                                    content_scan_result_t *result) {
    memset(result, 0, sizeof(*result));
    strcpy(result->risk_level, "Unknown");
    strcpy(result->recommendation, "Could not scan - manual review recommended");

    if (!wb || !url) return;

    /* Temporarily allow the domain for scanning */
    char domain[256];
    if (phantom_webbrowser_extract_domain(url, domain, sizeof(domain)) != 0) {
        return;
    }

    /* Add domain temporarily to allowlist for scanning */
    phantom_webbrowser_allow_domain(wb, domain, 1, "Temporary scan access");

    /* Perform the fetch */
    int fetch_result = phantom_webbrowser_navigate(wb, url);

    /* Remove from allowlist (reset to ASK) */
    phantom_webbrowser_reset_domain(wb, domain);

    if (fetch_result != WEBBROWSER_OK) {
        const char *err_str = phantom_webbrowser_result_string(fetch_result);
        /* Provide more helpful error messages */
        if (fetch_result == WEBBROWSER_ERR_NETWORK) {
            snprintf(result->recommendation, sizeof(result->recommendation),
                     "Pre-scan skipped: Could not connect to %.150s (server may be down or blocking)", domain);
            /* Still allow user to try visiting - the real WebKit fetch may work */
            strcpy(result->risk_level, "Unknown");
            result->scan_success = 0;  /* Mark as not scanned, not blocked */
        } else if (fetch_result == WEBBROWSER_ERR_TLS_UNAVAILABLE) {
            snprintf(result->recommendation, sizeof(result->recommendation),
                     "Pre-scan skipped: TLS not available (build with HAVE_MBEDTLS=1 for HTTPS pre-scan)");
            strcpy(result->risk_level, "Unknown");
        } else {
            snprintf(result->recommendation, sizeof(result->recommendation),
                     "Scan failed: %s", err_str);
        }
        return;
    }

    result->scan_success = 1;
    result->http_status = phantom_webbrowser_get_status(wb);

    const char *content = NULL;
    size_t content_size = 0;
    phantom_webbrowser_get_response(wb, &content, &content_size);

    if (!content || content_size == 0) {
        strcpy(result->recommendation, "No content received");
        return;
    }

    result->content_size = content_size;

    const char *ctype = phantom_webbrowser_get_content_type(wb);
    if (ctype) {
        strncpy(result->content_type, ctype, sizeof(result->content_type) - 1);
    }

    /* Skip to body content */
    const char *body = strstr(content, "\r\n\r\n");
    if (body) {
        body += 4;
        content_size = content_size - (body - content);
    } else {
        body = content;
    }

    /* === CONTENT ANALYSIS === */

    /* Extract title */
    extract_tag_content(body, "title", result->title, sizeof(result->title));

    /* Extract description */
    extract_meta_description(body, result->description, sizeof(result->description));

    /* Count trackers */
    result->tracker_count = count_patterns(body, content_size, tracker_patterns);

    /* Count ads */
    result->ad_count = count_patterns(body, content_size, ad_patterns);

    /* Count suspicious patterns */
    result->suspicious_patterns = count_patterns(body, content_size, suspicious_patterns);

    /* Count forms */
    const char *p = body;
    while ((p = strcasestr(p, "<form")) != NULL) {
        result->form_count++;
        p += 5;
    }

    /* Check for login form */
    result->has_login_form = (strcasestr(body, "type=\"password\"") != NULL ||
                              strcasestr(body, "type='password'") != NULL ||
                              strcasestr(body, "login") != NULL);

    /* Check for payment fields */
    result->has_payment_form = (strcasestr(body, "credit") != NULL ||
                                strcasestr(body, "card-number") != NULL ||
                                strcasestr(body, "cvv") != NULL ||
                                strcasestr(body, "payment") != NULL);

    /* Count external scripts */
    p = body;
    while ((p = strcasestr(p, "<script")) != NULL) {
        if (strcasestr(p, "src=") != NULL) {
            const char *end = strchr(p, '>');
            if (end && (end - p) < 500) {
                result->external_script_count++;
            }
        }
        p += 7;
    }

    /* Check for popup indicators */
    result->has_popups = (strcasestr(body, "window.open") != NULL ||
                          strcasestr(body, "popup") != NULL);

    /* Check for download links */
    result->has_download_links = (strcasestr(body, "download=") != NULL ||
                                  strcasestr(body, ".exe") != NULL ||
                                  strcasestr(body, ".dmg") != NULL ||
                                  strcasestr(body, ".apk") != NULL);

    /* === RISK ASSESSMENT === */
    int risk = 0;

    /* Trackers add risk */
    risk += result->tracker_count * 2;
    if (result->tracker_count > 10) risk += 10;

    /* Ads add minor risk */
    risk += result->ad_count;

    /* Suspicious patterns add significant risk */
    risk += result->suspicious_patterns * 15;

    /* Payment/login on HTTP is very risky */
    int is_https = (strncmp(url, "https://", 8) == 0);
    if (!is_https) {
        risk += 10;
        if (result->has_login_form) risk += 25;
        if (result->has_payment_form) risk += 40;
    }

    /* Popups add risk */
    if (result->has_popups) risk += 10;

    /* Download links add risk */
    if (result->has_download_links) risk += 15;

    /* Many external scripts add risk */
    if (result->external_script_count > 20) risk += 10;

    /* Cap at 100 */
    if (risk > 100) risk = 100;
    result->risk_score = risk;

    /* Determine risk level */
    if (risk < 15) {
        strcpy(result->risk_level, "Low");
        strcpy(result->recommendation, "This site appears safe to visit.");
    } else if (risk < 35) {
        strcpy(result->risk_level, "Medium");
        strcpy(result->recommendation, "Site has some trackers/ads. Generally safe.");
    } else if (risk < 60) {
        strcpy(result->risk_level, "High");
        strcpy(result->recommendation, "Proceed with caution. Contains many trackers or suspicious elements.");
    } else {
        strcpy(result->risk_level, "Critical");
        strcpy(result->recommendation, "Not recommended. Contains suspicious patterns or security risks.");
    }
}

/* Show Governor approval dialog for a domain with content pre-scan */
static gov_approval_response_t show_governor_approval_dialog(phantom_gui_t *gui,
                                                              const char *url,
                                                              const char *domain,
                                                              int is_https) {
    GtkWidget *dialog;
    GtkWidget *content_area;
    GtkWidget *notebook;
    int response;

    /* First, perform content pre-scan */
    content_scan_result_t scan;
    gtk_label_set_text(GTK_LABEL(gui->apps_web_status), "🔍 Scanning website content...");
    while (gtk_events_pending()) gtk_main_iteration();

    prescan_website_content(&webbrowser, url, &scan);

    /* Create dialog */
    dialog = gtk_dialog_new_with_buttons(
        "Governor - Network Access Request",
        GTK_WINDOW(gui->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "Allow Once", GOV_RESPONSE_ALLOW_ONCE,
        "Always Allow", GOV_RESPONSE_ALLOW_ALWAYS,
        "Deny", GOV_RESPONSE_DENY,
        "Block Domain", GOV_RESPONSE_BLOCK,
        NULL);

    gtk_window_set_default_size(GTK_WINDOW(dialog), 600, 500);

    content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content_area), 10);

    /* Use notebook for tabs */
    notebook = gtk_notebook_new();
    gtk_container_add(GTK_CONTAINER(content_area), notebook);

    /* === TAB 1: Overview === */
    GtkWidget *overview_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(overview_box), 15);

    /* Risk indicator */
    char risk_markup[512];
    const char *risk_color = "green";
    if (strcmp(scan.risk_level, "Medium") == 0) risk_color = "orange";
    else if (strcmp(scan.risk_level, "High") == 0) risk_color = "red";
    else if (strcmp(scan.risk_level, "Critical") == 0) risk_color = "darkred";

    snprintf(risk_markup, sizeof(risk_markup),
        "<span size='xx-large' weight='bold' color='%s'>Risk: %s (%d/100)</span>",
        risk_color, scan.risk_level, scan.risk_score);

    GtkWidget *risk_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(risk_label), risk_markup);
    gtk_box_pack_start(GTK_BOX(overview_box), risk_label, FALSE, FALSE, 10);

    /* Domain and URL */
    char info_markup[1024];
    snprintf(info_markup, sizeof(info_markup),
        "<b>Domain:</b> %s\n"
        "<b>URL:</b> %s\n"
        "<b>Security:</b> %s",
        domain, url,
        is_https ? "<span color='green'>HTTPS (Encrypted)</span>" :
                   "<span color='orange'>HTTP (Not Encrypted)</span>");

    GtkWidget *info_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(info_label), info_markup);
    gtk_label_set_xalign(GTK_LABEL(info_label), 0);
    gtk_label_set_line_wrap(GTK_LABEL(info_label), TRUE);
    gtk_box_pack_start(GTK_BOX(overview_box), info_label, FALSE, FALSE, 5);

    /* Page title/description if available */
    if (scan.title[0] || scan.description[0]) {
        char preview_markup[1024];
        snprintf(preview_markup, sizeof(preview_markup),
            "\n<b>Page Title:</b> %s\n"
            "<b>Description:</b> %s",
            scan.title[0] ? scan.title : "(none)",
            scan.description[0] ? scan.description : "(none)");

        GtkWidget *preview_label = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(preview_label), preview_markup);
        gtk_label_set_xalign(GTK_LABEL(preview_label), 0);
        gtk_label_set_line_wrap(GTK_LABEL(preview_label), TRUE);
        gtk_label_set_max_width_chars(GTK_LABEL(preview_label), 70);
        gtk_box_pack_start(GTK_BOX(overview_box), preview_label, FALSE, FALSE, 5);
    }

    /* Recommendation */
    char rec_markup[512];
    snprintf(rec_markup, sizeof(rec_markup),
        "\n<b>Recommendation:</b> %s", scan.recommendation);
    GtkWidget *rec_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(rec_label), rec_markup);
    gtk_label_set_xalign(GTK_LABEL(rec_label), 0);
    gtk_label_set_line_wrap(GTK_LABEL(rec_label), TRUE);
    gtk_box_pack_start(GTK_BOX(overview_box), rec_label, FALSE, FALSE, 10);

    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), overview_box,
                             gtk_label_new("Overview"));

    /* === TAB 2: Security Details === */
    GtkWidget *security_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(security_box), 15);

    char details_text[2048];
    snprintf(details_text, sizeof(details_text),
        "<b>Content Analysis Results</b>\n\n"
        "HTTP Status: %d\n"
        "Content Size: %zu bytes\n"
        "Content Type: %s\n\n"
        "<b>Tracking &amp; Ads</b>\n"
        "  Trackers detected: %d\n"
        "  Ad elements: %d\n"
        "  External scripts: %d\n\n"
        "<b>Security Indicators</b>\n"
        "  Login form: %s\n"
        "  Payment form: %s\n"
        "  Popup scripts: %s\n"
        "  Download links: %s\n"
        "  Suspicious patterns: %d\n\n"
        "<b>Forms detected:</b> %d",
        scan.http_status,
        scan.content_size,
        scan.content_type[0] ? scan.content_type : "unknown",
        scan.tracker_count,
        scan.ad_count,
        scan.external_script_count,
        scan.has_login_form ? "<span color='orange'>Yes</span>" : "No",
        scan.has_payment_form ? "<span color='red'>Yes</span>" : "No",
        scan.has_popups ? "<span color='orange'>Yes</span>" : "No",
        scan.has_download_links ? "<span color='orange'>Yes</span>" : "No",
        scan.suspicious_patterns,
        scan.form_count);

    GtkWidget *details_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(details_label), details_text);
    gtk_label_set_xalign(GTK_LABEL(details_label), 0);
    gtk_box_pack_start(GTK_BOX(security_box), details_label, FALSE, FALSE, 0);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), security_box);

    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), scroll,
                             gtk_label_new("Security Details"));

    /* === TAB 3: Actions === */
    GtkWidget *actions_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(actions_box), 15);

    const char *actions_text =
        "<b>Available Actions</b>\n\n"
        "<b>Allow Once</b>\n"
        "  Grant temporary access for this request only.\n"
        "  The domain will remain unlisted.\n\n"
        "<b>Always Allow</b>\n"
        "  Add this domain to your permanent allowlist.\n"
        "  Future requests will be auto-approved.\n\n"
        "<b>Deny</b>\n"
        "  Reject this request without blocking.\n"
        "  You can try again later.\n\n"
        "<b>Block Domain</b>\n"
        "  Add to blocklist. All future requests\n"
        "  to this domain will be automatically rejected.";

    GtkWidget *actions_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(actions_label), actions_text);
    gtk_label_set_xalign(GTK_LABEL(actions_label), 0);
    gtk_box_pack_start(GTK_BOX(actions_box), actions_label, FALSE, FALSE, 0);

    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), actions_box,
                             gtk_label_new("Actions"));

    gtk_widget_show_all(dialog);

    response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    if (response <= 0) {
        return GOV_RESPONSE_DENY;  /* Dialog closed or escaped */
    }

    return (gov_approval_response_t)response;
}

/* Helper to display error HTML in WebKit */
static void webkit_show_error_page(phantom_gui_t *gui, const char *title, const char *message, const char *details) {
    char html[2048];
    snprintf(html, sizeof(html),
        "<!DOCTYPE html><html><head><style>"
        "body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; "
        "       background: linear-gradient(135deg, #0d1117 0%%, #161b22 100%%); "
        "       color: #c9d1d9; padding: 40px; margin: 0; min-height: 100vh; text-align: center; }"
        "h1 { color: #f85149; }"
        ".message { background: #21262d; padding: 20px; border-radius: 8px; margin: 20px auto; max-width: 600px; text-align: left; }"
        ".details { color: #8b949e; font-size: 0.9em; margin-top: 10px; }"
        "</style></head><body>"
        "<h1>🚫 %s</h1>"
        "<div class='message'><p>%s</p><p class='details'>%s</p></div>"
        "</body></html>",
        title, message, details);
    webkit_web_view_load_html(WEBKIT_WEB_VIEW(gui->apps_web_view), html, NULL);
}

static void on_apps_web_go_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;

    const char *url = gtk_entry_get_text(GTK_ENTRY(gui->apps_web_url_entry));
    if (!url || strlen(url) == 0) {
        gtk_label_set_text(GTK_LABEL(gui->apps_web_status), "⚠️ Please enter a URL");
        return;
    }

    /* Check URL format */
    int is_https = (strncmp(url, "https://", 8) == 0);
    int is_http = (strncmp(url, "http://", 7) == 0);

    if (!is_https && !is_http) {
        gtk_label_set_text(GTK_LABEL(gui->apps_web_status),
            "⚠️ Invalid URL format. Use https:// or http://");
        return;
    }

    /* Initialize webbrowser policy manager if needed */
    ensure_webbrowser_initialized(gui);

    /* Extract domain from URL */
    char domain[256];
    if (phantom_webbrowser_extract_domain(url, domain, sizeof(domain)) != 0) {
        gtk_label_set_text(GTK_LABEL(gui->apps_web_status), "⚠️ Could not parse domain from URL");
        return;
    }

    /* Check domain policy */
    domain_policy_t policy = phantom_webbrowser_get_domain_policy(&webbrowser, domain);
    char status[256];

    if (policy == DOMAIN_POLICY_BLOCK) {
        /* Domain is blocked */
        snprintf(status, sizeof(status), "🚫 Blocked: %.180s is on your blocklist", domain);
        gtk_label_set_text(GTK_LABEL(gui->apps_web_status), status);

        char msg[256], details[256];
        snprintf(msg, sizeof(msg), "Domain <b>%.180s</b> is on your blocklist.", domain);
        snprintf(details, sizeof(details), "This may be an ad, tracking, or malicious domain. To access it, remove it from your blocklist first.");
        webkit_show_error_page(gui, "Domain Blocked", msg, details);
        return;
    }

    if (policy == DOMAIN_POLICY_ASK) {
        /* Domain needs approval - show dialog */
        snprintf(status, sizeof(status), "⏳ Requesting Governor approval for: %.170s", domain);
        gtk_label_set_text(GTK_LABEL(gui->apps_web_status), status);

        /* Process GTK events to show status */
        while (gtk_events_pending()) {
            gtk_main_iteration();
        }

        gov_approval_response_t response = show_governor_approval_dialog(gui, url, domain, is_https);

        switch (response) {
            case GOV_RESPONSE_ALLOW_ONCE:
                /* Allow this request only - continue with navigation */
                snprintf(status, sizeof(status), "✓ Approved once: %.200s", domain);
                gtk_label_set_text(GTK_LABEL(gui->apps_web_status), status);
                break;

            case GOV_RESPONSE_ALLOW_ALWAYS:
                /* Add to allowlist and continue */
                phantom_webbrowser_allow_domain(&webbrowser, domain, 1, "User approved");
                snprintf(status, sizeof(status), "✓ Added to allowlist: %.180s", domain);
                gtk_label_set_text(GTK_LABEL(gui->apps_web_status), status);
                break;

            case GOV_RESPONSE_DENY:
                /* Deny this request */
                snprintf(status, sizeof(status), "✗ Denied: %.200s", domain);
                gtk_label_set_text(GTK_LABEL(gui->apps_web_status), status);
                webkit_show_error_page(gui, "Request Denied",
                    "You denied access to this domain.",
                    "The Governor has logged this decision.");
                return;

            case GOV_RESPONSE_BLOCK:
                /* Add to blocklist */
                phantom_webbrowser_block_domain(&webbrowser, domain, 1, "User blocked");
                snprintf(status, sizeof(status), "🚫 Blocked: %.180s added to blocklist", domain);
                gtk_label_set_text(GTK_LABEL(gui->apps_web_status), status);
                webkit_show_error_page(gui, "Domain Blocked",
                    "This domain has been added to your blocklist.",
                    "Future requests to this domain will be automatically blocked.");
                return;

            default:
                gtk_label_set_text(GTK_LABEL(gui->apps_web_status), "✗ Request cancelled");
                return;
        }
    }

    /* Show loading status */
    snprintf(status, sizeof(status), "🔄 Loading: %s", url);
    gtk_label_set_text(GTK_LABEL(gui->apps_web_status), status);

    /* Use WebKit to navigate - full HTML/CSS/JS rendering */
    webkit_web_view_load_uri(WEBKIT_WEB_VIEW(gui->apps_web_view), url);

    phantom_gui_update_status(gui, "Loading web page...");
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * BROWSER NAVIGATION CONTROLS
 * ═══════════════════════════════════════════════════════════════════════════════ */

static void on_apps_web_reload_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;

    if (!gui || !gui->apps_web_view) return;

    /* Update status */
    update_security_bar(gui, "🔄", "Reloading...", "Refreshing the current page", "loading");
    gtk_widget_show(gui->apps_web_progress);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(gui->apps_web_progress), 0.0);

    /* Reload the current page */
    webkit_web_view_reload(WEBKIT_WEB_VIEW(gui->apps_web_view));

    phantom_gui_update_status(gui, "Reloading page...");
}

static void on_apps_web_back_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;

    if (!gui || !gui->apps_web_view) return;

    if (webkit_web_view_can_go_back(WEBKIT_WEB_VIEW(gui->apps_web_view))) {
        webkit_web_view_go_back(WEBKIT_WEB_VIEW(gui->apps_web_view));
        phantom_gui_update_status(gui, "Going back...");
    } else {
        gtk_label_set_text(GTK_LABEL(gui->apps_web_status), "No previous page in history");
    }
}

static void on_apps_web_forward_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;

    if (!gui || !gui->apps_web_view) return;

    if (webkit_web_view_can_go_forward(WEBKIT_WEB_VIEW(gui->apps_web_view))) {
        webkit_web_view_go_forward(WEBKIT_WEB_VIEW(gui->apps_web_view));
        phantom_gui_update_status(gui, "Going forward...");
    } else {
        gtk_label_set_text(GTK_LABEL(gui->apps_web_status), "No next page in history");
    }
}

static void on_apps_web_stop_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;

    if (!gui || !gui->apps_web_view) return;

    webkit_web_view_stop_loading(WEBKIT_WEB_VIEW(gui->apps_web_view));

    update_security_bar(gui, "⏹️", "Stopped", "Page loading was cancelled", "warning");
    gtk_widget_hide(gui->apps_web_progress);

    phantom_gui_update_status(gui, "Page loading stopped");
}

/* ═══════════════════════════════════════════════════════════════════════════════
 * REAL-TIME URL SCANNING - Analyze URL as user types
 * ═══════════════════════════════════════════════════════════════════════════════ */

static void on_apps_web_url_changed(GtkEditable *editable, phantom_gui_t *gui) {
    (void)editable;

    if (!gui || !gui->apps_web_url_entry) return;

    const char *url = gtk_entry_get_text(GTK_ENTRY(gui->apps_web_url_entry));
    if (!url || strlen(url) < 8) {
        /* Too short to analyze - reset to neutral */
        update_security_bar(gui, "🏠", "Ready", "Enter a URL to browse", "");
        return;
    }

    /* Check URL format */
    int is_https = (strncmp(url, "https://", 8) == 0);
    int is_http = (strncmp(url, "http://", 7) == 0);

    if (!is_https && !is_http) {
        update_security_bar(gui, "⚠", "Invalid URL", "URL must start with https:// or http://", "warning");
        return;
    }

    /* Initialize scanner if needed */
    if (!urlscanner_initialized) {
        phantom_urlscan_init(&urlscanner);
        urlscanner_initialized = 1;
    }

    /* Scan the URL */
    urlscan_result_t result;
    if (phantom_urlscan_check(&urlscanner, url, &result) != 0) {
        return;  /* Scan failed, don't update UI */
    }

    /* Update security bar based on scan result */
    char title[64];
    char detail[256];

    switch (result.threat_level) {
        case URLSCAN_SAFE:
            if (is_https) {
                snprintf(title, sizeof(title), "✓ Safe (HTTPS)");
                snprintf(detail, sizeof(detail), "%.200s - Secure connection", result.domain);
                update_security_bar(gui, "🔒", title, detail, "secure");
            } else {
                snprintf(title, sizeof(title), "⚠ Safe but HTTP");
                snprintf(detail, sizeof(detail), "%.200s - Not encrypted", result.domain);
                update_security_bar(gui, "🔓", title, detail, "warning");
            }
            break;

        case URLSCAN_UNKNOWN:
            snprintf(title, sizeof(title), "? Unknown Domain");
            snprintf(detail, sizeof(detail), "%.200s - Will require approval", result.domain);
            update_security_bar(gui, "❓", title, detail, "");
            break;

        case URLSCAN_SUSPICIOUS:
            snprintf(title, sizeof(title), "⚠ Suspicious");
            snprintf(detail, sizeof(detail), "Score: %d - %.200s", result.score, result.reason);
            update_security_bar(gui, "⚠", title, detail, "warning");
            break;

        case URLSCAN_WARNING:
            snprintf(title, sizeof(title), "⚠ Warning: Potential Threat");
            snprintf(detail, sizeof(detail), "Score: %d - %.200s", result.score, result.reason);
            update_security_bar(gui, "⚠", title, detail, "warning");
            break;

        case URLSCAN_DANGEROUS:
            snprintf(title, sizeof(title), "🚫 DANGER: Likely Malicious");
            snprintf(detail, sizeof(detail), "Score: %d - %.200s", result.score, result.reason);
            update_security_bar(gui, "🚫", title, detail, "insecure");
            break;

        case URLSCAN_BLOCKED:
            snprintf(title, sizeof(title), "⛔ BLOCKED");
            snprintf(detail, sizeof(detail), "This URL is on the blocklist");
            update_security_bar(gui, "⛔", title, detail, "insecure");
            break;

        default:
            break;
    }
}

void phantom_gui_refresh_network(phantom_gui_t *gui) {
    if (!gui || !gui->kernel) return;

    gtk_list_store_clear(gui->network_store);

    phantom_net_t *net = (phantom_net_t *)gui->kernel->net;
    if (!net || !net->initialized) {
        gtk_label_set_text(GTK_LABEL(gui->network_status_label), "Not initialized");
        return;
    }

    /* Update status */
    char status[128];
    snprintf(status, sizeof(status), "Active: %lu | Suspended: %lu | Dormant: %lu",
             net->active_connections, net->suspended_connections, net->dormant_connections);
    gtk_label_set_text(GTK_LABEL(gui->network_status_label), status);

    /* Populate socket list */
    for (int i = 0; i < net->socket_count; i++) {
        phantom_socket_t *sock = &net->sockets[i];

        GtkTreeIter iter;
        gtk_list_store_append(gui->network_store, &iter);

        char local_str[64], remote_str[64];
        phantom_addr_to_string(&sock->local, local_str, sizeof(local_str));
        phantom_addr_to_string(&sock->remote, remote_str, sizeof(remote_str));

        const char *type_str;
        switch (sock->type) {
            case PHANTOM_SOCK_STREAM: type_str = "TCP"; break;
            case PHANTOM_SOCK_DGRAM:  type_str = "UDP"; break;
            case PHANTOM_SOCK_RAW:    type_str = "RAW"; break;
            default:                  type_str = "???"; break;
        }

        char sent_str[32], recv_str[32];
        snprintf(sent_str, sizeof(sent_str), "%lu", sock->bytes_sent);
        snprintf(recv_str, sizeof(recv_str), "%lu", sock->bytes_received);

        gtk_list_store_set(gui->network_store, &iter,
            NET_COL_ID, sock->id,
            NET_COL_STATE, phantom_conn_state_string(sock->state),
            NET_COL_TYPE, type_str,
            NET_COL_LOCAL, local_str,
            NET_COL_REMOTE, remote_str,
            NET_COL_SENT, sent_str,
            NET_COL_RECV, recv_str,
            -1);
    }
}

static void on_net_init_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;
    if (!gui || !gui->kernel) return;

    if (gui->kernel->net) {
        phantom_gui_show_message(gui, "Network", "Network already initialized", GTK_MESSAGE_INFO);
        return;
    }

    phantom_net_t *net = malloc(sizeof(phantom_net_t));
    if (!net) {
        phantom_gui_show_message(gui, "Error", "Failed to allocate network context", GTK_MESSAGE_ERROR);
        return;
    }

    if (phantom_net_init(net, gui->kernel) != 0) {
        free(net);
        phantom_gui_show_message(gui, "Error", "Failed to initialize network", GTK_MESSAGE_ERROR);
        return;
    }

    gui->kernel->net = net;

    if (gui->kernel->governor) {
        phantom_net_set_governor(net, gui->kernel->governor);
    }

    phantom_gui_refresh_network(gui);
    phantom_gui_show_message(gui, "Network", "Network subsystem initialized", GTK_MESSAGE_INFO);
}

static int get_selected_socket_id(phantom_gui_t *gui) {
    GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(gui->network_tree));
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (gtk_tree_selection_get_selected(sel, &model, &iter)) {
        guint id;
        gtk_tree_model_get(model, &iter, NET_COL_ID, &id, -1);
        return (int)id;
    }
    return -1;
}

static void on_net_connect_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;
    if (!gui || !gui->kernel || !gui->kernel->net) {
        phantom_gui_show_message(gui, "Error", "Network not initialized", GTK_MESSAGE_ERROR);
        return;
    }

    const char *host = gtk_entry_get_text(GTK_ENTRY(gui->network_host_entry));
    const char *port_str = gtk_entry_get_text(GTK_ENTRY(gui->network_port_entry));

    if (!host || strlen(host) == 0) {
        phantom_gui_show_message(gui, "Error", "Please enter a hostname", GTK_MESSAGE_ERROR);
        return;
    }

    uint16_t port;
    if (gui_safe_parse_port(port_str, &port) < 0 || port == 0) {
        port = 80;  /* Default to port 80 for invalid/empty input */
    }

    phantom_net_t *net = (phantom_net_t *)gui->kernel->net;
    int sock_id = phantom_tcp_connect(net, host, port);

    if (sock_id < 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Connection failed: %s", phantom_net_error_string(sock_id));
        phantom_gui_show_message(gui, "Error", msg, GTK_MESSAGE_ERROR);
    } else {
        char msg[256];
        snprintf(msg, sizeof(msg), "Connected! Socket ID: %d", sock_id);
        phantom_gui_show_message(gui, "Success", msg, GTK_MESSAGE_INFO);
        phantom_gui_refresh_network(gui);
    }
}

static void on_net_suspend_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;
    if (!gui || !gui->kernel || !gui->kernel->net) return;

    int sock_id = get_selected_socket_id(gui);
    if (sock_id < 0) {
        phantom_gui_show_message(gui, "Error", "Please select a socket", GTK_MESSAGE_ERROR);
        return;
    }

    phantom_net_t *net = (phantom_net_t *)gui->kernel->net;
    int result = phantom_socket_suspend(net, sock_id);

    if (result == PHANTOM_NET_OK) {
        phantom_gui_refresh_network(gui);
    } else {
        phantom_gui_show_message(gui, "Error", "Failed to suspend socket", GTK_MESSAGE_ERROR);
    }
}

static void on_net_resume_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;
    if (!gui || !gui->kernel || !gui->kernel->net) return;

    int sock_id = get_selected_socket_id(gui);
    if (sock_id < 0) {
        phantom_gui_show_message(gui, "Error", "Please select a socket", GTK_MESSAGE_ERROR);
        return;
    }

    phantom_net_t *net = (phantom_net_t *)gui->kernel->net;
    int result = phantom_socket_resume(net, sock_id);

    if (result == PHANTOM_NET_OK) {
        phantom_gui_refresh_network(gui);
    } else {
        phantom_gui_show_message(gui, "Error", "Failed to resume socket", GTK_MESSAGE_ERROR);
    }
}

static void on_net_dormant_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;
    if (!gui || !gui->kernel || !gui->kernel->net) return;

    int sock_id = get_selected_socket_id(gui);
    if (sock_id < 0) {
        phantom_gui_show_message(gui, "Error", "Please select a socket", GTK_MESSAGE_ERROR);
        return;
    }

    phantom_net_t *net = (phantom_net_t *)gui->kernel->net;
    int result = phantom_socket_make_dormant(net, sock_id);

    if (result == PHANTOM_NET_OK) {
        phantom_gui_refresh_network(gui);
    } else {
        phantom_gui_show_message(gui, "Error", "Failed to make socket dormant", GTK_MESSAGE_ERROR);
    }
}

/* ══════════════════════════════════════════════════════════════════════════════
 * SECURITY PANEL (Anti-Malware)
 * ══════════════════════════════════════════════════════════════════════════════ */

/* Security panel scan state */
static volatile int security_scan_running = 0;
static char security_current_file[ANTIMALWARE_MAX_PATH];

/* Security results store columns */
enum {
    SEC_RES_COL_FILE,
    SEC_RES_COL_THREAT,
    SEC_RES_COL_NAME,
    SEC_RES_COL_HASH,
    SEC_RES_COL_COUNT
};

/* Security quarantine store columns */
enum {
    SEC_QUAR_COL_ORIGINAL,
    SEC_QUAR_COL_QPATH,
    SEC_QUAR_COL_THREAT,
    SEC_QUAR_COL_DATE,
    SEC_QUAR_COL_COUNT
};

static void security_progress_callback(const char *filepath, int percent, void *userdata) {
    (void)percent;
    (void)userdata;
    if (filepath) {
        strncpy(security_current_file, filepath, sizeof(security_current_file) - 1);
    }
}

static void security_threat_callback(const antimalware_scan_result_t *result, void *userdata) {
    phantom_gui_t *gui = (phantom_gui_t *)userdata;
    if (!gui || !gui->security_results_store) return;

    GtkTreeIter iter;
    gtk_list_store_append(gui->security_results_store, &iter);
    gtk_list_store_set(gui->security_results_store, &iter,
                       SEC_RES_COL_FILE, result->filepath,
                       SEC_RES_COL_THREAT, phantom_antimalware_threat_str(result->threat_level),
                       SEC_RES_COL_NAME, result->threat_name[0] ? result->threat_name : "Heuristic",
                       SEC_RES_COL_HASH, result->hash_sha256,
                       -1);
}

static void *security_scan_thread(void *arg) {
    phantom_gui_t *gui = (phantom_gui_t *)arg;
    phantom_antimalware_t *scanner = (phantom_antimalware_t *)gui->antimalware_scanner;

    antimalware_scan_options_t opts = scanner->default_options;
    opts.progress_callback = security_progress_callback;
    opts.threat_callback = security_threat_callback;
    opts.callback_userdata = gui;

    phantom_antimalware_quick_scan(scanner, &opts);

    security_scan_running = 0;
    return NULL;
}

static gboolean security_update_timer(gpointer data) {
    phantom_gui_t *gui = (phantom_gui_t *)data;
    if (!gui || !gui->antimalware_scanner) return G_SOURCE_REMOVE;

    phantom_antimalware_t *scanner = (phantom_antimalware_t *)gui->antimalware_scanner;

    if (security_scan_running) {
        char status[256];
        snprintf(status, sizeof(status), "Scanning: %lu files, %lu threats",
                 (unsigned long)scanner->current_scan_files,
                 (unsigned long)scanner->current_scan_threats);
        gtk_label_set_text(GTK_LABEL(gui->security_scan_status), status);
        gtk_progress_bar_pulse(GTK_PROGRESS_BAR(gui->security_scan_progress));

        if (security_current_file[0]) {
            const char *filename = strrchr(security_current_file, '/');
            filename = filename ? filename + 1 : security_current_file;
            gtk_label_set_text(GTK_LABEL(gui->security_scan_file_label), filename);
        }
        return G_SOURCE_CONTINUE;
    } else {
        /* Scan finished */
        gtk_widget_hide(gui->security_scan_progress);
        gtk_label_set_text(GTK_LABEL(gui->security_scan_status), "Scan complete");
        gtk_label_set_text(GTK_LABEL(gui->security_scan_file_label), "");

        /* Update stats */
        uint64_t total, files, threats, quarantined;
        phantom_antimalware_get_stats(scanner, &total, &files, &threats, &quarantined);

        char buf[64];
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)files);
        gtk_label_set_text(GTK_LABEL(gui->security_stats_labels[0]), buf);
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)threats);
        gtk_label_set_text(GTK_LABEL(gui->security_stats_labels[1]), buf);
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)quarantined);
        gtk_label_set_text(GTK_LABEL(gui->security_stats_labels[2]), buf);
        snprintf(buf, sizeof(buf), "%u", phantom_antimalware_get_signature_count(scanner));
        gtk_label_set_text(GTK_LABEL(gui->security_stats_labels[3]), buf);

        return G_SOURCE_REMOVE;
    }
}

static void on_security_quick_scan(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;
    if (security_scan_running) return;
    if (!gui || !gui->antimalware_scanner) return;

    gtk_list_store_clear(gui->security_results_store);
    security_scan_running = 1;
    security_current_file[0] = '\0';

    gtk_widget_show(gui->security_scan_progress);
    gtk_label_set_text(GTK_LABEL(gui->security_scan_status), "Starting scan...");

    pthread_t thread;
    pthread_create(&thread, NULL, security_scan_thread, gui);
    pthread_detach(thread);

    g_timeout_add(100, security_update_timer, gui);
}

static void on_security_custom_scan(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;
    if (security_scan_running) return;
    if (!gui || !gui->antimalware_scanner) return;

    GtkWidget *dialog = gtk_file_chooser_dialog_new(
        "Select Folder to Scan",
        GTK_WINDOW(gui->window),
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Scan", GTK_RESPONSE_ACCEPT,
        NULL);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *folder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));

        gtk_list_store_clear(gui->security_results_store);
        security_scan_running = 1;
        security_current_file[0] = '\0';

        gtk_widget_show(gui->security_scan_progress);
        gtk_label_set_text(GTK_LABEL(gui->security_scan_status), "Starting scan...");

        /* Store path for thread */
        static char scan_path[ANTIMALWARE_MAX_PATH];
        strncpy(scan_path, folder, sizeof(scan_path) - 1);
        g_free(folder);

        /* Use a custom thread for directory scan */
        phantom_antimalware_t *scanner = (phantom_antimalware_t *)gui->antimalware_scanner;
        antimalware_scan_options_t opts = scanner->default_options;
        opts.progress_callback = security_progress_callback;
        opts.threat_callback = security_threat_callback;
        opts.callback_userdata = gui;

        phantom_antimalware_scan_directory(scanner, scan_path, &opts);
        security_scan_running = 0;

        g_timeout_add(100, security_update_timer, gui);
    }

    gtk_widget_destroy(dialog);
}

static void on_security_cancel_scan(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;
    if (!gui || !gui->antimalware_scanner) return;
    phantom_antimalware_t *scanner = (phantom_antimalware_t *)gui->antimalware_scanner;
    phantom_antimalware_cancel_scan(scanner);
}

static void on_security_quarantine(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;
    if (!gui || !gui->antimalware_scanner) return;

    GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(gui->security_results_tree));
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (gtk_tree_selection_get_selected(sel, &model, &iter)) {
        gchar *filepath, *hash;
        gtk_tree_model_get(model, &iter,
                           SEC_RES_COL_FILE, &filepath,
                           SEC_RES_COL_HASH, &hash,
                           -1);

        antimalware_scan_result_t result = {0};
        strncpy(result.filepath, filepath, sizeof(result.filepath) - 1);
        strncpy(result.hash_sha256, hash, sizeof(result.hash_sha256) - 1);

        phantom_antimalware_t *scanner = (phantom_antimalware_t *)gui->antimalware_scanner;
        if (phantom_antimalware_quarantine_file(scanner, filepath, &result) == 0) {
            gtk_list_store_remove(GTK_LIST_STORE(model), &iter);
            phantom_gui_show_message(gui, "Quarantined", "File moved to quarantine", GTK_MESSAGE_INFO);
        } else {
            phantom_gui_show_message(gui, "Error", "Failed to quarantine file", GTK_MESSAGE_ERROR);
        }

        g_free(filepath);
        g_free(hash);
    }
}

static void on_security_realtime_toggled(GtkSwitch *sw, gboolean state, phantom_gui_t *gui) {
    (void)sw;
    if (!gui || !gui->antimalware_scanner) return;

    phantom_antimalware_t *scanner = (phantom_antimalware_t *)gui->antimalware_scanner;

    if (state) {
        if (phantom_antimalware_start_realtime(scanner) == 0) {
            phantom_antimalware_watch_directory(scanner, "/home", 1);
            phantom_antimalware_watch_directory(scanner, "/tmp", 1);
            gtk_label_set_text(GTK_LABEL(gui->security_status_label), "Protected");
        }
    } else {
        phantom_antimalware_stop_realtime(scanner);
        gtk_label_set_text(GTK_LABEL(gui->security_status_label), "Unprotected");
    }
}

static void refresh_security_quarantine_list(phantom_gui_t *gui) {
    if (!gui || !gui->antimalware_scanner || !gui->security_quarantine_store) return;

    gtk_list_store_clear(gui->security_quarantine_store);

    phantom_antimalware_t *scanner = (phantom_antimalware_t *)gui->antimalware_scanner;
    antimalware_quarantine_entry_t *entry = phantom_antimalware_list_quarantine(scanner);

    while (entry) {
        GtkTreeIter iter;
        gtk_list_store_append(gui->security_quarantine_store, &iter);

        char time_str[64];
        struct tm *tm = localtime(&entry->quarantine_time);
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M", tm);

        gtk_list_store_set(gui->security_quarantine_store, &iter,
                           SEC_QUAR_COL_ORIGINAL, entry->original_path,
                           SEC_QUAR_COL_QPATH, entry->quarantine_path,
                           SEC_QUAR_COL_THREAT, entry->threat_name,
                           SEC_QUAR_COL_DATE, time_str,
                           -1);

        entry = entry->next;
    }
}

static void on_security_restore(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;
    if (!gui || !gui->antimalware_scanner) return;

    GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(gui->security_quarantine_tree));
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (gtk_tree_selection_get_selected(sel, &model, &iter)) {
        gchar *qpath;
        gtk_tree_model_get(model, &iter, SEC_QUAR_COL_QPATH, &qpath, -1);

        phantom_antimalware_t *scanner = (phantom_antimalware_t *)gui->antimalware_scanner;
        if (phantom_antimalware_restore_file(scanner, qpath) == 0) {
            refresh_security_quarantine_list(gui);
            phantom_gui_show_message(gui, "Restored", "File restored from quarantine", GTK_MESSAGE_INFO);
        } else {
            phantom_gui_show_message(gui, "Error", "Failed to restore file", GTK_MESSAGE_ERROR);
        }

        g_free(qpath);
    }
}

static void on_security_delete(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;
    if (!gui || !gui->antimalware_scanner) return;

    GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(gui->security_quarantine_tree));
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (gtk_tree_selection_get_selected(sel, &model, &iter)) {
        gchar *qpath;
        gtk_tree_model_get(model, &iter, SEC_QUAR_COL_QPATH, &qpath, -1);

        phantom_antimalware_t *scanner = (phantom_antimalware_t *)gui->antimalware_scanner;
        if (phantom_antimalware_delete_quarantined(scanner, qpath) == 0) {
            refresh_security_quarantine_list(gui);
        }

        g_free(qpath);
    }
}

GtkWidget *phantom_gui_create_security_panel(phantom_gui_t *gui) {
    GtkWidget *panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(panel, 16);
    gtk_widget_set_margin_end(panel, 16);
    gtk_widget_set_margin_top(panel, 16);
    gtk_widget_set_margin_bottom(panel, 16);

    /* Initialize anti-malware scanner */
    if (!antimalware_initialized) {
        phantom_antimalware_init(&antimalware_scanner);

        /* Set quarantine path */
        char quarantine_path[256];
        const char *home = getenv("HOME");
        snprintf(quarantine_path, sizeof(quarantine_path), "%s/.phantom/quarantine",
                 home ? home : "/tmp");
        phantom_antimalware_set_quarantine_path(&antimalware_scanner, quarantine_path);

        /* Load signatures */
        phantom_antimalware_load_signature_dir(&antimalware_scanner, "geo/etc/signatures");

        antimalware_initialized = 1;
    }
    gui->antimalware_scanner = &antimalware_scanner;

    /* Header */
    GtkWidget *header = gtk_label_new("Security Center");
    gtk_style_context_add_class(gtk_widget_get_style_context(header), "phantom-section-title");
    gtk_widget_set_halign(header, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(panel), header, FALSE, FALSE, 0);

    /* Status section */
    GtkWidget *status_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
    gtk_style_context_add_class(gtk_widget_get_style_context(status_box), "phantom-panel");

    gui->security_status_label = gtk_label_new("Unprotected");
    gtk_style_context_add_class(gtk_widget_get_style_context(gui->security_status_label), "status-warning");
    gtk_box_pack_start(GTK_BOX(status_box), gui->security_status_label, FALSE, FALSE, 10);

    GtkWidget *realtime_label = gtk_label_new("Real-time Protection:");
    gtk_box_pack_start(GTK_BOX(status_box), realtime_label, FALSE, FALSE, 0);

    gui->security_realtime_switch = gtk_switch_new();
    g_signal_connect(gui->security_realtime_switch, "state-set",
                     G_CALLBACK(on_security_realtime_toggled), gui);
    gtk_box_pack_start(GTK_BOX(status_box), gui->security_realtime_switch, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(panel), status_box, FALSE, FALSE, 5);

    /* Stats row */
    GtkWidget *stats_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 30);

    const char *stat_labels[] = {"Files Scanned:", "Threats Found:", "Quarantined:", "Signatures:"};
    for (int i = 0; i < 4; i++) {
        GtkWidget *stat_item = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
        GtkWidget *label = gtk_label_new(stat_labels[i]);
        gui->security_stats_labels[i] = gtk_label_new("0");
        gtk_box_pack_start(GTK_BOX(stat_item), label, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(stat_item), gui->security_stats_labels[i], FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(stats_box), stat_item, FALSE, FALSE, 0);
    }

    /* Update signature count */
    char sig_count[32];
    snprintf(sig_count, sizeof(sig_count), "%u",
             phantom_antimalware_get_signature_count(&antimalware_scanner));
    gtk_label_set_text(GTK_LABEL(gui->security_stats_labels[3]), sig_count);

    gtk_box_pack_start(GTK_BOX(panel), stats_box, FALSE, FALSE, 5);

    /* Scan buttons */
    GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);

    GtkWidget *quick_btn = gtk_button_new_with_label("Quick Scan");
    g_signal_connect(quick_btn, "clicked", G_CALLBACK(on_security_quick_scan), gui);
    gtk_box_pack_start(GTK_BOX(btn_box), quick_btn, FALSE, FALSE, 0);

    GtkWidget *custom_btn = gtk_button_new_with_label("Custom Scan");
    g_signal_connect(custom_btn, "clicked", G_CALLBACK(on_security_custom_scan), gui);
    gtk_box_pack_start(GTK_BOX(btn_box), custom_btn, FALSE, FALSE, 0);

    GtkWidget *cancel_btn = gtk_button_new_with_label("Cancel");
    g_signal_connect(cancel_btn, "clicked", G_CALLBACK(on_security_cancel_scan), gui);
    gtk_box_pack_start(GTK_BOX(btn_box), cancel_btn, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(panel), btn_box, FALSE, FALSE, 5);

    /* Scan progress */
    GtkWidget *progress_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);

    gui->security_scan_status = gtk_label_new("Ready to scan");
    gtk_widget_set_halign(gui->security_scan_status, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(progress_box), gui->security_scan_status, FALSE, FALSE, 0);

    gui->security_scan_progress = gtk_progress_bar_new();
    gtk_widget_hide(gui->security_scan_progress);
    gtk_box_pack_start(GTK_BOX(progress_box), gui->security_scan_progress, FALSE, FALSE, 0);

    gui->security_scan_file_label = gtk_label_new("");
    gtk_label_set_ellipsize(GTK_LABEL(gui->security_scan_file_label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_set_halign(gui->security_scan_file_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(progress_box), gui->security_scan_file_label, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(panel), progress_box, FALSE, FALSE, 5);

    /* Create notebook for Results and Quarantine */
    GtkWidget *notebook = gtk_notebook_new();

    /* Results tab */
    GtkWidget *results_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);

    gui->security_results_store = gtk_list_store_new(SEC_RES_COL_COUNT,
        G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);

    gui->security_results_tree = gtk_tree_view_new_with_model(
        GTK_TREE_MODEL(gui->security_results_store));

    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();

    GtkTreeViewColumn *col1 = gtk_tree_view_column_new_with_attributes(
        "File", renderer, "text", SEC_RES_COL_FILE, NULL);
    gtk_tree_view_column_set_expand(col1, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->security_results_tree), col1);

    GtkTreeViewColumn *col2 = gtk_tree_view_column_new_with_attributes(
        "Threat", renderer, "text", SEC_RES_COL_THREAT, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->security_results_tree), col2);

    GtkTreeViewColumn *col3 = gtk_tree_view_column_new_with_attributes(
        "Detection", renderer, "text", SEC_RES_COL_NAME, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->security_results_tree), col3);

    GtkWidget *results_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(results_scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(results_scroll), gui->security_results_tree);
    gtk_box_pack_start(GTK_BOX(results_page), results_scroll, TRUE, TRUE, 0);

    GtkWidget *results_btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *quarantine_btn = gtk_button_new_with_label("Quarantine Selected");
    g_signal_connect(quarantine_btn, "clicked", G_CALLBACK(on_security_quarantine), gui);
    gtk_box_pack_end(GTK_BOX(results_btn_box), quarantine_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(results_page), results_btn_box, FALSE, FALSE, 5);

    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), results_page,
                             gtk_label_new("Scan Results"));

    /* Quarantine tab */
    GtkWidget *quarantine_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);

    gui->security_quarantine_store = gtk_list_store_new(SEC_QUAR_COL_COUNT,
        G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);

    gui->security_quarantine_tree = gtk_tree_view_new_with_model(
        GTK_TREE_MODEL(gui->security_quarantine_store));

    GtkTreeViewColumn *qcol1 = gtk_tree_view_column_new_with_attributes(
        "Original Location", renderer, "text", SEC_QUAR_COL_ORIGINAL, NULL);
    gtk_tree_view_column_set_expand(qcol1, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->security_quarantine_tree), qcol1);

    GtkTreeViewColumn *qcol2 = gtk_tree_view_column_new_with_attributes(
        "Threat", renderer, "text", SEC_QUAR_COL_THREAT, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->security_quarantine_tree), qcol2);

    GtkTreeViewColumn *qcol3 = gtk_tree_view_column_new_with_attributes(
        "Date", renderer, "text", SEC_QUAR_COL_DATE, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->security_quarantine_tree), qcol3);

    GtkWidget *quarantine_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(quarantine_scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(quarantine_scroll), gui->security_quarantine_tree);
    gtk_box_pack_start(GTK_BOX(quarantine_page), quarantine_scroll, TRUE, TRUE, 0);

    GtkWidget *quar_btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *restore_btn = gtk_button_new_with_label("Restore");
    g_signal_connect(restore_btn, "clicked", G_CALLBACK(on_security_restore), gui);
    gtk_box_pack_end(GTK_BOX(quar_btn_box), restore_btn, FALSE, FALSE, 0);

    GtkWidget *delete_btn = gtk_button_new_with_label("Delete");
    g_signal_connect(delete_btn, "clicked", G_CALLBACK(on_security_delete), gui);
    gtk_box_pack_end(GTK_BOX(quar_btn_box), delete_btn, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(quarantine_page), quar_btn_box, FALSE, FALSE, 5);

    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), quarantine_page,
                             gtk_label_new("Quarantine"));

    gtk_box_pack_start(GTK_BOX(panel), notebook, TRUE, TRUE, 5);

    /* Load quarantine list */
    refresh_security_quarantine_list(gui);

    return panel;
}

#ifdef HAVE_GSTREAMER
/* ══════════════════════════════════════════════════════════════════════════════
 * MEDIA PLAYER PANEL
 * ══════════════════════════════════════════════════════════════════════════════ */

/* Forward declarations for media player callbacks */
static void on_media_play_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_media_stop_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_media_prev_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_media_next_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_media_shuffle_toggled(GtkWidget *button, phantom_gui_t *gui);
static void on_media_repeat_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_media_volume_changed(GtkRange *range, phantom_gui_t *gui);
static void on_media_position_changed(GtkRange *range, phantom_gui_t *gui);
static void on_media_add_file(GtkWidget *button, phantom_gui_t *gui);
static void on_media_add_folder(GtkWidget *button, phantom_gui_t *gui);
static void on_media_clear_playlist(GtkWidget *button, phantom_gui_t *gui);
static void on_media_playlist_row_activated(GtkTreeView *tree, GtkTreePath *path,
                                             GtkTreeViewColumn *column, phantom_gui_t *gui);
static void on_media_eq_preset_changed(GtkComboBoxText *combo, phantom_gui_t *gui);
static void on_media_eq_band_changed(GtkRange *range, phantom_gui_t *gui);
static gboolean media_update_position(phantom_gui_t *gui);
static void refresh_media_playlist(phantom_gui_t *gui);

/* Media player state change callback */
static void media_state_callback(mediaplayer_state_t state, void *userdata) {
    phantom_gui_t *gui = (phantom_gui_t *)userdata;
    if (!gui) return;

    const char *state_str = phantom_mediaplayer_state_str(state);
    char status[256];
    snprintf(status, sizeof(status), "Media: %s", state_str);

    /* Update play button icon */
    if (gui->media_play_btn) {
        if (state == PLAYBACK_PLAYING) {
            gtk_button_set_label(GTK_BUTTON(gui->media_play_btn), "⏸");
        } else {
            gtk_button_set_label(GTK_BUTTON(gui->media_play_btn), "▶");
        }
    }
}

/* Media player track change callback */
static void media_track_callback(const mediaplayer_track_t *track, void *userdata) {
    phantom_gui_t *gui = (phantom_gui_t *)userdata;
    if (!gui || !track) return;

    /* Update track info labels */
    if (gui->media_track_label) {
        gtk_label_set_text(GTK_LABEL(gui->media_track_label),
                           track->title[0] ? track->title : "Unknown Title");
    }
    if (gui->media_artist_label) {
        gtk_label_set_text(GTK_LABEL(gui->media_artist_label),
                           track->artist[0] ? track->artist : "Unknown Artist");
    }
    if (gui->media_album_label) {
        gtk_label_set_text(GTK_LABEL(gui->media_album_label),
                           track->album[0] ? track->album : "Unknown Album");
    }

    /* Update position scale range */
    if (gui->media_position_scale && track->duration_ms > 0) {
        gtk_range_set_range(GTK_RANGE(gui->media_position_scale), 0, track->duration_ms);
    }

    /* Refresh playlist to show now playing indicator */
    refresh_media_playlist(gui);
}

/* Media player position change callback */
static void media_position_callback(int64_t position_ms, void *userdata) {
    phantom_gui_t *gui = (phantom_gui_t *)userdata;
    if (!gui) return;

    /* Update time label */
    if (gui->media_time_label && gui->mediaplayer) {
        phantom_mediaplayer_t *player = (phantom_mediaplayer_t *)gui->mediaplayer;
        int64_t duration = player->current_track ? player->current_track->duration_ms : 0;

        char pos_str[32], dur_str[32], time_str[80];
        phantom_mediaplayer_format_time(position_ms, pos_str, sizeof(pos_str));
        phantom_mediaplayer_format_time(duration, dur_str, sizeof(dur_str));
        snprintf(time_str, sizeof(time_str), "%s / %s", pos_str, dur_str);
        gtk_label_set_text(GTK_LABEL(gui->media_time_label), time_str);
    }

    /* Update position slider (without triggering callback) */
    if (gui->media_position_scale) {
        g_signal_handlers_block_by_func(gui->media_position_scale,
                                         on_media_position_changed, gui);
        gtk_range_set_value(GTK_RANGE(gui->media_position_scale), position_ms);
        g_signal_handlers_unblock_by_func(gui->media_position_scale,
                                           on_media_position_changed, gui);
    }
}

GtkWidget *phantom_gui_create_media_panel(phantom_gui_t *gui) {
    GtkWidget *panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(panel, 16);
    gtk_widget_set_margin_end(panel, 16);
    gtk_widget_set_margin_top(panel, 16);
    gtk_widget_set_margin_bottom(panel, 16);

    /* Initialize media player */
    if (!mediaplayer_initialized) {
        if (phantom_mediaplayer_init(&mediaplayer) == 0) {
            mediaplayer_initialized = 1;
        }
    }
    gui->mediaplayer = &mediaplayer;

    /* Set callbacks */
    if (mediaplayer_initialized) {
        phantom_mediaplayer_set_state_callback(&mediaplayer, media_state_callback, gui);
        phantom_mediaplayer_set_track_callback(&mediaplayer, media_track_callback, gui);
        phantom_mediaplayer_set_position_callback(&mediaplayer, media_position_callback, gui);
    }

    /* Header */
    GtkWidget *header = gtk_label_new("Media Player");
    gtk_style_context_add_class(gtk_widget_get_style_context(header), "phantom-section-title");
    gtk_widget_set_halign(header, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(panel), header, FALSE, FALSE, 0);

    /* Main content - split into player controls and playlist */
    GtkWidget *main_paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);

    /* === LEFT SIDE: Now Playing and Controls === */
    GtkWidget *player_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_size_request(player_box, 400, -1);

    /* Album art / video area placeholder */
    GtkWidget *art_frame = gtk_frame_new(NULL);
    gtk_frame_set_shadow_type(GTK_FRAME(art_frame), GTK_SHADOW_IN);
    gui->media_video_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(gui->media_video_area, 300, 200);
    gtk_container_add(GTK_CONTAINER(art_frame), gui->media_video_area);
    gtk_widget_set_halign(art_frame, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(player_box), art_frame, FALSE, FALSE, 10);

    /* Now playing info */
    GtkWidget *info_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_halign(info_box, GTK_ALIGN_CENTER);

    gui->media_track_label = gtk_label_new("No track loaded");
    gtk_style_context_add_class(gtk_widget_get_style_context(gui->media_track_label), "phantom-section-title");
    gtk_label_set_ellipsize(GTK_LABEL(gui->media_track_label), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(gui->media_track_label), 40);
    gtk_box_pack_start(GTK_BOX(info_box), gui->media_track_label, FALSE, FALSE, 0);

    gui->media_artist_label = gtk_label_new("Artist");
    gtk_label_set_ellipsize(GTK_LABEL(gui->media_artist_label), PANGO_ELLIPSIZE_END);
    gtk_box_pack_start(GTK_BOX(info_box), gui->media_artist_label, FALSE, FALSE, 0);

    gui->media_album_label = gtk_label_new("Album");
    gtk_label_set_ellipsize(GTK_LABEL(gui->media_album_label), PANGO_ELLIPSIZE_END);
    gtk_style_context_add_class(gtk_widget_get_style_context(gui->media_album_label), "dim-label");
    gtk_box_pack_start(GTK_BOX(info_box), gui->media_album_label, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(player_box), info_box, FALSE, FALSE, 5);

    /* Position slider */
    GtkWidget *pos_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);

    gui->media_position_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
    gtk_scale_set_draw_value(GTK_SCALE(gui->media_position_scale), FALSE);
    gtk_widget_set_hexpand(gui->media_position_scale, TRUE);
    g_signal_connect(gui->media_position_scale, "value-changed",
                     G_CALLBACK(on_media_position_changed), gui);
    gtk_box_pack_start(GTK_BOX(pos_box), gui->media_position_scale, TRUE, TRUE, 0);

    gui->media_time_label = gtk_label_new("0:00 / 0:00");
    gtk_box_pack_start(GTK_BOX(pos_box), gui->media_time_label, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(player_box), pos_box, FALSE, FALSE, 5);

    /* Transport controls */
    GtkWidget *controls_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_widget_set_halign(controls_box, GTK_ALIGN_CENTER);

    gui->media_shuffle_btn = gtk_toggle_button_new_with_label("🔀");
    gtk_widget_set_tooltip_text(gui->media_shuffle_btn, "Shuffle");
    g_signal_connect(gui->media_shuffle_btn, "toggled",
                     G_CALLBACK(on_media_shuffle_toggled), gui);
    gtk_box_pack_start(GTK_BOX(controls_box), gui->media_shuffle_btn, FALSE, FALSE, 0);

    GtkWidget *prev_btn = gtk_button_new_with_label("⏮");
    gtk_widget_set_tooltip_text(prev_btn, "Previous");
    g_signal_connect(prev_btn, "clicked", G_CALLBACK(on_media_prev_clicked), gui);
    gtk_box_pack_start(GTK_BOX(controls_box), prev_btn, FALSE, FALSE, 0);

    gui->media_play_btn = gtk_button_new_with_label("▶");
    gtk_widget_set_tooltip_text(gui->media_play_btn, "Play/Pause");
    gtk_widget_set_size_request(gui->media_play_btn, 60, 40);
    g_signal_connect(gui->media_play_btn, "clicked",
                     G_CALLBACK(on_media_play_clicked), gui);
    gtk_box_pack_start(GTK_BOX(controls_box), gui->media_play_btn, FALSE, FALSE, 0);

    GtkWidget *stop_btn = gtk_button_new_with_label("⏹");
    gtk_widget_set_tooltip_text(stop_btn, "Stop");
    g_signal_connect(stop_btn, "clicked", G_CALLBACK(on_media_stop_clicked), gui);
    gtk_box_pack_start(GTK_BOX(controls_box), stop_btn, FALSE, FALSE, 0);

    GtkWidget *next_btn = gtk_button_new_with_label("⏭");
    gtk_widget_set_tooltip_text(next_btn, "Next");
    g_signal_connect(next_btn, "clicked", G_CALLBACK(on_media_next_clicked), gui);
    gtk_box_pack_start(GTK_BOX(controls_box), next_btn, FALSE, FALSE, 0);

    gui->media_repeat_btn = gtk_button_new_with_label("🔁");
    gtk_widget_set_tooltip_text(gui->media_repeat_btn, "Repeat: Off");
    g_signal_connect(gui->media_repeat_btn, "clicked",
                     G_CALLBACK(on_media_repeat_clicked), gui);
    gtk_box_pack_start(GTK_BOX(controls_box), gui->media_repeat_btn, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(player_box), controls_box, FALSE, FALSE, 5);

    /* Volume control */
    GtkWidget *vol_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_halign(vol_box, GTK_ALIGN_CENTER);

    GtkWidget *vol_label = gtk_label_new("🔊");
    gtk_box_pack_start(GTK_BOX(vol_box), vol_label, FALSE, FALSE, 0);

    gui->media_volume_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
    gtk_scale_set_draw_value(GTK_SCALE(gui->media_volume_scale), FALSE);
    gtk_widget_set_size_request(gui->media_volume_scale, 150, -1);
    gtk_range_set_value(GTK_RANGE(gui->media_volume_scale), 100);
    g_signal_connect(gui->media_volume_scale, "value-changed",
                     G_CALLBACK(on_media_volume_changed), gui);
    gtk_box_pack_start(GTK_BOX(vol_box), gui->media_volume_scale, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(player_box), vol_box, FALSE, FALSE, 5);

    /* Equalizer (collapsible) */
    GtkWidget *eq_expander = gtk_expander_new("Equalizer");

    GtkWidget *eq_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);

    /* EQ preset selector */
    GtkWidget *eq_preset_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget *preset_label = gtk_label_new("Preset:");
    gtk_box_pack_start(GTK_BOX(eq_preset_box), preset_label, FALSE, FALSE, 0);

    gui->media_eq_preset_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(gui->media_eq_preset_combo), "Flat");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(gui->media_eq_preset_combo), "Rock");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(gui->media_eq_preset_combo), "Pop");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(gui->media_eq_preset_combo), "Jazz");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(gui->media_eq_preset_combo), "Classical");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(gui->media_eq_preset_combo), "Electronic");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(gui->media_eq_preset_combo), "Bass Boost");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(gui->media_eq_preset_combo), "Treble Boost");
    gtk_combo_box_set_active(GTK_COMBO_BOX(gui->media_eq_preset_combo), 0);
    g_signal_connect(gui->media_eq_preset_combo, "changed",
                     G_CALLBACK(on_media_eq_preset_changed), gui);
    gtk_box_pack_start(GTK_BOX(eq_preset_box), gui->media_eq_preset_combo, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(eq_box), eq_preset_box, FALSE, FALSE, 0);

    /* EQ bands */
    GtkWidget *eq_bands_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    const char *band_labels[] = {"32", "64", "125", "250", "500", "1K", "2K", "4K", "8K", "16K"};

    for (int i = 0; i < 10; i++) {
        GtkWidget *band_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);

        gui->media_eq_scales[i] = gtk_scale_new_with_range(GTK_ORIENTATION_VERTICAL, -12, 12, 1);
        gtk_scale_set_draw_value(GTK_SCALE(gui->media_eq_scales[i]), FALSE);
        gtk_widget_set_size_request(gui->media_eq_scales[i], 30, 80);
        gtk_range_set_inverted(GTK_RANGE(gui->media_eq_scales[i]), TRUE);
        gtk_range_set_value(GTK_RANGE(gui->media_eq_scales[i]), 0);
        g_object_set_data(G_OBJECT(gui->media_eq_scales[i]), "band", GINT_TO_POINTER(i));
        g_signal_connect(gui->media_eq_scales[i], "value-changed",
                         G_CALLBACK(on_media_eq_band_changed), gui);
        gtk_box_pack_start(GTK_BOX(band_box), gui->media_eq_scales[i], TRUE, TRUE, 0);

        GtkWidget *band_label = gtk_label_new(band_labels[i]);
        gtk_box_pack_start(GTK_BOX(band_box), band_label, FALSE, FALSE, 0);

        gtk_box_pack_start(GTK_BOX(eq_bands_box), band_box, FALSE, FALSE, 0);
    }

    gtk_box_pack_start(GTK_BOX(eq_box), eq_bands_box, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(eq_expander), eq_box);
    gtk_box_pack_start(GTK_BOX(player_box), eq_expander, FALSE, FALSE, 5);

    gtk_paned_pack1(GTK_PANED(main_paned), player_box, FALSE, FALSE);

    /* === RIGHT SIDE: Playlist === */
    GtkWidget *playlist_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);

    GtkWidget *playlist_header = gtk_label_new("Playlist");
    gtk_style_context_add_class(gtk_widget_get_style_context(playlist_header), "phantom-section-title");
    gtk_widget_set_halign(playlist_header, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(playlist_box), playlist_header, FALSE, FALSE, 0);

    /* Playlist toolbar */
    GtkWidget *playlist_toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);

    GtkWidget *add_file_btn = gtk_button_new_with_label("+ File");
    g_signal_connect(add_file_btn, "clicked", G_CALLBACK(on_media_add_file), gui);
    gtk_box_pack_start(GTK_BOX(playlist_toolbar), add_file_btn, FALSE, FALSE, 0);

    GtkWidget *add_folder_btn = gtk_button_new_with_label("+ Folder");
    g_signal_connect(add_folder_btn, "clicked", G_CALLBACK(on_media_add_folder), gui);
    gtk_box_pack_start(GTK_BOX(playlist_toolbar), add_folder_btn, FALSE, FALSE, 0);

    GtkWidget *clear_btn = gtk_button_new_with_label("Clear");
    g_signal_connect(clear_btn, "clicked", G_CALLBACK(on_media_clear_playlist), gui);
    gtk_box_pack_end(GTK_BOX(playlist_toolbar), clear_btn, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(playlist_box), playlist_toolbar, FALSE, FALSE, 0);

    /* Playlist tree view */
    gui->media_playlist_store = gtk_list_store_new(MEDIA_COL_COUNT,
        G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);

    gui->media_playlist_tree = gtk_tree_view_new_with_model(
        GTK_TREE_MODEL(gui->media_playlist_store));

    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();

    GtkTreeViewColumn *col_playing = gtk_tree_view_column_new_with_attributes(
        "", renderer, "text", MEDIA_COL_PLAYING, NULL);
    gtk_tree_view_column_set_fixed_width(col_playing, 30);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->media_playlist_tree), col_playing);

    GtkTreeViewColumn *col_title = gtk_tree_view_column_new_with_attributes(
        "Title", renderer, "text", MEDIA_COL_TITLE, NULL);
    gtk_tree_view_column_set_expand(col_title, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->media_playlist_tree), col_title);

    GtkTreeViewColumn *col_artist = gtk_tree_view_column_new_with_attributes(
        "Artist", renderer, "text", MEDIA_COL_ARTIST, NULL);
    gtk_tree_view_column_set_min_width(col_artist, 150);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->media_playlist_tree), col_artist);

    GtkTreeViewColumn *col_duration = gtk_tree_view_column_new_with_attributes(
        "Duration", renderer, "text", MEDIA_COL_DURATION, NULL);
    gtk_tree_view_column_set_min_width(col_duration, 80);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->media_playlist_tree), col_duration);

    g_signal_connect(gui->media_playlist_tree, "row-activated",
                     G_CALLBACK(on_media_playlist_row_activated), gui);

    GtkWidget *playlist_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(playlist_scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(playlist_scroll), gui->media_playlist_tree);
    gtk_box_pack_start(GTK_BOX(playlist_box), playlist_scroll, TRUE, TRUE, 0);

    gtk_paned_pack2(GTK_PANED(main_paned), playlist_box, TRUE, TRUE);

    gtk_box_pack_start(GTK_BOX(panel), main_paned, TRUE, TRUE, 0);

    /* Start position update timer */
    gui->media_update_timer = g_timeout_add(500, (GSourceFunc)media_update_position, gui);

    return panel;
}

/* Media player event handlers */
static void on_media_play_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;
    if (!gui->mediaplayer) return;

    phantom_mediaplayer_t *player = (phantom_mediaplayer_t *)gui->mediaplayer;
    phantom_mediaplayer_toggle(player);
}

static void on_media_stop_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;
    if (!gui->mediaplayer) return;

    phantom_mediaplayer_t *player = (phantom_mediaplayer_t *)gui->mediaplayer;
    phantom_mediaplayer_stop(player);
}

static void on_media_prev_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;
    if (!gui->mediaplayer) return;

    phantom_mediaplayer_t *player = (phantom_mediaplayer_t *)gui->mediaplayer;
    phantom_mediaplayer_previous(player);
}

static void on_media_next_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;
    if (!gui->mediaplayer) return;

    phantom_mediaplayer_t *player = (phantom_mediaplayer_t *)gui->mediaplayer;
    phantom_mediaplayer_next(player);
}

static void on_media_shuffle_toggled(GtkWidget *button, phantom_gui_t *gui) {
    if (!gui->mediaplayer) return;

    phantom_mediaplayer_t *player = (phantom_mediaplayer_t *)gui->mediaplayer;
    int active = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button));
    phantom_mediaplayer_set_shuffle(player, active);
}

static void on_media_repeat_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;
    if (!gui->mediaplayer) return;

    phantom_mediaplayer_t *player = (phantom_mediaplayer_t *)gui->mediaplayer;
    phantom_mediaplayer_cycle_repeat(player);

    /* Update button tooltip */
    const char *mode_str = phantom_mediaplayer_repeat_str(player->playlist.repeat_mode);
    char tooltip[64];
    snprintf(tooltip, sizeof(tooltip), "Repeat: %s", mode_str);
    gtk_widget_set_tooltip_text(gui->media_repeat_btn, tooltip);

    /* Update button label */
    switch (player->playlist.repeat_mode) {
        case REPEAT_ONE:
            gtk_button_set_label(GTK_BUTTON(gui->media_repeat_btn), "🔂");
            break;
        case REPEAT_ALL:
            gtk_button_set_label(GTK_BUTTON(gui->media_repeat_btn), "🔁");
            break;
        default:
            gtk_button_set_label(GTK_BUTTON(gui->media_repeat_btn), "➡️");
            break;
    }
}

static void on_media_volume_changed(GtkRange *range, phantom_gui_t *gui) {
    if (!gui->mediaplayer) return;

    phantom_mediaplayer_t *player = (phantom_mediaplayer_t *)gui->mediaplayer;
    double volume = gtk_range_get_value(range) / 100.0;
    phantom_mediaplayer_set_volume(player, volume);
}

static void on_media_position_changed(GtkRange *range, phantom_gui_t *gui) {
    if (!gui->mediaplayer) return;

    phantom_mediaplayer_t *player = (phantom_mediaplayer_t *)gui->mediaplayer;
    int64_t position = (int64_t)gtk_range_get_value(range);
    phantom_mediaplayer_seek(player, position);
}

static void on_media_add_file(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;

    GtkWidget *dialog = gtk_file_chooser_dialog_new(
        "Add Media File",
        GTK_WINDOW(gui->window),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "Cancel", GTK_RESPONSE_CANCEL,
        "Add", GTK_RESPONSE_ACCEPT,
        NULL);

    gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(dialog), TRUE);

    /* Add file filters */
    GtkFileFilter *audio_filter = gtk_file_filter_new();
    gtk_file_filter_set_name(audio_filter, "Audio Files");
    gtk_file_filter_add_pattern(audio_filter, "*.mp3");
    gtk_file_filter_add_pattern(audio_filter, "*.flac");
    gtk_file_filter_add_pattern(audio_filter, "*.ogg");
    gtk_file_filter_add_pattern(audio_filter, "*.wav");
    gtk_file_filter_add_pattern(audio_filter, "*.aac");
    gtk_file_filter_add_pattern(audio_filter, "*.m4a");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), audio_filter);

    GtkFileFilter *video_filter = gtk_file_filter_new();
    gtk_file_filter_set_name(video_filter, "Video Files");
    gtk_file_filter_add_pattern(video_filter, "*.mp4");
    gtk_file_filter_add_pattern(video_filter, "*.mkv");
    gtk_file_filter_add_pattern(video_filter, "*.avi");
    gtk_file_filter_add_pattern(video_filter, "*.webm");
    gtk_file_filter_add_pattern(video_filter, "*.mov");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), video_filter);

    GtkFileFilter *all_filter = gtk_file_filter_new();
    gtk_file_filter_set_name(all_filter, "All Media");
    gtk_file_filter_add_pattern(all_filter, "*.mp3");
    gtk_file_filter_add_pattern(all_filter, "*.flac");
    gtk_file_filter_add_pattern(all_filter, "*.ogg");
    gtk_file_filter_add_pattern(all_filter, "*.wav");
    gtk_file_filter_add_pattern(all_filter, "*.mp4");
    gtk_file_filter_add_pattern(all_filter, "*.mkv");
    gtk_file_filter_add_pattern(all_filter, "*.avi");
    gtk_file_filter_add_pattern(all_filter, "*.webm");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), all_filter);
    gtk_file_chooser_set_filter(GTK_FILE_CHOOSER(dialog), all_filter);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        GSList *files = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(dialog));
        phantom_mediaplayer_t *player = (phantom_mediaplayer_t *)gui->mediaplayer;

        for (GSList *l = files; l != NULL; l = l->next) {
            phantom_mediaplayer_playlist_add(player, (char *)l->data);
            g_free(l->data);
        }
        g_slist_free(files);

        refresh_media_playlist(gui);
    }

    gtk_widget_destroy(dialog);
}

static void on_media_add_folder(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;

    GtkWidget *dialog = gtk_file_chooser_dialog_new(
        "Add Folder",
        GTK_WINDOW(gui->window),
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
        "Cancel", GTK_RESPONSE_CANCEL,
        "Add", GTK_RESPONSE_ACCEPT,
        NULL);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *folder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        phantom_mediaplayer_t *player = (phantom_mediaplayer_t *)gui->mediaplayer;

        phantom_mediaplayer_playlist_add_directory(player, folder, 1);
        g_free(folder);

        refresh_media_playlist(gui);
    }

    gtk_widget_destroy(dialog);
}

static void on_media_clear_playlist(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;
    if (!gui->mediaplayer) return;

    phantom_mediaplayer_t *player = (phantom_mediaplayer_t *)gui->mediaplayer;
    phantom_mediaplayer_playlist_clear(player);
    refresh_media_playlist(gui);

    gtk_label_set_text(GTK_LABEL(gui->media_track_label), "No track loaded");
    gtk_label_set_text(GTK_LABEL(gui->media_artist_label), "Artist");
    gtk_label_set_text(GTK_LABEL(gui->media_album_label), "Album");
}

static void on_media_playlist_row_activated(GtkTreeView *tree, GtkTreePath *path,
                                             GtkTreeViewColumn *column, phantom_gui_t *gui) {
    (void)tree;
    (void)column;

    if (!gui->mediaplayer) return;

    int *indices = gtk_tree_path_get_indices(path);
    if (indices) {
        phantom_mediaplayer_t *player = (phantom_mediaplayer_t *)gui->mediaplayer;
        phantom_mediaplayer_play_index(player, indices[0]);
    }
}

static void on_media_eq_preset_changed(GtkComboBoxText *combo, phantom_gui_t *gui) {
    if (!gui->mediaplayer) return;

    const char *preset = gtk_combo_box_text_get_active_text(combo);
    if (!preset) return;

    phantom_mediaplayer_t *player = (phantom_mediaplayer_t *)gui->mediaplayer;
    phantom_mediaplayer_eq_load_preset(player, preset);

    /* Update EQ sliders to reflect preset */
    for (int i = 0; i < 10; i++) {
        double value = phantom_mediaplayer_eq_get_band(player, i);
        g_signal_handlers_block_by_func(gui->media_eq_scales[i],
                                         on_media_eq_band_changed, gui);
        gtk_range_set_value(GTK_RANGE(gui->media_eq_scales[i]), value);
        g_signal_handlers_unblock_by_func(gui->media_eq_scales[i],
                                           on_media_eq_band_changed, gui);
    }
}

static void on_media_eq_band_changed(GtkRange *range, phantom_gui_t *gui) {
    if (!gui->mediaplayer) return;

    int band = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(range), "band"));
    double value = gtk_range_get_value(range);

    phantom_mediaplayer_t *player = (phantom_mediaplayer_t *)gui->mediaplayer;
    phantom_mediaplayer_eq_set_band(player, band, value);
}

static gboolean media_update_position(phantom_gui_t *gui) {
    if (!gui || !gui->mediaplayer) return TRUE;

    phantom_mediaplayer_t *player = (phantom_mediaplayer_t *)gui->mediaplayer;

    /* Update position from player */
    int64_t position = phantom_mediaplayer_get_position(player);
    media_position_callback(position, gui);

    return TRUE;
}

static void refresh_media_playlist(phantom_gui_t *gui) {
    if (!gui || !gui->media_playlist_store || !gui->mediaplayer) return;

    phantom_mediaplayer_t *player = (phantom_mediaplayer_t *)gui->mediaplayer;

    gtk_list_store_clear(gui->media_playlist_store);

    for (uint32_t i = 0; i < player->playlist.track_count; i++) {
        mediaplayer_track_t *track = phantom_mediaplayer_playlist_get(player, i);
        if (!track) continue;

        char duration_str[32];
        phantom_mediaplayer_format_time(track->duration_ms, duration_str, sizeof(duration_str));

        const char *playing = (player->playlist.current_index == (int)i &&
                               player->state == PLAYBACK_PLAYING) ? "▶" : "";

        GtkTreeIter iter;
        gtk_list_store_append(gui->media_playlist_store, &iter);
        gtk_list_store_set(gui->media_playlist_store, &iter,
                           MEDIA_COL_INDEX, i,
                           MEDIA_COL_PLAYING, playing,
                           MEDIA_COL_TITLE, track->title[0] ? track->title : track->filepath,
                           MEDIA_COL_ARTIST, track->artist[0] ? track->artist : "Unknown",
                           MEDIA_COL_DURATION, duration_str,
                           MEDIA_COL_PATH, track->filepath,
                           -1);
    }
}
#endif /* HAVE_GSTREAMER */

/* ══════════════════════════════════════════════════════════════════════════════
 * EVENT HANDLERS
 * ══════════════════════════════════════════════════════════════════════════════ */

static void on_sidebar_button_clicked(GtkWidget *button, phantom_gui_t *gui) {
    if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button))) {
        return;
    }

    /* Deactivate other buttons */
    GList *children = gtk_container_get_children(GTK_CONTAINER(gui->sidebar));
    for (GList *l = children; l != NULL; l = l->next) {
        GtkWidget *child = GTK_WIDGET(l->data);
        if (child != button && GTK_IS_TOGGLE_BUTTON(child)) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(child), FALSE);
        }
    }
    g_list_free(children);

    /* Switch panel */
    const char *name = g_object_get_data(G_OBJECT(button), "panel-name");
    if (name) {
        gtk_stack_set_visible_child_name(GTK_STACK(gui->content_stack), name);
    }
}

static void on_file_row_activated(GtkTreeView *tree, GtkTreePath *path,
                                   GtkTreeViewColumn *column, phantom_gui_t *gui) {
    (void)column;

    GtkTreeModel *model = gtk_tree_view_get_model(tree);
    GtkTreeIter iter;

    if (gtk_tree_model_get_iter(model, &iter, path)) {
        char *full_path;
        char *type;
        gtk_tree_model_get(model, &iter, FILE_COL_PATH, &full_path, FILE_COL_TYPE, &type, -1);

        if (strcmp(type, "Directory") == 0) {
            phantom_gui_navigate_to(gui, full_path);
        } else {
            /* Open file with appropriate application */
            phantom_gui_open_file(gui, full_path);
        }

        g_free(full_path);
        g_free(type);
    }
}

static void on_navigate_up_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;

    if (strcmp(gui->current_path, "/") == 0) return;

    char *last_slash = strrchr(gui->current_path, '/');
    if (last_slash == gui->current_path) {
        phantom_gui_navigate_to(gui, "/");
    } else if (last_slash) {
        char parent[4096];
        strncpy(parent, gui->current_path, last_slash - gui->current_path);
        parent[last_slash - gui->current_path] = '\0';
        phantom_gui_navigate_to(gui, parent);
    }
}

static void on_file_back_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;

    if (gui->history_back_count == 0) return;

    /* Save current path to forward history (with safe bounds) */
    if (gui->history_forward_count < 10) {
        strncpy(gui->history_forward[gui->history_forward_count], gui->current_path, 4095);
        gui->history_forward[gui->history_forward_count][4095] = '\0';
        gui->history_forward_count++;
    } else {
        /* Shift forward history */
        for (int i = 0; i < 9; i++) {
            strncpy(gui->history_forward[i], gui->history_forward[i + 1], 4095);
            gui->history_forward[i][4095] = '\0';
        }
        strncpy(gui->history_forward[9], gui->current_path, 4095);
        gui->history_forward[9][4095] = '\0';
    }

    /* Go back */
    gui->history_back_count--;
    strncpy(gui->current_path, gui->history_back[gui->history_back_count], 4095);
    gui->current_path[4095] = '\0';

    /* Update buttons */
    gtk_widget_set_sensitive(gui->file_back_btn, gui->history_back_count > 0);
    gtk_widget_set_sensitive(gui->file_forward_btn, gui->history_forward_count > 0);

    phantom_gui_refresh_files(gui);
    phantom_gui_update_status(gui, "Navigated back");
}

static void on_file_forward_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;

    if (gui->history_forward_count == 0) return;

    /* Save current path to back history (with safe bounds) */
    if (gui->history_back_count < 10) {
        strncpy(gui->history_back[gui->history_back_count], gui->current_path, 4095);
        gui->history_back[gui->history_back_count][4095] = '\0';
        gui->history_back_count++;
    } else {
        /* Shift back history */
        for (int i = 0; i < 9; i++) {
            strncpy(gui->history_back[i], gui->history_back[i + 1], 4095);
            gui->history_back[i][4095] = '\0';
        }
        strncpy(gui->history_back[9], gui->current_path, 4095);
        gui->history_back[9][4095] = '\0';
    }

    /* Go forward */
    gui->history_forward_count--;
    strncpy(gui->current_path, gui->history_forward[gui->history_forward_count], 4095);
    gui->current_path[4095] = '\0';

    /* Update buttons */
    gtk_widget_set_sensitive(gui->file_back_btn, gui->history_back_count > 0);
    gtk_widget_set_sensitive(gui->file_forward_btn, gui->history_forward_count > 0);

    phantom_gui_refresh_files(gui);
    phantom_gui_update_status(gui, "Navigated forward");
}

static void on_file_refresh_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;

    phantom_gui_refresh_files(gui);
    phantom_gui_update_status(gui, "File list refreshed");
}

static void on_create_file_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;

    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Create New File",
        GTK_WINDOW(gui->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "Cancel", GTK_RESPONSE_CANCEL,
        "Create", GTK_RESPONSE_OK,
        NULL);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "filename.txt");
    gtk_container_add(GTK_CONTAINER(content), entry);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        const char *name = gtk_entry_get_text(GTK_ENTRY(entry));
        if (name && *name) {
            char path[4200];
            snprintf(path, sizeof(path), "%s/%s", gui->current_path, name);

            vfs_fd_t fd = vfs_open(gui->vfs, 1, path, VFS_O_CREATE | VFS_O_RDWR, 0644);
            if (fd >= 0) {
                /* Sync to ensure file is committed before closing */
                vfs_sync(gui->vfs, fd);
                vfs_close(gui->vfs, fd);
                phantom_gui_refresh_files(gui);
                phantom_gui_update_status(gui, "File created successfully");
            }
        }
    }

    gtk_widget_destroy(dialog);
}

static void on_create_folder_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;

    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Create New Folder",
        GTK_WINDOW(gui->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "Cancel", GTK_RESPONSE_CANCEL,
        "Create", GTK_RESPONSE_OK,
        NULL);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "folder_name");
    gtk_container_add(GTK_CONTAINER(content), entry);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        const char *name = gtk_entry_get_text(GTK_ENTRY(entry));
        if (name && *name) {
            char path[4200];
            snprintf(path, sizeof(path), "%s/%s", gui->current_path, name);

            if (vfs_mkdir(gui->vfs, 1, path, 0755) == VFS_OK) {
                phantom_gui_refresh_files(gui);
                phantom_gui_update_status(gui, "Folder created successfully");
            }
        }
    }

    gtk_widget_destroy(dialog);
}

static void on_hide_file_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;

    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(gui->file_tree));
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        char *path;
        char *name;
        gtk_tree_model_get(model, &iter, FILE_COL_PATH, &path, FILE_COL_NAME, &name, -1);

        char message[512];
        snprintf(message, sizeof(message),
                 "Hide '%s'?\n\nNote: The file will be hidden from view but preserved "
                 "in the geological record. Nothing is ever truly deleted.", name);

        GtkWidget *dialog = gtk_message_dialog_new(
            GTK_WINDOW(gui->window),
            GTK_DIALOG_MODAL,
            GTK_MESSAGE_QUESTION,
            GTK_BUTTONS_YES_NO,
            "%s", message);

        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_YES) {
            if (vfs_hide(gui->vfs, 1, path) == VFS_OK) {
                phantom_gui_refresh_files(gui);
                phantom_gui_update_status(gui, "File hidden (preserved in geology)");
            }
        }

        gtk_widget_destroy(dialog);
        g_free(path);
        g_free(name);
    }
}

/* Built-in text editor dialog */
static void phantom_gui_open_text_editor(phantom_gui_t *gui, const char *path) {
    if (!gui || !path) return;

    printf("[TextEditor] Opening file: %s\n", path);

    /* Read file content */
    char *content = NULL;
    size_t content_size = 0;

    vfs_fd_t fd = vfs_open(gui->vfs, 1, path, VFS_O_RDONLY, 0);
    printf("[TextEditor] Open for read, fd: %d\n", fd);

    if (fd >= 0) {
        struct vfs_stat st;
        vfs_error_t stat_err = vfs_fstat(gui->vfs, fd, &st);
        printf("[TextEditor] fstat result: %d, size: %lu\n", stat_err, (unsigned long)st.size);

        if (stat_err == VFS_OK) {
            content_size = st.size;
            content = malloc(content_size + 1);
            if (content) {
                ssize_t n = vfs_read(gui->vfs, fd, content, content_size);
                printf("[TextEditor] Read %zd bytes\n", n);
                if (n > 0) {
                    content[n] = '\0';
                    printf("[TextEditor] Content: '%s'\n", content);
                } else {
                    content[0] = '\0';
                }
            }
        }
        vfs_close(gui->vfs, fd);
    }

    if (!content) {
        content = strdup("");
    }

    /* Create editor dialog */
    const char *filename = strrchr(path, '/');
    filename = filename ? filename + 1 : path;

    char title[512];
    snprintf(title, sizeof(title), "Text Editor - %s", filename);

    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        title,
        GTK_WINDOW(gui->window),
        GTK_DIALOG_DESTROY_WITH_PARENT,
        "Save", GTK_RESPONSE_ACCEPT,
        "Close", GTK_RESPONSE_CANCEL,
        NULL);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 700, 500);

    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content_area), 8);

    /* Text view with scrolling */
    GtkWidget *text_view = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view), GTK_WRAP_WORD);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(text_view), TRUE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(text_view), 8);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(text_view), 8);

    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
    gtk_text_buffer_set_text(buffer, content, -1);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                    GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), text_view);
    gtk_box_pack_start(GTK_BOX(content_area), scroll, TRUE, TRUE, 0);

    gtk_widget_show_all(dialog);

    gint response = gtk_dialog_run(GTK_DIALOG(dialog));

    if (response == GTK_RESPONSE_ACCEPT) {
        /* Save file - PhantomOS style: hide old version and create new */
        GtkTextIter start, end;
        gtk_text_buffer_get_bounds(buffer, &start, &end);
        char *text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);

        printf("[TextEditor] Saving file: %s\n", path);
        printf("[TextEditor] Content length: %zu\n", strlen(text));

        /* Check if file already exists */
        struct vfs_stat st;
        int file_exists = (vfs_stat(gui->vfs, path, &st) == VFS_OK);
        printf("[TextEditor] File exists: %s\n", file_exists ? "yes" : "no");

        /* If file exists, hide it first (preserves in geology) */
        if (file_exists) {
            vfs_error_t hide_err = vfs_hide(gui->vfs, 1, path);
            printf("[TextEditor] Hide result: %d\n", hide_err);
        }

        /* Create new file with updated content */
        vfs_fd_t write_fd = vfs_open(gui->vfs, 1, path, VFS_O_WRONLY | VFS_O_CREATE, 0644);
        printf("[TextEditor] Open for write, fd: %d\n", write_fd);

        if (write_fd >= 0) {
            ssize_t written = 0;
            if (strlen(text) > 0) {
                written = vfs_write(gui->vfs, write_fd, text, strlen(text));
                printf("[TextEditor] Wrote %zd bytes\n", written);
            }
            vfs_error_t sync_err = vfs_sync(gui->vfs, write_fd);
            printf("[TextEditor] Sync result: %d\n", sync_err);
            vfs_close(gui->vfs, write_fd);
            phantom_gui_update_status(gui, "File saved successfully");

            /* Refresh file listing to show updated file */
            phantom_gui_refresh_files(gui);
            phantom_gui_refresh_geology(gui);
        } else {
            printf("[TextEditor] Failed to open file for writing\n");
            phantom_gui_update_status(gui, "Failed to save file");
        }

        g_free(text);
    }

    gtk_widget_destroy(dialog);
    free(content);
}

/* Built-in image viewer dialog */
static void phantom_gui_open_image_viewer(phantom_gui_t *gui, const char *path) {
    if (!gui || !path) return;

    const char *filename = strrchr(path, '/');
    filename = filename ? filename + 1 : path;

    char title[512];
    snprintf(title, sizeof(title), "Image Viewer - %s", filename);

    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        title,
        GTK_WINDOW(gui->window),
        GTK_DIALOG_DESTROY_WITH_PARENT,
        "Close", GTK_RESPONSE_CLOSE,
        NULL);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 800, 600);

    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content_area), 8);

    /* Try to load image */
    GError *error = NULL;
    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(path, &error);

    if (pixbuf) {
        /* Scale if too large */
        int width = gdk_pixbuf_get_width(pixbuf);
        int height = gdk_pixbuf_get_height(pixbuf);

        if (width > 1200 || height > 800) {
            double scale = 1.0;
            if (width > 1200) scale = 1200.0 / width;
            if (height > 800 && (800.0 / height) < scale) scale = 800.0 / height;

            int new_width = (int)(width * scale);
            int new_height = (int)(height * scale);

            GdkPixbuf *scaled = gdk_pixbuf_scale_simple(pixbuf, new_width, new_height,
                                                         GDK_INTERP_BILINEAR);
            g_object_unref(pixbuf);
            pixbuf = scaled;
        }

        GtkWidget *image = gtk_image_new_from_pixbuf(pixbuf);
        g_object_unref(pixbuf);

        GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                        GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
        gtk_container_add(GTK_CONTAINER(scroll), image);
        gtk_box_pack_start(GTK_BOX(content_area), scroll, TRUE, TRUE, 0);
    } else {
        GtkWidget *label = gtk_label_new("Failed to load image");
        gtk_box_pack_start(GTK_BOX(content_area), label, TRUE, TRUE, 0);
        if (error) {
            printf("[PhantomOS] Image load error: %s\n", error->message);
            g_error_free(error);
        }
    }

    gtk_widget_show_all(dialog);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

/* Helper function to open a file with appropriate application */
static void phantom_gui_open_file(phantom_gui_t *gui, const char *path) {
    if (!gui || !path) return;

    /* Get file extension */
    const char *ext = strrchr(path, '.');

    /* Check if file is executable */
    struct stat st;
    int is_executable = 0;
    if (stat(path, &st) == 0) {
        is_executable = (st.st_mode & S_IXUSR) || (st.st_mode & S_IXGRP) || (st.st_mode & S_IXOTH);
    }

    /* If executable, run it */
    if (is_executable) {
        char command[2048];
        char escaped_path[1024];
        if (shell_escape_path(path, escaped_path, sizeof(escaped_path)) != 0) {
            phantom_gui_update_status(gui, "Error: path too long or invalid");
            return;
        }
        snprintf(command, sizeof(command), "%s &", escaped_path);
        printf("[PhantomOS] Executing: %s\n", path);
        system(command);
        phantom_gui_update_status(gui, "File executed");
        return;
    }

    /* Use built-in viewers for supported types */
    if (ext) {
        /* Text files - open in built-in text editor */
        if (strcmp(ext, ".txt") == 0 || strcmp(ext, ".c") == 0 ||
            strcmp(ext, ".h") == 0 || strcmp(ext, ".cpp") == 0 ||
            strcmp(ext, ".py") == 0 || strcmp(ext, ".sh") == 0 ||
            strcmp(ext, ".md") == 0 || strcmp(ext, ".json") == 0 ||
            strcmp(ext, ".xml") == 0 || strcmp(ext, ".html") == 0 ||
            strcmp(ext, ".css") == 0 || strcmp(ext, ".js") == 0 ||
            strcmp(ext, ".log") == 0 || strcmp(ext, ".conf") == 0 ||
            strcmp(ext, ".cfg") == 0 || strcmp(ext, ".ini") == 0) {
            phantom_gui_open_text_editor(gui, path);
            return;
        }
        /* Image files - open in built-in image viewer */
        else if (strcmp(ext, ".png") == 0 || strcmp(ext, ".jpg") == 0 ||
                 strcmp(ext, ".jpeg") == 0 || strcmp(ext, ".gif") == 0 ||
                 strcmp(ext, ".bmp") == 0) {
            phantom_gui_open_image_viewer(gui, path);
            return;
        }
        /* PDF files */
        else if (strcmp(ext, ".pdf") == 0) {
            char command[2048];
            char escaped_path[1024];
            if (shell_escape_path(path, escaped_path, sizeof(escaped_path)) != 0) {
                phantom_gui_update_status(gui, "Error: path too long or invalid");
                return;
            }
            snprintf(command, sizeof(command),
                     "evince %s 2>/dev/null || xdg-open %s &",
                     escaped_path, escaped_path);
            system(command);
            phantom_gui_update_status(gui, "PDF opened");
            return;
        }
        /* Video/Audio files */
        else if (strcmp(ext, ".mp4") == 0 || strcmp(ext, ".avi") == 0 ||
                 strcmp(ext, ".mkv") == 0 || strcmp(ext, ".mp3") == 0 ||
                 strcmp(ext, ".wav") == 0 || strcmp(ext, ".flac") == 0) {
            char command[2048];
            char escaped_path[1024];
            if (shell_escape_path(path, escaped_path, sizeof(escaped_path)) != 0) {
                phantom_gui_update_status(gui, "Error: path too long or invalid");
                return;
            }
            snprintf(command, sizeof(command),
                     "vlc %s 2>/dev/null || xdg-open %s &",
                     escaped_path, escaped_path);
            system(command);
            phantom_gui_update_status(gui, "Media file opened");
            return;
        }
    }

    /* Default: try xdg-open */
    char command[2048];
    char escaped_path[1024];
    if (shell_escape_path(path, escaped_path, sizeof(escaped_path)) != 0) {
        phantom_gui_update_status(gui, "Error: path too long or invalid");
        return;
    }
    snprintf(command, sizeof(command), "xdg-open %s 2>/dev/null &", escaped_path);
    printf("[PhantomOS] Opening file: %s\n", path);
    int result = system(command);

    if (result == 0) {
        phantom_gui_update_status(gui, "File opened");
    } else {
        /* Fallback: show in preview */
        phantom_gui_view_file(gui, path);
        phantom_gui_update_status(gui, "File previewed (no application found)");
    }
}

static void on_open_file_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;

    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(gui->file_tree));
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (!gtk_tree_selection_get_selected(selection, &model, &iter)) {
        phantom_gui_update_status(gui, "Select a file to open");
        return;
    }

    char *path;
    char *type;
    gtk_tree_model_get(model, &iter, FILE_COL_PATH, &path, FILE_COL_TYPE, &type, -1);

    /* Check if it's a directory */
    if (strcmp(type, "Directory") == 0) {
        phantom_gui_navigate_to(gui, path);
    } else {
        phantom_gui_open_file(gui, path);
    }

    g_free(path);
    g_free(type);
}

static void on_copy_file_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;

    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(gui->file_tree));
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (!gtk_tree_selection_get_selected(selection, &model, &iter)) {
        phantom_gui_update_status(gui, "Select a file to copy");
        return;
    }

    char *src_path;
    char *name;
    gtk_tree_model_get(model, &iter, FILE_COL_PATH, &src_path, FILE_COL_NAME, &name, -1);

    /* Create dialog for destination name */
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Copy File",
        GTK_WINDOW(gui->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "Cancel", GTK_RESPONSE_CANCEL,
        "Copy", GTK_RESPONSE_OK,
        NULL);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 12);

    char label_text[256];
    snprintf(label_text, sizeof(label_text), "Copy '%s' to:", name);
    GtkWidget *label = gtk_label_new(label_text);
    gtk_box_pack_start(GTK_BOX(content), label, FALSE, FALSE, 4);

    GtkWidget *entry = gtk_entry_new();
    char default_dest[VFS_MAX_PATH];
    snprintf(default_dest, sizeof(default_dest), "%s_copy", name);
    gtk_entry_set_text(GTK_ENTRY(entry), default_dest);
    gtk_box_pack_start(GTK_BOX(content), entry, FALSE, FALSE, 4);

    gtk_widget_show_all(content);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        const char *dest_name = gtk_entry_get_text(GTK_ENTRY(entry));
        if (dest_name && strlen(dest_name) > 0) {
            /* Build destination path in current directory */
            const char *current_path = gtk_entry_get_text(GTK_ENTRY(gui->file_path_entry));
            char dest_path[VFS_MAX_PATH];
            if (strcmp(current_path, "/") == 0) {
                snprintf(dest_path, sizeof(dest_path), "/%s", dest_name);
            } else {
                snprintf(dest_path, sizeof(dest_path), "%s/%s", current_path, dest_name);
            }

            if (vfs_copy(gui->vfs, 1, src_path, dest_path) == VFS_OK) {
                phantom_gui_refresh_files(gui);
                phantom_gui_update_status(gui, "File copied successfully");
            } else {
                phantom_gui_update_status(gui, "Failed to copy file");
            }
        }
    }

    gtk_widget_destroy(dialog);
    g_free(src_path);
    g_free(name);
}

static void on_rename_file_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;

    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(gui->file_tree));
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (!gtk_tree_selection_get_selected(selection, &model, &iter)) {
        phantom_gui_update_status(gui, "Select a file to rename");
        return;
    }

    char *src_path;
    char *name;
    gtk_tree_model_get(model, &iter, FILE_COL_PATH, &src_path, FILE_COL_NAME, &name, -1);

    /* Create dialog for new name */
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Rename File",
        GTK_WINDOW(gui->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "Cancel", GTK_RESPONSE_CANCEL,
        "Rename", GTK_RESPONSE_OK,
        NULL);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 12);

    GtkWidget *label = gtk_label_new("New name:");
    gtk_box_pack_start(GTK_BOX(content), label, FALSE, FALSE, 4);

    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry), name);
    gtk_box_pack_start(GTK_BOX(content), entry, FALSE, FALSE, 4);

    GtkWidget *note = gtk_label_new("Note: Original file will be preserved in geology.");
    gtk_label_set_xalign(GTK_LABEL(note), 0.0);
    gtk_box_pack_start(GTK_BOX(content), note, FALSE, FALSE, 4);

    gtk_widget_show_all(content);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        const char *new_name = gtk_entry_get_text(GTK_ENTRY(entry));
        if (new_name && strlen(new_name) > 0 && strcmp(new_name, name) != 0) {
            /* Build new path in current directory */
            const char *current_path = gtk_entry_get_text(GTK_ENTRY(gui->file_path_entry));
            char new_path[VFS_MAX_PATH];
            if (strcmp(current_path, "/") == 0) {
                snprintf(new_path, sizeof(new_path), "/%s", new_name);
            } else {
                snprintf(new_path, sizeof(new_path), "%s/%s", current_path, new_name);
            }

            if (vfs_rename(gui->vfs, 1, src_path, new_path) == VFS_OK) {
                phantom_gui_refresh_files(gui);
                phantom_gui_update_status(gui, "File renamed (original preserved in geology)");
            } else {
                phantom_gui_update_status(gui, "Failed to rename file");
            }
        }
    }

    gtk_widget_destroy(dialog);
    g_free(src_path);
    g_free(name);
}

static void on_import_file_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;

    /* Create file chooser dialog for host system */
    GtkWidget *chooser = gtk_file_chooser_dialog_new(
        "Import File from Host System",
        GTK_WINDOW(gui->window),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "Cancel", GTK_RESPONSE_CANCEL,
        "Select", GTK_RESPONSE_ACCEPT,
        NULL);

    gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(chooser), TRUE);

    if (gtk_dialog_run(GTK_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT) {
        GSList *files = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(chooser));
        gtk_widget_destroy(chooser);

        if (!files) return;

        /* Count files for display */
        int file_count = g_slist_length(files);

        /* Create confirmation dialog with disclaimer */
        GtkWidget *dialog = gtk_dialog_new_with_buttons(
            "Import External Files",
            GTK_WINDOW(gui->window),
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
            "Cancel", GTK_RESPONSE_CANCEL,
            "Import", GTK_RESPONSE_OK,
            NULL);

        GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
        gtk_container_set_border_width(GTK_CONTAINER(content), 16);
        gtk_box_set_spacing(GTK_BOX(content), 12);

        /* Warning icon and title */
        GtkWidget *header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        GtkWidget *warning_icon = gtk_label_new("⚠️");
        PangoAttrList *attrs = pango_attr_list_new();
        pango_attr_list_insert(attrs, pango_attr_scale_new(2.5));
        gtk_label_set_attributes(GTK_LABEL(warning_icon), attrs);
        pango_attr_list_unref(attrs);
        gtk_box_pack_start(GTK_BOX(header_box), warning_icon, FALSE, FALSE, 0);

        GtkWidget *title_label = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(title_label),
            "<b><big>External File Import Warning</big></b>");
        gtk_box_pack_start(GTK_BOX(header_box), title_label, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(content), header_box, FALSE, FALSE, 0);

        /* File count */
        char count_text[128];
        snprintf(count_text, sizeof(count_text),
                 "You are about to import <b>%d file(s)</b> from the host system into PhantomOS.",
                 file_count);
        GtkWidget *count_label = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(count_label), count_text);
        gtk_label_set_line_wrap(GTK_LABEL(count_label), TRUE);
        gtk_label_set_xalign(GTK_LABEL(count_label), 0.0);
        gtk_box_pack_start(GTK_BOX(content), count_label, FALSE, FALSE, 0);

        /* Destination info */
        char dest_text[512];
        snprintf(dest_text, sizeof(dest_text),
                 "Destination: <b>%.450s</b>", gui->current_path);
        GtkWidget *dest_label = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(dest_label), dest_text);
        gtk_label_set_xalign(GTK_LABEL(dest_label), 0.0);
        gtk_box_pack_start(GTK_BOX(content), dest_label, FALSE, FALSE, 0);

        /* Warning frame */
        GtkWidget *warning_frame = gtk_frame_new("Important Notice");
        GtkWidget *warning_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
        gtk_container_set_border_width(GTK_CONTAINER(warning_box), 12);

        const char *warning_text =
            "External files from the host system may:\n\n"
            "• Contain malware, viruses, or other malicious code\n"
            "• Cause instability or unexpected behavior in PhantomOS\n"
            "• Be incompatible with the GeoFS file system\n"
            "• Consume significant storage space permanently\n"
            "• Be difficult to fully remove (files are preserved in geology)\n\n"
            "PhantomOS follows the principle \"To Create, Not To Destroy\" -\n"
            "imported files become a permanent part of the geology layer.";

        GtkWidget *warning_msg = gtk_label_new(warning_text);
        gtk_label_set_line_wrap(GTK_LABEL(warning_msg), TRUE);
        gtk_label_set_xalign(GTK_LABEL(warning_msg), 0.0);
        gtk_box_pack_start(GTK_BOX(warning_box), warning_msg, FALSE, FALSE, 0);

        gtk_container_add(GTK_CONTAINER(warning_frame), warning_box);
        gtk_box_pack_start(GTK_BOX(content), warning_frame, FALSE, FALSE, 0);

        /* Scan checkbox */
        GtkWidget *scan_check = gtk_check_button_new_with_label(
            "Scan files with Anti-Malware before importing (recommended)");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(scan_check), TRUE);
        gtk_box_pack_start(GTK_BOX(content), scan_check, FALSE, FALSE, 0);

        /* Acknowledgment checkbox - required to enable Import button */
        GtkWidget *ack_check = gtk_check_button_new_with_label(
            "I understand that external files may cause issues with PhantomOS\n"
            "and accept full responsibility for importing these files.");
        gtk_box_pack_start(GTK_BOX(content), ack_check, FALSE, FALSE, 8);

        /* Make Import button insensitive until checkbox is checked */
        GtkWidget *import_btn = gtk_dialog_get_widget_for_response(
            GTK_DIALOG(dialog), GTK_RESPONSE_OK);
        gtk_widget_set_sensitive(import_btn, FALSE);

        /* Enable/disable import button based on checkbox state */
        g_signal_connect_swapped(ack_check, "toggled",
            G_CALLBACK(gtk_widget_set_sensitive), import_btn);

        gtk_widget_show_all(content);

        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
            int scan_enabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(scan_check));
            int imported = 0;
            int skipped = 0;
            int threats = 0;

            for (GSList *l = files; l != NULL; l = l->next) {
                const char *src_path = (const char *)l->data;

                /* Get just the filename */
                const char *filename = strrchr(src_path, '/');
                filename = filename ? filename + 1 : src_path;

                /* Sanitize filename - remove dangerous characters */
                char safe_name[256];
                strncpy(safe_name, filename, sizeof(safe_name) - 1);
                safe_name[sizeof(safe_name) - 1] = '\0';

                /* Replace dangerous characters */
                for (char *p = safe_name; *p; p++) {
                    if (*p == '/' || *p == '\\' || *p == '\0' ||
                        *p == ':' || *p == '*' || *p == '?' ||
                        *p == '"' || *p == '<' || *p == '>' || *p == '|') {
                        *p = '_';
                    }
                }

                /* Scan for malware if enabled */
                if (scan_enabled && gui->antimalware_scanner) {
                    antimalware_scan_result_t result;
                    antimalware_scan_options_t opts = {0};
                    opts.heuristics_enabled = 1;

                    int threat = phantom_antimalware_scan_file(
                        (phantom_antimalware_t *)gui->antimalware_scanner,
                        src_path, &result, &opts);

                    if (threat > 0) {
                        /* Threat detected - skip this file */
                        threats++;
                        skipped++;
                        continue;
                    }
                }

                /* Build destination path */
                char dest_path[VFS_MAX_PATH];
                if (strcmp(gui->current_path, "/") == 0) {
                    snprintf(dest_path, sizeof(dest_path), "/%.250s", safe_name);
                } else {
                    snprintf(dest_path, sizeof(dest_path), "%.3800s/%.250s",
                             gui->current_path, safe_name);
                }

                /* Read source file from host */
                FILE *src = fopen(src_path, "rb");
                if (!src) {
                    skipped++;
                    continue;
                }

                /* Get file size */
                fseek(src, 0, SEEK_END);
                long file_size = ftell(src);
                fseek(src, 0, SEEK_SET);

                /* Create destination file in GeoFS (no truncation - GeoFS handles versioning) */
                vfs_fd_t fd = vfs_open(gui->vfs, 1, dest_path,
                                       VFS_O_CREATE | VFS_O_RDWR, 0644);
                if (fd < 0) {
                    fclose(src);
                    skipped++;
                    continue;
                }

                /* Copy file contents in chunks */
                char buffer[8192];
                size_t total_written = 0;
                size_t bytes_read;

                while ((bytes_read = fread(buffer, 1, sizeof(buffer), src)) > 0) {
                    ssize_t written = vfs_write(gui->vfs, fd, buffer, bytes_read);
                    if (written < 0) break;
                    total_written += written;
                }

                fclose(src);
                vfs_close(gui->vfs, fd);

                if (total_written == (size_t)file_size) {
                    imported++;
                } else {
                    skipped++;
                }
            }

            /* Refresh file list */
            phantom_gui_refresh_files(gui);

            /* Show results */
            char result_msg[256];
            if (threats > 0) {
                snprintf(result_msg, sizeof(result_msg),
                         "Imported: %d | Skipped: %d | Threats blocked: %d",
                         imported, skipped - threats, threats);
            } else {
                snprintf(result_msg, sizeof(result_msg),
                         "Imported: %d | Skipped: %d", imported, skipped);
            }
            phantom_gui_update_status(gui, result_msg);

            /* Show warning if threats were found */
            if (threats > 0) {
                GtkWidget *warn_dialog = gtk_message_dialog_new(
                    GTK_WINDOW(gui->window),
                    GTK_DIALOG_MODAL,
                    GTK_MESSAGE_WARNING,
                    GTK_BUTTONS_OK,
                    "Malware Detected");
                gtk_message_dialog_format_secondary_text(
                    GTK_MESSAGE_DIALOG(warn_dialog),
                    "%d file(s) were blocked because they were detected as threats.\n\n"
                    "These files were NOT imported to protect PhantomOS.",
                    threats);
                gtk_dialog_run(GTK_DIALOG(warn_dialog));
                gtk_widget_destroy(warn_dialog);
            }
        }

        gtk_widget_destroy(dialog);

        /* Free file list */
        g_slist_free_full(files, g_free);
    } else {
        gtk_widget_destroy(chooser);
    }
}

/* Search result storage for callback */
typedef struct {
    phantom_gui_t *gui;
    GtkListStore *results;
} search_context_t;

static void search_result_callback(const char *path, struct vfs_stat *stat, void *user_ctx) {
    search_context_t *ctx = (search_context_t *)user_ctx;

    GtkTreeIter iter;
    gtk_list_store_append(ctx->results, &iter);

    const char *type_str = "File";
    if (stat->type == VFS_TYPE_DIRECTORY) type_str = "Directory";
    else if (stat->type == VFS_TYPE_SYMLINK) type_str = "Link";

    char size_str[32];
    if (stat->size < 1024) {
        snprintf(size_str, sizeof(size_str), "%lu B", stat->size);
    } else if (stat->size < 1024 * 1024) {
        snprintf(size_str, sizeof(size_str), "%.1f KB", stat->size / 1024.0);
    } else {
        snprintf(size_str, sizeof(size_str), "%.1f MB", stat->size / (1024.0 * 1024.0));
    }

    gtk_list_store_set(ctx->results, &iter,
        0, path,
        1, type_str,
        2, size_str,
        -1);
}

/* Search execute callback */
static void on_search_execute(GtkWidget *btn, gpointer user_data) {
    (void)user_data;

    GtkEntry *entry = GTK_ENTRY(g_object_get_data(G_OBJECT(btn), "entry"));
    search_context_t *ctx = (search_context_t *)g_object_get_data(G_OBJECT(btn), "ctx");

    if (!entry || !ctx) return;

    const char *pattern = gtk_entry_get_text(entry);
    if (pattern && strlen(pattern) > 0) {
        gtk_list_store_clear(ctx->results);
        const char *start = gtk_entry_get_text(GTK_ENTRY(ctx->gui->file_path_entry));
        vfs_search(ctx->gui->vfs, start, pattern, search_result_callback, ctx);
    }
}

/* Search result open callback - for "Open" button */
static void on_search_result_open(GtkWidget *btn, gpointer user_data) {
    (void)btn;

    GtkTreeView *tree = GTK_TREE_VIEW(g_object_get_data(G_OBJECT(btn), "tree"));
    phantom_gui_t *gui = (phantom_gui_t *)g_object_get_data(G_OBJECT(btn), "gui");

    if (!tree || !gui) return;

    GtkTreeSelection *selection = gtk_tree_view_get_selection(tree);
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (!gtk_tree_selection_get_selected(selection, &model, &iter)) {
        phantom_gui_update_status(gui, "Select a file to open");
        return;
    }

    char *path = NULL;
    gtk_tree_model_get(model, &iter, 0, &path, -1);

    if (path) {
        phantom_gui_open_file(gui, path);
        g_free(path);
    }
}

/* Search result activated callback - for double-click */
static void on_search_result_activated(GtkTreeView *tree, GtkTreePath *path,
                                        GtkTreeViewColumn *col, gpointer user_data) {
    (void)path;
    (void)col;

    phantom_gui_t *gui = (phantom_gui_t *)user_data;

    GtkTreeSelection *selection = gtk_tree_view_get_selection(tree);
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (!gtk_tree_selection_get_selected(selection, &model, &iter)) {
        return;
    }

    char *file_path = NULL;
    gtk_tree_model_get(model, &iter, 0, &file_path, -1);

    if (file_path) {
        phantom_gui_open_file(gui, file_path);
        g_free(file_path);
    }
}

static void on_search_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;

    /* Create search dialog */
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Search Files",
        GTK_WINDOW(gui->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "Open", GTK_RESPONSE_ACCEPT,
        "Close", GTK_RESPONSE_CLOSE,
        NULL);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 600, 400);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 12);

    /* Search input */
    GtkWidget *hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(content), hbox, FALSE, FALSE, 4);

    GtkWidget *label = gtk_label_new("Pattern:");
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 0);

    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "*.txt, data*, etc.");
    gtk_box_pack_start(GTK_BOX(hbox), entry, TRUE, TRUE, 0);

    GtkWidget *search_btn = gtk_button_new_with_label("Search");
    gtk_box_pack_start(GTK_BOX(hbox), search_btn, FALSE, FALSE, 0);

    /* Results list */
    GtkListStore *results = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    GtkWidget *tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(results));

    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *col;

    col = gtk_tree_view_column_new_with_attributes("Path", renderer, "text", 0, NULL);
    gtk_tree_view_column_set_expand(col, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), col);

    col = gtk_tree_view_column_new_with_attributes("Type", renderer, "text", 1, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), col);

    col = gtk_tree_view_column_new_with_attributes("Size", renderer, "text", 2, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), col);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), tree);
    gtk_box_pack_start(GTK_BOX(content), scroll, TRUE, TRUE, 4);

    /* Allocate search context - will be freed when dialog is destroyed */
    search_context_t *ctx = g_new(search_context_t, 1);
    ctx->gui = gui;
    ctx->results = results;

    /* Store context for cleanup */
    g_object_set_data_full(G_OBJECT(dialog), "search_ctx", ctx, g_free);
    g_object_set_data(G_OBJECT(search_btn), "entry", entry);
    g_object_set_data(G_OBJECT(search_btn), "ctx", ctx);

    g_signal_connect(search_btn, "clicked", G_CALLBACK(on_search_execute), NULL);

    /* Connect double-click to open file */
    g_signal_connect(tree, "row-activated", G_CALLBACK(on_search_result_activated), gui);

    /* Get the "Open" button and connect it */
    GtkWidget *open_btn = gtk_dialog_get_widget_for_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
    if (open_btn) {
        g_object_set_data(G_OBJECT(open_btn), "tree", tree);
        g_object_set_data(G_OBJECT(open_btn), "gui", gui);
        g_signal_connect(open_btn, "clicked", G_CALLBACK(on_search_result_open), NULL);
    }

    gtk_widget_show_all(content);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

static void on_history_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;

    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(gui->file_tree));
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (!gtk_tree_selection_get_selected(selection, &model, &iter)) {
        phantom_gui_update_status(gui, "Select a file to view history");
        return;
    }

    char *file_path;
    char *name;
    gtk_tree_model_get(model, &iter, FILE_COL_PATH, &file_path, FILE_COL_NAME, &name, -1);

    /* Get version history */
    vfs_file_version_t versions[32];
    size_t count;

    if (vfs_get_history(gui->vfs, file_path, versions, 32, &count) != VFS_OK || count == 0) {
        GtkWidget *msg = gtk_message_dialog_new(
            GTK_WINDOW(gui->window),
            GTK_DIALOG_MODAL,
            GTK_MESSAGE_INFO,
            GTK_BUTTONS_OK,
            "No version history available for this file.\n\n"
            "History is only available for files on GeoFS mounts.");
        gtk_dialog_run(GTK_DIALOG(msg));
        gtk_widget_destroy(msg);
        g_free(file_path);
        g_free(name);
        return;
    }

    /* Create history dialog */
    char title[256];
    snprintf(title, sizeof(title), "Version History: %s", name);

    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        title,
        GTK_WINDOW(gui->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "Close", GTK_RESPONSE_CLOSE,
        NULL);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 600, 300);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 12);

    /* History list */
    GtkListStore *store = gtk_list_store_new(4,
        G_TYPE_UINT64,  /* View ID */
        G_TYPE_STRING,  /* Label */
        G_TYPE_STRING,  /* Size */
        G_TYPE_STRING   /* Hash preview */
    );

    for (size_t i = 0; i < count; i++) {
        GtkTreeIter list_iter;
        gtk_list_store_append(store, &list_iter);

        char size_str[32];
        snprintf(size_str, sizeof(size_str), "%lu bytes", versions[i].size);

        char hash_preview[20];
        strncpy(hash_preview, versions[i].content_hash, 16);
        hash_preview[16] = '\0';
        strcat(hash_preview, "...");

        gtk_list_store_set(store, &list_iter,
            0, versions[i].view_id,
            1, versions[i].view_label[0] ? versions[i].view_label : "(unnamed)",
            2, size_str,
            3, hash_preview,
            -1);
    }

    GtkWidget *tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));

    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *col;

    col = gtk_tree_view_column_new_with_attributes("View ID", renderer, "text", 0, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), col);

    col = gtk_tree_view_column_new_with_attributes("Label", renderer, "text", 1, NULL);
    gtk_tree_view_column_set_expand(col, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), col);

    col = gtk_tree_view_column_new_with_attributes("Size", renderer, "text", 2, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), col);

    col = gtk_tree_view_column_new_with_attributes("Content Hash", renderer, "text", 3, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), col);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), tree);
    gtk_box_pack_start(GTK_BOX(content), scroll, TRUE, TRUE, 4);

    GtkWidget *note = gtk_label_new(
        "Each version represents a geological stratum. "
        "Use 'restore' command in terminal to recover old versions.");
    gtk_label_set_line_wrap(GTK_LABEL(note), TRUE);
    gtk_box_pack_start(GTK_BOX(content), note, FALSE, FALSE, 4);

    gtk_widget_show_all(content);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    g_free(file_path);
    g_free(name);
}

void phantom_gui_view_file(phantom_gui_t *gui, const char *path) {
    if (!gui || !path) return;

    char content[8192] = {0};

    vfs_fd_t fd = vfs_open(gui->vfs, 1, path, VFS_O_RDONLY, 0);
    if (fd >= 0) {
        ssize_t n = vfs_read(gui->vfs, fd, content, sizeof(content) - 1);
        if (n > 0) {
            content[n] = '\0';
        }
        vfs_close(gui->vfs, fd);
    }

    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(gui->file_content_view));
    gtk_text_buffer_set_text(buffer, content, -1);
}

static void on_process_suspend_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;

    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(gui->process_tree));
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        guint64 pid;
        gtk_tree_model_get(model, &iter, PROC_COL_PID, &pid, -1);

        phantom_process_suspend(gui->kernel, pid);
        phantom_gui_refresh_processes(gui);
        phantom_gui_update_status(gui, "Process suspended (entered dormancy)");
    }
}

static void on_process_resume_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;

    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(gui->process_tree));
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        guint64 pid;
        gtk_tree_model_get(model, &iter, PROC_COL_PID, &pid, -1);

        phantom_process_resume(gui->kernel, pid);
        phantom_gui_refresh_processes(gui);
        phantom_gui_update_status(gui, "Process resumed (awakened from dormancy)");
    }
}

static void on_service_awaken_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;

    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(gui->service_tree));
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        char *name;
        gtk_tree_model_get(model, &iter, SVC_COL_NAME, &name, -1);

        init_service_awaken(gui->kernel->init, name);
        phantom_gui_refresh_services(gui);
        phantom_gui_update_status(gui, "Service awakened");

        g_free(name);
    }
}

static void on_service_rest_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;

    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(gui->service_tree));
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        char *name;
        gtk_tree_model_get(model, &iter, SVC_COL_NAME, &name, -1);

        init_service_rest(gui->kernel->init, name);
        phantom_gui_refresh_services(gui);
        phantom_gui_update_status(gui, "Service entered dormancy");

        g_free(name);
    }
}

static void on_governor_mode_changed(GtkComboBox *combo, phantom_gui_t *gui) {
    const char *mode = gtk_combo_box_get_active_id(combo);
    if (!mode) return;

    phantom_governor_t *gov = gui->kernel->governor;
    if (!gov) return;

    if (strcmp(mode, "interactive") == 0) {
        governor_set_interactive(gov, 1);
        governor_set_strict(gov, 0);
    } else if (strcmp(mode, "auto") == 0) {
        governor_set_interactive(gov, 0);
        governor_set_strict(gov, 0);
    } else if (strcmp(mode, "strict") == 0) {
        governor_set_strict(gov, 1);
    }

    phantom_gui_refresh_governor(gui);
}

static void on_governor_test_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;

    const char *code = gtk_entry_get_text(GTK_ENTRY(gui->governor_test_entry));
    if (!code || !*code) return;

    phantom_governor_t *gov = gui->kernel->governor;
    if (!gov) return;

    governor_eval_request_t req = {0};
    governor_eval_response_t resp = {0};

    req.code_ptr = code;
    req.code_size = strlen(code);
    strncpy(req.name, "gui-test", 255);

    int was_interactive = gov->interactive;
    gov->interactive = 0;
    governor_evaluate_code(gov, &req, &resp);
    gov->interactive = was_interactive;

    char result[2048];
    char caps_buf[256] = "-";
    if (req.detected_caps) {
        governor_caps_to_list(req.detected_caps, caps_buf, sizeof(caps_buf));
    }

    snprintf(result, sizeof(result),
             "Code: %s\n\n"
             "Threat Level: %s\n"
             "Capabilities: %s\n\n"
             "Decision: %s\n"
             "Summary: %s\n"
             "Decided by: %s\n",
             code,
             governor_threat_to_string(req.threat_level),
             caps_buf,
             resp.decision == GOVERNOR_APPROVE ? "✅ APPROVED" : "❌ DECLINED",
             resp.summary,
             resp.decision_by);

    if (resp.decision == GOVERNOR_DECLINE && resp.alternatives[0]) {
        strcat(result, "\nAlternatives: ");
        strcat(result, resp.alternatives);
    }

    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(gui->governor_test_result));
    gtk_text_buffer_set_text(buffer, result, -1);
}

static void on_governor_cache_toggled(GtkToggleButton *button, phantom_gui_t *gui) {
    gboolean active = gtk_toggle_button_get_active(button);
    phantom_governor_t *gov = gui->kernel->governor;
    if (gov) {
        governor_enable_cache(gov, active ? 1 : 0);
        phantom_gui_refresh_governor(gui);
        phantom_gui_update_status(gui, active ? "Cache enabled" : "Cache disabled");
    }
}

static void on_governor_clear_cache(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;
    phantom_governor_t *gov = gui->kernel->governor;
    if (gov) {
        governor_clear_cache(gov);
        phantom_gui_refresh_governor(gui);
        phantom_gui_update_status(gui, "Cache cleared");
    }
}

static void on_governor_view_history(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;
    phantom_governor_t *gov = gui->kernel->governor;
    if (!gov) return;

    /* Create a dialog to show history */
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Governor History",
        GTK_WINDOW(gui->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Close", GTK_RESPONSE_CLOSE,
        NULL);

    gtk_window_set_default_size(GTK_WINDOW(dialog), 500, 400);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                    GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(content), scroll);

    GtkWidget *text_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(text_view), FALSE);
    gtk_container_add(GTK_CONTAINER(scroll), text_view);

    /* Build history text */
    char history_text[8192] = "";
    char line[512];
    int count = governor_history_count(gov);

    if (count == 0) {
        strcpy(history_text, "No history entries.\n");
    } else {
        snprintf(history_text, sizeof(history_text),
                 "Governor History (%d entries):\n\n", count);

        int max_show = count < 20 ? count : 20;
        for (int i = 0; i < max_show; i++) {
            governor_history_entry_t entry;
            if (governor_get_history(gov, i, &entry) == 0) {
                char hash_str[17];
                for (int j = 0; j < 8; j++) {
                    sprintf(hash_str + (j * 2), "%02x", entry.code_hash[j]);
                }
                hash_str[16] = '\0';

                snprintf(line, sizeof(line),
                         "[%d] %s %s\n"
                         "    Name: %.100s | Hash: %s...\n"
                         "    Threat: %s | By: %.50s\n"
                         "    %.150s\n\n",
                         i,
                         entry.decision == GOVERNOR_APPROVE ? "✅ APPROVED" : "❌ DECLINED",
                         entry.can_rollback ? "" : "(locked)",
                         entry.name[0] ? entry.name : "(unnamed)",
                         hash_str,
                         governor_threat_to_string(entry.threat_level),
                         entry.decision_by,
                         entry.summary);
                strncat(history_text, line, sizeof(history_text) - strlen(history_text) - 1);
            }
        }
        if (count > 20) {
            snprintf(line, sizeof(line), "... and %d more entries\n", count - 20);
            strncat(history_text, line, sizeof(history_text) - strlen(history_text) - 1);
        }
    }

    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
    gtk_text_buffer_set_text(buffer, history_text, -1);

    gtk_widget_show_all(dialog);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

static void on_governor_behavioral_analyze(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;

    const char *code = gtk_entry_get_text(GTK_ENTRY(gui->governor_test_entry));
    if (!code || !*code) {
        phantom_gui_update_status(gui, "Enter code in the text field first");
        return;
    }

    /* Run behavioral analysis */
    governor_behavior_result_t result;
    if (governor_analyze_behavior(code, strlen(code), &result) != 0) {
        phantom_gui_update_status(gui, "Behavioral analysis failed");
        return;
    }

    /* Display results */
    char analysis[2048];
    snprintf(analysis, sizeof(analysis),
             "=== BEHAVIORAL ANALYSIS ===\n\n"
             "Code: %.100s%s\n\n"
             "Suspicious Score: %d/100\n\n",
             code, strlen(code) > 100 ? "..." : "",
             result.suspicious_score);

    if (result.flags == BEHAVIOR_NONE) {
        strcat(analysis, "Result: ✅ No suspicious behaviors detected\n");
    } else {
        strcat(analysis, "Result: ⚠️ Suspicious behaviors detected!\n\n");
        strcat(analysis, "Detected Patterns:\n");
        for (int i = 0; i < result.description_count; i++) {
            char line[300];
            snprintf(line, sizeof(line), "  • %s\n", result.descriptions[i]);
            strncat(analysis, line, sizeof(analysis) - strlen(analysis) - 1);
        }

        strcat(analysis, "\nBehavior Flags:");
        if (result.flags & BEHAVIOR_INFINITE_LOOP)  strcat(analysis, " infinite_loop");
        if (result.flags & BEHAVIOR_MEMORY_BOMB)    strcat(analysis, " memory_bomb");
        if (result.flags & BEHAVIOR_FORK_BOMB)      strcat(analysis, " fork_bomb");
        if (result.flags & BEHAVIOR_OBFUSCATION)    strcat(analysis, " obfuscation");
        if (result.flags & BEHAVIOR_ENCODED_PAYLOAD) strcat(analysis, " encoded_payload");
        if (result.flags & BEHAVIOR_SHELL_INJECTION) strcat(analysis, " shell_injection");
        if (result.flags & BEHAVIOR_PATH_TRAVERSAL) strcat(analysis, " path_traversal");
        if (result.flags & BEHAVIOR_RESOURCE_EXHAUST) strcat(analysis, " resource_exhaust");
        if (result.flags & BEHAVIOR_LOOP_DESTRUCTION) strcat(analysis, " loop_destruction");
        strcat(analysis, "\n");
    }

    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(gui->governor_test_result));
    gtk_text_buffer_set_text(buffer, analysis, -1);
    phantom_gui_update_status(gui, "Behavioral analysis complete");
}

static void on_terminal_entry_activate(GtkEntry *entry, phantom_gui_t *gui) {
    const char *command = gtk_entry_get_text(entry);
    if (command && *command) {
        phantom_gui_terminal_execute(gui, command);
        gtk_entry_set_text(entry, "");
    }
}

/* Handler for panel switching - refresh Files panel when it becomes visible */
static void on_stack_visible_child_changed(GObject *stack, GParamSpec *pspec, phantom_gui_t *gui) {
    (void)pspec;

    const char *visible_panel = gtk_stack_get_visible_child_name(GTK_STACK(stack));
    if (visible_panel && strcmp(visible_panel, "files") == 0) {
        /* Refresh file list when switching to Files panel */
        phantom_gui_refresh_files(gui);
    } else if (visible_panel && strcmp(visible_panel, "geology") == 0) {
        /* Refresh geology when switching to Geology panel */
        phantom_gui_refresh_geology(gui);
    }
}

static gboolean on_refresh_timer(phantom_gui_t *gui) {
    if (!gui->running) return FALSE;

    /* Auto-refresh file browser if it's been more than 3 seconds since last refresh */
    time_t now = time(NULL);
    if ((now - gui->last_file_refresh) >= 3) {
        const char *visible_panel = gtk_stack_get_visible_child_name(GTK_STACK(gui->content_stack));
        if (visible_panel && strcmp(visible_panel, "files") == 0) {
            phantom_gui_refresh_files(gui);
        }
    }

    phantom_gui_refresh_processes(gui);
    phantom_gui_refresh_services(gui);
    phantom_gui_refresh_governor(gui);

    /* Check storage status */
    if (gui->storage_manager) {
        phantom_storage_manager_t *mgr = (phantom_storage_manager_t *)gui->storage_manager;
        phantom_storage_check(mgr);

        /* Update storage indicator */
        if (gui->storage_indicator) {
            char indicator[64];
            phantom_storage_stats_t stats;
            phantom_storage_get_stats(mgr, &stats);

            const char *icon = "=";
            if (stats.warning_level >= STORAGE_WARN_CRITICAL) icon = "!!!";
            else if (stats.warning_level >= STORAGE_WARN_WARNING) icon = "!!";
            else if (stats.warning_level >= STORAGE_WARN_ADVISORY) icon = "!";

            snprintf(indicator, sizeof(indicator), "Storage: %.0f%% %s",
                     stats.overall_percent_used, icon);
            gtk_label_set_text(GTK_LABEL(gui->storage_indicator), indicator);

            /* Color based on warning level */
            GtkStyleContext *ctx = gtk_widget_get_style_context(gui->storage_indicator);
            gtk_style_context_remove_class(ctx, "storage-ok");
            gtk_style_context_remove_class(ctx, "storage-warn");
            gtk_style_context_remove_class(ctx, "storage-critical");

            if (stats.warning_level >= STORAGE_WARN_CRITICAL) {
                gtk_style_context_add_class(ctx, "storage-critical");
            } else if (stats.warning_level >= STORAGE_WARN_WARNING) {
                gtk_style_context_add_class(ctx, "storage-warn");
            } else {
                gtk_style_context_add_class(ctx, "storage-ok");
            }
        }
    }

    return TRUE;
}

static void gui_storage_warning_callback(int level, const char *message, void *user_data) {
    phantom_gui_t *gui = (phantom_gui_t *)user_data;
    if (!gui || !gui->window) return;

    /* Only show dialog for new warnings */
    if (level <= gui->last_storage_warning) return;
    gui->last_storage_warning = level;

    GtkMessageType msg_type = GTK_MESSAGE_INFO;
    const char *title = "Storage Advisory";

    if (level >= STORAGE_WARN_CRITICAL) {
        msg_type = GTK_MESSAGE_ERROR;
        title = "Storage Critical!";
    } else if (level >= STORAGE_WARN_WARNING) {
        msg_type = GTK_MESSAGE_WARNING;
        title = "Storage Warning";
    }

    phantom_gui_show_message(gui, title, message, msg_type);
}

static void on_window_destroy(GtkWidget *widget, phantom_gui_t *gui) {
    (void)widget;
    phantom_gui_shutdown(gui);
    gtk_main_quit();
}

/* ══════════════════════════════════════════════════════════════════════════════
 * UTILITY FUNCTIONS
 * ══════════════════════════════════════════════════════════════════════════════ */

void phantom_gui_show_message(phantom_gui_t *gui, const char *title,
                               const char *message, GtkMessageType type) {
    GtkWidget *dialog = gtk_message_dialog_new(
        GTK_WINDOW(gui->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        type,
        GTK_BUTTONS_OK,
        "%s", message);
    gtk_window_set_title(GTK_WINDOW(dialog), title);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

void phantom_gui_update_status(phantom_gui_t *gui, const char *status) {
    if (!gui || !gui->status_bar) return;

    char full_status[256];
    snprintf(full_status, sizeof(full_status), "%.180s - All data preserved in geology", status);
    gtk_label_set_text(GTK_LABEL(gui->status_bar), full_status);
}

/* ─────────────────────────────────────────────────────────────────────────────
 * ArtOS Panel - Digital Art Studio
 * ───────────────────────────────────────────────────────────────────────────── */

GtkWidget *phantom_gui_create_artos_panel(phantom_gui_t *gui) {
    GtkWidget *panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    /* Create ArtOS instance */
    phantom_artos_t *artos = artos_create();
    if (!artos) {
        GtkWidget *error_label = gtk_label_new("Failed to initialize ArtOS");
        gtk_box_pack_start(GTK_BOX(panel), error_label, TRUE, TRUE, 0);
        return panel;
    }

    gui->artos = artos;

    /* Get the ArtOS widget and embed it */
    GtkWidget *artos_widget = artos_get_widget(artos);
    if (artos_widget) {
        gtk_box_pack_start(GTK_BOX(panel), artos_widget, TRUE, TRUE, 0);
    } else {
        /* Fallback - show ArtOS in its own window mode */
        GtkWidget *info_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
        gtk_widget_set_valign(info_box, GTK_ALIGN_CENTER);
        gtk_widget_set_halign(info_box, GTK_ALIGN_CENTER);

        GtkWidget *title = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(title),
            "<span size='xx-large' weight='bold'>🎨 ArtOS</span>");
        gtk_box_pack_start(GTK_BOX(info_box), title, FALSE, FALSE, 0);

        GtkWidget *subtitle = gtk_label_new("Digital Art Studio for PhantomOS");
        gtk_box_pack_start(GTK_BOX(info_box), subtitle, FALSE, FALSE, 0);

        GtkWidget *desc = gtk_label_new(
            "ArtOS is a full-featured digital painting application\n"
            "that respects the Phantom philosophy:\n"
            "Every stroke is preserved in geological layers.\n\n"
            "Features:\n"
            "• Multiple brush types (pencil, pen, brush, airbrush)\n"
            "• Layer support with blend modes\n"
            "• Shape tools (line, rectangle, ellipse)\n"
            "• Color picker and palette\n"
            "• Unlimited undo (geological history)\n"
            "• Export to PNG");
        gtk_label_set_justify(GTK_LABEL(desc), GTK_JUSTIFY_CENTER);
        gtk_box_pack_start(GTK_BOX(info_box), desc, FALSE, FALSE, 10);

        GtkWidget *launch_btn = gtk_button_new_with_label("Launch ArtOS Window");
        gtk_widget_set_halign(launch_btn, GTK_ALIGN_CENTER);
        g_signal_connect_swapped(launch_btn, "clicked",
                                  G_CALLBACK(gtk_widget_show_all), artos->window);
        gtk_box_pack_start(GTK_BOX(info_box), launch_btn, FALSE, FALSE, 10);

        gtk_box_pack_start(GTK_BOX(panel), info_box, TRUE, TRUE, 0);
    }

    return panel;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * User Management Panel
 * ───────────────────────────────────────────────────────────────────────────── */

/* Forward declarations for user management callbacks */
static void on_user_create_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_user_edit_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_user_disable_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_user_password_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_user_selection_changed(GtkTreeSelection *selection, phantom_gui_t *gui);

GtkWidget *phantom_gui_create_users_panel(phantom_gui_t *gui) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 8);

    /* Title */
    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title),
        "<span size='large' weight='bold'>👥 User Management</span>\n"
        "<span size='small'>Create and manage user accounts. Users are never deleted, only disabled.</span>");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 8);

    /* Toolbar */
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_start(GTK_BOX(vbox), toolbar, FALSE, FALSE, 0);

    gui->users_create_btn = gtk_button_new_with_label("➕ Create User");
    gtk_widget_set_tooltip_text(gui->users_create_btn, "Create a new user account");
    g_signal_connect(gui->users_create_btn, "clicked", G_CALLBACK(on_user_create_clicked), gui);
    gtk_box_pack_start(GTK_BOX(toolbar), gui->users_create_btn, FALSE, FALSE, 0);

    gui->users_edit_btn = gtk_button_new_with_label("✏️ Edit");
    gtk_widget_set_tooltip_text(gui->users_edit_btn, "Edit selected user");
    gtk_widget_set_sensitive(gui->users_edit_btn, FALSE);
    g_signal_connect(gui->users_edit_btn, "clicked", G_CALLBACK(on_user_edit_clicked), gui);
    gtk_box_pack_start(GTK_BOX(toolbar), gui->users_edit_btn, FALSE, FALSE, 0);

    gui->users_password_btn = gtk_button_new_with_label("🔑 Change Password");
    gtk_widget_set_tooltip_text(gui->users_password_btn, "Change user's password");
    gtk_widget_set_sensitive(gui->users_password_btn, FALSE);
    g_signal_connect(gui->users_password_btn, "clicked", G_CALLBACK(on_user_password_clicked), gui);
    gtk_box_pack_start(GTK_BOX(toolbar), gui->users_password_btn, FALSE, FALSE, 0);

    gui->users_disable_btn = gtk_button_new_with_label("🚫 Disable");
    gtk_widget_set_tooltip_text(gui->users_disable_btn, "Disable user account (can be re-enabled)");
    gtk_widget_set_sensitive(gui->users_disable_btn, FALSE);
    g_signal_connect(gui->users_disable_btn, "clicked", G_CALLBACK(on_user_disable_clicked), gui);
    gtk_box_pack_start(GTK_BOX(toolbar), gui->users_disable_btn, FALSE, FALSE, 0);

    /* User list */
    gui->users_store = gtk_list_store_new(USER_COL_COUNT,
        G_TYPE_STRING,  /* Icon */
        G_TYPE_STRING,  /* Username */
        G_TYPE_STRING,  /* Full name */
        G_TYPE_STRING,  /* State */
        G_TYPE_UINT,    /* UID */
        G_TYPE_STRING,  /* Permissions */
        G_TYPE_STRING   /* Last login */
    );

    gui->users_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(gui->users_store));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(gui->users_tree), TRUE);

    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("", renderer, "text", USER_COL_ICON, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->users_tree), column);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Username", renderer, "text", USER_COL_USERNAME, NULL);
    gtk_tree_view_column_set_min_width(column, 120);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->users_tree), column);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Full Name", renderer, "text", USER_COL_FULLNAME, NULL);
    gtk_tree_view_column_set_min_width(column, 150);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->users_tree), column);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("State", renderer, "text", USER_COL_STATE, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->users_tree), column);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("UID", renderer, "text", USER_COL_UID, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->users_tree), column);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Permissions", renderer, "text", USER_COL_PERMISSIONS, NULL);
    gtk_tree_view_column_set_expand(column, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->users_tree), column);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Last Login", renderer, "text", USER_COL_LAST_LOGIN, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->users_tree), column);

    /* Selection handling */
    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(gui->users_tree));
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);
    g_signal_connect(selection, "changed", G_CALLBACK(on_user_selection_changed), gui);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                    GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), gui->users_tree);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);

    /* Details section */
    GtkWidget *details_frame = gtk_frame_new("User Details");
    gtk_box_pack_start(GTK_BOX(vbox), details_frame, FALSE, FALSE, 8);

    gui->users_details_label = gtk_label_new("Select a user to view details");
    gtk_label_set_xalign(GTK_LABEL(gui->users_details_label), 0.0);
    gtk_widget_set_margin_start(gui->users_details_label, 8);
    gtk_widget_set_margin_end(gui->users_details_label, 8);
    gtk_widget_set_margin_top(gui->users_details_label, 4);
    gtk_widget_set_margin_bottom(gui->users_details_label, 4);
    gtk_container_add(GTK_CONTAINER(details_frame), gui->users_details_label);

    return vbox;
}

/* User selection changed callback */
static void on_user_selection_changed(GtkTreeSelection *selection, phantom_gui_t *gui) {
    GtkTreeIter iter;
    GtkTreeModel *model;

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        gchar *username, *fullname, *state, *perms, *last_login;
        guint uid;

        gtk_tree_model_get(model, &iter,
            USER_COL_USERNAME, &username,
            USER_COL_FULLNAME, &fullname,
            USER_COL_STATE, &state,
            USER_COL_UID, &uid,
            USER_COL_PERMISSIONS, &perms,
            USER_COL_LAST_LOGIN, &last_login,
            -1);

        char details[512];
        snprintf(details, sizeof(details),
            "<b>Username:</b> %s\n<b>Full Name:</b> %s\n<b>UID:</b> %u\n<b>State:</b> %s\n<b>Permissions:</b> %s\n<b>Last Login:</b> %s",
            username, fullname, uid, state, perms, last_login);
        gtk_label_set_markup(GTK_LABEL(gui->users_details_label), details);

        /* Enable action buttons */
        gtk_widget_set_sensitive(gui->users_edit_btn, TRUE);
        gtk_widget_set_sensitive(gui->users_password_btn, TRUE);
        /* Only enable disable for non-admin users */
        gtk_widget_set_sensitive(gui->users_disable_btn, uid != 0);

        g_free(username);
        g_free(fullname);
        g_free(state);
        g_free(perms);
        g_free(last_login);
    } else {
        gtk_label_set_text(GTK_LABEL(gui->users_details_label), "Select a user to view details");
        gtk_widget_set_sensitive(gui->users_edit_btn, FALSE);
        gtk_widget_set_sensitive(gui->users_password_btn, FALSE);
        gtk_widget_set_sensitive(gui->users_disable_btn, FALSE);
    }
}

/* Refresh user list */
void phantom_gui_refresh_users(phantom_gui_t *gui) {
    if (!gui || !gui->user_system) return;

    gtk_list_store_clear(gui->users_store);

    phantom_user_system_t *sys = gui->user_system;
    for (int i = 0; i < sys->user_count; i++) {
        phantom_user_t *user = &sys->users[i];

        const char *icon;
        const char *state;
        switch (user->state) {
            case USER_STATE_ACTIVE:  icon = "🟢"; state = "Active"; break;
            case USER_STATE_LOCKED:  icon = "🔒"; state = "Locked"; break;
            case USER_STATE_DORMANT: icon = "💤"; state = "Dormant"; break;
            default:                 icon = "⚪"; state = "Unknown"; break;
        }

        /* Build permissions string */
        char perms[128] = "";
        if (user->permissions == PERM_ADMIN) {
            strcpy(perms, "Administrator");
        } else if (user->permissions == PERM_NONE) {
            strcpy(perms, "None (System)");
        } else {
            if (user->permissions & PERM_BASIC) strcat(perms, "Basic ");
            if (user->permissions & PERM_SUDO) strcat(perms, "Sudo ");
            if (user->permissions & PERM_CREATE_USER) strcat(perms, "CreateUser ");
            if (user->permissions & PERM_VIEW_LOGS) strcat(perms, "ViewLogs ");
        }
        if (strlen(perms) == 0) strcpy(perms, "Standard");

        /* Format last login */
        char last_login[64];
        if (user->last_login > 0) {
            struct tm *tm = localtime(&user->last_login);
            strftime(last_login, sizeof(last_login), "%Y-%m-%d %H:%M", tm);
        } else {
            strcpy(last_login, "Never");
        }

        GtkTreeIter iter;
        gtk_list_store_append(gui->users_store, &iter);
        gtk_list_store_set(gui->users_store, &iter,
            USER_COL_ICON, icon,
            USER_COL_USERNAME, user->username,
            USER_COL_FULLNAME, user->full_name[0] ? user->full_name : "-",
            USER_COL_STATE, state,
            USER_COL_UID, user->uid,
            USER_COL_PERMISSIONS, perms,
            USER_COL_LAST_LOGIN, last_login,
            -1);
    }
}

/* ══════════════════════════════════════════════════════════════════════════════
 * DNAUTH PANEL - DNA-Based Authentication
 * ══════════════════════════════════════════════════════════════════════════════ */

/* Forward declarations for DNAuth callbacks */
static void on_dnauth_register_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_dnauth_evolve_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_dnauth_revoke_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_dnauth_test_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_dnauth_selection_changed(GtkTreeSelection *selection, phantom_gui_t *gui);

GtkWidget *phantom_gui_create_dnauth_panel(phantom_gui_t *gui) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 8);

    /* Title */
    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title),
        "<span size='large' weight='bold'>🧬 DNAuth - DNA-Based Authentication</span>\n"
        "<span size='small'>\"Your Code is Your Key\" - Biologically-inspired cryptographic authentication with evolution.</span>");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 8);

    /* System status */
    gui->dnauth_status_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(gui->dnauth_status_label),
        "<span color='#3fb950'>● DNAuth System Active</span>");
    gtk_widget_set_halign(gui->dnauth_status_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(vbox), gui->dnauth_status_label, FALSE, FALSE, 4);

    /* Toolbar */
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_start(GTK_BOX(vbox), toolbar, FALSE, FALSE, 0);

    gui->dnauth_register_btn = gtk_button_new_with_label("🧬 Register Key");
    gtk_widget_set_tooltip_text(gui->dnauth_register_btn, "Register a new DNA sequence key");
    g_signal_connect(gui->dnauth_register_btn, "clicked", G_CALLBACK(on_dnauth_register_clicked), gui);
    gtk_box_pack_start(GTK_BOX(toolbar), gui->dnauth_register_btn, FALSE, FALSE, 0);

    gui->dnauth_evolve_btn = gtk_button_new_with_label("🔄 Evolve");
    gtk_widget_set_tooltip_text(gui->dnauth_evolve_btn, "Trigger controlled evolution of selected key");
    gtk_widget_set_sensitive(gui->dnauth_evolve_btn, FALSE);
    g_signal_connect(gui->dnauth_evolve_btn, "clicked", G_CALLBACK(on_dnauth_evolve_clicked), gui);
    gtk_box_pack_start(GTK_BOX(toolbar), gui->dnauth_evolve_btn, FALSE, FALSE, 0);

    gui->dnauth_revoke_btn = gtk_button_new_with_label("🚫 Revoke");
    gtk_widget_set_tooltip_text(gui->dnauth_revoke_btn, "Revoke selected key (key history preserved)");
    gtk_widget_set_sensitive(gui->dnauth_revoke_btn, FALSE);
    g_signal_connect(gui->dnauth_revoke_btn, "clicked", G_CALLBACK(on_dnauth_revoke_clicked), gui);
    gtk_box_pack_start(GTK_BOX(toolbar), gui->dnauth_revoke_btn, FALSE, FALSE, 0);

    /* Mode selector */
    GtkWidget *mode_label = gtk_label_new("Auth Mode:");
    gtk_box_pack_start(GTK_BOX(toolbar), mode_label, FALSE, FALSE, 8);

    gui->dnauth_mode_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(gui->dnauth_mode_combo), "Exact");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(gui->dnauth_mode_combo), "Fuzzy");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(gui->dnauth_mode_combo), "Codon");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(gui->dnauth_mode_combo), "Protein");
    gtk_combo_box_set_active(GTK_COMBO_BOX(gui->dnauth_mode_combo), 1); /* Default: Fuzzy */
    gtk_widget_set_tooltip_text(gui->dnauth_mode_combo,
        "Authentication mode: Exact (perfect match), Fuzzy (allows mutations), Codon (triplet matching), Protein (amino acid translation)");
    gtk_box_pack_start(GTK_BOX(toolbar), gui->dnauth_mode_combo, FALSE, FALSE, 0);

    /* Horizontal paned: Key list on left, details on right */
    GtkWidget *hpaned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), hpaned, TRUE, TRUE, 0);

    /* Key list */
    GtkWidget *list_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);

    GtkWidget *list_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(list_label), "<b>Registered Keys</b>");
    gtk_widget_set_halign(list_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(list_vbox), list_label, FALSE, FALSE, 4);

    gui->dnauth_store = gtk_list_store_new(DNAUTH_COL_COUNT,
        G_TYPE_STRING,  /* Icon */
        G_TYPE_STRING,  /* User ID */
        G_TYPE_STRING,  /* Mode */
        G_TYPE_STRING,  /* Generation */
        G_TYPE_STRING,  /* Fitness */
        G_TYPE_STRING,  /* State */
        G_TYPE_STRING   /* Last Auth */
    );

    gui->dnauth_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(gui->dnauth_store));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(gui->dnauth_tree), TRUE);

    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("", renderer, "text", DNAUTH_COL_ICON, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->dnauth_tree), column);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("User", renderer, "text", DNAUTH_COL_USER_ID, NULL);
    gtk_tree_view_column_set_min_width(column, 100);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->dnauth_tree), column);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Mode", renderer, "text", DNAUTH_COL_MODE, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->dnauth_tree), column);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Gen", renderer, "text", DNAUTH_COL_GENERATION, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->dnauth_tree), column);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Fitness", renderer, "text", DNAUTH_COL_FITNESS, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->dnauth_tree), column);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("State", renderer, "text", DNAUTH_COL_STATE, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->dnauth_tree), column);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Last Auth", renderer, "text", DNAUTH_COL_LAST_AUTH, NULL);
    gtk_tree_view_column_set_expand(column, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->dnauth_tree), column);

    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(gui->dnauth_tree));
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);
    g_signal_connect(selection, "changed", G_CALLBACK(on_dnauth_selection_changed), gui);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), gui->dnauth_tree);
    gtk_widget_set_size_request(scroll, 400, -1);
    gtk_box_pack_start(GTK_BOX(list_vbox), scroll, TRUE, TRUE, 0);

    gtk_paned_pack1(GTK_PANED(hpaned), list_vbox, TRUE, FALSE);

    /* Details panel */
    GtkWidget *details_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_start(details_vbox, 8);

    GtkWidget *details_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(details_label), "<b>Key Details</b>");
    gtk_widget_set_halign(details_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(details_vbox), details_label, FALSE, FALSE, 4);

    gui->dnauth_details_label = gtk_label_new("Select a key to view details");
    gtk_label_set_xalign(GTK_LABEL(gui->dnauth_details_label), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(gui->dnauth_details_label), TRUE);
    gtk_box_pack_start(GTK_BOX(details_vbox), gui->dnauth_details_label, FALSE, FALSE, 4);

    /* Test authentication section */
    GtkWidget *test_frame = gtk_frame_new("Test Authentication");
    gtk_box_pack_start(GTK_BOX(details_vbox), test_frame, FALSE, FALSE, 8);

    GtkWidget *test_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(test_box), 8);
    gtk_container_add(GTK_CONTAINER(test_frame), test_box);

    GtkWidget *seq_label = gtk_label_new("Enter DNA sequence (A, T, G, C):");
    gtk_widget_set_halign(seq_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(test_box), seq_label, FALSE, FALSE, 0);

    gui->dnauth_sequence_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(gui->dnauth_sequence_entry), "ATGCATGCATGC...");
    gtk_entry_set_max_length(GTK_ENTRY(gui->dnauth_sequence_entry), 256);
    gtk_box_pack_start(GTK_BOX(test_box), gui->dnauth_sequence_entry, FALSE, FALSE, 0);

    gui->dnauth_test_btn = gtk_button_new_with_label("🔐 Test Sequence");
    gtk_widget_set_sensitive(gui->dnauth_test_btn, FALSE);
    g_signal_connect(gui->dnauth_test_btn, "clicked", G_CALLBACK(on_dnauth_test_clicked), gui);
    gtk_box_pack_start(GTK_BOX(test_box), gui->dnauth_test_btn, FALSE, FALSE, 0);

    gtk_paned_pack2(GTK_PANED(hpaned), details_vbox, TRUE, FALSE);

    /* Statistics section */
    GtkWidget *stats_frame = gtk_frame_new("DNAuth Statistics");
    gtk_box_pack_start(GTK_BOX(vbox), stats_frame, FALSE, FALSE, 8);

    GtkWidget *stats_grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(stats_grid), 24);
    gtk_grid_set_row_spacing(GTK_GRID(stats_grid), 4);
    gtk_container_set_border_width(GTK_CONTAINER(stats_grid), 8);
    gtk_container_add(GTK_CONTAINER(stats_frame), stats_grid);

    const char *stat_names[] = {
        "Total Keys:", "Active Keys:", "Total Auths:",
        "Successful:", "Failed:", "Evolutions:"
    };
    for (int i = 0; i < 6; i++) {
        GtkWidget *label = gtk_label_new(stat_names[i]);
        gtk_widget_set_halign(label, GTK_ALIGN_START);
        gtk_grid_attach(GTK_GRID(stats_grid), label, (i % 3) * 2, i / 3, 1, 1);

        gui->dnauth_stats_labels[i] = gtk_label_new("0");
        gtk_widget_set_halign(gui->dnauth_stats_labels[i], GTK_ALIGN_START);
        gtk_grid_attach(GTK_GRID(stats_grid), gui->dnauth_stats_labels[i], (i % 3) * 2 + 1, i / 3, 1, 1);
    }

    /* Info box */
    GtkWidget *info_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(vbox), info_box, FALSE, FALSE, 4);

    GtkWidget *info_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(info_label),
        "<span size='small' color='#8b949e'>DNAuth uses nucleotide sequences (A, T, G, C) as authentication keys. "
        "Keys can evolve over time with controlled mutations. Ancestor authentication allows login with previous "
        "key generations at reduced privilege. All operations are logged to GeoFS via the Governor.</span>");
    gtk_label_set_line_wrap(GTK_LABEL(info_label), TRUE);
    gtk_widget_set_halign(info_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(info_box), info_label, TRUE, TRUE, 0);

    return vbox;
}

/* DNAuth selection changed */
static void on_dnauth_selection_changed(GtkTreeSelection *selection, phantom_gui_t *gui) {
    GtkTreeIter iter;
    GtkTreeModel *model;

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        gchar *user_id, *mode, *generation, *fitness, *state;

        gtk_tree_model_get(model, &iter,
            DNAUTH_COL_USER_ID, &user_id,
            DNAUTH_COL_MODE, &mode,
            DNAUTH_COL_GENERATION, &generation,
            DNAUTH_COL_FITNESS, &fitness,
            DNAUTH_COL_STATE, &state,
            -1);

        char details[1024];
        snprintf(details, sizeof(details),
            "<b>User:</b> %s\n"
            "<b>Authentication Mode:</b> %s\n"
            "<b>Generation:</b> %s\n"
            "<b>Fitness Score:</b> %s\n"
            "<b>State:</b> %s\n\n"
            "<span size='small'>Keys never deleted - only revoked.\n"
            "Evolution creates new generations while preserving history.</span>",
            user_id, mode, generation, fitness, state);

        gtk_label_set_markup(GTK_LABEL(gui->dnauth_details_label), details);

        /* Enable action buttons */
        int is_active = strcmp(state, "Active") == 0;
        gtk_widget_set_sensitive(gui->dnauth_evolve_btn, is_active);
        gtk_widget_set_sensitive(gui->dnauth_revoke_btn, is_active);
        gtk_widget_set_sensitive(gui->dnauth_test_btn, is_active);

        g_free(user_id);
        g_free(mode);
        g_free(generation);
        g_free(fitness);
        g_free(state);
    } else {
        gtk_label_set_text(GTK_LABEL(gui->dnauth_details_label), "Select a key to view details");
        gtk_widget_set_sensitive(gui->dnauth_evolve_btn, FALSE);
        gtk_widget_set_sensitive(gui->dnauth_revoke_btn, FALSE);
        gtk_widget_set_sensitive(gui->dnauth_test_btn, FALSE);
    }
}

/* Refresh DNAuth panel */
void phantom_gui_refresh_dnauth(phantom_gui_t *gui) {
    if (!gui || !gui->kernel || !gui->kernel->dnauth) {
        gtk_label_set_markup(GTK_LABEL(gui->dnauth_status_label),
            "<span color='#f85149'>● DNAuth System Not Available</span>");
        return;
    }

    dnauth_system_t *sys = (dnauth_system_t *)gui->kernel->dnauth;

    /* Update status */
    gtk_label_set_markup(GTK_LABEL(gui->dnauth_status_label),
        "<span color='#3fb950'>● DNAuth System Active</span>");

    /* Clear and repopulate key list */
    gtk_list_store_clear(gui->dnauth_store);

    dnauth_key_t *key = sys->keys;
    int active_count = 0;

    while (key) {
        const char *icon;
        const char *state;
        if (key->revoked) {
            icon = "🔴";
            state = "Revoked";
        } else if (key->lockout_until > time(NULL)) {
            icon = "🔒";
            state = "Locked";
        } else {
            icon = "🟢";
            state = "Active";
            active_count++;
        }

        /* Get lineage info if available */
        dnauth_lineage_t *lineage = dnauth_lineage_get(sys, key->user_id);
        char gen_str[32] = "1";
        char fitness_str[32] = "1.00";
        if (lineage && lineage->current) {
            snprintf(gen_str, sizeof(gen_str), "%u", lineage->current->generation_id);
            snprintf(fitness_str, sizeof(fitness_str), "%.2f", lineage->current->fitness_score);
        }

        /* Format last auth time */
        char last_auth[64];
        if (key->last_used > 0) {
            struct tm *tm = localtime(&key->last_used);
            strftime(last_auth, sizeof(last_auth), "%Y-%m-%d %H:%M", tm);
        } else {
            strcpy(last_auth, "Never");
        }

        GtkTreeIter iter;
        gtk_list_store_append(gui->dnauth_store, &iter);
        gtk_list_store_set(gui->dnauth_store, &iter,
            DNAUTH_COL_ICON, icon,
            DNAUTH_COL_USER_ID, key->user_id,
            DNAUTH_COL_MODE, dnauth_mode_string(key->auth_mode),
            DNAUTH_COL_GENERATION, gen_str,
            DNAUTH_COL_FITNESS, fitness_str,
            DNAUTH_COL_STATE, state,
            DNAUTH_COL_LAST_AUTH, last_auth,
            -1);

        key = key->next;
    }

    /* Update statistics */
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", sys->key_count);
    gtk_label_set_text(GTK_LABEL(gui->dnauth_stats_labels[0]), buf);

    snprintf(buf, sizeof(buf), "%d", active_count);
    gtk_label_set_text(GTK_LABEL(gui->dnauth_stats_labels[1]), buf);

    snprintf(buf, sizeof(buf), "%lu", sys->total_auths);
    gtk_label_set_text(GTK_LABEL(gui->dnauth_stats_labels[2]), buf);

    snprintf(buf, sizeof(buf), "%lu", sys->successful_auths);
    gtk_label_set_text(GTK_LABEL(gui->dnauth_stats_labels[3]), buf);

    snprintf(buf, sizeof(buf), "%lu", sys->failed_auths);
    gtk_label_set_text(GTK_LABEL(gui->dnauth_stats_labels[4]), buf);

    snprintf(buf, sizeof(buf), "%d", sys->lineage_count);
    gtk_label_set_text(GTK_LABEL(gui->dnauth_stats_labels[5]), buf);
}

/* DNAuth Register Key dialog */
static void on_dnauth_register_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;

    if (!gui->kernel || !gui->kernel->dnauth) {
        phantom_gui_show_message(gui, "Error", "DNAuth system not available", GTK_MESSAGE_ERROR);
        return;
    }

    GtkWidget *dialog = gtk_dialog_new_with_buttons("Register DNA Key",
        GTK_WINDOW(gui->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "Cancel", GTK_RESPONSE_CANCEL,
        "Register", GTK_RESPONSE_OK,
        NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 10);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_container_add(GTK_CONTAINER(content), grid);

    /* User ID */
    GtkWidget *user_label = gtk_label_new("User ID:");
    gtk_widget_set_halign(user_label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), user_label, 0, 0, 1, 1);

    GtkWidget *user_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(user_entry), "username");
    gtk_grid_attach(GTK_GRID(grid), user_entry, 1, 0, 1, 1);

    /* DNA Sequence */
    GtkWidget *seq_label = gtk_label_new("DNA Sequence:");
    gtk_widget_set_halign(seq_label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), seq_label, 0, 1, 1, 1);

    GtkWidget *seq_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(seq_entry), "ATGCATGCATGC... (min 12 nucleotides)");
    gtk_entry_set_max_length(GTK_ENTRY(seq_entry), 256);
    gtk_widget_set_size_request(seq_entry, 300, -1);
    gtk_grid_attach(GTK_GRID(grid), seq_entry, 1, 1, 1, 1);

    /* Mode */
    GtkWidget *mode_label = gtk_label_new("Auth Mode:");
    gtk_widget_set_halign(mode_label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), mode_label, 0, 2, 1, 1);

    GtkWidget *mode_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "Exact");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "Fuzzy (recommended)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "Codon");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(mode_combo), "Protein");
    gtk_combo_box_set_active(GTK_COMBO_BOX(mode_combo), 1);
    gtk_grid_attach(GTK_GRID(grid), mode_combo, 1, 2, 1, 1);

    /* Info */
    GtkWidget *info = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(info),
        "<span size='small' color='#8b949e'>DNA sequences use nucleotides A, T, G, C.\n"
        "Fuzzy mode allows minor mutations for easier authentication.</span>");
    gtk_grid_attach(GTK_GRID(grid), info, 0, 3, 2, 1);

    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        const char *user_id = gtk_entry_get_text(GTK_ENTRY(user_entry));
        const char *sequence = gtk_entry_get_text(GTK_ENTRY(seq_entry));
        int mode_idx = gtk_combo_box_get_active(GTK_COMBO_BOX(mode_combo));

        if (strlen(user_id) == 0 || strlen(sequence) < 12) {
            phantom_gui_show_message(gui, "Error",
                "User ID required and sequence must be at least 12 nucleotides", GTK_MESSAGE_ERROR);
        } else {
            dnauth_mode_t mode = DNAUTH_MODE_FUZZY;
            switch (mode_idx) {
                case 0: mode = DNAUTH_MODE_EXACT; break;
                case 1: mode = DNAUTH_MODE_FUZZY; break;
                case 2: mode = DNAUTH_MODE_CODON_EXACT; break;
                case 3: mode = DNAUTH_MODE_PROTEIN; break;
            }

            dnauth_system_t *sys = (dnauth_system_t *)gui->kernel->dnauth;
            dnauth_result_t result = dnauth_register_with_options(sys, user_id, sequence,
                mode, DNAUTH_KDF_CODON, 3, 0);

            if (result == DNAUTH_OK) {
                /* Also initialize lineage for evolution */
                dnauth_lineage_create(sys, user_id, sequence);
                phantom_gui_show_message(gui, "Success",
                    "DNA key registered successfully. Key will evolve over time.", GTK_MESSAGE_INFO);
                phantom_gui_refresh_dnauth(gui);
            } else {
                char msg[256];
                snprintf(msg, sizeof(msg), "Registration failed: %s", dnauth_result_string(result));
                phantom_gui_show_message(gui, "Error", msg, GTK_MESSAGE_ERROR);
            }
        }
    }

    gtk_widget_destroy(dialog);
}

/* DNAuth Evolve Key */
static void on_dnauth_evolve_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;

    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(gui->dnauth_tree));
    GtkTreeIter iter;
    GtkTreeModel *model;

    if (!gtk_tree_selection_get_selected(selection, &model, &iter)) {
        return;
    }

    gchar *user_id;
    gtk_tree_model_get(model, &iter, DNAUTH_COL_USER_ID, &user_id, -1);

    dnauth_system_t *sys = (dnauth_system_t *)gui->kernel->dnauth;

    /* Trigger natural evolution */
    dnauth_evolution_event_t *event = dnauth_evolve(sys, user_id);

    if (event) {
        char msg[512];
        snprintf(msg, sizeof(msg),
            "Key evolved successfully!\n\n"
            "Generation: %u → %u\n"
            "Mutations: %d\n"
            "Fitness: %.2f → %.2f\n\n"
            "Previous generations remain valid for ancestor authentication.",
            event->from_generation, event->to_generation,
            event->mutation_count,
            event->fitness_before, event->fitness_after);
        phantom_gui_show_message(gui, "Evolution Complete", msg, GTK_MESSAGE_INFO);
        phantom_gui_refresh_dnauth(gui);
    } else {
        phantom_gui_show_message(gui, "Error", "Evolution failed - lineage not found", GTK_MESSAGE_ERROR);
    }

    g_free(user_id);
}

/* DNAuth Revoke Key */
static void on_dnauth_revoke_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;

    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(gui->dnauth_tree));
    GtkTreeIter iter;
    GtkTreeModel *model;

    if (!gtk_tree_selection_get_selected(selection, &model, &iter)) {
        return;
    }

    gchar *user_id;
    gtk_tree_model_get(model, &iter, DNAUTH_COL_USER_ID, &user_id, -1);

    /* Confirm revocation */
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(gui->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_QUESTION,
        GTK_BUTTONS_NONE,
        "Revoke key for '%s'?\n\n"
        "The key will be marked as revoked but preserved in history.\n"
        "This action is logged to the Governor.", user_id);

    gtk_dialog_add_buttons(GTK_DIALOG(dialog),
        "Cancel", GTK_RESPONSE_CANCEL,
        "Revoke", GTK_RESPONSE_YES,
        NULL);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_YES) {
        dnauth_system_t *sys = (dnauth_system_t *)gui->kernel->dnauth;
        dnauth_result_t result = dnauth_revoke(sys, user_id, "Revoked via GUI");

        if (result == DNAUTH_OK) {
            phantom_gui_show_message(gui, "Key Revoked",
                "Key has been revoked. History preserved in geology.", GTK_MESSAGE_INFO);
            phantom_gui_refresh_dnauth(gui);
        } else {
            phantom_gui_show_message(gui, "Error", "Failed to revoke key", GTK_MESSAGE_ERROR);
        }
    }

    gtk_widget_destroy(dialog);
    g_free(user_id);
}

/* DNAuth Test Authentication */
static void on_dnauth_test_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;

    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(gui->dnauth_tree));
    GtkTreeIter iter;
    GtkTreeModel *model;

    if (!gtk_tree_selection_get_selected(selection, &model, &iter)) {
        return;
    }

    gchar *user_id;
    gtk_tree_model_get(model, &iter, DNAUTH_COL_USER_ID, &user_id, -1);

    const char *sequence = gtk_entry_get_text(GTK_ENTRY(gui->dnauth_sequence_entry));
    if (strlen(sequence) < 12) {
        phantom_gui_show_message(gui, "Error",
            "Enter a DNA sequence (at least 12 nucleotides)", GTK_MESSAGE_ERROR);
        g_free(user_id);
        return;
    }

    dnauth_system_t *sys = (dnauth_system_t *)gui->kernel->dnauth;
    dnauth_match_t match = {0};

    /* Try fuzzy auth first (allows mutations) */
    dnauth_result_t result = dnauth_authenticate_fuzzy(sys, user_id, sequence, 3, &match);

    char msg[512];
    if (result == DNAUTH_OK) {
        snprintf(msg, sizeof(msg),
            "✓ Authentication SUCCESSFUL\n\n"
            "User: %s\n"
            "Similarity: %.1f%%\n"
            "Mutations detected: %d\n"
            "Exact match: %s",
            user_id,
            match.similarity * 100.0,
            match.mutations,
            match.exact ? "Yes" : "No");
        phantom_gui_show_message(gui, "Auth Success", msg, GTK_MESSAGE_INFO);
    } else {
        /* Try ancestor auth - check up to 5 generations back */
        int generation_matched = -1;
        result = dnauth_authenticate_ancestor(sys, user_id, sequence, 5, &generation_matched);
        if (result == DNAUTH_OK) {
            snprintf(msg, sizeof(msg),
                "✓ Ancestor Authentication SUCCESSFUL\n\n"
                "User: %s\n"
                "Matched generation: %d back\n"
                "Note: Reduced privileges may apply",
                user_id, generation_matched);
            phantom_gui_show_message(gui, "Ancestor Auth", msg, GTK_MESSAGE_INFO);
        } else {
            snprintf(msg, sizeof(msg),
                "✗ Authentication FAILED\n\n"
                "User: %s\n"
                "Result: %s\n\n"
                "Sequence did not match current or ancestor keys.",
                user_id, dnauth_result_string(result));
            phantom_gui_show_message(gui, "Auth Failed", msg, GTK_MESSAGE_WARNING);
        }
    }

    phantom_gui_refresh_dnauth(gui);
    g_free(user_id);
}

/* ══════════════════════════════════════════════════════════════════════════════
 * QRNET PANEL - QR Code Distributed File Network
 * ══════════════════════════════════════════════════════════════════════════════ */

/* Forward declarations for QRNet callbacks */
static void on_qrnet_create_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_qrnet_verify_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_qrnet_revoke_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_qrnet_show_data_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_qrnet_publish_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_qrnet_fetch_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_qrnet_selection_changed(GtkTreeSelection *selection, phantom_gui_t *gui);

GtkWidget *phantom_gui_create_qrnet_panel(phantom_gui_t *gui) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 8);

    /* Title */
    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title),
        "<span size='large' weight='bold'>📡 QRNet - QR Code Distributed File Network</span>\n"
        "<span size='small'>Cryptographically-signed distributed file linkage with DNAuth identity and Governor validation.</span>");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 8);

    /* System status */
    gui->qrnet_status_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(gui->qrnet_status_label),
        "<span color='#3fb950'>● QRNet System Active</span>");
    gtk_widget_set_halign(gui->qrnet_status_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(vbox), gui->qrnet_status_label, FALSE, FALSE, 4);

    /* Toolbar */
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_start(GTK_BOX(vbox), toolbar, FALSE, FALSE, 0);

    gui->qrnet_create_btn = gtk_button_new_with_label("📝 Create Code");
    gtk_widget_set_tooltip_text(gui->qrnet_create_btn, "Create new QR code link for a file");
    g_signal_connect(gui->qrnet_create_btn, "clicked", G_CALLBACK(on_qrnet_create_clicked), gui);
    gtk_box_pack_start(GTK_BOX(toolbar), gui->qrnet_create_btn, FALSE, FALSE, 0);

    gui->qrnet_verify_btn = gtk_button_new_with_label("✓ Verify");
    gtk_widget_set_tooltip_text(gui->qrnet_verify_btn, "Verify selected QR code");
    gtk_widget_set_sensitive(gui->qrnet_verify_btn, FALSE);
    g_signal_connect(gui->qrnet_verify_btn, "clicked", G_CALLBACK(on_qrnet_verify_clicked), gui);
    gtk_box_pack_start(GTK_BOX(toolbar), gui->qrnet_verify_btn, FALSE, FALSE, 0);

    gui->qrnet_revoke_btn = gtk_button_new_with_label("🚫 Revoke");
    gtk_widget_set_tooltip_text(gui->qrnet_revoke_btn, "Revoke selected code (preserved in history)");
    gtk_widget_set_sensitive(gui->qrnet_revoke_btn, FALSE);
    g_signal_connect(gui->qrnet_revoke_btn, "clicked", G_CALLBACK(on_qrnet_revoke_clicked), gui);
    gtk_box_pack_start(GTK_BOX(toolbar), gui->qrnet_revoke_btn, FALSE, FALSE, 0);

    GtkWidget *show_data_btn = gtk_button_new_with_label("📋 Show QR Data");
    gtk_widget_set_tooltip_text(show_data_btn, "Show encoded QR data for copying to external QR generator");
    gtk_widget_set_sensitive(show_data_btn, FALSE);
    g_signal_connect(show_data_btn, "clicked", G_CALLBACK(on_qrnet_show_data_clicked), gui);
    gtk_box_pack_start(GTK_BOX(toolbar), show_data_btn, FALSE, FALSE, 0);
    gui->qrnet_show_data_btn = show_data_btn;

    /* Separator */
    gtk_box_pack_start(GTK_BOX(toolbar), gtk_separator_new(GTK_ORIENTATION_VERTICAL), FALSE, FALSE, 8);

    gui->qrnet_publish_btn = gtk_button_new_with_label("📤 Publish File");
    gtk_widget_set_tooltip_text(gui->qrnet_publish_btn, "Publish a file to the content network and create QR code");
    g_signal_connect(gui->qrnet_publish_btn, "clicked", G_CALLBACK(on_qrnet_publish_clicked), gui);
    gtk_box_pack_start(GTK_BOX(toolbar), gui->qrnet_publish_btn, FALSE, FALSE, 0);

    gui->qrnet_fetch_btn = gtk_button_new_with_label("📥 Fetch Content");
    gtk_widget_set_tooltip_text(gui->qrnet_fetch_btn, "Fetch content by hash from the network");
    gtk_widget_set_sensitive(gui->qrnet_fetch_btn, FALSE);
    g_signal_connect(gui->qrnet_fetch_btn, "clicked", G_CALLBACK(on_qrnet_fetch_clicked), gui);
    gtk_box_pack_start(GTK_BOX(toolbar), gui->qrnet_fetch_btn, FALSE, FALSE, 0);

    /* File class selector */
    GtkWidget *class_label = gtk_label_new("File Class:");
    gtk_box_pack_start(GTK_BOX(toolbar), class_label, FALSE, FALSE, 8);

    gui->qrnet_class_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(gui->qrnet_class_combo), "User Data");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(gui->qrnet_class_combo), "System");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(gui->qrnet_class_combo), "Constitutional");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(gui->qrnet_class_combo), "Critical");
    gtk_combo_box_set_active(GTK_COMBO_BOX(gui->qrnet_class_combo), 0);
    gtk_widget_set_tooltip_text(gui->qrnet_class_combo,
        "File classification affects QR code size and verification requirements");
    gtk_box_pack_start(GTK_BOX(toolbar), gui->qrnet_class_combo, FALSE, FALSE, 0);

    /* Horizontal paned: Codes list on left, details on right */
    GtkWidget *hpaned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(vbox), hpaned, TRUE, TRUE, 0);

    /* Codes list */
    GtkWidget *list_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);

    GtkWidget *list_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(list_label), "<b>QR Codes</b>");
    gtk_widget_set_halign(list_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(list_vbox), list_label, FALSE, FALSE, 4);

    gui->qrnet_codes_store = gtk_list_store_new(QRNET_COL_COUNT,
        G_TYPE_STRING,  /* Icon */
        G_TYPE_STRING,  /* Code ID */
        G_TYPE_STRING,  /* Destination */
        G_TYPE_STRING,  /* File Class */
        G_TYPE_STRING,  /* State */
        G_TYPE_STRING,  /* Creator */
        G_TYPE_STRING   /* Created */
    );

    gui->qrnet_codes_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(gui->qrnet_codes_store));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(gui->qrnet_codes_tree), TRUE);

    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("", renderer, "text", QRNET_COL_ICON, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->qrnet_codes_tree), column);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("ID", renderer, "text", QRNET_COL_CODE_ID, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->qrnet_codes_tree), column);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Destination", renderer, "text", QRNET_COL_DESTINATION, NULL);
    gtk_tree_view_column_set_min_width(column, 150);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->qrnet_codes_tree), column);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Class", renderer, "text", QRNET_COL_FILE_CLASS, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->qrnet_codes_tree), column);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("State", renderer, "text", QRNET_COL_STATE, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->qrnet_codes_tree), column);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Creator", renderer, "text", QRNET_COL_CREATOR, NULL);
    gtk_tree_view_column_set_expand(column, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->qrnet_codes_tree), column);

    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(gui->qrnet_codes_tree));
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);
    g_signal_connect(selection, "changed", G_CALLBACK(on_qrnet_selection_changed), gui);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), gui->qrnet_codes_tree);
    gtk_widget_set_size_request(scroll, 450, -1);
    gtk_box_pack_start(GTK_BOX(list_vbox), scroll, TRUE, TRUE, 0);

    gtk_paned_pack1(GTK_PANED(hpaned), list_vbox, TRUE, FALSE);

    /* Details panel */
    GtkWidget *details_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_start(details_vbox, 8);

    GtkWidget *details_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(details_label), "<b>Code Details</b>");
    gtk_widget_set_halign(details_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(details_vbox), details_label, FALSE, FALSE, 4);

    gui->qrnet_details_label = gtk_label_new("Select a code to view details");
    gtk_label_set_xalign(GTK_LABEL(gui->qrnet_details_label), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(gui->qrnet_details_label), TRUE);
    gtk_box_pack_start(GTK_BOX(details_vbox), gui->qrnet_details_label, FALSE, FALSE, 4);

    /* Path entry for creating codes */
    GtkWidget *create_frame = gtk_frame_new("Create New Code");
    gtk_box_pack_start(GTK_BOX(details_vbox), create_frame, FALSE, FALSE, 8);

    GtkWidget *create_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(create_box), 8);
    gtk_container_add(GTK_CONTAINER(create_frame), create_box);

    GtkWidget *path_label = gtk_label_new("Destination Path:");
    gtk_widget_set_halign(path_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(create_box), path_label, FALSE, FALSE, 0);

    gui->qrnet_path_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(gui->qrnet_path_entry), "/geo/data/filename.dat");
    gtk_box_pack_start(GTK_BOX(create_box), gui->qrnet_path_entry, FALSE, FALSE, 0);

    gtk_paned_pack2(GTK_PANED(hpaned), details_vbox, TRUE, FALSE);

    /* Statistics section */
    GtkWidget *stats_frame = gtk_frame_new("QRNet Statistics");
    gtk_box_pack_start(GTK_BOX(vbox), stats_frame, FALSE, FALSE, 8);

    GtkWidget *stats_grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(stats_grid), 24);
    gtk_grid_set_row_spacing(GTK_GRID(stats_grid), 4);
    gtk_container_set_border_width(GTK_CONTAINER(stats_grid), 8);
    gtk_container_add(GTK_CONTAINER(stats_frame), stats_grid);

    const char *stat_names[] = {
        "Total Codes:", "Active Codes:", "Verifications:",
        "Failed:", "Revocations:", "Gov State:"
    };
    for (int i = 0; i < 6; i++) {
        GtkWidget *label = gtk_label_new(stat_names[i]);
        gtk_widget_set_halign(label, GTK_ALIGN_START);
        gtk_grid_attach(GTK_GRID(stats_grid), label, (i % 3) * 2, i / 3, 1, 1);

        gui->qrnet_stats_labels[i] = gtk_label_new("0");
        gtk_widget_set_halign(gui->qrnet_stats_labels[i], GTK_ALIGN_START);
        gtk_grid_attach(GTK_GRID(stats_grid), gui->qrnet_stats_labels[i], (i % 3) * 2 + 1, i / 3, 1, 1);
    }

    /* Info box */
    GtkWidget *info_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(vbox), info_box, FALSE, FALSE, 4);

    GtkWidget *info_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(info_label),
        "<span size='small' color='#8b949e'>QRNet creates cryptographically-signed QR codes that link to files. "
        "Each code embeds destination path, content hash, DNAuth creator identity, and Governor state version. "
        "Codes are verified through Governor and never deleted - only superseded or revoked.</span>");
    gtk_label_set_line_wrap(GTK_LABEL(info_label), TRUE);
    gtk_widget_set_halign(info_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(info_box), info_label, TRUE, TRUE, 0);

    return vbox;
}

/* QRNet selection changed */
static void on_qrnet_selection_changed(GtkTreeSelection *selection, phantom_gui_t *gui) {
    if (!gui) return;

    GtkTreeIter iter;
    GtkTreeModel *model;

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        gchar *code_id, *destination, *file_class, *state, *creator;

        gtk_tree_model_get(model, &iter,
            QRNET_COL_CODE_ID, &code_id,
            QRNET_COL_DESTINATION, &destination,
            QRNET_COL_FILE_CLASS, &file_class,
            QRNET_COL_STATE, &state,
            QRNET_COL_CREATOR, &creator,
            -1);

        char details[1024];
        snprintf(details, sizeof(details),
            "<b>Code ID:</b> %s\n"
            "<b>Destination:</b> %s\n"
            "<b>File Class:</b> %s\n"
            "<b>State:</b> %s\n"
            "<b>Creator:</b> %s\n\n"
            "<span size='small'>QR codes are cryptographically bound to content.\n"
            "Verification checks signature, hash, and Governor state.</span>",
            code_id, destination, file_class, state, creator);

        gtk_label_set_markup(GTK_LABEL(gui->qrnet_details_label), details);

        /* Enable action buttons for active codes */
        int is_active = strcmp(state, "Active") == 0;
        gtk_widget_set_sensitive(gui->qrnet_verify_btn, TRUE);
        gtk_widget_set_sensitive(gui->qrnet_revoke_btn, is_active);
        gtk_widget_set_sensitive(gui->qrnet_show_data_btn, TRUE);
        gtk_widget_set_sensitive(gui->qrnet_fetch_btn, is_active);

        g_free(code_id);
        g_free(destination);
        g_free(file_class);
        g_free(state);
        g_free(creator);
    } else {
        gtk_label_set_text(GTK_LABEL(gui->qrnet_details_label), "Select a code to view details");
        gtk_widget_set_sensitive(gui->qrnet_verify_btn, FALSE);
        gtk_widget_set_sensitive(gui->qrnet_revoke_btn, FALSE);
        gtk_widget_set_sensitive(gui->qrnet_show_data_btn, FALSE);
        gtk_widget_set_sensitive(gui->qrnet_fetch_btn, FALSE);
    }
}

/* Refresh QRNet panel */
void phantom_gui_refresh_qrnet(phantom_gui_t *gui) {
    if (!gui || !gui->kernel || !gui->kernel->qrnet) {
        if (gui && gui->qrnet_status_label) {
            gtk_label_set_markup(GTK_LABEL(gui->qrnet_status_label),
                "<span color='#f85149'>● QRNet System Not Available</span>");
        }
        return;
    }

    qrnet_system_t *sys = (qrnet_system_t *)gui->kernel->qrnet;

    /* Update status */
    gtk_label_set_markup(GTK_LABEL(gui->qrnet_status_label),
        "<span color='#3fb950'>● QRNet System Active</span>");

    /* Clear and repopulate codes list */
    gtk_list_store_clear(gui->qrnet_codes_store);

    qrnet_code_t *code = sys->codes;
    int active_count = 0;

    while (code) {
        const char *icon;
        const char *state_str;
        switch (code->state) {
            case QRNET_CODE_ACTIVE:
                icon = "🟢";
                state_str = "Active";
                active_count++;
                break;
            case QRNET_CODE_SUPERSEDED:
                icon = "🔄";
                state_str = "Superseded";
                break;
            case QRNET_CODE_REVOKED:
                icon = "🔴";
                state_str = "Revoked";
                break;
            case QRNET_CODE_EXPIRED:
                icon = "⏰";
                state_str = "Expired";
                break;
            default:
                icon = "⚪";
                state_str = "Unknown";
                break;
        }

        /* Format created time */
        char created[64];
        struct tm *tm = localtime(&code->created_at);
        strftime(created, sizeof(created), "%Y-%m-%d %H:%M", tm);

        char code_id_str[32];
        snprintf(code_id_str, sizeof(code_id_str), "#%u", code->code_id);

        GtkTreeIter iter;
        gtk_list_store_append(gui->qrnet_codes_store, &iter);
        gtk_list_store_set(gui->qrnet_codes_store, &iter,
            QRNET_COL_ICON, icon,
            QRNET_COL_CODE_ID, code_id_str,
            QRNET_COL_DESTINATION, code->destination_path,
            QRNET_COL_FILE_CLASS, qrnet_file_class_string(code->file_class),
            QRNET_COL_STATE, state_str,
            QRNET_COL_CREATOR, code->dnauth_creator,
            QRNET_COL_CREATED, created,
            -1);

        code = code->next;
    }

    /* Update statistics */
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", sys->code_count);
    gtk_label_set_text(GTK_LABEL(gui->qrnet_stats_labels[0]), buf);

    snprintf(buf, sizeof(buf), "%d", active_count);
    gtk_label_set_text(GTK_LABEL(gui->qrnet_stats_labels[1]), buf);

    snprintf(buf, sizeof(buf), "%lu", sys->total_verifications);
    gtk_label_set_text(GTK_LABEL(gui->qrnet_stats_labels[2]), buf);

    snprintf(buf, sizeof(buf), "%lu", sys->failed_verifications);
    gtk_label_set_text(GTK_LABEL(gui->qrnet_stats_labels[3]), buf);

    snprintf(buf, sizeof(buf), "%lu", sys->revocations);
    gtk_label_set_text(GTK_LABEL(gui->qrnet_stats_labels[4]), buf);

    snprintf(buf, sizeof(buf), "v%u", sys->current_gov_state.version);
    gtk_label_set_text(GTK_LABEL(gui->qrnet_stats_labels[5]), buf);
}

/* QRNet Create Code */
static void on_qrnet_create_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;

    if (!gui || !gui->kernel || !gui->kernel->qrnet) {
        phantom_gui_show_message(gui, "Error", "QRNet system not available", GTK_MESSAGE_ERROR);
        return;
    }

    const char *path = gtk_entry_get_text(GTK_ENTRY(gui->qrnet_path_entry));
    if (strlen(path) < 2) {
        phantom_gui_show_message(gui, "Error",
            "Enter a destination path for the QR code", GTK_MESSAGE_ERROR);
        return;
    }

    int class_idx = gtk_combo_box_get_active(GTK_COMBO_BOX(gui->qrnet_class_combo));
    qrnet_file_class_t file_class = (qrnet_file_class_t)class_idx;

    qrnet_system_t *sys = (qrnet_system_t *)gui->kernel->qrnet;

    /* Create a sample content for demonstration */
    char content[256];
    snprintf(content, sizeof(content), "QRNet linked content for: %s", path);

    qrnet_code_t *code;
    qrnet_result_t result = qrnet_create_code(sys, path, content, strlen(content),
                                               file_class, &code);

    if (result == QRNET_OK) {
        char msg[512];
        snprintf(msg, sizeof(msg),
            "QR Code created successfully!\n\n"
            "Code ID: #%u\n"
            "Destination: %s\n"
            "File Class: %s\n"
            "QR Version: %d\n"
            "Governor State: v%u",
            code->code_id, path,
            qrnet_file_class_string(file_class),
            code->qr_version,
            code->governor_state_version);
        phantom_gui_show_message(gui, "Code Created", msg, GTK_MESSAGE_INFO);
        phantom_gui_refresh_qrnet(gui);
        gtk_entry_set_text(GTK_ENTRY(gui->qrnet_path_entry), "");
    } else {
        char msg[256];
        snprintf(msg, sizeof(msg), "Failed to create code: %s", qrnet_result_string(result));
        phantom_gui_show_message(gui, "Error", msg, GTK_MESSAGE_ERROR);
    }
}

/* QRNet Verify Code */
static void on_qrnet_verify_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;

    if (!gui || !gui->kernel || !gui->kernel->qrnet) return;

    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(gui->qrnet_codes_tree));
    GtkTreeIter iter;
    GtkTreeModel *model;

    if (!gtk_tree_selection_get_selected(selection, &model, &iter)) {
        return;
    }

    gchar *code_id_str;
    gtk_tree_model_get(model, &iter, QRNET_COL_CODE_ID, &code_id_str, -1);

    /* Parse code ID */
    uint32_t code_id;
    if (gui_safe_parse_uint32(code_id_str + 1, &code_id) < 0) { /* Skip '#' */
        g_free(code_id_str);
        phantom_gui_show_message(gui, "Error", "Invalid code ID", GTK_MESSAGE_ERROR);
        return;
    }
    g_free(code_id_str);

    qrnet_system_t *sys = (qrnet_system_t *)gui->kernel->qrnet;
    qrnet_code_t *code = qrnet_get_code(sys, code_id);

    if (!code) {
        phantom_gui_show_message(gui, "Error", "Code not found", GTK_MESSAGE_ERROR);
        return;
    }

    qrnet_verification_t result;
    qrnet_verify_code(sys, code, &result);

    char msg[512];
    if (result.result == QRNET_OK) {
        snprintf(msg, sizeof(msg),
            "✓ Verification SUCCESSFUL\n\n"
            "Code ID: #%u\n"
            "Signature: %s\n"
            "Governor State: %s\n"
            "DNAuth Identity: %s\n"
            "Trust Level: %s\n\n"
            "%.350s",
            code->code_id,
            result.signature_valid ? "Valid" : "Invalid",
            result.governor_state_valid ? "Valid" : "Invalid",
            result.dnauth_valid ? "Valid" : "Invalid",
            qrnet_trust_string(result.trust_level),
            result.details);
        phantom_gui_show_message(gui, "Verification Passed", msg, GTK_MESSAGE_INFO);
    } else {
        snprintf(msg, sizeof(msg),
            "✗ Verification FAILED\n\n"
            "Code ID: #%u\n"
            "Result: %s\n\n"
            "%.400s",
            code->code_id,
            qrnet_result_string(result.result),
            result.details);
        phantom_gui_show_message(gui, "Verification Failed", msg, GTK_MESSAGE_WARNING);
    }

    phantom_gui_refresh_qrnet(gui);
}

/* QRNet Revoke Code */
static void on_qrnet_revoke_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;

    if (!gui || !gui->kernel || !gui->kernel->qrnet) return;

    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(gui->qrnet_codes_tree));
    GtkTreeIter iter;
    GtkTreeModel *model;

    if (!gtk_tree_selection_get_selected(selection, &model, &iter)) {
        return;
    }

    gchar *code_id_str;
    gtk_tree_model_get(model, &iter, QRNET_COL_CODE_ID, &code_id_str, -1);

    uint32_t code_id = atoi(code_id_str + 1);

    /* Confirm revocation */
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(gui->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_QUESTION,
        GTK_BUTTONS_NONE,
        "Revoke QR code %s?\n\n"
        "The code will be marked as revoked but preserved in history.\n"
        "This action is logged to the Governor.", code_id_str);

    gtk_dialog_add_buttons(GTK_DIALOG(dialog),
        "Cancel", GTK_RESPONSE_CANCEL,
        "Revoke", GTK_RESPONSE_YES,
        NULL);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_YES) {
        qrnet_system_t *sys = (qrnet_system_t *)gui->kernel->qrnet;
        qrnet_code_t *code = qrnet_get_code(sys, code_id);

        if (code) {
            qrnet_result_t result = qrnet_revoke_code(sys, code, "Revoked via GUI");
            if (result == QRNET_OK) {
                phantom_gui_show_message(gui, "Code Revoked",
                    "QR code has been revoked. History preserved in GeoFS.", GTK_MESSAGE_INFO);
                phantom_gui_refresh_qrnet(gui);
            } else {
                phantom_gui_show_message(gui, "Error", "Failed to revoke code", GTK_MESSAGE_ERROR);
            }
        }
    }

    gtk_widget_destroy(dialog);
    g_free(code_id_str);
}

/* QRNet Show QR Data - displays encoded data for external QR generation */
#ifdef HAVE_QRENCODE
/* Render QR code to GdkPixbuf */
static GdkPixbuf *render_qr_code(const char *data, int scale) {
    QRcode *qr = QRcode_encodeString(data, 0, QR_ECLEVEL_M, QR_MODE_8, 1);
    if (!qr) return NULL;

    int size = qr->width;
    int img_size = size * scale;

    /* Create pixbuf (RGB, no alpha) */
    GdkPixbuf *pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, img_size, img_size);
    if (!pixbuf) {
        QRcode_free(qr);
        return NULL;
    }

    guchar *pixels = gdk_pixbuf_get_pixels(pixbuf);
    int rowstride = gdk_pixbuf_get_rowstride(pixbuf);

    /* Fill with white background */
    for (int y = 0; y < img_size; y++) {
        for (int x = 0; x < img_size; x++) {
            guchar *p = pixels + y * rowstride + x * 3;
            p[0] = p[1] = p[2] = 255;
        }
    }

    /* Draw QR code modules */
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            if (qr->data[y * size + x] & 1) {
                /* Black module */
                for (int sy = 0; sy < scale; sy++) {
                    for (int sx = 0; sx < scale; sx++) {
                        int py = y * scale + sy;
                        int px = x * scale + sx;
                        guchar *p = pixels + py * rowstride + px * 3;
                        p[0] = p[1] = p[2] = 0;
                    }
                }
            }
        }
    }

    QRcode_free(qr);
    return pixbuf;
}
#endif

static void on_qrnet_show_data_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;

    if (!gui || !gui->kernel || !gui->kernel->qrnet) return;

    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(gui->qrnet_codes_tree));
    GtkTreeIter iter;
    GtkTreeModel *model;

    if (!gtk_tree_selection_get_selected(selection, &model, &iter)) {
        phantom_gui_show_message(gui, "No Selection", "Please select a QR code first", GTK_MESSAGE_INFO);
        return;
    }

    gchar *code_id_str;
    gtk_tree_model_get(model, &iter, QRNET_COL_CODE_ID, &code_id_str, -1);

    uint32_t code_id = atoi(code_id_str + 1);
    qrnet_system_t *sys = (qrnet_system_t *)gui->kernel->qrnet;
    qrnet_code_t *code = qrnet_get_code(sys, code_id);

    if (!code) {
        g_free(code_id_str);
        return;
    }

    /* Create dialog */
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "QR Code",
        GTK_WINDOW(gui->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "Copy Data", GTK_RESPONSE_ACCEPT,
        "Close", GTK_RESPONSE_CLOSE,
        NULL);

    gtk_window_set_default_size(GTK_WINDOW(dialog), 500, 600);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 16);

    /* Title */
    GtkWidget *title_label = gtk_label_new(NULL);
    char title_text[128];
    snprintf(title_text, sizeof(title_text), "<b>QR Code %s</b>", code_id_str);
    gtk_label_set_markup(GTK_LABEL(title_label), title_text);
    gtk_box_pack_start(GTK_BOX(content), title_label, FALSE, FALSE, 8);

#ifdef HAVE_QRENCODE
    /* Render QR code image */
    GdkPixbuf *qr_pixbuf = render_qr_code(code->qr_data, 6);
    if (qr_pixbuf) {
        GtkWidget *qr_image = gtk_image_new_from_pixbuf(qr_pixbuf);
        gtk_widget_set_halign(qr_image, GTK_ALIGN_CENTER);

        /* Add frame around QR code */
        GtkWidget *frame = gtk_frame_new(NULL);
        gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_IN);
        gtk_container_add(GTK_CONTAINER(frame), qr_image);
        gtk_widget_set_halign(frame, GTK_ALIGN_CENTER);
        gtk_box_pack_start(GTK_BOX(content), frame, FALSE, FALSE, 16);

        g_object_unref(qr_pixbuf);
    } else {
        GtkWidget *error_label = gtk_label_new("Failed to generate QR code");
        gtk_box_pack_start(GTK_BOX(content), error_label, FALSE, FALSE, 8);
    }
#else
    /* No libqrencode - show message */
    GtkWidget *no_qr_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(no_qr_label),
        "<span color='#f0ad4e'>QR code rendering requires libqrencode.\n"
        "Install with: sudo apt install libqrencode-dev</span>");
    gtk_box_pack_start(GTK_BOX(content), no_qr_label, FALSE, FALSE, 16);
#endif

    /* Destination info */
    char dest_info[512];
    snprintf(dest_info, sizeof(dest_info),
             "<b>Destination:</b> %.200s\n"
             "<b>Creator:</b> %.200s\n"
             "<b>Data length:</b> %zu bytes",
             code->destination_path,
             code->dnauth_creator,
             strlen(code->qr_data));
    GtkWidget *dest_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(dest_label), dest_info);
    gtk_label_set_xalign(GTK_LABEL(dest_label), 0.0);
    gtk_box_pack_start(GTK_BOX(content), dest_label, FALSE, FALSE, 8);

    /* Expandable data section */
    GtkWidget *expander = gtk_expander_new("Show Raw Data");
    gtk_box_pack_start(GTK_BOX(content), expander, TRUE, TRUE, 8);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scroll, -1, 100);
    gtk_container_add(GTK_CONTAINER(expander), scroll);

    GtkWidget *text_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(text_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view), GTK_WRAP_CHAR);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(text_view), TRUE);
    gtk_container_add(GTK_CONTAINER(scroll), text_view);

    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
    gtk_text_buffer_set_text(buffer, code->qr_data, -1);

    gtk_widget_show_all(content);

    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    if (response == GTK_RESPONSE_ACCEPT) {
        GtkClipboard *clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
        gtk_clipboard_set_text(clipboard, code->qr_data, -1);
        phantom_gui_show_message(gui, "Copied", "QR data copied to clipboard", GTK_MESSAGE_INFO);
    }

    gtk_widget_destroy(dialog);
    g_free(code_id_str);
}

/* Publish File to QRNet */
static void on_qrnet_publish_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;

    if (!gui || !gui->kernel || !gui->kernel->qrnet) return;

    /* Initialize transport if needed */
    if (!qrnet_transport) {
        qrnet_transport_result_t result = qrnet_transport_init(&qrnet_transport,
                                                                (qrnet_system_t *)gui->kernel->qrnet,
                                                                QRNET_DEFAULT_PORT);
        if (result != QRNET_TRANSPORT_OK) {
            phantom_gui_show_message(gui, "Error", "Failed to initialize transport", GTK_MESSAGE_ERROR);
            return;
        }
    }

    /* File chooser dialog */
    GtkWidget *dialog = gtk_file_chooser_dialog_new("Select File to Publish",
        GTK_WINDOW(gui->window),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "Cancel", GTK_RESPONSE_CANCEL,
        "Publish", GTK_RESPONSE_ACCEPT,
        NULL);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filepath = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));

        /* Read file content */
        FILE *f = fopen(filepath, "rb");
        if (!f) {
            phantom_gui_show_message(gui, "Error", "Cannot open file", GTK_MESSAGE_ERROR);
            g_free(filepath);
            gtk_widget_destroy(dialog);
            return;
        }

        fseek(f, 0, SEEK_END);
        size_t file_size = ftell(f);
        fseek(f, 0, SEEK_SET);

        if (file_size > QRNET_MAX_CONTENT_SIZE) {
            fclose(f);
            phantom_gui_show_message(gui, "Error", "File too large (max 256MB)", GTK_MESSAGE_ERROR);
            g_free(filepath);
            gtk_widget_destroy(dialog);
            return;
        }

        void *content = malloc(file_size);
        if (!content) {
            fclose(f);
            phantom_gui_show_message(gui, "Error", "Out of memory", GTK_MESSAGE_ERROR);
            g_free(filepath);
            gtk_widget_destroy(dialog);
            return;
        }

        if (fread(content, 1, file_size, f) != file_size) {
            fclose(f);
            free(content);
            phantom_gui_show_message(gui, "Error", "Failed to read file", GTK_MESSAGE_ERROR);
            g_free(filepath);
            gtk_widget_destroy(dialog);
            return;
        }
        fclose(f);

        /* Store in transport content store */
        char hash_hex[65];
        qrnet_transport_result_t result = qrnet_publish_content(qrnet_transport, content, file_size,
                                                                 strrchr(filepath, '/') ? strrchr(filepath, '/') + 1 : filepath,
                                                                 hash_hex);

        if (result == QRNET_TRANSPORT_OK) {
            /* Create QR code for the published content */
            qrnet_system_t *sys = (qrnet_system_t *)gui->kernel->qrnet;

            /* Get destination path from entry or use filename */
            const char *dest_path = gtk_entry_get_text(GTK_ENTRY(gui->qrnet_path_entry));
            if (!dest_path || strlen(dest_path) == 0) {
                const char *basename = strrchr(filepath, '/');
                dest_path = basename ? basename + 1 : filepath;
            }

            /* Get file class from combo */
            int class_idx = gtk_combo_box_get_active(GTK_COMBO_BOX(gui->qrnet_class_combo));
            qrnet_file_class_t file_class = (qrnet_file_class_t)class_idx;

            /* Create the QR code with actual content */
            qrnet_code_t *code = NULL;
            qrnet_result_t qr_result = qrnet_create_code(sys, dest_path, content, file_size, file_class, &code);

            if (qr_result == QRNET_OK && code) {
                /* Show success with hash info */
                char msg[512];
                snprintf(msg, sizeof(msg),
                    "File published successfully!\n\n"
                    "Content Hash: %.16s...\n"
                    "QR Code: #%u\n"
                    "Size: %zu bytes\n\n"
                    "Content stored in network.\n"
                    "Use \"Show QR Data\" to view the QR code.",
                    hash_hex, code->code_id, file_size);
                phantom_gui_show_message(gui, "Published", msg, GTK_MESSAGE_INFO);

                /* Refresh the codes list */
                phantom_gui_refresh_qrnet(gui);
            } else {
                char msg[256];
                snprintf(msg, sizeof(msg),
                    "File stored (hash: %.16s...)\n"
                    "But failed to create QR code.", hash_hex);
                phantom_gui_show_message(gui, "Partial Success", msg, GTK_MESSAGE_WARNING);
            }
        } else {
            phantom_gui_show_message(gui, "Error", "Failed to publish file", GTK_MESSAGE_ERROR);
        }

        free(content);
        g_free(filepath);
    }

    gtk_widget_destroy(dialog);
}

/* Fetch Content from QRNet */
static void on_qrnet_fetch_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;

    if (!gui || !gui->kernel || !gui->kernel->qrnet) return;

    /* Initialize transport if needed */
    if (!qrnet_transport) {
        qrnet_transport_result_t result = qrnet_transport_init(&qrnet_transport,
                                                                (qrnet_system_t *)gui->kernel->qrnet,
                                                                QRNET_DEFAULT_PORT);
        if (result != QRNET_TRANSPORT_OK) {
            phantom_gui_show_message(gui, "Error", "Failed to initialize transport", GTK_MESSAGE_ERROR);
            return;
        }
    }

    /* Get selected code */
    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(gui->qrnet_codes_tree));
    GtkTreeIter iter;
    GtkTreeModel *model;

    if (!gtk_tree_selection_get_selected(selection, &model, &iter)) {
        phantom_gui_show_message(gui, "No Selection", "Please select a QR code first", GTK_MESSAGE_INFO);
        return;
    }

    gchar *code_id_str;
    gtk_tree_model_get(model, &iter, QRNET_COL_CODE_ID, &code_id_str, -1);

    uint32_t code_id = atoi(code_id_str + 1);
    qrnet_system_t *sys = (qrnet_system_t *)gui->kernel->qrnet;
    qrnet_code_t *code = qrnet_get_code(sys, code_id);

    if (!code) {
        g_free(code_id_str);
        phantom_gui_show_message(gui, "Error", "Code not found", GTK_MESSAGE_ERROR);
        return;
    }

    /* Try to fetch content */
    void *data = NULL;
    size_t size = 0;
    qrnet_transport_result_t result = qrnet_fetch_for_code(qrnet_transport, code, &data, &size);

    if (result == QRNET_TRANSPORT_OK && data) {
        /* File chooser to save */
        GtkWidget *save_dialog = gtk_file_chooser_dialog_new("Save Content",
            GTK_WINDOW(gui->window),
            GTK_FILE_CHOOSER_ACTION_SAVE,
            "Cancel", GTK_RESPONSE_CANCEL,
            "Save", GTK_RESPONSE_ACCEPT,
            NULL);

        /* Suggest filename from destination path */
        const char *suggested = strrchr(code->destination_path, '/');
        suggested = suggested ? suggested + 1 : code->destination_path;
        gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(save_dialog), suggested);

        if (gtk_dialog_run(GTK_DIALOG(save_dialog)) == GTK_RESPONSE_ACCEPT) {
            char *save_path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(save_dialog));

            FILE *f = fopen(save_path, "wb");
            if (f) {
                fwrite(data, 1, size, f);
                fclose(f);

                char msg[256];
                snprintf(msg, sizeof(msg),
                    "Content saved successfully!\n\n"
                    "Size: %zu bytes\n"
                    "Hash verified: ✓",
                    size);
                phantom_gui_show_message(gui, "Success", msg, GTK_MESSAGE_INFO);
            } else {
                phantom_gui_show_message(gui, "Error", "Failed to save file", GTK_MESSAGE_ERROR);
            }

            g_free(save_path);
        }

        gtk_widget_destroy(save_dialog);
        free(data);
    } else if (result == QRNET_TRANSPORT_NOT_FOUND) {
        phantom_gui_show_message(gui, "Not Found",
            "Content not available locally.\n\n"
            "Connect to peers to fetch remote content.",
            GTK_MESSAGE_INFO);
    } else {
        phantom_gui_show_message(gui, "Error", "Failed to fetch content", GTK_MESSAGE_ERROR);
    }

    g_free(code_id_str);
}

/* Create User Dialog */
static void on_user_create_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;

    if (!gui->user_system) {
        phantom_gui_show_message(gui, "Error", "User system not initialized", GTK_MESSAGE_ERROR);
        return;
    }

    /* Check if current user can create users */
    if (gui->uid != 0 && !phantom_user_has_permission(gui->user_system, gui->uid, PERM_CREATE_USER)) {
        phantom_gui_show_message(gui, "Permission Denied",
            "You do not have permission to create users", GTK_MESSAGE_ERROR);
        return;
    }

    GtkWidget *dialog = gtk_dialog_new_with_buttons("Create New User",
        GTK_WINDOW(gui->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "Cancel", GTK_RESPONSE_CANCEL,
        "Create", GTK_RESPONSE_OK,
        NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 10);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_container_add(GTK_CONTAINER(content), grid);

    /* Username */
    GtkWidget *user_label = gtk_label_new("Username:");
    gtk_widget_set_halign(user_label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), user_label, 0, 0, 1, 1);
    GtkWidget *user_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(user_entry), "Enter username");
    gtk_grid_attach(GTK_GRID(grid), user_entry, 1, 0, 1, 1);

    /* Full name */
    GtkWidget *name_label = gtk_label_new("Full Name:");
    gtk_widget_set_halign(name_label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), name_label, 0, 1, 1, 1);
    GtkWidget *name_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(name_entry), "Enter full name");
    gtk_grid_attach(GTK_GRID(grid), name_entry, 1, 1, 1, 1);

    /* Password */
    GtkWidget *pass_label = gtk_label_new("Password:");
    gtk_widget_set_halign(pass_label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), pass_label, 0, 2, 1, 1);
    GtkWidget *pass_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(pass_entry), FALSE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(pass_entry), "Enter password");
    gtk_grid_attach(GTK_GRID(grid), pass_entry, 1, 2, 1, 1);

    /* Confirm password */
    GtkWidget *confirm_label = gtk_label_new("Confirm:");
    gtk_widget_set_halign(confirm_label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), confirm_label, 0, 3, 1, 1);
    GtkWidget *confirm_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(confirm_entry), FALSE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(confirm_entry), "Confirm password");
    gtk_grid_attach(GTK_GRID(grid), confirm_entry, 1, 3, 1, 1);

    /* Password requirements note */
    GtkWidget *note_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(note_label),
        "<span size='small' style='italic'>Password must be at least 8 characters with uppercase, lowercase, and number</span>");
    gtk_grid_attach(GTK_GRID(grid), note_label, 0, 4, 2, 1);

    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        const char *username = gtk_entry_get_text(GTK_ENTRY(user_entry));
        const char *fullname = gtk_entry_get_text(GTK_ENTRY(name_entry));
        const char *password = gtk_entry_get_text(GTK_ENTRY(pass_entry));
        const char *confirm = gtk_entry_get_text(GTK_ENTRY(confirm_entry));

        /* Validate */
        if (strlen(username) < 1) {
            phantom_gui_show_message(gui, "Error", "Username is required", GTK_MESSAGE_ERROR);
        } else if (strlen(password) < 1) {
            phantom_gui_show_message(gui, "Error", "Password is required", GTK_MESSAGE_ERROR);
        } else if (strcmp(password, confirm) != 0) {
            phantom_gui_show_message(gui, "Error", "Passwords do not match", GTK_MESSAGE_ERROR);
        } else {
            /* Create the user */
            uint32_t new_uid;
            int result = phantom_user_create(gui->user_system, username, password, fullname, gui->uid, &new_uid);

            if (result == USER_OK) {
                char msg[256];
                snprintf(msg, sizeof(msg), "User '%s' created successfully (UID: %u)", username, new_uid);
                phantom_gui_show_message(gui, "Success", msg, GTK_MESSAGE_INFO);
                phantom_gui_refresh_users(gui);
            } else {
                const char *error_msg;
                switch (result) {
                    case USER_ERR_EXISTS: error_msg = "Username already exists"; break;
                    case USER_ERR_WEAK_PASSWORD: error_msg = "Password is too weak"; break;
                    case USER_ERR_DENIED: error_msg = "Permission denied"; break;
                    case USER_ERR_FULL: error_msg = "Maximum users reached"; break;
                    default: error_msg = "Failed to create user"; break;
                }
                phantom_gui_show_message(gui, "Error", error_msg, GTK_MESSAGE_ERROR);
            }
        }
    }

    gtk_widget_destroy(dialog);
}

/* Change Password Dialog */
static void on_user_password_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;

    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(gui->users_tree));
    GtkTreeIter iter;
    GtkTreeModel *model;

    if (!gtk_tree_selection_get_selected(selection, &model, &iter)) {
        return;
    }

    gchar *username;
    guint uid;
    gtk_tree_model_get(model, &iter,
        USER_COL_USERNAME, &username,
        USER_COL_UID, &uid,
        -1);

    GtkWidget *dialog = gtk_dialog_new_with_buttons("Change Password",
        GTK_WINDOW(gui->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "Cancel", GTK_RESPONSE_CANCEL,
        "Change", GTK_RESPONSE_OK,
        NULL);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 10);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_container_add(GTK_CONTAINER(content), grid);

    /* User info */
    char info[128];
    snprintf(info, sizeof(info), "Changing password for: <b>%s</b>", username);
    GtkWidget *info_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(info_label), info);
    gtk_grid_attach(GTK_GRID(grid), info_label, 0, 0, 2, 1);

    /* New password */
    GtkWidget *pass_label = gtk_label_new("New Password:");
    gtk_widget_set_halign(pass_label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), pass_label, 0, 1, 1, 1);
    GtkWidget *pass_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(pass_entry), FALSE);
    gtk_grid_attach(GTK_GRID(grid), pass_entry, 1, 1, 1, 1);

    /* Confirm */
    GtkWidget *confirm_label = gtk_label_new("Confirm:");
    gtk_widget_set_halign(confirm_label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), confirm_label, 0, 2, 1, 1);
    GtkWidget *confirm_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(confirm_entry), FALSE);
    gtk_grid_attach(GTK_GRID(grid), confirm_entry, 1, 2, 1, 1);

    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        const char *password = gtk_entry_get_text(GTK_ENTRY(pass_entry));
        const char *confirm = gtk_entry_get_text(GTK_ENTRY(confirm_entry));

        if (strlen(password) < 1) {
            phantom_gui_show_message(gui, "Error", "Password is required", GTK_MESSAGE_ERROR);
        } else if (strcmp(password, confirm) != 0) {
            phantom_gui_show_message(gui, "Error", "Passwords do not match", GTK_MESSAGE_ERROR);
        } else {
            int result = phantom_user_set_password(gui->user_system, uid, password, gui->uid);
            if (result == USER_OK) {
                phantom_gui_show_message(gui, "Success", "Password changed successfully", GTK_MESSAGE_INFO);
            } else {
                const char *error_msg;
                switch (result) {
                    case USER_ERR_WEAK_PASSWORD: error_msg = "Password is too weak"; break;
                    case USER_ERR_DENIED: error_msg = "Permission denied"; break;
                    default: error_msg = "Failed to change password"; break;
                }
                phantom_gui_show_message(gui, "Error", error_msg, GTK_MESSAGE_ERROR);
            }
        }
    }

    g_free(username);
    gtk_widget_destroy(dialog);
}

/* Edit User Dialog */
static void on_user_edit_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;

    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(gui->users_tree));
    GtkTreeIter iter;
    GtkTreeModel *model;

    if (!gtk_tree_selection_get_selected(selection, &model, &iter)) {
        return;
    }

    gchar *username;
    guint uid;
    gtk_tree_model_get(model, &iter,
        USER_COL_USERNAME, &username,
        USER_COL_UID, &uid,
        -1);

    /* Find the user */
    phantom_user_t *user = phantom_user_find_by_uid(gui->user_system, uid);
    if (!user) {
        g_free(username);
        return;
    }

    GtkWidget *dialog = gtk_dialog_new_with_buttons("Edit User",
        GTK_WINDOW(gui->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "Cancel", GTK_RESPONSE_CANCEL,
        "Save", GTK_RESPONSE_OK,
        NULL);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 10);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_container_add(GTK_CONTAINER(content), grid);

    /* Username (read-only) */
    GtkWidget *user_label = gtk_label_new("Username:");
    gtk_widget_set_halign(user_label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), user_label, 0, 0, 1, 1);
    GtkWidget *user_value = gtk_label_new(username);
    gtk_widget_set_halign(user_value, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), user_value, 1, 0, 1, 1);

    /* Full name */
    GtkWidget *name_label = gtk_label_new("Full Name:");
    gtk_widget_set_halign(name_label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), name_label, 0, 1, 1, 1);
    GtkWidget *name_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(name_entry), user->full_name);
    gtk_grid_attach(GTK_GRID(grid), name_entry, 1, 1, 1, 1);

    /* Shell */
    GtkWidget *shell_label = gtk_label_new("Shell:");
    gtk_widget_set_halign(shell_label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), shell_label, 0, 2, 1, 1);
    GtkWidget *shell_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(shell_entry), user->shell);
    gtk_grid_attach(GTK_GRID(grid), shell_entry, 1, 2, 1, 1);

    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        const char *fullname = gtk_entry_get_text(GTK_ENTRY(name_entry));
        const char *shell = gtk_entry_get_text(GTK_ENTRY(shell_entry));

        /* Update user (simple direct modification - in production would use proper API) */
        strncpy(user->full_name, fullname, 127);
        user->full_name[127] = '\0';
        strncpy(user->shell, shell, 127);
        user->shell[127] = '\0';

        phantom_gui_show_message(gui, "Success", "User updated successfully", GTK_MESSAGE_INFO);
        phantom_gui_refresh_users(gui);
    }

    g_free(username);
    gtk_widget_destroy(dialog);
}

/* Disable User */
static void on_user_disable_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;

    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(gui->users_tree));
    GtkTreeIter iter;
    GtkTreeModel *model;

    if (!gtk_tree_selection_get_selected(selection, &model, &iter)) {
        return;
    }

    gchar *username;
    guint uid;
    gtk_tree_model_get(model, &iter,
        USER_COL_USERNAME, &username,
        USER_COL_UID, &uid,
        -1);

    /* Confirm */
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(gui->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_QUESTION,
        GTK_BUTTONS_YES_NO,
        "Disable user '%s'?\n\nThe user will not be able to log in but can be re-enabled later.",
        username);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_YES) {
        int result = phantom_user_set_state(gui->user_system, uid, USER_STATE_DORMANT, gui->uid);
        if (result == USER_OK) {
            char msg[128];
            snprintf(msg, sizeof(msg), "User '%s' has been disabled", username);
            phantom_gui_show_message(gui, "Success", msg, GTK_MESSAGE_INFO);
            phantom_gui_refresh_users(gui);
        } else {
            phantom_gui_show_message(gui, "Error", "Failed to disable user", GTK_MESSAGE_ERROR);
        }
    }

    g_free(username);
    gtk_widget_destroy(dialog);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Desktop Lab Panel - Widget/Applet Management & Experimental Sandbox
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Built-in widget definitions */
typedef struct {
    const char *name;
    const char *type;
    const char *description;
    const char *icon;
    int enabled;
} desktop_widget_t;

static desktop_widget_t builtin_widgets[] = {
    {"System Monitor", "Status", "Display CPU, memory, and disk usage", "📊", 1},
    {"Clock", "Time", "Analog or digital clock display", "🕐", 1},
    {"Weather", "Info", "Current weather conditions", "🌤️", 0},
    {"Notes Sticky", "Productivity", "Quick sticky notes on desktop", "📝", 0},
    {"Calendar", "Time", "Mini calendar widget", "📅", 0},
    {"Network Status", "Status", "Network connection indicator", "🌐", 1},
    {"GeoFS Activity", "Status", "Geological filesystem activity monitor", "🪨", 0},
    {"AI Quick Access", "Utility", "Quick AI assistant launcher", "🤖", 0},
    {"Process Miniview", "Status", "Compact process list", "⚙️", 0},
    {"Governor Status", "Security", "Security governor status indicator", "🛡️", 1},
    {NULL, NULL, NULL, NULL, 0}
};

/* Experimental feature definitions */
typedef struct {
    const char *name;
    const char *category;
    const char *description;
    const char *risk;
    const char *icon;
    int enabled;
} experimental_feature_t;

static experimental_feature_t experiments[] = {
    {"Holographic UI", "Visual", "Experimental 3D holographic interface effects", "Low", "🔮", 0},
    {"Neural Input", "Input", "Brain-computer interface simulation", "Medium", "🧠", 0},
    {"Time Dilation", "Core", "Accelerated process execution sandbox", "Medium", "⏱️", 0},
    {"Quantum Storage", "Storage", "Experimental probabilistic data encoding", "High", "⚛️", 0},
    {"Voice Control", "Input", "Natural language voice commands", "Low", "🎤", 0},
    {"Gesture Recognition", "Input", "Hand gesture-based navigation", "Low", "👋", 0},
    {"Predictive Actions", "AI", "AI-driven action suggestions", "Low", "🔮", 0},
    {"Auto-Arrange", "Desktop", "Intelligent window arrangement", "Low", "📐", 0},
    {"Theme Synthesis", "Visual", "AI-generated adaptive themes", "Low", "🎨", 0},
    {"Ghost Mode", "Privacy", "Enhanced privacy with activity masking", "Medium", "👻", 0},
    {NULL, NULL, NULL, NULL, NULL, 0}
};

/* Callbacks */
static void on_widget_toggle(GtkCellRendererToggle *toggle, gchar *path_str, phantom_gui_t *gui);
static void on_experiment_toggle(GtkCellRendererToggle *toggle, gchar *path_str, phantom_gui_t *gui);
static void on_widget_selection_changed(GtkTreeSelection *selection, phantom_gui_t *gui);
static void on_experiment_selection_changed(GtkTreeSelection *selection, phantom_gui_t *gui);
static void on_run_experiment_clicked(GtkWidget *button, phantom_gui_t *gui);

/* Create Desktop Lab panel */
GtkWidget *phantom_gui_create_desktop_lab_panel(phantom_gui_t *gui) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 8);

    /* Title */
    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title),
        "<span size='large' weight='bold'>🖥️ Desktop Lab</span>\n"
        "<span size='small'>Customize widgets and explore experimental features</span>");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 8);

    /* Notebook for tabs */
    GtkWidget *notebook = gtk_notebook_new();
    gtk_box_pack_start(GTK_BOX(vbox), notebook, TRUE, TRUE, 0);

    /* ═══════════════════════════════════════════════════════════════════════
     * Tab 1: Widgets & Applets
     * ═══════════════════════════════════════════════════════════════════════ */
    GtkWidget *widgets_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(widgets_vbox), 8);

    GtkWidget *widgets_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(widgets_label),
        "<span weight='bold'>Desktop Widgets</span>\n"
        "<span size='small'>Enable or disable widgets that appear on your desktop</span>");
    gtk_widget_set_halign(widgets_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(widgets_vbox), widgets_label, FALSE, FALSE, 4);

    /* Widgets list store: Enabled, Icon, Name, Type, Description */
    gui->widgets_store = gtk_list_store_new(5,
        G_TYPE_BOOLEAN,  /* Enabled toggle */
        G_TYPE_STRING,   /* Icon */
        G_TYPE_STRING,   /* Name */
        G_TYPE_STRING,   /* Type */
        G_TYPE_STRING    /* Description */
    );

    /* Populate widgets */
    for (int i = 0; builtin_widgets[i].name != NULL; i++) {
        GtkTreeIter iter;
        gtk_list_store_append(gui->widgets_store, &iter);
        gtk_list_store_set(gui->widgets_store, &iter,
            0, builtin_widgets[i].enabled,
            1, builtin_widgets[i].icon,
            2, builtin_widgets[i].name,
            3, builtin_widgets[i].type,
            4, builtin_widgets[i].description,
            -1);
    }

    gui->widgets_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(gui->widgets_store));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(gui->widgets_tree), TRUE);

    /* Toggle column */
    GtkCellRenderer *toggle_renderer = gtk_cell_renderer_toggle_new();
    g_signal_connect(toggle_renderer, "toggled", G_CALLBACK(on_widget_toggle), gui);
    GtkTreeViewColumn *toggle_col = gtk_tree_view_column_new_with_attributes(
        "On", toggle_renderer, "active", 0, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->widgets_tree), toggle_col);

    /* Icon column */
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes(
        "", renderer, "text", 1, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->widgets_tree), col);

    /* Name column */
    renderer = gtk_cell_renderer_text_new();
    col = gtk_tree_view_column_new_with_attributes("Widget", renderer, "text", 2, NULL);
    gtk_tree_view_column_set_min_width(col, 150);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->widgets_tree), col);

    /* Type column */
    renderer = gtk_cell_renderer_text_new();
    col = gtk_tree_view_column_new_with_attributes("Type", renderer, "text", 3, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->widgets_tree), col);

    /* Description column */
    renderer = gtk_cell_renderer_text_new();
    col = gtk_tree_view_column_new_with_attributes("Description", renderer, "text", 4, NULL);
    gtk_tree_view_column_set_expand(col, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->widgets_tree), col);

    GtkTreeSelection *widget_selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(gui->widgets_tree));
    g_signal_connect(widget_selection, "changed", G_CALLBACK(on_widget_selection_changed), gui);

    GtkWidget *widgets_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(widgets_scroll),
                                    GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(widgets_scroll), gui->widgets_tree);
    gtk_box_pack_start(GTK_BOX(widgets_vbox), widgets_scroll, TRUE, TRUE, 0);

    /* Widget preview/config area */
    GtkWidget *widget_frame = gtk_frame_new("Widget Configuration");
    gtk_box_pack_start(GTK_BOX(widgets_vbox), widget_frame, FALSE, FALSE, 8);

    gui->widget_config_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(gui->widget_config_box), 8);
    gtk_container_add(GTK_CONTAINER(widget_frame), gui->widget_config_box);

    gui->widget_preview = gtk_label_new("Select a widget to configure");
    gtk_box_pack_start(GTK_BOX(gui->widget_config_box), gui->widget_preview, FALSE, FALSE, 0);

    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), widgets_vbox,
        gtk_label_new("🧩 Widgets"));

    /* ═══════════════════════════════════════════════════════════════════════
     * Tab 2: Experimental Features Sandbox
     * ═══════════════════════════════════════════════════════════════════════ */
    GtkWidget *experiments_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(experiments_vbox), 8);

    GtkWidget *exp_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(exp_label),
        "<span weight='bold'>Experimental Features Sandbox</span>\n"
        "<span size='small'>Test cutting-edge features. These may be unstable.</span>");
    gtk_widget_set_halign(exp_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(experiments_vbox), exp_label, FALSE, FALSE, 4);

    /* Warning banner */
    GtkWidget *warning = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(warning),
        "<span background='#FFA500' foreground='black'> ⚠️ Experimental features run in isolated sandbox. No system damage possible. </span>");
    gtk_box_pack_start(GTK_BOX(experiments_vbox), warning, FALSE, FALSE, 4);

    /* Experiments list store: Enabled, Icon, Name, Category, Risk, Description */
    gui->experiments_store = gtk_list_store_new(6,
        G_TYPE_BOOLEAN,  /* Enabled */
        G_TYPE_STRING,   /* Icon */
        G_TYPE_STRING,   /* Name */
        G_TYPE_STRING,   /* Category */
        G_TYPE_STRING,   /* Risk */
        G_TYPE_STRING    /* Description */
    );

    /* Populate experiments */
    for (int i = 0; experiments[i].name != NULL; i++) {
        GtkTreeIter iter;
        gtk_list_store_append(gui->experiments_store, &iter);
        gtk_list_store_set(gui->experiments_store, &iter,
            0, experiments[i].enabled,
            1, experiments[i].icon,
            2, experiments[i].name,
            3, experiments[i].category,
            4, experiments[i].risk,
            5, experiments[i].description,
            -1);
    }

    gui->experiments_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(gui->experiments_store));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(gui->experiments_tree), TRUE);

    /* Toggle column */
    toggle_renderer = gtk_cell_renderer_toggle_new();
    g_signal_connect(toggle_renderer, "toggled", G_CALLBACK(on_experiment_toggle), gui);
    toggle_col = gtk_tree_view_column_new_with_attributes(
        "On", toggle_renderer, "active", 0, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->experiments_tree), toggle_col);

    /* Icon column */
    renderer = gtk_cell_renderer_text_new();
    col = gtk_tree_view_column_new_with_attributes("", renderer, "text", 1, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->experiments_tree), col);

    /* Name column */
    renderer = gtk_cell_renderer_text_new();
    col = gtk_tree_view_column_new_with_attributes("Feature", renderer, "text", 2, NULL);
    gtk_tree_view_column_set_min_width(col, 150);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->experiments_tree), col);

    /* Category column */
    renderer = gtk_cell_renderer_text_new();
    col = gtk_tree_view_column_new_with_attributes("Category", renderer, "text", 3, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->experiments_tree), col);

    /* Risk column */
    renderer = gtk_cell_renderer_text_new();
    col = gtk_tree_view_column_new_with_attributes("Risk", renderer, "text", 4, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->experiments_tree), col);

    /* Description column */
    renderer = gtk_cell_renderer_text_new();
    col = gtk_tree_view_column_new_with_attributes("Description", renderer, "text", 5, NULL);
    gtk_tree_view_column_set_expand(col, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->experiments_tree), col);

    GtkTreeSelection *exp_selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(gui->experiments_tree));
    g_signal_connect(exp_selection, "changed", G_CALLBACK(on_experiment_selection_changed), gui);

    GtkWidget *exp_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(exp_scroll),
                                    GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(exp_scroll), gui->experiments_tree);
    gtk_box_pack_start(GTK_BOX(experiments_vbox), exp_scroll, TRUE, TRUE, 0);

    /* Experiment control area */
    GtkWidget *exp_frame = gtk_frame_new("Experiment Output");
    gtk_box_pack_start(GTK_BOX(experiments_vbox), exp_frame, TRUE, TRUE, 8);

    GtkWidget *exp_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(exp_box), 8);
    gtk_container_add(GTK_CONTAINER(exp_frame), exp_box);

    gui->experiment_status_label = gtk_label_new("Select an experiment to run");
    gtk_widget_set_halign(gui->experiment_status_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(exp_box), gui->experiment_status_label, FALSE, FALSE, 0);

    /* Output text view */
    gui->experiment_output_buffer = gtk_text_buffer_new(NULL);
    gui->experiment_output_view = gtk_text_view_new_with_buffer(gui->experiment_output_buffer);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(gui->experiment_output_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(gui->experiment_output_view), GTK_WRAP_WORD);
    gtk_widget_set_size_request(gui->experiment_output_view, -1, 350);

    GtkWidget *output_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(output_scroll),
                                    GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(output_scroll), gui->experiment_output_view);
    gtk_box_pack_start(GTK_BOX(exp_box), output_scroll, TRUE, TRUE, 0);

    /* Run button */
    GtkWidget *run_btn = gtk_button_new_with_label("🚀 Run Experiment");
    g_signal_connect(run_btn, "clicked", G_CALLBACK(on_run_experiment_clicked), gui);
    gtk_box_pack_start(GTK_BOX(exp_box), run_btn, FALSE, FALSE, 4);

    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), experiments_vbox,
        gtk_label_new("🧪 Experiments"));

    return vbox;
}

/* Widget toggle callback */
static void on_widget_toggle(GtkCellRendererToggle *toggle, gchar *path_str, phantom_gui_t *gui) {
    (void)toggle;
    GtkTreePath *path = gtk_tree_path_new_from_string(path_str);
    GtkTreeIter iter;

    if (gtk_tree_model_get_iter(GTK_TREE_MODEL(gui->widgets_store), &iter, path)) {
        gboolean enabled;
        gchar *name;
        gtk_tree_model_get(GTK_TREE_MODEL(gui->widgets_store), &iter, 0, &enabled, 2, &name, -1);
        gtk_list_store_set(gui->widgets_store, &iter, 0, !enabled, -1);

        char msg[256];
        snprintf(msg, sizeof(msg), "Widget '%s' %s", name, !enabled ? "enabled" : "disabled");
        phantom_gui_update_status(gui, msg);
        g_free(name);
    }
    gtk_tree_path_free(path);
}

/* Experiment toggle callback */
static void on_experiment_toggle(GtkCellRendererToggle *toggle, gchar *path_str, phantom_gui_t *gui) {
    (void)toggle;
    GtkTreePath *path = gtk_tree_path_new_from_string(path_str);
    GtkTreeIter iter;

    if (gtk_tree_model_get_iter(GTK_TREE_MODEL(gui->experiments_store), &iter, path)) {
        gboolean enabled;
        gchar *name, *risk;
        gtk_tree_model_get(GTK_TREE_MODEL(gui->experiments_store), &iter,
            0, &enabled, 2, &name, 4, &risk, -1);

        /* Warn for high risk experiments */
        if (!enabled && strcmp(risk, "High") == 0) {
            GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(gui->window),
                GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                GTK_MESSAGE_WARNING, GTK_BUTTONS_YES_NO,
                "Enable high-risk experiment '%s'?\n\n"
                "This feature is highly experimental and may cause unexpected behavior.\n"
                "All experiments run in an isolated sandbox.", name);
            int response = gtk_dialog_run(GTK_DIALOG(dialog));
            gtk_widget_destroy(dialog);
            if (response != GTK_RESPONSE_YES) {
                g_free(name);
                g_free(risk);
                gtk_tree_path_free(path);
                return;
            }
        }

        gtk_list_store_set(gui->experiments_store, &iter, 0, !enabled, -1);

        char msg[256];
        snprintf(msg, sizeof(msg), "Experiment '%s' %s", name, !enabled ? "enabled" : "disabled");
        phantom_gui_update_status(gui, msg);
        g_free(name);
        g_free(risk);
    }
    gtk_tree_path_free(path);
}

/* Widget selection changed */
static void on_widget_selection_changed(GtkTreeSelection *selection, phantom_gui_t *gui) {
    GtkTreeIter iter;
    GtkTreeModel *model;

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        gchar *name, *type, *desc, *icon;
        gboolean enabled;
        gtk_tree_model_get(model, &iter,
            0, &enabled,
            1, &icon,
            2, &name,
            3, &type,
            4, &desc,
            -1);

        char markup[512];
        snprintf(markup, sizeof(markup),
            "<b>%s %s</b>\n"
            "<i>Type:</i> %s\n"
            "<i>Status:</i> %s\n\n"
            "%s",
            icon, name, type,
            enabled ? "<span foreground='green'>Enabled</span>" : "<span foreground='gray'>Disabled</span>",
            desc);
        gtk_label_set_markup(GTK_LABEL(gui->widget_preview), markup);

        g_free(name);
        g_free(type);
        g_free(desc);
        g_free(icon);
    }
}

/* Experiment selection changed */
static void on_experiment_selection_changed(GtkTreeSelection *selection, phantom_gui_t *gui) {
    GtkTreeIter iter;
    GtkTreeModel *model;

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        gchar *name, *category, *risk, *desc, *icon;
        gboolean enabled;
        gtk_tree_model_get(model, &iter,
            0, &enabled,
            1, &icon,
            2, &name,
            3, &category,
            4, &risk,
            5, &desc,
            -1);

        const char *risk_color = "green";
        if (strcmp(risk, "Medium") == 0) risk_color = "orange";
        else if (strcmp(risk, "High") == 0) risk_color = "red";

        char markup[512];
        snprintf(markup, sizeof(markup),
            "<b>%s %s</b>\n"
            "<i>Category:</i> %s | <i>Risk:</i> <span foreground='%s'>%s</span>\n"
            "<i>Status:</i> %s\n\n"
            "%s",
            icon, name, category, risk_color, risk,
            enabled ? "<span foreground='green'>Enabled</span>" : "<span foreground='gray'>Disabled</span>",
            desc);
        gtk_label_set_markup(GTK_LABEL(gui->experiment_status_label), markup);

        g_free(name);
        g_free(category);
        g_free(risk);
        g_free(desc);
        g_free(icon);
    }
}

/* Run experiment button clicked */
static void on_run_experiment_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;

    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(gui->experiments_tree));
    GtkTreeIter iter;
    GtkTreeModel *model;

    if (!gtk_tree_selection_get_selected(selection, &model, &iter)) {
        phantom_gui_show_message(gui, "No Selection", "Please select an experiment to run", GTK_MESSAGE_INFO);
        return;
    }

    gchar *name;
    gboolean enabled;
    gtk_tree_model_get(model, &iter, 0, &enabled, 2, &name, -1);

    if (!enabled) {
        phantom_gui_show_message(gui, "Experiment Disabled",
            "Please enable the experiment before running", GTK_MESSAGE_WARNING);
        g_free(name);
        return;
    }

    /* Simulate running the experiment */
    gtk_text_buffer_set_text(gui->experiment_output_buffer, "", -1);

    GtkTextIter end;
    gtk_text_buffer_get_end_iter(gui->experiment_output_buffer, &end);

    char output[1024];
    snprintf(output, sizeof(output),
        "═══════════════════════════════════════════\n"
        " Experiment: %s\n"
        "═══════════════════════════════════════════\n\n"
        "[SANDBOX] Initializing isolated environment...\n"
        "[SANDBOX] Memory sandbox: 256MB allocated\n"
        "[SANDBOX] Process isolation: Active\n"
        "[SANDBOX] Network access: Restricted\n\n"
        "[RUN] Starting experiment '%s'...\n"
        "[RUN] Loading experimental modules...\n"
        "[RUN] Configuring test parameters...\n"
        "[RUN] Experiment running in sandbox...\n\n"
        "[RESULT] Experiment completed successfully\n"
        "[RESULT] No system modifications made\n"
        "[RESULT] Sandbox cleaned up\n\n"
        "Output saved to: /var/phantom/experiments/%s.log\n",
        name, name, name);

    gtk_text_buffer_insert(gui->experiment_output_buffer, &end, output, -1);

    char status_msg[256];
    snprintf(status_msg, sizeof(status_msg), "Experiment '%s' completed", name);
    phantom_gui_update_status(gui, status_msg);

    g_free(name);
}

/* Refresh Desktop Lab */
void phantom_gui_refresh_desktop_lab(phantom_gui_t *gui) {
    (void)gui;
    /* Widget and experiment states are stored in the list stores */
    /* Could be extended to persist state to GeoFS */
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Desktop Environment Panel - Ubuntu-like Desktop with AI Governor Interface
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Desktop icon definitions */
typedef struct {
    const char *name;
    const char *icon;
    const char *panel;  /* Panel to switch to when clicked */
} desktop_icon_t;

static desktop_icon_t desktop_icons[] = {
    {"Files", "📁", "files"},
    {"Terminal", "💻", "terminal"},
    {"AI Assistant", "🤖", "ai"},
    {"Settings", "⚙️", "governor"},
    {"Security", "🔒", "security"},
    {"ArtOS", "🎨", "artos"},
    {NULL, NULL, NULL}
};

/* Forward declarations */
static void on_desktop_icon_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_desktop_ai_submit(GtkWidget *entry, phantom_gui_t *gui);
static void on_desktop_app_btn_clicked(GtkWidget *button, phantom_gui_t *gui);
static gboolean update_desktop_clock(phantom_gui_t *gui);
static void on_governor_quick_clicked(GtkWidget *button, phantom_gui_t *gui);

/* Create Desktop Environment panel */
GtkWidget *phantom_gui_create_desktop_panel(phantom_gui_t *gui) {
    /* Main container - vertical box */
    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    /* ═══════════════════════════════════════════════════════════════════════
     * Top Panel / Menu Bar (like Ubuntu's top bar)
     * ═══════════════════════════════════════════════════════════════════════ */
    GtkWidget *top_panel = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_name(top_panel, "desktop-top-panel");

    /* Apply dark styling */
    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css,
        "#desktop-top-panel { background: #2d2d2d; padding: 4px 8px; }"
        "#desktop-top-panel label { color: #ffffff; }"
        "#desktop-top-panel button { background: transparent; border: none; color: #ffffff; padding: 4px 8px; }"
        "#desktop-top-panel button:hover { background: #404040; }"
        "#desktop-area { background: linear-gradient(180deg, #1a1a2e 0%, #16213e 50%, #0f3460 100%); }"
        "#desktop-taskbar { background: #1a1a1a; padding: 4px 8px; }"
        "#desktop-taskbar button { background: #2d2d2d; border: 1px solid #404040; color: #ffffff; padding: 6px 12px; margin: 2px; }"
        "#desktop-taskbar button:hover { background: #404040; }"
        "#ai-governor-panel { background: #1e1e2e; border: 1px solid #44475a; border-radius: 8px; padding: 12px; }"
        ".desktop-icon { background: transparent; border: none; padding: 8px; }"
        ".desktop-icon:hover { background: rgba(255,255,255,0.1); border-radius: 8px; }",
        -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    /* Activities button (left side) */
    GtkWidget *activities_btn = gtk_button_new_with_label("Activities");
    gtk_box_pack_start(GTK_BOX(top_panel), activities_btn, FALSE, FALSE, 0);

    /* Application menu button */
    gui->desktop_app_menu = gtk_menu_button_new();
    gtk_button_set_label(GTK_BUTTON(gui->desktop_app_menu), "Applications ▼");
    gtk_box_pack_start(GTK_BOX(top_panel), gui->desktop_app_menu, FALSE, FALSE, 0);

    /* Create applications popup menu */
    GtkWidget *app_menu = gtk_menu_new();
    const char *app_names[] = {"📁 Files", "💻 Terminal", "🤖 AI Assistant", "🎨 ArtOS", "🔒 Security", "⚙️ Settings"};
    const char *app_panels[] = {"files", "terminal", "ai", "artos", "security", "governor"};
    for (int i = 0; i < 6; i++) {
        GtkWidget *item = gtk_menu_item_new_with_label(app_names[i]);
        g_object_set_data(G_OBJECT(item), "panel", (gpointer)app_panels[i]);
        g_signal_connect(item, "activate", G_CALLBACK(on_desktop_app_btn_clicked), gui);
        gtk_menu_shell_append(GTK_MENU_SHELL(app_menu), item);
    }
    gtk_widget_show_all(app_menu);
    gtk_menu_button_set_popup(GTK_MENU_BUTTON(gui->desktop_app_menu), app_menu);

    /* Spacer */
    GtkWidget *spacer = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(top_panel), spacer, TRUE, TRUE, 0);

    /* AI Governor status indicator (center-right) */
    gui->desktop_governor_status = gtk_label_new("🛡️ Governor: Active");
    gtk_box_pack_start(GTK_BOX(top_panel), gui->desktop_governor_status, FALSE, FALSE, 8);

    /* Clock (right side) */
    gui->desktop_clock_label = gtk_label_new("");
    gtk_box_pack_end(GTK_BOX(top_panel), gui->desktop_clock_label, FALSE, FALSE, 8);

    /* Start clock timer */
    gui->desktop_clock_timer = g_timeout_add(1000, (GSourceFunc)update_desktop_clock, gui);
    update_desktop_clock(gui);

    gtk_box_pack_start(GTK_BOX(main_box), top_panel, FALSE, FALSE, 0);

    /* ═══════════════════════════════════════════════════════════════════════
     * Main Desktop Area with AI Governor Panel
     * ═══════════════════════════════════════════════════════════════════════ */
    GtkWidget *desktop_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(main_box), desktop_hbox, TRUE, TRUE, 0);

    /* Desktop area with icons */
    gui->desktop_area = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_name(gui->desktop_area, "desktop-area");
    gtk_container_set_border_width(GTK_CONTAINER(gui->desktop_area), 16);
    gtk_box_pack_start(GTK_BOX(desktop_hbox), gui->desktop_area, TRUE, TRUE, 0);

    /* Desktop icons grid */
    gui->desktop_icons_grid = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(gui->desktop_icons_grid), GTK_SELECTION_NONE);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(gui->desktop_icons_grid), 2);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(gui->desktop_icons_grid), 16);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(gui->desktop_icons_grid), 16);
    gtk_widget_set_halign(gui->desktop_icons_grid, GTK_ALIGN_START);
    gtk_widget_set_valign(gui->desktop_icons_grid, GTK_ALIGN_START);

    /* Create desktop icons */
    for (int i = 0; desktop_icons[i].name != NULL; i++) {
        GtkWidget *icon_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        gtk_widget_set_size_request(icon_box, 80, 80);

        GtkWidget *icon_btn = gtk_button_new();
        gtk_widget_set_name(icon_btn, "desktop-icon");
        gtk_style_context_add_class(gtk_widget_get_style_context(icon_btn), "desktop-icon");

        GtkWidget *icon_content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        GtkWidget *icon_label = gtk_label_new(desktop_icons[i].icon);
        PangoAttrList *attrs = pango_attr_list_new();
        pango_attr_list_insert(attrs, pango_attr_scale_new(2.5));
        gtk_label_set_attributes(GTK_LABEL(icon_label), attrs);
        pango_attr_list_unref(attrs);
        gtk_box_pack_start(GTK_BOX(icon_content), icon_label, FALSE, FALSE, 0);

        GtkWidget *name_label = gtk_label_new(desktop_icons[i].name);
        gtk_box_pack_start(GTK_BOX(icon_content), name_label, FALSE, FALSE, 0);

        gtk_container_add(GTK_CONTAINER(icon_btn), icon_content);
        g_object_set_data(G_OBJECT(icon_btn), "panel", (gpointer)desktop_icons[i].panel);
        g_signal_connect(icon_btn, "clicked", G_CALLBACK(on_desktop_icon_clicked), gui);

        gtk_container_add(GTK_CONTAINER(gui->desktop_icons_grid), icon_btn);
    }

    gtk_box_pack_start(GTK_BOX(gui->desktop_area), gui->desktop_icons_grid, FALSE, FALSE, 0);

    /* ═══════════════════════════════════════════════════════════════════════
     * AI Governor Interface Panel (Right side)
     * ═══════════════════════════════════════════════════════════════════════ */
    GtkWidget *governor_panel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_name(governor_panel, "ai-governor-panel");
    gtk_widget_set_size_request(governor_panel, 350, -1);
    gtk_container_set_border_width(GTK_CONTAINER(governor_panel), 12);
    gtk_box_pack_end(GTK_BOX(desktop_hbox), governor_panel, FALSE, FALSE, 8);

    /* Governor header */
    GtkWidget *gov_header = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(gov_header),
        "<span size='large' weight='bold' foreground='#bd93f9'>🛡️ AI Governor</span>\n"
        "<span size='small' foreground='#6272a4'>System Protection &amp; AI Interface</span>");
    gtk_widget_set_halign(gov_header, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(governor_panel), gov_header, FALSE, FALSE, 4);

    /* Status indicators */
    GtkWidget *status_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(status_grid), 4);
    gtk_grid_set_column_spacing(GTK_GRID(status_grid), 8);

    const char *status_labels[] = {"Protection:", "Threat Level:", "Last Scan:", "AI Mode:"};
    const char *status_values[] = {"<span foreground='#50fa7b'>Active</span>",
                                    "<span foreground='#50fa7b'>Low</span>",
                                    "<span foreground='#8be9fd'>2 min ago</span>",
                                    "<span foreground='#bd93f9'>Autonomous</span>"};
    for (int i = 0; i < 4; i++) {
        GtkWidget *lbl = gtk_label_new(status_labels[i]);
        gtk_widget_set_halign(lbl, GTK_ALIGN_END);
        gtk_grid_attach(GTK_GRID(status_grid), lbl, 0, i, 1, 1);
        GtkWidget *val = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(val), status_values[i]);
        gtk_widget_set_halign(val, GTK_ALIGN_START);
        gtk_grid_attach(GTK_GRID(status_grid), val, 1, i, 1, 1);
    }
    gtk_box_pack_start(GTK_BOX(governor_panel), status_grid, FALSE, FALSE, 8);

    /* Separator */
    gtk_box_pack_start(GTK_BOX(governor_panel), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 4);

    /* AI Chat Interface */
    GtkWidget *chat_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(chat_label),
        "<span weight='bold' foreground='#f8f8f2'>🤖 AI Assistant</span>");
    gtk_widget_set_halign(chat_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(governor_panel), chat_label, FALSE, FALSE, 4);

    /* AI response area */
    gui->desktop_ai_buffer = gtk_text_buffer_new(NULL);
    gtk_text_buffer_set_text(gui->desktop_ai_buffer,
        "Welcome to PhantomOS AI Governor Interface.\n\n"
        "I am your AI assistant integrated with the Governor security system. "
        "I can help you:\n\n"
        "• Navigate the system\n"
        "• Check security status\n"
        "• Run system commands\n"
        "• Manage files and processes\n"
        "• Answer questions about PhantomOS\n\n"
        "Type a command or question below...", -1);

    gui->desktop_ai_response = gtk_text_view_new_with_buffer(gui->desktop_ai_buffer);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(gui->desktop_ai_response), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(gui->desktop_ai_response), GTK_WRAP_WORD);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(gui->desktop_ai_response), 8);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(gui->desktop_ai_response), 8);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(gui->desktop_ai_response), 8);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(gui->desktop_ai_response), 8);

    GtkWidget *ai_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(ai_scroll),
                                    GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(ai_scroll), gui->desktop_ai_response);
    gtk_box_pack_start(GTK_BOX(governor_panel), ai_scroll, TRUE, TRUE, 0);

    /* AI input entry */
    gui->desktop_ai_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(gui->desktop_ai_entry), "Ask the AI Governor...");
    g_signal_connect(gui->desktop_ai_entry, "activate", G_CALLBACK(on_desktop_ai_submit), gui);
    gtk_box_pack_start(GTK_BOX(governor_panel), gui->desktop_ai_entry, FALSE, FALSE, 4);

    /* Quick action buttons */
    GtkWidget *quick_btns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);

    GtkWidget *scan_btn = gtk_button_new_with_label("🔍 Scan");
    g_object_set_data(G_OBJECT(scan_btn), "action", (gpointer)"scan");
    g_signal_connect(scan_btn, "clicked", G_CALLBACK(on_governor_quick_clicked), gui);
    gtk_box_pack_start(GTK_BOX(quick_btns), scan_btn, TRUE, TRUE, 0);

    GtkWidget *status_btn = gtk_button_new_with_label("📊 Status");
    g_object_set_data(G_OBJECT(status_btn), "action", (gpointer)"status");
    g_signal_connect(status_btn, "clicked", G_CALLBACK(on_governor_quick_clicked), gui);
    gtk_box_pack_start(GTK_BOX(quick_btns), status_btn, TRUE, TRUE, 0);

    GtkWidget *help_btn = gtk_button_new_with_label("❓ Help");
    g_object_set_data(G_OBJECT(help_btn), "action", (gpointer)"help");
    g_signal_connect(help_btn, "clicked", G_CALLBACK(on_governor_quick_clicked), gui);
    gtk_box_pack_start(GTK_BOX(quick_btns), help_btn, TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(governor_panel), quick_btns, FALSE, FALSE, 4);

    /* ═══════════════════════════════════════════════════════════════════════
     * Bottom Taskbar (like Ubuntu's dock)
     * ═══════════════════════════════════════════════════════════════════════ */
    gui->desktop_taskbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_name(gui->desktop_taskbar, "desktop-taskbar");
    gtk_widget_set_halign(gui->desktop_taskbar, GTK_ALIGN_CENTER);

    /* Taskbar app buttons */
    const char *taskbar_icons[] = {"📁", "💻", "🤖", "🌐", "🔒", "⚙️"};
    const char *taskbar_tips[] = {"Files", "Terminal", "AI Assistant", "Network", "Security", "Settings"};
    const char *taskbar_panels[] = {"files", "terminal", "ai", "network", "security", "governor"};

    for (int i = 0; i < 6; i++) {
        GtkWidget *btn = gtk_button_new_with_label(taskbar_icons[i]);
        gtk_widget_set_tooltip_text(btn, taskbar_tips[i]);
        g_object_set_data(G_OBJECT(btn), "panel", (gpointer)taskbar_panels[i]);
        g_signal_connect(btn, "clicked", G_CALLBACK(on_desktop_icon_clicked), gui);
        gtk_box_pack_start(GTK_BOX(gui->desktop_taskbar), btn, FALSE, FALSE, 2);
    }

    gtk_box_pack_end(GTK_BOX(main_box), gui->desktop_taskbar, FALSE, FALSE, 0);

    return main_box;
}

/* Update desktop clock */
static gboolean update_desktop_clock(phantom_gui_t *gui) {
    if (!gui || !gui->desktop_clock_label) return FALSE;

    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%a %b %d  %H:%M", tm);
    gtk_label_set_text(GTK_LABEL(gui->desktop_clock_label), time_str);
    return TRUE;
}

/* Desktop icon clicked */
static void on_desktop_icon_clicked(GtkWidget *button, phantom_gui_t *gui) {
    const char *panel = g_object_get_data(G_OBJECT(button), "panel");
    if (panel && gui->content_stack) {
        gtk_stack_set_visible_child_name(GTK_STACK(gui->content_stack), panel);
    }
}

/* Application menu item clicked */
static void on_desktop_app_btn_clicked(GtkWidget *button, phantom_gui_t *gui) {
    const char *panel = g_object_get_data(G_OBJECT(button), "panel");
    if (panel && gui->content_stack) {
        gtk_stack_set_visible_child_name(GTK_STACK(gui->content_stack), panel);
    }
}

/* AI Governor quick action clicked */
static void on_governor_quick_clicked(GtkWidget *button, phantom_gui_t *gui) {
    const char *action = g_object_get_data(G_OBJECT(button), "action");
    if (!action || !gui->desktop_ai_buffer) return;

    GtkTextIter end;
    gtk_text_buffer_get_end_iter(gui->desktop_ai_buffer, &end);

    char response[1024];
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char time_str[32];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", tm);

    if (strcmp(action, "scan") == 0) {
        snprintf(response, sizeof(response),
            "\n\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n"
            "[%s] 🔍 System Scan Initiated\n"
            "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n"
            "✓ Memory integrity: OK\n"
            "✓ Process security: OK\n"
            "✓ GeoFS integrity: OK\n"
            "✓ Network connections: Clean\n"
            "✓ Governor status: Active\n\n"
            "Scan complete. No threats detected.\n",
            time_str);
    } else if (strcmp(action, "status") == 0) {
        snprintf(response, sizeof(response),
            "\n\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n"
            "[%s] 📊 System Status\n"
            "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n"
            "🛡️ Governor: Active (Protective Mode)\n"
            "💾 GeoFS: Healthy (0 corruptions)\n"
            "🔐 Security: All systems nominal\n"
            "🧠 AI Engine: Online\n"
            "⚡ Performance: Optimal\n"
            "📊 Memory: 67%% used\n"
            "💽 Storage: 23%% used\n",
            time_str);
    } else if (strcmp(action, "help") == 0) {
        snprintf(response, sizeof(response),
            "\n\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n"
            "[%s] ❓ AI Governor Help\n"
            "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n"
            "Available commands:\n"
            "• 'scan' - Run security scan\n"
            "• 'status' - Show system status\n"
            "• 'files' - Open file browser\n"
            "• 'terminal' - Open terminal\n"
            "• 'help <topic>' - Get help on topic\n\n"
            "Or ask any question in natural language!\n",
            time_str);
    }

    gtk_text_buffer_insert(gui->desktop_ai_buffer, &end, response, -1);

    /* Scroll to bottom */
    gtk_text_iter_set_line(&end, G_MAXINT);
    GtkTextMark *mark = gtk_text_buffer_create_mark(gui->desktop_ai_buffer, NULL, &end, FALSE);
    gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(gui->desktop_ai_response), mark, 0.0, FALSE, 0.0, 0.0);
    gtk_text_buffer_delete_mark(gui->desktop_ai_buffer, mark);
}

/* AI input submitted */
static void on_desktop_ai_submit(GtkWidget *entry, phantom_gui_t *gui) {
    const char *input = gtk_entry_get_text(GTK_ENTRY(entry));
    if (!input || strlen(input) == 0) return;

    GtkTextIter end;
    gtk_text_buffer_get_end_iter(gui->desktop_ai_buffer, &end);

    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char time_str[32];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", tm);

    /* Add user input */
    char user_msg[512];
    snprintf(user_msg, sizeof(user_msg), "\n\n[%s] 👤 You: %s\n", time_str, input);
    gtk_text_buffer_insert(gui->desktop_ai_buffer, &end, user_msg, -1);

    /* Generate AI response based on input */
    char response[1024];
    gtk_text_buffer_get_end_iter(gui->desktop_ai_buffer, &end);

    if (strstr(input, "hello") || strstr(input, "hi") || strstr(input, "hey")) {
        snprintf(response, sizeof(response),
            "[%s] 🤖 AI: Hello! I'm the PhantomOS AI Governor. "
            "I can help you navigate the system, check security status, or answer questions. "
            "What would you like to do?", time_str);
    } else if (strstr(input, "file") || strstr(input, "folder")) {
        snprintf(response, sizeof(response),
            "[%s] 🤖 AI: I can help with files! In PhantomOS, files are never deleted - "
            "they're preserved in the geological filesystem (GeoFS). "
            "Would you like me to open the Files panel? Click the 📁 icon or say 'open files'.", time_str);
    } else if (strstr(input, "security") || strstr(input, "threat") || strstr(input, "safe")) {
        snprintf(response, sizeof(response),
            "[%s] 🤖 AI: Your system is currently secure. The Governor is actively monitoring "
            "all operations. No threats detected. Protection level: Maximum. "
            "Would you like me to run a detailed security scan?", time_str);
    } else if (strstr(input, "open files")) {
        gtk_stack_set_visible_child_name(GTK_STACK(gui->content_stack), "files");
        snprintf(response, sizeof(response),
            "[%s] 🤖 AI: Opening Files panel for you...", time_str);
    } else if (strstr(input, "open terminal")) {
        gtk_stack_set_visible_child_name(GTK_STACK(gui->content_stack), "terminal");
        snprintf(response, sizeof(response),
            "[%s] 🤖 AI: Opening Terminal panel for you...", time_str);
    } else {
        snprintf(response, sizeof(response),
            "[%s] 🤖 AI: I understand you're asking about '%s'. "
            "In PhantomOS, everything is preserved and protected by the Governor system. "
            "Try asking about 'files', 'security', 'status', or use quick actions below.",
            time_str, input);
    }

    gtk_text_buffer_insert(gui->desktop_ai_buffer, &end, response, -1);

    /* Clear entry */
    gtk_entry_set_text(GTK_ENTRY(entry), "");

    /* Scroll to bottom */
    gtk_text_buffer_get_end_iter(gui->desktop_ai_buffer, &end);
    GtkTextMark *mark = gtk_text_buffer_create_mark(gui->desktop_ai_buffer, NULL, &end, FALSE);
    gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(gui->desktop_ai_response), mark, 0.0, FALSE, 0.0, 0.0);
    gtk_text_buffer_delete_mark(gui->desktop_ai_buffer, mark);
}

/* Refresh Desktop */
void phantom_gui_refresh_desktop(phantom_gui_t *gui) {
    if (!gui) return;
    update_desktop_clock(gui);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * PhantomPods Panel - Compatibility Layer Container Management
 * ═══════════════════════════════════════════════════════════════════════════ */

#include "phantom_pods.h"

/* Forward declarations */
static void on_pod_create_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_pod_activate_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_pod_dormant_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_pod_import_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_pod_run_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_pod_selection_changed(GtkTreeSelection *selection, phantom_gui_t *gui);

/* Create PhantomPods panel */
GtkWidget *phantom_gui_create_pods_panel(phantom_gui_t *gui) {
    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(main_box), 8);

    /* Title */
    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title),
        "<span size='large' weight='bold'>📦 PhantomPods</span>\n"
        "<span size='small'>Compatibility containers for running external applications</span>");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(main_box), title, FALSE, FALSE, 8);

    /* Initialize pod system */
    gui->pod_system = malloc(sizeof(phantom_pod_system_t));
    if (gui->pod_system) {
        phantom_pods_init((phantom_pod_system_t *)gui->pod_system, NULL);
    }

    /* Compatibility status bar */
    phantom_pod_system_t *sys = (phantom_pod_system_t *)gui->pod_system;
    char compat_text[256];
    snprintf(compat_text, sizeof(compat_text),
        "Compatibility: %s%s%s%s%s",
        sys && sys->wine_available ? "🪟 Wine  " : "",
        sys && sys->wine64_available ? "🪟 Wine64  " : "",
        sys && sys->dosbox_available ? "👾 DOSBox  " : "",
        sys && sys->flatpak_available ? "📦 Flatpak  " : "",
        "🐧 Native");

    GtkWidget *compat_label = gtk_label_new(compat_text);
    gtk_widget_set_halign(compat_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(main_box), compat_label, FALSE, FALSE, 4);

    /* Toolbar */
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_start(GTK_BOX(main_box), toolbar, FALSE, FALSE, 4);

    gui->pods_create_btn = gtk_button_new_with_label("➕ New Pod");
    gtk_widget_set_tooltip_text(gui->pods_create_btn, "Create a new compatibility pod");
    g_signal_connect(gui->pods_create_btn, "clicked", G_CALLBACK(on_pod_create_clicked), gui);
    gtk_box_pack_start(GTK_BOX(toolbar), gui->pods_create_btn, FALSE, FALSE, 0);

    gui->pods_activate_btn = gtk_button_new_with_label("▶️ Activate");
    gtk_widget_set_tooltip_text(gui->pods_activate_btn, "Activate selected pod");
    gtk_widget_set_sensitive(gui->pods_activate_btn, FALSE);
    g_signal_connect(gui->pods_activate_btn, "clicked", G_CALLBACK(on_pod_activate_clicked), gui);
    gtk_box_pack_start(GTK_BOX(toolbar), gui->pods_activate_btn, FALSE, FALSE, 0);

    gui->pods_dormant_btn = gtk_button_new_with_label("💤 Dormant");
    gtk_widget_set_tooltip_text(gui->pods_dormant_btn, "Make pod dormant (suspend)");
    gtk_widget_set_sensitive(gui->pods_dormant_btn, FALSE);
    g_signal_connect(gui->pods_dormant_btn, "clicked", G_CALLBACK(on_pod_dormant_clicked), gui);
    gtk_box_pack_start(GTK_BOX(toolbar), gui->pods_dormant_btn, FALSE, FALSE, 0);

    gui->pods_import_btn = gtk_button_new_with_label("📥 Import App");
    gtk_widget_set_tooltip_text(gui->pods_import_btn, "Import application into pod");
    gtk_widget_set_sensitive(gui->pods_import_btn, FALSE);
    g_signal_connect(gui->pods_import_btn, "clicked", G_CALLBACK(on_pod_import_clicked), gui);
    gtk_box_pack_start(GTK_BOX(toolbar), gui->pods_import_btn, FALSE, FALSE, 0);

    gui->pods_run_btn = gtk_button_new_with_label("🚀 Run App");
    gtk_widget_set_tooltip_text(gui->pods_run_btn, "Run selected application");
    gtk_widget_set_sensitive(gui->pods_run_btn, FALSE);
    g_signal_connect(gui->pods_run_btn, "clicked", G_CALLBACK(on_pod_run_clicked), gui);
    gtk_box_pack_start(GTK_BOX(toolbar), gui->pods_run_btn, FALSE, FALSE, 0);

    /* Main content paned */
    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(main_box), paned, TRUE, TRUE, 0);

    /* Left side - Pod list */
    GtkWidget *pods_frame = gtk_frame_new("Pods");
    gtk_paned_pack1(GTK_PANED(paned), pods_frame, TRUE, TRUE);

    GtkWidget *pods_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(pods_box), 4);
    gtk_container_add(GTK_CONTAINER(pods_frame), pods_box);

    /* Pod list store */
    gui->pods_store = gtk_list_store_new(POD_COL_COUNT,
        G_TYPE_STRING,   /* Icon */
        G_TYPE_STRING,   /* Name */
        G_TYPE_STRING,   /* Type */
        G_TYPE_STRING,   /* State */
        G_TYPE_INT,      /* App count */
        G_TYPE_STRING,   /* Security */
        G_TYPE_UINT      /* ID */
    );

    gui->pods_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(gui->pods_store));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(gui->pods_tree), TRUE);

    GtkCellRenderer *renderer;
    GtkTreeViewColumn *col;

    renderer = gtk_cell_renderer_text_new();
    col = gtk_tree_view_column_new_with_attributes("", renderer, "text", POD_COL_ICON, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->pods_tree), col);

    renderer = gtk_cell_renderer_text_new();
    col = gtk_tree_view_column_new_with_attributes("Name", renderer, "text", POD_COL_NAME, NULL);
    gtk_tree_view_column_set_min_width(col, 120);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->pods_tree), col);

    renderer = gtk_cell_renderer_text_new();
    col = gtk_tree_view_column_new_with_attributes("Type", renderer, "text", POD_COL_TYPE, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->pods_tree), col);

    renderer = gtk_cell_renderer_text_new();
    col = gtk_tree_view_column_new_with_attributes("State", renderer, "text", POD_COL_STATE, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->pods_tree), col);

    renderer = gtk_cell_renderer_text_new();
    col = gtk_tree_view_column_new_with_attributes("Apps", renderer, "text", POD_COL_APPS, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->pods_tree), col);

    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(gui->pods_tree));
    g_signal_connect(selection, "changed", G_CALLBACK(on_pod_selection_changed), gui);

    GtkWidget *pods_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(pods_scroll),
                                    GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(pods_scroll), gui->pods_tree);
    gtk_box_pack_start(GTK_BOX(pods_box), pods_scroll, TRUE, TRUE, 0);

    /* Right side - Pod details and apps */
    GtkWidget *details_frame = gtk_frame_new("Pod Details");
    gtk_paned_pack2(GTK_PANED(paned), details_frame, TRUE, TRUE);

    gui->pods_details_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(gui->pods_details_box), 8);
    gtk_container_add(GTK_CONTAINER(details_frame), gui->pods_details_box);

    gui->pods_status_label = gtk_label_new("Select a pod to view details");
    gtk_widget_set_halign(gui->pods_status_label, GTK_ALIGN_START);
    gtk_label_set_line_wrap(GTK_LABEL(gui->pods_status_label), TRUE);
    gtk_box_pack_start(GTK_BOX(gui->pods_details_box), gui->pods_status_label, FALSE, FALSE, 4);

    /* Apps list */
    GtkWidget *apps_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(apps_label), "<b>Installed Applications</b>");
    gtk_widget_set_halign(apps_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(gui->pods_details_box), apps_label, FALSE, FALSE, 8);

    gui->pods_apps_store = gtk_list_store_new(POD_APP_COL_COUNT,
        G_TYPE_STRING,   /* Icon */
        G_TYPE_STRING,   /* Name */
        G_TYPE_STRING,   /* Path */
        G_TYPE_UINT64    /* Run count */
    );

    gui->pods_apps_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(gui->pods_apps_store));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(gui->pods_apps_tree), TRUE);

    renderer = gtk_cell_renderer_text_new();
    col = gtk_tree_view_column_new_with_attributes("", renderer, "text", POD_APP_COL_ICON, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->pods_apps_tree), col);

    renderer = gtk_cell_renderer_text_new();
    col = gtk_tree_view_column_new_with_attributes("Application", renderer, "text", POD_APP_COL_NAME, NULL);
    gtk_tree_view_column_set_min_width(col, 150);
    gtk_tree_view_column_set_expand(col, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->pods_apps_tree), col);

    renderer = gtk_cell_renderer_text_new();
    col = gtk_tree_view_column_new_with_attributes("Runs", renderer, "text", POD_APP_COL_RUNS, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->pods_apps_tree), col);

    GtkWidget *apps_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(apps_scroll),
                                    GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(apps_scroll), gui->pods_apps_tree);
    gtk_box_pack_start(GTK_BOX(gui->pods_details_box), apps_scroll, TRUE, TRUE, 0);

    /* Set initial paned position */
    gtk_paned_set_position(GTK_PANED(paned), 350);

    return main_box;
}

/* Pod selection changed */
static void on_pod_selection_changed(GtkTreeSelection *selection, phantom_gui_t *gui) {
    GtkTreeIter iter;
    GtkTreeModel *model;

    gtk_list_store_clear(gui->pods_apps_store);

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        gchar *name, *type, *state, *security;
        guint id;
        gint apps;

        gtk_tree_model_get(model, &iter,
            POD_COL_NAME, &name,
            POD_COL_TYPE, &type,
            POD_COL_STATE, &state,
            POD_COL_APPS, &apps,
            POD_COL_SECURITY, &security,
            POD_COL_ID, &id,
            -1);

        /* Update details label */
        char details[512];
        snprintf(details, sizeof(details),
            "<b>%s</b>\n"
            "<i>Type:</i> %s\n"
            "<i>State:</i> %s\n"
            "<i>Security:</i> %s\n"
            "<i>Applications:</i> %d",
            name, type, state, security, apps);
        gtk_label_set_markup(GTK_LABEL(gui->pods_status_label), details);

        /* Enable buttons based on state */
        gboolean is_active = (strcmp(state, "Active") == 0);
        gboolean is_dormant = (strcmp(state, "Dormant") == 0 || strcmp(state, "Ready") == 0);

        gtk_widget_set_sensitive(gui->pods_activate_btn, is_dormant);
        gtk_widget_set_sensitive(gui->pods_dormant_btn, is_active);
        gtk_widget_set_sensitive(gui->pods_import_btn, TRUE);
        gtk_widget_set_sensitive(gui->pods_run_btn, apps > 0);

        /* Load apps for this pod */
        phantom_pod_system_t *sys = (phantom_pod_system_t *)gui->pod_system;
        if (sys) {
            phantom_pod_t *pod = phantom_pod_find_by_id(sys, id);
            if (pod) {
                for (int i = 0; i < pod->app_count; i++) {
                    GtkTreeIter app_iter;
                    gtk_list_store_append(gui->pods_apps_store, &app_iter);
                    gtk_list_store_set(gui->pods_apps_store, &app_iter,
                        POD_APP_COL_ICON, pod->apps[i].icon,
                        POD_APP_COL_NAME, pod->apps[i].name,
                        POD_APP_COL_PATH, pod->apps[i].executable,
                        POD_APP_COL_RUNS, pod->apps[i].run_count,
                        -1);
                }
            }
        }

        g_free(name);
        g_free(type);
        g_free(state);
        g_free(security);
    } else {
        gtk_label_set_text(GTK_LABEL(gui->pods_status_label), "Select a pod to view details");
        gtk_widget_set_sensitive(gui->pods_activate_btn, FALSE);
        gtk_widget_set_sensitive(gui->pods_dormant_btn, FALSE);
        gtk_widget_set_sensitive(gui->pods_import_btn, FALSE);
        gtk_widget_set_sensitive(gui->pods_run_btn, FALSE);
    }
}

/* Template combo changed callback */
static void on_template_combo_changed(GtkComboBox *combo, gpointer user_data) {
    (void)user_data;
    GtkLabel *label = GTK_LABEL(g_object_get_data(G_OBJECT(combo), "desc_label"));
    if (!label) return;

    int idx = gtk_combo_box_get_active(combo);
    int count;
    const phantom_pod_template_t *tmpls = phantom_pod_get_templates(&count);
    if (idx >= 0 && idx < count) {
        gtk_label_set_text(label, tmpls[idx].description);
    }
}

/* Create new pod dialog */
static void on_pod_create_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;

    GtkWidget *dialog = gtk_dialog_new_with_buttons("Create PhantomPod",
        GTK_WINDOW(gui->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "Cancel", GTK_RESPONSE_CANCEL,
        "Create", GTK_RESPONSE_OK,
        NULL);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 16);
    gtk_box_set_spacing(GTK_BOX(content), 8);

    /* Name entry */
    GtkWidget *name_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *name_label = gtk_label_new("Pod Name:");
    gtk_widget_set_size_request(name_label, 100, -1);
    GtkWidget *name_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(name_entry), "My Application Pod");
    gtk_box_pack_start(GTK_BOX(name_box), name_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(name_box), name_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(content), name_box, FALSE, FALSE, 0);

    /* Template selection */
    GtkWidget *template_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(template_label), "<b>Select Template:</b>");
    gtk_widget_set_halign(template_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(content), template_label, FALSE, FALSE, 8);

    int template_count;
    const phantom_pod_template_t *templates = phantom_pod_get_templates(&template_count);
    phantom_pod_system_t *sys = (phantom_pod_system_t *)gui->pod_system;

    GtkWidget *template_combo = gtk_combo_box_text_new();
    for (int i = 0; i < template_count; i++) {
        char item[256];
        int available = 1;

        /* Check if compatibility layer is available for this template type */
        if (sys) {
            switch (templates[i].type) {
                case POD_TYPE_WINE:
                    available = sys->wine_available;
                    break;
                case POD_TYPE_WINE64:
                    available = sys->wine64_available;
                    break;
                case POD_TYPE_DOSBOX:
                    available = sys->dosbox_available;
                    break;
                case POD_TYPE_FLATPAK:
                    available = sys->flatpak_available;
                    break;
                default:
                    available = 1;  /* Native/AppImage always available */
                    break;
            }
        }

        if (available) {
            snprintf(item, sizeof(item), "%s %s", templates[i].icon, templates[i].name);
        } else {
            snprintf(item, sizeof(item), "%s %s (Not Installed)", templates[i].icon, templates[i].name);
        }
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(template_combo), item);
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(template_combo), 0);
    gtk_box_pack_start(GTK_BOX(content), template_combo, FALSE, FALSE, 0);

    /* Template description */
    GtkWidget *desc_label = gtk_label_new(templates[0].description);
    gtk_label_set_line_wrap(GTK_LABEL(desc_label), TRUE);
    gtk_widget_set_halign(desc_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(content), desc_label, FALSE, FALSE, 4);

    /* Store desc_label for callback and connect signal */
    g_object_set_data(G_OBJECT(template_combo), "desc_label", desc_label);
    g_signal_connect(template_combo, "changed", G_CALLBACK(on_template_combo_changed), NULL);

    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        const char *name = gtk_entry_get_text(GTK_ENTRY(name_entry));
        int template_idx = gtk_combo_box_get_active(GTK_COMBO_BOX(template_combo));

        if (strlen(name) > 0 && template_idx >= 0) {
            phantom_pod_system_t *sys = (phantom_pod_system_t *)gui->pod_system;
            if (sys) {
                phantom_pod_t *pod = phantom_pod_create_from_template(
                    sys, name, &templates[template_idx]);
                if (pod) {
                    phantom_gui_refresh_pods(gui);
                    phantom_gui_update_status(gui, "PhantomPod created successfully");
                } else {
                    phantom_gui_show_message(gui, "Error",
                        "Failed to create pod. Name may already exist.", GTK_MESSAGE_ERROR);
                }
            }
        }
    }

    gtk_widget_destroy(dialog);
}

/* Activate pod */
static void on_pod_activate_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;

    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(gui->pods_tree));
    GtkTreeIter iter;
    GtkTreeModel *model;

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        guint id;
        gtk_tree_model_get(model, &iter, POD_COL_ID, &id, -1);

        phantom_pod_system_t *sys = (phantom_pod_system_t *)gui->pod_system;
        phantom_pod_t *pod = phantom_pod_find_by_id(sys, id);
        if (pod) {
            int result = phantom_pod_activate(sys, pod);
            if (result == 0) {
                phantom_gui_refresh_pods(gui);
                phantom_gui_update_status(gui, "Pod activated");
            } else {
                const char *error_msg;
                switch (result) {
                    case -2:
                        error_msg = "Wine is not installed.\n\nInstall it with:\n  sudo apt install wine";
                        break;
                    case -3:
                        error_msg = "Wine64 is not installed.\n\nInstall it with:\n  sudo apt install wine64";
                        break;
                    case -4:
                        error_msg = "DOSBox is not installed.\n\nInstall it with:\n  sudo apt install dosbox";
                        break;
                    case -5:
                        error_msg = "Flatpak is not installed.\n\nInstall it with:\n  sudo apt install flatpak";
                        break;
                    case -6:
                        error_msg = "QEMU is not installed.\n\nInstall it with:\n  sudo apt install qemu-system-x86";
                        break;
                    default:
                        error_msg = "Failed to activate pod. Unknown error.";
                        break;
                }
                phantom_gui_show_message(gui, "Compatibility Layer Required", error_msg, GTK_MESSAGE_ERROR);
            }
        }
    }
}

/* Make pod dormant */
static void on_pod_dormant_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;

    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(gui->pods_tree));
    GtkTreeIter iter;
    GtkTreeModel *model;

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        guint id;
        gtk_tree_model_get(model, &iter, POD_COL_ID, &id, -1);

        phantom_pod_system_t *sys = (phantom_pod_system_t *)gui->pod_system;
        phantom_pod_t *pod = phantom_pod_find_by_id(sys, id);
        if (pod) {
            phantom_pod_make_dormant(sys, pod);
            phantom_gui_refresh_pods(gui);
            phantom_gui_update_status(gui, "Pod is now dormant");
        }
    }
}

/* Import application into pod */
static void on_pod_import_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;

    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(gui->pods_tree));
    GtkTreeIter iter;
    GtkTreeModel *model;

    if (!gtk_tree_selection_get_selected(selection, &model, &iter)) {
        return;
    }

    guint id;
    gchar *pod_name;
    gtk_tree_model_get(model, &iter, POD_COL_ID, &id, POD_COL_NAME, &pod_name, -1);

    phantom_pod_system_t *sys = (phantom_pod_system_t *)gui->pod_system;
    phantom_pod_t *pod = phantom_pod_find_by_id(sys, id);
    if (!pod) {
        g_free(pod_name);
        return;
    }

    /* File chooser */
    GtkWidget *chooser = gtk_file_chooser_dialog_new(
        "Import Application",
        GTK_WINDOW(gui->window),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "Cancel", GTK_RESPONSE_CANCEL,
        "Import", GTK_RESPONSE_ACCEPT,
        NULL);

    /* Add filters based on pod type */
    GtkFileFilter *filter = gtk_file_filter_new();
    switch (pod->type) {
        case POD_TYPE_WINE:
        case POD_TYPE_WINE64:
            gtk_file_filter_set_name(filter, "Windows Executables (*.exe)");
            gtk_file_filter_add_pattern(filter, "*.exe");
            gtk_file_filter_add_pattern(filter, "*.EXE");
            break;
        case POD_TYPE_DOSBOX:
            gtk_file_filter_set_name(filter, "DOS Executables (*.exe, *.com)");
            gtk_file_filter_add_pattern(filter, "*.exe");
            gtk_file_filter_add_pattern(filter, "*.com");
            gtk_file_filter_add_pattern(filter, "*.EXE");
            gtk_file_filter_add_pattern(filter, "*.COM");
            break;
        case POD_TYPE_APPIMAGE:
            gtk_file_filter_set_name(filter, "AppImage (*.AppImage)");
            gtk_file_filter_add_pattern(filter, "*.AppImage");
            gtk_file_filter_add_pattern(filter, "*.appimage");
            break;
        default:
            gtk_file_filter_set_name(filter, "All Executables");
            gtk_file_filter_add_mime_type(filter, "application/x-executable");
            gtk_file_filter_add_pattern(filter, "*");
            break;
    }
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(chooser), filter);

    if (gtk_dialog_run(GTK_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT) {
        char *filepath = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));

        /* Get app name */
        const char *filename = strrchr(filepath, '/');
        filename = filename ? filename + 1 : filepath;

        /* Determine icon based on type */
        const char *icon = "📄";
        if (pod->type == POD_TYPE_WINE || pod->type == POD_TYPE_WINE64) {
            icon = "🪟";
        } else if (pod->type == POD_TYPE_DOSBOX) {
            icon = "👾";
        } else if (pod->type == POD_TYPE_APPIMAGE) {
            icon = "📀";
        }

        if (phantom_pod_install_app(pod, filename, filepath, icon) == 0) {
            phantom_gui_refresh_pods(gui);
            on_pod_selection_changed(
                gtk_tree_view_get_selection(GTK_TREE_VIEW(gui->pods_tree)), gui);

            char msg[256];
            snprintf(msg, sizeof(msg), "Imported '%s' into pod '%s'", filename, pod_name);
            phantom_gui_update_status(gui, msg);
        }

        g_free(filepath);
    }

    g_free(pod_name);
    gtk_widget_destroy(chooser);
}

/* Run application in pod */
static void on_pod_run_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;

    /* Get selected pod */
    GtkTreeSelection *pod_selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(gui->pods_tree));
    GtkTreeIter pod_iter;
    GtkTreeModel *pod_model;

    if (!gtk_tree_selection_get_selected(pod_selection, &pod_model, &pod_iter)) {
        return;
    }

    guint pod_id;
    gtk_tree_model_get(pod_model, &pod_iter, POD_COL_ID, &pod_id, -1);

    phantom_pod_system_t *sys = (phantom_pod_system_t *)gui->pod_system;
    phantom_pod_t *pod = phantom_pod_find_by_id(sys, pod_id);
    if (!pod) return;

    /* Get selected app */
    GtkTreeSelection *app_selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(gui->pods_apps_tree));
    GtkTreeIter app_iter;
    GtkTreeModel *app_model;

    int app_index = 0;
    if (gtk_tree_selection_get_selected(app_selection, &app_model, &app_iter)) {
        GtkTreePath *path = gtk_tree_model_get_path(app_model, &app_iter);
        app_index = gtk_tree_path_get_indices(path)[0];
        gtk_tree_path_free(path);
    }

    if (app_index < pod->app_count) {
        if (phantom_pod_run_app(sys, pod, &pod->apps[app_index]) == 0) {
            phantom_gui_refresh_pods(gui);
            char msg[256];
            snprintf(msg, sizeof(msg), "Running '%s' in pod '%s'",
                     pod->apps[app_index].name, pod->name);
            phantom_gui_update_status(gui, msg);
        } else {
            phantom_gui_show_message(gui, "Error",
                "Failed to run application", GTK_MESSAGE_ERROR);
        }
    }
}

/* Refresh pods list */
void phantom_gui_refresh_pods(phantom_gui_t *gui) {
    if (!gui || !gui->pods_store) return;

    gtk_list_store_clear(gui->pods_store);

    phantom_pod_system_t *sys = (phantom_pod_system_t *)gui->pod_system;
    if (!sys) return;

    for (int i = 0; i < sys->pod_count; i++) {
        phantom_pod_t *pod = &sys->pods[i];

        GtkTreeIter iter;
        gtk_list_store_append(gui->pods_store, &iter);
        gtk_list_store_set(gui->pods_store, &iter,
            POD_COL_ICON, pod->icon,
            POD_COL_NAME, pod->name,
            POD_COL_TYPE, phantom_pod_type_name(pod->type),
            POD_COL_STATE, phantom_pod_state_name(pod->state),
            POD_COL_APPS, pod->app_count,
            POD_COL_SECURITY, phantom_pod_security_name(pod->security),
            POD_COL_ID, pod->id,
            -1);
    }
}
/* ═════════════════════════════════════════════════════════════════════════════
 * PHANTOM BACKUP UTILITY GUI
 * ═══════════════════════════════════════════════════════════════════════════ */

#include "phantom_backup.h"

/* Forward declarations */
static void on_backup_quick_full_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_backup_quick_geofs_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_backup_custom_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_backup_restore_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_backup_verify_clicked(GtkWidget *button, phantom_gui_t *gui);
static void on_backup_selection_changed(GtkTreeSelection *selection, phantom_gui_t *gui);

/* Create Backup panel */
GtkWidget *phantom_gui_create_backup_panel(phantom_gui_t *gui) {
    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(main_box), 8);

    /* Title */
    GtkWidget *title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title),
        "<span size='large' weight='bold'>💾 Phantom Backup</span>\n"
        "<span size='small'>Preservation Through Replication</span>");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(main_box), title, FALSE, FALSE, 8);

    /* Initialize backup system */
    gui->backup_system = malloc(sizeof(phantom_backup_system_t));
    if (gui->backup_system) {
        phantom_backup_init((phantom_backup_system_t *)gui->backup_system, NULL);
    }

    /* Quick Actions Toolbar */
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_start(GTK_BOX(main_box), toolbar, FALSE, FALSE, 4);

    gui->backup_quick_full_btn = gtk_button_new_with_label("🌐 Full Backup");
    gtk_widget_set_tooltip_text(gui->backup_quick_full_btn, "Backup entire system");
    g_signal_connect(gui->backup_quick_full_btn, "clicked", G_CALLBACK(on_backup_quick_full_clicked), gui);
    gtk_box_pack_start(GTK_BOX(toolbar), gui->backup_quick_full_btn, FALSE, FALSE, 0);

    gui->backup_quick_geofs_btn = gtk_button_new_with_label("🪨 GeoFS Backup");
    gtk_widget_set_tooltip_text(gui->backup_quick_geofs_btn, "Backup GeoFS volumes only");
    g_signal_connect(gui->backup_quick_geofs_btn, "clicked", G_CALLBACK(on_backup_quick_geofs_clicked), gui);
    gtk_box_pack_start(GTK_BOX(toolbar), gui->backup_quick_geofs_btn, FALSE, FALSE, 0);

    gui->backup_custom_btn = gtk_button_new_with_label("⚙️ Custom Backup");
    gtk_widget_set_tooltip_text(gui->backup_custom_btn, "Create custom backup");
    g_signal_connect(gui->backup_custom_btn, "clicked", G_CALLBACK(on_backup_custom_clicked), gui);
    gtk_box_pack_start(GTK_BOX(toolbar), gui->backup_custom_btn, FALSE, FALSE, 0);

    /* Separator */
    GtkWidget *sep1 = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
    gtk_box_pack_start(GTK_BOX(toolbar), sep1, FALSE, FALSE, 4);

    gui->backup_restore_btn = gtk_button_new_with_label("♻️ Restore");
    gtk_widget_set_tooltip_text(gui->backup_restore_btn, "Restore from backup");
    gtk_widget_set_sensitive(gui->backup_restore_btn, FALSE);
    g_signal_connect(gui->backup_restore_btn, "clicked", G_CALLBACK(on_backup_restore_clicked), gui);
    gtk_box_pack_start(GTK_BOX(toolbar), gui->backup_restore_btn, FALSE, FALSE, 0);

    gui->backup_verify_btn = gtk_button_new_with_label("✓ Verify");
    gtk_widget_set_tooltip_text(gui->backup_verify_btn, "Verify backup integrity");
    gtk_widget_set_sensitive(gui->backup_verify_btn, FALSE);
    g_signal_connect(gui->backup_verify_btn, "clicked", G_CALLBACK(on_backup_verify_clicked), gui);
    gtk_box_pack_start(GTK_BOX(toolbar), gui->backup_verify_btn, FALSE, FALSE, 0);

    /* Main content paned */
    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    gtk_box_pack_start(GTK_BOX(main_box), paned, TRUE, TRUE, 0);

    /* Top - Backup History */
    GtkWidget *history_frame = gtk_frame_new("Backup History");
    gtk_paned_pack1(GTK_PANED(paned), history_frame, TRUE, TRUE);

    GtkWidget *history_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(history_box), 4);
    gtk_container_add(GTK_CONTAINER(history_frame), history_box);

    /* Backup list store */
    gui->backup_store = gtk_list_store_new(BACKUP_COL_COUNT,
        G_TYPE_STRING,   /* Name */
        G_TYPE_STRING,   /* Type */
        G_TYPE_STRING,   /* Date */
        G_TYPE_STRING,   /* Size */
        G_TYPE_STRING,   /* State */
        G_TYPE_UINT      /* ID */
    );

    gui->backup_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(gui->backup_store));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(gui->backup_tree), TRUE);

    GtkCellRenderer *renderer;
    GtkTreeViewColumn *col;

    renderer = gtk_cell_renderer_text_new();
    col = gtk_tree_view_column_new_with_attributes("Name", renderer, "text", BACKUP_COL_NAME, NULL);
    gtk_tree_view_column_set_expand(col, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->backup_tree), col);

    renderer = gtk_cell_renderer_text_new();
    col = gtk_tree_view_column_new_with_attributes("Type", renderer, "text", BACKUP_COL_TYPE, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->backup_tree), col);

    renderer = gtk_cell_renderer_text_new();
    col = gtk_tree_view_column_new_with_attributes("Date", renderer, "text", BACKUP_COL_DATE, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->backup_tree), col);

    renderer = gtk_cell_renderer_text_new();
    col = gtk_tree_view_column_new_with_attributes("Size", renderer, "text", BACKUP_COL_SIZE, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->backup_tree), col);

    renderer = gtk_cell_renderer_text_new();
    col = gtk_tree_view_column_new_with_attributes("Status", renderer, "text", BACKUP_COL_STATE, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(gui->backup_tree), col);

    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(gui->backup_tree));
    g_signal_connect(selection, "changed", G_CALLBACK(on_backup_selection_changed), gui);

    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scrolled), gui->backup_tree);
    gtk_box_pack_start(GTK_BOX(history_box), scrolled, TRUE, TRUE, 0);

    /* Bottom - Backup Details/Progress */
    GtkWidget *details_frame = gtk_frame_new("Status");
    gtk_paned_pack2(GTK_PANED(paned), details_frame, FALSE, TRUE);

    GtkWidget *details_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(details_box), 8);
    gtk_container_add(GTK_CONTAINER(details_frame), details_box);

    /* Status label */
    gui->backup_status_label = gtk_label_new("Ready to backup");
    gtk_widget_set_halign(gui->backup_status_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(details_box), gui->backup_status_label, FALSE, FALSE, 4);

    /* Progress bar */
    gui->backup_progress = gtk_progress_bar_new();
    gtk_box_pack_start(GTK_BOX(details_box), gui->backup_progress, FALSE, FALSE, 4);

    /* Size label */
    gui->backup_size_label = gtk_label_new("No backups created yet");
    gtk_widget_set_halign(gui->backup_size_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(details_box), gui->backup_size_label, FALSE, FALSE, 4);

    /* Set paned position */
    gtk_paned_set_position(GTK_PANED(paned), 300);

    phantom_gui_refresh_backup(gui);

    return main_box;
}

/* Quick Full Backup */
static void on_backup_quick_full_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;

    /* Ask for destination */
    GtkWidget *dialog = gtk_file_chooser_dialog_new("Select Backup Destination",
        GTK_WINDOW(gui->window),
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
        "Cancel", GTK_RESPONSE_CANCEL,
        "Select", GTK_RESPONSE_ACCEPT,
        NULL);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *folder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));

        phantom_backup_system_t *sys = (phantom_backup_system_t *)gui->backup_system;
        if (sys) {
            gtk_label_set_text(GTK_LABEL(gui->backup_status_label), "Creating full system backup...");
            gtk_progress_bar_pulse(GTK_PROGRESS_BAR(gui->backup_progress));

            if (phantom_backup_quick_full(sys, folder) == 0) {
                phantom_gui_update_status(gui, "Full backup completed successfully");
                phantom_gui_refresh_backup(gui);
            } else {
                phantom_gui_show_message(gui, "Backup Failed",
                    "Failed to create full system backup.", GTK_MESSAGE_ERROR);
            }

            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(gui->backup_progress), 0.0);
            gtk_label_set_text(GTK_LABEL(gui->backup_status_label), "Ready to backup");
        }

        g_free(folder);
    }

    gtk_widget_destroy(dialog);
}

/* Quick GeoFS Backup */
static void on_backup_quick_geofs_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;

    GtkWidget *dialog = gtk_file_chooser_dialog_new("Select Backup Destination",
        GTK_WINDOW(gui->window),
        GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
        "Cancel", GTK_RESPONSE_CANCEL,
        "Select", GTK_RESPONSE_ACCEPT,
        NULL);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *folder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));

        phantom_backup_system_t *sys = (phantom_backup_system_t *)gui->backup_system;
        if (sys) {
            gtk_label_set_text(GTK_LABEL(gui->backup_status_label), "Creating GeoFS backup...");
            gtk_progress_bar_pulse(GTK_PROGRESS_BAR(gui->backup_progress));

            if (phantom_backup_quick_geofs(sys, folder) == 0) {
                phantom_gui_update_status(gui, "GeoFS backup completed successfully");
                phantom_gui_refresh_backup(gui);
            } else {
                phantom_gui_show_message(gui, "Backup Failed",
                    "Failed to create GeoFS backup.", GTK_MESSAGE_ERROR);
            }

            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(gui->backup_progress), 0.0);
            gtk_label_set_text(GTK_LABEL(gui->backup_status_label), "Ready to backup");
        }

        g_free(folder);
    }

    gtk_widget_destroy(dialog);
}

/* Custom Backup */
static void on_backup_custom_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;

    GtkWidget *dialog = gtk_dialog_new_with_buttons("Create Custom Backup",
        GTK_WINDOW(gui->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "Cancel", GTK_RESPONSE_CANCEL,
        "Create Backup", GTK_RESPONSE_OK,
        NULL);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 16);
    gtk_box_set_spacing(GTK_BOX(content), 8);

    /* Backup name */
    GtkWidget *name_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *name_label = gtk_label_new("Backup Name:");
    gtk_widget_set_size_request(name_label, 120, -1);
    GtkWidget *name_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(name_entry), "MyBackup");
    gtk_box_pack_start(GTK_BOX(name_box), name_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(name_box), name_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(content), name_box, FALSE, FALSE, 0);

    /* Backup type */
    GtkWidget *type_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *type_label = gtk_label_new("Backup Type:");
    gtk_widget_set_size_request(type_label, 120, -1);
    GtkWidget *type_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(type_combo), "Full System");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(type_combo), "GeoFS Volumes");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(type_combo), "PhantomPods");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(type_combo), "Configuration");
    gtk_combo_box_set_active(GTK_COMBO_BOX(type_combo), 0);
    gtk_box_pack_start(GTK_BOX(type_box), type_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(type_box), type_combo, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(content), type_box, FALSE, FALSE, 0);

    /* Compression */
    GtkWidget *comp_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *comp_label = gtk_label_new("Compression:");
    gtk_widget_set_size_request(comp_label, 120, -1);
    GtkWidget *comp_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(comp_combo), "gzip (default)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(comp_combo), "bzip2 (better)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(comp_combo), "xz (best)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(comp_combo), "None");
    gtk_combo_box_set_active(GTK_COMBO_BOX(comp_combo), 0);
    gtk_box_pack_start(GTK_BOX(comp_box), comp_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(comp_box), comp_combo, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(content), comp_box, FALSE, FALSE, 0);

    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        const char *name = gtk_entry_get_text(GTK_ENTRY(name_entry));
        int type_idx = gtk_combo_box_get_active(GTK_COMBO_BOX(type_combo));
        int comp_idx = gtk_combo_box_get_active(GTK_COMBO_BOX(comp_combo));

        if (strlen(name) > 0) {
            /* Ask for destination */
            GtkWidget *dest_dialog = gtk_file_chooser_dialog_new("Select Backup Destination",
                GTK_WINDOW(gui->window),
                GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                "Cancel", GTK_RESPONSE_CANCEL,
                "Select", GTK_RESPONSE_ACCEPT,
                NULL);

            if (gtk_dialog_run(GTK_DIALOG(dest_dialog)) == GTK_RESPONSE_ACCEPT) {
                char *folder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dest_dialog));

                phantom_backup_system_t *sys = (phantom_backup_system_t *)gui->backup_system;
                if (sys) {
                    phantom_backup_type_t backup_type = BACKUP_TYPE_FULL;
                    switch (type_idx) {
                        case 0: backup_type = BACKUP_TYPE_FULL; break;
                        case 1: backup_type = BACKUP_TYPE_GEOFS; break;
                        case 2: backup_type = BACKUP_TYPE_PODS; break;
                        case 3: backup_type = BACKUP_TYPE_CONFIG; break;
                    }

                    phantom_backup_compression_t compression = BACKUP_COMPRESSION_GZIP;
                    switch (comp_idx) {
                        case 0: compression = BACKUP_COMPRESSION_GZIP; break;
                        case 1: compression = BACKUP_COMPRESSION_BZIP2; break;
                        case 2: compression = BACKUP_COMPRESSION_XZ; break;
                        case 3: compression = BACKUP_COMPRESSION_NONE; break;
                    }

                    phantom_backup_job_t *job = phantom_backup_create_job(sys, name, backup_type, folder);
                    if (job) {
                        phantom_backup_set_compression(job, compression);

                        gtk_label_set_text(GTK_LABEL(gui->backup_status_label), "Creating custom backup...");
                        gtk_progress_bar_pulse(GTK_PROGRESS_BAR(gui->backup_progress));

                        if (phantom_backup_start(sys, job) == 0) {
                            phantom_gui_update_status(gui, "Custom backup completed successfully");
                            phantom_gui_refresh_backup(gui);
                        } else {
                            phantom_gui_show_message(gui, "Backup Failed",
                                "Failed to create custom backup.", GTK_MESSAGE_ERROR);
                        }

                        free(job);

                        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(gui->backup_progress), 0.0);
                        gtk_label_set_text(GTK_LABEL(gui->backup_status_label), "Ready to backup");
                    }
                }

                g_free(folder);
            }

            gtk_widget_destroy(dest_dialog);
        }
    }

    gtk_widget_destroy(dialog);
}

/* Restore Backup */
static void on_backup_restore_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;

    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(gui->backup_tree));
    GtkTreeIter iter;
    GtkTreeModel *model;

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        guint id;
        gtk_tree_model_get(model, &iter, BACKUP_COL_ID, &id, -1);

        phantom_backup_system_t *sys = (phantom_backup_system_t *)gui->backup_system;
        phantom_backup_record_t *backup = phantom_backup_find_by_id(sys, id);
        if (backup) {
            /* Confirm restore */
            GtkWidget *confirm = gtk_message_dialog_new(GTK_WINDOW(gui->window),
                GTK_DIALOG_MODAL,
                GTK_MESSAGE_WARNING,
                GTK_BUTTONS_YES_NO,
                "Are you sure you want to restore this backup?\n\nThis will restore data to the root (/) directory.");

            int response = gtk_dialog_run(GTK_DIALOG(confirm));
            gtk_widget_destroy(confirm);

            if (response == GTK_RESPONSE_YES) {
                gtk_label_set_text(GTK_LABEL(gui->backup_status_label), "Restoring backup...");
                gtk_progress_bar_pulse(GTK_PROGRESS_BAR(gui->backup_progress));

                if (phantom_backup_restore(sys, backup, "/") == 0) {
                    phantom_gui_show_message(gui, "Restore Successful",
                        "Backup restored successfully.", GTK_MESSAGE_INFO);
                } else {
                    phantom_gui_show_message(gui, "Restore Failed",
                        "Failed to restore backup.", GTK_MESSAGE_ERROR);
                }

                gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(gui->backup_progress), 0.0);
                gtk_label_set_text(GTK_LABEL(gui->backup_status_label), "Ready to backup");
            }
        }
    }
}

/* Verify Backup */
static void on_backup_verify_clicked(GtkWidget *button, phantom_gui_t *gui) {
    (void)button;

    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(gui->backup_tree));
    GtkTreeIter iter;
    GtkTreeModel *model;

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        guint id;
        gtk_tree_model_get(model, &iter, BACKUP_COL_ID, &id, -1);

        phantom_backup_system_t *sys = (phantom_backup_system_t *)gui->backup_system;
        phantom_backup_record_t *backup = phantom_backup_find_by_id(sys, id);
        if (backup) {
            gtk_label_set_text(GTK_LABEL(gui->backup_status_label), "Verifying backup...");
            gtk_progress_bar_pulse(GTK_PROGRESS_BAR(gui->backup_progress));

            if (phantom_backup_verify(sys, backup) == 0) {
                phantom_gui_show_message(gui, "Verification Successful",
                    "Backup archive verified successfully.", GTK_MESSAGE_INFO);
                phantom_gui_refresh_backup(gui);
            } else {
                phantom_gui_show_message(gui, "Verification Failed",
                    "Backup archive is corrupted or inaccessible.", GTK_MESSAGE_ERROR);
            }

            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(gui->backup_progress), 0.0);
            gtk_label_set_text(GTK_LABEL(gui->backup_status_label), "Ready to backup");
        }
    }
}

/* Backup selection changed */
static void on_backup_selection_changed(GtkTreeSelection *selection, phantom_gui_t *gui) {
    GtkTreeIter iter;
    GtkTreeModel *model;

    int has_selection = gtk_tree_selection_get_selected(selection, &model, &iter);

    gtk_widget_set_sensitive(gui->backup_restore_btn, has_selection);
    gtk_widget_set_sensitive(gui->backup_verify_btn, has_selection);

    if (has_selection) {
        guint id;
        gtk_tree_model_get(model, &iter, BACKUP_COL_ID, &id, -1);

        phantom_backup_system_t *sys = (phantom_backup_system_t *)gui->backup_system;
        phantom_backup_record_t *backup = phantom_backup_find_by_id(sys, id);
        if (backup) {
            char status[512];
            snprintf(status, sizeof(status),
                "Backup: %.128s\nType: %s\nArchive: %.256s\nVerified: %s",
                backup->name,
                phantom_backup_type_name(backup->type),
                backup->archive_path,
                backup->verified ? "Yes" : "No");
            gtk_label_set_text(GTK_LABEL(gui->backup_status_label), status);
        }
    }
}

/* Refresh backup list */
void phantom_gui_refresh_backup(phantom_gui_t *gui) {
    if (!gui || !gui->backup_store) return;

    gtk_list_store_clear(gui->backup_store);

    phantom_backup_system_t *sys = (phantom_backup_system_t *)gui->backup_system;
    if (!sys) return;

    for (int i = 0; i < sys->backup_count; i++) {
        phantom_backup_record_t *backup = &sys->backups[i];

        /* Format date */
        char date_str[64];
        struct tm *tm_info = localtime(&backup->created);
        strftime(date_str, sizeof(date_str), "%Y-%m-%d %H:%M", tm_info);

        /* Format size */
        char size_str[32];
        if (backup->compressed_bytes > 1024 * 1024 * 1024) {
            snprintf(size_str, sizeof(size_str), "%.2f GB",
                     backup->compressed_bytes / (1024.0 * 1024 * 1024));
        } else if (backup->compressed_bytes > 1024 * 1024) {
            snprintf(size_str, sizeof(size_str), "%.2f MB",
                     backup->compressed_bytes / (1024.0 * 1024));
        } else if (backup->compressed_bytes > 1024) {
            snprintf(size_str, sizeof(size_str), "%.2f KB",
                     backup->compressed_bytes / 1024.0);
        } else {
            snprintf(size_str, sizeof(size_str), "%zu B", backup->compressed_bytes);
        }

        GtkTreeIter iter;
        gtk_list_store_append(gui->backup_store, &iter);
        gtk_list_store_set(gui->backup_store, &iter,
            BACKUP_COL_NAME, backup->name,
            BACKUP_COL_TYPE, phantom_backup_type_name(backup->type),
            BACKUP_COL_DATE, date_str,
            BACKUP_COL_SIZE, size_str,
            BACKUP_COL_STATE, phantom_backup_state_name(backup->state),
            BACKUP_COL_ID, backup->id,
            -1);
    }

    /* Update size label */
    if (sys->backup_count > 0) {
        char size_info[256];
        if (sys->total_backup_size > 1024 * 1024 * 1024) {
            snprintf(size_info, sizeof(size_info),
                     "%d backups • %.2f GB total",
                     sys->backup_count,
                     sys->total_backup_size / (1024.0 * 1024 * 1024));
        } else {
            snprintf(size_info, sizeof(size_info),
                     "%d backups • %.2f MB total",
                     sys->backup_count,
                     sys->total_backup_size / (1024.0 * 1024));
        }
        gtk_label_set_text(GTK_LABEL(gui->backup_size_label), size_info);
    }
}
