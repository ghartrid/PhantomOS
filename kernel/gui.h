/*
 * ══════════════════════════════════════════════════════════════════════════════
 *                            PHANTOM GUI
 *                     "To Create, Not To Destroy"
 * ══════════════════════════════════════════════════════════════════════════════
 *
 * A graphical user interface for PhantomOS that embodies the Phantom philosophy.
 * Built with GTK3 for maximum compatibility.
 *
 * Features:
 * - File browser with GeoFS integration (no delete button!)
 * - Process viewer (suspend/resume, not kill)
 * - Service manager
 * - Governor status and control
 * - Integrated terminal
 * - Geology viewer (time-travel through storage)
 */

#ifndef PHANTOM_GUI_H
#define PHANTOM_GUI_H

#include <gtk/gtk.h>
#include "phantom.h"
#include "vfs.h"
#include "init.h"
#include "governor.h"
#include "phantom_user.h"

/* ─────────────────────────────────────────────────────────────────────────────
 * GUI Context
 * ───────────────────────────────────────────────────────────────────────────── */

typedef struct phantom_gui {
    /* GTK widgets */
    GtkWidget *window;
    GtkWidget *header_bar;
    GtkWidget *main_paned;
    GtkWidget *sidebar;
    GtkWidget *content_stack;
    GtkWidget *status_bar;

    /* Panels */
    GtkWidget *file_browser;
    GtkWidget *process_viewer;
    GtkWidget *service_manager;
    GtkWidget *governor_panel;
    GtkWidget *geology_viewer;
    GtkWidget *terminal_panel;
    GtkWidget *constitution_view;

    /* File browser widgets */
    GtkWidget *file_tree;
    GtkListStore *file_store;
    GtkWidget *file_path_entry;
    GtkWidget *file_content_view;
    GtkWidget *file_back_btn;
    GtkWidget *file_forward_btn;
    GtkWidget *file_refresh_btn;
    GtkWidget *file_info_label;
    char current_path[4096];
    char history_back[10][4096];
    char history_forward[10][4096];
    int history_back_count;
    int history_forward_count;
    time_t last_file_refresh;

    /* Process viewer widgets */
    GtkWidget *process_tree;
    GtkListStore *process_store;
    GtkWidget *process_details;

    /* Service manager widgets */
    GtkWidget *service_tree;
    GtkListStore *service_store;
    GtkWidget *service_details;

    /* Governor widgets */
    GtkWidget *governor_status_label;
    GtkWidget *governor_mode_combo;
    GtkWidget *governor_stats_view;
    GtkWidget *governor_test_entry;
    GtkWidget *governor_test_result;

    /* Geology viewer widgets */
    GtkWidget *geology_timeline;
    GtkWidget *geology_content;
    GtkListStore *geology_store;

    /* Terminal widgets */
    GtkWidget *terminal_view;
    GtkTextBuffer *terminal_buffer;
    GtkWidget *terminal_entry;

    /* AI Assistant widgets */
    GtkWidget *ai_panel;
    GtkWidget *ai_chat_view;
    GtkTextBuffer *ai_chat_buffer;
    GtkWidget *ai_input_entry;
    GtkWidget *ai_status_label;
    GtkWidget *ai_mode_combo;

    /* Network panel widgets */
    GtkWidget *network_panel;
    GtkWidget *network_status_label;
    GtkWidget *network_tree;
    GtkListStore *network_store;
    GtkWidget *network_host_entry;
    GtkWidget *network_port_entry;

    /* Apps panel widgets */
    GtkWidget *apps_panel;
    GtkWidget *apps_notes_list;
    GtkListStore *apps_notes_store;
    GtkWidget *apps_note_content;
    GtkWidget *apps_note_title_entry;
    GtkWidget *apps_monitor_labels[8];  /* CPU, Mem, Geo, Net, Gov stats */
    GtkWidget *apps_web_url_entry;
    GtkWidget *apps_web_status;
    GtkWidget *apps_web_security_bar;   /* Large security indicator bar */
    GtkWidget *apps_web_security_icon;  /* Security icon label */
    GtkWidget *apps_web_security_text;  /* Security status text */
    GtkWidget *apps_web_progress;       /* Progress bar for loading */
    GtkWidget *apps_web_view;           /* WebKitWebView for rendering pages */

    /* Security panel widgets (Anti-Malware) */
    GtkWidget *security_panel;
    GtkWidget *security_status_label;
    GtkWidget *security_realtime_switch;
    GtkWidget *security_scan_progress;
    GtkWidget *security_scan_status;
    GtkWidget *security_scan_file_label;
    GtkWidget *security_results_tree;
    GtkListStore *security_results_store;
    GtkWidget *security_quarantine_tree;
    GtkListStore *security_quarantine_store;
    GtkWidget *security_stats_labels[4];  /* Files scanned, threats, quarantined, signatures */
    void *antimalware_scanner;  /* phantom_antimalware_t* */

    /* ArtOS widgets (Digital Art Studio) */
    GtkWidget *artos_panel;
    void *artos;  /* phantom_artos_t* */

    /* User Management widgets */
    GtkWidget *users_panel;
    GtkWidget *users_tree;
    GtkListStore *users_store;
    GtkWidget *users_details_label;
    GtkWidget *users_create_btn;
    GtkWidget *users_edit_btn;
    GtkWidget *users_disable_btn;
    GtkWidget *users_password_btn;

    /* DNAuth widgets (DNA-Based Authentication) */
    GtkWidget *dnauth_panel;
    GtkWidget *dnauth_tree;                /* User key list */
    GtkListStore *dnauth_store;
    GtkWidget *dnauth_status_label;        /* System status */
    GtkWidget *dnauth_details_label;       /* Selected key details */
    GtkWidget *dnauth_register_btn;
    GtkWidget *dnauth_evolve_btn;
    GtkWidget *dnauth_revoke_btn;
    GtkWidget *dnauth_test_btn;
    GtkWidget *dnauth_sequence_entry;      /* For testing sequences */
    GtkWidget *dnauth_mode_combo;          /* Auth mode selector */
    GtkWidget *dnauth_stats_labels[6];     /* Statistics display */
    void *dnauth_system;                   /* dnauth_system_t* */

    /* QRNet widgets (QR Code Distributed File Network) */
    GtkWidget *qrnet_panel;
    GtkWidget *qrnet_codes_tree;           /* QR codes list */
    GtkListStore *qrnet_codes_store;
    GtkWidget *qrnet_nodes_tree;           /* Network nodes list */
    GtkListStore *qrnet_nodes_store;
    GtkWidget *qrnet_status_label;         /* System status */
    GtkWidget *qrnet_details_label;        /* Selected code details */
    GtkWidget *qrnet_create_btn;
    GtkWidget *qrnet_verify_btn;
    GtkWidget *qrnet_revoke_btn;
    GtkWidget *qrnet_show_data_btn;
    GtkWidget *qrnet_export_btn;
    GtkWidget *qrnet_publish_btn;          /* Publish file to network */
    GtkWidget *qrnet_fetch_btn;            /* Fetch content by hash */
    GtkWidget *qrnet_path_entry;           /* Destination path entry */
    GtkWidget *qrnet_class_combo;          /* File class selector */
    GtkWidget *qrnet_stats_labels[6];      /* Statistics display */

    /* Desktop Lab widgets */
    GtkWidget *desktop_lab_panel;
    GtkWidget *widgets_tree;
    GtkListStore *widgets_store;
    GtkWidget *widget_preview;
    GtkWidget *widget_config_box;
    GtkWidget *experiments_tree;
    GtkListStore *experiments_store;
    GtkWidget *experiment_status_label;
    GtkWidget *experiment_output_view;
    GtkTextBuffer *experiment_output_buffer;

    /* Desktop Environment widgets */
    GtkWidget *desktop_panel;
    GtkWidget *desktop_area;               /* Main desktop drawing area */
    GtkWidget *desktop_taskbar;            /* Bottom taskbar */
    GtkWidget *desktop_app_menu;           /* Application menu */
    GtkWidget *desktop_clock_label;        /* Clock in taskbar */
    GtkWidget *desktop_governor_btn;       /* AI Governor quick access */
    GtkWidget *desktop_governor_status;    /* Governor status indicator */
    GtkWidget *desktop_ai_entry;           /* AI command input */
    GtkWidget *desktop_ai_response;        /* AI response area */
    GtkTextBuffer *desktop_ai_buffer;      /* AI conversation buffer */
    GtkWidget *desktop_wallpaper;          /* Wallpaper display */
    GtkWidget *desktop_icons_grid;         /* Desktop icons */
    guint desktop_clock_timer;             /* Clock update timer */

    /* PhantomPods widgets */
    GtkWidget *pods_panel;
    GtkWidget *pods_tree;
    GtkListStore *pods_store;
    GtkWidget *pods_details_box;
    GtkWidget *pods_status_label;
    GtkWidget *pods_apps_tree;
    GtkListStore *pods_apps_store;
    GtkWidget *pods_create_btn;
    GtkWidget *pods_activate_btn;
    GtkWidget *pods_dormant_btn;
    GtkWidget *pods_import_btn;
    GtkWidget *pods_run_btn;
    void *pod_system;                      /* phantom_pod_system_t* */

    /* MusiKey widgets (Musical Authentication) */
    GtkWidget *musikey_panel;
    GtkWidget *musikey_piano_area;         /* Piano keyboard drawing area */
    GtkWidget *musikey_visualizer_area;    /* Audio visualizer */
    GtkWidget *musikey_username_entry;     /* Username input */
    GtkWidget *musikey_passphrase_entry;   /* Passphrase input */
    GtkWidget *musikey_enroll_btn;         /* Enroll button */
    GtkWidget *musikey_auth_btn;           /* Authenticate button */
    GtkWidget *musikey_play_btn;           /* Play preview button */
    GtkWidget *musikey_status_label;       /* Status display */
    GtkWidget *musikey_entropy_label;      /* Entropy bits display */
    GtkWidget *musikey_users_tree;         /* Enrolled users list */
    GtkListStore *musikey_users_store;     /* User data store */
    void *musikey_system;                  /* MusiKey context */
    void *musikey_current_song;            /* Current song for playback */
    guint musikey_anim_timer;              /* Animation timer */
    float musikey_piano_highlights[25];    /* Piano key highlight values */
    float musikey_vis_bars[32];            /* Visualizer bar heights */
    int musikey_playing;                   /* Playback state */

    /* Backup utility widgets */
    GtkWidget *backup_panel;
    GtkWidget *backup_tree;
    GtkListStore *backup_store;
    GtkWidget *backup_progress;
    GtkWidget *backup_status_label;
    GtkWidget *backup_items_tree;
    GtkListStore *backup_items_store;
    GtkWidget *backup_quick_full_btn;
    GtkWidget *backup_quick_geofs_btn;
    GtkWidget *backup_custom_btn;
    GtkWidget *backup_restore_btn;
    GtkWidget *backup_verify_btn;
    GtkWidget *backup_size_label;
    void *backup_system;                   /* phantom_backup_system_t* */

    /* Media player widgets */
    GtkWidget *media_panel;
    GtkWidget *media_video_area;           /* Drawing area for video */
    GtkWidget *media_album_art;            /* Album art display */
    GtkWidget *media_track_label;          /* Current track title */
    GtkWidget *media_artist_label;         /* Artist name */
    GtkWidget *media_album_label;          /* Album name */
    GtkWidget *media_time_label;           /* Current time / duration */
    GtkWidget *media_position_scale;       /* Seek bar */
    GtkWidget *media_volume_scale;         /* Volume slider */
    GtkWidget *media_play_btn;             /* Play/pause button */
    GtkWidget *media_shuffle_btn;          /* Shuffle toggle */
    GtkWidget *media_repeat_btn;           /* Repeat mode toggle */
    GtkWidget *media_playlist_tree;        /* Playlist view */
    GtkListStore *media_playlist_store;    /* Playlist data */
    GtkWidget *media_eq_scales[10];        /* Equalizer bands */
    GtkWidget *media_eq_preset_combo;      /* EQ presets */
    void *mediaplayer;                     /* phantom_mediaplayer_t* */
    guint media_update_timer;              /* Position update timer */

    /* Kernel references */
    struct phantom_kernel *kernel;
    struct vfs_context *vfs;

    /* User authentication */
    phantom_user_system_t *user_system;
    phantom_session_t *session;
    uint32_t uid;
    char username[PHANTOM_MAX_USERNAME];
    int logged_in;

    /* State */
    int running;
    guint refresh_timer;

    /* Storage manager */
    void *storage_manager;  /* phantom_storage_manager_t* */
    GtkWidget *storage_indicator;
    int last_storage_warning;

} phantom_gui_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * GUI API
 * ───────────────────────────────────────────────────────────────────────────── */

