/*
 * ==============================================================================
 *                                 ARTOS
 *                    Digital Art Studio for PhantomOS
 *                       "To Create, Not To Destroy"
 * ==============================================================================
 *
 * ArtOS is the integrated digital art component of PhantomOS, providing
 * a full-featured painting and drawing environment that respects the
 * Phantom philosophy - every stroke is preserved in geological layers.
 *
 * Features:
 * - Multi-layer canvas with unlimited undo (geological history)
 * - Multiple brush types (pencil, pen, brush, airbrush, eraser*)
 * - Color picker with palette support
 * - Shape tools (rectangle, ellipse, line, polygon)
 * - Selection and transform tools
 * - Text tool
 * - Filters and effects
 * - Export to PNG, JPEG, SVG
 *
 * * Note: "Eraser" in Phantom philosophy paints with transparency,
 *         the original strokes remain in history
 */

#ifndef PHANTOM_ARTOS_H
#define PHANTOM_ARTOS_H

#include <gtk/gtk.h>
#include <cairo.h>
#include <stdint.h>

/* Forward declaration */
struct phantom_artos;
typedef struct phantom_artos phantom_artos_t;

/* ==============================================================================
 * Constants
 * ============================================================================== */

#define ARTOS_MAX_LAYERS        64
#define ARTOS_MAX_UNDO          1000    /* Unlimited undo via geological layers */
#define ARTOS_MAX_BRUSHES       32
#define ARTOS_MAX_PALETTE       256
#define ARTOS_DEFAULT_WIDTH     1920
#define ARTOS_DEFAULT_HEIGHT    1080
#define ARTOS_DICTATION_MAX_CMD 256
#define ARTOS_DICTATION_HISTORY 100

/* ==============================================================================
 * Types and Enumerations
 * ============================================================================== */

/* Color with alpha - defined early for use in dictation structures */
typedef struct artos_color {
    double r;       /* 0.0 - 1.0 */
    double g;
    double b;
    double a;
} artos_color_t;

/* Tool types */
typedef enum {
    ARTOS_TOOL_PENCIL,          /* Hard-edged freehand */
    ARTOS_TOOL_PEN,             /* Smooth anti-aliased line */
    ARTOS_TOOL_BRUSH,           /* Soft brush with opacity */
    ARTOS_TOOL_AIRBRUSH,        /* Spray paint effect */
    ARTOS_TOOL_ERASER,          /* Paints transparency (preserves history) */
    ARTOS_TOOL_BUCKET,          /* Fill tool */
    ARTOS_TOOL_GRADIENT,        /* Gradient fill */
    ARTOS_TOOL_EYEDROPPER,      /* Color picker from canvas */
    ARTOS_TOOL_LINE,            /* Straight line */
    ARTOS_TOOL_RECTANGLE,       /* Rectangle shape */
    ARTOS_TOOL_ELLIPSE,         /* Ellipse/circle shape */
    ARTOS_TOOL_POLYGON,         /* Polygon shape */
    ARTOS_TOOL_TEXT,            /* Text insertion */
    ARTOS_TOOL_SELECT_RECT,     /* Rectangular selection */
    ARTOS_TOOL_SELECT_FREE,     /* Freehand selection */
    ARTOS_TOOL_SELECT_WAND,     /* Magic wand selection */
    ARTOS_TOOL_MOVE,            /* Move selection/layer */
    ARTOS_TOOL_ZOOM,            /* Zoom in/out */
    ARTOS_TOOL_PAN,             /* Pan/scroll canvas */
    ARTOS_TOOL_SMUDGE,          /* Smudge/blur tool */
    ARTOS_TOOL_CLONE,           /* Clone stamp */
    ARTOS_TOOL_COUNT
} artos_tool_t;

/* Brush shape */
typedef enum {
    ARTOS_BRUSH_ROUND,
    ARTOS_BRUSH_SQUARE,
    ARTOS_BRUSH_DIAMOND,
    ARTOS_BRUSH_CUSTOM
} artos_brush_shape_t;

/* Blend modes */
typedef enum {
    ARTOS_BLEND_NORMAL,
    ARTOS_BLEND_MULTIPLY,
    ARTOS_BLEND_SCREEN,
    ARTOS_BLEND_OVERLAY,
    ARTOS_BLEND_DARKEN,
    ARTOS_BLEND_LIGHTEN,
    ARTOS_BLEND_COLOR_DODGE,
    ARTOS_BLEND_COLOR_BURN,
    ARTOS_BLEND_HARD_LIGHT,
    ARTOS_BLEND_SOFT_LIGHT,
    ARTOS_BLEND_DIFFERENCE,
    ARTOS_BLEND_EXCLUSION,
    ARTOS_BLEND_HUE,
    ARTOS_BLEND_SATURATION,
    ARTOS_BLEND_COLOR,
    ARTOS_BLEND_LUMINOSITY,
    ARTOS_BLEND_COUNT
} artos_blend_mode_t;

/* Dictation command types */
typedef enum {
    ARTOS_DICT_CMD_NONE,
    /* Shape commands */
    ARTOS_DICT_CMD_DRAW_LINE,       /* "draw a line from X to Y" */
    ARTOS_DICT_CMD_DRAW_RECT,       /* "draw a rectangle at X, Y" */
    ARTOS_DICT_CMD_DRAW_CIRCLE,     /* "draw a circle at center" */
    ARTOS_DICT_CMD_DRAW_ELLIPSE,    /* "draw an ellipse" */
    ARTOS_DICT_CMD_DRAW_TRIANGLE,   /* "draw a triangle" */
    ARTOS_DICT_CMD_DRAW_STAR,       /* "draw a star" */
    ARTOS_DICT_CMD_DRAW_ARROW,      /* "draw an arrow" */
    ARTOS_DICT_CMD_DRAW_HEART,      /* "draw a heart" */
    ARTOS_DICT_CMD_DRAW_SPIRAL,     /* "draw a spiral" */
    /* Color commands */
    ARTOS_DICT_CMD_SET_COLOR,       /* "set color to red" */
    ARTOS_DICT_CMD_SET_FILL,        /* "fill with blue" */
    /* Size commands */
    ARTOS_DICT_CMD_SET_SIZE,        /* "set brush size to 20" */
    ARTOS_DICT_CMD_BIGGER,          /* "make it bigger" */
    ARTOS_DICT_CMD_SMALLER,         /* "make it smaller" */
    /* Tool commands */
    ARTOS_DICT_CMD_USE_BRUSH,       /* "use brush" */
    ARTOS_DICT_CMD_USE_PENCIL,      /* "use pencil" */
    ARTOS_DICT_CMD_USE_ERASER,      /* "use eraser" */
    /* Action commands */
    ARTOS_DICT_CMD_UNDO,            /* "undo" */
    ARTOS_DICT_CMD_REDO,            /* "redo" */
    ARTOS_DICT_CMD_CLEAR,           /* "clear canvas" */
    ARTOS_DICT_CMD_NEW_LAYER,       /* "new layer" */
    /* Position commands */
    ARTOS_DICT_CMD_MOVE_TO,         /* "move to center" */
    ARTOS_DICT_CMD_GO_LEFT,         /* "go left" */
    ARTOS_DICT_CMD_GO_RIGHT,        /* "go right" */
    ARTOS_DICT_CMD_GO_UP,           /* "go up" */
    ARTOS_DICT_CMD_GO_DOWN,         /* "go down" */
    /* Continuous drawing */
    ARTOS_DICT_CMD_START_DRAWING,   /* "start drawing" */
    ARTOS_DICT_CMD_STOP_DRAWING,    /* "stop drawing" / "pen up" */
    ARTOS_DICT_CMD_COUNT
} artos_dictation_cmd_t;

/* Position reference for dictation */
typedef enum {
    ARTOS_POS_ABSOLUTE,     /* Absolute coordinates */
    ARTOS_POS_CENTER,       /* Center of canvas */
    ARTOS_POS_TOP_LEFT,
    ARTOS_POS_TOP_RIGHT,
    ARTOS_POS_BOTTOM_LEFT,
    ARTOS_POS_BOTTOM_RIGHT,
    ARTOS_POS_TOP,
    ARTOS_POS_BOTTOM,
    ARTOS_POS_LEFT,
    ARTOS_POS_RIGHT,
    ARTOS_POS_CURSOR,       /* Current cursor/pen position */
    ARTOS_POS_RELATIVE      /* Relative to current position */
} artos_position_ref_t;

/* Parsed dictation command */
typedef struct artos_dictation_parsed {
    artos_dictation_cmd_t command;
    char raw_text[ARTOS_DICTATION_MAX_CMD];

    /* Shape parameters */
    artos_position_ref_t pos_ref;
    double x1, y1;          /* Start/center position */
    double x2, y2;          /* End position (for lines) */
    double width, height;   /* Size for shapes */
    double radius;          /* For circles */
    int filled;             /* Fill shape or stroke */
    int points;             /* For stars/polygons */

    /* Color parameters */
    artos_color_t color;
    int has_color;

    /* Size parameters */
    double size;
    int has_size;

    /* Tool */
    artos_tool_t tool;
    int has_tool;

    /* Movement */
    double move_amount;

    /* Confidence score (0.0 - 1.0) */
    double confidence;

    /* Error message if parsing failed */
    char error[128];
    int success;
} artos_dictation_parsed_t;

/* Dictation history entry */
typedef struct artos_dictation_entry {
    char command[ARTOS_DICTATION_MAX_CMD];
    artos_dictation_cmd_t type;
    time_t timestamp;
    int executed;
} artos_dictation_entry_t;

/* Dictation state */
typedef struct artos_dictation {
    /* Current state */
    int enabled;
    int listening;
    int continuous_draw;    /* Pen down mode */

    /* Current pen position (for relative drawing) */
    double pen_x;
    double pen_y;

    /* Default shape parameters */
    double default_size;
    int default_filled;

    /* Command history */
    artos_dictation_entry_t history[ARTOS_DICTATION_HISTORY];
    int history_count;
    int history_index;

    /* Voice input buffer */
    char input_buffer[ARTOS_DICTATION_MAX_CMD];

    /* Feedback */
    char last_feedback[256];
    int show_feedback;
    guint feedback_timer;

    /* Named colors lookup */
    struct {
        const char *name;
        artos_color_t color;
    } color_names[64];
    int color_count;

    /* Voice recognition (GStreamer pipeline) */
    void *voice_pipeline;       /* GstElement* */
    void *voice_source;         /* GstElement* - audio source */
    void *voice_convert;        /* GstElement* - audio converter */
    void *voice_resample;       /* GstElement* - audio resampler */
    void *voice_sink;           /* GstElement* - app sink for audio */
    int voice_initialized;
    int voice_recording;
    guint voice_timeout;        /* Silence detection timeout */

    /* Audio level monitoring */
    double audio_level;
    guint level_update_timer;
} artos_dictation_t;

