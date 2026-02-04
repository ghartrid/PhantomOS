/*
 * ══════════════════════════════════════════════════════════════════════════════
 *
 *                           PHANTOM EXPLORER
 *                    GTK4 GUI for PhantomOS GeoFS
 *
 *                      "To Create, Not To Destroy"
 *
 *    A graphical file explorer for GeoFS volumes.
 *    Browse files, view content, navigate through geological strata.
 *
 *    Build: make
 *    Usage: ./phantom-explorer [volume.geo]
 *
 * ══════════════════════════════════════════════════════════════════════════════
 */

#include <gtk/gtk.h>
#include <string.h>
#include "../geofs.h"

/* ══════════════════════════════════════════════════════════════════════════════
 * APPLICATION STATE
 * ══════════════════════════════════════════════════════════════════════════════ */

typedef struct {
    GtkApplication *app;
    GtkWindow *window;

    /* Volume state */
    geofs_volume_t *volume;
    char volume_path[GEOFS_MAX_PATH];
    char current_dir[GEOFS_MAX_PATH];

    /* UI components */
    GtkWidget *header_bar;
    GtkWidget *file_list;
    GtkWidget *content_view;
    GtkWidget *view_selector;
    GtkWidget *path_label;
    GtkWidget *status_bar;
    GtkWidget *info_panel;

    /* List store for files */
    GListStore *file_store;

} PhantomExplorer;

/* File item for the list - using GObject properly */
#define PHANTOM_TYPE_FILE_ITEM (phantom_file_item_get_type())
G_DECLARE_FINAL_TYPE(PhantomFileItem, phantom_file_item, PHANTOM, FILE_ITEM, GObject)

struct _PhantomFileItem {
    GObject parent_instance;
    char name[GEOFS_MAX_NAME + 1];
    char hash[65];
    uint64_t size;
    geofs_time_t created;
    int is_dir;
};

G_DEFINE_TYPE(PhantomFileItem, phantom_file_item, G_TYPE_OBJECT)

static void phantom_file_item_class_init(PhantomFileItemClass *klass) {
    (void)klass;
}

static void phantom_file_item_init(PhantomFileItem *self) {
    memset(self->name, 0, sizeof(self->name));
    memset(self->hash, 0, sizeof(self->hash));
    self->size = 0;
    self->created = 0;
    self->is_dir = 0;
}

static PhantomFileItem *phantom_file_item_new(const struct geofs_dirent *entry) {
    PhantomFileItem *item = g_object_new(PHANTOM_TYPE_FILE_ITEM, NULL);
    strncpy(item->name, entry->name, GEOFS_MAX_NAME);
    geofs_hash_to_string(entry->content_hash, item->hash);
    item->size = entry->size;
    item->created = entry->created;
    item->is_dir = entry->is_dir;
    return item;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * CALLBACKS FOR DIRECTORY LISTING
 * ══════════════════════════════════════════════════════════════════════════════ */

static void add_file_callback(const struct geofs_dirent *entry, void *ctx) {
    GListStore *store = G_LIST_STORE(ctx);
    PhantomFileItem *item = phantom_file_item_new(entry);
    g_list_store_append(store, item);
    g_object_unref(item);
}

/* ══════════════════════════════════════════════════════════════════════════════
 * UI UPDATE FUNCTIONS
 * ══════════════════════════════════════════════════════════════════════════════ */

static void update_file_list(PhantomExplorer *explorer) {
    if (!explorer->volume) return;

    /* Clear existing items */
    g_list_store_remove_all(explorer->file_store);

    /* List files in current directory */
    geofs_ref_list(explorer->volume, explorer->current_dir, add_file_callback, explorer->file_store);

    /* Update path label */
    char path_text[GEOFS_MAX_PATH + 64];
    snprintf(path_text, sizeof(path_text), "View %lu: %s",
             geofs_view_current(explorer->volume), explorer->current_dir);
    gtk_label_set_text(GTK_LABEL(explorer->path_label), path_text);
}

static void update_status(PhantomExplorer *explorer, const char *message) {
    gtk_label_set_text(GTK_LABEL(explorer->status_bar), message);
}

static void show_file_content(PhantomExplorer *explorer, const char *path) {
    if (!explorer->volume) return;

    geofs_hash_t hash;
    geofs_error_t err = geofs_ref_resolve(explorer->volume, path, hash);
    if (err != GEOFS_OK) {
        update_status(explorer, "Failed to resolve file");
        return;
    }

    uint64_t size;
    err = geofs_content_size(explorer->volume, hash, &size);
    if (err != GEOFS_OK) {
        update_status(explorer, "Failed to get file size");
        return;
    }

    /* Limit display size */
    if (size > 1024 * 1024) {
        update_status(explorer, "File too large to display");
        return;
    }

    char *content = g_malloc(size + 1);
    size_t got;
    err = geofs_content_read(explorer->volume, hash, content, size, &got);
    if (err != GEOFS_OK) {
        g_free(content);
        update_status(explorer, "Failed to read file");
        return;
    }
    content[got] = '\0';

    /* Update text view */
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(explorer->content_view));
    gtk_text_buffer_set_text(buffer, content, got);

    g_free(content);

    char status[256];
    snprintf(status, sizeof(status), "Loaded: %s (%zu bytes)", path, got);
    update_status(explorer, status);
}