/* Lifecycle */
int phantom_gui_init(phantom_gui_t *gui,
                     struct phantom_kernel *kernel,
                     struct vfs_context *vfs);
void phantom_gui_set_user_system(phantom_gui_t *gui, phantom_user_system_t *user_sys);
int phantom_gui_login(phantom_gui_t *gui);  /* Show login dialog, returns 0 on success */
void phantom_gui_run(phantom_gui_t *gui);
void phantom_gui_shutdown(phantom_gui_t *gui);

/* Panel creation */
GtkWidget *phantom_gui_create_file_browser(phantom_gui_t *gui);
GtkWidget *phantom_gui_create_process_viewer(phantom_gui_t *gui);
GtkWidget *phantom_gui_create_service_manager(phantom_gui_t *gui);
GtkWidget *phantom_gui_create_governor_panel(phantom_gui_t *gui);
GtkWidget *phantom_gui_create_geology_viewer(phantom_gui_t *gui);
GtkWidget *phantom_gui_create_terminal(phantom_gui_t *gui);
GtkWidget *phantom_gui_create_constitution_view(phantom_gui_t *gui);
GtkWidget *phantom_gui_create_ai_panel(phantom_gui_t *gui);
GtkWidget *phantom_gui_create_network_panel(phantom_gui_t *gui);
GtkWidget *phantom_gui_create_apps_panel(phantom_gui_t *gui);
GtkWidget *phantom_gui_create_security_panel(phantom_gui_t *gui);
GtkWidget *phantom_gui_create_media_panel(phantom_gui_t *gui);
GtkWidget *phantom_gui_create_artos_panel(phantom_gui_t *gui);
GtkWidget *phantom_gui_create_users_panel(phantom_gui_t *gui);
GtkWidget *phantom_gui_create_dnauth_panel(phantom_gui_t *gui);
GtkWidget *phantom_gui_create_qrnet_panel(phantom_gui_t *gui);
GtkWidget *phantom_gui_create_desktop_lab_panel(phantom_gui_t *gui);
GtkWidget *phantom_gui_create_desktop_panel(phantom_gui_t *gui);
GtkWidget *phantom_gui_create_pods_panel(phantom_gui_t *gui);
GtkWidget *phantom_gui_create_backup_panel(phantom_gui_t *gui);
GtkWidget *phantom_gui_create_musikey_panel(phantom_gui_t *gui);