/* Face tracking mode */
typedef enum {
    ARTOS_FACE_MODE_NOSE,       /* Track nose tip for drawing */
    ARTOS_FACE_MODE_HEAD,       /* Track head center */
    ARTOS_FACE_MODE_EYES,       /* Track eye gaze direction */
    ARTOS_FACE_MODE_MOUTH       /* Track mouth for gesture control */
} artos_face_mode_t;

/* Face tracking gesture */
typedef enum {
    ARTOS_FACE_GESTURE_NONE,
    ARTOS_FACE_GESTURE_BLINK_LEFT,      /* Left eye blink */
    ARTOS_FACE_GESTURE_BLINK_RIGHT,     /* Right eye blink */
    ARTOS_FACE_GESTURE_BLINK_BOTH,      /* Both eyes blink */
    ARTOS_FACE_GESTURE_MOUTH_OPEN,      /* Open mouth */
    ARTOS_FACE_GESTURE_SMILE,           /* Smile detected */
    ARTOS_FACE_GESTURE_RAISE_EYEBROWS,  /* Eyebrows raised */
    ARTOS_FACE_GESTURE_NOD,             /* Head nod */
    ARTOS_FACE_GESTURE_SHAKE            /* Head shake */
} artos_face_gesture_t;

/* Face tracking state */
typedef struct artos_facetrack {
    /* Enable state */
    int enabled;
    int tracking;
    int drawing;                /* Pen down state */

    /* Tracking mode */
    artos_face_mode_t mode;

    /* Current face position (normalized 0.0 - 1.0) */
    double face_x;
    double face_y;

    /* Mapped canvas position */
    double canvas_x;
    double canvas_y;
    double last_canvas_x;
    double last_canvas_y;

    /* Tracking zone (screen area mapped to canvas) */
    double zone_x1, zone_y1;    /* Top-left of tracking zone */
    double zone_x2, zone_y2;    /* Bottom-right of tracking zone */

    /* Smoothing */
    double smoothing;           /* 0.0 = no smoothing, 1.0 = max smoothing */
    double smooth_x, smooth_y;  /* Smoothed position */

    /* Sensitivity */
    double sensitivity;         /* Movement multiplier */

    /* Gesture detection */
    artos_face_gesture_t last_gesture;
    int gesture_cooldown;       /* Frames to wait between gestures */

    /* Gesture actions */
    int blink_to_draw;          /* Blink toggles pen up/down */
    int mouth_to_draw;          /* Open mouth toggles pen up/down */
    int smile_to_undo;          /* Smile triggers undo */

    /* Face detection subprocess */
    GPid child_pid;
    int stdout_fd;
    void *stdout_channel;       /* GIOChannel* */
    guint stdout_watch;
    guint update_timer;

    /* Calibration */
    int calibrating;
    int calibration_step;
    double calib_points[4][2];  /* 4 corner calibration points */

    /* Statistics */
    int frames_processed;
    double fps;
    time_t start_time;

    /* Webcam preview */
    int show_preview;
    unsigned char *preview_data;
    int preview_width;
    int preview_height;
} artos_facetrack_t;

/* ==============================================================================
 * AI-Assisted Drawing
 * ============================================================================== */

/* AI assistance mode */
typedef enum {
    ARTOS_AI_MODE_OFF,              /* No AI assistance */
    ARTOS_AI_MODE_SUGGEST,          /* Show suggestions, user accepts */
    ARTOS_AI_MODE_AUTO_COMPLETE,    /* Auto-complete strokes */
    ARTOS_AI_MODE_STYLE_TRANSFER,   /* Apply style from reference */
    ARTOS_AI_MODE_GENERATE          /* Generate from prompt */
} artos_ai_mode_t;

/* AI suggestion type */
typedef enum {
    ARTOS_AI_SUGGEST_STROKE,        /* Complete the current stroke */
    ARTOS_AI_SUGGEST_SHAPE,         /* Recognize and perfect shape */
    ARTOS_AI_SUGGEST_COLOR,         /* Suggest harmonious colors */
    ARTOS_AI_SUGGEST_COMPOSITION,   /* Suggest layout improvements */
    ARTOS_AI_SUGGEST_STYLE          /* Apply artistic style */
} artos_ai_suggest_t;

/* AI stroke prediction point */
typedef struct artos_ai_point {
    double x, y;
    double confidence;
} artos_ai_point_t;

/* AI suggestion */
typedef struct artos_ai_suggestion {
    artos_ai_suggest_t type;
    char description[256];

    /* For stroke suggestions */
    artos_ai_point_t *points;
    int point_count;

    /* For shape recognition */
    char shape_name[32];
    double shape_params[8];         /* Shape-specific parameters */

    /* For color suggestions */
    artos_color_t colors[8];
    int color_count;

    /* Confidence score */
    double confidence;

    /* Preview surface */
    cairo_surface_t *preview;

    struct artos_ai_suggestion *next;
} artos_ai_suggestion_t;

/* AI assistant state */
typedef struct artos_ai_assist {
    int enabled;
    artos_ai_mode_t mode;

    /* Current stroke being analyzed */
    artos_ai_point_t *stroke_buffer;
    int stroke_count;
    int stroke_capacity;

    /* Pending suggestions */
    artos_ai_suggestion_t *suggestions;
    int suggestion_count;
    int selected_suggestion;

    /* Shape recognition */
    int shape_recognition;
    double shape_tolerance;         /* How close to perfect shape */

    /* Style transfer */
    cairo_surface_t *style_reference;
    char style_name[64];
    double style_strength;          /* 0.0 - 1.0 */

    /* Generation prompt */
    char prompt[512];
    int generating;

    /* AI backend (subprocess) */
    GPid ai_pid;
    int ai_stdin_fd;
    int ai_stdout_fd;
    void *ai_stdout_channel;
    guint ai_watch;

    /* Settings */
    int auto_suggest;               /* Show suggestions automatically */
    int suggest_delay_ms;           /* Delay before showing suggestions */
    guint suggest_timer;
} artos_ai_assist_t;

/* ==============================================================================
 * Voice-to-Art Generation
 * ============================================================================== */

/* Voice-to-art state */
typedef struct artos_voice_art {
    int enabled;
    int listening;
    int generating;

    /* Voice input */
    char transcript[1024];
    double audio_level;

    /* Generation settings */
    char style_preset[64];          /* "realistic", "cartoon", "abstract", etc. */
    int width, height;
    double creativity;              /* 0.0 = literal, 1.0 = creative */

    /* Generated images */
    cairo_surface_t *generated[4];  /* Up to 4 variations */
    int generated_count;
    int selected_image;

    /* History */
    struct {
        char prompt[256];
        cairo_surface_t *thumbnail;
        time_t timestamp;
    } history[20];
    int history_count;

    /* Backend process */
    GPid gen_pid;
    int gen_stdout_fd;
    void *gen_channel;
    guint gen_watch;

    /* Progress */
    double progress;
    char status[128];
} artos_voice_art_t;

/* ==============================================================================
 * Collaborative Canvas
 * ============================================================================== */

/* Collaboration user */
typedef struct artos_collab_user {
    uint32_t user_id;
    char name[64];
    char avatar_url[256];
    artos_color_t cursor_color;

    /* Current cursor position */
    double cursor_x, cursor_y;
    int is_drawing;

    /* Currently selected tool/color */
    artos_tool_t tool;
    artos_color_t color;
    double brush_size;

    /* Connection status */
    int connected;
    time_t last_seen;

    struct artos_collab_user *next;
} artos_collab_user_t;

/* Collaboration operation (for sync) */
typedef enum {
    ARTOS_COLLAB_OP_STROKE,         /* Draw stroke */
    ARTOS_COLLAB_OP_FILL,           /* Fill area */
    ARTOS_COLLAB_OP_ERASE,          /* Erase */
    ARTOS_COLLAB_OP_UNDO,           /* Undo operation */
    ARTOS_COLLAB_OP_REDO,           /* Redo operation */
    ARTOS_COLLAB_OP_LAYER_ADD,      /* Add layer */
    ARTOS_COLLAB_OP_LAYER_DELETE,   /* Delete layer */
    ARTOS_COLLAB_OP_LAYER_MOVE,     /* Reorder layer */
    ARTOS_COLLAB_OP_CURSOR_MOVE,    /* Cursor position update */
    ARTOS_COLLAB_OP_CHAT            /* Chat message */
} artos_collab_op_t;

/* Collaboration message */
typedef struct artos_collab_msg {
    artos_collab_op_t op;
    uint32_t user_id;
    uint64_t timestamp;
    uint32_t seq_num;               /* Sequence number for ordering */

    /* Operation data (varies by op type) */
    union {
        struct {
            artos_ai_point_t *points;
            int point_count;
            artos_color_t color;
            double brush_size;
            int layer_index;
        } stroke;

        struct {
            double x, y;
            artos_color_t color;
            int layer_index;
        } fill;

        struct {
            double x, y;
        } cursor;

        struct {
            char text[256];
        } chat;

        struct {
            int layer_index;
            int new_index;
        } layer;
    } data;

    struct artos_collab_msg *next;
} artos_collab_msg_t;

