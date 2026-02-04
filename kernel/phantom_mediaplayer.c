/*
 * ==============================================================================
 *                        PHANTOM MEDIA PLAYER
 *                     "To Create, Not To Destroy"
 * ==============================================================================
 *
 * Media player implementation using GStreamer.
 */

#include "phantom_mediaplayer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>

/* ─────────────────────────────────────────────────────────────────────────────
 * Supported File Extensions
 * ───────────────────────────────────────────────────────────────────────────── */

static const char *audio_extensions[] = {
    ".mp3", ".flac", ".ogg", ".wav", ".aac", ".m4a", ".wma", ".opus",
    ".aiff", ".ape", ".mka", NULL
};

static const char *video_extensions[] = {
    ".mp4", ".mkv", ".avi", ".webm", ".mov", ".wmv", ".flv", ".m4v",
    ".mpeg", ".mpg", ".ogv", ".3gp", NULL
};

/* ─────────────────────────────────────────────────────────────────────────────
 * Equalizer Presets
 * ───────────────────────────────────────────────────────────────────────────── */

typedef struct {
    const char *name;
    double bands[MEDIAPLAYER_EQ_BANDS];
} eq_preset_t;

static const eq_preset_t eq_presets[] = {
    {"Flat",       {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
    {"Rock",       {5, 4, 3, 1, -1, -1, 0, 2, 3, 4}},
    {"Pop",        {-1, 2, 4, 5, 4, 2, 0, -1, -1, -1}},
    {"Jazz",       {3, 2, 1, 2, -2, -2, 0, 1, 2, 3}},
    {"Classical",  {4, 3, 2, 1, -1, -1, 0, 2, 3, 4}},
    {"Bass Boost", {6, 5, 4, 2, 0, 0, 0, 0, 0, 0}},
    {"Treble",     {0, 0, 0, 0, 0, 0, 2, 4, 5, 6}},
    {"Vocal",      {-2, -1, 0, 3, 5, 5, 4, 2, 0, -1}},
    {"Electronic", {4, 3, 1, 0, -2, -1, 1, 2, 4, 5}},
    {NULL, {0}}
};

/* ─────────────────────────────────────────────────────────────────────────────
 * Helper Functions
 * ───────────────────────────────────────────────────────────────────────────── */

static const char *get_file_extension(const char *filepath) {
    const char *ext = strrchr(filepath, '.');
    return ext ? ext : "";
}

static int strcasecmp_ext(const char *s1, const char *s2) {
    while (*s1 && *s2) {
        int c1 = tolower((unsigned char)*s1);
        int c2 = tolower((unsigned char)*s2);
        if (c1 != c2) return c1 - c2;
        s1++;
        s2++;
    }
    return tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
}

mediaplayer_media_type_t phantom_mediaplayer_get_type(const char *filepath) {
    const char *ext = get_file_extension(filepath);

    for (int i = 0; audio_extensions[i]; i++) {
        if (strcasecmp_ext(ext, audio_extensions[i]) == 0) {
            return MEDIA_TYPE_AUDIO;
        }
    }

    for (int i = 0; video_extensions[i]; i++) {
        if (strcasecmp_ext(ext, video_extensions[i]) == 0) {
            return MEDIA_TYPE_VIDEO;
        }
    }

    return MEDIA_TYPE_UNKNOWN;
}

int phantom_mediaplayer_is_supported(const char *filepath) {
    return phantom_mediaplayer_get_type(filepath) != MEDIA_TYPE_UNKNOWN;
}

void phantom_mediaplayer_format_time(int64_t ms, char *buf, size_t size) {
    if (ms < 0) ms = 0;

    int64_t seconds = ms / 1000;
    int64_t minutes = seconds / 60;
    int64_t hours = minutes / 60;

    seconds %= 60;
    minutes %= 60;

    if (hours > 0) {
        snprintf(buf, size, "%lld:%02lld:%02lld",
                 (long long)hours, (long long)minutes, (long long)seconds);
    } else {
        snprintf(buf, size, "%lld:%02lld", (long long)minutes, (long long)seconds);
    }
}

const char *phantom_mediaplayer_state_str(mediaplayer_state_t state) {
    switch (state) {
        case PLAYBACK_STOPPED:   return "Stopped";
        case PLAYBACK_PLAYING:   return "Playing";
        case PLAYBACK_PAUSED:    return "Paused";
        case PLAYBACK_BUFFERING: return "Buffering";
        case PLAYBACK_ERROR:     return "Error";
        default:                 return "Unknown";
    }
}

const char *phantom_mediaplayer_repeat_str(mediaplayer_repeat_t mode) {
    switch (mode) {
        case REPEAT_NONE: return "Off";
        case REPEAT_ONE:  return "One";
        case REPEAT_ALL:  return "All";
        default:          return "Unknown";
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * GStreamer Bus Callback
 * ───────────────────────────────────────────────────────────────────────────── */

static gboolean bus_callback(GstBus *bus, GstMessage *msg, gpointer data) {
    (void)bus;
    phantom_mediaplayer_t *player = (phantom_mediaplayer_t *)data;

    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_ERROR: {
            GError *err;
            gchar *debug;
            gst_message_parse_error(msg, &err, &debug);

            if (player->on_error) {
                player->on_error(err->message, player->callback_userdata);
            }

            g_error_free(err);
            g_free(debug);

            player->state = PLAYBACK_ERROR;
            if (player->on_state_changed) {
                player->on_state_changed(player->state, player->callback_userdata);
            }
            break;
        }

        case GST_MESSAGE_EOS:
            /* End of stream - play next track */
            if (player->playlist.repeat_mode == REPEAT_ONE) {
                phantom_mediaplayer_seek(player, 0);
                phantom_mediaplayer_play(player);
            } else {
                phantom_mediaplayer_next(player);
            }
            break;

        case GST_MESSAGE_STATE_CHANGED:
            if (GST_MESSAGE_SRC(msg) == GST_OBJECT(player->pipeline)) {
                GstState old_state, new_state, pending_state;
                gst_message_parse_state_changed(msg, &old_state, &new_state, &pending_state);
                (void)old_state;
                (void)pending_state;

                switch (new_state) {
                    case GST_STATE_PLAYING:
                        player->state = PLAYBACK_PLAYING;
                        break;
                    case GST_STATE_PAUSED:
                        player->state = PLAYBACK_PAUSED;
                        break;
                    case GST_STATE_NULL:
                    case GST_STATE_READY:
                        player->state = PLAYBACK_STOPPED;
                        break;
                    default:
                        break;
                }

                if (player->on_state_changed) {
                    player->on_state_changed(player->state, player->callback_userdata);
                }
            }
            break;

        case GST_MESSAGE_TAG: {
            GstTagList *tags;
            gst_message_parse_tag(msg, &tags);

            if (player->current_track) {
                gchar *str;

                if (gst_tag_list_get_string(tags, GST_TAG_TITLE, &str)) {
                    strncpy(player->current_track->title, str,
                            sizeof(player->current_track->title) - 1);
                    g_free(str);
                }

                if (gst_tag_list_get_string(tags, GST_TAG_ARTIST, &str)) {
                    strncpy(player->current_track->artist, str,
                            sizeof(player->current_track->artist) - 1);
                    g_free(str);
                }

                if (gst_tag_list_get_string(tags, GST_TAG_ALBUM, &str)) {
                    strncpy(player->current_track->album, str,
                            sizeof(player->current_track->album) - 1);
                    g_free(str);
                }

                guint bitrate;
                if (gst_tag_list_get_uint(tags, GST_TAG_BITRATE, &bitrate)) {
                    player->current_track->bitrate = bitrate / 1000;
                }
            }

            gst_tag_list_unref(tags);
            break;
        }

        case GST_MESSAGE_DURATION_CHANGED:
            if (player->current_track && player->pipeline) {
                gint64 duration;
                if (gst_element_query_duration(player->pipeline, GST_FORMAT_TIME, &duration)) {
                    player->current_track->duration_ms = duration / GST_MSECOND;
                }
            }
            break;

        default:
            break;
    }

    return TRUE;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Core API Implementation
 * ───────────────────────────────────────────────────────────────────────────── */

int phantom_mediaplayer_init(phantom_mediaplayer_t *player) {
    if (!player) return -1;

    memset(player, 0, sizeof(phantom_mediaplayer_t));

    /* Initialize GStreamer */
    GError *err = NULL;
    if (!gst_init_check(NULL, NULL, &err)) {
        if (err) {
            fprintf(stderr, "GStreamer init failed: %s\n", err->message);
            g_error_free(err);
        }
        return -1;
    }

    /* Initialize playlist */
    player->playlist.capacity = 100;
    player->playlist.tracks = calloc(player->playlist.capacity, sizeof(mediaplayer_track_t));
    if (!player->playlist.tracks) return -1;

    player->playlist.shuffle_order = calloc(player->playlist.capacity, sizeof(int));
    if (!player->playlist.shuffle_order) {
        free(player->playlist.tracks);
        return -1;
    }

    strcpy(player->playlist.name, "Default Playlist");
    player->playlist.current_index = -1;
    player->playlist.repeat_mode = REPEAT_NONE;

    /* Initialize volume */
    player->volume = 1.0;
    player->muted = 0;

    /* Initialize equalizer */
    phantom_mediaplayer_eq_reset(player);

    player->state = PLAYBACK_STOPPED;
    player->initialized = 1;

    return 0;
}

void phantom_mediaplayer_shutdown(phantom_mediaplayer_t *player) {
    if (!player || !player->initialized) return;

    phantom_mediaplayer_stop(player);

    if (player->pipeline) {
        gst_element_set_state(player->pipeline, GST_STATE_NULL);
        gst_object_unref(player->pipeline);
        player->pipeline = NULL;
    }

    if (player->playlist.tracks) {
        free(player->playlist.tracks);
        player->playlist.tracks = NULL;
    }

    if (player->playlist.shuffle_order) {
        free(player->playlist.shuffle_order);
        player->playlist.shuffle_order = NULL;
    }

    player->initialized = 0;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Playback Control
 * ───────────────────────────────────────────────────────────────────────────── */

int phantom_mediaplayer_play_file(phantom_mediaplayer_t *player, const char *filepath) {
    if (!player || !filepath) return -1;

    /* Stop current playback */
    phantom_mediaplayer_stop(player);

    /* Create playbin pipeline if needed */
    if (player->pipeline) {
        gst_object_unref(player->pipeline);
    }

    player->pipeline = gst_element_factory_make("playbin", "player");
    if (!player->pipeline) {
        fprintf(stderr, "Could not create playbin\n");
        return -1;
    }

    /* Set up bus watch */
    GstBus *bus = gst_element_get_bus(player->pipeline);
    gst_bus_add_watch(bus, bus_callback, player);
    gst_object_unref(bus);

    /* Create URI from filepath */
    char uri[MEDIAPLAYER_MAX_PATH + 10];
    if (strncmp(filepath, "file://", 7) == 0 ||
        strncmp(filepath, "http://", 7) == 0 ||
        strncmp(filepath, "https://", 8) == 0) {
        strncpy(uri, filepath, sizeof(uri) - 1);
    } else {
        snprintf(uri, sizeof(uri), "file://%s", filepath);
    }

    /* Set URI */
    g_object_set(player->pipeline, "uri", uri, NULL);

    /* Set volume */
    g_object_set(player->pipeline, "volume", player->muted ? 0.0 : player->volume, NULL);

    /* Set video sink if available */
    if (player->video_sink) {
        g_object_set(player->pipeline, "video-sink", player->video_sink, NULL);
    }

    /* Find or create track entry */
    player->current_track = NULL;
    for (uint32_t i = 0; i < player->playlist.track_count; i++) {
        if (strcmp(player->playlist.tracks[i].filepath, filepath) == 0) {
            player->current_track = &player->playlist.tracks[i];
            player->playlist.current_index = i;
            break;
        }
    }

    if (!player->current_track) {
        /* Add to playlist */
        phantom_mediaplayer_playlist_add(player, filepath);
        if (player->playlist.track_count > 0) {
            player->current_track = &player->playlist.tracks[player->playlist.track_count - 1];
            player->playlist.current_index = player->playlist.track_count - 1;
        }
    }

    /* Start playback */
    GstStateChangeReturn ret = gst_element_set_state(player->pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        fprintf(stderr, "Failed to start playback\n");
        return -1;
    }

    player->total_tracks_played++;

    if (player->on_track_changed && player->current_track) {
        player->on_track_changed(player->current_track, player->callback_userdata);
    }

    return 0;
}

int phantom_mediaplayer_play(phantom_mediaplayer_t *player) {
    if (!player) return -1;

    if (!player->pipeline) {
        /* Try to play first track in playlist */
        if (player->playlist.track_count > 0) {
            int index = player->playlist.current_index >= 0 ?
                        player->playlist.current_index : 0;
            return phantom_mediaplayer_play_index(player, index);
        }
        return -1;
    }

    gst_element_set_state(player->pipeline, GST_STATE_PLAYING);
    return 0;
}

int phantom_mediaplayer_pause(phantom_mediaplayer_t *player) {
    if (!player || !player->pipeline) return -1;

    gst_element_set_state(player->pipeline, GST_STATE_PAUSED);
    return 0;
}

int phantom_mediaplayer_stop(phantom_mediaplayer_t *player) {
    if (!player || !player->pipeline) return -1;

    gst_element_set_state(player->pipeline, GST_STATE_NULL);
    player->state = PLAYBACK_STOPPED;
    player->position_ms = 0;

    if (player->on_state_changed) {
        player->on_state_changed(player->state, player->callback_userdata);
    }

    return 0;
}

int phantom_mediaplayer_toggle(phantom_mediaplayer_t *player) {
    if (!player) return -1;

    if (player->state == PLAYBACK_PLAYING) {
        return phantom_mediaplayer_pause(player);
    } else {
        return phantom_mediaplayer_play(player);
    }
}

int phantom_mediaplayer_seek(phantom_mediaplayer_t *player, int64_t position_ms) {
    if (!player || !player->pipeline) return -1;

    gint64 position = position_ms * GST_MSECOND;
    gst_element_seek_simple(player->pipeline, GST_FORMAT_TIME,
                            GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT,
                            position);

    player->position_ms = position_ms;
    return 0;
}

int phantom_mediaplayer_seek_relative(phantom_mediaplayer_t *player, int64_t offset_ms) {
    if (!player) return -1;

    int64_t new_pos = phantom_mediaplayer_get_position(player) + offset_ms;
    if (new_pos < 0) new_pos = 0;

    if (player->current_track && new_pos > player->current_track->duration_ms) {
        new_pos = player->current_track->duration_ms;
    }

    return phantom_mediaplayer_seek(player, new_pos);
}

int64_t phantom_mediaplayer_get_position(phantom_mediaplayer_t *player) {
    if (!player || !player->pipeline) return 0;

    gint64 position;
    if (gst_element_query_position(player->pipeline, GST_FORMAT_TIME, &position)) {
        player->position_ms = position / GST_MSECOND;
    }

    return player->position_ms;
}

mediaplayer_state_t phantom_mediaplayer_get_state(phantom_mediaplayer_t *player) {
    return player ? player->state : PLAYBACK_STOPPED;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Volume Control
 * ───────────────────────────────────────────────────────────────────────────── */

int phantom_mediaplayer_set_volume(phantom_mediaplayer_t *player, double volume) {
    if (!player) return -1;

    if (volume < 0.0) volume = 0.0;
    if (volume > 1.0) volume = 1.0;

    player->volume = volume;

    if (player->pipeline && !player->muted) {
        g_object_set(player->pipeline, "volume", volume, NULL);
    }

    return 0;
}

double phantom_mediaplayer_get_volume(phantom_mediaplayer_t *player) {
    return player ? player->volume : 0.0;
}

int phantom_mediaplayer_set_mute(phantom_mediaplayer_t *player, int mute) {
    if (!player) return -1;

    player->muted = mute ? 1 : 0;

    if (player->pipeline) {
        g_object_set(player->pipeline, "volume",
                     player->muted ? 0.0 : player->volume, NULL);
    }

    return 0;
}

int phantom_mediaplayer_toggle_mute(phantom_mediaplayer_t *player) {
    return phantom_mediaplayer_set_mute(player, !player->muted);
}

int phantom_mediaplayer_is_muted(phantom_mediaplayer_t *player) {
    return player ? player->muted : 0;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Playlist Management
 * ───────────────────────────────────────────────────────────────────────────── */

static void generate_shuffle_order(mediaplayer_playlist_t *playlist) {
    /* Fisher-Yates shuffle */
    for (uint32_t i = 0; i < playlist->track_count; i++) {
        playlist->shuffle_order[i] = i;
    }

    for (uint32_t i = playlist->track_count - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int tmp = playlist->shuffle_order[i];
        playlist->shuffle_order[i] = playlist->shuffle_order[j];
        playlist->shuffle_order[j] = tmp;
    }
}

int phantom_mediaplayer_playlist_add(phantom_mediaplayer_t *player, const char *filepath) {
    if (!player || !filepath) return -1;

    if (!phantom_mediaplayer_is_supported(filepath)) return -1;

    /* Expand capacity if needed */
    if (player->playlist.track_count >= player->playlist.capacity) {
        uint32_t new_cap = player->playlist.capacity * 2;
        mediaplayer_track_t *new_tracks = realloc(player->playlist.tracks,
                                                   new_cap * sizeof(mediaplayer_track_t));
        int *new_order = realloc(player->playlist.shuffle_order, new_cap * sizeof(int));

        if (!new_tracks || !new_order) {
            free(new_tracks);
            free(new_order);
            return -1;
        }

        player->playlist.tracks = new_tracks;
        player->playlist.shuffle_order = new_order;
        player->playlist.capacity = new_cap;
    }

    /* Create track entry */
    mediaplayer_track_t *track = &player->playlist.tracks[player->playlist.track_count];
    memset(track, 0, sizeof(mediaplayer_track_t));

    strncpy(track->filepath, filepath, sizeof(track->filepath) - 1);
    track->type = phantom_mediaplayer_get_type(filepath);
    track->added_time = time(NULL);

    /* Extract filename as default title */
    const char *filename = strrchr(filepath, '/');
    filename = filename ? filename + 1 : filepath;
    strncpy(track->title, filename, sizeof(track->title) - 1);

    /* Remove extension from title */
    char *ext = strrchr(track->title, '.');
    if (ext) *ext = '\0';

    player->playlist.track_count++;

    /* Update shuffle order */
    if (player->playlist.shuffle_enabled) {
        generate_shuffle_order(&player->playlist);
    }

    return 0;
}

int phantom_mediaplayer_playlist_add_directory(phantom_mediaplayer_t *player,
                                                 const char *dirpath, int recursive) {
    if (!player || !dirpath) return -1;

    DIR *dir = opendir(dirpath);
    if (!dir) return -1;

    struct dirent *entry;
    int added = 0;
    char filepath[MEDIAPLAYER_MAX_PATH];

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        snprintf(filepath, sizeof(filepath), "%s/%s", dirpath, entry->d_name);

        struct stat st;
        if (stat(filepath, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            if (recursive) {
                added += phantom_mediaplayer_playlist_add_directory(player, filepath, 1);
            }
        } else if (S_ISREG(st.st_mode)) {
            if (phantom_mediaplayer_playlist_add(player, filepath) == 0) {
                added++;
            }
        }
    }

    closedir(dir);
    return added;
}

int phantom_mediaplayer_playlist_remove(phantom_mediaplayer_t *player, int index) {
    if (!player || index < 0 || (uint32_t)index >= player->playlist.track_count) {
        return -1;
    }

    /* Shift tracks down */
    for (uint32_t i = index; i < player->playlist.track_count - 1; i++) {
        player->playlist.tracks[i] = player->playlist.tracks[i + 1];
    }

    player->playlist.track_count--;

    /* Adjust current index */
    if (player->playlist.current_index >= index) {
        player->playlist.current_index--;
        if (player->playlist.current_index < 0 && player->playlist.track_count > 0) {
            player->playlist.current_index = 0;
        }
    }

    /* Update shuffle order */
    if (player->playlist.shuffle_enabled) {
        generate_shuffle_order(&player->playlist);
    }

    return 0;
}

void phantom_mediaplayer_playlist_clear(phantom_mediaplayer_t *player) {
    if (!player) return;

    phantom_mediaplayer_stop(player);
    player->playlist.track_count = 0;
    player->playlist.current_index = -1;
    player->current_track = NULL;
}

uint32_t phantom_mediaplayer_playlist_count(phantom_mediaplayer_t *player) {
    return player ? player->playlist.track_count : 0;
}

mediaplayer_track_t *phantom_mediaplayer_playlist_get(phantom_mediaplayer_t *player, int index) {
    if (!player || index < 0 || (uint32_t)index >= player->playlist.track_count) {
        return NULL;
    }
    return &player->playlist.tracks[index];
}

int phantom_mediaplayer_next(phantom_mediaplayer_t *player) {
    if (!player || player->playlist.track_count == 0) return -1;

    int next_index;

    if (player->playlist.shuffle_enabled) {
        /* Find current position in shuffle order */
        int shuffle_pos = 0;
        for (uint32_t i = 0; i < player->playlist.track_count; i++) {
            if (player->playlist.shuffle_order[i] == player->playlist.current_index) {
                shuffle_pos = i;
                break;
            }
        }

        shuffle_pos++;
        if ((uint32_t)shuffle_pos >= player->playlist.track_count) {
            if (player->playlist.repeat_mode == REPEAT_ALL) {
                generate_shuffle_order(&player->playlist);
                shuffle_pos = 0;
            } else {
                phantom_mediaplayer_stop(player);
                return 0;
            }
        }

        next_index = player->playlist.shuffle_order[shuffle_pos];
    } else {
        next_index = player->playlist.current_index + 1;

        if ((uint32_t)next_index >= player->playlist.track_count) {
            if (player->playlist.repeat_mode == REPEAT_ALL) {
                next_index = 0;
            } else {
                phantom_mediaplayer_stop(player);
                return 0;
            }
        }
    }

    return phantom_mediaplayer_play_index(player, next_index);
}

int phantom_mediaplayer_previous(phantom_mediaplayer_t *player) {
    if (!player || player->playlist.track_count == 0) return -1;

    /* If more than 3 seconds in, restart current track */
    if (phantom_mediaplayer_get_position(player) > 3000) {
        return phantom_mediaplayer_seek(player, 0);
    }

    int prev_index;

    if (player->playlist.shuffle_enabled) {
        int shuffle_pos = 0;
        for (uint32_t i = 0; i < player->playlist.track_count; i++) {
            if (player->playlist.shuffle_order[i] == player->playlist.current_index) {
                shuffle_pos = i;
                break;
            }
        }

        shuffle_pos--;
        if (shuffle_pos < 0) {
            shuffle_pos = player->playlist.track_count - 1;
        }

        prev_index = player->playlist.shuffle_order[shuffle_pos];
    } else {
        prev_index = player->playlist.current_index - 1;
        if (prev_index < 0) {
            prev_index = player->playlist.track_count - 1;
        }
    }

    return phantom_mediaplayer_play_index(player, prev_index);
}

int phantom_mediaplayer_play_index(phantom_mediaplayer_t *player, int index) {
    if (!player || index < 0 || (uint32_t)index >= player->playlist.track_count) {
        return -1;
    }

    return phantom_mediaplayer_play_file(player, player->playlist.tracks[index].filepath);
}

void phantom_mediaplayer_set_shuffle(phantom_mediaplayer_t *player, int enabled) {
    if (!player) return;

    player->playlist.shuffle_enabled = enabled ? 1 : 0;

    if (enabled) {
        generate_shuffle_order(&player->playlist);
    }
}

void phantom_mediaplayer_toggle_shuffle(phantom_mediaplayer_t *player) {
    if (player) {
        phantom_mediaplayer_set_shuffle(player, !player->playlist.shuffle_enabled);
    }
}

void phantom_mediaplayer_set_repeat(phantom_mediaplayer_t *player, mediaplayer_repeat_t mode) {
    if (player) {
        player->playlist.repeat_mode = mode;
    }
}

void phantom_mediaplayer_cycle_repeat(phantom_mediaplayer_t *player) {
    if (!player) return;

    switch (player->playlist.repeat_mode) {
        case REPEAT_NONE: player->playlist.repeat_mode = REPEAT_ALL; break;
        case REPEAT_ALL:  player->playlist.repeat_mode = REPEAT_ONE; break;
        case REPEAT_ONE:  player->playlist.repeat_mode = REPEAT_NONE; break;
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Playlist Persistence
 * ───────────────────────────────────────────────────────────────────────────── */

int phantom_mediaplayer_playlist_save(phantom_mediaplayer_t *player, const char *filepath) {
    if (!player || !filepath) return -1;

    FILE *fp = fopen(filepath, "w");
    if (!fp) return -1;

    fprintf(fp, "#EXTM3U\n");
    fprintf(fp, "#PLAYLIST:%s\n", player->playlist.name);

    for (uint32_t i = 0; i < player->playlist.track_count; i++) {
        mediaplayer_track_t *track = &player->playlist.tracks[i];

        fprintf(fp, "#EXTINF:%lld,%s - %s\n",
                (long long)(track->duration_ms / 1000),
                track->artist[0] ? track->artist : "Unknown",
                track->title);
        fprintf(fp, "%s\n", track->filepath);
    }

    fclose(fp);
    return 0;
}

int phantom_mediaplayer_playlist_load(phantom_mediaplayer_t *player, const char *filepath) {
    if (!player || !filepath) return -1;

    FILE *fp = fopen(filepath, "r");
    if (!fp) return -1;

    phantom_mediaplayer_playlist_clear(player);

    char line[MEDIAPLAYER_MAX_PATH];
    int loaded = 0;

    while (fgets(line, sizeof(line), fp)) {
        /* Remove newline */
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';

        /* Skip comments and empty lines */
        if (line[0] == '#' || line[0] == '\0') continue;

        /* Add file to playlist */
        if (phantom_mediaplayer_playlist_add(player, line) == 0) {
            loaded++;
        }
    }

    fclose(fp);
    return loaded;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Equalizer
 * ───────────────────────────────────────────────────────────────────────────── */

void phantom_mediaplayer_eq_enable(phantom_mediaplayer_t *player, int enabled) {
    if (player) {
        player->equalizer.enabled = enabled ? 1 : 0;
    }
}

void phantom_mediaplayer_eq_set_band(phantom_mediaplayer_t *player, int band, double value) {
    if (!player || band < 0 || band >= MEDIAPLAYER_EQ_BANDS) return;

    if (value < -12.0) value = -12.0;
    if (value > 12.0) value = 12.0;

    player->equalizer.bands[band] = value;
}

double phantom_mediaplayer_eq_get_band(phantom_mediaplayer_t *player, int band) {
    if (!player || band < 0 || band >= MEDIAPLAYER_EQ_BANDS) return 0.0;
    return player->equalizer.bands[band];
}

void phantom_mediaplayer_eq_load_preset(phantom_mediaplayer_t *player, const char *preset) {
    if (!player || !preset) return;

    for (int i = 0; eq_presets[i].name; i++) {
        if (strcmp(eq_presets[i].name, preset) == 0) {
            memcpy(player->equalizer.bands, eq_presets[i].bands, sizeof(player->equalizer.bands));
            strncpy(player->equalizer.preset_name, preset,
                    sizeof(player->equalizer.preset_name) - 1);
            return;
        }
    }
}

void phantom_mediaplayer_eq_reset(phantom_mediaplayer_t *player) {
    if (!player) return;

    memset(player->equalizer.bands, 0, sizeof(player->equalizer.bands));
    strcpy(player->equalizer.preset_name, "Flat");
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Video
 * ───────────────────────────────────────────────────────────────────────────── */

int phantom_mediaplayer_set_video_output(phantom_mediaplayer_t *player, void *widget) {
    if (!player) return -1;

    player->video_sink = widget;
    return 0;
}

int phantom_mediaplayer_set_fullscreen(phantom_mediaplayer_t *player, int fullscreen) {
    (void)player;
    (void)fullscreen;
    /* Fullscreen handling is done in the GUI layer */
    return 0;
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Callbacks
 * ───────────────────────────────────────────────────────────────────────────── */

void phantom_mediaplayer_set_state_callback(phantom_mediaplayer_t *player,
    void (*callback)(mediaplayer_state_t state, void *userdata), void *userdata) {
    if (player) {
        player->on_state_changed = callback;
        player->callback_userdata = userdata;
    }
}

void phantom_mediaplayer_set_position_callback(phantom_mediaplayer_t *player,
    void (*callback)(int64_t position_ms, void *userdata), void *userdata) {
    if (player) {
        player->on_position_changed = callback;
        player->callback_userdata = userdata;
    }
}

void phantom_mediaplayer_set_track_callback(phantom_mediaplayer_t *player,
    void (*callback)(const mediaplayer_track_t *track, void *userdata), void *userdata) {
    if (player) {
        player->on_track_changed = callback;
        player->callback_userdata = userdata;
    }
}

void phantom_mediaplayer_set_error_callback(phantom_mediaplayer_t *player,
    void (*callback)(const char *message, void *userdata), void *userdata) {
    if (player) {
        player->on_error = callback;
        player->callback_userdata = userdata;
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Metadata
 * ───────────────────────────────────────────────────────────────────────────── */

int phantom_mediaplayer_get_metadata(const char *filepath, mediaplayer_track_t *track) {
    if (!filepath || !track) return -1;

    memset(track, 0, sizeof(mediaplayer_track_t));
    strncpy(track->filepath, filepath, sizeof(track->filepath) - 1);
    track->type = phantom_mediaplayer_get_type(filepath);

    /* Extract filename as default title */
    const char *filename = strrchr(filepath, '/');
    filename = filename ? filename + 1 : filepath;
    strncpy(track->title, filename, sizeof(track->title) - 1);

    /* Remove extension */
    char *ext = strrchr(track->title, '.');
    if (ext) *ext = '\0';

    /* Use GStreamer discoverer for detailed metadata */
    GstDiscoverer *discoverer = gst_discoverer_new(5 * GST_SECOND, NULL);
    if (!discoverer) return 0;

    char uri[MEDIAPLAYER_MAX_PATH + 10];
    snprintf(uri, sizeof(uri), "file://%s", filepath);

    GstDiscovererInfo *info = gst_discoverer_discover_uri(discoverer, uri, NULL);
    if (info) {
        /* Duration */
        track->duration_ms = gst_discoverer_info_get_duration(info) / GST_MSECOND;

        /* Tags */
        const GstTagList *tags = gst_discoverer_info_get_tags(info);
        if (tags) {
            gchar *str;
            if (gst_tag_list_get_string(tags, GST_TAG_TITLE, &str)) {
                strncpy(track->title, str, sizeof(track->title) - 1);
                g_free(str);
            }
            if (gst_tag_list_get_string(tags, GST_TAG_ARTIST, &str)) {
                strncpy(track->artist, str, sizeof(track->artist) - 1);
                g_free(str);
            }
            if (gst_tag_list_get_string(tags, GST_TAG_ALBUM, &str)) {
                strncpy(track->album, str, sizeof(track->album) - 1);
                g_free(str);
            }

            guint bitrate;
            if (gst_tag_list_get_uint(tags, GST_TAG_BITRATE, &bitrate)) {
                track->bitrate = bitrate / 1000;
            }
        }

        /* Stream info for video dimensions */
        GList *streams = gst_discoverer_info_get_video_streams(info);
        if (streams) {
            GstDiscovererVideoInfo *vinfo = (GstDiscovererVideoInfo *)streams->data;
            track->width = gst_discoverer_video_info_get_width(vinfo);
            track->height = gst_discoverer_video_info_get_height(vinfo);
            gst_discoverer_stream_info_list_free(streams);
        }

        /* Audio info */
        streams = gst_discoverer_info_get_audio_streams(info);
        if (streams) {
            GstDiscovererAudioInfo *ainfo = (GstDiscovererAudioInfo *)streams->data;
            track->sample_rate = gst_discoverer_audio_info_get_sample_rate(ainfo);
            track->channels = gst_discoverer_audio_info_get_channels(ainfo);
            gst_discoverer_stream_info_list_free(streams);
        }

        gst_discoverer_info_unref(info);
    }

    g_object_unref(discoverer);
    return 0;
}