/* Refresh functions */
void phantom_gui_refresh_files(phantom_gui_t *gui);
void phantom_gui_refresh_processes(phantom_gui_t *gui);
void phantom_gui_refresh_services(phantom_gui_t *gui);
void phantom_gui_refresh_governor(phantom_gui_t *gui);
void phantom_gui_refresh_geology(phantom_gui_t *gui);
void phantom_gui_refresh_network(phantom_gui_t *gui);
void phantom_gui_refresh_users(phantom_gui_t *gui);
void phantom_gui_refresh_dnauth(phantom_gui_t *gui);
void phantom_gui_refresh_qrnet(phantom_gui_t *gui);
void phantom_gui_refresh_desktop_lab(phantom_gui_t *gui);
void phantom_gui_refresh_desktop(phantom_gui_t *gui);
void phantom_gui_refresh_pods(phantom_gui_t *gui);
void phantom_gui_refresh_backup(phantom_gui_t *gui);
void phantom_gui_refresh_musikey(phantom_gui_t *gui);

/* File browser actions */
void phantom_gui_navigate_to(phantom_gui_t *gui, const char *path);
void phantom_gui_create_file(phantom_gui_t *gui, const char *name);
void phantom_gui_create_folder(phantom_gui_t *gui, const char *name);
void phantom_gui_hide_file(phantom_gui_t *gui, const char *path);
void phantom_gui_view_file(phantom_gui_t *gui, const char *path);