/* ══════════════════════════════════════════════════════════════════════════════
 * VIEW MANAGEMENT
 * ══════════════════════════════════════════════════════════════════════════════ */

typedef struct {
    GtkDropDown *dropdown;
    GListStore *store;
} ViewPopulateContext;

static void add_view_callback(const struct geofs_view_info *info, void *ctx) {
    ViewPopulateContext *vctx = ctx;
    char label[128];
    snprintf(label, sizeof(label), "View %lu: %s", info->id,
             info->label[0] ? info->label : "(unlabeled)");
    gtk_string_list_append(GTK_STRING_LIST(vctx->store), label);
}

static void populate_view_selector(PhantomExplorer *explorer) {
    if (!explorer->volume) return;

    GtkStringList *model = gtk_string_list_new(NULL);

    ViewPopulateContext ctx = {
        .dropdown = GTK_DROP_DOWN(explorer->view_selector),
        .store = G_LIST_STORE(model)
    };

    geofs_view_list(explorer->volume, add_view_callback, &ctx);

    gtk_drop_down_set_model(GTK_DROP_DOWN(explorer->view_selector), G_LIST_MODEL(model));
    g_object_unref(model);
}

static void on_view_changed(GtkDropDown *dropdown, GParamSpec *pspec, gpointer user_data) {
    (void)pspec;
    PhantomExplorer *explorer = user_data;

    guint selected = gtk_drop_down_get_selected(dropdown);
    if (selected != GTK_INVALID_LIST_POSITION && explorer->volume) {
        geofs_view_switch(explorer->volume, selected + 1);  /* Views are 1-indexed */
        update_file_list(explorer);

        char status[64];
        snprintf(status, sizeof(status), "Switched to view %u", selected + 1);
        update_status(explorer, status);
    }
}

/* ══════════════════════════════════════════════════════════════════════════════
 * FILE LIST INTERACTION
 * ══════════════════════════════════════════════════════════════════════════════ */

static void on_file_activated(GtkListView *list_view, guint position, gpointer user_data) {
    (void)list_view;
    PhantomExplorer *explorer = user_data;

    PhantomFileItem *item = g_list_model_get_item(G_LIST_MODEL(explorer->file_store), position);
    if (!item) return;

    /* Build full path */
    char full_path[GEOFS_MAX_PATH];
    if (strcmp(explorer->current_dir, "/") == 0) {
        snprintf(full_path, sizeof(full_path), "/%s", item->name);
    } else {
        snprintf(full_path, sizeof(full_path), "%s/%s", explorer->current_dir, item->name);
    }

    if (item->is_dir) {
        /* Navigate into directory */
        strncpy(explorer->current_dir, full_path, GEOFS_MAX_PATH - 1);
        update_file_list(explorer);
    } else {
        /* Show file content */
        show_file_content(explorer, full_path);
    }

    g_object_unref(item);
}

/* ══════════════════════════════════════════════════════════════════════════════
 * VOLUME OPERATIONS
 * ══════════════════════════════════════════════════════════════════════════════ */