/* Collaboration state */
typedef struct artos_collab {
    int enabled;
    int connected;
    int is_host;

    /* Session info */
    char session_id[64];
    char session_name[128];
    char password[64];

    /* Local user */
    uint32_t local_user_id;
    char local_name[64];

    /* Connected users */
    artos_collab_user_t *users;
    int user_count;

    /* Message queue */
    artos_collab_msg_t *outgoing;
    artos_collab_msg_t *incoming;
    uint32_t local_seq;
    uint32_t remote_seq;

    /* Network */
    int socket_fd;
    char server_host[256];
    int server_port;
    void *socket_channel;
    guint socket_watch;

    /* Chat history */
    struct {
        uint32_t user_id;
        char name[64];
        char message[256];
        time_t timestamp;
    } chat_history[100];
    int chat_count;

    /* Conflict resolution */
    int use_crdt;                   /* Use CRDT for conflict-free sync */

    /* Statistics */
    int ops_sent;
    int ops_received;
    double latency_ms;
} artos_collab_t;

/* ==============================================================================
 * DrawNet - Real-time Multi-User Drawing Network
 * ============================================================================== */

/* DrawNet connection state */
typedef enum {
    DRAWNET_STATE_DISCONNECTED,
    DRAWNET_STATE_DISCOVERING,      /* Scanning for peers */
    DRAWNET_STATE_CONNECTING,       /* Establishing connection */
    DRAWNET_STATE_CONNECTED,        /* Active session */
    DRAWNET_STATE_SYNCING,          /* Synchronizing canvas */
    DRAWNET_STATE_ERROR
} artos_drawnet_state_t;

/* DrawNet peer discovery method */
typedef enum {
    DRAWNET_DISCOVER_LOCAL,         /* Local network mDNS/Avahi */
    DRAWNET_DISCOVER_DIRECT,        /* Direct IP connection */
    DRAWNET_DISCOVER_RELAY,         /* Via relay server */
    DRAWNET_DISCOVER_QR             /* QR code connection */
} artos_drawnet_discover_t;

/* DrawNet sync mode */
typedef enum {
    DRAWNET_SYNC_REALTIME,          /* Every stroke point synced */
    DRAWNET_SYNC_STROKE,            /* Sync on stroke completion */
    DRAWNET_SYNC_INTERVAL,          /* Sync at intervals */
    DRAWNET_SYNC_MANUAL             /* Manual sync only */
} artos_drawnet_sync_t;

/* DrawNet permission level */
typedef enum {
    DRAWNET_PERM_VIEW,              /* Can only view */
    DRAWNET_PERM_DRAW,              /* Can draw on shared layer */
    DRAWNET_PERM_EDIT,              /* Can edit any layer */
    DRAWNET_PERM_ADMIN              /* Full control */
} artos_drawnet_perm_t;

/* DrawNet protocol message type */
typedef enum {
    DRAWNET_MSG_HELLO,              /* Initial handshake */
    DRAWNET_MSG_ACK,                /* Acknowledgment */
    DRAWNET_MSG_PING,               /* Keep-alive ping */
    DRAWNET_MSG_PONG,               /* Ping response */
    DRAWNET_MSG_JOIN,               /* Request to join session */
    DRAWNET_MSG_LEAVE,              /* Leaving session */
    DRAWNET_MSG_PEER_LIST,          /* List of connected peers */
    DRAWNET_MSG_CURSOR,             /* Cursor position update */
    DRAWNET_MSG_STROKE_START,       /* Begin new stroke */
    DRAWNET_MSG_STROKE_POINT,       /* Stroke point data */
    DRAWNET_MSG_STROKE_END,         /* End stroke */
    DRAWNET_MSG_CANVAS_REQUEST,     /* Request full canvas */
    DRAWNET_MSG_CANVAS_DATA,        /* Canvas bitmap data (chunked) */
    DRAWNET_MSG_LAYER_OP,           /* Layer operation */
    DRAWNET_MSG_UNDO,               /* Undo request */
    DRAWNET_MSG_REDO,               /* Redo request */
    DRAWNET_MSG_CHAT,               /* Chat message */
    DRAWNET_MSG_TOOL_CHANGE,        /* Tool/color change notification */
    DRAWNET_MSG_REACTION,           /* Emoji reaction */
    DRAWNET_MSG_KICK,               /* Kick user */
    DRAWNET_MSG_BAN                 /* Ban user */
} artos_drawnet_msg_type_t;

/* DrawNet peer information */
typedef struct artos_drawnet_peer {
    uint32_t peer_id;
    char name[64];
    char hostname[128];
    char ip_address[64];
    uint16_t port;

    /* Visual representation */
    artos_color_t cursor_color;
    cairo_surface_t *avatar;

    /* Current state */
    double cursor_x, cursor_y;
    int is_drawing;
    artos_tool_t current_tool;
    artos_color_t current_color;
    double brush_size;

    /* Permission */
    artos_drawnet_perm_t permission;

    /* Connection quality */
    double latency_ms;
    int packets_lost;
    time_t last_seen;
    int connected;

    /* Activity indicator */
    int show_cursor;
    guint cursor_fade_timer;
    double cursor_opacity;

    /* Network socket (for direct peer connection) */
    int socket_fd;
    GIOChannel *channel;
    guint channel_watch;

    /* Receive buffer for partial packets */
    uint8_t *recv_buffer;
    size_t recv_buffer_size;
    size_t recv_buffer_used;

    struct artos_drawnet_peer *next;
} artos_drawnet_peer_t;

/* DrawNet network packet */
typedef struct artos_drawnet_packet {
    /* Header */
    uint32_t magic;                 /* "DNET" = 0x444E4554 */
    uint16_t version;
    artos_drawnet_msg_type_t type;
    uint32_t sender_id;
    uint32_t seq_num;
    uint64_t timestamp;
    uint32_t payload_len;

    /* Payload (variable based on type) */
    uint8_t *payload;

    struct artos_drawnet_packet *next;
} artos_drawnet_packet_t;

/* DrawNet session configuration */
typedef struct artos_drawnet_config {
    char session_name[128];
    char password[64];              /* Optional password */
    int max_peers;                  /* Maximum connected peers (0 = unlimited) */
    artos_drawnet_sync_t sync_mode;
    int sync_interval_ms;           /* For SYNC_INTERVAL mode */
    artos_drawnet_perm_t default_perm; /* Default permission for new peers */
    int require_approval;           /* Require host approval to join */
    int allow_anonymous;            /* Allow peers without names */
    int compress_canvas;            /* Compress canvas data for transfer */
    int share_cursor;               /* Share cursor position */
    int share_tool;                 /* Share tool/color changes */
} artos_drawnet_config_t;

/* DrawNet main state */
typedef struct artos_drawnet {
    int enabled;
    artos_drawnet_state_t state;
    int is_host;

    /* Session info */
    char session_id[32];            /* Short alphanumeric code */
    char session_qr[1024];          /* QR code data for sharing */
    artos_drawnet_config_t config;

    /* Local peer identity */
    uint32_t local_id;
    char local_name[64];
    artos_color_t local_cursor_color;

    /* Connected peers */
    artos_drawnet_peer_t *peers;
    int peer_count;
    int peer_capacity;

    /* Network sockets */
    int tcp_socket;                 /* Main TCP connection */
    int udp_socket;                 /* UDP for cursor updates */
    int listen_socket;              /* Listen for connections (host only) */
    uint16_t listen_port;

    /* GLib IO channels */
    void *tcp_channel;
    void *udp_channel;
    void *listen_channel;
    guint tcp_watch;
    guint udp_watch;
    guint listen_watch;

    /* mDNS/Avahi for local discovery */
    void *avahi_client;             /* AvahiClient* */
    void *avahi_browser;            /* AvahiServiceBrowser* */
    void *avahi_entry_group;        /* AvahiEntryGroup* - for publishing */

    /* Packet queue */
    artos_drawnet_packet_t *outgoing;
    artos_drawnet_packet_t *incoming;
    uint32_t local_seq;
    GMutex queue_mutex;

    /* Canvas sync */
    int canvas_sync_pending;
    int canvas_chunk_current;
    int canvas_chunk_total;
    uint8_t *canvas_buffer;
    size_t canvas_buffer_size;

    /* Current stroke being broadcast */
    uint32_t current_stroke_id;

    /* Timers */
    guint ping_timer;               /* Keep-alive timer */
    guint cursor_timer;             /* Cursor broadcast timer */
    guint sync_timer;               /* Canvas sync timer */
    guint discovery_timer;          /* Peer discovery timer */

    /* Statistics */
    uint64_t bytes_sent;
    uint64_t bytes_received;
    int packets_sent;
    int packets_received;
    double avg_latency_ms;
    time_t session_start;

    /* Discovered peers (before connecting) */
    struct {
        char name[64];
        char host[128];
        uint16_t port;
        int active;
    } discovered[32];
    int discovered_count;

    /* Error handling */
    char last_error[256];
    int error_code;

    /* Callbacks */
    void (*on_peer_joined)(struct artos_drawnet *net, artos_drawnet_peer_t *peer);
    void (*on_peer_left)(struct artos_drawnet *net, artos_drawnet_peer_t *peer);
    void (*on_stroke_received)(struct artos_drawnet *net, artos_drawnet_peer_t *peer,
                               artos_ai_point_t *points, int count);
    void (*on_chat_received)(struct artos_drawnet *net, artos_drawnet_peer_t *peer,
                             const char *message);
    void *callback_data;

    /* Governor integration */
    void *governor;                 /* phantom_governor_t* - for capability checking */
    int governor_checks;            /* Enable Governor capability checking */
    int governor_approved;          /* Network capability has been approved */
    char governor_approval_scope[256]; /* Approved scope (e.g., "drawnet_session") */

} artos_drawnet_t;

/* DrawNet constants */
#define DRAWNET_MAGIC           0x444E4554  /* "DNET" */
#define DRAWNET_VERSION         1
#define DRAWNET_DEFAULT_PORT    34567
#define DRAWNET_MAX_PACKET      65536
#define DRAWNET_PING_INTERVAL   5000        /* ms */
#define DRAWNET_CURSOR_INTERVAL 50          /* ms */
#define DRAWNET_TIMEOUT         30000       /* ms */
#define DRAWNET_CHUNK_SIZE      32768       /* 32KB chunks for canvas transfer */

/* ==============================================================================
 * DrawNet Wire Protocol - Packed Structures for Network Transmission
 * ============================================================================== */

