/*
 * ==============================================================================
 *                        PHANTOM MEDIA PLAYER
 *                     "To Create, Not To Destroy"
 * ==============================================================================
 *
 * Media player for PhantomOS using GStreamer backend.
 * Supports audio and video playback with playlist management.
 *
 * Features:
 * - Audio playback (MP3, FLAC, OGG, WAV, AAC)
 * - Video playback (MP4, MKV, AVI, WebM)
 * - Playlist management
 * - Shuffle and repeat modes
 * - Volume control
 * - Seeking and position tracking
 * - Metadata extraction (artist, album, title)
 */

#ifndef PHANTOM_MEDIAPLAYER_H
#define PHANTOM_MEDIAPLAYER_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>

/* ─────────────────────────────────────────────────────────────────────────────
 * Constants
 * ───────────────────────────────────────────────────────────────────────────── */

#define MEDIAPLAYER_MAX_PATH        4096
#define MEDIAPLAYER_MAX_TITLE       256
#define MEDIAPLAYER_MAX_ARTIST      256
#define MEDIAPLAYER_MAX_ALBUM       256
#define MEDIAPLAYER_MAX_PLAYLIST    1000

/* ─────────────────────────────────────────────────────────────────────────────
 * Playback State
 * ───────────────────────────────────────────────────────────────────────────── */

typedef enum {
    PLAYBACK_STOPPED = 0,
    PLAYBACK_PLAYING = 1,
    PLAYBACK_PAUSED = 2,
    PLAYBACK_BUFFERING = 3,
    PLAYBACK_ERROR = 4
} mediaplayer_state_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Media Type
 * ───────────────────────────────────────────────────────────────────────────── */

typedef enum {
    MEDIA_TYPE_UNKNOWN = 0,
    MEDIA_TYPE_AUDIO,
    MEDIA_TYPE_VIDEO
} mediaplayer_media_type_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Repeat Mode
 * ───────────────────────────────────────────────────────────────────────────── */

typedef enum {
    REPEAT_NONE = 0,
    REPEAT_ONE,
    REPEAT_ALL
} mediaplayer_repeat_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Media File Info
 * ───────────────────────────────────────────────────────────────────────────── */

typedef struct mediaplayer_track {
    char filepath[MEDIAPLAYER_MAX_PATH];
    char title[MEDIAPLAYER_MAX_TITLE];
    char artist[MEDIAPLAYER_MAX_ARTIST];
    char album[MEDIAPLAYER_MAX_ALBUM];
    mediaplayer_media_type_t type;
    int64_t duration_ms;        /* Duration in milliseconds */
    int bitrate;                /* Bitrate in kbps */
    int sample_rate;            /* Sample rate in Hz */
    int channels;               /* Number of audio channels */
    int width;                  /* Video width (0 for audio) */
    int height;                 /* Video height (0 for audio) */
    time_t added_time;          /* When added to playlist */
} mediaplayer_track_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Playlist
 * ───────────────────────────────────────────────────────────────────────────── */

typedef struct mediaplayer_playlist {
    char name[MEDIAPLAYER_MAX_TITLE];
    mediaplayer_track_t *tracks;
    uint32_t track_count;
    uint32_t capacity;
    int current_index;
    int shuffle_enabled;
    mediaplayer_repeat_t repeat_mode;
    int *shuffle_order;         /* Shuffled indices */
} mediaplayer_playlist_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Equalizer Band
 * ───────────────────────────────────────────────────────────────────────────── */

#define MEDIAPLAYER_EQ_BANDS 10

typedef struct mediaplayer_equalizer {
    int enabled;
    double bands[MEDIAPLAYER_EQ_BANDS];  /* -12.0 to +12.0 dB */
    char preset_name[64];
} mediaplayer_equalizer_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Media Player Context
 * ───────────────────────────────────────────────────────────────────────────── */