static void open_volume(PhantomExplorer *explorer, const char *path) {
    /* Close existing volume */
    if (explorer->volume) {
        geofs_volume_close(explorer->volume);
        explorer->volume = NULL;
    }

    /* Open new volume */
    geofs_error_t err = geofs_volume_open(path, &explorer->volume);
    if (err != GEOFS_OK) {
        GtkAlertDialog *dialog = gtk_alert_dialog_new("Failed to open volume: %s",
                                                       geofs_strerror(err));
        gtk_alert_dialog_show(dialog, explorer->window);
        g_object_unref(dialog);
        return;
    }

    strncpy(explorer->volume_path, path, GEOFS_MAX_PATH - 1);
    strcpy(explorer->current_dir, "/");

    /* Update window title */
    char title[GEOFS_MAX_PATH + 32];
    snprintf(title, sizeof(title), "Phantom Explorer - %s", path);
    gtk_window_set_title(explorer->window, title);

    /* Populate UI */
    populate_view_selector(explorer);
    update_file_list(explorer);

    char status[256];
    snprintf(status, sizeof(status), "Opened: %s (View %lu)",
             path, geofs_view_current(explorer->volume));
    update_status(explorer, status);
}

static void on_open_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    PhantomExplorer *explorer = user_data;

    GtkFileDialog *dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, "Open GeoFS Volume");

    /* Add filter for .geo files */
    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "GeoFS Volumes (*.geo)");
    gtk_file_filter_add_pattern(filter, "*.geo");

    GListStore *filters = g_list_store_new(GTK_TYPE_FILE_FILTER);
    g_list_store_append(filters, filter);
    gtk_file_dialog_set_filters(dialog, G_LIST_MODEL(filters));

    gtk_file_dialog_open(dialog, explorer->window, NULL,
        (GAsyncReadyCallback)({
            void callback(GObject *source, GAsyncResult *result, gpointer data) {
                PhantomExplorer *exp = data;
                GtkFileDialog *dlg = GTK_FILE_DIALOG(source);
                GFile *file = gtk_file_dialog_open_finish(dlg, result, NULL);
                if (file) {
                    char *path = g_file_get_path(file);
                    open_volume(exp, path);
                    g_free(path);
                    g_object_unref(file);
                }
            }
            callback;
        }), explorer);

    g_object_unref(filters);
    g_object_unref(filter);
    g_object_unref(dialog);
}

static void on_up_clicked(GtkButton *button, gpointer user_data) {
    (void)button;
    PhantomExplorer *explorer = user_data;

    if (strcmp(explorer->current_dir, "/") == 0) return;

    /* Go up one directory */
    char *last_slash = strrchr(explorer->current_dir, '/');
    if (last_slash && last_slash != explorer->current_dir) {
        *last_slash = '\0';
    } else {
        strcpy(explorer->current_dir, "/");
    }

    update_file_list(explorer);
}

/* ══════════════════════════════════════════════════════════════════════════════
 * UI FACTORY FUNCTIONS
 * ══════════════════════════════════════════════════════════════════════════════ */

static void setup_file_item(GtkListItemFactory *factory, GtkListItem *list_item, gpointer user_data) {
    (void)factory;
    (void)user_data;

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_start(box, 8);
    gtk_widget_set_margin_end(box, 8);
    gtk_widget_set_margin_top(box, 4);
    gtk_widget_set_margin_bottom(box, 4);

    GtkWidget *icon = gtk_image_new();
    gtk_box_append(GTK_BOX(box), icon);

    GtkWidget *name_label = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(name_label), 0);
    gtk_widget_set_hexpand(name_label, TRUE);
    gtk_box_append(GTK_BOX(box), name_label);

    GtkWidget *size_label = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(size_label), 1);
    gtk_widget_add_css_class(size_label, "dim-label");
    gtk_box_append(GTK_BOX(box), size_label);

    GtkWidget *hash_label = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(hash_label), 1);
    gtk_widget_add_css_class(hash_label, "dim-label");
    gtk_widget_add_css_class(hash_label, "monospace");
    gtk_box_append(GTK_BOX(box), hash_label);

    gtk_list_item_set_child(list_item, box);
}