/* Wire format header (32 bytes) */
typedef struct __attribute__((packed)) drawnet_wire_header {
    uint32_t magic;           /* 0x444E4554 "DNET" */
    uint16_t version;         /* Protocol version (1) */
    uint16_t msg_type;        /* artos_drawnet_msg_type_t */
    uint32_t sender_id;       /* Peer ID */
    uint32_t seq_num;         /* Sequence number */
    uint64_t timestamp;       /* Milliseconds since epoch */
    uint32_t payload_len;     /* Length of payload */
    uint32_t flags;           /* Reserved for future use */
} drawnet_wire_header_t;

/* HELLO message payload */
typedef struct __attribute__((packed)) drawnet_msg_hello {
    char session_id[32];      /* Session code to join */
    char name[64];            /* Peer display name */
    uint32_t color_rgba;      /* Cursor color packed RGBA */
    uint32_t capabilities;    /* Supported features bitmap */
} drawnet_msg_hello_t;

/* ACK message payload */
typedef struct __attribute__((packed)) drawnet_msg_ack {
    uint32_t result;          /* 0=success, 1=wrong password, 2=full, 3=banned */
    uint32_t assigned_id;     /* Assigned peer ID */
    uint32_t assigned_perm;   /* Permission level assigned */
    char session_name[128];   /* Full session name */
    uint32_t peer_count;      /* Current peer count */
} drawnet_msg_ack_t;

/* Cursor position message */
typedef struct __attribute__((packed)) drawnet_msg_cursor {
    double x;                 /* Canvas X coordinate */
    double y;                 /* Canvas Y coordinate */
    uint8_t is_drawing;       /* Currently drawing flag */
    uint8_t padding[7];       /* Alignment padding */
} drawnet_msg_cursor_t;

/* Stroke start message */
typedef struct __attribute__((packed)) drawnet_msg_stroke_start {
    uint32_t stroke_id;       /* Unique stroke identifier */
    uint32_t color_rgba;      /* Stroke color */
    double brush_size;        /* Brush size in pixels */
    uint32_t tool;            /* artos_tool_t */
    uint32_t layer_index;     /* Target layer */
} drawnet_msg_stroke_start_t;

/* Stroke point message */
typedef struct __attribute__((packed)) drawnet_msg_stroke_point {
    uint32_t stroke_id;       /* Matching stroke_start ID */
    double x;
    double y;
    double pressure;          /* Tablet pressure 0.0-1.0 */
} drawnet_msg_stroke_point_t;

/* Stroke end message */
typedef struct __attribute__((packed)) drawnet_msg_stroke_end {
    uint32_t stroke_id;
    uint32_t point_count;     /* Total points in stroke */
} drawnet_msg_stroke_end_t;

/* Chat message */
typedef struct __attribute__((packed)) drawnet_msg_chat {
    char message[512];        /* Null-terminated message */
} drawnet_msg_chat_t;

/* Tool change message */
typedef struct __attribute__((packed)) drawnet_msg_tool_change {
    uint32_t tool;            /* artos_tool_t */
    uint32_t color_rgba;      /* Current color */
    double brush_size;        /* Current brush size */
} drawnet_msg_tool_change_t;

/* Canvas chunk message */
typedef struct __attribute__((packed)) drawnet_msg_canvas_chunk {
    uint32_t chunk_index;     /* 0-based chunk number */
    uint32_t total_chunks;    /* Total chunks in transfer */
    uint64_t total_size;      /* Total PNG size in bytes */
    uint32_t chunk_size;      /* Size of data in this chunk */
    /* Followed by chunk_size bytes of PNG data */
} drawnet_msg_canvas_chunk_t;

/* Kick message */
typedef struct __attribute__((packed)) drawnet_msg_kick {
    uint32_t peer_id;         /* Peer to kick */
    char reason[128];         /* Optional reason message */
} drawnet_msg_kick_t;

/* Peer info for peer list broadcast */
typedef struct __attribute__((packed)) drawnet_peer_info {
    uint32_t peer_id;
    char name[64];
    uint32_t color_rgba;
    uint32_t permission;
    uint8_t connected;
    uint8_t padding[3];
} drawnet_peer_info_t;

/* ==============================================================================
 * Creative Journal - Session Logging & Stroke Archaeology
 * ============================================================================== */

#define JOURNAL_MAX_SESSIONS    1000
#define JOURNAL_MAX_NOTES       4096
#define JOURNAL_THUMBNAIL_SIZE  256

/* Journal entry types */
typedef enum {
    JOURNAL_ENTRY_SESSION_START,    /* New session started */
    JOURNAL_ENTRY_SESSION_END,      /* Session ended */
    JOURNAL_ENTRY_STROKE,           /* Stroke recorded */
    JOURNAL_ENTRY_TOOL_CHANGE,      /* Tool changed */
    JOURNAL_ENTRY_COLOR_CHANGE,     /* Color changed */
    JOURNAL_ENTRY_LAYER_OP,         /* Layer operation */
    JOURNAL_ENTRY_SAVE,             /* Document saved */
    JOURNAL_ENTRY_EXPORT,           /* Document exported */
    JOURNAL_ENTRY_UNDO,             /* Undo performed */
    JOURNAL_ENTRY_NOTE,             /* User note added */
    JOURNAL_ENTRY_MILESTONE         /* User-marked milestone */
} artos_journal_entry_type_t;

/* Journal entry */
typedef struct artos_journal_entry {
    artos_journal_entry_type_t type;
    time_t timestamp;
    uint32_t session_id;

    /* Entry-specific data */
    union {
        struct {
            int stroke_count;
            double duration_secs;
        } stroke;

        struct {
            artos_tool_t old_tool;
            artos_tool_t new_tool;
        } tool_change;

        struct {
            artos_color_t old_color;
            artos_color_t new_color;
        } color_change;

        struct {
            char operation[32];
            int layer_index;
        } layer_op;

        struct {
            char note[JOURNAL_MAX_NOTES];
        } note;
    } data;

    struct artos_journal_entry *next;
} artos_journal_entry_t;

/* Session record for creative journal */
typedef struct artos_journal_session {
    uint32_t session_id;
    time_t start_time;
    time_t end_time;
    double duration_secs;

    /* Statistics */
    int stroke_count;
    int undo_count;
    int tool_changes;
    int color_changes;
    int layers_created;

    /* Thumbnail at session end */
    cairo_surface_t *thumbnail;

    /* User notes */
    char notes[JOURNAL_MAX_NOTES];
    int has_milestone;
    char milestone_name[64];

    /* Document state hash (for archaeology) */
    char state_hash[65];        /* SHA-256 hex */

    struct artos_journal_session *next;
} artos_journal_session_t;

/* Creative Journal state */
typedef struct artos_journal {
    int enabled;
    int auto_log;               /* Auto-log all actions */

    /* Current session */
    artos_journal_session_t *current_session;
    uint32_t next_session_id;
    time_t session_start;

    /* Session history */
    artos_journal_session_t *sessions;
    int session_count;

    /* Entry log */
    artos_journal_entry_t *entries;
    int entry_count;

    /* Time tracking */
    time_t last_activity;
    double total_time_secs;
    int idle_timeout_secs;      /* Mark idle after this many seconds */
    int is_idle;

    /* Stroke archaeology (GeoFS integration) */
    int archaeology_enabled;
    char archaeology_path[4096];    /* Path to GeoFS volume */

    /* Version snapshots for archaeology */
    struct {
        char hash[65];
        time_t timestamp;
        char description[128];
        cairo_surface_t *thumbnail;
    } snapshots[100];
    int snapshot_count;

    /* Statistics */
    int total_strokes;
    int total_sessions;
    double total_hours;

    /* File path for journal storage */
    char filepath[4096];
    int modified;
} artos_journal_t;

/* ==============================================================================
 * Voice Commands - Quick Shortcuts
 * ============================================================================== */

/* Voice command categories */
typedef enum {
    VOICE_CMD_TOOL,         /* Tool switching */
    VOICE_CMD_ACTION,       /* Undo, redo, save, etc. */
    VOICE_CMD_VIEW,         /* Zoom, pan, rotate */
    VOICE_CMD_COLOR,        /* Color changes */
    VOICE_CMD_BRUSH,        /* Brush settings */
    VOICE_CMD_LAYER,        /* Layer operations */
    VOICE_CMD_SELECTION,    /* Selection operations */
    VOICE_CMD_FILE,         /* File operations */
    VOICE_CMD_CUSTOM        /* User-defined */
} artos_voice_cmd_category_t;

/* Voice command definition */
typedef struct artos_voice_command {
    char phrase[64];                /* Trigger phrase */
    char aliases[4][64];            /* Alternative phrases */
    int alias_count;
    artos_voice_cmd_category_t category;

    /* Action to perform */
    void (*action)(struct phantom_artos *artos, const char *params);
    char params[128];               /* Optional parameters */

    /* Feedback */
    char feedback[128];             /* Spoken/shown feedback */
    int beep_on_recognize;          /* Beep when recognized */

    struct artos_voice_command *next;
} artos_voice_command_t;

/* Voice command state */
typedef struct artos_voice_commands {
    int enabled;
    int listening;

    /* Command registry */
    artos_voice_command_t *commands;
    int command_count;

    /* Recognition settings */
    double confidence_threshold;    /* 0.0-1.0, minimum confidence */
    int continuous_listen;          /* Always listening */
    char wake_word[32];             /* Wake word (e.g., "hey artos") */
    int require_wake_word;          /* Require wake word first */

    /* Last recognized */
    char last_phrase[128];
    double last_confidence;
    artos_voice_command_t *last_command;

    /* Audio feedback */
    int audio_feedback;             /* Play sounds */
    int visual_feedback;            /* Show overlay */

    /* Custom commands */
    artos_voice_command_t *custom_commands;
    int custom_count;
} artos_voice_commands_t;

/* ==============================================================================
 * AI Smart Features
 * ============================================================================== */

/* AI color suggestion */
typedef struct artos_ai_color_suggest {
    int enabled;

    /* Current palette analysis */
    artos_color_t dominant_colors[8];
    int dominant_count;

    /* Suggested colors */
    artos_color_t suggestions[12];
    char suggestion_reasons[12][64];    /* Why this color */
    int suggestion_count;

    /* Harmony analysis */
    int detected_harmony;               /* artos_color_harmony_t value */
    double harmony_score;               /* How well colors harmonize */

    /* Temperature analysis */
    double warm_ratio;                  /* 0.0 cold, 1.0 warm */
    double saturation_avg;
    double value_avg;
} artos_ai_color_suggest_t;