/* Process actions */
void phantom_gui_suspend_process(phantom_gui_t *gui, phantom_pid_t pid);
void phantom_gui_resume_process(phantom_gui_t *gui, phantom_pid_t pid);

/* Service actions */
void phantom_gui_awaken_service(phantom_gui_t *gui, const char *name);
void phantom_gui_rest_service(phantom_gui_t *gui, const char *name);

/* Governor actions */
void phantom_gui_set_governor_mode(phantom_gui_t *gui, const char *mode);
void phantom_gui_test_code(phantom_gui_t *gui, const char *code);

/* Terminal */
void phantom_gui_terminal_write(phantom_gui_t *gui, const char *text);
void phantom_gui_terminal_execute(phantom_gui_t *gui, const char *command);

/* Utility */
void phantom_gui_show_message(phantom_gui_t *gui, const char *title,
                               const char *message, GtkMessageType type);
void phantom_gui_update_status(phantom_gui_t *gui, const char *status);

/* ─────────────────────────────────────────────────────────────────────────────
 * File Store Columns
 * ───────────────────────────────────────────────────────────────────────────── */

enum {
    FILE_COL_ICON,
    FILE_COL_NAME,
    FILE_COL_TYPE,
    FILE_COL_SIZE,
    FILE_COL_PATH,
    FILE_COL_COUNT
};