static void bind_file_item(GtkListItemFactory *factory, GtkListItem *list_item, gpointer user_data) {
    (void)factory;
    (void)user_data;

    GtkWidget *box = gtk_list_item_get_child(list_item);
    PhantomFileItem *item = gtk_list_item_get_item(list_item);

    GtkWidget *icon = gtk_widget_get_first_child(box);
    GtkWidget *name_label = gtk_widget_get_next_sibling(icon);
    GtkWidget *size_label = gtk_widget_get_next_sibling(name_label);
    GtkWidget *hash_label = gtk_widget_get_next_sibling(size_label);

    /* Set icon */
    gtk_image_set_from_icon_name(GTK_IMAGE(icon),
        item->is_dir ? "folder" : "text-x-generic");

    /* Set name */
    gtk_label_set_text(GTK_LABEL(name_label), item->name);

    /* Set size */
    char size_str[32];
    if (item->size < 1024) {
        snprintf(size_str, sizeof(size_str), "%lu B", item->size);
    } else if (item->size < 1024 * 1024) {
        snprintf(size_str, sizeof(size_str), "%.1f KB", item->size / 1024.0);
    } else {
        snprintf(size_str, sizeof(size_str), "%.1f MB", item->size / (1024.0 * 1024.0));
    }
    gtk_label_set_text(GTK_LABEL(size_label), size_str);

    /* Set hash (truncated) */
    char hash_short[20];
    snprintf(hash_short, sizeof(hash_short), "%.16s...", item->hash);
    gtk_label_set_text(GTK_LABEL(hash_label), hash_short);
}

/* ══════════════════════════════════════════════════════════════════════════════
 * WINDOW SETUP
 * ══════════════════════════════════════════════════════════════════════════════ */

static void build_ui(PhantomExplorer *explorer) {
    /* Create main window */
    explorer->window = GTK_WINDOW(gtk_application_window_new(explorer->app));
    gtk_window_set_title(explorer->window, "Phantom Explorer");
    gtk_window_set_default_size(explorer->window, 1200, 700);

    /* Header bar */
    explorer->header_bar = gtk_header_bar_new();
    gtk_window_set_titlebar(explorer->window, explorer->header_bar);

    /* Open button */
    GtkWidget *open_btn = gtk_button_new_from_icon_name("document-open");
    gtk_widget_set_tooltip_text(open_btn, "Open GeoFS Volume");
    g_signal_connect(open_btn, "clicked", G_CALLBACK(on_open_clicked), explorer);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(explorer->header_bar), open_btn);

    /* Up button */
    GtkWidget *up_btn = gtk_button_new_from_icon_name("go-up");
    gtk_widget_set_tooltip_text(up_btn, "Go Up");
    g_signal_connect(up_btn, "clicked", G_CALLBACK(on_up_clicked), explorer);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(explorer->header_bar), up_btn);

    /* View selector */
    explorer->view_selector = gtk_drop_down_new(NULL, NULL);
    gtk_widget_set_tooltip_text(explorer->view_selector, "Geological Stratum (View)");
    g_signal_connect(explorer->view_selector, "notify::selected",
                     G_CALLBACK(on_view_changed), explorer);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(explorer->header_bar), explorer->view_selector);

    GtkWidget *view_label = gtk_label_new("View:");
    gtk_header_bar_pack_end(GTK_HEADER_BAR(explorer->header_bar), view_label);

    /* Main content */
    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(explorer->window, main_box);

    /* Path bar */
    GtkWidget *path_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(path_box, 8);
    gtk_widget_set_margin_end(path_box, 8);
    gtk_widget_set_margin_top(path_box, 8);
    gtk_widget_set_margin_bottom(path_box, 8);
    gtk_box_append(GTK_BOX(main_box), path_box);

    GtkWidget *path_icon = gtk_image_new_from_icon_name("folder-open");
    gtk_box_append(GTK_BOX(path_box), path_icon);

    explorer->path_label = gtk_label_new("No volume open");
    gtk_label_set_xalign(GTK_LABEL(explorer->path_label), 0);
    gtk_widget_set_hexpand(explorer->path_label, TRUE);
    gtk_box_append(GTK_BOX(path_box), explorer->path_label);

    /* Paned view: file list | content */
    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_set_position(GTK_PANED(paned), 500);
    gtk_widget_set_vexpand(paned, TRUE);
    gtk_box_append(GTK_BOX(main_box), paned);

    /* File list */
    explorer->file_store = g_list_store_new(PHANTOM_TYPE_FILE_ITEM);

    GtkListItemFactory *factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory, "setup", G_CALLBACK(setup_file_item), explorer);
    g_signal_connect(factory, "bind", G_CALLBACK(bind_file_item), explorer);

    GtkSelectionModel *selection = GTK_SELECTION_MODEL(
        gtk_single_selection_new(G_LIST_MODEL(explorer->file_store)));

    explorer->file_list = gtk_list_view_new(selection, factory);
    g_signal_connect(explorer->file_list, "activate", G_CALLBACK(on_file_activated), explorer);

    GtkWidget *file_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(file_scroll), explorer->file_list);
    gtk_paned_set_start_child(GTK_PANED(paned), file_scroll);

    /* Content view */
    GtkWidget *content_frame = gtk_frame_new("Content");
    gtk_paned_set_end_child(GTK_PANED(paned), content_frame);

    GtkWidget *content_scroll = gtk_scrolled_window_new();
    gtk_frame_set_child(GTK_FRAME(content_frame), content_scroll);

    explorer->content_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(explorer->content_view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(explorer->content_view), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(explorer->content_view), GTK_WRAP_WORD_CHAR);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(content_scroll), explorer->content_view);

    /* Status bar */
    explorer->status_bar = gtk_label_new("Ready - \"To Create, Not To Destroy\"");
    gtk_label_set_xalign(GTK_LABEL(explorer->status_bar), 0);
    gtk_widget_set_margin_start(explorer->status_bar, 8);
    gtk_widget_set_margin_end(explorer->status_bar, 8);
    gtk_widget_set_margin_top(explorer->status_bar, 4);
    gtk_widget_set_margin_bottom(explorer->status_bar, 4);
    gtk_widget_add_css_class(explorer->status_bar, "dim-label");
    gtk_box_append(GTK_BOX(main_box), explorer->status_bar);

    /* Apply CSS */
    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_string(css,
        ".monospace { font-family: monospace; font-size: 0.9em; }"
        "window { background: @theme_bg_color; }"
    );
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(css),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);
}