/* AI perspective guide */
typedef struct artos_ai_perspective {
    int enabled;
    int detected;

    /* Vanishing points (up to 3 for 3-point perspective) */
    struct {
        double x, y;
        double confidence;
        int active;
    } vanishing_points[3];
    int point_count;

    /* Horizon line */
    double horizon_y;
    double horizon_angle;
    int horizon_detected;

    /* Guide lines to draw */
    struct {
        double x1, y1, x2, y2;
        double opacity;
    } guide_lines[32];
    int guide_count;

    /* Settings */
    int show_guides;
    int snap_to_perspective;
    double guide_opacity;
    artos_color_t guide_color;
} artos_ai_perspective_t;

/* AI sketch cleanup hints */
typedef struct artos_ai_sketch_cleanup {
    int enabled;
    int analyzing;

    /* Detected issues */
    struct {
        double x, y;
        char issue[64];             /* "wobbly line", "gap", "overshoot" */
        double severity;            /* 0.0-1.0 */
        cairo_surface_t *suggestion;/* Visual suggestion overlay */
    } issues[64];
    int issue_count;

    /* Overall analysis */
    double line_steadiness;         /* 0.0 shaky, 1.0 smooth */
    double closure_score;           /* How well shapes are closed */
    double symmetry_score;          /* Detected symmetry */

    /* Suggested improvements */
    int suggest_stabilizer;         /* Recommend turning on stabilizer */
    int suggest_strength;           /* Recommended stabilizer strength */
} artos_ai_sketch_cleanup_t;

/* Transform mode */
typedef enum {
    ARTOS_TRANSFORM_NONE,
    ARTOS_TRANSFORM_MOVE,
    ARTOS_TRANSFORM_SCALE,
    ARTOS_TRANSFORM_ROTATE,
    ARTOS_TRANSFORM_FLIP_H,
    ARTOS_TRANSFORM_FLIP_V,
    ARTOS_TRANSFORM_FREE
} artos_transform_mode_t;

/* Color harmony types */
typedef enum {
    ARTOS_HARMONY_NONE,
    ARTOS_HARMONY_COMPLEMENTARY,
    ARTOS_HARMONY_ANALOGOUS,
    ARTOS_HARMONY_TRIADIC,
    ARTOS_HARMONY_SPLIT_COMPLEMENTARY,
    ARTOS_HARMONY_TETRADIC,
    ARTOS_HARMONY_MONOCHROMATIC
} artos_color_harmony_t;

/* Symmetry mode */
typedef enum {
    ARTOS_SYMMETRY_NONE,
    ARTOS_SYMMETRY_HORIZONTAL,      /* Mirror left/right */
    ARTOS_SYMMETRY_VERTICAL,        /* Mirror top/bottom */
    ARTOS_SYMMETRY_BOTH,            /* 4-way symmetry */
    ARTOS_SYMMETRY_RADIAL_3,        /* 3-point radial */
    ARTOS_SYMMETRY_RADIAL_4,        /* 4-point radial */
    ARTOS_SYMMETRY_RADIAL_6,        /* 6-point radial */
    ARTOS_SYMMETRY_RADIAL_8         /* 8-point radial */
} artos_symmetry_mode_t;

/* Brush stabilization point for smoothing */
typedef struct artos_stabilizer_point {
    double x, y;
    double pressure;
    guint32 time;
} artos_stabilizer_point_t;

#define ARTOS_STABILIZER_MAX_POINTS 32

/* Reference image */
typedef struct artos_reference {
    cairo_surface_t *image;
    char filepath[4096];
    double x, y;            /* Position */
    double scale;           /* Display scale */
    double opacity;         /* Transparency */
    int locked;             /* Lock position */
    int visible;
    struct artos_reference *next;
} artos_reference_t;

/* Layer structure */
typedef struct artos_layer {
    char name[64];
    cairo_surface_t *surface;
    cairo_surface_t *mask;          /* Layer mask (grayscale) */
    int mask_enabled;               /* Is mask active */
    int mask_visible;               /* Show mask overlay */
    int clipping;                   /* Clip to layer below */
    int visible;
    int locked;
    double opacity;
    artos_blend_mode_t blend_mode;
    int width;
    int height;
} artos_layer_t;

/* Brush settings */
typedef struct artos_brush {
    char name[32];
    artos_brush_shape_t shape;
    double size;            /* Diameter in pixels */
    double hardness;        /* 0.0 (soft) to 1.0 (hard) */
    double opacity;         /* 0.0 to 1.0 */
    double flow;            /* Paint flow rate 0.0 to 1.0 */
    double spacing;         /* Spacing between dabs (% of size) */
    int pressure_size;      /* Pressure affects size */
    int pressure_opacity;   /* Pressure affects opacity */
    double angle;           /* Brush angle in degrees */
    double roundness;       /* 0.0 to 1.0 */
    cairo_surface_t *tip;   /* Custom brush tip (optional) */
} artos_brush_t;

/* Point for drawing paths */
typedef struct artos_point {
    double x;
    double y;
    double pressure;        /* Tablet pressure 0.0 to 1.0 */
    double tilt_x;          /* Tablet tilt */
    double tilt_y;
} artos_point_t;

/* Stroke for undo/redo (geological layer) */
typedef struct artos_stroke {
    artos_point_t *points;
    int point_count;
    int point_capacity;
    artos_tool_t tool;
    artos_brush_t brush;
    artos_color_t color;
    int layer_index;
    cairo_surface_t *before_snapshot;   /* State before stroke */
    struct artos_stroke *next;
} artos_stroke_t;

/* Selection */
typedef struct artos_selection {
    cairo_surface_t *mask;      /* Alpha mask for selection */
    int has_selection;
    int x, y;                   /* Bounding box */
    int width, height;
    int marching_ants_offset;   /* Animation offset */
} artos_selection_t;

/* Document/Canvas */
typedef struct artos_document {
    char name[256];
    char filepath[4096];
    int width;
    int height;
    int dpi;

    /* Layers */
    artos_layer_t *layers[ARTOS_MAX_LAYERS];
    int layer_count;
    int active_layer;

    /* Undo history (geological layers) */
    artos_stroke_t *undo_stack;
    artos_stroke_t *redo_stack;
    int undo_count;

    /* Selection */
    artos_selection_t selection;

    /* Modified flag */
    int modified;

    /* Composite surface (flattened preview) */
    cairo_surface_t *composite;
    int composite_dirty;
} artos_document_t;

/* Main ArtOS context */
struct phantom_artos {
    /* GTK widgets */
    GtkWidget *window;
    GtkWidget *canvas_area;         /* Main drawing area */
    GtkWidget *tool_palette;        /* Tool buttons */
    GtkWidget *color_button;        /* Color selector button */
    GtkWidget *brush_size_scale;    /* Brush size slider */
    GtkWidget *brush_opacity_scale; /* Brush opacity slider */
    GtkWidget *brush_hardness_scale;/* Brush hardness slider */
    GtkWidget *layer_tree;          /* Layer list */
    GtkListStore *layer_store;
    GtkWidget *brush_combo;         /* Brush presets */
    GtkWidget *zoom_label;          /* Current zoom level */
    GtkWidget *coords_label;        /* Mouse coordinates */
    GtkWidget *status_bar;

    /* Document */
    artos_document_t *document;

    /* Current tool and settings */
    artos_tool_t current_tool;
    artos_brush_t current_brush;
    artos_color_t foreground_color;
    artos_color_t background_color;

    /* Brush presets */
    artos_brush_t brushes[ARTOS_MAX_BRUSHES];
    int brush_count;

    /* Color palette */
    artos_color_t palette[ARTOS_MAX_PALETTE];
    int palette_count;

    /* View state */
    double zoom;
    double pan_x;
    double pan_y;
    int canvas_width;       /* Allocated canvas widget size */
    int canvas_height;

    /* Drawing state */
    int is_drawing;
    artos_stroke_t *current_stroke;
    double last_x;
    double last_y;

    /* Shape tool state */
    int shape_start_x;
    int shape_start_y;
    int shape_drawing;
    cairo_surface_t *shape_preview;

    /* Clone tool state */
    int clone_source_set;
    double clone_source_x;
    double clone_source_y;

    /* Transform state */
    artos_transform_mode_t transform_mode;
    int transforming;
    double transform_start_x;
    double transform_start_y;
    double transform_angle;
    double transform_scale_x;
    double transform_scale_y;
    cairo_surface_t *transform_preview;

    /* Reference images */
    artos_reference_t *references;
    int reference_count;
    artos_reference_t *active_reference;
    GtkWidget *reference_panel;
    GtkWidget *reference_list;
    GtkListStore *reference_store;
    GtkWidget *reference_opacity_scale;
    int show_references;

    /* Color harmony */
    artos_color_harmony_t color_harmony;
    artos_color_t harmony_colors[6];
    int harmony_color_count;
    GtkWidget *color_wheel_area;
    GtkWidget *harmony_combo;

    /* Symmetry mode */
    artos_symmetry_mode_t symmetry_mode;
    double symmetry_center_x;
    double symmetry_center_y;
    int symmetry_show_guides;
    GtkWidget *symmetry_combo;
    GtkWidget *symmetry_panel;

    /* Brush stabilization */
    int stabilizer_enabled;
    int stabilizer_strength;        /* 1-10, higher = more smoothing */
    artos_stabilizer_point_t stabilizer_buffer[ARTOS_STABILIZER_MAX_POINTS];
    int stabilizer_count;
    int stabilizer_index;
    GtkWidget *stabilizer_check;
    GtkWidget *stabilizer_scale;

    /* Canvas rotation */
    double canvas_rotation;         /* Degrees */
    int canvas_flip_h;              /* Flip view horizontally */
    int canvas_flip_v;              /* Flip view vertically */
    GtkWidget *rotation_scale;
    GtkWidget *rotation_panel;

    /* Grid and guides */
    int show_grid;
    int grid_size;
    int snap_to_grid;

    /* Animation timer for marching ants */
    guint selection_timer;

    /* Tablet/stylus support */
    GdkDevice *stylus_device;
    int has_pressure;