typedef struct phantom_mediaplayer {
    int initialized;

    /* Playback state */
    mediaplayer_state_t state;
    mediaplayer_track_t *current_track;
    int64_t position_ms;        /* Current position in milliseconds */
    double volume;              /* 0.0 to 1.0 */
    int muted;

    /* Playlist */
    mediaplayer_playlist_t playlist;

    /* Equalizer */
    mediaplayer_equalizer_t equalizer;

    /* GStreamer pipeline (void* to avoid header dependency) */
    void *pipeline;
    void *video_sink;
    void *audio_sink;

    /* Callbacks */
    void (*on_state_changed)(mediaplayer_state_t state, void *userdata);
    void (*on_position_changed)(int64_t position_ms, void *userdata);
    void (*on_track_changed)(const mediaplayer_track_t *track, void *userdata);
    void (*on_error)(const char *message, void *userdata);
    void *callback_userdata;

    /* Statistics */
    uint64_t total_tracks_played;
    uint64_t total_play_time_ms;
} phantom_mediaplayer_t;

/* ─────────────────────────────────────────────────────────────────────────────
 * Core API
 * ───────────────────────────────────────────────────────────────────────────── */

/* Initialize the media player */
int phantom_mediaplayer_init(phantom_mediaplayer_t *player);

/* Shutdown and cleanup */
void phantom_mediaplayer_shutdown(phantom_mediaplayer_t *player);

/* ─────────────────────────────────────────────────────────────────────────────
 * Playback Control
 * ───────────────────────────────────────────────────────────────────────────── */

/* Load and play a media file */
int phantom_mediaplayer_play_file(phantom_mediaplayer_t *player, const char *filepath);

/* Play current track or resume */
int phantom_mediaplayer_play(phantom_mediaplayer_t *player);

/* Pause playback */
int phantom_mediaplayer_pause(phantom_mediaplayer_t *player);

/* Stop playback */
int phantom_mediaplayer_stop(phantom_mediaplayer_t *player);

/* Toggle play/pause */
int phantom_mediaplayer_toggle(phantom_mediaplayer_t *player);

/* Seek to position (in milliseconds) */
int phantom_mediaplayer_seek(phantom_mediaplayer_t *player, int64_t position_ms);

/* Seek relative to current position */
int phantom_mediaplayer_seek_relative(phantom_mediaplayer_t *player, int64_t offset_ms);

/* Get current position */
int64_t phantom_mediaplayer_get_position(phantom_mediaplayer_t *player);

/* Get current state */
mediaplayer_state_t phantom_mediaplayer_get_state(phantom_mediaplayer_t *player);

/* ─────────────────────────────────────────────────────────────────────────────
 * Volume Control
 * ───────────────────────────────────────────────────────────────────────────── */

/* Set volume (0.0 to 1.0) */
int phantom_mediaplayer_set_volume(phantom_mediaplayer_t *player, double volume);

/* Get volume */
double phantom_mediaplayer_get_volume(phantom_mediaplayer_t *player);

/* Mute/unmute */
int phantom_mediaplayer_set_mute(phantom_mediaplayer_t *player, int mute);

/* Toggle mute */
int phantom_mediaplayer_toggle_mute(phantom_mediaplayer_t *player);

/* Check if muted */
int phantom_mediaplayer_is_muted(phantom_mediaplayer_t *player);

/* ─────────────────────────────────────────────────────────────────────────────
 * Playlist Management
 * ───────────────────────────────────────────────────────────────────────────── */

/* Add file to playlist */
int phantom_mediaplayer_playlist_add(phantom_mediaplayer_t *player, const char *filepath);

/* Add directory to playlist (recursive) */
int phantom_mediaplayer_playlist_add_directory(phantom_mediaplayer_t *player,
                                                 const char *dirpath, int recursive);

/* Remove track from playlist */
int phantom_mediaplayer_playlist_remove(phantom_mediaplayer_t *player, int index);

/* Clear playlist */
void phantom_mediaplayer_playlist_clear(phantom_mediaplayer_t *player);

/* Get playlist track count */
uint32_t phantom_mediaplayer_playlist_count(phantom_mediaplayer_t *player);

/* Get track at index */
mediaplayer_track_t *phantom_mediaplayer_playlist_get(phantom_mediaplayer_t *player, int index);

/* Play next track */
int phantom_mediaplayer_next(phantom_mediaplayer_t *player);

/* Play previous track */
int phantom_mediaplayer_previous(phantom_mediaplayer_t *player);

/* Play track at index */
int phantom_mediaplayer_play_index(phantom_mediaplayer_t *player, int index);