/* ─────────────────────────────────────────────────────────────────────────────
 * Process Store Columns
 * ───────────────────────────────────────────────────────────────────────────── */

enum {
    PROC_COL_PID,
    PROC_COL_NAME,
    PROC_COL_STATE,
    PROC_COL_PRIORITY,
    PROC_COL_MEMORY,
    PROC_COL_COUNT
};

/* ─────────────────────────────────────────────────────────────────────────────
 * Service Store Columns
 * ───────────────────────────────────────────────────────────────────────────── */

enum {
    SVC_COL_ICON,
    SVC_COL_NAME,
    SVC_COL_STATE,
    SVC_COL_TYPE,
    SVC_COL_DESC,
    SVC_COL_COUNT
};

/* ─────────────────────────────────────────────────────────────────────────────
 * Geology Store Columns (File History)
 * ───────────────────────────────────────────────────────────────────────────── */

enum {
    GEO_COL_PATH,       /* File path */
    GEO_COL_OPERATION,  /* Operation type: Created, Modified, Hidden */
    GEO_COL_TIMESTAMP,  /* Timestamp */
    GEO_COL_SIZE,       /* File size */
    GEO_COL_VIEW_ID,    /* View ID */
    GEO_COL_COUNT
};

/* ─────────────────────────────────────────────────────────────────────────────
 * Network Store Columns
 * ───────────────────────────────────────────────────────────────────────────── */

enum {
    NET_COL_ID,
    NET_COL_STATE,
    NET_COL_TYPE,
    NET_COL_LOCAL,
    NET_COL_REMOTE,
    NET_COL_SENT,
    NET_COL_RECV,
    NET_COL_COUNT
};

/* ─────────────────────────────────────────────────────────────────────────────
 * Media Playlist Store Columns
 * ───────────────────────────────────────────────────────────────────────────── */

enum {
    MEDIA_COL_INDEX,
    MEDIA_COL_PLAYING,    /* Currently playing indicator */
    MEDIA_COL_TITLE,
    MEDIA_COL_ARTIST,
    MEDIA_COL_DURATION,
    MEDIA_COL_PATH,
    MEDIA_COL_COUNT
};