    /* Dictation drawing system */
    artos_dictation_t dictation;
    GtkWidget *dictation_panel;
    GtkWidget *dictation_entry;     /* Text input for voice commands */
    GtkWidget *dictation_toggle;    /* Enable/disable button */
    GtkWidget *dictation_listen_btn; /* Voice listen button */
    GtkWidget *dictation_level_bar;  /* Audio level indicator */
    GtkWidget *dictation_feedback;  /* Visual feedback label */
    GtkWidget *dictation_history_view;  /* Command history */
    GtkListStore *dictation_history_store;

    /* Face tracking drawing system */
    artos_facetrack_t facetrack;
    GtkWidget *facetrack_panel;
    GtkWidget *facetrack_toggle;        /* Enable/disable button */
    GtkWidget *facetrack_start_btn;     /* Start/stop tracking */
    GtkWidget *facetrack_camera_btn;    /* Show camera preview */
    GtkWidget *facetrack_calibrate_btn; /* Calibration button */
    GtkWidget *facetrack_mode_combo;    /* Tracking mode selector */
    GtkWidget *facetrack_sensitivity_scale;  /* Sensitivity slider */
    GtkWidget *facetrack_smoothing_scale;    /* Smoothing slider */
    GtkWidget *facetrack_preview_area;  /* Webcam preview */
    GtkWidget *facetrack_status_label;  /* Status display */
    GtkWidget *facetrack_pos_label;     /* Position display */
    GtkWidget *facetrack_fps_label;     /* FPS display */
    GtkWidget *facetrack_gesture_label; /* Last gesture display */
    GtkWidget *facetrack_blink_check;   /* Blink to draw checkbox */
    GtkWidget *facetrack_mouth_check;   /* Mouth to draw checkbox */
    GtkWidget *facetrack_smile_check;   /* Smile to undo checkbox */

    /* AI-Assisted Drawing system */
    artos_ai_assist_t ai_assist;
    GtkWidget *ai_panel;
    GtkWidget *ai_toggle;               /* Enable AI assistance */
    GtkWidget *ai_mode_combo;           /* AI mode selector */
    GtkWidget *ai_suggest_area;         /* Suggestion preview */
    GtkWidget *ai_accept_btn;           /* Accept suggestion */
    GtkWidget *ai_reject_btn;           /* Reject suggestion */
    GtkWidget *ai_prompt_entry;         /* Prompt input for generation */
    GtkWidget *ai_generate_btn;         /* Generate from prompt */
    GtkWidget *ai_status_label;         /* Status display */
    GtkWidget *ai_progress_bar;         /* Generation progress */
    GtkWidget *ai_shape_check;          /* Shape recognition */
    GtkWidget *ai_style_combo;          /* Style preset */

    /* Voice-to-Art generation system */
    artos_voice_art_t voice_art;
    GtkWidget *voiceart_panel;
    GtkWidget *voiceart_toggle;         /* Enable voice art */
    GtkWidget *voiceart_listen_btn;     /* Start listening */
    GtkWidget *voiceart_transcript;     /* Show transcript */
    GtkWidget *voiceart_style_combo;    /* Art style selector */
    GtkWidget *voiceart_creativity;     /* Creativity slider */
    GtkWidget *voiceart_preview_area;   /* Generated image preview */
    GtkWidget *voiceart_generate_btn;   /* Generate from text */
    GtkWidget *voiceart_apply_btn;      /* Apply to canvas */
    GtkWidget *voiceart_status_label;   /* Status display */
    GtkWidget *voiceart_progress_bar;   /* Generation progress */
    GtkWidget *voiceart_history_combo;  /* History of generations */

    /* Collaborative Canvas system */
    artos_collab_t collab;
    GtkWidget *collab_panel;
    GtkWidget *collab_toggle;           /* Enable collaboration */
    GtkWidget *collab_host_btn;         /* Host session */
    GtkWidget *collab_join_btn;         /* Join session */
    GtkWidget *collab_session_entry;    /* Session ID input */
    GtkWidget *collab_name_entry;       /* User name */
    GtkWidget *collab_users_list;       /* Connected users */
    GtkWidget *collab_chat_view;        /* Chat messages */
    GtkWidget *collab_chat_entry;       /* Chat input */
    GtkWidget *collab_send_btn;         /* Send chat */
    GtkWidget *collab_status_label;     /* Connection status */
    GtkWidget *collab_latency_label;    /* Network latency */
    GtkListStore *collab_users_store;   /* Users list store */
    GtkTextBuffer *collab_chat_buffer;  /* Chat text buffer */

    /* DrawNet - Real-time Multi-User Drawing Network */
    artos_drawnet_t drawnet;
    GtkWidget *drawnet_panel;
    GtkWidget *drawnet_toggle;          /* Enable DrawNet */
    GtkWidget *drawnet_host_btn;        /* Host session */
    GtkWidget *drawnet_join_btn;        /* Join session */
    GtkWidget *drawnet_scan_btn;        /* Scan for peers */
    GtkWidget *drawnet_name_entry;      /* User name */
    GtkWidget *drawnet_session_entry;   /* Session code */
    GtkWidget *drawnet_ip_entry;        /* Direct IP entry */
    GtkWidget *drawnet_port_spin;       /* Port number */
    GtkWidget *drawnet_peers_tree;      /* Connected peers */
    GtkListStore *drawnet_peers_store;  /* Peers list store */
    GtkWidget *drawnet_discovered_tree; /* Discovered sessions */
    GtkListStore *drawnet_discovered_store; /* Discovered list store */
    GtkWidget *drawnet_status_label;    /* Connection status */
    GtkWidget *drawnet_code_label;      /* Session code display */
    GtkWidget *drawnet_stats_label;     /* Network stats */
    GtkWidget *drawnet_sync_combo;      /* Sync mode selector */
    GtkWidget *drawnet_perm_combo;      /* Permission selector */
    GtkWidget *drawnet_cursor_check;    /* Share cursor position */
    GtkWidget *drawnet_canvas_area;     /* Mini preview of peer cursors */
    GtkWidget *drawnet_chat_view;       /* Chat messages */
    GtkWidget *drawnet_chat_entry;      /* Chat input */
    GtkWidget *drawnet_send_btn;        /* Send chat */
    GtkTextBuffer *drawnet_chat_buffer; /* Chat text buffer */
    GtkWidget *drawnet_progress_bar;    /* Canvas sync progress */

    /* Creative Journal */
    artos_journal_t journal;
    GtkWidget *journal_panel;
    GtkWidget *journal_toggle;          /* Enable journaling */
    GtkWidget *journal_note_entry;      /* Add note */
    GtkWidget *journal_milestone_btn;   /* Mark milestone */
    GtkWidget *journal_sessions_tree;   /* Session history */
    GtkListStore *journal_sessions_store;
    GtkWidget *journal_timeline;        /* Visual timeline */
    GtkWidget *journal_stats_label;     /* Statistics display */
    GtkWidget *journal_thumbnail_area;  /* Session thumbnail preview */
    GtkWidget *journal_archaeology_btn; /* Open archaeology view */
    GtkWidget *journal_export_btn;      /* Export journal */

    /* Voice Commands */
    artos_voice_commands_t voice_commands;
    GtkWidget *voicecmd_panel;
    GtkWidget *voicecmd_toggle;         /* Enable voice commands */
    GtkWidget *voicecmd_listen_btn;     /* Start/stop listening */
    GtkWidget *voicecmd_status_label;   /* Recognition status */
    GtkWidget *voicecmd_phrase_label;   /* Last recognized phrase */
    GtkWidget *voicecmd_confidence_bar; /* Confidence level */
    GtkWidget *voicecmd_commands_tree;  /* Available commands list */
    GtkListStore *voicecmd_commands_store;
    GtkWidget *voicecmd_wake_entry;     /* Wake word entry */
    GtkWidget *voicecmd_threshold_scale;/* Confidence threshold */

    /* AI Smart Features */
    artos_ai_color_suggest_t ai_color_suggest;
    artos_ai_perspective_t ai_perspective;
    artos_ai_sketch_cleanup_t ai_sketch_cleanup;
    GtkWidget *ai_smart_panel;
    GtkWidget *ai_color_suggest_toggle;
    GtkWidget *ai_color_suggest_area;   /* Color suggestion display */
    GtkWidget *ai_perspective_toggle;
    GtkWidget *ai_perspective_area;     /* Perspective guides overlay */
    GtkWidget *ai_sketch_toggle;
    GtkWidget *ai_sketch_issues_label;  /* Cleanup hints */
    GtkWidget *ai_sketch_apply_btn;     /* Apply suggestions */

};

/* ==============================================================================
 * Function Prototypes
 * ============================================================================== */

/* Lifecycle */
phantom_artos_t *artos_create(void);
void artos_destroy(phantom_artos_t *artos);
GtkWidget *artos_get_widget(phantom_artos_t *artos);

/* Document management */
artos_document_t *artos_document_new(int width, int height, const char *name);
artos_document_t *artos_document_open(const char *filepath);
int artos_document_save(artos_document_t *doc, const char *filepath);
int artos_document_export_png(artos_document_t *doc, const char *filepath);
int artos_document_export_jpeg(artos_document_t *doc, const char *filepath, int quality);
void artos_document_free(artos_document_t *doc);

/* Layer operations */
int artos_layer_add(artos_document_t *doc, const char *name);
int artos_layer_remove(artos_document_t *doc, int index);  /* Hides, doesn't destroy */
int artos_layer_duplicate(artos_document_t *doc, int index);
int artos_layer_merge_down(artos_document_t *doc, int index);
int artos_layer_move(artos_document_t *doc, int from, int to);
void artos_layer_set_visible(artos_document_t *doc, int index, int visible);
void artos_layer_set_opacity(artos_document_t *doc, int index, double opacity);
void artos_layer_set_blend_mode(artos_document_t *doc, int index, artos_blend_mode_t mode);
artos_layer_t *artos_layer_get_active(artos_document_t *doc);

/* Layer mask operations */
int artos_layer_add_mask(artos_document_t *doc, int index);
void artos_layer_delete_mask(artos_document_t *doc, int index);
void artos_layer_enable_mask(artos_document_t *doc, int index, int enable);
void artos_layer_set_clipping(artos_document_t *doc, int index, int clip);
void artos_layer_apply_mask(artos_document_t *doc, int index);