/* Set shuffle mode */
void phantom_mediaplayer_set_shuffle(phantom_mediaplayer_t *player, int enabled);

/* Toggle shuffle */
void phantom_mediaplayer_toggle_shuffle(phantom_mediaplayer_t *player);

/* Set repeat mode */
void phantom_mediaplayer_set_repeat(phantom_mediaplayer_t *player, mediaplayer_repeat_t mode);

/* Cycle repeat mode */
void phantom_mediaplayer_cycle_repeat(phantom_mediaplayer_t *player);

/* ─────────────────────────────────────────────────────────────────────────────
 * Playlist Persistence
 * ───────────────────────────────────────────────────────────────────────────── */

/* Save playlist to file */
int phantom_mediaplayer_playlist_save(phantom_mediaplayer_t *player, const char *filepath);

/* Load playlist from file */
int phantom_mediaplayer_playlist_load(phantom_mediaplayer_t *player, const char *filepath);

/* ─────────────────────────────────────────────────────────────────────────────
 * Equalizer
 * ───────────────────────────────────────────────────────────────────────────── */

/* Enable/disable equalizer */
void phantom_mediaplayer_eq_enable(phantom_mediaplayer_t *player, int enabled);

/* Set equalizer band (0-9, value -12.0 to +12.0 dB) */
void phantom_mediaplayer_eq_set_band(phantom_mediaplayer_t *player, int band, double value);

/* Get equalizer band value */
double phantom_mediaplayer_eq_get_band(phantom_mediaplayer_t *player, int band);

/* Load equalizer preset */
void phantom_mediaplayer_eq_load_preset(phantom_mediaplayer_t *player, const char *preset);

/* Reset equalizer to flat */
void phantom_mediaplayer_eq_reset(phantom_mediaplayer_t *player);

/* ─────────────────────────────────────────────────────────────────────────────
 * Metadata
 * ───────────────────────────────────────────────────────────────────────────── */

/* Get metadata for a file without adding to playlist */
int phantom_mediaplayer_get_metadata(const char *filepath, mediaplayer_track_t *track);

/* ─────────────────────────────────────────────────────────────────────────────
 * Video
 * ───────────────────────────────────────────────────────────────────────────── */

/* Set video output widget (GtkDrawingArea or similar) */
int phantom_mediaplayer_set_video_output(phantom_mediaplayer_t *player, void *widget);

/* Set video fullscreen */
int phantom_mediaplayer_set_fullscreen(phantom_mediaplayer_t *player, int fullscreen);

/* ─────────────────────────────────────────────────────────────────────────────
 * Callbacks
 * ───────────────────────────────────────────────────────────────────────────── */

/* Set state change callback */
void phantom_mediaplayer_set_state_callback(phantom_mediaplayer_t *player,
    void (*callback)(mediaplayer_state_t state, void *userdata), void *userdata);

/* Set position change callback */
void phantom_mediaplayer_set_position_callback(phantom_mediaplayer_t *player,
    void (*callback)(int64_t position_ms, void *userdata), void *userdata);

/* Set track change callback */
void phantom_mediaplayer_set_track_callback(phantom_mediaplayer_t *player,
    void (*callback)(const mediaplayer_track_t *track, void *userdata), void *userdata);

/* Set error callback */
void phantom_mediaplayer_set_error_callback(phantom_mediaplayer_t *player,
    void (*callback)(const char *message, void *userdata), void *userdata);

/* ─────────────────────────────────────────────────────────────────────────────
 * Utility Functions
 * ───────────────────────────────────────────────────────────────────────────── */

/* Format time as MM:SS or HH:MM:SS */
void phantom_mediaplayer_format_time(int64_t ms, char *buf, size_t size);

/* Get state as string */
const char *phantom_mediaplayer_state_str(mediaplayer_state_t state);

/* Get repeat mode as string */
const char *phantom_mediaplayer_repeat_str(mediaplayer_repeat_t mode);

/* Check if file is supported media */
int phantom_mediaplayer_is_supported(const char *filepath);

/* Get media type from file */
mediaplayer_media_type_t phantom_mediaplayer_get_type(const char *filepath);

#endif /* PHANTOM_MEDIAPLAYER_H */