/* ─────────────────────────────────────────────────────────────────────────────
 * User Store Columns
 * ───────────────────────────────────────────────────────────────────────────── */

enum {
    USER_COL_ICON,
    USER_COL_USERNAME,
    USER_COL_FULLNAME,
    USER_COL_STATE,
    USER_COL_UID,
    USER_COL_PERMISSIONS,
    USER_COL_LAST_LOGIN,
    USER_COL_COUNT
};

/* ─────────────────────────────────────────────────────────────────────────────
 * DNAuth Store Columns (DNA-Based Authentication)
 * ───────────────────────────────────────────────────────────────────────────── */

enum {
    DNAUTH_COL_ICON,
    DNAUTH_COL_USER_ID,
    DNAUTH_COL_MODE,
    DNAUTH_COL_GENERATION,
    DNAUTH_COL_FITNESS,
    DNAUTH_COL_STATE,
    DNAUTH_COL_LAST_AUTH,
    DNAUTH_COL_COUNT
};

/* ─────────────────────────────────────────────────────────────────────────────
 * QRNet Store Columns (QR Code Distributed File Network)
 * ───────────────────────────────────────────────────────────────────────────── */

enum {
    QRNET_COL_ICON,
    QRNET_COL_CODE_ID,
    QRNET_COL_DESTINATION,
    QRNET_COL_FILE_CLASS,
    QRNET_COL_STATE,
    QRNET_COL_CREATOR,
    QRNET_COL_CREATED,
    QRNET_COL_COUNT
};

enum {
    QRNET_NODE_COL_ICON,
    QRNET_NODE_COL_ID,
    QRNET_NODE_COL_TRUST,
    QRNET_NODE_COL_STATE,
    QRNET_NODE_COL_LAST_SYNC,
    QRNET_NODE_COL_COUNT
};

/* ─────────────────────────────────────────────────────────────────────────────
 * Desktop Lab Widget Store Columns
 * ───────────────────────────────────────────────────────────────────────────── */

enum {
    WIDGET_COL_ICON,
    WIDGET_COL_NAME,
    WIDGET_COL_TYPE,
    WIDGET_COL_STATE,
    WIDGET_COL_DESCRIPTION,
    WIDGET_COL_COUNT
};

/* ─────────────────────────────────────────────────────────────────────────────
 * Desktop Lab Experiment Store Columns
 * ───────────────────────────────────────────────────────────────────────────── */

enum {
    EXPERIMENT_COL_ICON,
    EXPERIMENT_COL_NAME,
    EXPERIMENT_COL_STATUS,
    EXPERIMENT_COL_CATEGORY,
    EXPERIMENT_COL_RISK_LEVEL,
    EXPERIMENT_COL_DESCRIPTION,
    EXPERIMENT_COL_COUNT
};

/* ─────────────────────────────────────────────────────────────────────────────
 * PhantomPods Store Columns
 * ───────────────────────────────────────────────────────────────────────────── */

enum {
    POD_COL_ICON,
    POD_COL_NAME,
    POD_COL_TYPE,
    POD_COL_STATE,
    POD_COL_APPS,
    POD_COL_SECURITY,
    POD_COL_ID,
    POD_COL_COUNT
};

enum {
    POD_APP_COL_ICON,
    POD_APP_COL_NAME,
    POD_APP_COL_PATH,
    POD_APP_COL_RUNS,
    POD_APP_COL_COUNT
};

enum {
    BACKUP_COL_NAME,
    BACKUP_COL_TYPE,
    BACKUP_COL_DATE,
    BACKUP_COL_SIZE,
    BACKUP_COL_STATE,
    BACKUP_COL_ID,
    BACKUP_COL_COUNT
};

enum {
    BACKUP_ITEM_COL_ENABLED,
    BACKUP_ITEM_COL_NAME,
    BACKUP_ITEM_COL_PATH,
    BACKUP_ITEM_COL_SIZE,
    BACKUP_ITEM_COL_COUNT
};

#endif /* PHANTOM_GUI_H */