/* Transform operations */
void artos_transform_begin(phantom_artos_t *artos, artos_transform_mode_t mode);
void artos_transform_update(phantom_artos_t *artos, double x, double y);
void artos_transform_apply(phantom_artos_t *artos);
void artos_transform_cancel(phantom_artos_t *artos);
void artos_transform_rotate(phantom_artos_t *artos, double angle);
void artos_transform_scale(phantom_artos_t *artos, double sx, double sy);
void artos_transform_flip_horizontal(phantom_artos_t *artos);
void artos_transform_flip_vertical(phantom_artos_t *artos);
GtkWidget *artos_create_transform_panel(phantom_artos_t *artos);

/* Reference image operations */
int artos_reference_add(phantom_artos_t *artos, const char *filepath);
void artos_reference_remove(phantom_artos_t *artos, int index);
void artos_reference_set_opacity(phantom_artos_t *artos, int index, double opacity);
void artos_reference_set_position(phantom_artos_t *artos, int index, double x, double y);
void artos_reference_set_scale(phantom_artos_t *artos, int index, double scale);
void artos_reference_toggle_visible(phantom_artos_t *artos, int index);
GtkWidget *artos_create_reference_panel(phantom_artos_t *artos);

/* Color harmony */
void artos_color_harmony_update(phantom_artos_t *artos);
void artos_color_harmony_set_type(phantom_artos_t *artos, artos_color_harmony_t type);
void artos_color_wheel_get_harmonies(artos_color_t *base, artos_color_harmony_t type,
                                      artos_color_t *out_colors, int *out_count);
GtkWidget *artos_create_color_wheel_panel(phantom_artos_t *artos);

/* Symmetry mode */
void artos_symmetry_set_mode(phantom_artos_t *artos, artos_symmetry_mode_t mode);
void artos_symmetry_set_center(phantom_artos_t *artos, double x, double y);
void artos_symmetry_draw_point(phantom_artos_t *artos, double x, double y, double pressure);
GtkWidget *artos_create_symmetry_panel(phantom_artos_t *artos);

/* Brush stabilization */
void artos_stabilizer_enable(phantom_artos_t *artos, int enable);
void artos_stabilizer_set_strength(phantom_artos_t *artos, int strength);
void artos_stabilizer_add_point(phantom_artos_t *artos, double x, double y, double pressure);
void artos_stabilizer_get_smoothed(phantom_artos_t *artos, double *x, double *y, double *pressure);
void artos_stabilizer_reset(phantom_artos_t *artos);
GtkWidget *artos_create_stabilizer_panel(phantom_artos_t *artos);

/* Canvas rotation */
void artos_canvas_set_rotation(phantom_artos_t *artos, double degrees);
void artos_canvas_rotate(phantom_artos_t *artos, double delta);
void artos_canvas_reset_rotation(phantom_artos_t *artos);
void artos_canvas_flip_view(phantom_artos_t *artos, int horizontal);
void artos_canvas_to_doc_coords(phantom_artos_t *artos, double cx, double cy, double *dx, double *dy);
GtkWidget *artos_create_canvas_rotation_panel(phantom_artos_t *artos);

/* Drawing operations */
void artos_draw_point(phantom_artos_t *artos, double x, double y, double pressure);
void artos_draw_line(phantom_artos_t *artos, double x1, double y1, double x2, double y2);
void artos_draw_brush_stroke(phantom_artos_t *artos, artos_point_t *points, int count);
void artos_fill(phantom_artos_t *artos, double x, double y, artos_color_t *color);
void artos_draw_shape(phantom_artos_t *artos, artos_tool_t shape,
                      double x1, double y1, double x2, double y2, int filled);

/* Selection operations */
void artos_select_all(phantom_artos_t *artos);
void artos_select_none(phantom_artos_t *artos);
void artos_select_invert(phantom_artos_t *artos);
void artos_select_rect(phantom_artos_t *artos, int x, int y, int w, int h);
void artos_select_ellipse(phantom_artos_t *artos, int x, int y, int w, int h);
void artos_select_by_color(phantom_artos_t *artos, double x, double y, double tolerance);

/* Undo/Redo (geological time travel) */
void artos_undo(phantom_artos_t *artos);
void artos_redo(phantom_artos_t *artos);
void artos_history_clear(artos_document_t *doc);  /* Note: history preserved in GeoFS */

/* Tool operations */
void artos_set_tool(phantom_artos_t *artos, artos_tool_t tool);
void artos_set_brush(phantom_artos_t *artos, artos_brush_t *brush);
void artos_set_foreground_color(phantom_artos_t *artos, artos_color_t *color);
void artos_set_background_color(phantom_artos_t *artos, artos_color_t *color);
void artos_swap_colors(phantom_artos_t *artos);

/* View operations */
void artos_zoom_in(phantom_artos_t *artos);
void artos_zoom_out(phantom_artos_t *artos);
void artos_zoom_fit(phantom_artos_t *artos);
void artos_zoom_100(phantom_artos_t *artos);
void artos_pan(phantom_artos_t *artos, double dx, double dy);

/* Filters (non-destructive in Phantom philosophy) */
void artos_filter_blur(artos_document_t *doc, int layer, double radius);
void artos_filter_sharpen(artos_document_t *doc, int layer, double amount);
void artos_filter_brightness_contrast(artos_document_t *doc, int layer,
                                       double brightness, double contrast);
void artos_filter_hue_saturation(artos_document_t *doc, int layer,
                                  double hue, double saturation, double lightness);
void artos_filter_invert(artos_document_t *doc, int layer);
void artos_filter_grayscale(artos_document_t *doc, int layer);

/* Brush presets */
void artos_init_default_brushes(phantom_artos_t *artos);
void artos_brush_save(phantom_artos_t *artos, const char *name);
void artos_brush_load(phantom_artos_t *artos, const char *name);

/* Palette */
void artos_palette_add(phantom_artos_t *artos, artos_color_t *color);
void artos_palette_load(phantom_artos_t *artos, const char *filepath);
void artos_palette_save(phantom_artos_t *artos, const char *filepath);

/* Utility */
void artos_color_from_hsv(artos_color_t *color, double h, double s, double v);
void artos_color_to_hsv(artos_color_t *color, double *h, double *s, double *v);
void artos_color_from_hex(artos_color_t *color, const char *hex);
void artos_color_to_hex(artos_color_t *color, char *hex, size_t len);
void artos_update_composite(artos_document_t *doc);

/* Dictation Drawing */
void artos_dictation_init(phantom_artos_t *artos);
void artos_dictation_enable(phantom_artos_t *artos, int enable);
int artos_dictation_parse(const char *text, artos_dictation_parsed_t *result);
int artos_dictation_execute(phantom_artos_t *artos, artos_dictation_parsed_t *cmd);
void artos_dictation_process_text(phantom_artos_t *artos, const char *text);
void artos_dictation_show_feedback(phantom_artos_t *artos, const char *message);
void artos_dictation_add_history(phantom_artos_t *artos, const char *command,
                                  artos_dictation_cmd_t type, int executed);
GtkWidget *artos_create_dictation_panel(phantom_artos_t *artos);

/* Voice Recognition */
int artos_voice_init(phantom_artos_t *artos);
void artos_voice_cleanup(phantom_artos_t *artos);
void artos_voice_start_listening(phantom_artos_t *artos);
void artos_voice_stop_listening(phantom_artos_t *artos);
int artos_voice_is_listening(phantom_artos_t *artos);

/* Advanced shape drawing for dictation */
void artos_draw_triangle(phantom_artos_t *artos, double cx, double cy, double size, int filled);
void artos_draw_star(phantom_artos_t *artos, double cx, double cy, double size, int points, int filled);
void artos_draw_arrow(phantom_artos_t *artos, double x1, double y1, double x2, double y2);
void artos_draw_heart(phantom_artos_t *artos, double cx, double cy, double size, int filled);
void artos_draw_spiral(phantom_artos_t *artos, double cx, double cy, double size, double turns);
void artos_draw_circle(phantom_artos_t *artos, double cx, double cy, double radius, int filled);

/* Face Tracking Drawing */
int artos_facetrack_init(phantom_artos_t *artos);
void artos_facetrack_cleanup(phantom_artos_t *artos);
void artos_facetrack_enable(phantom_artos_t *artos, int enable);
void artos_facetrack_start(phantom_artos_t *artos);
void artos_facetrack_stop(phantom_artos_t *artos);
int artos_facetrack_is_tracking(phantom_artos_t *artos);
void artos_facetrack_set_mode(phantom_artos_t *artos, artos_face_mode_t mode);
void artos_facetrack_set_sensitivity(phantom_artos_t *artos, double sensitivity);
void artos_facetrack_set_smoothing(phantom_artos_t *artos, double smoothing);
void artos_facetrack_calibrate(phantom_artos_t *artos);
void artos_facetrack_pen_down(phantom_artos_t *artos);
void artos_facetrack_pen_up(phantom_artos_t *artos);
void artos_facetrack_toggle_draw(phantom_artos_t *artos);
GtkWidget *artos_create_facetrack_panel(phantom_artos_t *artos);

/* AI-Assisted Drawing */
int artos_ai_init(phantom_artos_t *artos);
void artos_ai_cleanup(phantom_artos_t *artos);
void artos_ai_enable(phantom_artos_t *artos, int enable);
void artos_ai_set_mode(phantom_artos_t *artos, artos_ai_mode_t mode);
void artos_ai_analyze_stroke(phantom_artos_t *artos, artos_ai_point_t *points, int count);
void artos_ai_accept_suggestion(phantom_artos_t *artos);
void artos_ai_reject_suggestion(phantom_artos_t *artos);
void artos_ai_next_suggestion(phantom_artos_t *artos);
void artos_ai_generate_from_prompt(phantom_artos_t *artos, const char *prompt);
void artos_ai_load_style_reference(phantom_artos_t *artos, const char *filepath);
void artos_ai_clear_suggestions(phantom_artos_t *artos);
int artos_ai_recognize_shape(artos_ai_point_t *points, int count, char *shape_name, double *params);
GtkWidget *artos_create_ai_panel(phantom_artos_t *artos);