/* ══════════════════════════════════════════════════════════════════════════════
 * APPLICATION LIFECYCLE
 * ══════════════════════════════════════════════════════════════════════════════ */

static void on_activate(GtkApplication *app, gpointer user_data) {
    PhantomExplorer *explorer = user_data;
    explorer->app = app;

    build_ui(explorer);

    gtk_window_present(explorer->window);
}

static void on_open(GApplication *app, GFile **files, int n_files, const char *hint, gpointer user_data) {
    (void)hint;
    PhantomExplorer *explorer = user_data;

    on_activate(GTK_APPLICATION(app), user_data);

    if (n_files > 0) {
        char *path = g_file_get_path(files[0]);
        if (path) {
            open_volume(explorer, path);
            g_free(path);
        }
    }
}

static void on_shutdown(GApplication *app, gpointer user_data) {
    (void)app;
    PhantomExplorer *explorer = user_data;

    if (explorer->volume) {
        geofs_volume_close(explorer->volume);
        explorer->volume = NULL;
    }
}

/* ══════════════════════════════════════════════════════════════════════════════
 * MAIN
 * ══════════════════════════════════════════════════════════════════════════════ */

int main(int argc, char **argv) {
    PhantomExplorer explorer = {0};
    strcpy(explorer.current_dir, "/");

    GtkApplication *app = gtk_application_new("org.phantom.explorer",
                                               G_APPLICATION_HANDLES_OPEN);

    g_signal_connect(app, "activate", G_CALLBACK(on_activate), &explorer);
    g_signal_connect(app, "open", G_CALLBACK(on_open), &explorer);
    g_signal_connect(app, "shutdown", G_CALLBACK(on_shutdown), &explorer);

    int status = g_application_run(G_APPLICATION(app), argc, argv);

    g_object_unref(app);

    return status;
}