/* Voice-to-Art Generation */
int artos_voiceart_init(phantom_artos_t *artos);
void artos_voiceart_cleanup(phantom_artos_t *artos);
void artos_voiceart_enable(phantom_artos_t *artos, int enable);
void artos_voiceart_start_listening(phantom_artos_t *artos);
void artos_voiceart_stop_listening(phantom_artos_t *artos);
void artos_voiceart_generate(phantom_artos_t *artos, const char *prompt);
void artos_voiceart_apply_to_canvas(phantom_artos_t *artos, int image_index);
void artos_voiceart_set_style(phantom_artos_t *artos, const char *style);
void artos_voiceart_set_creativity(phantom_artos_t *artos, double creativity);
GtkWidget *artos_create_voiceart_panel(phantom_artos_t *artos);

/* Collaborative Canvas */
int artos_collab_init(phantom_artos_t *artos);
void artos_collab_cleanup(phantom_artos_t *artos);
void artos_collab_enable(phantom_artos_t *artos, int enable);
int artos_collab_host_session(phantom_artos_t *artos, const char *name, const char *password);
int artos_collab_join_session(phantom_artos_t *artos, const char *session_id, const char *password);
void artos_collab_leave_session(phantom_artos_t *artos);
void artos_collab_send_stroke(phantom_artos_t *artos, artos_ai_point_t *points, int count);
void artos_collab_send_cursor(phantom_artos_t *artos, double x, double y);
void artos_collab_send_chat(phantom_artos_t *artos, const char *message);
void artos_collab_set_username(phantom_artos_t *artos, const char *name);
artos_collab_user_t *artos_collab_get_users(phantom_artos_t *artos);
GtkWidget *artos_create_collab_panel(phantom_artos_t *artos);

/* DrawNet - Real-time Multi-User Drawing Network */
int artos_drawnet_init(phantom_artos_t *artos);
void artos_drawnet_cleanup(phantom_artos_t *artos);
void artos_drawnet_enable(phantom_artos_t *artos, int enable);
void artos_drawnet_set_governor(phantom_artos_t *artos, void *governor);
int artos_drawnet_check_capability(phantom_artos_t *artos, const char *operation);
int artos_drawnet_host_session(phantom_artos_t *artos, const char *name);
int artos_drawnet_join_session(phantom_artos_t *artos, const char *session_code);
int artos_drawnet_join_direct(phantom_artos_t *artos, const char *host, uint16_t port);
void artos_drawnet_leave_session(phantom_artos_t *artos);
void artos_drawnet_scan_start(phantom_artos_t *artos);
void artos_drawnet_scan_stop(phantom_artos_t *artos);
void artos_drawnet_set_username(phantom_artos_t *artos, const char *name);
void artos_drawnet_set_sync_mode(phantom_artos_t *artos, artos_drawnet_sync_t mode);
void artos_drawnet_set_permission(phantom_artos_t *artos, uint32_t peer_id, artos_drawnet_perm_t perm);
void artos_drawnet_broadcast_stroke_start(phantom_artos_t *artos);
void artos_drawnet_broadcast_stroke_point(phantom_artos_t *artos, double x, double y, double pressure);
void artos_drawnet_broadcast_stroke_end(phantom_artos_t *artos);
void artos_drawnet_broadcast_cursor(phantom_artos_t *artos, double x, double y);
void artos_drawnet_broadcast_tool_change(phantom_artos_t *artos);
void artos_drawnet_send_chat(phantom_artos_t *artos, const char *message);
void artos_drawnet_send_reaction(phantom_artos_t *artos, const char *emoji);
void artos_drawnet_request_canvas(phantom_artos_t *artos, uint32_t peer_id);
void artos_drawnet_send_canvas(phantom_artos_t *artos, uint32_t peer_id);
void artos_drawnet_kick_peer(phantom_artos_t *artos, uint32_t peer_id);
artos_drawnet_peer_t *artos_drawnet_get_peers(phantom_artos_t *artos);
int artos_drawnet_get_peer_count(phantom_artos_t *artos);
const char *artos_drawnet_get_session_code(phantom_artos_t *artos);
const char *artos_drawnet_get_state_string(artos_drawnet_state_t state);
GtkWidget *artos_create_drawnet_panel(phantom_artos_t *artos);

/* Creative Journal */
int artos_journal_init(phantom_artos_t *artos);
void artos_journal_cleanup(phantom_artos_t *artos);
void artos_journal_enable(phantom_artos_t *artos, int enable);
void artos_journal_start_session(phantom_artos_t *artos);
void artos_journal_end_session(phantom_artos_t *artos);
void artos_journal_log_stroke(phantom_artos_t *artos);
void artos_journal_log_tool_change(phantom_artos_t *artos, artos_tool_t old_tool, artos_tool_t new_tool);
void artos_journal_log_color_change(phantom_artos_t *artos, artos_color_t *old_color, artos_color_t *new_color);
void artos_journal_log_layer_op(phantom_artos_t *artos, const char *operation, int layer_index);
void artos_journal_add_note(phantom_artos_t *artos, const char *note);
void artos_journal_mark_milestone(phantom_artos_t *artos, const char *name);
void artos_journal_create_snapshot(phantom_artos_t *artos, const char *description);
cairo_surface_t *artos_journal_get_thumbnail(phantom_artos_t *artos);
int artos_journal_save(phantom_artos_t *artos, const char *filepath);
int artos_journal_load(phantom_artos_t *artos, const char *filepath);
int artos_journal_export_html(phantom_artos_t *artos, const char *filepath);
artos_journal_session_t *artos_journal_get_sessions(phantom_artos_t *artos);
int artos_journal_get_session_count(phantom_artos_t *artos);
GtkWidget *artos_create_journal_panel(phantom_artos_t *artos);

/* Stroke Archaeology (GeoFS integration) */
int artos_archaeology_init(phantom_artos_t *artos, const char *geofs_path);
void artos_archaeology_cleanup(phantom_artos_t *artos);
int artos_archaeology_get_versions(phantom_artos_t *artos, time_t **timestamps, int *count);
cairo_surface_t *artos_archaeology_restore_version(phantom_artos_t *artos, time_t timestamp);
int artos_archaeology_excavate_strokes(phantom_artos_t *artos, time_t start, time_t end);
GtkWidget *artos_create_archaeology_panel(phantom_artos_t *artos);

/* Voice Commands */
int artos_voicecmd_init(phantom_artos_t *artos);
void artos_voicecmd_cleanup(phantom_artos_t *artos);
void artos_voicecmd_enable(phantom_artos_t *artos, int enable);
void artos_voicecmd_start_listening(phantom_artos_t *artos);
void artos_voicecmd_stop_listening(phantom_artos_t *artos);
int artos_voicecmd_is_listening(phantom_artos_t *artos);
void artos_voicecmd_register(phantom_artos_t *artos, const char *phrase,
                             artos_voice_cmd_category_t category,
                             void (*action)(phantom_artos_t*, const char*),
                             const char *params, const char *feedback);
void artos_voicecmd_register_alias(phantom_artos_t *artos, const char *phrase, const char *alias);
void artos_voicecmd_set_wake_word(phantom_artos_t *artos, const char *wake_word);
void artos_voicecmd_set_threshold(phantom_artos_t *artos, double threshold);
int artos_voicecmd_process(phantom_artos_t *artos, const char *phrase, double confidence);
void artos_voicecmd_init_defaults(phantom_artos_t *artos);
GtkWidget *artos_create_voicecmd_panel(phantom_artos_t *artos);

/* AI Color Suggestion */
void artos_ai_color_suggest_init(phantom_artos_t *artos);
void artos_ai_color_suggest_enable(phantom_artos_t *artos, int enable);
void artos_ai_color_suggest_analyze(phantom_artos_t *artos);
void artos_ai_color_suggest_from_image(phantom_artos_t *artos, cairo_surface_t *image);
artos_color_t *artos_ai_color_suggest_get(phantom_artos_t *artos, int *count);
void artos_ai_color_suggest_apply(phantom_artos_t *artos, int index);
GtkWidget *artos_create_ai_color_suggest_panel(phantom_artos_t *artos);

/* AI Perspective Detection */
void artos_ai_perspective_init(phantom_artos_t *artos);
void artos_ai_perspective_enable(phantom_artos_t *artos, int enable);
void artos_ai_perspective_detect(phantom_artos_t *artos);
void artos_ai_perspective_add_vanishing_point(phantom_artos_t *artos, double x, double y);
void artos_ai_perspective_remove_vanishing_point(phantom_artos_t *artos, int index);
void artos_ai_perspective_set_horizon(phantom_artos_t *artos, double y, double angle);
void artos_ai_perspective_draw_guides(phantom_artos_t *artos, cairo_t *cr);
int artos_ai_perspective_snap_point(phantom_artos_t *artos, double *x, double *y);
GtkWidget *artos_create_ai_perspective_panel(phantom_artos_t *artos);

/* AI Sketch Cleanup */
void artos_ai_sketch_cleanup_init(phantom_artos_t *artos);
void artos_ai_sketch_cleanup_enable(phantom_artos_t *artos, int enable);
void artos_ai_sketch_cleanup_analyze(phantom_artos_t *artos);
void artos_ai_sketch_cleanup_get_issues(phantom_artos_t *artos, int *count);
void artos_ai_sketch_cleanup_apply_fix(phantom_artos_t *artos, int issue_index);
void artos_ai_sketch_cleanup_apply_all(phantom_artos_t *artos);
GtkWidget *artos_create_ai_sketch_cleanup_panel(phantom_artos_t *artos);

/* Combined AI Smart Features Panel */
GtkWidget *artos_create_ai_smart_panel(phantom_artos_t *artos);

/* ==============================================================================
 * Layer Store Columns
 * ============================================================================== */

enum {
    ARTOS_LAYER_COL_VISIBLE,
    ARTOS_LAYER_COL_LOCKED,
    ARTOS_LAYER_COL_THUMBNAIL,
    ARTOS_LAYER_COL_NAME,
    ARTOS_LAYER_COL_OPACITY,
    ARTOS_LAYER_COL_INDEX,
    ARTOS_LAYER_COL_COUNT
};

#endif /* PHANTOM_ARTOS_H */
