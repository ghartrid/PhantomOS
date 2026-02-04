/*
 * ==============================================================================
 *                                 ARTOS
 *                    Digital Art Studio for PhantomOS
 *                       "To Create, Not To Destroy"
 * ==============================================================================
 */

#include "phantom_artos.h"
#include "governor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <cairo.h>

/* ==============================================================================
 * Forward Declarations
 * ============================================================================== */

static void artos_build_ui(phantom_artos_t *artos);
static GtkWidget *artos_create_toolbar(phantom_artos_t *artos);
static GtkWidget *artos_create_tool_palette(phantom_artos_t *artos);
static GtkWidget *artos_create_brush_settings(phantom_artos_t *artos);
static GtkWidget *artos_create_layer_panel(phantom_artos_t *artos);
static GtkWidget *artos_create_color_panel(phantom_artos_t *artos);
static gboolean on_canvas_draw(GtkWidget *widget, cairo_t *cr, phantom_artos_t *artos);
static gboolean on_canvas_button_press(GtkWidget *widget, GdkEventButton *event, phantom_artos_t *artos);
static gboolean on_canvas_button_release(GtkWidget *widget, GdkEventButton *event, phantom_artos_t *artos);
static gboolean on_canvas_motion(GtkWidget *widget, GdkEventMotion *event, phantom_artos_t *artos);
static gboolean on_canvas_scroll(GtkWidget *widget, GdkEventScroll *event, phantom_artos_t *artos);
static void on_tool_selected(GtkToggleButton *button, phantom_artos_t *artos);
static void on_brush_size_changed(GtkRange *range, phantom_artos_t *artos);
static void on_brush_opacity_changed(GtkRange *range, phantom_artos_t *artos);
static void on_brush_hardness_changed(GtkRange *range, phantom_artos_t *artos);
static void on_color_set(GtkColorButton *button, phantom_artos_t *artos);
static void lambda_quick_color(GtkButton *button, phantom_artos_t *artos);
static void on_layer_add_clicked(GtkButton *button, phantom_artos_t *artos);
static void on_layer_visibility_toggled(GtkCellRendererToggle *renderer, gchar *path, phantom_artos_t *artos);
static void artos_refresh_layer_list(phantom_artos_t *artos);
static void artos_begin_stroke(phantom_artos_t *artos, double x, double y, double pressure);
static void artos_continue_stroke(phantom_artos_t *artos, double x, double y, double pressure);
static void artos_end_stroke(phantom_artos_t *artos);
static void artos_render_brush_dab(phantom_artos_t *artos, cairo_t *cr, double x, double y, double pressure);

/* Dictation drawing forward declarations */
GtkWidget *artos_create_dictation_panel(phantom_artos_t *artos);
static void on_dictation_toggle(GtkToggleButton *button, phantom_artos_t *artos);
static void on_dictation_entry_activate(GtkEntry *entry, phantom_artos_t *artos);
static void on_listen_button_clicked(GtkButton *button, phantom_artos_t *artos);
void artos_dictation_init(phantom_artos_t *artos);
int artos_dictation_parse(const char *text, artos_dictation_parsed_t *result);
int artos_dictation_execute(phantom_artos_t *artos, artos_dictation_parsed_t *cmd);
int artos_voice_is_listening(phantom_artos_t *artos);
void artos_voice_stop_listening(phantom_artos_t *artos);

/* Face tracking forward declarations */
GtkWidget *artos_create_facetrack_panel(phantom_artos_t *artos);
int artos_facetrack_init(phantom_artos_t *artos);
void artos_facetrack_cleanup(phantom_artos_t *artos);
void artos_facetrack_start(phantom_artos_t *artos);
void artos_facetrack_stop(phantom_artos_t *artos);
static gboolean facetrack_preview_refresh(gpointer data);

/* AI assistance forward declarations */
GtkWidget *artos_create_ai_panel(phantom_artos_t *artos);
int artos_ai_init(phantom_artos_t *artos);
void artos_ai_cleanup(phantom_artos_t *artos);
void artos_ai_clear_suggestions(phantom_artos_t *artos);

/* Voice-to-Art forward declarations */
GtkWidget *artos_create_voiceart_panel(phantom_artos_t *artos);
int artos_voiceart_init(phantom_artos_t *artos);
void artos_voiceart_cleanup(phantom_artos_t *artos);

/* Collaboration forward declarations */
GtkWidget *artos_create_collab_panel(phantom_artos_t *artos);
int artos_collab_init(phantom_artos_t *artos);
void artos_collab_cleanup(phantom_artos_t *artos);
void artos_collab_leave_session(phantom_artos_t *artos);

/* DrawNet forward declarations */
GtkWidget *artos_create_drawnet_panel(phantom_artos_t *artos);
int artos_drawnet_init(phantom_artos_t *artos);
void artos_drawnet_cleanup(phantom_artos_t *artos);
void artos_drawnet_leave_session(phantom_artos_t *artos);

/* ==============================================================================
 * Color Utilities
 * ============================================================================== */

void artos_color_from_hsv(artos_color_t *color, double h, double s, double v) {
    double c = v * s;
    double x = c * (1 - fabs(fmod(h / 60.0, 2) - 1));
    double m = v - c;
    double r, g, b;

    if (h < 60)      { r = c; g = x; b = 0; }
    else if (h < 120) { r = x; g = c; b = 0; }
    else if (h < 180) { r = 0; g = c; b = x; }
    else if (h < 240) { r = 0; g = x; b = c; }
    else if (h < 300) { r = x; g = 0; b = c; }
    else             { r = c; g = 0; b = x; }

    color->r = r + m;
    color->g = g + m;
    color->b = b + m;
}

void artos_color_to_hsv(artos_color_t *color, double *h, double *s, double *v) {
    double max = fmax(color->r, fmax(color->g, color->b));
    double min = fmin(color->r, fmin(color->g, color->b));
    double d = max - min;

    *v = max;
    *s = (max == 0) ? 0 : d / max;

    if (d == 0) {
        *h = 0;
    } else if (max == color->r) {
        *h = 60 * fmod((color->g - color->b) / d, 6);
    } else if (max == color->g) {
        *h = 60 * ((color->b - color->r) / d + 2);
    } else {
        *h = 60 * ((color->r - color->g) / d + 4);
    }
    if (*h < 0) *h += 360;
}

void artos_color_from_hex(artos_color_t *color, const char *hex) {
    if (hex[0] == '#') hex++;
    unsigned int r, g, b;
    if (sscanf(hex, "%02x%02x%02x", &r, &g, &b) == 3) {
        color->r = r / 255.0;
        color->g = g / 255.0;
        color->b = b / 255.0;
        color->a = 1.0;
    }
}

void artos_color_to_hex(artos_color_t *color, char *hex, size_t len) {
    snprintf(hex, len, "#%02X%02X%02X",
             (unsigned int)(color->r * 255),
             (unsigned int)(color->g * 255),
             (unsigned int)(color->b * 255));
}

/* ==============================================================================
 * Document Management
 * ============================================================================== */

artos_document_t *artos_document_new(int width, int height, const char *name) {
    artos_document_t *doc = calloc(1, sizeof(artos_document_t));
    if (!doc) return NULL;

    strncpy(doc->name, name ? name : "Untitled", sizeof(doc->name) - 1);
    doc->width = width;
    doc->height = height;
    doc->dpi = 72;
    doc->layer_count = 0;
    doc->active_layer = -1;
    doc->modified = 0;
    doc->composite_dirty = 1;

    /* Create composite surface */
    doc->composite = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);

    /* Add default background layer */
    artos_layer_add(doc, "Background");

    /* Fill background with white */
    if (doc->layers[0]) {
        cairo_t *cr = cairo_create(doc->layers[0]->surface);
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_paint(cr);
        cairo_destroy(cr);
    }

    return doc;
}

void artos_document_free(artos_document_t *doc) {
    if (!doc) return;

    /* Free layers */
    for (int i = 0; i < doc->layer_count; i++) {
        if (doc->layers[i]) {
            if (doc->layers[i]->surface) {
                cairo_surface_destroy(doc->layers[i]->surface);
            }
            free(doc->layers[i]);
        }
    }

    /* Free undo stack */
    artos_stroke_t *stroke = doc->undo_stack;
    while (stroke) {
        artos_stroke_t *next = stroke->next;
        if (stroke->points) free(stroke->points);
        if (stroke->before_snapshot) cairo_surface_destroy(stroke->before_snapshot);
        free(stroke);
        stroke = next;
    }

    /* Free redo stack */
    stroke = doc->redo_stack;
    while (stroke) {
        artos_stroke_t *next = stroke->next;
        if (stroke->points) free(stroke->points);
        if (stroke->before_snapshot) cairo_surface_destroy(stroke->before_snapshot);
        free(stroke);
        stroke = next;
    }

    /* Free selection mask */
    if (doc->selection.mask) {
        cairo_surface_destroy(doc->selection.mask);
    }

    /* Free composite */
    if (doc->composite) {
        cairo_surface_destroy(doc->composite);
    }

    free(doc);
}

int artos_document_export_png(artos_document_t *doc, const char *filepath) {
    if (!doc || !filepath) return -1;

    artos_update_composite(doc);
    cairo_status_t status = cairo_surface_write_to_png(doc->composite, filepath);
    return (status == CAIRO_STATUS_SUCCESS) ? 0 : -1;
}

/* ==============================================================================
 * Layer Operations
 * ============================================================================== */

int artos_layer_add(artos_document_t *doc, const char *name) {
    if (!doc || doc->layer_count >= ARTOS_MAX_LAYERS) return -1;

    artos_layer_t *layer = calloc(1, sizeof(artos_layer_t));
    if (!layer) return -1;

    strncpy(layer->name, name ? name : "Layer", sizeof(layer->name) - 1);
    layer->width = doc->width;
    layer->height = doc->height;
    layer->visible = 1;
    layer->locked = 0;
    layer->opacity = 1.0;
    layer->blend_mode = ARTOS_BLEND_NORMAL;

    /* Create transparent surface */
    layer->surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                                 doc->width, doc->height);
    if (cairo_surface_status(layer->surface) != CAIRO_STATUS_SUCCESS) {
        free(layer);
        return -1;
    }

    /* Clear to transparent */
    cairo_t *cr = cairo_create(layer->surface);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_destroy(cr);

    doc->layers[doc->layer_count] = layer;
    doc->active_layer = doc->layer_count;
    doc->layer_count++;
    doc->composite_dirty = 1;
    doc->modified = 1;

    return doc->layer_count - 1;
}

int artos_layer_remove(artos_document_t *doc, int index) {
    /* In Phantom philosophy, we don't destroy - we hide */
    if (!doc || index < 0 || index >= doc->layer_count) return -1;

    doc->layers[index]->visible = 0;
    doc->composite_dirty = 1;
    doc->modified = 1;

    /* Note: Layer data preserved in geological history */
    return 0;
}

int artos_layer_duplicate(artos_document_t *doc, int index) {
    if (!doc || index < 0 || index >= doc->layer_count) return -1;
    if (doc->layer_count >= ARTOS_MAX_LAYERS) return -1;

    artos_layer_t *src = doc->layers[index];
    artos_layer_t *layer = calloc(1, sizeof(artos_layer_t));
    if (!layer) return -1;

    snprintf(layer->name, sizeof(layer->name), "%.55s copy", src->name);
    layer->width = src->width;
    layer->height = src->height;
    layer->visible = 1;
    layer->locked = 0;
    layer->opacity = src->opacity;
    layer->blend_mode = src->blend_mode;

    layer->surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                                 src->width, src->height);

    /* Copy content */
    cairo_t *cr = cairo_create(layer->surface);
    cairo_set_source_surface(cr, src->surface, 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);

    /* Insert above source */
    for (int i = doc->layer_count; i > index + 1; i--) {
        doc->layers[i] = doc->layers[i - 1];
    }
    doc->layers[index + 1] = layer;
    doc->layer_count++;
    doc->active_layer = index + 1;
    doc->composite_dirty = 1;
    doc->modified = 1;

    return index + 1;
}

void artos_layer_set_visible(artos_document_t *doc, int index, int visible) {
    if (!doc || index < 0 || index >= doc->layer_count) return;
    doc->layers[index]->visible = visible;
    doc->composite_dirty = 1;
}

void artos_layer_set_opacity(artos_document_t *doc, int index, double opacity) {
    if (!doc || index < 0 || index >= doc->layer_count) return;
    doc->layers[index]->opacity = fmax(0.0, fmin(1.0, opacity));
    doc->composite_dirty = 1;
}

artos_layer_t *artos_layer_get_active(artos_document_t *doc) {
    if (!doc || doc->active_layer < 0 || doc->active_layer >= doc->layer_count) {
        return NULL;
    }
    return doc->layers[doc->active_layer];
}

/* ==============================================================================
 * Layer Mask Operations
 * ============================================================================== */

int artos_layer_add_mask(artos_document_t *doc, int index) {
    if (!doc || index < 0 || index >= doc->layer_count) return -1;
    artos_layer_t *layer = doc->layers[index];
    if (layer->mask) return 0;  /* Already has mask */

    /* Create white mask (fully opaque) */
    layer->mask = cairo_image_surface_create(CAIRO_FORMAT_A8, layer->width, layer->height);
    cairo_t *cr = cairo_create(layer->mask);
    cairo_set_source_rgba(cr, 1, 1, 1, 1);
    cairo_paint(cr);
    cairo_destroy(cr);

    layer->mask_enabled = 1;
    doc->composite_dirty = 1;
    doc->modified = 1;
    return 0;
}

void artos_layer_delete_mask(artos_document_t *doc, int index) {
    if (!doc || index < 0 || index >= doc->layer_count) return;
    artos_layer_t *layer = doc->layers[index];
    if (layer->mask) {
        cairo_surface_destroy(layer->mask);
        layer->mask = NULL;
        layer->mask_enabled = 0;
        doc->composite_dirty = 1;
        doc->modified = 1;
    }
}

void artos_layer_enable_mask(artos_document_t *doc, int index, int enable) {
    if (!doc || index < 0 || index >= doc->layer_count) return;
    doc->layers[index]->mask_enabled = enable && doc->layers[index]->mask;
    doc->composite_dirty = 1;
}

void artos_layer_set_clipping(artos_document_t *doc, int index, int clip) {
    if (!doc || index < 0 || index >= doc->layer_count) return;
    doc->layers[index]->clipping = clip;
    doc->composite_dirty = 1;
    doc->modified = 1;
}

void artos_layer_apply_mask(artos_document_t *doc, int index) {
    if (!doc || index < 0 || index >= doc->layer_count) return;
    artos_layer_t *layer = doc->layers[index];
    if (!layer->mask) return;

    /* Apply mask to layer content */
    cairo_t *cr = cairo_create(layer->surface);
    cairo_set_operator(cr, CAIRO_OPERATOR_DEST_IN);
    cairo_mask_surface(cr, layer->mask, 0, 0);
    cairo_destroy(cr);

    /* Delete the mask */
    cairo_surface_destroy(layer->mask);
    layer->mask = NULL;
    layer->mask_enabled = 0;
    doc->composite_dirty = 1;
    doc->modified = 1;
}

/* ==============================================================================
 * Transform Operations
 * ============================================================================== */

void artos_transform_begin(phantom_artos_t *artos, artos_transform_mode_t mode) {
    if (!artos || !artos->document) return;
    artos->transform_mode = mode;
    artos->transforming = 1;
    artos->transform_angle = 0;
    artos->transform_scale_x = 1.0;
    artos->transform_scale_y = 1.0;

    /* Create preview surface */
    artos_layer_t *layer = artos_layer_get_active(artos->document);
    if (layer && !artos->transform_preview) {
        artos->transform_preview = cairo_surface_create_similar(
            layer->surface, CAIRO_CONTENT_COLOR_ALPHA,
            layer->width, layer->height);
        cairo_t *cr = cairo_create(artos->transform_preview);
        cairo_set_source_surface(cr, layer->surface, 0, 0);
        cairo_paint(cr);
        cairo_destroy(cr);
    }
}

void artos_transform_apply(phantom_artos_t *artos) {
    if (!artos || !artos->transforming || !artos->document) return;

    artos_layer_t *layer = artos_layer_get_active(artos->document);
    if (!layer) return;

    cairo_t *cr = cairo_create(layer->surface);

    /* Clear and apply transformed content */
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    /* Apply transformation */
    cairo_translate(cr, layer->width / 2.0, layer->height / 2.0);
    cairo_rotate(cr, artos->transform_angle * M_PI / 180.0);
    cairo_scale(cr, artos->transform_scale_x, artos->transform_scale_y);
    cairo_translate(cr, -layer->width / 2.0, -layer->height / 2.0);

    if (artos->transform_preview) {
        cairo_set_source_surface(cr, artos->transform_preview, 0, 0);
        cairo_paint(cr);
    }

    cairo_destroy(cr);

    /* Cleanup */
    if (artos->transform_preview) {
        cairo_surface_destroy(artos->transform_preview);
        artos->transform_preview = NULL;
    }

    artos->transforming = 0;
    artos->transform_mode = ARTOS_TRANSFORM_NONE;
    artos->document->composite_dirty = 1;
    artos->document->modified = 1;

    gtk_widget_queue_draw(artos->canvas_area);
}

void artos_transform_cancel(phantom_artos_t *artos) {
    if (!artos) return;
    if (artos->transform_preview) {
        cairo_surface_destroy(artos->transform_preview);
        artos->transform_preview = NULL;
    }
    artos->transforming = 0;
    artos->transform_mode = ARTOS_TRANSFORM_NONE;
    gtk_widget_queue_draw(artos->canvas_area);
}

void artos_transform_rotate(phantom_artos_t *artos, double angle) {
    if (!artos) return;
    artos->transform_angle += angle;
    gtk_widget_queue_draw(artos->canvas_area);
}

void artos_transform_scale(phantom_artos_t *artos, double sx, double sy) {
    if (!artos) return;
    artos->transform_scale_x *= sx;
    artos->transform_scale_y *= sy;
    gtk_widget_queue_draw(artos->canvas_area);
}

void artos_transform_flip_horizontal(phantom_artos_t *artos) {
    if (!artos || !artos->document) return;
    artos_layer_t *layer = artos_layer_get_active(artos->document);
    if (!layer) return;

    cairo_surface_t *temp = cairo_image_surface_create(
        cairo_image_surface_get_format(layer->surface),
        layer->width, layer->height);

    cairo_t *cr = cairo_create(temp);
    cairo_translate(cr, layer->width, 0);
    cairo_scale(cr, -1, 1);
    cairo_set_source_surface(cr, layer->surface, 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);

    /* Copy back */
    cr = cairo_create(layer->surface);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_surface(cr, temp, 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);

    cairo_surface_destroy(temp);
    artos->document->composite_dirty = 1;
    artos->document->modified = 1;
    gtk_widget_queue_draw(artos->canvas_area);
}

void artos_transform_flip_vertical(phantom_artos_t *artos) {
    if (!artos || !artos->document) return;
    artos_layer_t *layer = artos_layer_get_active(artos->document);
    if (!layer) return;

    cairo_surface_t *temp = cairo_image_surface_create(
        cairo_image_surface_get_format(layer->surface),
        layer->width, layer->height);

    cairo_t *cr = cairo_create(temp);
    cairo_translate(cr, 0, layer->height);
    cairo_scale(cr, 1, -1);
    cairo_set_source_surface(cr, layer->surface, 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);

    /* Copy back */
    cr = cairo_create(layer->surface);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_surface(cr, temp, 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);

    cairo_surface_destroy(temp);
    artos->document->composite_dirty = 1;
    artos->document->modified = 1;
    gtk_widget_queue_draw(artos->canvas_area);
}

/* ==============================================================================
 * Reference Image Operations
 * ============================================================================== */

int artos_reference_add(phantom_artos_t *artos, const char *filepath) {
    if (!artos || !filepath) return -1;

    cairo_surface_t *image = cairo_image_surface_create_from_png(filepath);
    if (cairo_surface_status(image) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(image);
        return -1;
    }

    artos_reference_t *ref = calloc(1, sizeof(artos_reference_t));
    if (!ref) {
        cairo_surface_destroy(image);
        return -1;
    }

    ref->image = image;
    strncpy(ref->filepath, filepath, sizeof(ref->filepath) - 1);
    ref->scale = 1.0;
    ref->opacity = 0.5;
    ref->visible = 1;
    ref->x = 10;
    ref->y = 10;

    /* Add to list */
    ref->next = artos->references;
    artos->references = ref;
    artos->reference_count++;

    gtk_widget_queue_draw(artos->canvas_area);
    return 0;
}

void artos_reference_remove(phantom_artos_t *artos, int index) {
    if (!artos || index < 0) return;

    artos_reference_t *prev = NULL;
    artos_reference_t *ref = artos->references;
    int i = 0;

    while (ref && i < index) {
        prev = ref;
        ref = ref->next;
        i++;
    }

    if (!ref) return;

    if (prev) {
        prev->next = ref->next;
    } else {
        artos->references = ref->next;
    }

    cairo_surface_destroy(ref->image);
    free(ref);
    artos->reference_count--;

    gtk_widget_queue_draw(artos->canvas_area);
}

void artos_reference_set_opacity(phantom_artos_t *artos, int index, double opacity) {
    if (!artos) return;
    artos_reference_t *ref = artos->references;
    for (int i = 0; ref && i < index; i++) ref = ref->next;
    if (ref) {
        ref->opacity = fmax(0.0, fmin(1.0, opacity));
        gtk_widget_queue_draw(artos->canvas_area);
    }
}

void artos_reference_set_scale(phantom_artos_t *artos, int index, double scale) {
    if (!artos) return;
    artos_reference_t *ref = artos->references;
    for (int i = 0; ref && i < index; i++) ref = ref->next;
    if (ref) {
        ref->scale = fmax(0.1, fmin(5.0, scale));
        gtk_widget_queue_draw(artos->canvas_area);
    }
}

void artos_reference_toggle_visible(phantom_artos_t *artos, int index) {
    if (!artos) return;
    artos_reference_t *ref = artos->references;
    for (int i = 0; ref && i < index; i++) ref = ref->next;
    if (ref) {
        ref->visible = !ref->visible;
        gtk_widget_queue_draw(artos->canvas_area);
    }
}

/* ==============================================================================
 * Color Harmony
 * ============================================================================== */

void artos_color_wheel_get_harmonies(artos_color_t *base, artos_color_harmony_t type,
                                      artos_color_t *out_colors, int *out_count) {
    if (!base || !out_colors || !out_count) return;

    double h, s, v;
    artos_color_to_hsv(base, &h, &s, &v);

    *out_count = 0;
    out_colors[(*out_count)++] = *base;

    switch (type) {
        case ARTOS_HARMONY_COMPLEMENTARY:
            artos_color_from_hsv(&out_colors[(*out_count)++], fmod(h + 180, 360), s, v);
            break;

        case ARTOS_HARMONY_ANALOGOUS:
            artos_color_from_hsv(&out_colors[(*out_count)++], fmod(h + 30, 360), s, v);
            artos_color_from_hsv(&out_colors[(*out_count)++], fmod(h + 330, 360), s, v);
            break;

        case ARTOS_HARMONY_TRIADIC:
            artos_color_from_hsv(&out_colors[(*out_count)++], fmod(h + 120, 360), s, v);
            artos_color_from_hsv(&out_colors[(*out_count)++], fmod(h + 240, 360), s, v);
            break;

        case ARTOS_HARMONY_SPLIT_COMPLEMENTARY:
            artos_color_from_hsv(&out_colors[(*out_count)++], fmod(h + 150, 360), s, v);
            artos_color_from_hsv(&out_colors[(*out_count)++], fmod(h + 210, 360), s, v);
            break;

        case ARTOS_HARMONY_TETRADIC:
            artos_color_from_hsv(&out_colors[(*out_count)++], fmod(h + 90, 360), s, v);
            artos_color_from_hsv(&out_colors[(*out_count)++], fmod(h + 180, 360), s, v);
            artos_color_from_hsv(&out_colors[(*out_count)++], fmod(h + 270, 360), s, v);
            break;

        case ARTOS_HARMONY_MONOCHROMATIC:
            artos_color_from_hsv(&out_colors[(*out_count)++], h, s * 0.5, v);
            artos_color_from_hsv(&out_colors[(*out_count)++], h, s, v * 0.5);
            break;

        default:
            break;
    }
}

void artos_color_harmony_update(phantom_artos_t *artos) {
    if (!artos) return;
    artos_color_wheel_get_harmonies(&artos->foreground_color, artos->color_harmony,
                                     artos->harmony_colors, &artos->harmony_color_count);
    if (artos->color_wheel_area) {
        gtk_widget_queue_draw(artos->color_wheel_area);
    }
}

void artos_color_harmony_set_type(phantom_artos_t *artos, artos_color_harmony_t type) {
    if (!artos) return;
    artos->color_harmony = type;
    artos_color_harmony_update(artos);
}

/* ==============================================================================
 * Symmetry Mode
 * ============================================================================== */

void artos_symmetry_set_mode(phantom_artos_t *artos, artos_symmetry_mode_t mode) {
    if (!artos) return;
    artos->symmetry_mode = mode;

    /* Default center to canvas center */
    if (artos->document && (artos->symmetry_center_x == 0 && artos->symmetry_center_y == 0)) {
        artos->symmetry_center_x = artos->document->width / 2.0;
        artos->symmetry_center_y = artos->document->height / 2.0;
    }

    gtk_widget_queue_draw(artos->canvas_area);
}

void artos_symmetry_set_center(phantom_artos_t *artos, double x, double y) {
    if (!artos) return;
    artos->symmetry_center_x = x;
    artos->symmetry_center_y = y;
    gtk_widget_queue_draw(artos->canvas_area);
}

/* Helper to draw a point with symmetry applied */
void artos_symmetry_draw_point(phantom_artos_t *artos, double x, double y, double pressure) {
    if (!artos || !artos->document) return;

    artos_layer_t *layer = artos_layer_get_active(artos->document);
    if (!layer) return;

    cairo_t *cr = cairo_create(layer->surface);
    double cx = artos->symmetry_center_x;
    double cy = artos->symmetry_center_y;

    /* Calculate brush size based on pressure */
    double size = artos->current_brush.size * pressure;

    /* Set color */
    cairo_set_source_rgba(cr,
        artos->foreground_color.r,
        artos->foreground_color.g,
        artos->foreground_color.b,
        artos->foreground_color.a * artos->current_brush.opacity);

    /* Draw original point */
    cairo_arc(cr, x, y, size / 2, 0, 2 * M_PI);
    cairo_fill(cr);

    switch (artos->symmetry_mode) {
        case ARTOS_SYMMETRY_HORIZONTAL:
            /* Mirror across vertical axis */
            cairo_arc(cr, 2 * cx - x, y, size / 2, 0, 2 * M_PI);
            cairo_fill(cr);
            break;

        case ARTOS_SYMMETRY_VERTICAL:
            /* Mirror across horizontal axis */
            cairo_arc(cr, x, 2 * cy - y, size / 2, 0, 2 * M_PI);
            cairo_fill(cr);
            break;

        case ARTOS_SYMMETRY_BOTH:
            /* 4-way symmetry */
            cairo_arc(cr, 2 * cx - x, y, size / 2, 0, 2 * M_PI);
            cairo_fill(cr);
            cairo_arc(cr, x, 2 * cy - y, size / 2, 0, 2 * M_PI);
            cairo_fill(cr);
            cairo_arc(cr, 2 * cx - x, 2 * cy - y, size / 2, 0, 2 * M_PI);
            cairo_fill(cr);
            break;

        case ARTOS_SYMMETRY_RADIAL_3:
        case ARTOS_SYMMETRY_RADIAL_4:
        case ARTOS_SYMMETRY_RADIAL_6:
        case ARTOS_SYMMETRY_RADIAL_8: {
            int n = 3;
            if (artos->symmetry_mode == ARTOS_SYMMETRY_RADIAL_4) n = 4;
            else if (artos->symmetry_mode == ARTOS_SYMMETRY_RADIAL_6) n = 6;
            else if (artos->symmetry_mode == ARTOS_SYMMETRY_RADIAL_8) n = 8;

            double dx = x - cx;
            double dy = y - cy;
            double angle_step = 2 * M_PI / n;

            for (int i = 1; i < n; i++) {
                double angle = i * angle_step;
                double nx = cx + dx * cos(angle) - dy * sin(angle);
                double ny = cy + dx * sin(angle) + dy * cos(angle);
                cairo_arc(cr, nx, ny, size / 2, 0, 2 * M_PI);
                cairo_fill(cr);
            }
            break;
        }

        default:
            break;
    }

    cairo_destroy(cr);
    artos->document->composite_dirty = 1;
}

/* ==============================================================================
 * Brush Stabilization
 * ============================================================================== */

void artos_stabilizer_enable(phantom_artos_t *artos, int enable) {
    if (!artos) return;
    artos->stabilizer_enabled = enable;
    if (!enable) {
        artos_stabilizer_reset(artos);
    }
}

void artos_stabilizer_set_strength(phantom_artos_t *artos, int strength) {
    if (!artos) return;
    artos->stabilizer_strength = (strength < 1) ? 1 : (strength > 10) ? 10 : strength;
}

void artos_stabilizer_add_point(phantom_artos_t *artos, double x, double y, double pressure) {
    if (!artos) return;

    int idx = artos->stabilizer_index % ARTOS_STABILIZER_MAX_POINTS;
    artos->stabilizer_buffer[idx].x = x;
    artos->stabilizer_buffer[idx].y = y;
    artos->stabilizer_buffer[idx].pressure = pressure;
    artos->stabilizer_buffer[idx].time = g_get_monotonic_time() / 1000;

    artos->stabilizer_index++;
    if (artos->stabilizer_count < ARTOS_STABILIZER_MAX_POINTS) {
        artos->stabilizer_count++;
    }
}

void artos_stabilizer_get_smoothed(phantom_artos_t *artos, double *x, double *y, double *pressure) {
    if (!artos || artos->stabilizer_count == 0) {
        return;
    }

    /* Use weighted moving average based on strength */
    int window = artos->stabilizer_strength + 2;  /* 3 to 12 points */
    if (window > artos->stabilizer_count) {
        window = artos->stabilizer_count;
    }

    double sum_x = 0, sum_y = 0, sum_p = 0;
    double weight_sum = 0;

    for (int i = 0; i < window; i++) {
        int idx = (artos->stabilizer_index - 1 - i + ARTOS_STABILIZER_MAX_POINTS) % ARTOS_STABILIZER_MAX_POINTS;
        double weight = 1.0 / (i + 1);  /* More recent points have higher weight */

        sum_x += artos->stabilizer_buffer[idx].x * weight;
        sum_y += artos->stabilizer_buffer[idx].y * weight;
        sum_p += artos->stabilizer_buffer[idx].pressure * weight;
        weight_sum += weight;
    }

    *x = sum_x / weight_sum;
    *y = sum_y / weight_sum;
    *pressure = sum_p / weight_sum;
}

void artos_stabilizer_reset(phantom_artos_t *artos) {
    if (!artos) return;
    artos->stabilizer_count = 0;
    artos->stabilizer_index = 0;
}

/* ==============================================================================
 * Canvas Rotation
 * ============================================================================== */

void artos_canvas_set_rotation(phantom_artos_t *artos, double degrees) {
    if (!artos) return;
    artos->canvas_rotation = fmod(degrees, 360.0);
    if (artos->canvas_rotation < 0) artos->canvas_rotation += 360.0;
    gtk_widget_queue_draw(artos->canvas_area);
}

void artos_canvas_rotate(phantom_artos_t *artos, double delta) {
    if (!artos) return;
    artos_canvas_set_rotation(artos, artos->canvas_rotation + delta);
}

void artos_canvas_reset_rotation(phantom_artos_t *artos) {
    if (!artos) return;
    artos->canvas_rotation = 0;
    artos->canvas_flip_h = 0;
    artos->canvas_flip_v = 0;
    gtk_widget_queue_draw(artos->canvas_area);
}

void artos_canvas_flip_view(phantom_artos_t *artos, int horizontal) {
    if (!artos) return;
    if (horizontal) {
        artos->canvas_flip_h = !artos->canvas_flip_h;
    } else {
        artos->canvas_flip_v = !artos->canvas_flip_v;
    }
    gtk_widget_queue_draw(artos->canvas_area);
}

/* Convert canvas (screen) coordinates to document coordinates */
void artos_canvas_to_doc_coords(phantom_artos_t *artos, double cx, double cy, double *dx, double *dy) {
    if (!artos || !artos->document) {
        *dx = cx;
        *dy = cy;
        return;
    }

    /* Get canvas center */
    double canvas_cx = artos->canvas_width / 2.0;
    double canvas_cy = artos->canvas_height / 2.0;

    /* Translate to center */
    double tx = cx - canvas_cx;
    double ty = cy - canvas_cy;

    /* Apply inverse rotation */
    double angle = -artos->canvas_rotation * M_PI / 180.0;
    double rx = tx * cos(angle) - ty * sin(angle);
    double ry = tx * sin(angle) + ty * cos(angle);

    /* Apply inverse flip */
    if (artos->canvas_flip_h) rx = -rx;
    if (artos->canvas_flip_v) ry = -ry;

    /* Apply inverse zoom and pan */
    double doc_cx = artos->document->width / 2.0;
    double doc_cy = artos->document->height / 2.0;

    *dx = rx / artos->zoom + doc_cx - artos->pan_x / artos->zoom;
    *dy = ry / artos->zoom + doc_cy - artos->pan_y / artos->zoom;
}

/* ==============================================================================
 * Composite Update (Flatten all layers for display)
 * ============================================================================== */

void artos_update_composite(artos_document_t *doc) {
    if (!doc || !doc->composite_dirty) return;

    cairo_t *cr = cairo_create(doc->composite);

    /* Clear with transparency (checkerboard will be drawn by view) */
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    /* Composite all visible layers from bottom to top */
    for (int i = 0; i < doc->layer_count; i++) {
        artos_layer_t *layer = doc->layers[i];
        if (!layer || !layer->visible) continue;

        cairo_save(cr);

        /* Set blend mode */
        switch (layer->blend_mode) {
            case ARTOS_BLEND_MULTIPLY:
                cairo_set_operator(cr, CAIRO_OPERATOR_MULTIPLY);
                break;
            case ARTOS_BLEND_SCREEN:
                cairo_set_operator(cr, CAIRO_OPERATOR_SCREEN);
                break;
            case ARTOS_BLEND_OVERLAY:
                cairo_set_operator(cr, CAIRO_OPERATOR_OVERLAY);
                break;
            case ARTOS_BLEND_DARKEN:
                cairo_set_operator(cr, CAIRO_OPERATOR_DARKEN);
                break;
            case ARTOS_BLEND_LIGHTEN:
                cairo_set_operator(cr, CAIRO_OPERATOR_LIGHTEN);
                break;
            case ARTOS_BLEND_COLOR_DODGE:
                cairo_set_operator(cr, CAIRO_OPERATOR_COLOR_DODGE);
                break;
            case ARTOS_BLEND_COLOR_BURN:
                cairo_set_operator(cr, CAIRO_OPERATOR_COLOR_BURN);
                break;
            case ARTOS_BLEND_HARD_LIGHT:
                cairo_set_operator(cr, CAIRO_OPERATOR_HARD_LIGHT);
                break;
            case ARTOS_BLEND_SOFT_LIGHT:
                cairo_set_operator(cr, CAIRO_OPERATOR_SOFT_LIGHT);
                break;
            case ARTOS_BLEND_DIFFERENCE:
                cairo_set_operator(cr, CAIRO_OPERATOR_DIFFERENCE);
                break;
            case ARTOS_BLEND_EXCLUSION:
                cairo_set_operator(cr, CAIRO_OPERATOR_EXCLUSION);
                break;
            default:
                cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
                break;
        }

        cairo_set_source_surface(cr, layer->surface, 0, 0);
        cairo_paint_with_alpha(cr, layer->opacity);
        cairo_restore(cr);
    }

    cairo_destroy(cr);
    doc->composite_dirty = 0;
}

/* ==============================================================================
 * Brush Presets
 * ============================================================================== */

void artos_init_default_brushes(phantom_artos_t *artos) {
    /* Pencil - hard edge, full opacity */
    artos_brush_t *pencil = &artos->brushes[0];
    strcpy(pencil->name, "Pencil");
    pencil->shape = ARTOS_BRUSH_ROUND;
    pencil->size = 2;
    pencil->hardness = 1.0;
    pencil->opacity = 1.0;
    pencil->flow = 1.0;
    pencil->spacing = 0.1;
    pencil->pressure_size = 1;
    pencil->pressure_opacity = 0;

    /* Pen - anti-aliased, full opacity */
    artos_brush_t *pen = &artos->brushes[1];
    strcpy(pen->name, "Pen");
    pen->shape = ARTOS_BRUSH_ROUND;
    pen->size = 3;
    pen->hardness = 0.9;
    pen->opacity = 1.0;
    pen->flow = 1.0;
    pen->spacing = 0.05;
    pen->pressure_size = 1;
    pen->pressure_opacity = 0;

    /* Soft Brush - soft edge, variable opacity */
    artos_brush_t *soft = &artos->brushes[2];
    strcpy(soft->name, "Soft Brush");
    soft->shape = ARTOS_BRUSH_ROUND;
    soft->size = 30;
    soft->hardness = 0.2;
    soft->opacity = 0.7;
    soft->flow = 0.5;
    soft->spacing = 0.1;
    soft->pressure_size = 1;
    soft->pressure_opacity = 1;

    /* Hard Brush */
    artos_brush_t *hard = &artos->brushes[3];
    strcpy(hard->name, "Hard Brush");
    hard->shape = ARTOS_BRUSH_ROUND;
    hard->size = 20;
    hard->hardness = 0.8;
    hard->opacity = 1.0;
    hard->flow = 0.8;
    hard->spacing = 0.1;
    hard->pressure_size = 1;
    hard->pressure_opacity = 0;

    /* Airbrush */
    artos_brush_t *airbrush = &artos->brushes[4];
    strcpy(airbrush->name, "Airbrush");
    airbrush->shape = ARTOS_BRUSH_ROUND;
    airbrush->size = 50;
    airbrush->hardness = 0.0;
    airbrush->opacity = 0.3;
    airbrush->flow = 0.2;
    airbrush->spacing = 0.05;
    airbrush->pressure_size = 0;
    airbrush->pressure_opacity = 1;

    /* Marker */
    artos_brush_t *marker = &artos->brushes[5];
    strcpy(marker->name, "Marker");
    marker->shape = ARTOS_BRUSH_ROUND;
    marker->size = 15;
    marker->hardness = 0.5;
    marker->opacity = 0.6;
    marker->flow = 1.0;
    marker->spacing = 0.1;
    marker->pressure_size = 0;
    marker->pressure_opacity = 0;

    /* Eraser (paints transparency) */
    artos_brush_t *eraser = &artos->brushes[6];
    strcpy(eraser->name, "Eraser");
    eraser->shape = ARTOS_BRUSH_ROUND;
    eraser->size = 20;
    eraser->hardness = 0.8;
    eraser->opacity = 1.0;
    eraser->flow = 1.0;
    eraser->spacing = 0.1;
    eraser->pressure_size = 1;
    eraser->pressure_opacity = 0;

    /* Calligraphy */
    artos_brush_t *calli = &artos->brushes[7];
    strcpy(calli->name, "Calligraphy");
    calli->shape = ARTOS_BRUSH_ROUND;
    calli->size = 10;
    calli->hardness = 1.0;
    calli->opacity = 1.0;
    calli->flow = 1.0;
    calli->spacing = 0.05;
    calli->angle = 45;
    calli->roundness = 0.3;
    calli->pressure_size = 1;
    calli->pressure_opacity = 0;

    artos->brush_count = 8;
    artos->current_brush = artos->brushes[2];  /* Default to Soft Brush */
}

/* ==============================================================================
 * Drawing Operations
 * ============================================================================== */

static void artos_render_brush_dab(phantom_artos_t *artos, cairo_t *cr,
                                   double x, double y, double pressure) {
    artos_brush_t *brush = &artos->current_brush;
    double size = brush->size;
    double opacity = brush->opacity * brush->flow;

    /* Apply pressure sensitivity */
    if (brush->pressure_size) {
        size *= pressure;
    }
    if (brush->pressure_opacity) {
        opacity *= pressure;
    }

    /* Handle eraser tool */
    if (artos->current_tool == ARTOS_TOOL_ERASER) {
        cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    } else {
        cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
        cairo_set_source_rgba(cr,
                              artos->foreground_color.r,
                              artos->foreground_color.g,
                              artos->foreground_color.b,
                              opacity);
    }

    if (brush->hardness >= 0.99) {
        /* Hard brush - simple circle */
        cairo_arc(cr, x, y, size / 2, 0, 2 * M_PI);
        cairo_fill(cr);
    } else {
        /* Soft brush - radial gradient */
        cairo_pattern_t *pattern = cairo_pattern_create_radial(
            x, y, 0,
            x, y, size / 2
        );

        if (artos->current_tool == ARTOS_TOOL_ERASER) {
            cairo_pattern_add_color_stop_rgba(pattern, 0, 0, 0, 0, 1);
            cairo_pattern_add_color_stop_rgba(pattern, brush->hardness, 0, 0, 0, 1);
            cairo_pattern_add_color_stop_rgba(pattern, 1, 0, 0, 0, 0);
        } else {
            cairo_pattern_add_color_stop_rgba(pattern, 0,
                artos->foreground_color.r,
                artos->foreground_color.g,
                artos->foreground_color.b,
                opacity);
            cairo_pattern_add_color_stop_rgba(pattern, brush->hardness,
                artos->foreground_color.r,
                artos->foreground_color.g,
                artos->foreground_color.b,
                opacity);
            cairo_pattern_add_color_stop_rgba(pattern, 1,
                artos->foreground_color.r,
                artos->foreground_color.g,
                artos->foreground_color.b,
                0);
        }

        cairo_set_source(cr, pattern);
        cairo_arc(cr, x, y, size / 2, 0, 2 * M_PI);
        cairo_fill(cr);
        cairo_pattern_destroy(pattern);
    }
}

static void artos_draw_stroke_segment(phantom_artos_t *artos, cairo_t *cr,
                                      double x1, double y1, double p1,
                                      double x2, double y2, double p2) {
    artos_brush_t *brush = &artos->current_brush;
    double dist = sqrt((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1));
    double spacing = fmax(1.0, brush->size * brush->spacing);
    int steps = (int)(dist / spacing) + 1;

    for (int i = 0; i <= steps; i++) {
        double t = (double)i / steps;
        double x = x1 + (x2 - x1) * t;
        double y = y1 + (y2 - y1) * t;
        double p = p1 + (p2 - p1) * t;
        artos_render_brush_dab(artos, cr, x, y, p);
    }
}

static void artos_begin_stroke(phantom_artos_t *artos, double x, double y, double pressure) {
    if (!artos->document) return;

    artos_layer_t *layer = artos_layer_get_active(artos->document);
    if (!layer || layer->locked) return;

    artos->is_drawing = 1;
    artos->last_x = x;
    artos->last_y = y;

    /* Create stroke record for undo */
    artos->current_stroke = calloc(1, sizeof(artos_stroke_t));
    if (artos->current_stroke) {
        artos->current_stroke->tool = artos->current_tool;
        artos->current_stroke->brush = artos->current_brush;
        artos->current_stroke->color = artos->foreground_color;
        artos->current_stroke->layer_index = artos->document->active_layer;
        artos->current_stroke->point_capacity = 1000;
        artos->current_stroke->points = malloc(sizeof(artos_point_t) * 1000);

        /* Save layer state before stroke */
        artos->current_stroke->before_snapshot = cairo_image_surface_create(
            CAIRO_FORMAT_ARGB32, layer->width, layer->height);
        cairo_t *cr = cairo_create(artos->current_stroke->before_snapshot);
        cairo_set_source_surface(cr, layer->surface, 0, 0);
        cairo_paint(cr);
        cairo_destroy(cr);
    }

    /* Draw first dab */
    cairo_t *cr = cairo_create(layer->surface);
    artos_render_brush_dab(artos, cr, x, y, pressure);
    cairo_destroy(cr);

    /* Add point to stroke */
    if (artos->current_stroke && artos->current_stroke->points) {
        artos->current_stroke->points[0].x = x;
        artos->current_stroke->points[0].y = y;
        artos->current_stroke->points[0].pressure = pressure;
        artos->current_stroke->point_count = 1;
    }

    artos->document->composite_dirty = 1;
    artos->document->modified = 1;
}

static void artos_continue_stroke(phantom_artos_t *artos, double x, double y, double pressure) {
    if (!artos->is_drawing || !artos->document) return;

    artos_layer_t *layer = artos_layer_get_active(artos->document);
    if (!layer || layer->locked) return;

    cairo_t *cr = cairo_create(layer->surface);
    artos_draw_stroke_segment(artos, cr,
                              artos->last_x, artos->last_y, 1.0,
                              x, y, pressure);
    cairo_destroy(cr);

    /* Add point to stroke */
    if (artos->current_stroke && artos->current_stroke->points) {
        if (artos->current_stroke->point_count >= artos->current_stroke->point_capacity) {
            artos->current_stroke->point_capacity *= 2;
            artos->current_stroke->points = realloc(artos->current_stroke->points,
                sizeof(artos_point_t) * artos->current_stroke->point_capacity);
        }
        if (artos->current_stroke->points) {
            int idx = artos->current_stroke->point_count;
            artos->current_stroke->points[idx].x = x;
            artos->current_stroke->points[idx].y = y;
            artos->current_stroke->points[idx].pressure = pressure;
            artos->current_stroke->point_count++;
        }
    }

    artos->last_x = x;
    artos->last_y = y;
    artos->document->composite_dirty = 1;
}

static void artos_end_stroke(phantom_artos_t *artos) {
    if (!artos->is_drawing) return;

    artos->is_drawing = 0;

    /* Add stroke to undo stack */
    if (artos->current_stroke) {
        artos->current_stroke->next = artos->document->undo_stack;
        artos->document->undo_stack = artos->current_stroke;
        artos->document->undo_count++;
        artos->current_stroke = NULL;

        /* Clear redo stack */
        artos_stroke_t *stroke = artos->document->redo_stack;
        while (stroke) {
            artos_stroke_t *next = stroke->next;
            if (stroke->points) free(stroke->points);
            if (stroke->before_snapshot) cairo_surface_destroy(stroke->before_snapshot);
            free(stroke);
            stroke = next;
        }
        artos->document->redo_stack = NULL;
    }
}

void artos_draw_line(phantom_artos_t *artos, double x1, double y1, double x2, double y2) {
    if (!artos->document) return;

    artos_layer_t *layer = artos_layer_get_active(artos->document);
    if (!layer || layer->locked) return;

    cairo_t *cr = cairo_create(layer->surface);
    cairo_set_source_rgba(cr,
                          artos->foreground_color.r,
                          artos->foreground_color.g,
                          artos->foreground_color.b,
                          artos->foreground_color.a);
    cairo_set_line_width(cr, artos->current_brush.size);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_move_to(cr, x1, y1);
    cairo_line_to(cr, x2, y2);
    cairo_stroke(cr);
    cairo_destroy(cr);

    artos->document->composite_dirty = 1;
    artos->document->modified = 1;
}

void artos_draw_shape(phantom_artos_t *artos, artos_tool_t shape,
                      double x1, double y1, double x2, double y2, int filled) {
    if (!artos->document) return;

    artos_layer_t *layer = artos_layer_get_active(artos->document);
    if (!layer || layer->locked) return;

    cairo_t *cr = cairo_create(layer->surface);
    cairo_set_source_rgba(cr,
                          artos->foreground_color.r,
                          artos->foreground_color.g,
                          artos->foreground_color.b,
                          artos->foreground_color.a);
    cairo_set_line_width(cr, artos->current_brush.size);

    double w = x2 - x1;
    double h = y2 - y1;

    switch (shape) {
        case ARTOS_TOOL_RECTANGLE:
            cairo_rectangle(cr, x1, y1, w, h);
            break;
        case ARTOS_TOOL_ELLIPSE:
            cairo_save(cr);
            cairo_translate(cr, x1 + w / 2, y1 + h / 2);
            cairo_scale(cr, w / 2, h / 2);
            cairo_arc(cr, 0, 0, 1, 0, 2 * M_PI);
            cairo_restore(cr);
            break;
        case ARTOS_TOOL_LINE:
            cairo_move_to(cr, x1, y1);
            cairo_line_to(cr, x2, y2);
            break;
        default:
            break;
    }

    if (filled && shape != ARTOS_TOOL_LINE) {
        cairo_fill(cr);
    } else {
        cairo_stroke(cr);
    }

    cairo_destroy(cr);
    artos->document->composite_dirty = 1;
    artos->document->modified = 1;
}

/* ==============================================================================
 * Undo/Redo
 * ============================================================================== */

void artos_undo(phantom_artos_t *artos) {
    if (!artos->document || !artos->document->undo_stack) return;

    artos_stroke_t *stroke = artos->document->undo_stack;
    artos->document->undo_stack = stroke->next;
    artos->document->undo_count--;

    /* Restore layer state */
    if (stroke->layer_index >= 0 && stroke->layer_index < artos->document->layer_count) {
        artos_layer_t *layer = artos->document->layers[stroke->layer_index];
        if (layer && stroke->before_snapshot) {
            cairo_t *cr = cairo_create(layer->surface);
            cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
            cairo_set_source_surface(cr, stroke->before_snapshot, 0, 0);
            cairo_paint(cr);
            cairo_destroy(cr);
        }
    }

    /* Move to redo stack */
    stroke->next = artos->document->redo_stack;
    artos->document->redo_stack = stroke;

    artos->document->composite_dirty = 1;
    gtk_widget_queue_draw(artos->canvas_area);
}

void artos_redo(phantom_artos_t *artos) {
    if (!artos->document || !artos->document->redo_stack) return;

    artos_stroke_t *stroke = artos->document->redo_stack;
    artos->document->redo_stack = stroke->next;

    /* Replay stroke */
    if (stroke->layer_index >= 0 && stroke->layer_index < artos->document->layer_count) {
        artos_layer_t *layer = artos->document->layers[stroke->layer_index];
        if (layer && stroke->points && stroke->point_count > 0) {
            artos_tool_t saved_tool = artos->current_tool;
            artos_brush_t saved_brush = artos->current_brush;
            artos_color_t saved_color = artos->foreground_color;

            artos->current_tool = stroke->tool;
            artos->current_brush = stroke->brush;
            artos->foreground_color = stroke->color;

            cairo_t *cr = cairo_create(layer->surface);
            artos_render_brush_dab(artos, cr,
                                   stroke->points[0].x,
                                   stroke->points[0].y,
                                   stroke->points[0].pressure);

            for (int i = 1; i < stroke->point_count; i++) {
                artos_draw_stroke_segment(artos, cr,
                    stroke->points[i - 1].x, stroke->points[i - 1].y, stroke->points[i - 1].pressure,
                    stroke->points[i].x, stroke->points[i].y, stroke->points[i].pressure);
            }
            cairo_destroy(cr);

            artos->current_tool = saved_tool;
            artos->current_brush = saved_brush;
            artos->foreground_color = saved_color;
        }
    }

    /* Move to undo stack */
    stroke->next = artos->document->undo_stack;
    artos->document->undo_stack = stroke;
    artos->document->undo_count++;

    artos->document->composite_dirty = 1;
    gtk_widget_queue_draw(artos->canvas_area);
}

/* ==============================================================================
 * Selection Operations
 * ============================================================================== */

void artos_select_all(phantom_artos_t *artos) {
    if (!artos->document) return;

    artos_selection_t *sel = &artos->document->selection;
    if (sel->mask) cairo_surface_destroy(sel->mask);

    sel->mask = cairo_image_surface_create(CAIRO_FORMAT_A8,
                                           artos->document->width,
                                           artos->document->height);
    cairo_t *cr = cairo_create(sel->mask);
    cairo_set_source_rgba(cr, 1, 1, 1, 1);
    cairo_paint(cr);
    cairo_destroy(cr);

    sel->has_selection = 1;
    sel->x = 0;
    sel->y = 0;
    sel->width = artos->document->width;
    sel->height = artos->document->height;

    gtk_widget_queue_draw(artos->canvas_area);
}

void artos_select_none(phantom_artos_t *artos) {
    if (!artos->document) return;

    artos_selection_t *sel = &artos->document->selection;
    if (sel->mask) {
        cairo_surface_destroy(sel->mask);
        sel->mask = NULL;
    }
    sel->has_selection = 0;

    gtk_widget_queue_draw(artos->canvas_area);
}

void artos_select_rect(phantom_artos_t *artos, int x, int y, int w, int h) {
    if (!artos->document) return;

    artos_selection_t *sel = &artos->document->selection;
    if (sel->mask) cairo_surface_destroy(sel->mask);

    sel->mask = cairo_image_surface_create(CAIRO_FORMAT_A8,
                                           artos->document->width,
                                           artos->document->height);
    cairo_t *cr = cairo_create(sel->mask);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_set_source_rgba(cr, 1, 1, 1, 1);
    cairo_rectangle(cr, x, y, w, h);
    cairo_fill(cr);
    cairo_destroy(cr);

    sel->has_selection = 1;
    sel->x = x;
    sel->y = y;
    sel->width = w;
    sel->height = h;

    gtk_widget_queue_draw(artos->canvas_area);
}

/* ==============================================================================
 * Tool Setting Functions
 * ============================================================================== */

void artos_set_tool(phantom_artos_t *artos, artos_tool_t tool) {
    artos->current_tool = tool;

    /* Update cursor based on tool */
    GdkCursor *cursor = NULL;
    GdkDisplay *display = gdk_display_get_default();

    switch (tool) {
        case ARTOS_TOOL_PENCIL:
        case ARTOS_TOOL_PEN:
        case ARTOS_TOOL_BRUSH:
        case ARTOS_TOOL_AIRBRUSH:
            cursor = gdk_cursor_new_from_name(display, "crosshair");
            break;
        case ARTOS_TOOL_ERASER:
            cursor = gdk_cursor_new_from_name(display, "cell");
            break;
        case ARTOS_TOOL_EYEDROPPER:
            cursor = gdk_cursor_new_from_name(display, "crosshair");
            break;
        case ARTOS_TOOL_BUCKET:
            cursor = gdk_cursor_new_from_name(display, "cell");
            break;
        case ARTOS_TOOL_MOVE:
            cursor = gdk_cursor_new_from_name(display, "move");
            break;
        case ARTOS_TOOL_ZOOM:
            cursor = gdk_cursor_new_from_name(display, "zoom-in");
            break;
        case ARTOS_TOOL_PAN:
            cursor = gdk_cursor_new_from_name(display, "grab");
            break;
        case ARTOS_TOOL_TEXT:
            cursor = gdk_cursor_new_from_name(display, "text");
            break;
        default:
            cursor = gdk_cursor_new_from_name(display, "default");
            break;
    }

    if (cursor && artos->canvas_area) {
        GdkWindow *window = gtk_widget_get_window(artos->canvas_area);
        if (window) {
            gdk_window_set_cursor(window, cursor);
        }
        g_object_unref(cursor);
    }
}

void artos_set_foreground_color(phantom_artos_t *artos, artos_color_t *color) {
    if (color) {
        artos->foreground_color = *color;
    }
}

void artos_set_background_color(phantom_artos_t *artos, artos_color_t *color) {
    if (color) {
        artos->background_color = *color;
    }
}

void artos_swap_colors(phantom_artos_t *artos) {
    artos_color_t tmp = artos->foreground_color;
    artos->foreground_color = artos->background_color;
    artos->background_color = tmp;
}

/* ==============================================================================
 * View Operations
 * ============================================================================== */

void artos_zoom_in(phantom_artos_t *artos) {
    artos->zoom *= 1.25;
    if (artos->zoom > 32.0) artos->zoom = 32.0;
    gtk_widget_queue_draw(artos->canvas_area);

    char buf[32];
    snprintf(buf, sizeof(buf), "%.0f%%", artos->zoom * 100);
    gtk_label_set_text(GTK_LABEL(artos->zoom_label), buf);
}

void artos_zoom_out(phantom_artos_t *artos) {
    artos->zoom /= 1.25;
    if (artos->zoom < 0.01) artos->zoom = 0.01;
    gtk_widget_queue_draw(artos->canvas_area);

    char buf[32];
    snprintf(buf, sizeof(buf), "%.0f%%", artos->zoom * 100);
    gtk_label_set_text(GTK_LABEL(artos->zoom_label), buf);
}

void artos_zoom_100(phantom_artos_t *artos) {
    artos->zoom = 1.0;
    gtk_widget_queue_draw(artos->canvas_area);

    gtk_label_set_text(GTK_LABEL(artos->zoom_label), "100%");
}

void artos_zoom_fit(phantom_artos_t *artos) {
    if (!artos->document) return;

    double zoom_x = (double)artos->canvas_width / artos->document->width;
    double zoom_y = (double)artos->canvas_height / artos->document->height;
    artos->zoom = fmin(zoom_x, zoom_y) * 0.9;  /* 90% to leave margin */

    artos->pan_x = 0;
    artos->pan_y = 0;

    gtk_widget_queue_draw(artos->canvas_area);

    char buf[32];
    snprintf(buf, sizeof(buf), "%.0f%%", artos->zoom * 100);
    gtk_label_set_text(GTK_LABEL(artos->zoom_label), buf);
}

/* ==============================================================================
 * Canvas Event Handlers
 * ============================================================================== */

static gboolean on_canvas_draw(GtkWidget *widget, cairo_t *cr, phantom_artos_t *artos) {
    (void)widget;

    GtkAllocation alloc;
    gtk_widget_get_allocation(artos->canvas_area, &alloc);
    artos->canvas_width = alloc.width;
    artos->canvas_height = alloc.height;

    /* Dark background */
    cairo_set_source_rgb(cr, 0.2, 0.2, 0.2);
    cairo_paint(cr);

    if (!artos->document) return TRUE;

    /* Calculate canvas position (centered) */
    double doc_display_w = artos->document->width * artos->zoom;
    double doc_display_h = artos->document->height * artos->zoom;
    double offset_x = (alloc.width - doc_display_w) / 2 + artos->pan_x;
    double offset_y = (alloc.height - doc_display_h) / 2 + artos->pan_y;

    /* Draw checkerboard pattern for transparency */
    cairo_save(cr);
    cairo_translate(cr, offset_x, offset_y);
    cairo_scale(cr, artos->zoom, artos->zoom);
    cairo_rectangle(cr, 0, 0, artos->document->width, artos->document->height);
    cairo_clip(cr);

    int check_size = 8;
    for (int y = 0; y < artos->document->height; y += check_size) {
        for (int x = 0; x < artos->document->width; x += check_size) {
            if ((x / check_size + y / check_size) % 2 == 0) {
                cairo_set_source_rgb(cr, 0.9, 0.9, 0.9);
            } else {
                cairo_set_source_rgb(cr, 0.7, 0.7, 0.7);
            }
            cairo_rectangle(cr, x, y, check_size, check_size);
            cairo_fill(cr);
        }
    }
    cairo_restore(cr);

    /* Draw composite image */
    artos_update_composite(artos->document);

    cairo_save(cr);
    cairo_translate(cr, offset_x, offset_y);
    cairo_scale(cr, artos->zoom, artos->zoom);
    cairo_set_source_surface(cr, artos->document->composite, 0, 0);
    cairo_paint(cr);
    cairo_restore(cr);

    /* Draw selection marching ants */
    if (artos->document->selection.has_selection) {
        artos_selection_t *sel = &artos->document->selection;
        cairo_save(cr);
        cairo_translate(cr, offset_x, offset_y);
        cairo_scale(cr, artos->zoom, artos->zoom);

        double dashes[] = {4.0, 4.0};
        cairo_set_dash(cr, dashes, 2, sel->marching_ants_offset);
        cairo_set_line_width(cr, 1.0 / artos->zoom);

        cairo_set_source_rgb(cr, 0, 0, 0);
        cairo_rectangle(cr, sel->x, sel->y, sel->width, sel->height);
        cairo_stroke(cr);

        cairo_set_dash(cr, dashes, 2, sel->marching_ants_offset + 4);
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_rectangle(cr, sel->x, sel->y, sel->width, sel->height);
        cairo_stroke(cr);

        cairo_restore(cr);
    }

    /* Draw canvas border */
    cairo_save(cr);
    cairo_set_source_rgb(cr, 0.4, 0.4, 0.4);
    cairo_set_line_width(cr, 1);
    cairo_rectangle(cr, offset_x - 1, offset_y - 1, doc_display_w + 2, doc_display_h + 2);
    cairo_stroke(cr);
    cairo_restore(cr);

    return TRUE;
}

/* Convert widget coordinates to document coordinates */
static void widget_to_doc_coords(phantom_artos_t *artos, double wx, double wy,
                                  double *dx, double *dy) {
    GtkAllocation alloc;
    gtk_widget_get_allocation(artos->canvas_area, &alloc);

    double doc_display_w = artos->document->width * artos->zoom;
    double doc_display_h = artos->document->height * artos->zoom;
    double offset_x = (alloc.width - doc_display_w) / 2 + artos->pan_x;
    double offset_y = (alloc.height - doc_display_h) / 2 + artos->pan_y;

    *dx = (wx - offset_x) / artos->zoom;
    *dy = (wy - offset_y) / artos->zoom;
}

static gboolean on_canvas_button_press(GtkWidget *widget, GdkEventButton *event,
                                        phantom_artos_t *artos) {
    (void)widget;

    if (!artos->document) return TRUE;

    double doc_x, doc_y;
    widget_to_doc_coords(artos, event->x, event->y, &doc_x, &doc_y);

    /* Get pressure from device */
    double pressure = 1.0;
    GdkDevice *device = gdk_event_get_source_device((GdkEvent *)event);
    if (device) {
        gdouble p;
        if (gdk_event_get_axis((GdkEvent *)event, GDK_AXIS_PRESSURE, &p)) {
            pressure = p;
        }
    }

    if (event->button == 1) {  /* Left button */
        switch (artos->current_tool) {
            case ARTOS_TOOL_PENCIL:
            case ARTOS_TOOL_PEN:
            case ARTOS_TOOL_BRUSH:
            case ARTOS_TOOL_AIRBRUSH:
            case ARTOS_TOOL_ERASER:
            case ARTOS_TOOL_SMUDGE:
                artos_begin_stroke(artos, doc_x, doc_y, pressure);
                break;

            case ARTOS_TOOL_LINE:
            case ARTOS_TOOL_RECTANGLE:
            case ARTOS_TOOL_ELLIPSE:
                artos->shape_drawing = 1;
                artos->shape_start_x = doc_x;
                artos->shape_start_y = doc_y;
                break;

            case ARTOS_TOOL_EYEDROPPER:
                /* Pick color from canvas */
                if (doc_x >= 0 && doc_x < artos->document->width &&
                    doc_y >= 0 && doc_y < artos->document->height) {
                    artos_update_composite(artos->document);
                    unsigned char *data = cairo_image_surface_get_data(artos->document->composite);
                    int stride = cairo_image_surface_get_stride(artos->document->composite);
                    int px = (int)doc_x;
                    int py = (int)doc_y;
                    unsigned char *pixel = data + py * stride + px * 4;
                    artos->foreground_color.b = pixel[0] / 255.0;
                    artos->foreground_color.g = pixel[1] / 255.0;
                    artos->foreground_color.r = pixel[2] / 255.0;
                    artos->foreground_color.a = pixel[3] / 255.0;

                    /* Update color button */
                    GdkRGBA rgba = {
                        artos->foreground_color.r,
                        artos->foreground_color.g,
                        artos->foreground_color.b,
                        artos->foreground_color.a
                    };
                    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(artos->color_button), &rgba);
                }
                break;

            case ARTOS_TOOL_SELECT_RECT:
                artos->shape_drawing = 1;
                artos->shape_start_x = doc_x;
                artos->shape_start_y = doc_y;
                break;

            case ARTOS_TOOL_PAN:
                artos->is_drawing = 1;  /* Reuse for pan dragging */
                artos->last_x = event->x;
                artos->last_y = event->y;
                break;

            case ARTOS_TOOL_ZOOM:
                if (event->state & GDK_SHIFT_MASK) {
                    artos_zoom_out(artos);
                } else {
                    artos_zoom_in(artos);
                }
                break;

            default:
                break;
        }
    } else if (event->button == 2) {  /* Middle button - pan */
        artos->is_drawing = 1;
        artos->last_x = event->x;
        artos->last_y = event->y;
    } else if (event->button == 3) {  /* Right button - quick color pick */
        if (doc_x >= 0 && doc_x < artos->document->width &&
            doc_y >= 0 && doc_y < artos->document->height) {
            artos_update_composite(artos->document);
            unsigned char *data = cairo_image_surface_get_data(artos->document->composite);
            int stride = cairo_image_surface_get_stride(artos->document->composite);
            int px = (int)doc_x;
            int py = (int)doc_y;
            unsigned char *pixel = data + py * stride + px * 4;
            artos->foreground_color.b = pixel[0] / 255.0;
            artos->foreground_color.g = pixel[1] / 255.0;
            artos->foreground_color.r = pixel[2] / 255.0;
            artos->foreground_color.a = pixel[3] / 255.0;

            GdkRGBA rgba = {
                artos->foreground_color.r,
                artos->foreground_color.g,
                artos->foreground_color.b,
                artos->foreground_color.a
            };
            gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(artos->color_button), &rgba);
        }
    }

    gtk_widget_queue_draw(artos->canvas_area);
    return TRUE;
}

static gboolean on_canvas_button_release(GtkWidget *widget, GdkEventButton *event,
                                          phantom_artos_t *artos) {
    (void)widget;

    if (!artos->document) return TRUE;

    double doc_x, doc_y;
    widget_to_doc_coords(artos, event->x, event->y, &doc_x, &doc_y);

    if (event->button == 1) {
        switch (artos->current_tool) {
            case ARTOS_TOOL_PENCIL:
            case ARTOS_TOOL_PEN:
            case ARTOS_TOOL_BRUSH:
            case ARTOS_TOOL_AIRBRUSH:
            case ARTOS_TOOL_ERASER:
            case ARTOS_TOOL_SMUDGE:
                artos_end_stroke(artos);
                break;

            case ARTOS_TOOL_LINE:
            case ARTOS_TOOL_RECTANGLE:
            case ARTOS_TOOL_ELLIPSE:
                if (artos->shape_drawing) {
                    artos_draw_shape(artos, artos->current_tool,
                                     artos->shape_start_x, artos->shape_start_y,
                                     doc_x, doc_y, 0);  /* 0 = stroke, not fill */
                    artos->shape_drawing = 0;
                }
                break;

            case ARTOS_TOOL_SELECT_RECT:
                if (artos->shape_drawing) {
                    int x = fmin(artos->shape_start_x, doc_x);
                    int y = fmin(artos->shape_start_y, doc_y);
                    int w = fabs(doc_x - artos->shape_start_x);
                    int h = fabs(doc_y - artos->shape_start_y);
                    artos_select_rect(artos, x, y, w, h);
                    artos->shape_drawing = 0;
                }
                break;

            case ARTOS_TOOL_PAN:
                artos->is_drawing = 0;
                break;

            default:
                break;
        }
    } else if (event->button == 2) {
        artos->is_drawing = 0;
    }

    gtk_widget_queue_draw(artos->canvas_area);
    return TRUE;
}

static gboolean on_canvas_motion(GtkWidget *widget, GdkEventMotion *event,
                                  phantom_artos_t *artos) {
    (void)widget;

    if (!artos->document) return TRUE;

    double doc_x, doc_y;
    widget_to_doc_coords(artos, event->x, event->y, &doc_x, &doc_y);

    /* Update coordinates label */
    char buf[128];
    snprintf(buf, sizeof(buf), "X: %.0f  Y: %.0f", doc_x, doc_y);
    gtk_label_set_text(GTK_LABEL(artos->coords_label), buf);

    /* Get pressure from device */
    double pressure = 1.0;
    GdkDevice *device = gdk_event_get_source_device((GdkEvent *)event);
    if (device) {
        gdouble p;
        if (gdk_event_get_axis((GdkEvent *)event, GDK_AXIS_PRESSURE, &p)) {
            pressure = p;
        }
    }

    if (event->state & GDK_BUTTON1_MASK) {
        switch (artos->current_tool) {
            case ARTOS_TOOL_PENCIL:
            case ARTOS_TOOL_PEN:
            case ARTOS_TOOL_BRUSH:
            case ARTOS_TOOL_AIRBRUSH:
            case ARTOS_TOOL_ERASER:
            case ARTOS_TOOL_SMUDGE:
                artos_continue_stroke(artos, doc_x, doc_y, pressure);
                break;

            case ARTOS_TOOL_PAN:
                if (artos->is_drawing) {
                    artos->pan_x += event->x - artos->last_x;
                    artos->pan_y += event->y - artos->last_y;
                    artos->last_x = event->x;
                    artos->last_y = event->y;
                }
                break;

            default:
                break;
        }
    }

    /* Middle button - pan */
    if ((event->state & GDK_BUTTON2_MASK) && artos->is_drawing) {
        artos->pan_x += event->x - artos->last_x;
        artos->pan_y += event->y - artos->last_y;
        artos->last_x = event->x;
        artos->last_y = event->y;
    }

    gtk_widget_queue_draw(artos->canvas_area);
    return TRUE;
}

static gboolean on_canvas_scroll(GtkWidget *widget, GdkEventScroll *event,
                                  phantom_artos_t *artos) {
    (void)widget;

    if (event->state & GDK_CONTROL_MASK) {
        /* Ctrl+Scroll = Zoom */
        if (event->direction == GDK_SCROLL_UP) {
            artos_zoom_in(artos);
        } else if (event->direction == GDK_SCROLL_DOWN) {
            artos_zoom_out(artos);
        }
    } else {
        /* Plain scroll = Pan */
        if (event->direction == GDK_SCROLL_UP) {
            artos->pan_y += 50;
        } else if (event->direction == GDK_SCROLL_DOWN) {
            artos->pan_y -= 50;
        } else if (event->direction == GDK_SCROLL_LEFT) {
            artos->pan_x += 50;
        } else if (event->direction == GDK_SCROLL_RIGHT) {
            artos->pan_x -= 50;
        }
        gtk_widget_queue_draw(artos->canvas_area);
    }

    return TRUE;
}

/* ==============================================================================
 * UI Event Handlers
 * ============================================================================== */

static void on_tool_selected(GtkToggleButton *button, phantom_artos_t *artos) {
    if (!gtk_toggle_button_get_active(button)) return;

    const char *name = gtk_widget_get_name(GTK_WIDGET(button));
    if (!name) return;

    if (strcmp(name, "tool_pencil") == 0) artos_set_tool(artos, ARTOS_TOOL_PENCIL);
    else if (strcmp(name, "tool_pen") == 0) artos_set_tool(artos, ARTOS_TOOL_PEN);
    else if (strcmp(name, "tool_brush") == 0) artos_set_tool(artos, ARTOS_TOOL_BRUSH);
    else if (strcmp(name, "tool_airbrush") == 0) artos_set_tool(artos, ARTOS_TOOL_AIRBRUSH);
    else if (strcmp(name, "tool_eraser") == 0) artos_set_tool(artos, ARTOS_TOOL_ERASER);
    else if (strcmp(name, "tool_bucket") == 0) artos_set_tool(artos, ARTOS_TOOL_BUCKET);
    else if (strcmp(name, "tool_eyedropper") == 0) artos_set_tool(artos, ARTOS_TOOL_EYEDROPPER);
    else if (strcmp(name, "tool_line") == 0) artos_set_tool(artos, ARTOS_TOOL_LINE);
    else if (strcmp(name, "tool_rectangle") == 0) artos_set_tool(artos, ARTOS_TOOL_RECTANGLE);
    else if (strcmp(name, "tool_ellipse") == 0) artos_set_tool(artos, ARTOS_TOOL_ELLIPSE);
    else if (strcmp(name, "tool_text") == 0) artos_set_tool(artos, ARTOS_TOOL_TEXT);
    else if (strcmp(name, "tool_select") == 0) artos_set_tool(artos, ARTOS_TOOL_SELECT_RECT);
    else if (strcmp(name, "tool_move") == 0) artos_set_tool(artos, ARTOS_TOOL_MOVE);
    else if (strcmp(name, "tool_zoom") == 0) artos_set_tool(artos, ARTOS_TOOL_ZOOM);
    else if (strcmp(name, "tool_pan") == 0) artos_set_tool(artos, ARTOS_TOOL_PAN);
}

static void on_brush_size_changed(GtkRange *range, phantom_artos_t *artos) {
    artos->current_brush.size = gtk_range_get_value(range);
}

static void on_brush_opacity_changed(GtkRange *range, phantom_artos_t *artos) {
    artos->current_brush.opacity = gtk_range_get_value(range) / 100.0;
}

static void on_brush_hardness_changed(GtkRange *range, phantom_artos_t *artos) {
    artos->current_brush.hardness = gtk_range_get_value(range) / 100.0;
}

static void on_color_set(GtkColorButton *button, phantom_artos_t *artos) {
    GdkRGBA rgba;
    gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(button), &rgba);
    artos->foreground_color.r = rgba.red;
    artos->foreground_color.g = rgba.green;
    artos->foreground_color.b = rgba.blue;
    artos->foreground_color.a = rgba.alpha;
}

static void on_layer_add_clicked(GtkButton *button, phantom_artos_t *artos) {
    (void)button;
    if (!artos->document) return;

    char name[32];
    snprintf(name, sizeof(name), "Layer %d", artos->document->layer_count + 1);
    artos_layer_add(artos->document, name);
    artos_refresh_layer_list(artos);
    gtk_widget_queue_draw(artos->canvas_area);
}

static void on_layer_visibility_toggled(GtkCellRendererToggle *renderer, gchar *path,
                                         phantom_artos_t *artos) {
    (void)renderer;
    if (!artos->document) return;

    GtkTreeIter iter;
    if (gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(artos->layer_store), &iter, path)) {
        gboolean visible;
        gint index;
        gtk_tree_model_get(GTK_TREE_MODEL(artos->layer_store), &iter,
                           ARTOS_LAYER_COL_VISIBLE, &visible,
                           ARTOS_LAYER_COL_INDEX, &index, -1);

        artos_layer_set_visible(artos->document, index, !visible);
        gtk_list_store_set(artos->layer_store, &iter,
                           ARTOS_LAYER_COL_VISIBLE, !visible, -1);
        gtk_widget_queue_draw(artos->canvas_area);
    }
}

static void on_layer_selected(GtkTreeSelection *selection, phantom_artos_t *artos) {
    GtkTreeIter iter;
    GtkTreeModel *model;

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        gint index;
        gtk_tree_model_get(model, &iter, ARTOS_LAYER_COL_INDEX, &index, -1);
        artos->document->active_layer = index;
    }
}

static void artos_refresh_layer_list(phantom_artos_t *artos) {
    if (!artos->layer_store || !artos->document) return;

    gtk_list_store_clear(artos->layer_store);

    /* Add layers in reverse order (top to bottom) */
    for (int i = artos->document->layer_count - 1; i >= 0; i--) {
        artos_layer_t *layer = artos->document->layers[i];
        if (!layer) continue;

        GtkTreeIter iter;
        gtk_list_store_append(artos->layer_store, &iter);
        gtk_list_store_set(artos->layer_store, &iter,
                           ARTOS_LAYER_COL_VISIBLE, layer->visible,
                           ARTOS_LAYER_COL_LOCKED, layer->locked,
                           ARTOS_LAYER_COL_NAME, layer->name,
                           ARTOS_LAYER_COL_OPACITY, (gint)(layer->opacity * 100),
                           ARTOS_LAYER_COL_INDEX, i,
                           -1);
    }
}

/* ==============================================================================
 * Menu/Toolbar Actions
 * ============================================================================== */

static void on_new_clicked(GtkButton *button, phantom_artos_t *artos) {
    (void)button;

    /* Simple new document dialog */
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "New Document",
        GTK_WINDOW(artos->window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Create", GTK_RESPONSE_OK,
        NULL);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 10);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Width:"), 0, 0, 1, 1);
    GtkWidget *width_spin = gtk_spin_button_new_with_range(1, 10000, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(width_spin), 1920);
    gtk_grid_attach(GTK_GRID(grid), width_spin, 1, 0, 1, 1);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Height:"), 0, 1, 1, 1);
    GtkWidget *height_spin = gtk_spin_button_new_with_range(1, 10000, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(height_spin), 1080);
    gtk_grid_attach(GTK_GRID(grid), height_spin, 1, 1, 1, 1);

    gtk_box_pack_start(GTK_BOX(content), grid, TRUE, TRUE, 0);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        int width = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(width_spin));
        int height = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(height_spin));

        if (artos->document) {
            artos_document_free(artos->document);
        }
        artos->document = artos_document_new(width, height, "Untitled");
        artos_refresh_layer_list(artos);
        artos_zoom_fit(artos);
    }

    gtk_widget_destroy(dialog);
}

static void on_save_clicked(GtkButton *button, phantom_artos_t *artos) {
    (void)button;
    if (!artos->document) return;

    GtkWidget *dialog = gtk_file_chooser_dialog_new(
        "Export Image",
        GTK_WINDOW(artos->window),
        GTK_FILE_CHOOSER_ACTION_SAVE,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Export", GTK_RESPONSE_ACCEPT,
        NULL);

    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);

    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "PNG Images");
    gtk_file_filter_add_pattern(filter, "*.png");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));

        /* Ensure .png extension */
        char filepath[4096];
        if (!strstr(filename, ".png")) {
            snprintf(filepath, sizeof(filepath), "%s.png", filename);
        } else {
            strncpy(filepath, filename, sizeof(filepath) - 1);
        }

        if (artos_document_export_png(artos->document, filepath) == 0) {
            GtkWidget *msg = gtk_message_dialog_new(GTK_WINDOW(artos->window),
                GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
                "Image exported to %s", filepath);
            gtk_dialog_run(GTK_DIALOG(msg));
            gtk_widget_destroy(msg);
        }

        g_free(filename);
    }

    gtk_widget_destroy(dialog);
}

static void on_undo_clicked(GtkButton *button, phantom_artos_t *artos) {
    (void)button;
    artos_undo(artos);
}

static void on_redo_clicked(GtkButton *button, phantom_artos_t *artos) {
    (void)button;
    artos_redo(artos);
}

static void on_zoom_in_clicked(GtkButton *button, phantom_artos_t *artos) {
    (void)button;
    artos_zoom_in(artos);
}

static void on_zoom_out_clicked(GtkButton *button, phantom_artos_t *artos) {
    (void)button;
    artos_zoom_out(artos);
}

static void on_zoom_fit_clicked(GtkButton *button, phantom_artos_t *artos) {
    (void)button;
    artos_zoom_fit(artos);
}

static void on_zoom_100_clicked(GtkButton *button, phantom_artos_t *artos) {
    (void)button;
    artos_zoom_100(artos);
}

/* Marching ants animation timer */
static gboolean on_selection_animate(phantom_artos_t *artos) {
    if (artos->document && artos->document->selection.has_selection) {
        artos->document->selection.marching_ants_offset++;
        if (artos->document->selection.marching_ants_offset > 8) {
            artos->document->selection.marching_ants_offset = 0;
        }
        gtk_widget_queue_draw(artos->canvas_area);
    }
    return TRUE;
}

/* ==============================================================================
 * UI Building
 * ============================================================================== */

static GtkWidget *artos_create_toolbar(phantom_artos_t *artos) {
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(toolbar), 5);

    /* New button */
    GtkWidget *new_btn = gtk_button_new_with_label("New");
    g_signal_connect(new_btn, "clicked", G_CALLBACK(on_new_clicked), artos);
    gtk_box_pack_start(GTK_BOX(toolbar), new_btn, FALSE, FALSE, 0);

    /* Save/Export button */
    GtkWidget *save_btn = gtk_button_new_with_label("Export");
    g_signal_connect(save_btn, "clicked", G_CALLBACK(on_save_clicked), artos);
    gtk_box_pack_start(GTK_BOX(toolbar), save_btn, FALSE, FALSE, 0);

    /* Separator */
    gtk_box_pack_start(GTK_BOX(toolbar), gtk_separator_new(GTK_ORIENTATION_VERTICAL), FALSE, FALSE, 5);

    /* Undo/Redo */
    GtkWidget *undo_btn = gtk_button_new_with_label("Undo");
    g_signal_connect(undo_btn, "clicked", G_CALLBACK(on_undo_clicked), artos);
    gtk_box_pack_start(GTK_BOX(toolbar), undo_btn, FALSE, FALSE, 0);

    GtkWidget *redo_btn = gtk_button_new_with_label("Redo");
    g_signal_connect(redo_btn, "clicked", G_CALLBACK(on_redo_clicked), artos);
    gtk_box_pack_start(GTK_BOX(toolbar), redo_btn, FALSE, FALSE, 0);

    /* Separator */
    gtk_box_pack_start(GTK_BOX(toolbar), gtk_separator_new(GTK_ORIENTATION_VERTICAL), FALSE, FALSE, 5);

    /* Zoom controls */
    GtkWidget *zoom_out_btn = gtk_button_new_with_label("-");
    g_signal_connect(zoom_out_btn, "clicked", G_CALLBACK(on_zoom_out_clicked), artos);
    gtk_box_pack_start(GTK_BOX(toolbar), zoom_out_btn, FALSE, FALSE, 0);

    artos->zoom_label = gtk_label_new("100%");
    gtk_widget_set_size_request(artos->zoom_label, 60, -1);
    gtk_box_pack_start(GTK_BOX(toolbar), artos->zoom_label, FALSE, FALSE, 0);

    GtkWidget *zoom_in_btn = gtk_button_new_with_label("+");
    g_signal_connect(zoom_in_btn, "clicked", G_CALLBACK(on_zoom_in_clicked), artos);
    gtk_box_pack_start(GTK_BOX(toolbar), zoom_in_btn, FALSE, FALSE, 0);

    GtkWidget *zoom_fit_btn = gtk_button_new_with_label("Fit");
    g_signal_connect(zoom_fit_btn, "clicked", G_CALLBACK(on_zoom_fit_clicked), artos);
    gtk_box_pack_start(GTK_BOX(toolbar), zoom_fit_btn, FALSE, FALSE, 0);

    GtkWidget *zoom_100_btn = gtk_button_new_with_label("100%");
    g_signal_connect(zoom_100_btn, "clicked", G_CALLBACK(on_zoom_100_clicked), artos);
    gtk_box_pack_start(GTK_BOX(toolbar), zoom_100_btn, FALSE, FALSE, 0);

    /* Spacer */
    gtk_box_pack_start(GTK_BOX(toolbar), gtk_label_new(""), TRUE, TRUE, 0);

    /* Coordinates */
    artos->coords_label = gtk_label_new("X: 0  Y: 0");
    gtk_box_pack_end(GTK_BOX(toolbar), artos->coords_label, FALSE, FALSE, 5);

    return toolbar;
}

static GtkWidget *artos_create_tool_palette(phantom_artos_t *artos) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_container_set_border_width(GTK_CONTAINER(box), 5);

    /* Create radio button group */
    GSList *group = NULL;

    struct {
        const char *label;
        const char *name;
        const char *tooltip;
    } tools[] = {
        {"✏️", "tool_pencil", "Pencil - Hard edge freehand"},
        {"🖊️", "tool_pen", "Pen - Smooth anti-aliased"},
        {"🖌️", "tool_brush", "Brush - Soft variable opacity"},
        {"💨", "tool_airbrush", "Airbrush - Spray paint"},
        {"🧽", "tool_eraser", "Eraser - Paint transparency"},
        {"🪣", "tool_bucket", "Fill - Flood fill area"},
        {"💧", "tool_eyedropper", "Eyedropper - Pick color"},
        {"📏", "tool_line", "Line - Straight line"},
        {"⬜", "tool_rectangle", "Rectangle"},
        {"⭕", "tool_ellipse", "Ellipse/Circle"},
        {"📝", "tool_text", "Text"},
        {"⬚", "tool_select", "Select Rectangle"},
        {"✥", "tool_move", "Move"},
        {"🔍", "tool_zoom", "Zoom"},
        {"✋", "tool_pan", "Pan/Scroll"},
    };

    int tool_count = sizeof(tools) / sizeof(tools[0]);
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 2);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 2);

    for (int i = 0; i < tool_count; i++) {
        GtkWidget *btn = gtk_radio_button_new(group);
        group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(btn));

        gtk_button_set_label(GTK_BUTTON(btn), tools[i].label);
        gtk_widget_set_name(btn, tools[i].name);
        gtk_widget_set_tooltip_text(btn, tools[i].tooltip);
        gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(btn), FALSE);  /* Button mode */
        gtk_widget_set_size_request(btn, 36, 36);

        g_signal_connect(btn, "toggled", G_CALLBACK(on_tool_selected), artos);

        gtk_grid_attach(GTK_GRID(grid), btn, i % 2, i / 2, 1, 1);

        /* Default to brush */
        if (strcmp(tools[i].name, "tool_brush") == 0) {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn), TRUE);
        }
    }

    gtk_box_pack_start(GTK_BOX(box), grid, FALSE, FALSE, 0);

    return box;
}

static GtkWidget *artos_create_brush_settings(phantom_artos_t *artos) {
    GtkWidget *frame = gtk_frame_new("Brush");
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(box), 5);

    /* Size */
    GtkWidget *size_label = gtk_label_new("Size:");
    gtk_widget_set_halign(size_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), size_label, FALSE, FALSE, 0);

    artos->brush_size_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 1, 200, 1);
    gtk_scale_set_value_pos(GTK_SCALE(artos->brush_size_scale), GTK_POS_RIGHT);
    gtk_range_set_value(GTK_RANGE(artos->brush_size_scale), artos->current_brush.size);
    g_signal_connect(artos->brush_size_scale, "value-changed", G_CALLBACK(on_brush_size_changed), artos);
    gtk_box_pack_start(GTK_BOX(box), artos->brush_size_scale, FALSE, FALSE, 0);

    /* Opacity */
    GtkWidget *opacity_label = gtk_label_new("Opacity:");
    gtk_widget_set_halign(opacity_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), opacity_label, FALSE, FALSE, 0);

    artos->brush_opacity_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
    gtk_scale_set_value_pos(GTK_SCALE(artos->brush_opacity_scale), GTK_POS_RIGHT);
    gtk_range_set_value(GTK_RANGE(artos->brush_opacity_scale), artos->current_brush.opacity * 100);
    g_signal_connect(artos->brush_opacity_scale, "value-changed", G_CALLBACK(on_brush_opacity_changed), artos);
    gtk_box_pack_start(GTK_BOX(box), artos->brush_opacity_scale, FALSE, FALSE, 0);

    /* Hardness */
    GtkWidget *hardness_label = gtk_label_new("Hardness:");
    gtk_widget_set_halign(hardness_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), hardness_label, FALSE, FALSE, 0);

    artos->brush_hardness_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
    gtk_scale_set_value_pos(GTK_SCALE(artos->brush_hardness_scale), GTK_POS_RIGHT);
    gtk_range_set_value(GTK_RANGE(artos->brush_hardness_scale), artos->current_brush.hardness * 100);
    g_signal_connect(artos->brush_hardness_scale, "value-changed", G_CALLBACK(on_brush_hardness_changed), artos);
    gtk_box_pack_start(GTK_BOX(box), artos->brush_hardness_scale, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(frame), box);
    return frame;
}

/* Quick color button callback */
static void lambda_quick_color(GtkButton *button, phantom_artos_t *artos) {
    const char *hex = gtk_widget_get_name(GTK_WIDGET(button));
    artos_color_from_hex(&artos->foreground_color, hex);
    artos->foreground_color.a = 1.0;

    GdkRGBA rgba = {
        artos->foreground_color.r,
        artos->foreground_color.g,
        artos->foreground_color.b,
        artos->foreground_color.a
    };
    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(artos->color_button), &rgba);
}

static GtkWidget *artos_create_color_panel(phantom_artos_t *artos) {
    GtkWidget *frame = gtk_frame_new("Color");
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(box), 5);

    /* Color button */
    artos->color_button = gtk_color_button_new();
    GdkRGBA rgba = {
        artos->foreground_color.r,
        artos->foreground_color.g,
        artos->foreground_color.b,
        artos->foreground_color.a
    };
    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(artos->color_button), &rgba);
    gtk_color_chooser_set_use_alpha(GTK_COLOR_CHOOSER(artos->color_button), TRUE);
    g_signal_connect(artos->color_button, "color-set", G_CALLBACK(on_color_set), artos);
    gtk_widget_set_size_request(artos->color_button, 64, 64);
    gtk_box_pack_start(GTK_BOX(box), artos->color_button, FALSE, FALSE, 0);

    /* Quick colors */
    GtkWidget *palette_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(palette_grid), 2);
    gtk_grid_set_column_spacing(GTK_GRID(palette_grid), 2);

    const char *quick_colors[] = {
        "#000000", "#FFFFFF", "#FF0000", "#00FF00",
        "#0000FF", "#FFFF00", "#FF00FF", "#00FFFF",
        "#808080", "#C0C0C0", "#800000", "#008000",
        "#000080", "#808000", "#800080", "#008080"
    };

    for (int i = 0; i < 16; i++) {
        GtkWidget *btn = gtk_button_new();
        gtk_widget_set_size_request(btn, 20, 20);

        /* Set button color via CSS */
        GtkCssProvider *provider = gtk_css_provider_new();
        char css[128];
        snprintf(css, sizeof(css), "button { background: %s; min-width: 0; min-height: 0; padding: 0; }", quick_colors[i]);
        gtk_css_provider_load_from_data(provider, css, -1, NULL);
        gtk_style_context_add_provider(gtk_widget_get_style_context(btn),
                                        GTK_STYLE_PROVIDER(provider),
                                        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref(provider);

        /* Store color in widget name */
        gtk_widget_set_name(btn, quick_colors[i]);

        g_signal_connect(btn, "clicked", G_CALLBACK(lambda_quick_color), artos);

        gtk_grid_attach(GTK_GRID(palette_grid), btn, i % 4, i / 4, 1, 1);
    }

    gtk_box_pack_start(GTK_BOX(box), palette_grid, FALSE, FALSE, 5);

    gtk_container_add(GTK_CONTAINER(frame), box);
    return frame;
}

static GtkWidget *artos_create_layer_panel(phantom_artos_t *artos) {
    GtkWidget *frame = gtk_frame_new("Layers");
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(box), 5);

    /* Layer buttons */
    GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);

    GtkWidget *add_btn = gtk_button_new_with_label("+");
    gtk_widget_set_tooltip_text(add_btn, "Add layer");
    g_signal_connect(add_btn, "clicked", G_CALLBACK(on_layer_add_clicked), artos);
    gtk_box_pack_start(GTK_BOX(btn_box), add_btn, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(box), btn_box, FALSE, FALSE, 0);

    /* Layer list */
    artos->layer_store = gtk_list_store_new(ARTOS_LAYER_COL_COUNT,
                                             G_TYPE_BOOLEAN,    /* Visible */
                                             G_TYPE_BOOLEAN,    /* Locked */
                                             GDK_TYPE_PIXBUF,   /* Thumbnail */
                                             G_TYPE_STRING,     /* Name */
                                             G_TYPE_INT,        /* Opacity */
                                             G_TYPE_INT);       /* Index */

    artos->layer_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(artos->layer_store));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(artos->layer_tree), FALSE);

    /* Visible column */
    GtkCellRenderer *toggle_renderer = gtk_cell_renderer_toggle_new();
    g_signal_connect(toggle_renderer, "toggled", G_CALLBACK(on_layer_visibility_toggled), artos);
    GtkTreeViewColumn *visible_col = gtk_tree_view_column_new_with_attributes(
        "", toggle_renderer, "active", ARTOS_LAYER_COL_VISIBLE, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(artos->layer_tree), visible_col);

    /* Name column */
    GtkCellRenderer *text_renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *name_col = gtk_tree_view_column_new_with_attributes(
        "Layer", text_renderer, "text", ARTOS_LAYER_COL_NAME, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(artos->layer_tree), name_col);

    /* Selection handler */
    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(artos->layer_tree));
    g_signal_connect(selection, "changed", G_CALLBACK(on_layer_selected), artos);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scroll, -1, 150);
    gtk_container_add(GTK_CONTAINER(scroll), artos->layer_tree);

    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);

    gtk_container_add(GTK_CONTAINER(frame), box);
    return frame;
}

/* ==============================================================================
 * Transform Panel
 * ============================================================================== */

static void on_rotate_left_clicked(GtkButton *button, phantom_artos_t *artos) {
    (void)button;
    artos_transform_begin(artos, ARTOS_TRANSFORM_ROTATE);
    artos_transform_rotate(artos, -90);
    artos_transform_apply(artos);
}

static void on_rotate_right_clicked(GtkButton *button, phantom_artos_t *artos) {
    (void)button;
    artos_transform_begin(artos, ARTOS_TRANSFORM_ROTATE);
    artos_transform_rotate(artos, 90);
    artos_transform_apply(artos);
}

static void on_flip_h_clicked(GtkButton *button, phantom_artos_t *artos) {
    (void)button;
    artos_transform_flip_horizontal(artos);
}

static void on_flip_v_clicked(GtkButton *button, phantom_artos_t *artos) {
    (void)button;
    artos_transform_flip_vertical(artos);
}

GtkWidget *artos_create_transform_panel(phantom_artos_t *artos) {
    GtkWidget *frame = gtk_frame_new("Transform");
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(box), 5);

    /* Rotation buttons */
    GtkWidget *rotate_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget *rotate_left = gtk_button_new_with_label("↶ 90°");
    GtkWidget *rotate_right = gtk_button_new_with_label("↷ 90°");
    g_signal_connect(rotate_left, "clicked", G_CALLBACK(on_rotate_left_clicked), artos);
    g_signal_connect(rotate_right, "clicked", G_CALLBACK(on_rotate_right_clicked), artos);
    gtk_box_pack_start(GTK_BOX(rotate_box), rotate_left, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(rotate_box), rotate_right, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), rotate_box, FALSE, FALSE, 0);

    /* Flip buttons */
    GtkWidget *flip_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget *flip_h = gtk_button_new_with_label("↔ Flip H");
    GtkWidget *flip_v = gtk_button_new_with_label("↕ Flip V");
    g_signal_connect(flip_h, "clicked", G_CALLBACK(on_flip_h_clicked), artos);
    g_signal_connect(flip_v, "clicked", G_CALLBACK(on_flip_v_clicked), artos);
    gtk_box_pack_start(GTK_BOX(flip_box), flip_h, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(flip_box), flip_v, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), flip_box, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(frame), box);
    return frame;
}

/* ==============================================================================
 * Reference Image Panel
 * ============================================================================== */

static void on_add_reference_clicked(GtkButton *button, phantom_artos_t *artos) {
    (void)button;
    GtkWidget *dialog = gtk_file_chooser_dialog_new("Add Reference Image",
        GTK_WINDOW(artos->window), GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Open", GTK_RESPONSE_ACCEPT, NULL);

    GtkFileFilter *filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Images");
    gtk_file_filter_add_mime_type(filter, "image/png");
    gtk_file_filter_add_mime_type(filter, "image/jpeg");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        artos_reference_add(artos, filename);
        g_free(filename);
    }
    gtk_widget_destroy(dialog);
}

static void on_reference_opacity_changed(GtkRange *range, phantom_artos_t *artos) {
    double value = gtk_range_get_value(range);
    if (artos->active_reference) {
        artos->active_reference->opacity = value;
        gtk_widget_queue_draw(artos->canvas_area);
    }
}

static void on_toggle_references_clicked(GtkToggleButton *button, phantom_artos_t *artos) {
    artos->show_references = gtk_toggle_button_get_active(button);
    gtk_widget_queue_draw(artos->canvas_area);
}

GtkWidget *artos_create_reference_panel(phantom_artos_t *artos) {
    GtkWidget *frame = gtk_frame_new("Reference Images");
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(box), 5);

    /* Show/hide toggle */
    GtkWidget *toggle = gtk_check_button_new_with_label("Show References");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(toggle), TRUE);
    artos->show_references = 1;
    g_signal_connect(toggle, "toggled", G_CALLBACK(on_toggle_references_clicked), artos);
    gtk_box_pack_start(GTK_BOX(box), toggle, FALSE, FALSE, 0);

    /* Add button */
    GtkWidget *add_btn = gtk_button_new_with_label("+ Add Reference");
    g_signal_connect(add_btn, "clicked", G_CALLBACK(on_add_reference_clicked), artos);
    gtk_box_pack_start(GTK_BOX(box), add_btn, FALSE, FALSE, 0);

    /* Opacity slider */
    GtkWidget *opacity_label = gtk_label_new("Opacity:");
    gtk_widget_set_halign(opacity_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), opacity_label, FALSE, FALSE, 0);

    artos->reference_opacity_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.1, 1.0, 0.1);
    gtk_range_set_value(GTK_RANGE(artos->reference_opacity_scale), 0.5);
    g_signal_connect(artos->reference_opacity_scale, "value-changed",
                     G_CALLBACK(on_reference_opacity_changed), artos);
    gtk_box_pack_start(GTK_BOX(box), artos->reference_opacity_scale, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(frame), box);
    artos->reference_panel = frame;
    return frame;
}

/* ==============================================================================
 * Color Wheel with Harmony
 * ============================================================================== */

static gboolean on_color_wheel_draw(GtkWidget *widget, cairo_t *cr, phantom_artos_t *artos) {
    int width = gtk_widget_get_allocated_width(widget);
    int height = gtk_widget_get_allocated_height(widget);
    int size = (width < height ? width : height) - 10;
    int cx = width / 2;
    int cy = height / 2;
    int radius = size / 2;

    /* Draw color wheel */
    for (int y = -radius; y <= radius; y++) {
        for (int x = -radius; x <= radius; x++) {
            double dist = sqrt(x * x + y * y);
            if (dist <= radius) {
                double h = atan2(y, x) * 180.0 / M_PI + 180.0;
                double s = dist / radius;
                artos_color_t c;
                artos_color_from_hsv(&c, h, s, 1.0);
                cairo_set_source_rgb(cr, c.r, c.g, c.b);
                cairo_rectangle(cr, cx + x, cy + y, 1, 1);
                cairo_fill(cr);
            }
        }
    }

    /* Draw harmony colors */
    for (int i = 0; i < artos->harmony_color_count; i++) {
        double h, s, v;
        artos_color_to_hsv(&artos->harmony_colors[i], &h, &s, &v);
        double angle = (h - 180.0) * M_PI / 180.0;
        double r = s * radius;
        int px = cx + (int)(r * cos(angle));
        int py = cy + (int)(r * sin(angle));

        /* Draw marker */
        cairo_set_source_rgb(cr, 0, 0, 0);
        cairo_arc(cr, px, py, 6, 0, 2 * M_PI);
        cairo_stroke(cr);
        cairo_set_source_rgb(cr, artos->harmony_colors[i].r,
                             artos->harmony_colors[i].g,
                             artos->harmony_colors[i].b);
        cairo_arc(cr, px, py, 5, 0, 2 * M_PI);
        cairo_fill(cr);
    }

    return TRUE;
}

static void on_harmony_changed(GtkComboBox *combo, phantom_artos_t *artos) {
    int active = gtk_combo_box_get_active(combo);
    artos_color_harmony_set_type(artos, (artos_color_harmony_t)active);
}

GtkWidget *artos_create_color_wheel_panel(phantom_artos_t *artos) {
    GtkWidget *frame = gtk_frame_new("Color Harmony");
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(box), 5);

    /* Harmony type selector */
    artos->harmony_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(artos->harmony_combo), "None");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(artos->harmony_combo), "Complementary");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(artos->harmony_combo), "Analogous");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(artos->harmony_combo), "Triadic");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(artos->harmony_combo), "Split Comp.");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(artos->harmony_combo), "Tetradic");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(artos->harmony_combo), "Monochromatic");
    gtk_combo_box_set_active(GTK_COMBO_BOX(artos->harmony_combo), 0);
    g_signal_connect(artos->harmony_combo, "changed", G_CALLBACK(on_harmony_changed), artos);
    gtk_box_pack_start(GTK_BOX(box), artos->harmony_combo, FALSE, FALSE, 0);

    /* Color wheel drawing area */
    artos->color_wheel_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(artos->color_wheel_area, 150, 150);
    g_signal_connect(artos->color_wheel_area, "draw", G_CALLBACK(on_color_wheel_draw), artos);
    gtk_box_pack_start(GTK_BOX(box), artos->color_wheel_area, FALSE, FALSE, 0);

    /* Initialize harmony */
    artos->color_harmony = ARTOS_HARMONY_NONE;
    artos_color_harmony_update(artos);

    gtk_container_add(GTK_CONTAINER(frame), box);
    return frame;
}

/* ==============================================================================
 * Symmetry Mode Panel
 * ============================================================================== */

static void on_symmetry_changed(GtkComboBox *combo, phantom_artos_t *artos) {
    int active = gtk_combo_box_get_active(combo);
    artos_symmetry_set_mode(artos, (artos_symmetry_mode_t)active);
}

static void on_symmetry_guides_toggled(GtkToggleButton *button, phantom_artos_t *artos) {
    artos->symmetry_show_guides = gtk_toggle_button_get_active(button);
    gtk_widget_queue_draw(artos->canvas_area);
}

GtkWidget *artos_create_symmetry_panel(phantom_artos_t *artos) {
    GtkWidget *frame = gtk_frame_new("Symmetry");
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(box), 5);

    /* Symmetry mode selector */
    artos->symmetry_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(artos->symmetry_combo), "Off");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(artos->symmetry_combo), "↔ Horizontal");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(artos->symmetry_combo), "↕ Vertical");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(artos->symmetry_combo), "✚ Both (4-way)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(artos->symmetry_combo), "△ Radial 3");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(artos->symmetry_combo), "◇ Radial 4");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(artos->symmetry_combo), "✡ Radial 6");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(artos->symmetry_combo), "✴ Radial 8");
    gtk_combo_box_set_active(GTK_COMBO_BOX(artos->symmetry_combo), 0);
    g_signal_connect(artos->symmetry_combo, "changed", G_CALLBACK(on_symmetry_changed), artos);
    gtk_box_pack_start(GTK_BOX(box), artos->symmetry_combo, FALSE, FALSE, 0);

    /* Show guides checkbox */
    GtkWidget *guides_check = gtk_check_button_new_with_label("Show Guides");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(guides_check), TRUE);
    artos->symmetry_show_guides = 1;
    g_signal_connect(guides_check, "toggled", G_CALLBACK(on_symmetry_guides_toggled), artos);
    gtk_box_pack_start(GTK_BOX(box), guides_check, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(frame), box);
    artos->symmetry_panel = frame;
    return frame;
}

/* ==============================================================================
 * Brush Stabilizer Panel
 * ============================================================================== */

static void on_stabilizer_toggled(GtkToggleButton *button, phantom_artos_t *artos) {
    artos_stabilizer_enable(artos, gtk_toggle_button_get_active(button));
}

static void on_stabilizer_strength_changed(GtkRange *range, phantom_artos_t *artos) {
    int value = (int)gtk_range_get_value(range);
    artos_stabilizer_set_strength(artos, value);
}

GtkWidget *artos_create_stabilizer_panel(phantom_artos_t *artos) {
    GtkWidget *frame = gtk_frame_new("Brush Stabilizer");
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(box), 5);

    /* Enable checkbox */
    artos->stabilizer_check = gtk_check_button_new_with_label("Enable Stabilizer");
    g_signal_connect(artos->stabilizer_check, "toggled", G_CALLBACK(on_stabilizer_toggled), artos);
    gtk_box_pack_start(GTK_BOX(box), artos->stabilizer_check, FALSE, FALSE, 0);

    /* Strength slider */
    GtkWidget *label = gtk_label_new("Smoothing:");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);

    artos->stabilizer_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 1, 10, 1);
    gtk_range_set_value(GTK_RANGE(artos->stabilizer_scale), 5);
    gtk_scale_set_draw_value(GTK_SCALE(artos->stabilizer_scale), TRUE);
    artos->stabilizer_strength = 5;
    g_signal_connect(artos->stabilizer_scale, "value-changed",
                     G_CALLBACK(on_stabilizer_strength_changed), artos);
    gtk_box_pack_start(GTK_BOX(box), artos->stabilizer_scale, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(frame), box);
    return frame;
}

/* ==============================================================================
 * Canvas Rotation Panel
 * ============================================================================== */

static void on_rotation_changed(GtkRange *range, phantom_artos_t *artos) {
    double value = gtk_range_get_value(range);
    artos_canvas_set_rotation(artos, value);
}

static void on_rotation_reset_clicked(GtkButton *button, phantom_artos_t *artos) {
    (void)button;
    artos_canvas_reset_rotation(artos);
    gtk_range_set_value(GTK_RANGE(artos->rotation_scale), 0);
}

static void on_flip_view_h_clicked(GtkButton *button, phantom_artos_t *artos) {
    (void)button;
    artos_canvas_flip_view(artos, 1);
}

static void on_flip_view_v_clicked(GtkButton *button, phantom_artos_t *artos) {
    (void)button;
    artos_canvas_flip_view(artos, 0);
}

GtkWidget *artos_create_canvas_rotation_panel(phantom_artos_t *artos) {
    GtkWidget *frame = gtk_frame_new("Canvas View");
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(box), 5);

    /* Rotation slider */
    GtkWidget *label = gtk_label_new("Rotation:");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);

    artos->rotation_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, -180, 180, 15);
    gtk_range_set_value(GTK_RANGE(artos->rotation_scale), 0);
    gtk_scale_set_draw_value(GTK_SCALE(artos->rotation_scale), TRUE);
    g_signal_connect(artos->rotation_scale, "value-changed",
                     G_CALLBACK(on_rotation_changed), artos);
    gtk_box_pack_start(GTK_BOX(box), artos->rotation_scale, FALSE, FALSE, 0);

    /* Flip and reset buttons */
    GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);

    GtkWidget *flip_h_btn = gtk_button_new_with_label("↔");
    gtk_widget_set_tooltip_text(flip_h_btn, "Flip View Horizontal");
    g_signal_connect(flip_h_btn, "clicked", G_CALLBACK(on_flip_view_h_clicked), artos);
    gtk_box_pack_start(GTK_BOX(btn_box), flip_h_btn, TRUE, TRUE, 0);

    GtkWidget *flip_v_btn = gtk_button_new_with_label("↕");
    gtk_widget_set_tooltip_text(flip_v_btn, "Flip View Vertical");
    g_signal_connect(flip_v_btn, "clicked", G_CALLBACK(on_flip_view_v_clicked), artos);
    gtk_box_pack_start(GTK_BOX(btn_box), flip_v_btn, TRUE, TRUE, 0);

    GtkWidget *reset_btn = gtk_button_new_with_label("Reset");
    g_signal_connect(reset_btn, "clicked", G_CALLBACK(on_rotation_reset_clicked), artos);
    gtk_box_pack_start(GTK_BOX(btn_box), reset_btn, TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(box), btn_box, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(frame), box);
    artos->rotation_panel = frame;
    return frame;
}

static void artos_build_ui(phantom_artos_t *artos) {
    /* Main horizontal layout */
    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

    /* Left sidebar - tools */
    GtkWidget *left_sidebar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_size_request(left_sidebar, 100, -1);

    gtk_box_pack_start(GTK_BOX(left_sidebar), artos_create_tool_palette(artos), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(left_sidebar), artos_create_color_panel(artos), FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(main_box), left_sidebar, FALSE, FALSE, 0);

    /* Center - canvas area */
    GtkWidget *center_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    /* Toolbar */
    gtk_box_pack_start(GTK_BOX(center_box), artos_create_toolbar(artos), FALSE, FALSE, 0);

    /* Canvas */
    artos->canvas_area = gtk_drawing_area_new();
    gtk_widget_set_can_focus(artos->canvas_area, TRUE);
    gtk_widget_add_events(artos->canvas_area,
                          GDK_BUTTON_PRESS_MASK |
                          GDK_BUTTON_RELEASE_MASK |
                          GDK_POINTER_MOTION_MASK |
                          GDK_SCROLL_MASK |
                          GDK_KEY_PRESS_MASK);

    g_signal_connect(artos->canvas_area, "draw", G_CALLBACK(on_canvas_draw), artos);
    g_signal_connect(artos->canvas_area, "button-press-event", G_CALLBACK(on_canvas_button_press), artos);
    g_signal_connect(artos->canvas_area, "button-release-event", G_CALLBACK(on_canvas_button_release), artos);
    g_signal_connect(artos->canvas_area, "motion-notify-event", G_CALLBACK(on_canvas_motion), artos);
    g_signal_connect(artos->canvas_area, "scroll-event", G_CALLBACK(on_canvas_scroll), artos);

    gtk_box_pack_start(GTK_BOX(center_box), artos->canvas_area, TRUE, TRUE, 0);

    /* Status bar */
    artos->status_bar = gtk_statusbar_new();
    gtk_box_pack_start(GTK_BOX(center_box), artos->status_bar, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(main_box), center_box, TRUE, TRUE, 0);

    /* Right sidebar - brush settings, layers, and dictation */
    GtkWidget *right_sidebar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_size_request(right_sidebar, 220, -1);

    /* Make right sidebar scrollable for all panels */
    GtkWidget *right_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(right_scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

    GtkWidget *right_inner = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(right_inner), 5);

    gtk_box_pack_start(GTK_BOX(right_inner), artos_create_brush_settings(artos), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(right_inner), artos_create_stabilizer_panel(artos), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(right_inner), artos_create_color_wheel_panel(artos), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(right_inner), artos_create_symmetry_panel(artos), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(right_inner), artos_create_layer_panel(artos), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(right_inner), artos_create_transform_panel(artos), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(right_inner), artos_create_canvas_rotation_panel(artos), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(right_inner), artos_create_reference_panel(artos), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(right_inner), artos_create_dictation_panel(artos), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(right_inner), artos_create_facetrack_panel(artos), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(right_inner), artos_create_ai_panel(artos), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(right_inner), artos_create_voiceart_panel(artos), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(right_inner), artos_create_collab_panel(artos), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(right_inner), artos_create_drawnet_panel(artos), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(right_inner), artos_create_journal_panel(artos), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(right_inner), artos_create_voicecmd_panel(artos), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(right_inner), artos_create_ai_smart_panel(artos), FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(right_scroll), right_inner);
    gtk_box_pack_start(GTK_BOX(right_sidebar), right_scroll, TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(main_box), right_sidebar, FALSE, FALSE, 0);

    /* Add to window */
    gtk_container_add(GTK_CONTAINER(artos->window), main_box);
}

/* ==============================================================================
 * Public API
 * ============================================================================== */

phantom_artos_t *artos_create(void) {
    phantom_artos_t *artos = calloc(1, sizeof(phantom_artos_t));
    if (!artos) return NULL;

    /* Initialize defaults */
    artos->zoom = 1.0;
    artos->pan_x = 0;
    artos->pan_y = 0;
    artos->current_tool = ARTOS_TOOL_BRUSH;

    /* Default colors */
    artos->foreground_color = (artos_color_t){0, 0, 0, 1};  /* Black */
    artos->background_color = (artos_color_t){1, 1, 1, 1};  /* White */

    /* Initialize default brushes */
    artos_init_default_brushes(artos);

    /* Initialize Creative Journal */
    artos_journal_init(artos);

    /* Initialize Voice Commands */
    artos_voicecmd_init(artos);

    /* Initialize AI Smart Features */
    artos_ai_color_suggest_init(artos);
    artos_ai_perspective_init(artos);
    artos_ai_sketch_cleanup_init(artos);

    /* Create window */
    artos->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(artos->window), "ArtOS - Digital Art Studio");
    gtk_window_set_default_size(GTK_WINDOW(artos->window), 1400, 900);

    /* Build UI */
    artos_build_ui(artos);

    /* Create default document */
    artos->document = artos_document_new(1920, 1080, "Untitled");
    artos_refresh_layer_list(artos);

    /* Start marching ants animation */
    artos->selection_timer = g_timeout_add(100, (GSourceFunc)on_selection_animate, artos);

    return artos;
}

void artos_destroy(phantom_artos_t *artos) {
    if (!artos) return;

    if (artos->selection_timer) {
        g_source_remove(artos->selection_timer);
    }

    /* End journal session if active */
    if (artos->journal.current_session) {
        artos_journal_end_session(artos);
    }
    artos_journal_cleanup(artos);

    /* Cleanup voice commands */
    artos_voicecmd_cleanup(artos);

    if (artos->document) {
        artos_document_free(artos->document);
    }

    if (artos->window) {
        gtk_widget_destroy(artos->window);
    }

    free(artos);
}

GtkWidget *artos_get_widget(phantom_artos_t *artos) {
    if (!artos) return NULL;

    /* Return the main content (without window wrapper) for embedding */
    GtkWidget *main_box = gtk_bin_get_child(GTK_BIN(artos->window));
    if (main_box) {
        g_object_ref(main_box);
        gtk_container_remove(GTK_CONTAINER(artos->window), main_box);
        return main_box;
    }

    return NULL;
}

/* ==============================================================================
 * Dictation Drawing System
 * "Draw with your voice"
 * ============================================================================== */

/* Named color definitions */
static struct {
    const char *name;
    double r, g, b;
} named_colors[] = {
    {"red", 1.0, 0.0, 0.0},
    {"green", 0.0, 0.5, 0.0},
    {"blue", 0.0, 0.0, 1.0},
    {"yellow", 1.0, 1.0, 0.0},
    {"orange", 1.0, 0.65, 0.0},
    {"purple", 0.5, 0.0, 0.5},
    {"violet", 0.93, 0.51, 0.93},
    {"pink", 1.0, 0.75, 0.8},
    {"cyan", 0.0, 1.0, 1.0},
    {"magenta", 1.0, 0.0, 1.0},
    {"white", 1.0, 1.0, 1.0},
    {"black", 0.0, 0.0, 0.0},
    {"gray", 0.5, 0.5, 0.5},
    {"grey", 0.5, 0.5, 0.5},
    {"brown", 0.65, 0.16, 0.16},
    {"gold", 1.0, 0.84, 0.0},
    {"silver", 0.75, 0.75, 0.75},
    {"navy", 0.0, 0.0, 0.5},
    {"teal", 0.0, 0.5, 0.5},
    {"maroon", 0.5, 0.0, 0.0},
    {"olive", 0.5, 0.5, 0.0},
    {"lime", 0.0, 1.0, 0.0},
    {"aqua", 0.0, 1.0, 1.0},
    {"coral", 1.0, 0.5, 0.31},
    {"salmon", 0.98, 0.5, 0.45},
    {"turquoise", 0.25, 0.88, 0.82},
    {"indigo", 0.29, 0.0, 0.51},
    {"beige", 0.96, 0.96, 0.86},
    {"tan", 0.82, 0.71, 0.55},
    {"crimson", 0.86, 0.08, 0.24},
    {"scarlet", 1.0, 0.14, 0.0},
    {"sky blue", 0.53, 0.81, 0.92},
    {"forest green", 0.13, 0.55, 0.13},
    {"dark blue", 0.0, 0.0, 0.55},
    {"light blue", 0.68, 0.85, 0.9},
    {"dark green", 0.0, 0.39, 0.0},
    {"light green", 0.56, 0.93, 0.56},
    {NULL, 0, 0, 0}
};

/* Helper to find color by name */
static int find_color_by_name(const char *name, artos_color_t *color) {
    char lower[64];
    int i = 0;
    while (name[i] && i < 63) {
        lower[i] = tolower((unsigned char)name[i]);
        i++;
    }
    lower[i] = '\0';

    for (int j = 0; named_colors[j].name != NULL; j++) {
        if (strcmp(lower, named_colors[j].name) == 0) {
            color->r = named_colors[j].r;
            color->g = named_colors[j].g;
            color->b = named_colors[j].b;
            color->a = 1.0;
            return 1;
        }
    }
    return 0;
}

/* Helper to extract number from text */
static double extract_number(const char *text, double default_val) {
    const char *p = text;
    while (*p) {
        if (isdigit((unsigned char)*p) || (*p == '.' && isdigit((unsigned char)*(p+1)))) {
            return atof(p);
        }
        p++;
    }
    return default_val;
}

/* Helper to check if text contains word */
static int contains_word(const char *text, const char *word) {
    char lower_text[512];
    char lower_word[64];

    int i = 0;
    while (text[i] && i < 511) {
        lower_text[i] = tolower((unsigned char)text[i]);
        i++;
    }
    lower_text[i] = '\0';

    i = 0;
    while (word[i] && i < 63) {
        lower_word[i] = tolower((unsigned char)word[i]);
        i++;
    }
    lower_word[i] = '\0';

    return strstr(lower_text, lower_word) != NULL;
}

/* Initialize dictation system */
void artos_dictation_init(phantom_artos_t *artos) {
    if (!artos) return;

    memset(&artos->dictation, 0, sizeof(artos_dictation_t));
    artos->dictation.default_size = 100;
    artos->dictation.default_filled = 0;
    artos->dictation.pen_x = ARTOS_DEFAULT_WIDTH / 2;
    artos->dictation.pen_y = ARTOS_DEFAULT_HEIGHT / 2;

    /* Initialize color lookup */
    artos->dictation.color_count = 0;
    for (int i = 0; named_colors[i].name != NULL && artos->dictation.color_count < 64; i++) {
        artos->dictation.color_names[artos->dictation.color_count].name = named_colors[i].name;
        artos->dictation.color_names[artos->dictation.color_count].color.r = named_colors[i].r;
        artos->dictation.color_names[artos->dictation.color_count].color.g = named_colors[i].g;
        artos->dictation.color_names[artos->dictation.color_count].color.b = named_colors[i].b;
        artos->dictation.color_names[artos->dictation.color_count].color.a = 1.0;
        artos->dictation.color_count++;
    }
}

/* Enable/disable dictation mode */
void artos_dictation_enable(phantom_artos_t *artos, int enable) {
    if (!artos) return;
    artos->dictation.enabled = enable;

    if (enable) {
        artos_dictation_show_feedback(artos, "Dictation enabled. Type commands like: draw a red circle");
    } else {
        artos_dictation_show_feedback(artos, "Dictation disabled");
    }
}

/* Parse voice/text command into structured command */
int artos_dictation_parse(const char *text, artos_dictation_parsed_t *result) {
    if (!text || !result) return 0;

    memset(result, 0, sizeof(artos_dictation_parsed_t));
    strncpy(result->raw_text, text, ARTOS_DICTATION_MAX_CMD - 1);
    result->confidence = 1.0;
    result->success = 1;
    result->pos_ref = ARTOS_POS_CENTER;  /* Default to center */

    /* Check for shape drawing commands */
    if (contains_word(text, "line")) {
        result->command = ARTOS_DICT_CMD_DRAW_LINE;
        result->width = extract_number(text, 200);
    }
    else if (contains_word(text, "rectangle") || contains_word(text, "rect") || contains_word(text, "square")) {
        result->command = ARTOS_DICT_CMD_DRAW_RECT;
        result->width = extract_number(text, 100);
        result->height = contains_word(text, "square") ? result->width : extract_number(text, result->width);
        result->filled = contains_word(text, "filled") || contains_word(text, "fill") || contains_word(text, "solid");
    }
    else if (contains_word(text, "circle")) {
        result->command = ARTOS_DICT_CMD_DRAW_CIRCLE;
        result->radius = extract_number(text, 50);
        result->filled = contains_word(text, "filled") || contains_word(text, "fill") || contains_word(text, "solid");
    }
    else if (contains_word(text, "ellipse") || contains_word(text, "oval")) {
        result->command = ARTOS_DICT_CMD_DRAW_ELLIPSE;
        result->width = extract_number(text, 100);
        result->height = extract_number(text, result->width * 0.6);
        result->filled = contains_word(text, "filled") || contains_word(text, "fill") || contains_word(text, "solid");
    }
    else if (contains_word(text, "triangle")) {
        result->command = ARTOS_DICT_CMD_DRAW_TRIANGLE;
        result->width = extract_number(text, 100);
        result->filled = contains_word(text, "filled") || contains_word(text, "fill") || contains_word(text, "solid");
    }
    else if (contains_word(text, "star")) {
        result->command = ARTOS_DICT_CMD_DRAW_STAR;
        result->width = extract_number(text, 80);
        result->points = 5;  /* Default 5-pointed star */
        if (contains_word(text, "6") || contains_word(text, "six")) result->points = 6;
        if (contains_word(text, "7") || contains_word(text, "seven")) result->points = 7;
        if (contains_word(text, "8") || contains_word(text, "eight")) result->points = 8;
        result->filled = contains_word(text, "filled") || contains_word(text, "fill") || contains_word(text, "solid");
    }
    else if (contains_word(text, "arrow")) {
        result->command = ARTOS_DICT_CMD_DRAW_ARROW;
        result->width = extract_number(text, 150);
    }
    else if (contains_word(text, "heart")) {
        result->command = ARTOS_DICT_CMD_DRAW_HEART;
        result->width = extract_number(text, 80);
        result->filled = contains_word(text, "filled") || contains_word(text, "fill") || contains_word(text, "solid");
    }
    else if (contains_word(text, "spiral")) {
        result->command = ARTOS_DICT_CMD_DRAW_SPIRAL;
        result->width = extract_number(text, 100);
        result->radius = 3;  /* Number of turns */
    }
    /* Color commands */
    else if (contains_word(text, "color") || contains_word(text, "colour")) {
        result->command = ARTOS_DICT_CMD_SET_COLOR;
        /* Find color name in text */
        for (int i = 0; named_colors[i].name != NULL; i++) {
            if (contains_word(text, named_colors[i].name)) {
                result->color.r = named_colors[i].r;
                result->color.g = named_colors[i].g;
                result->color.b = named_colors[i].b;
                result->color.a = 1.0;
                result->has_color = 1;
                break;
            }
        }
    }
    /* Size commands */
    else if (contains_word(text, "size") || contains_word(text, "brush")) {
        result->command = ARTOS_DICT_CMD_SET_SIZE;
        result->size = extract_number(text, 10);
        result->has_size = 1;
    }
    else if (contains_word(text, "bigger") || contains_word(text, "larger")) {
        result->command = ARTOS_DICT_CMD_BIGGER;
    }
    else if (contains_word(text, "smaller") || contains_word(text, "less")) {
        result->command = ARTOS_DICT_CMD_SMALLER;
    }
    /* Tool commands */
    else if (contains_word(text, "pencil")) {
        result->command = ARTOS_DICT_CMD_USE_PENCIL;
        result->tool = ARTOS_TOOL_PENCIL;
        result->has_tool = 1;
    }
    else if (contains_word(text, "eraser") || contains_word(text, "erase")) {
        result->command = ARTOS_DICT_CMD_USE_ERASER;
        result->tool = ARTOS_TOOL_ERASER;
        result->has_tool = 1;
    }
    else if (contains_word(text, "brush") && !contains_word(text, "size")) {
        result->command = ARTOS_DICT_CMD_USE_BRUSH;
        result->tool = ARTOS_TOOL_BRUSH;
        result->has_tool = 1;
    }
    /* Action commands */
    else if (contains_word(text, "undo")) {
        result->command = ARTOS_DICT_CMD_UNDO;
    }
    else if (contains_word(text, "redo")) {
        result->command = ARTOS_DICT_CMD_REDO;
    }
    else if (contains_word(text, "clear") || contains_word(text, "erase all")) {
        result->command = ARTOS_DICT_CMD_CLEAR;
    }
    else if (contains_word(text, "new layer") || contains_word(text, "add layer")) {
        result->command = ARTOS_DICT_CMD_NEW_LAYER;
    }
    /* Movement commands */
    else if (contains_word(text, "move to") || contains_word(text, "go to")) {
        result->command = ARTOS_DICT_CMD_MOVE_TO;
        if (contains_word(text, "center")) result->pos_ref = ARTOS_POS_CENTER;
        else if (contains_word(text, "top left")) result->pos_ref = ARTOS_POS_TOP_LEFT;
        else if (contains_word(text, "top right")) result->pos_ref = ARTOS_POS_TOP_RIGHT;
        else if (contains_word(text, "bottom left")) result->pos_ref = ARTOS_POS_BOTTOM_LEFT;
        else if (contains_word(text, "bottom right")) result->pos_ref = ARTOS_POS_BOTTOM_RIGHT;
        else if (contains_word(text, "top")) result->pos_ref = ARTOS_POS_TOP;
        else if (contains_word(text, "bottom")) result->pos_ref = ARTOS_POS_BOTTOM;
        else if (contains_word(text, "left")) result->pos_ref = ARTOS_POS_LEFT;
        else if (contains_word(text, "right")) result->pos_ref = ARTOS_POS_RIGHT;
    }
    else if (contains_word(text, "go left") || contains_word(text, "move left")) {
        result->command = ARTOS_DICT_CMD_GO_LEFT;
        result->move_amount = extract_number(text, 50);
    }
    else if (contains_word(text, "go right") || contains_word(text, "move right")) {
        result->command = ARTOS_DICT_CMD_GO_RIGHT;
        result->move_amount = extract_number(text, 50);
    }
    else if (contains_word(text, "go up") || contains_word(text, "move up")) {
        result->command = ARTOS_DICT_CMD_GO_UP;
        result->move_amount = extract_number(text, 50);
    }
    else if (contains_word(text, "go down") || contains_word(text, "move down")) {
        result->command = ARTOS_DICT_CMD_GO_DOWN;
        result->move_amount = extract_number(text, 50);
    }
    /* Continuous drawing */
    else if (contains_word(text, "pen down") || contains_word(text, "start drawing")) {
        result->command = ARTOS_DICT_CMD_START_DRAWING;
    }
    else if (contains_word(text, "pen up") || contains_word(text, "stop drawing")) {
        result->command = ARTOS_DICT_CMD_STOP_DRAWING;
    }
    else {
        result->command = ARTOS_DICT_CMD_NONE;
        snprintf(result->error, sizeof(result->error), "Unknown command: %s", text);
        result->success = 0;
    }

    /* Check for color modifier in draw commands */
    if (result->command >= ARTOS_DICT_CMD_DRAW_LINE &&
        result->command <= ARTOS_DICT_CMD_DRAW_SPIRAL) {
        for (int i = 0; named_colors[i].name != NULL; i++) {
            if (contains_word(text, named_colors[i].name)) {
                result->color.r = named_colors[i].r;
                result->color.g = named_colors[i].g;
                result->color.b = named_colors[i].b;
                result->color.a = 1.0;
                result->has_color = 1;
                break;
            }
        }
    }

    /* Position reference */
    if (contains_word(text, "at center") || contains_word(text, "in center")) {
        result->pos_ref = ARTOS_POS_CENTER;
    } else if (contains_word(text, "at top")) {
        result->pos_ref = ARTOS_POS_TOP;
    } else if (contains_word(text, "at bottom")) {
        result->pos_ref = ARTOS_POS_BOTTOM;
    } else if (contains_word(text, "at left")) {
        result->pos_ref = ARTOS_POS_LEFT;
    } else if (contains_word(text, "at right")) {
        result->pos_ref = ARTOS_POS_RIGHT;
    } else if (contains_word(text, "here") || contains_word(text, "at cursor")) {
        result->pos_ref = ARTOS_POS_CURSOR;
    }

    return result->success;
}

/* Advanced shape drawing functions */
void artos_draw_circle(phantom_artos_t *artos, double cx, double cy, double radius, int filled) {
    if (!artos || !artos->document) return;

    artos_layer_t *layer = artos_layer_get_active(artos->document);
    if (!layer || layer->locked) return;

    cairo_t *cr = cairo_create(layer->surface);
    cairo_set_source_rgba(cr,
                          artos->foreground_color.r,
                          artos->foreground_color.g,
                          artos->foreground_color.b,
                          artos->foreground_color.a);
    cairo_set_line_width(cr, artos->current_brush.size);

    cairo_arc(cr, cx, cy, radius, 0, 2 * M_PI);

    if (filled) {
        cairo_fill(cr);
    } else {
        cairo_stroke(cr);
    }

    cairo_destroy(cr);
    artos->document->composite_dirty = 1;
    artos->document->modified = 1;
    gtk_widget_queue_draw(artos->canvas_area);
}

void artos_draw_triangle(phantom_artos_t *artos, double cx, double cy, double size, int filled) {
    if (!artos || !artos->document) return;

    artos_layer_t *layer = artos_layer_get_active(artos->document);
    if (!layer || layer->locked) return;

    cairo_t *cr = cairo_create(layer->surface);
    cairo_set_source_rgba(cr,
                          artos->foreground_color.r,
                          artos->foreground_color.g,
                          artos->foreground_color.b,
                          artos->foreground_color.a);
    cairo_set_line_width(cr, artos->current_brush.size);

    double h = size * 0.866;  /* height = size * sqrt(3)/2 */
    cairo_move_to(cr, cx, cy - h * 2/3);
    cairo_line_to(cr, cx - size/2, cy + h/3);
    cairo_line_to(cr, cx + size/2, cy + h/3);
    cairo_close_path(cr);

    if (filled) {
        cairo_fill(cr);
    } else {
        cairo_stroke(cr);
    }

    cairo_destroy(cr);
    artos->document->composite_dirty = 1;
    artos->document->modified = 1;
    gtk_widget_queue_draw(artos->canvas_area);
}

void artos_draw_star(phantom_artos_t *artos, double cx, double cy, double size, int points, int filled) {
    if (!artos || !artos->document || points < 3) return;

    artos_layer_t *layer = artos_layer_get_active(artos->document);
    if (!layer || layer->locked) return;

    cairo_t *cr = cairo_create(layer->surface);
    cairo_set_source_rgba(cr,
                          artos->foreground_color.r,
                          artos->foreground_color.g,
                          artos->foreground_color.b,
                          artos->foreground_color.a);
    cairo_set_line_width(cr, artos->current_brush.size);

    double outer_radius = size;
    double inner_radius = size * 0.4;
    double angle_step = M_PI / points;

    for (int i = 0; i < points * 2; i++) {
        double radius = (i % 2 == 0) ? outer_radius : inner_radius;
        double angle = -M_PI / 2 + i * angle_step;
        double x = cx + radius * cos(angle);
        double y = cy + radius * sin(angle);

        if (i == 0) {
            cairo_move_to(cr, x, y);
        } else {
            cairo_line_to(cr, x, y);
        }
    }
    cairo_close_path(cr);

    if (filled) {
        cairo_fill(cr);
    } else {
        cairo_stroke(cr);
    }

    cairo_destroy(cr);
    artos->document->composite_dirty = 1;
    artos->document->modified = 1;
    gtk_widget_queue_draw(artos->canvas_area);
}

void artos_draw_arrow(phantom_artos_t *artos, double x1, double y1, double x2, double y2) {
    if (!artos || !artos->document) return;

    artos_layer_t *layer = artos_layer_get_active(artos->document);
    if (!layer || layer->locked) return;

    cairo_t *cr = cairo_create(layer->surface);
    cairo_set_source_rgba(cr,
                          artos->foreground_color.r,
                          artos->foreground_color.g,
                          artos->foreground_color.b,
                          artos->foreground_color.a);
    cairo_set_line_width(cr, artos->current_brush.size);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

    /* Draw shaft */
    cairo_move_to(cr, x1, y1);
    cairo_line_to(cr, x2, y2);
    cairo_stroke(cr);

    /* Draw arrowhead */
    double angle = atan2(y2 - y1, x2 - x1);
    double head_size = 15 + artos->current_brush.size;
    double head_angle = M_PI / 6;

    cairo_move_to(cr, x2, y2);
    cairo_line_to(cr, x2 - head_size * cos(angle - head_angle),
                      y2 - head_size * sin(angle - head_angle));
    cairo_move_to(cr, x2, y2);
    cairo_line_to(cr, x2 - head_size * cos(angle + head_angle),
                      y2 - head_size * sin(angle + head_angle));
    cairo_stroke(cr);

    cairo_destroy(cr);
    artos->document->composite_dirty = 1;
    artos->document->modified = 1;
    gtk_widget_queue_draw(artos->canvas_area);
}

void artos_draw_heart(phantom_artos_t *artos, double cx, double cy, double size, int filled) {
    if (!artos || !artos->document) return;

    artos_layer_t *layer = artos_layer_get_active(artos->document);
    if (!layer || layer->locked) return;

    cairo_t *cr = cairo_create(layer->surface);
    cairo_set_source_rgba(cr,
                          artos->foreground_color.r,
                          artos->foreground_color.g,
                          artos->foreground_color.b,
                          artos->foreground_color.a);
    cairo_set_line_width(cr, artos->current_brush.size);

    double s = size / 2;

    /* Heart shape using bezier curves */
    cairo_move_to(cr, cx, cy + s * 0.4);
    cairo_curve_to(cr, cx, cy - s * 0.2, cx - s, cy - s * 0.4, cx - s, cy + s * 0.1);
    cairo_curve_to(cr, cx - s, cy + s * 0.6, cx, cy + s, cx, cy + s);
    cairo_curve_to(cr, cx, cy + s, cx + s, cy + s * 0.6, cx + s, cy + s * 0.1);
    cairo_curve_to(cr, cx + s, cy - s * 0.4, cx, cy - s * 0.2, cx, cy + s * 0.4);
    cairo_close_path(cr);

    if (filled) {
        cairo_fill(cr);
    } else {
        cairo_stroke(cr);
    }

    cairo_destroy(cr);
    artos->document->composite_dirty = 1;
    artos->document->modified = 1;
    gtk_widget_queue_draw(artos->canvas_area);
}

void artos_draw_spiral(phantom_artos_t *artos, double cx, double cy, double size, double turns) {
    if (!artos || !artos->document) return;

    artos_layer_t *layer = artos_layer_get_active(artos->document);
    if (!layer || layer->locked) return;

    cairo_t *cr = cairo_create(layer->surface);
    cairo_set_source_rgba(cr,
                          artos->foreground_color.r,
                          artos->foreground_color.g,
                          artos->foreground_color.b,
                          artos->foreground_color.a);
    cairo_set_line_width(cr, artos->current_brush.size);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

    int steps = (int)(turns * 100);
    double max_angle = turns * 2 * M_PI;

    for (int i = 0; i <= steps; i++) {
        double t = (double)i / steps;
        double angle = t * max_angle;
        double radius = t * size;
        double x = cx + radius * cos(angle);
        double y = cy + radius * sin(angle);

        if (i == 0) {
            cairo_move_to(cr, x, y);
        } else {
            cairo_line_to(cr, x, y);
        }
    }
    cairo_stroke(cr);

    cairo_destroy(cr);
    artos->document->composite_dirty = 1;
    artos->document->modified = 1;
    gtk_widget_queue_draw(artos->canvas_area);
}

/* Get position from reference */
static void get_position_from_ref(phantom_artos_t *artos, artos_position_ref_t ref,
                                   double *x, double *y) {
    if (!artos || !artos->document) return;

    int w = artos->document->width;
    int h = artos->document->height;

    switch (ref) {
        case ARTOS_POS_CENTER:
            *x = w / 2;
            *y = h / 2;
            break;
        case ARTOS_POS_TOP_LEFT:
            *x = w * 0.2;
            *y = h * 0.2;
            break;
        case ARTOS_POS_TOP_RIGHT:
            *x = w * 0.8;
            *y = h * 0.2;
            break;
        case ARTOS_POS_BOTTOM_LEFT:
            *x = w * 0.2;
            *y = h * 0.8;
            break;
        case ARTOS_POS_BOTTOM_RIGHT:
            *x = w * 0.8;
            *y = h * 0.8;
            break;
        case ARTOS_POS_TOP:
            *x = w / 2;
            *y = h * 0.2;
            break;
        case ARTOS_POS_BOTTOM:
            *x = w / 2;
            *y = h * 0.8;
            break;
        case ARTOS_POS_LEFT:
            *x = w * 0.2;
            *y = h / 2;
            break;
        case ARTOS_POS_RIGHT:
            *x = w * 0.8;
            *y = h / 2;
            break;
        case ARTOS_POS_CURSOR:
            *x = artos->dictation.pen_x;
            *y = artos->dictation.pen_y;
            break;
        default:
            *x = w / 2;
            *y = h / 2;
            break;
    }
}

/* Execute parsed dictation command */
int artos_dictation_execute(phantom_artos_t *artos, artos_dictation_parsed_t *cmd) {
    if (!artos || !cmd || !cmd->success) return 0;

    double x, y;
    artos_color_t saved_color;
    char feedback[256];

    /* Save current color if command has color override */
    if (cmd->has_color) {
        saved_color = artos->foreground_color;
        artos->foreground_color = cmd->color;
    }

    get_position_from_ref(artos, cmd->pos_ref, &x, &y);

    switch (cmd->command) {
        case ARTOS_DICT_CMD_DRAW_LINE:
            artos_draw_line(artos, x - cmd->width/2, y, x + cmd->width/2, y);
            snprintf(feedback, sizeof(feedback), "Drew line (%.0f px)", cmd->width);
            break;

        case ARTOS_DICT_CMD_DRAW_RECT: {
            double w = cmd->width > 0 ? cmd->width : 100;
            double h = cmd->height > 0 ? cmd->height : w;
            artos_draw_shape(artos, ARTOS_TOOL_RECTANGLE,
                             x - w/2, y - h/2, x + w/2, y + h/2, cmd->filled);
            snprintf(feedback, sizeof(feedback), "Drew %s rectangle (%.0fx%.0f)",
                     cmd->filled ? "filled" : "outline", w, h);
            break;
        }

        case ARTOS_DICT_CMD_DRAW_CIRCLE:
            artos_draw_circle(artos, x, y, cmd->radius > 0 ? cmd->radius : 50, cmd->filled);
            snprintf(feedback, sizeof(feedback), "Drew %s circle (r=%.0f)",
                     cmd->filled ? "filled" : "outline", cmd->radius);
            break;

        case ARTOS_DICT_CMD_DRAW_ELLIPSE: {
            double w = cmd->width > 0 ? cmd->width : 100;
            double h = cmd->height > 0 ? cmd->height : 60;
            artos_draw_shape(artos, ARTOS_TOOL_ELLIPSE,
                             x - w/2, y - h/2, x + w/2, y + h/2, cmd->filled);
            snprintf(feedback, sizeof(feedback), "Drew %s ellipse (%.0fx%.0f)",
                     cmd->filled ? "filled" : "outline", w, h);
            break;
        }

        case ARTOS_DICT_CMD_DRAW_TRIANGLE:
            artos_draw_triangle(artos, x, y, cmd->width > 0 ? cmd->width : 100, cmd->filled);
            snprintf(feedback, sizeof(feedback), "Drew %s triangle",
                     cmd->filled ? "filled" : "outline");
            break;

        case ARTOS_DICT_CMD_DRAW_STAR:
            artos_draw_star(artos, x, y, cmd->width > 0 ? cmd->width : 80,
                            cmd->points > 0 ? cmd->points : 5, cmd->filled);
            snprintf(feedback, sizeof(feedback), "Drew %d-pointed %s star",
                     cmd->points > 0 ? cmd->points : 5, cmd->filled ? "filled" : "outline");
            break;

        case ARTOS_DICT_CMD_DRAW_ARROW:
            artos_draw_arrow(artos, x - cmd->width/2, y, x + cmd->width/2, y);
            snprintf(feedback, sizeof(feedback), "Drew arrow (%.0f px)", cmd->width);
            break;

        case ARTOS_DICT_CMD_DRAW_HEART:
            artos_draw_heart(artos, x, y, cmd->width > 0 ? cmd->width : 80, cmd->filled);
            snprintf(feedback, sizeof(feedback), "Drew %s heart",
                     cmd->filled ? "filled" : "outline");
            break;

        case ARTOS_DICT_CMD_DRAW_SPIRAL:
            artos_draw_spiral(artos, x, y, cmd->width > 0 ? cmd->width : 100, 3);
            snprintf(feedback, sizeof(feedback), "Drew spiral");
            break;

        case ARTOS_DICT_CMD_SET_COLOR:
            if (cmd->has_color) {
                artos->foreground_color = cmd->color;
                /* Update color button */
                if (artos->color_button) {
                    GdkRGBA rgba = {cmd->color.r, cmd->color.g, cmd->color.b, cmd->color.a};
                    gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(artos->color_button), &rgba);
                }
                snprintf(feedback, sizeof(feedback), "Set color");
            }
            break;

        case ARTOS_DICT_CMD_SET_SIZE:
            if (cmd->has_size && cmd->size > 0) {
                artos->current_brush.size = cmd->size;
                if (artos->brush_size_scale) {
                    gtk_range_set_value(GTK_RANGE(artos->brush_size_scale), cmd->size);
                }
                snprintf(feedback, sizeof(feedback), "Set brush size to %.0f", cmd->size);
            }
            break;

        case ARTOS_DICT_CMD_BIGGER:
            artos->current_brush.size *= 1.5;
            if (artos->brush_size_scale) {
                gtk_range_set_value(GTK_RANGE(artos->brush_size_scale), artos->current_brush.size);
            }
            snprintf(feedback, sizeof(feedback), "Brush size: %.0f", artos->current_brush.size);
            break;

        case ARTOS_DICT_CMD_SMALLER:
            artos->current_brush.size /= 1.5;
            if (artos->current_brush.size < 1) artos->current_brush.size = 1;
            if (artos->brush_size_scale) {
                gtk_range_set_value(GTK_RANGE(artos->brush_size_scale), artos->current_brush.size);
            }
            snprintf(feedback, sizeof(feedback), "Brush size: %.0f", artos->current_brush.size);
            break;

        case ARTOS_DICT_CMD_USE_PENCIL:
            artos_set_tool(artos, ARTOS_TOOL_PENCIL);
            snprintf(feedback, sizeof(feedback), "Using pencil tool");
            break;

        case ARTOS_DICT_CMD_USE_BRUSH:
            artos_set_tool(artos, ARTOS_TOOL_BRUSH);
            snprintf(feedback, sizeof(feedback), "Using brush tool");
            break;

        case ARTOS_DICT_CMD_USE_ERASER:
            artos_set_tool(artos, ARTOS_TOOL_ERASER);
            snprintf(feedback, sizeof(feedback), "Using eraser tool");
            break;

        case ARTOS_DICT_CMD_UNDO:
            artos_undo(artos);
            snprintf(feedback, sizeof(feedback), "Undo");
            break;

        case ARTOS_DICT_CMD_REDO:
            artos_redo(artos);
            snprintf(feedback, sizeof(feedback), "Redo");
            break;

        case ARTOS_DICT_CMD_NEW_LAYER:
            artos_layer_add(artos->document, NULL);
            snprintf(feedback, sizeof(feedback), "Added new layer");
            break;

        case ARTOS_DICT_CMD_MOVE_TO:
            get_position_from_ref(artos, cmd->pos_ref,
                                   &artos->dictation.pen_x, &artos->dictation.pen_y);
            snprintf(feedback, sizeof(feedback), "Moved to (%.0f, %.0f)",
                     artos->dictation.pen_x, artos->dictation.pen_y);
            break;

        case ARTOS_DICT_CMD_GO_LEFT:
            artos->dictation.pen_x -= cmd->move_amount;
            snprintf(feedback, sizeof(feedback), "Moved left to (%.0f, %.0f)",
                     artos->dictation.pen_x, artos->dictation.pen_y);
            break;

        case ARTOS_DICT_CMD_GO_RIGHT:
            artos->dictation.pen_x += cmd->move_amount;
            snprintf(feedback, sizeof(feedback), "Moved right to (%.0f, %.0f)",
                     artos->dictation.pen_x, artos->dictation.pen_y);
            break;

        case ARTOS_DICT_CMD_GO_UP:
            artos->dictation.pen_y -= cmd->move_amount;
            snprintf(feedback, sizeof(feedback), "Moved up to (%.0f, %.0f)",
                     artos->dictation.pen_x, artos->dictation.pen_y);
            break;

        case ARTOS_DICT_CMD_GO_DOWN:
            artos->dictation.pen_y += cmd->move_amount;
            snprintf(feedback, sizeof(feedback), "Moved down to (%.0f, %.0f)",
                     artos->dictation.pen_x, artos->dictation.pen_y);
            break;

        case ARTOS_DICT_CMD_START_DRAWING:
            artos->dictation.continuous_draw = 1;
            snprintf(feedback, sizeof(feedback), "Pen down - drawing enabled");
            break;

        case ARTOS_DICT_CMD_STOP_DRAWING:
            artos->dictation.continuous_draw = 0;
            snprintf(feedback, sizeof(feedback), "Pen up - drawing stopped");
            break;

        default:
            snprintf(feedback, sizeof(feedback), "Unknown command");
            break;
    }

    /* Restore color if it was overridden */
    if (cmd->has_color && cmd->command != ARTOS_DICT_CMD_SET_COLOR) {
        artos->foreground_color = saved_color;
    }

    artos_dictation_show_feedback(artos, feedback);
    return 1;
}

/* Process text input as voice command */
void artos_dictation_process_text(phantom_artos_t *artos, const char *text) {
    if (!artos || !text || !artos->dictation.enabled) return;

    artos_dictation_parsed_t cmd;
    if (artos_dictation_parse(text, &cmd)) {
        artos_dictation_execute(artos, &cmd);
        artos_dictation_add_history(artos, text, cmd.command, 1);
    } else {
        artos_dictation_show_feedback(artos, cmd.error);
        artos_dictation_add_history(artos, text, ARTOS_DICT_CMD_NONE, 0);
    }
}

/* Show feedback message */
static gboolean hide_feedback(gpointer data) {
    phantom_artos_t *artos = (phantom_artos_t *)data;
    if (!artos) return FALSE;

    if (artos->dictation_feedback) {
        gtk_widget_hide(artos->dictation_feedback);
    }
    artos->dictation.feedback_timer = 0;
    return FALSE;
}

void artos_dictation_show_feedback(phantom_artos_t *artos, const char *message) {
    if (!artos) return;

    strncpy(artos->dictation.last_feedback, message, sizeof(artos->dictation.last_feedback) - 1);
    artos->dictation.last_feedback[sizeof(artos->dictation.last_feedback) - 1] = '\0';

    if (artos->dictation_feedback) {
        gtk_label_set_text(GTK_LABEL(artos->dictation_feedback), message);
        gtk_widget_show(artos->dictation_feedback);

        /* Auto-hide after 3 seconds */
        if (artos->dictation.feedback_timer) {
            g_source_remove(artos->dictation.feedback_timer);
        }
        artos->dictation.feedback_timer = g_timeout_add(3000, hide_feedback, artos);
    }
}

/* Add to command history */
void artos_dictation_add_history(phantom_artos_t *artos, const char *command,
                                  artos_dictation_cmd_t type, int executed) {
    if (!artos) return;

    int idx = artos->dictation.history_index;
    strncpy(artos->dictation.history[idx].command, command, ARTOS_DICTATION_MAX_CMD - 1);
    artos->dictation.history[idx].command[ARTOS_DICTATION_MAX_CMD - 1] = '\0';
    artos->dictation.history[idx].type = type;
    artos->dictation.history[idx].timestamp = time(NULL);
    artos->dictation.history[idx].executed = executed;

    artos->dictation.history_index = (idx + 1) % ARTOS_DICTATION_HISTORY;
    if (artos->dictation.history_count < ARTOS_DICTATION_HISTORY) {
        artos->dictation.history_count++;
    }

    /* Update history list store */
    if (artos->dictation_history_store) {
        GtkTreeIter iter;
        gtk_list_store_prepend(artos->dictation_history_store, &iter);
        gtk_list_store_set(artos->dictation_history_store, &iter,
                           0, executed ? "✓" : "✗",
                           1, command,
                           -1);

        /* Limit history display to 50 items */
        int count = gtk_tree_model_iter_n_children(
            GTK_TREE_MODEL(artos->dictation_history_store), NULL);
        while (count > 50) {
            GtkTreeIter last;
            if (gtk_tree_model_iter_nth_child(
                    GTK_TREE_MODEL(artos->dictation_history_store), &last, NULL, count - 1)) {
                gtk_list_store_remove(artos->dictation_history_store, &last);
            }
            count--;
        }
    }
}

/* Dictation entry callback */
static void on_dictation_entry_activate(GtkEntry *entry, phantom_artos_t *artos) {
    const char *text = gtk_entry_get_text(entry);
    if (text && strlen(text) > 0) {
        artos_dictation_process_text(artos, text);
        gtk_entry_set_text(entry, "");
    }
}

/* Dictation toggle callback */
static void on_dictation_toggle(GtkToggleButton *button, phantom_artos_t *artos) {
    int enabled = gtk_toggle_button_get_active(button);
    artos_dictation_enable(artos, enabled);

    if (artos->dictation_entry) {
        gtk_widget_set_sensitive(artos->dictation_entry, enabled);
    }

    if (artos->dictation_listen_btn) {
        gtk_widget_set_sensitive(artos->dictation_listen_btn, enabled);
    }

    /* Stop listening if disabled */
    if (!enabled && artos_voice_is_listening(artos)) {
        artos_voice_stop_listening(artos);
    }
}

/* Create dictation panel */
GtkWidget *artos_create_dictation_panel(phantom_artos_t *artos) {
    GtkWidget *frame = gtk_frame_new("🎤 Dictation Drawing");
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(box), 5);

    /* Initialize dictation system */
    artos_dictation_init(artos);

    /* Enable toggle */
    artos->dictation_toggle = gtk_toggle_button_new_with_label("Enable Dictation");
    g_signal_connect(artos->dictation_toggle, "toggled", G_CALLBACK(on_dictation_toggle), artos);
    gtk_box_pack_start(GTK_BOX(box), artos->dictation_toggle, FALSE, FALSE, 0);

    /* Voice input section */
    GtkWidget *voice_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);

    /* Listen button - large and prominent */
    artos->dictation_listen_btn = gtk_button_new_with_label("🎤 Listen");
    gtk_widget_set_size_request(artos->dictation_listen_btn, 100, 40);
    gtk_widget_set_sensitive(artos->dictation_listen_btn, FALSE);
    g_signal_connect(artos->dictation_listen_btn, "clicked",
                     G_CALLBACK(on_listen_button_clicked), artos);

    /* Add CSS styling for the listen button */
    GtkCssProvider *btn_css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(btn_css,
        "button { font-size: 14px; font-weight: bold; }"
        "button.recording { background: #cc3333; color: white; }",
        -1, NULL);
    gtk_style_context_add_provider(gtk_widget_get_style_context(artos->dictation_listen_btn),
                                    GTK_STYLE_PROVIDER(btn_css),
                                    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(btn_css);

    gtk_box_pack_start(GTK_BOX(voice_box), artos->dictation_listen_btn, FALSE, FALSE, 0);

    /* Audio level indicator */
    artos->dictation_level_bar = gtk_level_bar_new_for_interval(0.0, 1.0);
    gtk_level_bar_set_value(GTK_LEVEL_BAR(artos->dictation_level_bar), 0.0);
    gtk_widget_set_size_request(artos->dictation_level_bar, -1, 20);
    gtk_orientable_set_orientation(GTK_ORIENTABLE(artos->dictation_level_bar),
                                   GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(voice_box), artos->dictation_level_bar, TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(box), voice_box, FALSE, FALSE, 5);

    /* Separator */
    gtk_box_pack_start(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL),
                       FALSE, FALSE, 5);

    /* Text input label */
    GtkWidget *text_label = gtk_label_new("Or type command:");
    gtk_widget_set_halign(text_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), text_label, FALSE, FALSE, 0);

    /* Command entry */
    artos->dictation_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(artos->dictation_entry),
        "draw a red circle");
    gtk_widget_set_sensitive(artos->dictation_entry, FALSE);
    g_signal_connect(artos->dictation_entry, "activate",
                     G_CALLBACK(on_dictation_entry_activate), artos);
    gtk_box_pack_start(GTK_BOX(box), artos->dictation_entry, FALSE, FALSE, 0);

    /* Feedback label */
    artos->dictation_feedback = gtk_label_new("");
    gtk_label_set_line_wrap(GTK_LABEL(artos->dictation_feedback), TRUE);
    gtk_widget_set_halign(artos->dictation_feedback, GTK_ALIGN_START);
    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css,
        "label { background: #2a5298; color: white; padding: 5px; border-radius: 3px; }",
        -1, NULL);
    gtk_style_context_add_provider(gtk_widget_get_style_context(artos->dictation_feedback),
                                    GTK_STYLE_PROVIDER(css),
                                    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);
    gtk_widget_set_no_show_all(artos->dictation_feedback, TRUE);
    gtk_box_pack_start(GTK_BOX(box), artos->dictation_feedback, FALSE, FALSE, 0);

    /* Help text */
    GtkWidget *help_expander = gtk_expander_new("Commands Help");
    GtkWidget *help_label = gtk_label_new(
        "Shape commands:\n"
        "  • draw a [color] circle/square/triangle/star/heart\n"
        "  • draw a filled red rectangle\n"
        "  • draw a 6-pointed star\n"
        "  • draw an arrow/spiral/line\n\n"
        "Color: set color to blue\n"
        "Size: brush size 20, bigger, smaller\n"
        "Tools: use pencil/brush/eraser\n"
        "Actions: undo, redo, new layer\n"
        "Position: at center/top/bottom/left/right\n"
        "Movement: go left 100, move up");
    gtk_label_set_line_wrap(GTK_LABEL(help_label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(help_label), 0);
    gtk_container_add(GTK_CONTAINER(help_expander), help_label);
    gtk_box_pack_start(GTK_BOX(box), help_expander, FALSE, FALSE, 5);

    /* History */
    GtkWidget *history_label = gtk_label_new("History:");
    gtk_widget_set_halign(history_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), history_label, FALSE, FALSE, 0);

    artos->dictation_history_store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
    artos->dictation_history_view = gtk_tree_view_new_with_model(
        GTK_TREE_MODEL(artos->dictation_history_store));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(artos->dictation_history_view), FALSE);

    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *col1 = gtk_tree_view_column_new_with_attributes(
        "", renderer, "text", 0, NULL);
    gtk_tree_view_column_set_min_width(col1, 20);
    gtk_tree_view_append_column(GTK_TREE_VIEW(artos->dictation_history_view), col1);

    GtkTreeViewColumn *col2 = gtk_tree_view_column_new_with_attributes(
        "Command", renderer, "text", 1, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(artos->dictation_history_view), col2);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scroll, -1, 100);
    gtk_container_add(GTK_CONTAINER(scroll), artos->dictation_history_view);
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);

    gtk_container_add(GTK_CONTAINER(frame), box);
    artos->dictation_panel = frame;

    return frame;
}

/* ==============================================================================
 * Voice Recognition System
 * Uses system speech recognition or fallback to simple command matching
 * ============================================================================== */

/* Voice recognition subprocess data */
typedef struct {
    phantom_artos_t *artos;
    GPid child_pid;
    int stdout_fd;
    GIOChannel *stdout_channel;
    guint stdout_watch;
    char buffer[4096];
    int buffer_pos;
} voice_recognition_data_t;

static voice_recognition_data_t *voice_data = NULL;

/* Forward declaration */
static void on_listen_button_clicked(GtkButton *button, phantom_artos_t *artos);
static gboolean on_voice_stdout(GIOChannel *channel, GIOCondition cond, gpointer data);
static void on_voice_child_exit(GPid pid, gint status, gpointer data);

/* Initialize voice recognition system */
int artos_voice_init(phantom_artos_t *artos) {
    if (artos->dictation.voice_initialized) {
        return 1;  /* Already initialized */
    }

    /* Check if speech recognition tools are available */
    /* We'll use a simple approach: record audio with arecord,
       and use a built-in command matcher or external STT service */

    artos->dictation.voice_initialized = 1;
    artos->dictation.voice_recording = 0;
    artos->dictation.audio_level = 0.0;

    return 1;
}

/* Cleanup voice recognition */
void artos_voice_cleanup(phantom_artos_t *artos) {
    if (artos->dictation.voice_recording) {
        artos_voice_stop_listening(artos);
    }

    if (artos->dictation.level_update_timer) {
        g_source_remove(artos->dictation.level_update_timer);
        artos->dictation.level_update_timer = 0;
    }

    artos->dictation.voice_initialized = 0;
}

/* Process recognized speech text */
static void voice_process_result(phantom_artos_t *artos, const char *text) {
    if (!text || strlen(text) == 0) return;

    /* Show what was heard */
    char feedback[512];
    snprintf(feedback, sizeof(feedback), "Heard: \"%.480s\"", text);
    artos_dictation_show_feedback(artos, feedback);

    /* Process the command */
    artos_dictation_process_text(artos, text);
}

/* Update audio level display */
static gboolean update_audio_level(gpointer data) {
    phantom_artos_t *artos = (phantom_artos_t *)data;

    if (artos->dictation_level_bar && artos->dictation.voice_recording) {
        /* Simulate audio level animation while recording */
        static double phase = 0.0;
        phase += 0.3;
        double level = 0.3 + 0.4 * sin(phase) + 0.3 * ((double)rand() / RAND_MAX);
        gtk_level_bar_set_value(GTK_LEVEL_BAR(artos->dictation_level_bar), level);
    }

    return artos->dictation.voice_recording ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
}

/* Handle stdout data from voice recognition process */
static gboolean on_voice_stdout(GIOChannel *channel, GIOCondition cond, gpointer data) {
    voice_recognition_data_t *vdata = (voice_recognition_data_t *)data;

    if (!vdata || !vdata->artos) {
        return G_SOURCE_REMOVE;
    }

    phantom_artos_t *artos = vdata->artos;

    if (cond & G_IO_IN) {
        gchar buf[256];
        gsize bytes_read;
        GError *error = NULL;

        GIOStatus status = g_io_channel_read_chars(channel, buf, sizeof(buf) - 1,
                                                    &bytes_read, &error);

        if (status == G_IO_STATUS_NORMAL && bytes_read > 0) {
            buf[bytes_read] = '\0';

            /* Append to buffer */
            int space = (int)(sizeof(vdata->buffer) - vdata->buffer_pos - 1);
            if (space > 0) {
                int copy = ((int)bytes_read < space) ? (int)bytes_read : space;
                memcpy(vdata->buffer + vdata->buffer_pos, buf, copy);
                vdata->buffer_pos += copy;
                vdata->buffer[vdata->buffer_pos] = '\0';
            }

            /* Look for complete lines */
            char *newline;
            while ((newline = strchr(vdata->buffer, '\n')) != NULL) {
                *newline = '\0';

                /* Process this line as recognized text */
                char *line = vdata->buffer;
                while (*line && (*line == ' ' || *line == '\t')) line++;

                if (strlen(line) > 0) {
                    voice_process_result(artos, line);
                }

                /* Shift buffer */
                int remaining = vdata->buffer_pos - (int)(newline - vdata->buffer + 1);
                if (remaining > 0) {
                    memmove(vdata->buffer, newline + 1, remaining);
                }
                vdata->buffer_pos = remaining;
                vdata->buffer[vdata->buffer_pos] = '\0';
            }
        }

        if (error) {
            g_error_free(error);
        }
    }

    if (cond & (G_IO_HUP | G_IO_ERR)) {
        /* Mark watch as removed so child_exit doesn't try to remove it again */
        vdata->stdout_watch = 0;
        return G_SOURCE_REMOVE;
    }

    return G_SOURCE_CONTINUE;
}

/* Handle voice recognition process exit */
static void on_voice_child_exit(GPid pid, gint status, gpointer data) {
    voice_recognition_data_t *vdata = (voice_recognition_data_t *)data;
    (void)status;

    if (!vdata) return;

    phantom_artos_t *artos = vdata->artos;

    g_spawn_close_pid(pid);

    /* Remove the stdout watch if it's still active */
    if (vdata->stdout_watch > 0) {
        g_source_remove(vdata->stdout_watch);
        vdata->stdout_watch = 0;
    }

    /* Process any remaining buffer content */
    if (artos && vdata->buffer_pos > 0) {
        vdata->buffer[vdata->buffer_pos] = '\0';
        char *line = vdata->buffer;
        while (*line && (*line == ' ' || *line == '\t')) line++;
        if (strlen(line) > 0) {
            voice_process_result(artos, line);
        }
    }

    /* Cleanup IO channel */
    if (vdata->stdout_channel) {
        g_io_channel_shutdown(vdata->stdout_channel, FALSE, NULL);
        g_io_channel_unref(vdata->stdout_channel);
        vdata->stdout_channel = NULL;
    }

    /* Close file descriptor */
    if (vdata->stdout_fd > 0) {
        close(vdata->stdout_fd);
        vdata->stdout_fd = 0;
    }

    if (artos) {
        /* Stop the level update timer BEFORE marking recording as stopped */
        if (artos->dictation.level_update_timer) {
            g_source_remove(artos->dictation.level_update_timer);
            artos->dictation.level_update_timer = 0;
        }

        /* Stop the voice timeout timer if it's still running */
        if (artos->dictation.voice_timeout) {
            g_source_remove(artos->dictation.voice_timeout);
            artos->dictation.voice_timeout = 0;
        }

        artos->dictation.voice_recording = 0;

        /* Update UI */
        if (artos->dictation_listen_btn) {
            gtk_button_set_label(GTK_BUTTON(artos->dictation_listen_btn), "🎤 Listen");

            /* Reset button style */
            GtkStyleContext *ctx = gtk_widget_get_style_context(artos->dictation_listen_btn);
            gtk_style_context_remove_class(ctx, "recording");
        }

        if (artos->dictation_level_bar) {
            gtk_level_bar_set_value(GTK_LEVEL_BAR(artos->dictation_level_bar), 0.0);
        }

        artos_dictation_show_feedback(artos, "Listening stopped");
    }

    free(vdata);
    voice_data = NULL;
}

/* Silence timeout - auto-stop after period of silence */
static gboolean on_voice_timeout(gpointer data) {
    phantom_artos_t *artos = (phantom_artos_t *)data;

    if (artos->dictation.voice_recording) {
        artos_voice_stop_listening(artos);
        artos_dictation_show_feedback(artos, "Stopped (timeout)");
    }

    artos->dictation.voice_timeout = 0;
    return G_SOURCE_REMOVE;
}

/* Start voice listening */
void artos_voice_start_listening(phantom_artos_t *artos) {
    if (!artos->dictation.voice_initialized) {
        artos_voice_init(artos);
    }

    if (artos->dictation.voice_recording) {
        return;  /* Already recording */
    }

    voice_recognition_data_t *vdata = calloc(1, sizeof(voice_recognition_data_t));
    if (!vdata) {
        artos_dictation_show_feedback(artos, "Error: Memory allocation failed");
        return;
    }
    vdata->artos = artos;
    voice_data = vdata;

    GError *error = NULL;
    int stdout_fd;
    GPid child_pid;

    /* Build the voice recognition command.
       Try our Python helper script with Vosk first, then fallback options */
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
        "if [ -f ~/.phantomos-venv/bin/python ]; then "
        "  ~/.phantomos-venv/bin/python %s/voice_recognize.py --duration 5 2>/dev/null; "
        "elif command -v arecord >/dev/null && command -v vosk-transcriber >/dev/null; then "
        "  timeout 5 arecord -q -f S16_LE -r 16000 -c 1 -t wav - 2>/dev/null | vosk-transcriber 2>/dev/null; "
        "else "
        "  echo 'Voice recognition not available. Type commands instead.'; "
        "fi",
        "/opt/phantomos");

    const char *argv[] = {
        "/bin/sh", "-c", cmd, NULL
    };

    gboolean spawned = g_spawn_async_with_pipes(
        NULL,                   /* Working directory */
        (gchar **)argv,
        NULL,                   /* Environment */
        G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH,
        NULL, NULL,             /* Child setup */
        &child_pid,
        NULL,                   /* stdin */
        &stdout_fd,             /* stdout */
        NULL,                   /* stderr */
        &error
    );

    if (!spawned) {
        artos_dictation_show_feedback(artos,
            "Voice input unavailable. Type commands instead.");
        if (error) {
            g_error_free(error);
        }
        free(vdata);
        voice_data = NULL;
        return;
    }

    vdata->child_pid = child_pid;
    vdata->stdout_fd = stdout_fd;

    /* Set up stdout monitoring */
    vdata->stdout_channel = g_io_channel_unix_new(stdout_fd);
    g_io_channel_set_flags(vdata->stdout_channel, G_IO_FLAG_NONBLOCK, NULL);
    vdata->stdout_watch = g_io_add_watch(vdata->stdout_channel,
                                          G_IO_IN | G_IO_HUP | G_IO_ERR,
                                          on_voice_stdout, vdata);

    /* Watch for child exit */
    g_child_watch_add(child_pid, on_voice_child_exit, vdata);

    artos->dictation.voice_recording = 1;

    /* Update UI */
    if (artos->dictation_listen_btn) {
        gtk_button_set_label(GTK_BUTTON(artos->dictation_listen_btn), "🔴 Stop");

        /* Add recording style */
        GtkStyleContext *ctx = gtk_widget_get_style_context(artos->dictation_listen_btn);
        gtk_style_context_add_class(ctx, "recording");
    }

    artos_dictation_show_feedback(artos, "🎤 Listening... Speak now!");

    /* Start audio level animation */
    artos->dictation.level_update_timer = g_timeout_add(100, update_audio_level, artos);

    /* Set timeout for auto-stop (10 seconds) */
    artos->dictation.voice_timeout = g_timeout_add(10000, on_voice_timeout, artos);
}

/* Stop voice listening */
void artos_voice_stop_listening(phantom_artos_t *artos) {
    if (!artos->dictation.voice_recording) {
        return;
    }

    /* Cancel timeout */
    if (artos->dictation.voice_timeout) {
        g_source_remove(artos->dictation.voice_timeout);
        artos->dictation.voice_timeout = 0;
    }

    /* Kill the recording process */
    if (voice_data && voice_data->child_pid > 0) {
        kill(voice_data->child_pid, SIGTERM);
    }

    /* The child exit handler will clean up */
}

/* Check if currently listening */
int artos_voice_is_listening(phantom_artos_t *artos) {
    return artos->dictation.voice_recording;
}

/* Listen button callback */
static void on_listen_button_clicked(GtkButton *button, phantom_artos_t *artos) {
    (void)button;

    if (!artos->dictation.enabled) {
        artos_dictation_show_feedback(artos, "Enable dictation first!");
        return;
    }

    if (artos_voice_is_listening(artos)) {
        artos_voice_stop_listening(artos);
    } else {
        artos_voice_start_listening(artos);
    }
}

/* ==============================================================================
 * Face Tracking Drawing System
 * "Draw with your face" - use head/nose movements to control the brush
 * ============================================================================== */

/* Forward declarations */
static void on_facetrack_toggle(GtkToggleButton *button, phantom_artos_t *artos);
static void on_facetrack_start_clicked(GtkButton *button, phantom_artos_t *artos);
static void on_facetrack_calibrate_clicked(GtkButton *button, phantom_artos_t *artos);
static void on_facetrack_mode_changed(GtkComboBox *combo, phantom_artos_t *artos);
static gboolean on_facetrack_stdout(GIOChannel *channel, GIOCondition cond, gpointer data);
static void on_facetrack_child_exit(GPid pid, gint status, gpointer data);

/* Face tracking data for subprocess communication */
typedef struct {
    phantom_artos_t *artos;
    GPid child_pid;
    int stdout_fd;
    GIOChannel *stdout_channel;
    guint stdout_watch;
    char buffer[4096];
    int buffer_pos;
} facetrack_data_t;

static facetrack_data_t *facetrack_data = NULL;

/* Initialize face tracking */
int artos_facetrack_init(phantom_artos_t *artos) {
    if (!artos) return 0;

    /* Set defaults */
    artos->facetrack.enabled = 0;
    artos->facetrack.tracking = 0;
    artos->facetrack.drawing = 0;
    artos->facetrack.mode = ARTOS_FACE_MODE_NOSE;

    artos->facetrack.face_x = 0.5;
    artos->facetrack.face_y = 0.5;
    artos->facetrack.canvas_x = 0;
    artos->facetrack.canvas_y = 0;
    artos->facetrack.last_canvas_x = 0;
    artos->facetrack.last_canvas_y = 0;

    /* Default tracking zone (full canvas) */
    artos->facetrack.zone_x1 = 0.0;
    artos->facetrack.zone_y1 = 0.0;
    artos->facetrack.zone_x2 = 1.0;
    artos->facetrack.zone_y2 = 1.0;

    artos->facetrack.smoothing = 0.3;
    artos->facetrack.sensitivity = 1.5;
    artos->facetrack.smooth_x = 0.5;
    artos->facetrack.smooth_y = 0.5;

    artos->facetrack.last_gesture = ARTOS_FACE_GESTURE_NONE;
    artos->facetrack.gesture_cooldown = 0;

    /* Default gesture actions */
    artos->facetrack.blink_to_draw = 1;      /* Blink toggles pen */
    artos->facetrack.mouth_to_draw = 0;
    artos->facetrack.smile_to_undo = 0;

    artos->facetrack.child_pid = 0;
    artos->facetrack.stdout_fd = 0;
    artos->facetrack.stdout_channel = NULL;
    artos->facetrack.stdout_watch = 0;
    artos->facetrack.update_timer = 0;

    artos->facetrack.calibrating = 0;
    artos->facetrack.calibration_step = 0;

    artos->facetrack.frames_processed = 0;
    artos->facetrack.fps = 0.0;
    artos->facetrack.start_time = time(NULL);

    artos->facetrack.show_preview = 0;
    artos->facetrack.preview_data = NULL;
    artos->facetrack.preview_width = 0;
    artos->facetrack.preview_height = 0;

    return 1;
}

/* Cleanup face tracking */
void artos_facetrack_cleanup(phantom_artos_t *artos) {
    if (!artos) return;

    if (artos->facetrack.tracking) {
        artos_facetrack_stop(artos);
    }

    if (artos->facetrack.update_timer) {
        g_source_remove(artos->facetrack.update_timer);
        artos->facetrack.update_timer = 0;
    }

    if (artos->facetrack.preview_data) {
        free(artos->facetrack.preview_data);
        artos->facetrack.preview_data = NULL;
    }
}

/* Enable/disable face tracking */
void artos_facetrack_enable(phantom_artos_t *artos, int enable) {
    if (!artos) return;

    artos->facetrack.enabled = enable;

    if (!enable && artos->facetrack.tracking) {
        artos_facetrack_stop(artos);
    }
}

/* Map face position to canvas coordinates */
static void facetrack_map_to_canvas(phantom_artos_t *artos) {
    if (!artos || !artos->document) return;

    /* Get canvas dimensions */
    int canvas_w = artos->document->width;
    int canvas_h = artos->document->height;

    /* Map normalized face position to canvas position */
    double norm_x = (artos->facetrack.face_x - artos->facetrack.zone_x1) /
                    (artos->facetrack.zone_x2 - artos->facetrack.zone_x1);
    double norm_y = (artos->facetrack.face_y - artos->facetrack.zone_y1) /
                    (artos->facetrack.zone_y2 - artos->facetrack.zone_y1);

    /* Clamp to 0-1 range */
    if (norm_x < 0) norm_x = 0;
    if (norm_x > 1) norm_x = 1;
    if (norm_y < 0) norm_y = 0;
    if (norm_y > 1) norm_y = 1;

    /* Apply sensitivity */
    double center_x = 0.5, center_y = 0.5;
    norm_x = center_x + (norm_x - center_x) * artos->facetrack.sensitivity;
    norm_y = center_y + (norm_y - center_y) * artos->facetrack.sensitivity;

    /* Clamp again */
    if (norm_x < 0) norm_x = 0;
    if (norm_x > 1) norm_x = 1;
    if (norm_y < 0) norm_y = 0;
    if (norm_y > 1) norm_y = 1;

    /* Map to canvas coordinates */
    artos->facetrack.canvas_x = norm_x * canvas_w;
    artos->facetrack.canvas_y = norm_y * canvas_h;
}

/* Process face tracking data and draw */
static void facetrack_process_position(phantom_artos_t *artos) {
    if (!artos || !artos->facetrack.enabled || !artos->facetrack.tracking) return;

    facetrack_map_to_canvas(artos);

    /* Update position label */
    if (artos->facetrack_pos_label) {
        char pos_text[64];
        snprintf(pos_text, sizeof(pos_text), "Position: %.0f, %.0f",
                 artos->facetrack.canvas_x, artos->facetrack.canvas_y);
        gtk_label_set_text(GTK_LABEL(artos->facetrack_pos_label), pos_text);
    }

    /* Refresh face tracking preview to show current position */
    if (artos->facetrack_preview_area) {
        gtk_widget_queue_draw(artos->facetrack_preview_area);
    }

    /* Draw if pen is down */
    if (artos->facetrack.drawing && artos->document) {
        double dx = artos->facetrack.canvas_x - artos->facetrack.last_canvas_x;
        double dy = artos->facetrack.canvas_y - artos->facetrack.last_canvas_y;
        double dist = sqrt(dx * dx + dy * dy);

        /* Only draw if moved enough */
        if (dist > 2.0) {
            artos_draw_line(artos,
                            artos->facetrack.last_canvas_x,
                            artos->facetrack.last_canvas_y,
                            artos->facetrack.canvas_x,
                            artos->facetrack.canvas_y);

            /* Refresh canvas immediately to show real-time drawing */
            if (artos->canvas_area) {
                gtk_widget_queue_draw(artos->canvas_area);
            }
        }
    }

    artos->facetrack.last_canvas_x = artos->facetrack.canvas_x;
    artos->facetrack.last_canvas_y = artos->facetrack.canvas_y;
}

/* Process gesture from face tracking */
static void facetrack_process_gesture(phantom_artos_t *artos, const char *gesture_str) {
    if (!artos || !gesture_str) return;

    artos_face_gesture_t gesture = ARTOS_FACE_GESTURE_NONE;

    if (strcmp(gesture_str, "blink_both") == 0) {
        gesture = ARTOS_FACE_GESTURE_BLINK_BOTH;
    } else if (strcmp(gesture_str, "blink_left") == 0) {
        gesture = ARTOS_FACE_GESTURE_BLINK_LEFT;
    } else if (strcmp(gesture_str, "blink_right") == 0) {
        gesture = ARTOS_FACE_GESTURE_BLINK_RIGHT;
    } else if (strcmp(gesture_str, "mouth_open") == 0) {
        gesture = ARTOS_FACE_GESTURE_MOUTH_OPEN;
    } else if (strcmp(gesture_str, "smile") == 0) {
        gesture = ARTOS_FACE_GESTURE_SMILE;
    }

    if (gesture != ARTOS_FACE_GESTURE_NONE) {
        artos->facetrack.last_gesture = gesture;

        /* Update gesture label */
        if (artos->facetrack_gesture_label) {
            char gesture_text[64];
            snprintf(gesture_text, sizeof(gesture_text), "Gesture: %s", gesture_str);
            gtk_label_set_text(GTK_LABEL(artos->facetrack_gesture_label), gesture_text);
        }

        /* Execute gesture actions */
        if (artos->facetrack.blink_to_draw &&
            (gesture == ARTOS_FACE_GESTURE_BLINK_BOTH ||
             gesture == ARTOS_FACE_GESTURE_BLINK_LEFT ||
             gesture == ARTOS_FACE_GESTURE_BLINK_RIGHT)) {
            artos_facetrack_toggle_draw(artos);
        }

        if (artos->facetrack.mouth_to_draw &&
            gesture == ARTOS_FACE_GESTURE_MOUTH_OPEN) {
            artos_facetrack_toggle_draw(artos);
        }

        if (artos->facetrack.smile_to_undo &&
            gesture == ARTOS_FACE_GESTURE_SMILE) {
            artos_undo(artos);
        }
    }
}

/* Handle stdout data from face tracking process */
static gboolean on_facetrack_stdout(GIOChannel *channel, GIOCondition cond, gpointer data) {
    facetrack_data_t *ftdata = (facetrack_data_t *)data;

    if (!ftdata || !ftdata->artos) {
        return G_SOURCE_REMOVE;
    }

    phantom_artos_t *artos = ftdata->artos;

    if (cond & G_IO_IN) {
        gchar buf[512];
        gsize bytes_read;
        GError *error = NULL;

        GIOStatus status = g_io_channel_read_chars(channel, buf, sizeof(buf) - 1,
                                                    &bytes_read, &error);

        if (status == G_IO_STATUS_NORMAL && bytes_read > 0) {
            buf[bytes_read] = '\0';

            /* Append to buffer */
            int space = (int)(sizeof(ftdata->buffer) - ftdata->buffer_pos - 1);
            if (space > 0) {
                int copy = ((int)bytes_read < space) ? (int)bytes_read : space;
                memcpy(ftdata->buffer + ftdata->buffer_pos, buf, copy);
                ftdata->buffer_pos += copy;
                ftdata->buffer[ftdata->buffer_pos] = '\0';
            }

            /* Look for complete JSON lines */
            char *newline;
            while ((newline = strchr(ftdata->buffer, '\n')) != NULL) {
                *newline = '\0';

                char *line = ftdata->buffer;
                while (*line && (*line == ' ' || *line == '\t')) line++;

                /* Parse JSON */
                if (strlen(line) > 0 && line[0] == '{') {
                    /* Check for error messages first */
                    char *error_pos = strstr(line, "\"error\":");
                    if (error_pos) {
                        error_pos += 8;
                        while (*error_pos && (*error_pos == ' ' || *error_pos == '"')) error_pos++;
                        char error_msg[256] = {0};
                        int ei = 0;
                        while (*error_pos && *error_pos != '"' && ei < 255) {
                            error_msg[ei++] = *error_pos++;
                        }
                        error_msg[ei] = '\0';

                        if (artos->facetrack_status_label) {
                            char status_text[300];
                            snprintf(status_text, sizeof(status_text), "Error: %s", error_msg);
                            gtk_label_set_text(GTK_LABEL(artos->facetrack_status_label), status_text);
                        }

                        /* Shift buffer and continue */
                        int remaining = ftdata->buffer_pos - (int)(newline - ftdata->buffer + 1);
                        if (remaining > 0) {
                            memmove(ftdata->buffer, newline + 1, remaining);
                        }
                        ftdata->buffer_pos = remaining;
                        ftdata->buffer[ftdata->buffer_pos] = '\0';
                        continue;
                    }

                    /* Check for status messages */
                    char *status_pos = strstr(line, "\"status\":");
                    if (status_pos) {
                        status_pos += 9;
                        while (*status_pos && (*status_pos == ' ' || *status_pos == '"')) status_pos++;
                        char status_msg[64] = {0};
                        int si = 0;
                        while (*status_pos && *status_pos != '"' && *status_pos != ',' && si < 63) {
                            status_msg[si++] = *status_pos++;
                        }
                        status_msg[si] = '\0';

                        if (artos->facetrack_status_label) {
                            char status_text[128];
                            if (strcmp(status_msg, "started") == 0) {
                                snprintf(status_text, sizeof(status_text), "Status: Tracking active");
                            } else if (strcmp(status_msg, "downloading_model") == 0) {
                                snprintf(status_text, sizeof(status_text), "Status: Downloading AI model...");
                            } else if (strcmp(status_msg, "loading_camera_module") == 0) {
                                snprintf(status_text, sizeof(status_text), "Status: Loading camera driver...");
                            } else {
                                snprintf(status_text, sizeof(status_text), "Status: %s", status_msg);
                            }
                            gtk_label_set_text(GTK_LABEL(artos->facetrack_status_label), status_text);
                        }

                        /* Shift buffer and continue */
                        int remaining = ftdata->buffer_pos - (int)(newline - ftdata->buffer + 1);
                        if (remaining > 0) {
                            memmove(ftdata->buffer, newline + 1, remaining);
                        }
                        ftdata->buffer_pos = remaining;
                        ftdata->buffer[ftdata->buffer_pos] = '\0';
                        continue;
                    }

                    /* Simple JSON parsing for: {"x": 0.5, "y": 0.5, "gesture": "none", ...} */
                    double x = 0.5, y = 0.5;
                    double fps = 0.0;
                    char gesture[32] = "none";
                    int face_detected = 0;

                    /* Parse x */
                    char *x_pos = strstr(line, "\"x\":");
                    if (x_pos) {
                        x = atof(x_pos + 4);
                    }

                    /* Parse y */
                    char *y_pos = strstr(line, "\"y\":");
                    if (y_pos) {
                        y = atof(y_pos + 4);
                    }

                    /* Parse fps */
                    char *fps_pos = strstr(line, "\"fps\":");
                    if (fps_pos) {
                        fps = atof(fps_pos + 6);
                    }

                    /* Parse face_detected */
                    if (strstr(line, "\"face_detected\": true") ||
                        strstr(line, "\"face_detected\":true")) {
                        face_detected = 1;
                    }

                    /* Parse gesture */
                    char *gest_pos = strstr(line, "\"gesture\":");
                    if (gest_pos) {
                        gest_pos += 10;
                        while (*gest_pos && (*gest_pos == ' ' || *gest_pos == '"')) gest_pos++;
                        int gi = 0;
                        while (*gest_pos && *gest_pos != '"' && *gest_pos != ',' && gi < 31) {
                            gesture[gi++] = *gest_pos++;
                        }
                        gesture[gi] = '\0';
                    }

                    /* Update tracking data */
                    if (face_detected) {
                        artos->facetrack.face_x = x;
                        artos->facetrack.face_y = y;
                        artos->facetrack.fps = fps;
                        artos->facetrack.frames_processed++;

                        /* Update FPS label */
                        if (artos->facetrack_fps_label) {
                            char fps_text[32];
                            snprintf(fps_text, sizeof(fps_text), "FPS: %.1f", fps);
                            gtk_label_set_text(GTK_LABEL(artos->facetrack_fps_label), fps_text);
                        }

                        /* Process position and draw */
                        facetrack_process_position(artos);

                        /* Process gestures */
                        if (strcmp(gesture, "none") != 0) {
                            facetrack_process_gesture(artos, gesture);
                        }
                    }
                }

                /* Shift buffer */
                int remaining = ftdata->buffer_pos - (int)(newline - ftdata->buffer + 1);
                if (remaining > 0) {
                    memmove(ftdata->buffer, newline + 1, remaining);
                }
                ftdata->buffer_pos = remaining;
                ftdata->buffer[ftdata->buffer_pos] = '\0';
            }
        }

        if (error) {
            g_error_free(error);
        }
    }

    if (cond & (G_IO_HUP | G_IO_ERR)) {
        ftdata->stdout_watch = 0;
        return G_SOURCE_REMOVE;
    }

    return G_SOURCE_CONTINUE;
}

/* Handle face tracking process exit */
static void on_facetrack_child_exit(GPid pid, gint status, gpointer data) {
    facetrack_data_t *ftdata = (facetrack_data_t *)data;
    (void)status;

    if (!ftdata) return;

    phantom_artos_t *artos = ftdata->artos;

    g_spawn_close_pid(pid);

    /* Remove stdout watch if still active */
    if (ftdata->stdout_watch > 0) {
        g_source_remove(ftdata->stdout_watch);
        ftdata->stdout_watch = 0;
    }

    /* Cleanup IO channel */
    if (ftdata->stdout_channel) {
        g_io_channel_shutdown(ftdata->stdout_channel, FALSE, NULL);
        g_io_channel_unref(ftdata->stdout_channel);
        ftdata->stdout_channel = NULL;
    }

    /* Close file descriptor */
    if (ftdata->stdout_fd > 0) {
        close(ftdata->stdout_fd);
        ftdata->stdout_fd = 0;
    }

    if (artos) {
        artos->facetrack.tracking = 0;
        artos->facetrack.drawing = 0;

        /* Update UI */
        if (artos->facetrack_start_btn) {
            gtk_button_set_label(GTK_BUTTON(artos->facetrack_start_btn), "▶ Start Tracking");
        }

        if (artos->facetrack_status_label) {
            gtk_label_set_text(GTK_LABEL(artos->facetrack_status_label), "Status: Stopped");
        }
    }

    free(ftdata);
    facetrack_data = NULL;
}

/* Start face tracking */
void artos_facetrack_start(phantom_artos_t *artos) {
    if (!artos || !artos->facetrack.enabled || artos->facetrack.tracking) {
        return;
    }

    facetrack_data_t *ftdata = calloc(1, sizeof(facetrack_data_t));
    if (!ftdata) {
        return;
    }
    ftdata->artos = artos;
    facetrack_data = ftdata;

    GError *error = NULL;
    int stdout_fd;
    GPid child_pid;

    /* Build command based on mode */
    const char *mode_str = "nose";
    switch (artos->facetrack.mode) {
        case ARTOS_FACE_MODE_NOSE: mode_str = "nose"; break;
        case ARTOS_FACE_MODE_HEAD: mode_str = "head"; break;
        case ARTOS_FACE_MODE_EYES: mode_str = "eyes"; break;
        case ARTOS_FACE_MODE_MOUTH: mode_str = "mouth"; break;
    }

    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
        "if [ -f ~/.phantomos-venv/bin/python ]; then "
        "  ~/.phantomos-venv/bin/python %s/face_track.py --mode %s --smoothing %.2f; "
        "elif command -v python3 >/dev/null; then "
        "  python3 %s/face_track.py --mode %s --smoothing %.2f; "
        "else "
        "  echo '{\"error\": \"Python not found\"}'; "
        "fi",
        "/opt/phantomos", mode_str, artos->facetrack.smoothing,
        "/opt/phantomos", mode_str, artos->facetrack.smoothing);

    const char *argv[] = {
        "/bin/sh", "-c", cmd, NULL
    };

    gboolean spawned = g_spawn_async_with_pipes(
        NULL,
        (gchar **)argv,
        NULL,
        G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH,
        NULL, NULL,
        &child_pid,
        NULL,
        &stdout_fd,
        NULL,
        &error
    );

    if (!spawned) {
        if (artos->facetrack_status_label) {
            gtk_label_set_text(GTK_LABEL(artos->facetrack_status_label),
                              "Status: Failed to start");
        }
        if (error) {
            g_error_free(error);
        }
        free(ftdata);
        facetrack_data = NULL;
        return;
    }

    ftdata->child_pid = child_pid;
    ftdata->stdout_fd = stdout_fd;

    /* Set up stdout monitoring */
    ftdata->stdout_channel = g_io_channel_unix_new(stdout_fd);
    g_io_channel_set_flags(ftdata->stdout_channel, G_IO_FLAG_NONBLOCK, NULL);
    ftdata->stdout_watch = g_io_add_watch(ftdata->stdout_channel,
                                           G_IO_IN | G_IO_HUP | G_IO_ERR,
                                           on_facetrack_stdout, ftdata);

    /* Watch for child exit */
    g_child_watch_add(child_pid, on_facetrack_child_exit, ftdata);

    artos->facetrack.tracking = 1;
    artos->facetrack.frames_processed = 0;
    artos->facetrack.start_time = time(NULL);

    /* Update UI */
    if (artos->facetrack_start_btn) {
        gtk_button_set_label(GTK_BUTTON(artos->facetrack_start_btn), "⏹ Stop Tracking");
    }

    if (artos->facetrack_status_label) {
        gtk_label_set_text(GTK_LABEL(artos->facetrack_status_label), "Status: Tracking...");
    }

    /* Start preview refresh timer (30 fps) */
    if (artos->facetrack.update_timer == 0) {
        artos->facetrack.update_timer = g_timeout_add(33, facetrack_preview_refresh, artos);
    }
}

/* Stop face tracking */
void artos_facetrack_stop(phantom_artos_t *artos) {
    if (!artos || !artos->facetrack.tracking) {
        return;
    }

    /* Stop the preview refresh timer */
    if (artos->facetrack.update_timer > 0) {
        g_source_remove(artos->facetrack.update_timer);
        artos->facetrack.update_timer = 0;
    }

    /* Kill the tracking process */
    if (facetrack_data && facetrack_data->child_pid > 0) {
        kill(facetrack_data->child_pid, SIGTERM);
    }

    artos->facetrack.drawing = 0;

    /* Refresh preview to show stopped state */
    if (artos->facetrack_preview_area) {
        gtk_widget_queue_draw(artos->facetrack_preview_area);
    }
}

/* Check if tracking */
int artos_facetrack_is_tracking(phantom_artos_t *artos) {
    return artos ? artos->facetrack.tracking : 0;
}

/* Set tracking mode */
void artos_facetrack_set_mode(phantom_artos_t *artos, artos_face_mode_t mode) {
    if (!artos) return;

    artos->facetrack.mode = mode;

    /* Restart tracking if active - note: requires manual restart for mode change */
    if (artos->facetrack.tracking) {
        artos_facetrack_stop(artos);
        /* User needs to manually restart after mode change */
    }
}

/* Set sensitivity */
void artos_facetrack_set_sensitivity(phantom_artos_t *artos, double sensitivity) {
    if (!artos) return;
    artos->facetrack.sensitivity = sensitivity;
}

/* Set smoothing */
void artos_facetrack_set_smoothing(phantom_artos_t *artos, double smoothing) {
    if (!artos) return;
    artos->facetrack.smoothing = smoothing;
}

/* Start calibration */
void artos_facetrack_calibrate(phantom_artos_t *artos) {
    if (!artos) return;

    artos->facetrack.calibrating = 1;
    artos->facetrack.calibration_step = 0;

    if (artos->facetrack_status_label) {
        gtk_label_set_text(GTK_LABEL(artos->facetrack_status_label),
                          "Calibration: Look at TOP-LEFT corner...");
    }
}

/* Pen down - start drawing */
void artos_facetrack_pen_down(phantom_artos_t *artos) {
    if (!artos) return;

    artos->facetrack.drawing = 1;
    artos->facetrack.last_canvas_x = artos->facetrack.canvas_x;
    artos->facetrack.last_canvas_y = artos->facetrack.canvas_y;

    if (artos->facetrack_status_label) {
        gtk_label_set_text(GTK_LABEL(artos->facetrack_status_label), "Status: Drawing...");
    }
}

/* Pen up - stop drawing */
void artos_facetrack_pen_up(phantom_artos_t *artos) {
    if (!artos) return;

    artos->facetrack.drawing = 0;

    if (artos->facetrack_status_label && artos->facetrack.tracking) {
        gtk_label_set_text(GTK_LABEL(artos->facetrack_status_label), "Status: Tracking...");
    }
}

/* Toggle drawing state */
void artos_facetrack_toggle_draw(phantom_artos_t *artos) {
    if (!artos) return;

    if (artos->facetrack.drawing) {
        artos_facetrack_pen_up(artos);
    } else {
        artos_facetrack_pen_down(artos);
    }
}

/* UI Callbacks */
static void on_facetrack_toggle(GtkToggleButton *button, phantom_artos_t *artos) {
    int enabled = gtk_toggle_button_get_active(button);
    artos_facetrack_enable(artos, enabled);

    /* Update widget sensitivity */
    if (artos->facetrack_start_btn) {
        gtk_widget_set_sensitive(artos->facetrack_start_btn, enabled);
    }
    /* Note: facetrack_camera_btn is always enabled for testing camera */
    if (artos->facetrack_calibrate_btn) {
        gtk_widget_set_sensitive(artos->facetrack_calibrate_btn, enabled);
    }
    if (artos->facetrack_mode_combo) {
        gtk_widget_set_sensitive(artos->facetrack_mode_combo, enabled);
    }
    if (artos->facetrack_sensitivity_scale) {
        gtk_widget_set_sensitive(artos->facetrack_sensitivity_scale, enabled);
    }
    if (artos->facetrack_smoothing_scale) {
        gtk_widget_set_sensitive(artos->facetrack_smoothing_scale, enabled);
    }
}

static void on_facetrack_start_clicked(GtkButton *button, phantom_artos_t *artos) {
    (void)button;

    if (!artos->facetrack.enabled) {
        return;
    }

    if (artos->facetrack.tracking) {
        artos_facetrack_stop(artos);
    } else {
        artos_facetrack_start(artos);
    }
}

static void on_facetrack_camera_clicked(GtkButton *button, phantom_artos_t *artos) {
    (void)button;

    /* Get the mode as a string */
    const char *mode_str = "nose";
    switch (artos->facetrack.mode) {
        case ARTOS_FACE_MODE_NOSE: mode_str = "nose"; break;
        case ARTOS_FACE_MODE_HEAD: mode_str = "head"; break;
        case ARTOS_FACE_MODE_EYES: mode_str = "eyes"; break;
        case ARTOS_FACE_MODE_MOUTH: mode_str = "mouth"; break;
    }

    /* Build command to run face_track.py with --preview flag */
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "~/.phantomos-venv/bin/python3 "
             "/opt/phantomos/face_track.py "
             "--mode %s --preview &",
             mode_str);

    /* Launch the preview window in background */
    int result = system(cmd);
    if (result != 0) {
        gtk_label_set_text(GTK_LABEL(artos->facetrack_status_label),
                           "Failed to open camera preview");
    } else {
        gtk_label_set_text(GTK_LABEL(artos->facetrack_status_label),
                           "Camera preview opened (press Q to close)");
    }
}

static void on_facetrack_calibrate_clicked(GtkButton *button, phantom_artos_t *artos) {
    (void)button;
    artos_facetrack_calibrate(artos);
}

static void on_facetrack_mode_changed(GtkComboBox *combo, phantom_artos_t *artos) {
    int active = gtk_combo_box_get_active(combo);
    artos_facetrack_set_mode(artos, (artos_face_mode_t)active);
}

static void on_facetrack_sensitivity_changed(GtkRange *range, phantom_artos_t *artos) {
    double value = gtk_range_get_value(range);
    artos_facetrack_set_sensitivity(artos, value);
}

static void on_facetrack_smoothing_changed(GtkRange *range, phantom_artos_t *artos) {
    double value = gtk_range_get_value(range);
    artos_facetrack_set_smoothing(artos, value);
}

static void on_facetrack_blink_toggled(GtkToggleButton *button, phantom_artos_t *artos) {
    artos->facetrack.blink_to_draw = gtk_toggle_button_get_active(button);
}

static void on_facetrack_mouth_toggled(GtkToggleButton *button, phantom_artos_t *artos) {
    artos->facetrack.mouth_to_draw = gtk_toggle_button_get_active(button);
}

static void on_facetrack_smile_toggled(GtkToggleButton *button, phantom_artos_t *artos) {
    artos->facetrack.smile_to_undo = gtk_toggle_button_get_active(button);
}

/* Draw callback for face tracking preview */
static gboolean on_facetrack_preview_draw(GtkWidget *widget, cairo_t *cr, phantom_artos_t *artos) {
    int width = gtk_widget_get_allocated_width(widget);
    int height = gtk_widget_get_allocated_height(widget);

    /* Draw background */
    cairo_set_source_rgb(cr, 0.1, 0.1, 0.15);
    cairo_paint(cr);

    if (artos->facetrack.tracking) {
        /* Draw face position indicator */
        double face_x = artos->facetrack.face_x * width;
        double face_y = artos->facetrack.face_y * height;

        /* Draw crosshair at face position */
        cairo_set_source_rgba(cr, 0.0, 1.0, 0.0, 0.8);
        cairo_set_line_width(cr, 2.0);

        /* Vertical line */
        cairo_move_to(cr, face_x, 0);
        cairo_line_to(cr, face_x, height);
        cairo_stroke(cr);

        /* Horizontal line */
        cairo_move_to(cr, 0, face_y);
        cairo_line_to(cr, width, face_y);
        cairo_stroke(cr);

        /* Draw circle at tracking point */
        if (artos->facetrack.drawing) {
            cairo_set_source_rgba(cr, 1.0, 0.0, 0.0, 0.9);  /* Red when drawing */
        } else {
            cairo_set_source_rgba(cr, 0.0, 1.0, 0.0, 0.9);  /* Green when tracking */
        }
        cairo_arc(cr, face_x, face_y, 15, 0, 2 * G_PI);
        cairo_fill(cr);

        /* Inner dot */
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_arc(cr, face_x, face_y, 5, 0, 2 * G_PI);
        cairo_fill(cr);

        /* Draw mode indicator text */
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 12);

        const char *mode_text = "NOSE";
        switch (artos->facetrack.mode) {
            case ARTOS_FACE_MODE_NOSE: mode_text = "NOSE"; break;
            case ARTOS_FACE_MODE_HEAD: mode_text = "HEAD"; break;
            case ARTOS_FACE_MODE_EYES: mode_text = "EYES"; break;
            case ARTOS_FACE_MODE_MOUTH: mode_text = "MOUTH"; break;
        }
        cairo_move_to(cr, 5, 15);
        cairo_show_text(cr, mode_text);

        /* Draw drawing state */
        if (artos->facetrack.drawing) {
            cairo_set_source_rgb(cr, 1.0, 0.3, 0.3);
            cairo_move_to(cr, 5, height - 10);
            cairo_show_text(cr, "● DRAWING");
        } else {
            cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);
            cairo_move_to(cr, 5, height - 10);
            cairo_show_text(cr, "○ PEN UP");
        }

        /* Draw last gesture if any */
        if (artos->facetrack.last_gesture != ARTOS_FACE_GESTURE_NONE) {
            cairo_set_source_rgb(cr, 1.0, 1.0, 0.0);
            const char *gesture_text = "";
            switch (artos->facetrack.last_gesture) {
                case ARTOS_FACE_GESTURE_BLINK_LEFT: gesture_text = "👁 LEFT BLINK"; break;
                case ARTOS_FACE_GESTURE_BLINK_RIGHT: gesture_text = "👁 RIGHT BLINK"; break;
                case ARTOS_FACE_GESTURE_BLINK_BOTH: gesture_text = "👀 BLINK"; break;
                case ARTOS_FACE_GESTURE_MOUTH_OPEN: gesture_text = "👄 MOUTH OPEN"; break;
                case ARTOS_FACE_GESTURE_SMILE: gesture_text = "😊 SMILE"; break;
                default: break;
            }
            cairo_move_to(cr, width - 100, 15);
            cairo_show_text(cr, gesture_text);
        }
    } else {
        /* Not tracking - show instructions */
        cairo_set_source_rgb(cr, 0.6, 0.6, 0.6);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 14);

        cairo_text_extents_t extents;
        const char *text = "Enable tracking to see preview";
        cairo_text_extents(cr, text, &extents);
        cairo_move_to(cr, (width - extents.width) / 2, height / 2);
        cairo_show_text(cr, text);

        /* Draw camera icon */
        cairo_set_source_rgb(cr, 0.4, 0.4, 0.4);
        double cx = width / 2;
        double cy = height / 2 - 30;

        /* Camera body */
        cairo_rectangle(cr, cx - 25, cy - 15, 50, 30);
        cairo_stroke(cr);

        /* Lens */
        cairo_arc(cr, cx, cy, 10, 0, 2 * G_PI);
        cairo_stroke(cr);

        /* Flash */
        cairo_rectangle(cr, cx + 15, cy - 20, 8, 5);
        cairo_stroke(cr);
    }

    return FALSE;
}

/* Timer to refresh face tracking preview */
static gboolean facetrack_preview_refresh(gpointer data) {
    phantom_artos_t *artos = (phantom_artos_t *)data;
    if (artos->facetrack_preview_area && artos->facetrack.tracking) {
        gtk_widget_queue_draw(artos->facetrack_preview_area);
    }
    return artos->facetrack.tracking ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
}

/* Create face tracking panel */
GtkWidget *artos_create_facetrack_panel(phantom_artos_t *artos) {
    GtkWidget *frame = gtk_frame_new("👤 Draw with Face");
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(box), 10);

    /* Initialize face tracking */
    artos_facetrack_init(artos);

    /* Preview area - shows face position visualization */
    GtkWidget *preview_frame = gtk_frame_new("Face Preview");
    artos->facetrack_preview_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(artos->facetrack_preview_area, 200, 150);
    g_signal_connect(artos->facetrack_preview_area, "draw",
                     G_CALLBACK(on_facetrack_preview_draw), artos);
    gtk_container_add(GTK_CONTAINER(preview_frame), artos->facetrack_preview_area);
    gtk_box_pack_start(GTK_BOX(box), preview_frame, FALSE, FALSE, 5);

    /* Enable toggle */
    artos->facetrack_toggle = gtk_toggle_button_new_with_label("Enable Face Tracking");
    g_signal_connect(artos->facetrack_toggle, "toggled",
                     G_CALLBACK(on_facetrack_toggle), artos);
    gtk_box_pack_start(GTK_BOX(box), artos->facetrack_toggle, FALSE, FALSE, 0);

    /* Start/Stop button */
    artos->facetrack_start_btn = gtk_button_new_with_label("▶ Start Tracking");
    gtk_widget_set_sensitive(artos->facetrack_start_btn, FALSE);
    g_signal_connect(artos->facetrack_start_btn, "clicked",
                     G_CALLBACK(on_facetrack_start_clicked), artos);
    gtk_box_pack_start(GTK_BOX(box), artos->facetrack_start_btn, FALSE, FALSE, 5);

    /* Show Camera button - opens live webcam preview window (always enabled) */
    artos->facetrack_camera_btn = gtk_button_new_with_label("📹 Show Camera");
    g_signal_connect(artos->facetrack_camera_btn, "clicked",
                     G_CALLBACK(on_facetrack_camera_clicked), artos);
    gtk_box_pack_start(GTK_BOX(box), artos->facetrack_camera_btn, FALSE, FALSE, 0);

    /* Tracking mode selector */
    GtkWidget *mode_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget *mode_label = gtk_label_new("Track:");
    gtk_box_pack_start(GTK_BOX(mode_box), mode_label, FALSE, FALSE, 0);

    artos->facetrack_mode_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(artos->facetrack_mode_combo), "Nose");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(artos->facetrack_mode_combo), "Head Center");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(artos->facetrack_mode_combo), "Eyes");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(artos->facetrack_mode_combo), "Mouth");
    gtk_combo_box_set_active(GTK_COMBO_BOX(artos->facetrack_mode_combo), 0);
    gtk_widget_set_sensitive(artos->facetrack_mode_combo, FALSE);
    g_signal_connect(artos->facetrack_mode_combo, "changed",
                     G_CALLBACK(on_facetrack_mode_changed), artos);
    gtk_box_pack_start(GTK_BOX(mode_box), artos->facetrack_mode_combo, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), mode_box, FALSE, FALSE, 0);

    /* Sensitivity slider */
    GtkWidget *sens_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget *sens_label = gtk_label_new("Sensitivity:");
    gtk_box_pack_start(GTK_BOX(sens_box), sens_label, FALSE, FALSE, 0);
    artos->facetrack_sensitivity_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,
                                                                   0.5, 3.0, 0.1);
    gtk_range_set_value(GTK_RANGE(artos->facetrack_sensitivity_scale), 1.5);
    gtk_widget_set_sensitive(artos->facetrack_sensitivity_scale, FALSE);
    g_signal_connect(artos->facetrack_sensitivity_scale, "value-changed",
                     G_CALLBACK(on_facetrack_sensitivity_changed), artos);
    gtk_box_pack_start(GTK_BOX(sens_box), artos->facetrack_sensitivity_scale, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), sens_box, FALSE, FALSE, 0);

    /* Smoothing slider */
    GtkWidget *smooth_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget *smooth_label = gtk_label_new("Smoothing:");
    gtk_box_pack_start(GTK_BOX(smooth_box), smooth_label, FALSE, FALSE, 0);
    artos->facetrack_smoothing_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,
                                                                 0.0, 0.9, 0.05);
    gtk_range_set_value(GTK_RANGE(artos->facetrack_smoothing_scale), 0.3);
    gtk_widget_set_sensitive(artos->facetrack_smoothing_scale, FALSE);
    g_signal_connect(artos->facetrack_smoothing_scale, "value-changed",
                     G_CALLBACK(on_facetrack_smoothing_changed), artos);
    gtk_box_pack_start(GTK_BOX(smooth_box), artos->facetrack_smoothing_scale, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), smooth_box, FALSE, FALSE, 0);

    /* Separator */
    gtk_box_pack_start(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL),
                       FALSE, FALSE, 5);

    /* Gesture controls */
    GtkWidget *gesture_label = gtk_label_new("Gesture Actions:");
    gtk_widget_set_halign(gesture_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), gesture_label, FALSE, FALSE, 0);

    artos->facetrack_blink_check = gtk_check_button_new_with_label("Blink to toggle drawing");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(artos->facetrack_blink_check), TRUE);
    g_signal_connect(artos->facetrack_blink_check, "toggled",
                     G_CALLBACK(on_facetrack_blink_toggled), artos);
    gtk_box_pack_start(GTK_BOX(box), artos->facetrack_blink_check, FALSE, FALSE, 0);

    artos->facetrack_mouth_check = gtk_check_button_new_with_label("Open mouth to toggle drawing");
    g_signal_connect(artos->facetrack_mouth_check, "toggled",
                     G_CALLBACK(on_facetrack_mouth_toggled), artos);
    gtk_box_pack_start(GTK_BOX(box), artos->facetrack_mouth_check, FALSE, FALSE, 0);

    artos->facetrack_smile_check = gtk_check_button_new_with_label("Smile to undo");
    g_signal_connect(artos->facetrack_smile_check, "toggled",
                     G_CALLBACK(on_facetrack_smile_toggled), artos);
    gtk_box_pack_start(GTK_BOX(box), artos->facetrack_smile_check, FALSE, FALSE, 0);

    /* Separator */
    gtk_box_pack_start(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL),
                       FALSE, FALSE, 5);

    /* Status display */
    artos->facetrack_status_label = gtk_label_new("Status: Not started");
    gtk_widget_set_halign(artos->facetrack_status_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), artos->facetrack_status_label, FALSE, FALSE, 0);

    artos->facetrack_pos_label = gtk_label_new("Position: --, --");
    gtk_widget_set_halign(artos->facetrack_pos_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), artos->facetrack_pos_label, FALSE, FALSE, 0);

    artos->facetrack_fps_label = gtk_label_new("FPS: --");
    gtk_widget_set_halign(artos->facetrack_fps_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), artos->facetrack_fps_label, FALSE, FALSE, 0);

    artos->facetrack_gesture_label = gtk_label_new("Gesture: none");
    gtk_widget_set_halign(artos->facetrack_gesture_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), artos->facetrack_gesture_label, FALSE, FALSE, 0);

    /* Calibrate button */
    artos->facetrack_calibrate_btn = gtk_button_new_with_label("🎯 Calibrate");
    gtk_widget_set_sensitive(artos->facetrack_calibrate_btn, FALSE);
    g_signal_connect(artos->facetrack_calibrate_btn, "clicked",
                     G_CALLBACK(on_facetrack_calibrate_clicked), artos);
    gtk_box_pack_start(GTK_BOX(box), artos->facetrack_calibrate_btn, FALSE, FALSE, 5);

    /* Instructions */
    GtkWidget *instructions = gtk_label_new(
        "Move your face to control the brush.\n"
        "Blink to toggle drawing on/off.\n"
        "Requires webcam and OpenCV/MediaPipe."
    );
    gtk_label_set_line_wrap(GTK_LABEL(instructions), TRUE);
    gtk_widget_set_halign(instructions, GTK_ALIGN_START);

    GtkStyleContext *ctx = gtk_widget_get_style_context(instructions);
    gtk_style_context_add_class(ctx, "dim-label");

    gtk_box_pack_start(GTK_BOX(box), instructions, FALSE, FALSE, 5);

    gtk_container_add(GTK_CONTAINER(frame), box);
    artos->facetrack_panel = frame;

    return frame;
}

/* ==============================================================================
 * AI-Assisted Drawing Implementation
 * ============================================================================== */

/* Shape recognition - analyzes stroke points to identify geometric shapes */
int artos_ai_recognize_shape(artos_ai_point_t *points, int count, char *shape_name, double *params) {
    if (!points || count < 3 || !shape_name || !params) return 0;

    /* Calculate bounding box */
    double min_x = points[0].x, max_x = points[0].x;
    double min_y = points[0].y, max_y = points[0].y;

    for (int i = 1; i < count; i++) {
        if (points[i].x < min_x) min_x = points[i].x;
        if (points[i].x > max_x) max_x = points[i].x;
        if (points[i].y < min_y) min_y = points[i].y;
        if (points[i].y > max_y) max_y = points[i].y;
    }

    double width = max_x - min_x;
    double height = max_y - min_y;
    double cx = (min_x + max_x) / 2;
    double cy = (min_y + max_y) / 2;

    /* Check if stroke is closed (start near end) */
    double start_end_dist = sqrt(pow(points[0].x - points[count-1].x, 2) +
                                  pow(points[0].y - points[count-1].y, 2));
    int is_closed = start_end_dist < (width + height) * 0.15;

    /* Calculate average radius from center for circle detection */
    double avg_radius = 0;
    double radius_variance = 0;
    for (int i = 0; i < count; i++) {
        double r = sqrt(pow(points[i].x - cx, 2) + pow(points[i].y - cy, 2));
        avg_radius += r;
    }
    avg_radius /= count;

    for (int i = 0; i < count; i++) {
        double r = sqrt(pow(points[i].x - cx, 2) + pow(points[i].y - cy, 2));
        radius_variance += pow(r - avg_radius, 2);
    }
    radius_variance = sqrt(radius_variance / count);

    /* Circle: closed shape with consistent radius */
    if (is_closed && radius_variance / avg_radius < 0.15) {
        strcpy(shape_name, "circle");
        params[0] = cx;
        params[1] = cy;
        params[2] = avg_radius;
        return 1;
    }

    /* Ellipse: closed shape with width != height */
    if (is_closed && fabs(width - height) > 20) {
        double ratio = width / height;
        if (ratio > 1.2 || ratio < 0.8) {
            strcpy(shape_name, "ellipse");
            params[0] = cx;
            params[1] = cy;
            params[2] = width / 2;
            params[3] = height / 2;
            return 1;
        }
    }

    /* Rectangle: closed shape with ~90 degree corners */
    if (is_closed) {
        /* Count significant direction changes */
        int corners = 0;
        for (int i = 2; i < count; i++) {
            double dx1 = points[i-1].x - points[i-2].x;
            double dy1 = points[i-1].y - points[i-2].y;
            double dx2 = points[i].x - points[i-1].x;
            double dy2 = points[i].y - points[i-1].y;

            double len1 = sqrt(dx1*dx1 + dy1*dy1);
            double len2 = sqrt(dx2*dx2 + dy2*dy2);
            if (len1 < 1 || len2 < 1) continue;

            double dot = (dx1*dx2 + dy1*dy2) / (len1 * len2);
            double angle = acos(fmax(-1, fmin(1, dot))) * 180 / M_PI;

            if (angle > 60 && angle < 120) corners++;
        }

        if (corners >= 3 && corners <= 6) {
            strcpy(shape_name, "rectangle");
            params[0] = min_x;
            params[1] = min_y;
            params[2] = width;
            params[3] = height;
            return 1;
        }
    }

    /* Triangle: closed shape with 3 corners */
    if (is_closed) {
        int corners = 0;
        double corner_x[3], corner_y[3];

        for (int i = 2; i < count && corners < 3; i++) {
            double dx1 = points[i-1].x - points[i-2].x;
            double dy1 = points[i-1].y - points[i-2].y;
            double dx2 = points[i].x - points[i-1].x;
            double dy2 = points[i].y - points[i-1].y;

            double len1 = sqrt(dx1*dx1 + dy1*dy1);
            double len2 = sqrt(dx2*dx2 + dy2*dy2);
            if (len1 < 1 || len2 < 1) continue;

            double dot = (dx1*dx2 + dy1*dy2) / (len1 * len2);
            double angle = acos(fmax(-1, fmin(1, dot))) * 180 / M_PI;

            if (angle > 30 && angle < 150) {
                corner_x[corners] = points[i-1].x;
                corner_y[corners] = points[i-1].y;
                corners++;
            }
        }

        if (corners == 3) {
            strcpy(shape_name, "triangle");
            params[0] = corner_x[0]; params[1] = corner_y[0];
            params[2] = corner_x[1]; params[3] = corner_y[1];
            params[4] = corner_x[2]; params[5] = corner_y[2];
            return 1;
        }
    }

    /* Line: not closed, mostly straight */
    if (!is_closed && count >= 2) {
        double total_dist = 0;
        for (int i = 1; i < count; i++) {
            total_dist += sqrt(pow(points[i].x - points[i-1].x, 2) +
                              pow(points[i].y - points[i-1].y, 2));
        }

        double direct_dist = sqrt(pow(points[count-1].x - points[0].x, 2) +
                                  pow(points[count-1].y - points[0].y, 2));

        if (direct_dist > 20 && total_dist / direct_dist < 1.2) {
            strcpy(shape_name, "line");
            params[0] = points[0].x;
            params[1] = points[0].y;
            params[2] = points[count-1].x;
            params[3] = points[count-1].y;
            return 1;
        }
    }

    return 0;  /* No shape recognized */
}

/* Initialize AI assistance */
int artos_ai_init(phantom_artos_t *artos) {
    if (!artos) return 0;

    memset(&artos->ai_assist, 0, sizeof(artos_ai_assist_t));
    artos->ai_assist.mode = ARTOS_AI_MODE_SUGGEST;
    artos->ai_assist.shape_recognition = 1;
    artos->ai_assist.shape_tolerance = 0.2;
    artos->ai_assist.style_strength = 0.5;
    artos->ai_assist.auto_suggest = 1;
    artos->ai_assist.suggest_delay_ms = 500;

    /* Allocate stroke buffer */
    artos->ai_assist.stroke_capacity = 1000;
    artos->ai_assist.stroke_buffer = malloc(sizeof(artos_ai_point_t) * artos->ai_assist.stroke_capacity);

    return artos->ai_assist.stroke_buffer != NULL;
}

/* Cleanup AI assistance */
void artos_ai_cleanup(phantom_artos_t *artos) {
    if (!artos) return;

    if (artos->ai_assist.stroke_buffer) {
        free(artos->ai_assist.stroke_buffer);
        artos->ai_assist.stroke_buffer = NULL;
    }

    artos_ai_clear_suggestions(artos);

    if (artos->ai_assist.style_reference) {
        cairo_surface_destroy(artos->ai_assist.style_reference);
        artos->ai_assist.style_reference = NULL;
    }

    if (artos->ai_assist.ai_pid > 0) {
        kill(artos->ai_assist.ai_pid, SIGTERM);
        artos->ai_assist.ai_pid = 0;
    }
}

/* Enable/disable AI assistance */
void artos_ai_enable(phantom_artos_t *artos, int enable) {
    if (!artos) return;
    artos->ai_assist.enabled = enable;

    if (artos->ai_status_label) {
        gtk_label_set_text(GTK_LABEL(artos->ai_status_label),
                           enable ? "AI Assistant Ready" : "AI Assistant Disabled");
    }
}

/* Set AI mode */
void artos_ai_set_mode(phantom_artos_t *artos, artos_ai_mode_t mode) {
    if (!artos) return;
    artos->ai_assist.mode = mode;
}

/* Analyze stroke for suggestions */
void artos_ai_analyze_stroke(phantom_artos_t *artos, artos_ai_point_t *points, int count) {
    if (!artos || !points || count < 3 || !artos->ai_assist.enabled) return;

    /* Copy points to buffer */
    if (count > artos->ai_assist.stroke_capacity) {
        artos->ai_assist.stroke_capacity = count * 2;
        artos->ai_assist.stroke_buffer = realloc(artos->ai_assist.stroke_buffer,
                                                  sizeof(artos_ai_point_t) * artos->ai_assist.stroke_capacity);
    }
    memcpy(artos->ai_assist.stroke_buffer, points, sizeof(artos_ai_point_t) * count);
    artos->ai_assist.stroke_count = count;

    /* Clear previous suggestions */
    artos_ai_clear_suggestions(artos);

    /* Shape recognition */
    if (artos->ai_assist.shape_recognition) {
        char shape_name[32];
        double params[8];

        if (artos_ai_recognize_shape(points, count, shape_name, params)) {
            /* Create shape suggestion */
            artos_ai_suggestion_t *sug = calloc(1, sizeof(artos_ai_suggestion_t));
            sug->type = ARTOS_AI_SUGGEST_SHAPE;
            snprintf(sug->description, sizeof(sug->description),
                     "Perfect %s detected", shape_name);
            strncpy(sug->shape_name, shape_name, sizeof(sug->shape_name) - 1);
            memcpy(sug->shape_params, params, sizeof(params));
            sug->confidence = 0.85;

            sug->next = artos->ai_assist.suggestions;
            artos->ai_assist.suggestions = sug;
            artos->ai_assist.suggestion_count++;

            /* Update UI */
            if (artos->ai_status_label) {
                char status[256];
                snprintf(status, sizeof(status), "Recognized: %s (%.0f%% confident)",
                         shape_name, sug->confidence * 100);
                gtk_label_set_text(GTK_LABEL(artos->ai_status_label), status);
            }

            /* Redraw suggestion preview */
            if (artos->ai_suggest_area) {
                gtk_widget_queue_draw(artos->ai_suggest_area);
            }
        }
    }
}

/* Accept current suggestion */
void artos_ai_accept_suggestion(phantom_artos_t *artos) {
    if (!artos || !artos->ai_assist.suggestions) return;

    artos_ai_suggestion_t *sug = artos->ai_assist.suggestions;

    /* Apply the suggestion based on type */
    if (sug->type == ARTOS_AI_SUGGEST_SHAPE) {
        double *p = sug->shape_params;

        if (strcmp(sug->shape_name, "circle") == 0) {
            artos_draw_circle(artos, p[0], p[1], p[2], 0);
        } else if (strcmp(sug->shape_name, "ellipse") == 0) {
            artos_draw_shape(artos, ARTOS_TOOL_ELLIPSE,
                            p[0] - p[2], p[1] - p[3],
                            p[0] + p[2], p[1] + p[3], 0);
        } else if (strcmp(sug->shape_name, "rectangle") == 0) {
            artos_draw_shape(artos, ARTOS_TOOL_RECTANGLE,
                            p[0], p[1], p[0] + p[2], p[1] + p[3], 0);
        } else if (strcmp(sug->shape_name, "triangle") == 0) {
            double cx = (p[0] + p[2] + p[4]) / 3;
            double cy = (p[1] + p[3] + p[5]) / 3;
            double size = sqrt(pow(p[2] - p[0], 2) + pow(p[3] - p[1], 2));
            artos_draw_triangle(artos, cx, cy, size, 0);
        } else if (strcmp(sug->shape_name, "line") == 0) {
            artos_draw_line(artos, p[0], p[1], p[2], p[3]);
        }

        if (artos->canvas_area) {
            gtk_widget_queue_draw(artos->canvas_area);
        }
    }

    /* Clear suggestions after accepting */
    artos_ai_clear_suggestions(artos);

    if (artos->ai_status_label) {
        gtk_label_set_text(GTK_LABEL(artos->ai_status_label), "Shape applied!");
    }
}

/* Reject current suggestion */
void artos_ai_reject_suggestion(phantom_artos_t *artos) {
    if (!artos) return;
    artos_ai_clear_suggestions(artos);

    if (artos->ai_status_label) {
        gtk_label_set_text(GTK_LABEL(artos->ai_status_label), "Suggestion rejected");
    }
}

/* Cycle to next suggestion */
void artos_ai_next_suggestion(phantom_artos_t *artos) {
    if (!artos || !artos->ai_assist.suggestions) return;

    artos->ai_assist.selected_suggestion++;
    if (artos->ai_assist.selected_suggestion >= artos->ai_assist.suggestion_count) {
        artos->ai_assist.selected_suggestion = 0;
    }

    if (artos->ai_suggest_area) {
        gtk_widget_queue_draw(artos->ai_suggest_area);
    }
}

/* Generate from text prompt */
void artos_ai_generate_from_prompt(phantom_artos_t *artos, const char *prompt) {
    if (!artos || !prompt) return;

    strncpy(artos->ai_assist.prompt, prompt, sizeof(artos->ai_assist.prompt) - 1);
    artos->ai_assist.generating = 1;

    if (artos->ai_status_label) {
        gtk_label_set_text(GTK_LABEL(artos->ai_status_label), "Generating from prompt...");
    }

    /* TODO: Launch AI backend subprocess for actual generation */
    /* For now, show placeholder message */
    if (artos->ai_progress_bar) {
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(artos->ai_progress_bar), 0.5);
    }
}

/* Load style reference image */
void artos_ai_load_style_reference(phantom_artos_t *artos, const char *filepath) {
    if (!artos || !filepath) return;

    if (artos->ai_assist.style_reference) {
        cairo_surface_destroy(artos->ai_assist.style_reference);
    }

    artos->ai_assist.style_reference = cairo_image_surface_create_from_png(filepath);

    if (cairo_surface_status(artos->ai_assist.style_reference) == CAIRO_STATUS_SUCCESS) {
        snprintf(artos->ai_assist.style_name, sizeof(artos->ai_assist.style_name),
                 "%s", strrchr(filepath, '/') ? strrchr(filepath, '/') + 1 : filepath);

        if (artos->ai_status_label) {
            char status[256];
            snprintf(status, sizeof(status), "Style loaded: %s", artos->ai_assist.style_name);
            gtk_label_set_text(GTK_LABEL(artos->ai_status_label), status);
        }
    }
}

/* Clear all suggestions */
void artos_ai_clear_suggestions(phantom_artos_t *artos) {
    if (!artos) return;

    artos_ai_suggestion_t *sug = artos->ai_assist.suggestions;
    while (sug) {
        artos_ai_suggestion_t *next = sug->next;
        if (sug->points) free(sug->points);
        if (sug->preview) cairo_surface_destroy(sug->preview);
        free(sug);
        sug = next;
    }

    artos->ai_assist.suggestions = NULL;
    artos->ai_assist.suggestion_count = 0;
    artos->ai_assist.selected_suggestion = 0;
}

/* UI Callbacks for AI panel */
static void on_ai_toggle(GtkToggleButton *button, phantom_artos_t *artos) {
    artos_ai_enable(artos, gtk_toggle_button_get_active(button));
}

static void on_ai_mode_changed(GtkComboBox *combo, phantom_artos_t *artos) {
    int active = gtk_combo_box_get_active(combo);
    artos_ai_set_mode(artos, (artos_ai_mode_t)active);
}

static void on_ai_accept_clicked(GtkButton *button, phantom_artos_t *artos) {
    (void)button;
    artos_ai_accept_suggestion(artos);
}

static void on_ai_reject_clicked(GtkButton *button, phantom_artos_t *artos) {
    (void)button;
    artos_ai_reject_suggestion(artos);
}

static void on_ai_generate_clicked(GtkButton *button, phantom_artos_t *artos) {
    (void)button;
    if (artos->ai_prompt_entry) {
        const char *prompt = gtk_entry_get_text(GTK_ENTRY(artos->ai_prompt_entry));
        artos_ai_generate_from_prompt(artos, prompt);
    }
}

static void on_ai_shape_toggled(GtkToggleButton *button, phantom_artos_t *artos) {
    artos->ai_assist.shape_recognition = gtk_toggle_button_get_active(button);
}

/* Draw AI suggestion preview */
static gboolean on_ai_suggest_draw(GtkWidget *widget, cairo_t *cr, phantom_artos_t *artos) {
    int width = gtk_widget_get_allocated_width(widget);
    int height = gtk_widget_get_allocated_height(widget);

    /* Background */
    cairo_set_source_rgb(cr, 0.15, 0.15, 0.2);
    cairo_paint(cr);

    if (!artos->ai_assist.suggestions) {
        /* No suggestions - show placeholder */
        cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 12);

        cairo_text_extents_t extents;
        const char *text = "Draw to see AI suggestions";
        cairo_text_extents(cr, text, &extents);
        cairo_move_to(cr, (width - extents.width) / 2, height / 2);
        cairo_show_text(cr, text);
        return FALSE;
    }

    artos_ai_suggestion_t *sug = artos->ai_assist.suggestions;

    /* Draw shape preview */
    if (sug->type == ARTOS_AI_SUGGEST_SHAPE) {
        double *p = sug->shape_params;
        double scale = 0.8;
        double ox = width * 0.1;
        double oy = height * 0.1;

        cairo_set_source_rgba(cr, 0.2, 0.8, 0.2, 0.8);
        cairo_set_line_width(cr, 2.0);

        if (strcmp(sug->shape_name, "circle") == 0) {
            cairo_arc(cr, width/2, height/2, fmin(width, height) * 0.3, 0, 2 * M_PI);
            cairo_stroke(cr);
        } else if (strcmp(sug->shape_name, "rectangle") == 0) {
            cairo_rectangle(cr, ox, oy, width * scale, height * scale);
            cairo_stroke(cr);
        } else if (strcmp(sug->shape_name, "ellipse") == 0) {
            cairo_save(cr);
            cairo_translate(cr, width/2, height/2);
            cairo_scale(cr, width * 0.4, height * 0.3);
            cairo_arc(cr, 0, 0, 1, 0, 2 * M_PI);
            cairo_restore(cr);
            cairo_stroke(cr);
        } else if (strcmp(sug->shape_name, "triangle") == 0) {
            cairo_move_to(cr, width/2, height * 0.1);
            cairo_line_to(cr, width * 0.1, height * 0.9);
            cairo_line_to(cr, width * 0.9, height * 0.9);
            cairo_close_path(cr);
            cairo_stroke(cr);
        } else if (strcmp(sug->shape_name, "line") == 0) {
            cairo_move_to(cr, width * 0.1, height * 0.5);
            cairo_line_to(cr, width * 0.9, height * 0.5);
            cairo_stroke(cr);
        }

        /* Show description */
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_set_font_size(cr, 10);
        cairo_move_to(cr, 5, height - 5);
        cairo_show_text(cr, sug->description);
    }

    return FALSE;
}

/* Create AI assistance panel */
GtkWidget *artos_create_ai_panel(phantom_artos_t *artos) {
    GtkWidget *frame = gtk_frame_new("🤖 AI Assistant");
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(box), 10);

    /* Initialize AI */
    artos_ai_init(artos);

    /* Enable toggle */
    artos->ai_toggle = gtk_toggle_button_new_with_label("Enable AI Assistance");
    g_signal_connect(artos->ai_toggle, "toggled", G_CALLBACK(on_ai_toggle), artos);
    gtk_box_pack_start(GTK_BOX(box), artos->ai_toggle, FALSE, FALSE, 0);

    /* Mode selector */
    GtkWidget *mode_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget *mode_label = gtk_label_new("Mode:");
    gtk_box_pack_start(GTK_BOX(mode_box), mode_label, FALSE, FALSE, 0);

    artos->ai_mode_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(artos->ai_mode_combo), "Off");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(artos->ai_mode_combo), "Suggest");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(artos->ai_mode_combo), "Auto-Complete");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(artos->ai_mode_combo), "Style Transfer");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(artos->ai_mode_combo), "Generate");
    gtk_combo_box_set_active(GTK_COMBO_BOX(artos->ai_mode_combo), 1);
    g_signal_connect(artos->ai_mode_combo, "changed", G_CALLBACK(on_ai_mode_changed), artos);
    gtk_box_pack_start(GTK_BOX(mode_box), artos->ai_mode_combo, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), mode_box, FALSE, FALSE, 0);

    /* Shape recognition checkbox */
    artos->ai_shape_check = gtk_check_button_new_with_label("Shape Recognition");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(artos->ai_shape_check), TRUE);
    g_signal_connect(artos->ai_shape_check, "toggled", G_CALLBACK(on_ai_shape_toggled), artos);
    gtk_box_pack_start(GTK_BOX(box), artos->ai_shape_check, FALSE, FALSE, 0);

    /* Suggestion preview area */
    GtkWidget *preview_frame = gtk_frame_new("Suggestion");
    artos->ai_suggest_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(artos->ai_suggest_area, 150, 100);
    g_signal_connect(artos->ai_suggest_area, "draw", G_CALLBACK(on_ai_suggest_draw), artos);
    gtk_container_add(GTK_CONTAINER(preview_frame), artos->ai_suggest_area);
    gtk_box_pack_start(GTK_BOX(box), preview_frame, FALSE, FALSE, 5);

    /* Accept/Reject buttons */
    GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    artos->ai_accept_btn = gtk_button_new_with_label("✓ Accept");
    artos->ai_reject_btn = gtk_button_new_with_label("✗ Reject");
    g_signal_connect(artos->ai_accept_btn, "clicked", G_CALLBACK(on_ai_accept_clicked), artos);
    g_signal_connect(artos->ai_reject_btn, "clicked", G_CALLBACK(on_ai_reject_clicked), artos);
    gtk_box_pack_start(GTK_BOX(btn_box), artos->ai_accept_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(btn_box), artos->ai_reject_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), btn_box, FALSE, FALSE, 0);

    /* Separator */
    gtk_box_pack_start(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 5);

    /* Prompt input for generation */
    GtkWidget *prompt_label = gtk_label_new("Generate from prompt:");
    gtk_widget_set_halign(prompt_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), prompt_label, FALSE, FALSE, 0);

    artos->ai_prompt_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(artos->ai_prompt_entry), "Describe what to draw...");
    gtk_box_pack_start(GTK_BOX(box), artos->ai_prompt_entry, FALSE, FALSE, 0);

    artos->ai_generate_btn = gtk_button_new_with_label("🎨 Generate");
    g_signal_connect(artos->ai_generate_btn, "clicked", G_CALLBACK(on_ai_generate_clicked), artos);
    gtk_box_pack_start(GTK_BOX(box), artos->ai_generate_btn, FALSE, FALSE, 0);

    /* Progress bar */
    artos->ai_progress_bar = gtk_progress_bar_new();
    gtk_box_pack_start(GTK_BOX(box), artos->ai_progress_bar, FALSE, FALSE, 0);

    /* Status label */
    artos->ai_status_label = gtk_label_new("AI Assistant Ready");
    gtk_widget_set_halign(artos->ai_status_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), artos->ai_status_label, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(frame), box);
    artos->ai_panel = frame;

    return frame;
}

/* ==============================================================================
 * Voice-to-Art Generation Implementation
 * ============================================================================== */

/* Initialize voice-to-art */
int artos_voiceart_init(phantom_artos_t *artos) {
    if (!artos) return 0;

    memset(&artos->voice_art, 0, sizeof(artos_voice_art_t));
    strcpy(artos->voice_art.style_preset, "realistic");
    artos->voice_art.width = 512;
    artos->voice_art.height = 512;
    artos->voice_art.creativity = 0.7;

    return 1;
}

/* Cleanup voice-to-art */
void artos_voiceart_cleanup(phantom_artos_t *artos) {
    if (!artos) return;

    /* Free generated images */
    for (int i = 0; i < artos->voice_art.generated_count; i++) {
        if (artos->voice_art.generated[i]) {
            cairo_surface_destroy(artos->voice_art.generated[i]);
            artos->voice_art.generated[i] = NULL;
        }
    }

    /* Free history thumbnails */
    for (int i = 0; i < artos->voice_art.history_count; i++) {
        if (artos->voice_art.history[i].thumbnail) {
            cairo_surface_destroy(artos->voice_art.history[i].thumbnail);
            artos->voice_art.history[i].thumbnail = NULL;
        }
    }

    /* Kill generation process if running */
    if (artos->voice_art.gen_pid > 0) {
        kill(artos->voice_art.gen_pid, SIGTERM);
        artos->voice_art.gen_pid = 0;
    }
}

/* Enable/disable voice-to-art */
void artos_voiceart_enable(phantom_artos_t *artos, int enable) {
    if (!artos) return;
    artos->voice_art.enabled = enable;

    if (artos->voiceart_status_label) {
        gtk_label_set_text(GTK_LABEL(artos->voiceart_status_label),
                           enable ? "Voice-to-Art Ready" : "Voice-to-Art Disabled");
    }
}

/* Start listening for voice input */
void artos_voiceart_start_listening(phantom_artos_t *artos) {
    if (!artos || !artos->voice_art.enabled) return;

    artos->voice_art.listening = 1;
    memset(artos->voice_art.transcript, 0, sizeof(artos->voice_art.transcript));

    if (artos->voiceart_status_label) {
        gtk_label_set_text(GTK_LABEL(artos->voiceart_status_label), "Listening... Describe your image");
    }

    if (artos->voiceart_listen_btn) {
        gtk_button_set_label(GTK_BUTTON(artos->voiceart_listen_btn), "🔴 Listening...");
    }

    /* TODO: Start actual voice recognition using GStreamer/Whisper */
}

/* Stop listening */
void artos_voiceart_stop_listening(phantom_artos_t *artos) {
    if (!artos) return;

    artos->voice_art.listening = 0;

    if (artos->voiceart_listen_btn) {
        gtk_button_set_label(GTK_BUTTON(artos->voiceart_listen_btn), "🎤 Listen");
    }

    /* If we have a transcript, trigger generation */
    if (strlen(artos->voice_art.transcript) > 0) {
        artos_voiceart_generate(artos, artos->voice_art.transcript);
    }
}

/* Generate image from prompt */
void artos_voiceart_generate(phantom_artos_t *artos, const char *prompt) {
    if (!artos || !prompt || strlen(prompt) == 0) return;

    strncpy(artos->voice_art.transcript, prompt, sizeof(artos->voice_art.transcript) - 1);
    artos->voice_art.generating = 1;
    artos->voice_art.progress = 0;

    if (artos->voiceart_status_label) {
        char status[256];
        snprintf(status, sizeof(status), "Generating: \"%s\"...",
                 strlen(prompt) > 30 ? "..." : prompt);
        gtk_label_set_text(GTK_LABEL(artos->voiceart_status_label), status);
    }

    strcpy(artos->voice_art.status, "Generating image...");

    /* Start progress animation */
    if (artos->voiceart_progress_bar) {
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(artos->voiceart_progress_bar), 0.1);
    }

    /* For demo: Create a placeholder colored rectangle based on prompt keywords */
    /* In production, this would call a real AI image generation backend */
    cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                                        artos->voice_art.width,
                                                        artos->voice_art.height);
    cairo_t *cr = cairo_create(surf);

    /* Parse prompt for color/content hints */
    double r = 0.3, g = 0.3, b = 0.5;  /* Default blue-gray */

    if (strstr(prompt, "sunset") || strstr(prompt, "orange")) {
        r = 0.9; g = 0.5; b = 0.2;
    } else if (strstr(prompt, "forest") || strstr(prompt, "green") || strstr(prompt, "tree")) {
        r = 0.2; g = 0.6; b = 0.3;
    } else if (strstr(prompt, "ocean") || strstr(prompt, "sea") || strstr(prompt, "blue")) {
        r = 0.1; g = 0.4; b = 0.8;
    } else if (strstr(prompt, "night") || strstr(prompt, "dark")) {
        r = 0.1; g = 0.1; b = 0.2;
    } else if (strstr(prompt, "fire") || strstr(prompt, "red")) {
        r = 0.8; g = 0.2; b = 0.1;
    }

    /* Create gradient background */
    cairo_pattern_t *grad = cairo_pattern_create_linear(0, 0, 0, artos->voice_art.height);
    cairo_pattern_add_color_stop_rgb(grad, 0, r * 1.2, g * 1.2, b * 1.2);
    cairo_pattern_add_color_stop_rgb(grad, 1, r * 0.5, g * 0.5, b * 0.5);
    cairo_set_source(cr, grad);
    cairo_paint(cr);
    cairo_pattern_destroy(grad);

    /* Add some abstract shapes based on prompt */
    srand((unsigned int)time(NULL));
    for (int i = 0; i < 5; i++) {
        double cx = (rand() % 100) / 100.0 * artos->voice_art.width;
        double cy = (rand() % 100) / 100.0 * artos->voice_art.height;
        double size = 30 + (rand() % 100);

        cairo_set_source_rgba(cr, 1 - r, 1 - g, 1 - b, 0.3);
        cairo_arc(cr, cx, cy, size, 0, 2 * M_PI);
        cairo_fill(cr);
    }

    /* Add prompt text at bottom */
    cairo_set_source_rgba(cr, 1, 1, 1, 0.8);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 14);
    cairo_move_to(cr, 10, artos->voice_art.height - 15);
    cairo_show_text(cr, "[AI Generated Placeholder]");

    cairo_destroy(cr);

    /* Store generated image */
    if (artos->voice_art.generated[0]) {
        cairo_surface_destroy(artos->voice_art.generated[0]);
    }
    artos->voice_art.generated[0] = surf;
    artos->voice_art.generated_count = 1;
    artos->voice_art.selected_image = 0;

    /* Add to history */
    if (artos->voice_art.history_count < 20) {
        int idx = artos->voice_art.history_count++;
        strncpy(artos->voice_art.history[idx].prompt, prompt,
                sizeof(artos->voice_art.history[idx].prompt) - 1);
        artos->voice_art.history[idx].timestamp = time(NULL);
        /* Create thumbnail */
        artos->voice_art.history[idx].thumbnail = cairo_surface_create_similar(
            surf, CAIRO_CONTENT_COLOR_ALPHA, 64, 64);
        cairo_t *thumb_cr = cairo_create(artos->voice_art.history[idx].thumbnail);
        cairo_scale(thumb_cr, 64.0 / artos->voice_art.width, 64.0 / artos->voice_art.height);
        cairo_set_source_surface(thumb_cr, surf, 0, 0);
        cairo_paint(thumb_cr);
        cairo_destroy(thumb_cr);
    }

    artos->voice_art.generating = 0;
    artos->voice_art.progress = 1.0;

    if (artos->voiceart_progress_bar) {
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(artos->voiceart_progress_bar), 1.0);
    }

    if (artos->voiceart_status_label) {
        gtk_label_set_text(GTK_LABEL(artos->voiceart_status_label), "Image generated! Click Apply to use.");
    }

    /* Redraw preview */
    if (artos->voiceart_preview_area) {
        gtk_widget_queue_draw(artos->voiceart_preview_area);
    }
}

/* Apply generated image to canvas */
void artos_voiceart_apply_to_canvas(phantom_artos_t *artos, int image_index) {
    if (!artos || !artos->document) return;
    if (image_index < 0 || image_index >= artos->voice_art.generated_count) return;

    cairo_surface_t *src = artos->voice_art.generated[image_index];
    if (!src) return;

    artos_layer_t *layer = artos_layer_get_active(artos->document);
    if (!layer || layer->locked) return;

    cairo_t *cr = cairo_create(layer->surface);

    /* Scale to fit canvas */
    int src_w = cairo_image_surface_get_width(src);
    int src_h = cairo_image_surface_get_height(src);
    double scale_x = (double)layer->width / src_w;
    double scale_y = (double)layer->height / src_h;
    double scale = fmin(scale_x, scale_y);

    double ox = (layer->width - src_w * scale) / 2;
    double oy = (layer->height - src_h * scale) / 2;

    cairo_translate(cr, ox, oy);
    cairo_scale(cr, scale, scale);
    cairo_set_source_surface(cr, src, 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);

    artos->document->composite_dirty = 1;
    artos->document->modified = 1;

    if (artos->canvas_area) {
        gtk_widget_queue_draw(artos->canvas_area);
    }

    if (artos->voiceart_status_label) {
        gtk_label_set_text(GTK_LABEL(artos->voiceart_status_label), "Image applied to canvas!");
    }
}

/* Set art style */
void artos_voiceart_set_style(phantom_artos_t *artos, const char *style) {
    if (!artos || !style) return;
    strncpy(artos->voice_art.style_preset, style, sizeof(artos->voice_art.style_preset) - 1);
}

/* Set creativity level */
void artos_voiceart_set_creativity(phantom_artos_t *artos, double creativity) {
    if (!artos) return;
    artos->voice_art.creativity = fmax(0.0, fmin(1.0, creativity));
}

/* UI Callbacks for Voice-to-Art panel */
static void on_voiceart_toggle(GtkToggleButton *button, phantom_artos_t *artos) {
    artos_voiceart_enable(artos, gtk_toggle_button_get_active(button));
}

static void on_voiceart_listen_clicked(GtkButton *button, phantom_artos_t *artos) {
    (void)button;
    if (artos->voice_art.listening) {
        artos_voiceart_stop_listening(artos);
    } else {
        artos_voiceart_start_listening(artos);
    }
}

static void on_voiceart_generate_clicked(GtkButton *button, phantom_artos_t *artos) {
    (void)button;
    if (artos->voiceart_transcript) {
        GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(artos->voiceart_transcript));
        GtkTextIter start, end;
        gtk_text_buffer_get_bounds(buf, &start, &end);
        char *text = gtk_text_buffer_get_text(buf, &start, &end, FALSE);
        if (text && strlen(text) > 0) {
            artos_voiceart_generate(artos, text);
        }
        g_free(text);
    }
}

static void on_voiceart_apply_clicked(GtkButton *button, phantom_artos_t *artos) {
    (void)button;
    artos_voiceart_apply_to_canvas(artos, artos->voice_art.selected_image);
}

static void on_voiceart_style_changed(GtkComboBox *combo, phantom_artos_t *artos) {
    const char *styles[] = {"realistic", "cartoon", "abstract", "watercolor", "sketch", "pixel"};
    int idx = gtk_combo_box_get_active(combo);
    if (idx >= 0 && idx < 6) {
        artos_voiceart_set_style(artos, styles[idx]);
    }
}

static void on_voiceart_creativity_changed(GtkRange *range, phantom_artos_t *artos) {
    artos_voiceart_set_creativity(artos, gtk_range_get_value(range));
}

/* Draw voice-to-art preview */
static gboolean on_voiceart_preview_draw(GtkWidget *widget, cairo_t *cr, phantom_artos_t *artos) {
    int width = gtk_widget_get_allocated_width(widget);
    int height = gtk_widget_get_allocated_height(widget);

    /* Background */
    cairo_set_source_rgb(cr, 0.1, 0.1, 0.15);
    cairo_paint(cr);

    if (artos->voice_art.generated_count > 0 && artos->voice_art.generated[0]) {
        cairo_surface_t *src = artos->voice_art.generated[artos->voice_art.selected_image];
        int src_w = cairo_image_surface_get_width(src);
        int src_h = cairo_image_surface_get_height(src);

        double scale = fmin((double)width / src_w, (double)height / src_h);
        double ox = (width - src_w * scale) / 2;
        double oy = (height - src_h * scale) / 2;

        cairo_translate(cr, ox, oy);
        cairo_scale(cr, scale, scale);
        cairo_set_source_surface(cr, src, 0, 0);
        cairo_paint(cr);
    } else {
        /* Placeholder */
        cairo_set_source_rgb(cr, 0.5, 0.5, 0.5);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 14);

        cairo_text_extents_t extents;
        const char *text = "Describe and generate art";
        cairo_text_extents(cr, text, &extents);
        cairo_move_to(cr, (width - extents.width) / 2, height / 2);
        cairo_show_text(cr, text);
    }

    return FALSE;
}

/* Create Voice-to-Art panel */
GtkWidget *artos_create_voiceart_panel(phantom_artos_t *artos) {
    GtkWidget *frame = gtk_frame_new("🎨 Voice-to-Art");
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(box), 10);

    /* Initialize */
    artos_voiceart_init(artos);

    /* Enable toggle */
    artos->voiceart_toggle = gtk_toggle_button_new_with_label("Enable Voice-to-Art");
    g_signal_connect(artos->voiceart_toggle, "toggled", G_CALLBACK(on_voiceart_toggle), artos);
    gtk_box_pack_start(GTK_BOX(box), artos->voiceart_toggle, FALSE, FALSE, 0);

    /* Listen button */
    artos->voiceart_listen_btn = gtk_button_new_with_label("🎤 Listen");
    g_signal_connect(artos->voiceart_listen_btn, "clicked", G_CALLBACK(on_voiceart_listen_clicked), artos);
    gtk_box_pack_start(GTK_BOX(box), artos->voiceart_listen_btn, FALSE, FALSE, 0);

    /* Transcript text area */
    GtkWidget *transcript_label = gtk_label_new("Describe your image:");
    gtk_widget_set_halign(transcript_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), transcript_label, FALSE, FALSE, 0);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll), 60);
    artos->voiceart_transcript = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(artos->voiceart_transcript), GTK_WRAP_WORD);
    gtk_container_add(GTK_CONTAINER(scroll), artos->voiceart_transcript);
    gtk_box_pack_start(GTK_BOX(box), scroll, FALSE, FALSE, 0);

    /* Style selector */
    GtkWidget *style_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget *style_label = gtk_label_new("Style:");
    gtk_box_pack_start(GTK_BOX(style_box), style_label, FALSE, FALSE, 0);

    artos->voiceart_style_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(artos->voiceart_style_combo), "Realistic");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(artos->voiceart_style_combo), "Cartoon");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(artos->voiceart_style_combo), "Abstract");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(artos->voiceart_style_combo), "Watercolor");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(artos->voiceart_style_combo), "Sketch");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(artos->voiceart_style_combo), "Pixel Art");
    gtk_combo_box_set_active(GTK_COMBO_BOX(artos->voiceart_style_combo), 0);
    g_signal_connect(artos->voiceart_style_combo, "changed", G_CALLBACK(on_voiceart_style_changed), artos);
    gtk_box_pack_start(GTK_BOX(style_box), artos->voiceart_style_combo, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), style_box, FALSE, FALSE, 0);

    /* Creativity slider */
    GtkWidget *creat_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget *creat_label = gtk_label_new("Creativity:");
    gtk_box_pack_start(GTK_BOX(creat_box), creat_label, FALSE, FALSE, 0);
    artos->voiceart_creativity = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 1, 0.1);
    gtk_range_set_value(GTK_RANGE(artos->voiceart_creativity), 0.7);
    g_signal_connect(artos->voiceart_creativity, "value-changed", G_CALLBACK(on_voiceart_creativity_changed), artos);
    gtk_box_pack_start(GTK_BOX(creat_box), artos->voiceart_creativity, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), creat_box, FALSE, FALSE, 0);

    /* Generate button */
    artos->voiceart_generate_btn = gtk_button_new_with_label("✨ Generate Image");
    g_signal_connect(artos->voiceart_generate_btn, "clicked", G_CALLBACK(on_voiceart_generate_clicked), artos);
    gtk_box_pack_start(GTK_BOX(box), artos->voiceart_generate_btn, FALSE, FALSE, 0);

    /* Progress bar */
    artos->voiceart_progress_bar = gtk_progress_bar_new();
    gtk_box_pack_start(GTK_BOX(box), artos->voiceart_progress_bar, FALSE, FALSE, 0);

    /* Preview area */
    GtkWidget *preview_frame = gtk_frame_new("Preview");
    artos->voiceart_preview_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(artos->voiceart_preview_area, 200, 150);
    g_signal_connect(artos->voiceart_preview_area, "draw", G_CALLBACK(on_voiceart_preview_draw), artos);
    gtk_container_add(GTK_CONTAINER(preview_frame), artos->voiceart_preview_area);
    gtk_box_pack_start(GTK_BOX(box), preview_frame, TRUE, TRUE, 5);

    /* Apply button */
    artos->voiceart_apply_btn = gtk_button_new_with_label("📋 Apply to Canvas");
    g_signal_connect(artos->voiceart_apply_btn, "clicked", G_CALLBACK(on_voiceart_apply_clicked), artos);
    gtk_box_pack_start(GTK_BOX(box), artos->voiceart_apply_btn, FALSE, FALSE, 0);

    /* Status label */
    artos->voiceart_status_label = gtk_label_new("Voice-to-Art Ready");
    gtk_widget_set_halign(artos->voiceart_status_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), artos->voiceart_status_label, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(frame), box);
    artos->voiceart_panel = frame;

    return frame;
}

/* ==============================================================================
 * Collaborative Canvas Implementation
 * ============================================================================== */

/* Generate random user ID */
static uint32_t collab_generate_user_id(void) {
    srand((unsigned int)time(NULL) ^ (unsigned int)getpid());
    return (uint32_t)rand();
}

/* Generate random session ID */
static void collab_generate_session_id(char *buf, size_t len) {
    const char *chars = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";  /* Avoid confusing chars */
    srand((unsigned int)time(NULL) ^ (unsigned int)getpid());

    for (size_t i = 0; i < len - 1 && i < 8; i++) {
        buf[i] = chars[rand() % strlen(chars)];
    }
    buf[len > 8 ? 8 : len - 1] = '\0';
}

/* Generate random cursor color */
static void collab_random_color(artos_color_t *color) {
    srand((unsigned int)time(NULL) ^ (unsigned int)getpid());
    double hue = (rand() % 360) / 360.0;

    /* HSV to RGB (saturation=0.7, value=0.9) */
    double s = 0.7, v = 0.9;
    double c = v * s;
    double x = c * (1 - fabs(fmod(hue * 6, 2) - 1));
    double m = v - c;

    double r, g, b;
    int h_i = (int)(hue * 6) % 6;
    switch (h_i) {
        case 0: r = c; g = x; b = 0; break;
        case 1: r = x; g = c; b = 0; break;
        case 2: r = 0; g = c; b = x; break;
        case 3: r = 0; g = x; b = c; break;
        case 4: r = x; g = 0; b = c; break;
        default: r = c; g = 0; b = x; break;
    }

    color->r = r + m;
    color->g = g + m;
    color->b = b + m;
    color->a = 1.0;
}

/* Initialize collaboration */
int artos_collab_init(phantom_artos_t *artos) {
    if (!artos) return 0;

    memset(&artos->collab, 0, sizeof(artos_collab_t));
    artos->collab.local_user_id = collab_generate_user_id();
    strcpy(artos->collab.local_name, "Artist");
    artos->collab.server_port = 7777;
    strcpy(artos->collab.server_host, "localhost");
    artos->collab.socket_fd = -1;
    artos->collab.use_crdt = 1;

    return 1;
}

/* Cleanup collaboration */
void artos_collab_cleanup(phantom_artos_t *artos) {
    if (!artos) return;

    artos_collab_leave_session(artos);

    /* Free users list */
    artos_collab_user_t *user = artos->collab.users;
    while (user) {
        artos_collab_user_t *next = user->next;
        free(user);
        user = next;
    }
    artos->collab.users = NULL;

    /* Free message queues */
    artos_collab_msg_t *msg = artos->collab.outgoing;
    while (msg) {
        artos_collab_msg_t *next = msg->next;
        if (msg->op == ARTOS_COLLAB_OP_STROKE && msg->data.stroke.points) {
            free(msg->data.stroke.points);
        }
        free(msg);
        msg = next;
    }
    artos->collab.outgoing = NULL;

    msg = artos->collab.incoming;
    while (msg) {
        artos_collab_msg_t *next = msg->next;
        if (msg->op == ARTOS_COLLAB_OP_STROKE && msg->data.stroke.points) {
            free(msg->data.stroke.points);
        }
        free(msg);
        msg = next;
    }
    artos->collab.incoming = NULL;
}

/* Enable/disable collaboration */
void artos_collab_enable(phantom_artos_t *artos, int enable) {
    if (!artos) return;
    artos->collab.enabled = enable;

    if (artos->collab_status_label) {
        gtk_label_set_text(GTK_LABEL(artos->collab_status_label),
                           enable ? "Collaboration Ready" : "Collaboration Disabled");
    }
}

/* Host a new session */
int artos_collab_host_session(phantom_artos_t *artos, const char *name, const char *password) {
    if (!artos || !artos->collab.enabled) return 0;

    /* Generate session ID */
    collab_generate_session_id(artos->collab.session_id, sizeof(artos->collab.session_id));

    if (name) {
        strncpy(artos->collab.session_name, name, sizeof(artos->collab.session_name) - 1);
    } else {
        snprintf(artos->collab.session_name, sizeof(artos->collab.session_name),
                 "%s's Canvas", artos->collab.local_name);
    }

    if (password) {
        strncpy(artos->collab.password, password, sizeof(artos->collab.password) - 1);
    }

    artos->collab.is_host = 1;
    artos->collab.connected = 1;
    artos->collab.user_count = 1;

    /* Add self to users list */
    artos_collab_user_t *self = calloc(1, sizeof(artos_collab_user_t));
    self->user_id = artos->collab.local_user_id;
    strncpy(self->name, artos->collab.local_name, sizeof(self->name) - 1);
    collab_random_color(&self->cursor_color);
    self->connected = 1;
    self->last_seen = time(NULL);
    self->next = artos->collab.users;
    artos->collab.users = self;

    if (artos->collab_status_label) {
        char status[256];
        snprintf(status, sizeof(status), "Hosting: %s (Code: %s)",
                 artos->collab.session_name, artos->collab.session_id);
        gtk_label_set_text(GTK_LABEL(artos->collab_status_label), status);
    }

    /* Update users list in UI */
    if (artos->collab_users_store) {
        GtkTreeIter iter;
        gtk_list_store_append(artos->collab_users_store, &iter);
        gtk_list_store_set(artos->collab_users_store, &iter,
                           0, self->name,
                           1, "Host",
                           2, "Connected",
                           -1);
    }

    /* TODO: Start actual network server for P2P or relay connections */

    return 1;
}

/* Join an existing session */
int artos_collab_join_session(phantom_artos_t *artos, const char *session_id, const char *password) {
    if (!artos || !artos->collab.enabled || !session_id) return 0;

    strncpy(artos->collab.session_id, session_id, sizeof(artos->collab.session_id) - 1);

    if (password) {
        strncpy(artos->collab.password, password, sizeof(artos->collab.password) - 1);
    }

    artos->collab.is_host = 0;
    artos->collab.connected = 1;

    /* Add self to users */
    artos_collab_user_t *self = calloc(1, sizeof(artos_collab_user_t));
    self->user_id = artos->collab.local_user_id;
    strncpy(self->name, artos->collab.local_name, sizeof(self->name) - 1);
    collab_random_color(&self->cursor_color);
    self->connected = 1;
    self->last_seen = time(NULL);
    self->next = artos->collab.users;
    artos->collab.users = self;
    artos->collab.user_count = 1;

    if (artos->collab_status_label) {
        char status[256];
        snprintf(status, sizeof(status), "Joined session: %s", session_id);
        gtk_label_set_text(GTK_LABEL(artos->collab_status_label), status);
    }

    /* TODO: Connect to remote host */

    return 1;
}

/* Leave current session */
void artos_collab_leave_session(phantom_artos_t *artos) {
    if (!artos) return;

    if (artos->collab.socket_fd >= 0) {
        close(artos->collab.socket_fd);
        artos->collab.socket_fd = -1;
    }

    if (artos->collab.socket_watch) {
        g_source_remove(artos->collab.socket_watch);
        artos->collab.socket_watch = 0;
    }

    artos->collab.connected = 0;
    artos->collab.is_host = 0;
    memset(artos->collab.session_id, 0, sizeof(artos->collab.session_id));

    /* Clear users list */
    artos_collab_user_t *user = artos->collab.users;
    while (user) {
        artos_collab_user_t *next = user->next;
        free(user);
        user = next;
    }
    artos->collab.users = NULL;
    artos->collab.user_count = 0;

    if (artos->collab_users_store) {
        gtk_list_store_clear(artos->collab_users_store);
    }

    if (artos->collab_status_label) {
        gtk_label_set_text(GTK_LABEL(artos->collab_status_label), "Disconnected");
    }
}

/* Send stroke to collaborators */
void artos_collab_send_stroke(phantom_artos_t *artos, artos_ai_point_t *points, int count) {
    if (!artos || !artos->collab.connected || !points || count <= 0) return;

    artos_collab_msg_t *msg = calloc(1, sizeof(artos_collab_msg_t));
    msg->op = ARTOS_COLLAB_OP_STROKE;
    msg->user_id = artos->collab.local_user_id;
    msg->timestamp = (uint64_t)time(NULL) * 1000;
    msg->seq_num = ++artos->collab.local_seq;

    msg->data.stroke.points = malloc(sizeof(artos_ai_point_t) * count);
    memcpy(msg->data.stroke.points, points, sizeof(artos_ai_point_t) * count);
    msg->data.stroke.point_count = count;
    msg->data.stroke.color = artos->foreground_color;
    msg->data.stroke.brush_size = artos->current_brush.size;
    msg->data.stroke.layer_index = artos->document ? artos->document->active_layer : 0;

    /* Add to outgoing queue */
    msg->next = artos->collab.outgoing;
    artos->collab.outgoing = msg;
    artos->collab.ops_sent++;

    /* TODO: Actually send over network */
}

/* Send cursor position */
void artos_collab_send_cursor(phantom_artos_t *artos, double x, double y) {
    if (!artos || !artos->collab.connected) return;

    /* TODO: Send cursor update to collaborators */
    /* For now, just update local user */
    artos_collab_user_t *self = artos->collab.users;
    while (self) {
        if (self->user_id == artos->collab.local_user_id) {
            self->cursor_x = x;
            self->cursor_y = y;
            break;
        }
        self = self->next;
    }
}

/* Send chat message */
void artos_collab_send_chat(phantom_artos_t *artos, const char *message) {
    if (!artos || !artos->collab.connected || !message) return;

    /* Add to local chat history */
    if (artos->collab.chat_count < 100) {
        int idx = artos->collab.chat_count++;
        artos->collab.chat_history[idx].user_id = artos->collab.local_user_id;
        strncpy(artos->collab.chat_history[idx].name, artos->collab.local_name,
                sizeof(artos->collab.chat_history[idx].name) - 1);
        strncpy(artos->collab.chat_history[idx].message, message,
                sizeof(artos->collab.chat_history[idx].message) - 1);
        artos->collab.chat_history[idx].timestamp = time(NULL);
    }

    /* Update chat view */
    if (artos->collab_chat_buffer) {
        GtkTextIter end;
        gtk_text_buffer_get_end_iter(artos->collab_chat_buffer, &end);

        char line[384];
        snprintf(line, sizeof(line), "%s: %s\n", artos->collab.local_name, message);
        gtk_text_buffer_insert(artos->collab_chat_buffer, &end, line, -1);
    }

    /* TODO: Send to collaborators */
}

/* Set username */
void artos_collab_set_username(phantom_artos_t *artos, const char *name) {
    if (!artos || !name) return;
    strncpy(artos->collab.local_name, name, sizeof(artos->collab.local_name) - 1);

    /* Update in users list */
    artos_collab_user_t *self = artos->collab.users;
    while (self) {
        if (self->user_id == artos->collab.local_user_id) {
            strncpy(self->name, name, sizeof(self->name) - 1);
            break;
        }
        self = self->next;
    }
}

/* Get users list */
artos_collab_user_t *artos_collab_get_users(phantom_artos_t *artos) {
    return artos ? artos->collab.users : NULL;
}

/* UI Callbacks for Collaboration panel */
static void on_collab_toggle(GtkToggleButton *button, phantom_artos_t *artos) {
    artos_collab_enable(artos, gtk_toggle_button_get_active(button));
}

static void on_collab_host_clicked(GtkButton *button, phantom_artos_t *artos) {
    (void)button;
    if (artos->collab.connected) {
        artos_collab_leave_session(artos);
    }
    artos_collab_host_session(artos, NULL, NULL);
}

static void on_collab_join_clicked(GtkButton *button, phantom_artos_t *artos) {
    (void)button;
    if (artos->collab_session_entry) {
        const char *session_id = gtk_entry_get_text(GTK_ENTRY(artos->collab_session_entry));
        if (session_id && strlen(session_id) > 0) {
            if (artos->collab.connected) {
                artos_collab_leave_session(artos);
            }
            artos_collab_join_session(artos, session_id, NULL);
        }
    }
}

static void on_collab_send_clicked(GtkButton *button, phantom_artos_t *artos) {
    (void)button;
    if (artos->collab_chat_entry) {
        const char *msg = gtk_entry_get_text(GTK_ENTRY(artos->collab_chat_entry));
        if (msg && strlen(msg) > 0) {
            artos_collab_send_chat(artos, msg);
            gtk_entry_set_text(GTK_ENTRY(artos->collab_chat_entry), "");
        }
    }
}

static void on_collab_name_changed(GtkEntry *entry, phantom_artos_t *artos) {
    const char *name = gtk_entry_get_text(entry);
    if (name && strlen(name) > 0) {
        artos_collab_set_username(artos, name);
    }
}

/* Create Collaboration panel */
GtkWidget *artos_create_collab_panel(phantom_artos_t *artos) {
    GtkWidget *frame = gtk_frame_new("👥 Collaborative Canvas");
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(box), 10);

    /* Initialize */
    artos_collab_init(artos);

    /* Enable toggle */
    artos->collab_toggle = gtk_toggle_button_new_with_label("Enable Collaboration");
    g_signal_connect(artos->collab_toggle, "toggled", G_CALLBACK(on_collab_toggle), artos);
    gtk_box_pack_start(GTK_BOX(box), artos->collab_toggle, FALSE, FALSE, 0);

    /* Username entry */
    GtkWidget *name_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget *name_label = gtk_label_new("Your name:");
    gtk_box_pack_start(GTK_BOX(name_box), name_label, FALSE, FALSE, 0);
    artos->collab_name_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(artos->collab_name_entry), "Artist");
    g_signal_connect(artos->collab_name_entry, "changed", G_CALLBACK(on_collab_name_changed), artos);
    gtk_box_pack_start(GTK_BOX(name_box), artos->collab_name_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), name_box, FALSE, FALSE, 0);

    /* Separator */
    gtk_box_pack_start(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 5);

    /* Host/Join buttons */
    GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    artos->collab_host_btn = gtk_button_new_with_label("🏠 Host Session");
    artos->collab_join_btn = gtk_button_new_with_label("🔗 Join Session");
    g_signal_connect(artos->collab_host_btn, "clicked", G_CALLBACK(on_collab_host_clicked), artos);
    g_signal_connect(artos->collab_join_btn, "clicked", G_CALLBACK(on_collab_join_clicked), artos);
    gtk_box_pack_start(GTK_BOX(btn_box), artos->collab_host_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(btn_box), artos->collab_join_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), btn_box, FALSE, FALSE, 0);

    /* Session ID entry (for joining) */
    GtkWidget *session_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget *session_label = gtk_label_new("Session code:");
    gtk_box_pack_start(GTK_BOX(session_box), session_label, FALSE, FALSE, 0);
    artos->collab_session_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(artos->collab_session_entry), "Enter code...");
    gtk_entry_set_max_length(GTK_ENTRY(artos->collab_session_entry), 8);
    gtk_box_pack_start(GTK_BOX(session_box), artos->collab_session_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), session_box, FALSE, FALSE, 0);

    /* Status label */
    artos->collab_status_label = gtk_label_new("Not connected");
    gtk_widget_set_halign(artos->collab_status_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), artos->collab_status_label, FALSE, FALSE, 0);

    /* Latency label */
    artos->collab_latency_label = gtk_label_new("Latency: --");
    gtk_widget_set_halign(artos->collab_latency_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), artos->collab_latency_label, FALSE, FALSE, 0);

    /* Separator */
    gtk_box_pack_start(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 5);

    /* Connected users list */
    GtkWidget *users_label = gtk_label_new("Connected users:");
    gtk_widget_set_halign(users_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), users_label, FALSE, FALSE, 0);

    artos->collab_users_store = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    artos->collab_users_list = gtk_tree_view_new_with_model(GTK_TREE_MODEL(artos->collab_users_store));

    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes("Name", renderer, "text", 0, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(artos->collab_users_list), col);
    col = gtk_tree_view_column_new_with_attributes("Role", renderer, "text", 1, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(artos->collab_users_list), col);
    col = gtk_tree_view_column_new_with_attributes("Status", renderer, "text", 2, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(artos->collab_users_list), col);

    GtkWidget *users_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(users_scroll), 60);
    gtk_container_add(GTK_CONTAINER(users_scroll), artos->collab_users_list);
    gtk_box_pack_start(GTK_BOX(box), users_scroll, FALSE, FALSE, 0);

    /* Separator */
    gtk_box_pack_start(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 5);

    /* Chat */
    GtkWidget *chat_label = gtk_label_new("Chat:");
    gtk_widget_set_halign(chat_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), chat_label, FALSE, FALSE, 0);

    artos->collab_chat_buffer = gtk_text_buffer_new(NULL);
    artos->collab_chat_view = gtk_text_view_new_with_buffer(artos->collab_chat_buffer);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(artos->collab_chat_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(artos->collab_chat_view), GTK_WRAP_WORD);

    GtkWidget *chat_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(chat_scroll), 80);
    gtk_container_add(GTK_CONTAINER(chat_scroll), artos->collab_chat_view);
    gtk_box_pack_start(GTK_BOX(box), chat_scroll, TRUE, TRUE, 0);

    /* Chat input */
    GtkWidget *chat_input_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    artos->collab_chat_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(artos->collab_chat_entry), "Type message...");
    gtk_box_pack_start(GTK_BOX(chat_input_box), artos->collab_chat_entry, TRUE, TRUE, 0);

    artos->collab_send_btn = gtk_button_new_with_label("Send");
    g_signal_connect(artos->collab_send_btn, "clicked", G_CALLBACK(on_collab_send_clicked), artos);
    gtk_box_pack_start(GTK_BOX(chat_input_box), artos->collab_send_btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), chat_input_box, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(frame), box);
    artos->collab_panel = frame;

    return frame;
}

/* ==============================================================================
 * DrawNet - Real-time Multi-User Drawing Network
 * ==============================================================================
 *
 * DrawNet provides peer-to-peer collaborative drawing with:
 * - Local network discovery via mDNS/Avahi
 * - Direct TCP/UDP connections for drawing data
 * - Real-time cursor and stroke synchronization
 * - Canvas state transfer for late joiners
 */

/* Forward declarations for DrawNet */
static void on_drawnet_toggle(GtkToggleButton *button, phantom_artos_t *artos);
static void on_drawnet_host_clicked(GtkButton *button, phantom_artos_t *artos);
static void on_drawnet_join_clicked(GtkButton *button, phantom_artos_t *artos);
static void on_drawnet_scan_clicked(GtkButton *button, phantom_artos_t *artos);
static void on_drawnet_send_clicked(GtkButton *button, phantom_artos_t *artos);
static void on_drawnet_name_changed(GtkEntry *entry, phantom_artos_t *artos);
static void on_drawnet_sync_changed(GtkComboBox *combo, phantom_artos_t *artos);
static void on_drawnet_cursor_toggled(GtkToggleButton *button, phantom_artos_t *artos);
static void on_drawnet_discovered_select(GtkTreeSelection *sel, phantom_artos_t *artos);
static gboolean drawnet_ping_timer(gpointer data);
static gboolean drawnet_cursor_timer(gpointer data);
static void drawnet_update_status(phantom_artos_t *artos);
static void drawnet_update_peers_list(phantom_artos_t *artos);
static void drawnet_add_chat_message(phantom_artos_t *artos, const char *name, const char *msg);

/* Generate random session code */
static void drawnet_generate_session_code(char *code, size_t len) {
    static const char chars[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    for (size_t i = 0; i < len - 1; i++) {
        code[i] = chars[rand() % (sizeof(chars) - 1)];
    }
    code[len - 1] = '\0';
}

/* Generate random peer ID */
static uint32_t drawnet_generate_peer_id(void) {
    return (uint32_t)rand() ^ ((uint32_t)rand() << 16) ^ (uint32_t)time(NULL);
}

/* Generate random cursor color */
static void drawnet_generate_cursor_color(artos_color_t *color) {
    double h = (rand() % 360);
    artos_color_from_hsv(color, h, 0.8, 0.9);
    color->a = 1.0;
}

/* Get state string for display */
const char *artos_drawnet_get_state_string(artos_drawnet_state_t state) {
    switch (state) {
        case DRAWNET_STATE_DISCONNECTED: return "Disconnected";
        case DRAWNET_STATE_DISCOVERING:  return "Scanning...";
        case DRAWNET_STATE_CONNECTING:   return "Connecting...";
        case DRAWNET_STATE_CONNECTED:    return "Connected";
        case DRAWNET_STATE_SYNCING:      return "Syncing canvas...";
        case DRAWNET_STATE_ERROR:        return "Error";
        default: return "Unknown";
    }
}

/* Initialize DrawNet */
int artos_drawnet_init(phantom_artos_t *artos) {
    if (!artos) return -1;

    memset(&artos->drawnet, 0, sizeof(artos_drawnet_t));

    /* Initialize mutex */
    g_mutex_init(&artos->drawnet.queue_mutex);

    /* Generate local identity */
    artos->drawnet.local_id = drawnet_generate_peer_id();
    strncpy(artos->drawnet.local_name, "Artist", sizeof(artos->drawnet.local_name) - 1);
    drawnet_generate_cursor_color(&artos->drawnet.local_cursor_color);

    /* Default configuration */
    artos->drawnet.config.sync_mode = DRAWNET_SYNC_REALTIME;
    artos->drawnet.config.sync_interval_ms = 100;
    artos->drawnet.config.default_perm = DRAWNET_PERM_DRAW;
    artos->drawnet.config.share_cursor = 1;
    artos->drawnet.config.share_tool = 1;
    artos->drawnet.config.compress_canvas = 1;
    artos->drawnet.config.max_peers = 16;

    /* Initialize socket descriptors */
    artos->drawnet.tcp_socket = -1;
    artos->drawnet.udp_socket = -1;
    artos->drawnet.listen_socket = -1;
    artos->drawnet.listen_port = DRAWNET_DEFAULT_PORT;

    artos->drawnet.state = DRAWNET_STATE_DISCONNECTED;

    /* Enable Governor checks by default */
    artos->drawnet.governor_checks = 1;
    artos->drawnet.governor_approved = 0;

    return 0;
}

/* Set Governor for capability checking */
void artos_drawnet_set_governor(phantom_artos_t *artos, void *governor) {
    if (!artos) return;
    artos->drawnet.governor = governor;
    artos->drawnet.governor_checks = (governor != NULL) ? 1 : 0;
    printf("[DrawNet] Governor %s for capability checking\n",
           governor ? "enabled" : "disabled");
}

/* Check network capability with Governor */
int artos_drawnet_check_capability(phantom_artos_t *artos, const char *operation) {
    if (!artos) return 0;

    /* If Governor checks are disabled, allow */
    if (!artos->drawnet.governor_checks || !artos->drawnet.governor) {
        printf("[DrawNet] Governor checks disabled, allowing %s\n", operation);
        return 1;
    }

    /* If already approved for this session, allow */
    if (artos->drawnet.governor_approved) {
        return 1;
    }

    /* Build code snippet for Governor evaluation */
    char code[512];
    snprintf(code, sizeof(code),
             "/* DrawNet Network Operation */\n"
             "drawnet_%s();\n"
             "/* Requires: CAP_NETWORK for peer-to-peer drawing */",
             operation);

    /* Create Governor evaluation request */
    governor_eval_request_t req = {0};
    governor_eval_response_t resp = {0};

    req.code_ptr = code;
    req.code_size = strlen(code);
    req.declared_caps = CAP_NETWORK;
    strncpy(req.name, "DrawNet", sizeof(req.name) - 1);
    snprintf(req.description, sizeof(req.description),
             "DrawNet collaborative drawing: %s", operation);

    /* Evaluate with Governor */
    phantom_governor_t *gov = (phantom_governor_t *)artos->drawnet.governor;
    int err = governor_evaluate_code(gov, &req, &resp);

    if (err != 0) {
        printf("[DrawNet] Governor evaluation error for %s\n", operation);
        snprintf(artos->drawnet.last_error, sizeof(artos->drawnet.last_error),
                 "Governor evaluation failed");
        return 0;
    }

    if (resp.decision != GOVERNOR_APPROVE) {
        printf("[DrawNet] Governor denied network operation: %s\n", operation);
        printf("[DrawNet] Reason: %s\n", resp.decline_reason);
        snprintf(artos->drawnet.last_error, sizeof(artos->drawnet.last_error),
                 "Governor denied: %.200s", resp.decline_reason);

        /* Update UI to show denial */
        if (artos->drawnet_status_label) {
            char status[256];
            snprintf(status, sizeof(status), "Denied: %.200s", resp.decline_reason);
            gtk_label_set_text(GTK_LABEL(artos->drawnet_status_label), status);
        }

        return 0;
    }

    /* Approved! Cache the approval for this session */
    artos->drawnet.governor_approved = 1;
    strncpy(artos->drawnet.governor_approval_scope, operation,
            sizeof(artos->drawnet.governor_approval_scope) - 1);

    printf("[DrawNet] Governor approved network capability for %s\n", operation);
    printf("[DrawNet] Granted capabilities: %s\n", resp.summary);

    /* Log to Governor history */
    governor_log_decision(gov, &req, &resp);

    return 1;
}

/* Cleanup DrawNet */
void artos_drawnet_cleanup(phantom_artos_t *artos) {
    if (!artos) return;

    /* Leave any active session */
    artos_drawnet_leave_session(artos);

    /* Stop timers */
    if (artos->drawnet.ping_timer) {
        g_source_remove(artos->drawnet.ping_timer);
        artos->drawnet.ping_timer = 0;
    }
    if (artos->drawnet.cursor_timer) {
        g_source_remove(artos->drawnet.cursor_timer);
        artos->drawnet.cursor_timer = 0;
    }
    if (artos->drawnet.discovery_timer) {
        g_source_remove(artos->drawnet.discovery_timer);
        artos->drawnet.discovery_timer = 0;
    }

    /* Free peer list */
    artos_drawnet_peer_t *peer = artos->drawnet.peers;
    while (peer) {
        artos_drawnet_peer_t *next = peer->next;
        if (peer->avatar) {
            cairo_surface_destroy(peer->avatar);
        }
        free(peer);
        peer = next;
    }
    artos->drawnet.peers = NULL;
    artos->drawnet.peer_count = 0;

    /* Free packet queues */
    artos_drawnet_packet_t *pkt = artos->drawnet.outgoing;
    while (pkt) {
        artos_drawnet_packet_t *next = pkt->next;
        if (pkt->payload) free(pkt->payload);
        free(pkt);
        pkt = next;
    }
    artos->drawnet.outgoing = NULL;

    pkt = artos->drawnet.incoming;
    while (pkt) {
        artos_drawnet_packet_t *next = pkt->next;
        if (pkt->payload) free(pkt->payload);
        free(pkt);
        pkt = next;
    }
    artos->drawnet.incoming = NULL;

    /* Free canvas buffer */
    if (artos->drawnet.canvas_buffer) {
        free(artos->drawnet.canvas_buffer);
        artos->drawnet.canvas_buffer = NULL;
    }

    /* Clear mutex */
    g_mutex_clear(&artos->drawnet.queue_mutex);
}

/* ==============================================================================
 * DrawNet Network Infrastructure
 * ============================================================================== */

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>

/* Forward declarations for network handlers */
static gboolean drawnet_on_accept(GIOChannel *channel, GIOCondition cond, gpointer data);
static gboolean drawnet_on_receive(GIOChannel *channel, GIOCondition cond, gpointer data);
static void drawnet_handle_packet(phantom_artos_t *artos, artos_drawnet_peer_t *peer,
                                   drawnet_wire_header_t *header, void *payload);

/* Convert artos_color_t to RGBA uint32 */
static uint32_t drawnet_color_to_rgba(const artos_color_t *color) {
    if (!color) return 0;
    return ((uint32_t)(color->r) << 24) |
           ((uint32_t)(color->g) << 16) |
           ((uint32_t)(color->b) << 8) |
           ((uint32_t)(color->a * 255));
}

/* Convert RGBA uint32 to artos_color_t */
static void drawnet_rgba_to_color(uint32_t rgba, artos_color_t *color) {
    if (!color) return;
    color->r = (rgba >> 24) & 0xFF;
    color->g = (rgba >> 16) & 0xFF;
    color->b = (rgba >> 8) & 0xFF;
    color->a = (rgba & 0xFF) / 255.0;
}

/* Find peer by ID */
static artos_drawnet_peer_t *drawnet_find_peer(phantom_artos_t *artos, uint32_t peer_id) {
    if (!artos) return NULL;
    artos_drawnet_peer_t *peer = artos->drawnet.peers;
    while (peer) {
        if (peer->peer_id == peer_id) return peer;
        peer = peer->next;
    }
    return NULL;
}

/* Find peer by socket */
static artos_drawnet_peer_t *drawnet_find_peer_by_socket(phantom_artos_t *artos, int socket_fd) {
    if (!artos || socket_fd < 0) return NULL;
    artos_drawnet_peer_t *peer = artos->drawnet.peers;
    while (peer) {
        if (peer->socket_fd == socket_fd) return peer;
        peer = peer->next;
    }
    return NULL;
}

/* Remove peer from list */
static void drawnet_remove_peer(phantom_artos_t *artos, uint32_t peer_id) {
    if (!artos) return;
    artos_drawnet_peer_t **pp = &artos->drawnet.peers;
    while (*pp) {
        if ((*pp)->peer_id == peer_id) {
            artos_drawnet_peer_t *peer = *pp;
            *pp = peer->next;
            if (peer->socket_fd >= 0) close(peer->socket_fd);
            if (peer->channel) g_io_channel_unref(peer->channel);
            if (peer->recv_buffer) free(peer->recv_buffer);
            if (peer->avatar) cairo_surface_destroy(peer->avatar);
            free(peer);
            artos->drawnet.peer_count--;
            return;
        }
        pp = &(*pp)->next;
    }
}

/* Create TCP listen socket */
static int drawnet_create_listen_socket(uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        printf("[DrawNet] Failed to create socket: %s\n", strerror(errno));
        return -1;
    }

    /* Allow port reuse */
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    /* Bind to port */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        printf("[DrawNet] Failed to bind to port %d: %s\n", port, strerror(errno));
        close(fd);
        return -1;
    }

    /* Listen for connections */
    if (listen(fd, 16) < 0) {
        printf("[DrawNet] Failed to listen: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    /* Set non-blocking */
    fcntl(fd, F_SETFL, O_NONBLOCK);

    printf("[DrawNet] Listening on port %d\n", port);
    return fd;
}

/* Connect to remote peer */
static int drawnet_connect_to_peer(const char *host, uint16_t port) {
    if (!host || !*host) return -1;

    /* Resolve hostname */
    struct hostent *he = gethostbyname(host);
    if (!he) {
        printf("[DrawNet] Failed to resolve host '%s'\n", host);
        return -1;
    }

    /* Create socket */
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        printf("[DrawNet] Failed to create socket: %s\n", strerror(errno));
        return -1;
    }

    /* Set options */
    int opt = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

    /* Connect */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    memcpy(&addr.sin_addr, he->h_addr, he->h_length);
    addr.sin_port = htons(port);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        printf("[DrawNet] Failed to connect to %s:%d: %s\n", host, port, strerror(errno));
        close(fd);
        return -1;
    }

    /* Set non-blocking after connect */
    fcntl(fd, F_SETFL, O_NONBLOCK);

    printf("[DrawNet] Connected to %s:%d\n", host, port);
    return fd;
}

/* Setup GLib IO channel for socket */
static void drawnet_setup_peer_channel(phantom_artos_t *artos, artos_drawnet_peer_t *peer) {
    if (!artos || !peer || peer->socket_fd < 0) return;

    peer->channel = g_io_channel_unix_new(peer->socket_fd);
    g_io_channel_set_flags(peer->channel, G_IO_FLAG_NONBLOCK, NULL);
    g_io_channel_set_encoding(peer->channel, NULL, NULL);
    peer->channel_watch = g_io_add_watch(peer->channel,
                                          G_IO_IN | G_IO_HUP | G_IO_ERR,
                                          drawnet_on_receive, artos);

    /* Allocate receive buffer */
    peer->recv_buffer_size = DRAWNET_MAX_PACKET;
    peer->recv_buffer = malloc(peer->recv_buffer_size);
    peer->recv_buffer_used = 0;
}

/* Send packet to a peer */
static int drawnet_send_packet(int socket_fd, artos_drawnet_msg_type_t type,
                               uint32_t sender_id, uint32_t seq,
                               const void *payload, size_t payload_len) {
    if (socket_fd < 0) return -1;

    /* Build header */
    drawnet_wire_header_t header;
    memset(&header, 0, sizeof(header));
    header.magic = htonl(DRAWNET_MAGIC);
    header.version = htons(DRAWNET_VERSION);
    header.msg_type = htons((uint16_t)type);
    header.sender_id = htonl(sender_id);
    header.seq_num = htonl(seq);
    header.timestamp = htobe64((uint64_t)time(NULL) * 1000);
    header.payload_len = htonl((uint32_t)payload_len);
    header.flags = 0;

    /* Send header */
    ssize_t sent = send(socket_fd, &header, sizeof(header), MSG_NOSIGNAL);
    if (sent != sizeof(header)) {
        return -1;
    }

    /* Send payload if any */
    if (payload && payload_len > 0) {
        sent = send(socket_fd, payload, payload_len, MSG_NOSIGNAL);
        if (sent != (ssize_t)payload_len) {
            return -1;
        }
    }

    return 0;
}

/* Broadcast packet to all connected peers */
static void drawnet_broadcast_packet(phantom_artos_t *artos,
                                      artos_drawnet_msg_type_t type,
                                      const void *payload, size_t payload_len) {
    if (!artos) return;

    artos->drawnet.local_seq++;

    artos_drawnet_peer_t *peer = artos->drawnet.peers;
    while (peer) {
        /* Don't send to ourselves or disconnected peers */
        if (peer->peer_id != artos->drawnet.local_id &&
            peer->socket_fd >= 0 && peer->connected) {
            drawnet_send_packet(peer->socket_fd, type,
                               artos->drawnet.local_id, artos->drawnet.local_seq,
                               payload, payload_len);
        }
        peer = peer->next;
    }

    artos->drawnet.packets_sent++;
    artos->drawnet.bytes_sent += sizeof(drawnet_wire_header_t) + payload_len;
}

/* Broadcast peer list to all peers */
static void drawnet_broadcast_peer_list(phantom_artos_t *artos) {
    if (!artos) return;

    /* Count peers */
    int count = artos->drawnet.peer_count;
    if (count <= 0) return;

    /* Build peer list */
    size_t list_size = sizeof(uint32_t) + count * sizeof(drawnet_peer_info_t);
    uint8_t *buffer = malloc(list_size);
    if (!buffer) return;

    uint32_t *pcount = (uint32_t *)buffer;
    *pcount = htonl(count);

    drawnet_peer_info_t *info = (drawnet_peer_info_t *)(buffer + sizeof(uint32_t));
    artos_drawnet_peer_t *peer = artos->drawnet.peers;
    while (peer && info < (drawnet_peer_info_t *)(buffer + list_size)) {
        info->peer_id = htonl(peer->peer_id);
        strncpy(info->name, peer->name, sizeof(info->name) - 1);
        info->color_rgba = htonl(drawnet_color_to_rgba(&peer->cursor_color));
        info->permission = htonl(peer->permission);
        info->connected = peer->connected ? 1 : 0;
        info++;
        peer = peer->next;
    }

    drawnet_broadcast_packet(artos, DRAWNET_MSG_PEER_LIST, buffer, list_size);
    free(buffer);
}

/* Handle incoming connection (host only) */
static gboolean drawnet_on_accept(GIOChannel *channel, GIOCondition cond, gpointer data) {
    phantom_artos_t *artos = (phantom_artos_t *)data;
    if (!artos) return G_SOURCE_REMOVE;

    if (cond & (G_IO_HUP | G_IO_ERR)) {
        printf("[DrawNet] Listen socket error\n");
        return G_SOURCE_REMOVE;
    }

    /* Accept connection */
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    int client_fd = accept(artos->drawnet.listen_socket,
                           (struct sockaddr *)&client_addr, &addr_len);
    if (client_fd < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            printf("[DrawNet] Accept failed: %s\n", strerror(errno));
        }
        return G_SOURCE_CONTINUE;
    }

    /* Set socket options */
    int opt = 1;
    setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
    fcntl(client_fd, F_SETFL, O_NONBLOCK);

    /* Create peer entry */
    artos_drawnet_peer_t *peer = calloc(1, sizeof(artos_drawnet_peer_t));
    if (!peer) {
        close(client_fd);
        return G_SOURCE_CONTINUE;
    }

    peer->peer_id = 0;  /* Will be set after HELLO */
    peer->socket_fd = client_fd;
    inet_ntop(AF_INET, &client_addr.sin_addr, peer->ip_address, sizeof(peer->ip_address));
    peer->port = ntohs(client_addr.sin_port);
    peer->connected = 0;  /* Not yet - waiting for HELLO */
    peer->last_seen = time(NULL);
    peer->show_cursor = 1;
    peer->cursor_opacity = 1.0;

    /* Setup IO channel */
    drawnet_setup_peer_channel(artos, peer);

    /* Add to peer list (temporarily, will be confirmed on HELLO) */
    peer->next = artos->drawnet.peers;
    artos->drawnet.peers = peer;

    printf("[DrawNet] Incoming connection from %s:%d\n", peer->ip_address, peer->port);
    return G_SOURCE_CONTINUE;
}

/* Handle incoming data from peer */
static gboolean drawnet_on_receive(GIOChannel *channel, GIOCondition cond, gpointer data) {
    phantom_artos_t *artos = (phantom_artos_t *)data;
    if (!artos) return G_SOURCE_REMOVE;

    /* Find peer by channel */
    int fd = g_io_channel_unix_get_fd(channel);
    artos_drawnet_peer_t *peer = drawnet_find_peer_by_socket(artos, fd);
    if (!peer) {
        /* Check if this is the host socket (for clients) */
        if (fd == artos->drawnet.tcp_socket) {
            peer = drawnet_find_peer(artos, 0);  /* Host peer has ID 0 initially */
        }
        if (!peer) return G_SOURCE_REMOVE;
    }

    if (cond & (G_IO_HUP | G_IO_ERR)) {
        printf("[DrawNet] Peer %s disconnected\n", peer->name[0] ? peer->name : "unknown");
        if (peer->peer_id != artos->drawnet.local_id) {
            drawnet_remove_peer(artos, peer->peer_id);
            drawnet_update_peers_list(artos);
            drawnet_broadcast_peer_list(artos);
        }
        return G_SOURCE_REMOVE;
    }

    /* Read data into buffer */
    ssize_t received = recv(fd, peer->recv_buffer + peer->recv_buffer_used,
                            peer->recv_buffer_size - peer->recv_buffer_used, 0);
    if (received <= 0) {
        if (received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return G_SOURCE_CONTINUE;
        }
        printf("[DrawNet] Peer read error or disconnect\n");
        if (peer->peer_id != artos->drawnet.local_id) {
            drawnet_remove_peer(artos, peer->peer_id);
            drawnet_update_peers_list(artos);
        }
        return G_SOURCE_REMOVE;
    }

    peer->recv_buffer_used += received;
    peer->last_seen = time(NULL);
    artos->drawnet.bytes_received += received;

    /* Process complete packets */
    while (peer->recv_buffer_used >= sizeof(drawnet_wire_header_t)) {
        drawnet_wire_header_t *header = (drawnet_wire_header_t *)peer->recv_buffer;

        /* Validate magic */
        if (ntohl(header->magic) != DRAWNET_MAGIC) {
            printf("[DrawNet] Invalid packet magic\n");
            peer->recv_buffer_used = 0;  /* Discard buffer */
            break;
        }

        uint32_t payload_len = ntohl(header->payload_len);
        size_t packet_size = sizeof(drawnet_wire_header_t) + payload_len;

        if (peer->recv_buffer_used < packet_size) {
            break;  /* Wait for more data */
        }

        /* Process packet */
        void *payload = payload_len > 0 ? peer->recv_buffer + sizeof(drawnet_wire_header_t) : NULL;
        drawnet_handle_packet(artos, peer, header, payload);

        /* Remove processed packet from buffer */
        memmove(peer->recv_buffer, peer->recv_buffer + packet_size,
                peer->recv_buffer_used - packet_size);
        peer->recv_buffer_used -= packet_size;
        artos->drawnet.packets_received++;
    }

    return G_SOURCE_CONTINUE;
}

/* Handle received packet */
static void drawnet_handle_packet(phantom_artos_t *artos, artos_drawnet_peer_t *peer,
                                   drawnet_wire_header_t *header, void *payload) {
    if (!artos || !peer || !header) return;

    uint16_t msg_type = ntohs(header->msg_type);
    uint32_t sender_id = ntohl(header->sender_id);

    switch (msg_type) {
        case DRAWNET_MSG_HELLO: {
            if (!payload) break;
            drawnet_msg_hello_t *hello = (drawnet_msg_hello_t *)payload;

            /* Validate session code */
            if (strncmp(hello->session_id, artos->drawnet.session_id,
                       sizeof(hello->session_id)) != 0) {
                printf("[DrawNet] Invalid session code from peer\n");
                /* Send rejection ACK */
                drawnet_msg_ack_t ack = {0};
                ack.result = htonl(1);  /* Wrong session */
                drawnet_send_packet(peer->socket_fd, DRAWNET_MSG_ACK,
                                   artos->drawnet.local_id, 0, &ack, sizeof(ack));
                break;
            }

            /* Update peer info */
            peer->peer_id = sender_id ? sender_id : drawnet_generate_peer_id();
            strncpy(peer->name, hello->name, sizeof(peer->name) - 1);
            drawnet_rgba_to_color(ntohl(hello->color_rgba), &peer->cursor_color);
            peer->permission = artos->drawnet.config.default_perm;
            peer->connected = 1;

            /* Send ACK */
            drawnet_msg_ack_t ack = {0};
            ack.result = htonl(0);  /* Success */
            ack.assigned_id = htonl(peer->peer_id);
            ack.assigned_perm = htonl(peer->permission);
            strncpy(ack.session_name, artos->drawnet.config.session_name,
                    sizeof(ack.session_name) - 1);
            ack.peer_count = htonl(artos->drawnet.peer_count);
            drawnet_send_packet(peer->socket_fd, DRAWNET_MSG_ACK,
                               artos->drawnet.local_id, 0, &ack, sizeof(ack));

            artos->drawnet.peer_count++;
            printf("[DrawNet] Peer '%s' joined (ID: %u)\n", peer->name, peer->peer_id);

            /* Notify UI */
            drawnet_update_peers_list(artos);
            char msg[256];
            snprintf(msg, sizeof(msg), "%s joined the session", peer->name);
            drawnet_add_chat_message(artos, "[System]", msg);

            /* Broadcast updated peer list */
            drawnet_broadcast_peer_list(artos);
            break;
        }

        case DRAWNET_MSG_ACK: {
            if (!payload) break;
            drawnet_msg_ack_t *ack = (drawnet_msg_ack_t *)payload;

            uint32_t result = ntohl(ack->result);
            if (result == 0) {
                /* Success - we're connected */
                artos->drawnet.local_id = ntohl(ack->assigned_id);
                artos->drawnet.state = DRAWNET_STATE_CONNECTED;
                printf("[DrawNet] Joined session: %s\n", ack->session_name);
                drawnet_add_chat_message(artos, "[System]", "Connected to session!");
                drawnet_update_status(artos);

                /* Request canvas */
                drawnet_send_packet(peer->socket_fd, DRAWNET_MSG_CANVAS_REQUEST,
                                   artos->drawnet.local_id, ++artos->drawnet.local_seq,
                                   NULL, 0);
            } else {
                artos->drawnet.state = DRAWNET_STATE_ERROR;
                printf("[DrawNet] Join failed: result=%u\n", result);
                drawnet_add_chat_message(artos, "[System]", "Failed to join session");
                drawnet_update_status(artos);
            }
            break;
        }

        case DRAWNET_MSG_PING: {
            /* Send PONG */
            drawnet_send_packet(peer->socket_fd, DRAWNET_MSG_PONG,
                               artos->drawnet.local_id, ntohl(header->seq_num),
                               NULL, 0);
            break;
        }

        case DRAWNET_MSG_PONG: {
            /* Calculate latency */
            uint64_t sent_time = be64toh(header->timestamp);
            uint64_t now = (uint64_t)time(NULL) * 1000;
            peer->latency_ms = (now > sent_time) ? (now - sent_time) : 0;
            break;
        }

        case DRAWNET_MSG_CURSOR: {
            if (!payload) break;
            drawnet_msg_cursor_t *cursor = (drawnet_msg_cursor_t *)payload;

            artos_drawnet_peer_t *p = drawnet_find_peer(artos, sender_id);
            if (p && p != drawnet_find_peer(artos, artos->drawnet.local_id)) {
                p->cursor_x = cursor->x;
                p->cursor_y = cursor->y;
                p->is_drawing = cursor->is_drawing;
                p->last_seen = time(NULL);
                p->show_cursor = 1;
                p->cursor_opacity = 1.0;

                /* Redraw cursor preview */
                if (artos->drawnet_canvas_area) {
                    gtk_widget_queue_draw(artos->drawnet_canvas_area);
                }
            }
            break;
        }

        case DRAWNET_MSG_STROKE_START: {
            if (!payload || !artos->document) break;
            drawnet_msg_stroke_start_t *stroke = (drawnet_msg_stroke_start_t *)payload;

            /* TODO: Create stroke on canvas */
            artos_drawnet_peer_t *p = drawnet_find_peer(artos, sender_id);
            if (p) {
                p->is_drawing = 1;
                drawnet_rgba_to_color(ntohl(stroke->color_rgba), &p->current_color);
                p->brush_size = stroke->brush_size;
                p->current_tool = ntohl(stroke->tool);
            }
            break;
        }

        case DRAWNET_MSG_STROKE_POINT: {
            if (!payload || !artos->document) break;
            /* drawnet_msg_stroke_point_t *point = (drawnet_msg_stroke_point_t *)payload; */
            /* TODO: Add point to current stroke */
            break;
        }

        case DRAWNET_MSG_STROKE_END: {
            if (!artos->document) break;
            artos_drawnet_peer_t *p = drawnet_find_peer(artos, sender_id);
            if (p) {
                p->is_drawing = 0;
            }
            /* TODO: Finalize stroke */
            break;
        }

        case DRAWNET_MSG_CHAT: {
            if (!payload) break;
            drawnet_msg_chat_t *chat = (drawnet_msg_chat_t *)payload;
            artos_drawnet_peer_t *p = drawnet_find_peer(artos, sender_id);
            const char *name = p ? p->name : "Unknown";
            drawnet_add_chat_message(artos, name, chat->message);
            break;
        }

        case DRAWNET_MSG_TOOL_CHANGE: {
            if (!payload) break;
            drawnet_msg_tool_change_t *tc = (drawnet_msg_tool_change_t *)payload;
            artos_drawnet_peer_t *p = drawnet_find_peer(artos, sender_id);
            if (p) {
                p->current_tool = ntohl(tc->tool);
                drawnet_rgba_to_color(ntohl(tc->color_rgba), &p->current_color);
                p->brush_size = tc->brush_size;
            }
            break;
        }

        case DRAWNET_MSG_CANVAS_REQUEST: {
            /* Send canvas to requester */
            artos_drawnet_send_canvas(artos, sender_id);
            break;
        }

        case DRAWNET_MSG_CANVAS_DATA: {
            if (!payload) break;
            drawnet_msg_canvas_chunk_t *chunk = (drawnet_msg_canvas_chunk_t *)payload;
            uint32_t chunk_index = ntohl(chunk->chunk_index);
            uint32_t total_chunks = ntohl(chunk->total_chunks);
            uint64_t total_size = be64toh(chunk->total_size);
            uint32_t chunk_size = ntohl(chunk->chunk_size);
            uint8_t *chunk_data = (uint8_t *)payload + sizeof(drawnet_msg_canvas_chunk_t);

            /* First chunk - allocate buffer */
            if (chunk_index == 0) {
                if (artos->drawnet.canvas_buffer) free(artos->drawnet.canvas_buffer);
                artos->drawnet.canvas_buffer = malloc(total_size);
                artos->drawnet.canvas_buffer_size = total_size;
                artos->drawnet.canvas_chunk_total = total_chunks;
                artos->drawnet.canvas_chunk_current = 0;
                artos->drawnet.canvas_sync_pending = 1;
                artos->drawnet.state = DRAWNET_STATE_SYNCING;
                drawnet_update_status(artos);
            }

            /* Copy chunk data */
            if (artos->drawnet.canvas_buffer) {
                size_t offset = chunk_index * DRAWNET_CHUNK_SIZE;
                if (offset + chunk_size <= artos->drawnet.canvas_buffer_size) {
                    memcpy(artos->drawnet.canvas_buffer + offset, chunk_data, chunk_size);
                }
                artos->drawnet.canvas_chunk_current++;
            }

            /* All chunks received */
            if (artos->drawnet.canvas_chunk_current >= artos->drawnet.canvas_chunk_total) {
                printf("[DrawNet] Canvas received (%zu bytes)\n", artos->drawnet.canvas_buffer_size);
                /* TODO: Load PNG into document */
                free(artos->drawnet.canvas_buffer);
                artos->drawnet.canvas_buffer = NULL;
                artos->drawnet.canvas_sync_pending = 0;
                artos->drawnet.state = DRAWNET_STATE_CONNECTED;
                drawnet_update_status(artos);
                drawnet_add_chat_message(artos, "[System]", "Canvas synchronized!");
            }
            break;
        }

        case DRAWNET_MSG_PEER_LIST: {
            /* Update our peer list from host */
            if (!payload) break;
            uint32_t count = ntohl(*(uint32_t *)payload);
            drawnet_peer_info_t *infos = (drawnet_peer_info_t *)((uint8_t *)payload + sizeof(uint32_t));

            for (uint32_t i = 0; i < count; i++) {
                uint32_t pid = ntohl(infos[i].peer_id);
                if (pid == artos->drawnet.local_id) continue;

                artos_drawnet_peer_t *p = drawnet_find_peer(artos, pid);
                if (!p) {
                    /* Add new peer */
                    p = calloc(1, sizeof(artos_drawnet_peer_t));
                    if (p) {
                        p->peer_id = pid;
                        p->socket_fd = -1;
                        p->next = artos->drawnet.peers;
                        artos->drawnet.peers = p;
                        artos->drawnet.peer_count++;
                    }
                }
                if (p) {
                    strncpy(p->name, infos[i].name, sizeof(p->name) - 1);
                    drawnet_rgba_to_color(ntohl(infos[i].color_rgba), &p->cursor_color);
                    p->permission = ntohl(infos[i].permission);
                    p->connected = infos[i].connected;
                    p->show_cursor = 1;
                    p->cursor_opacity = 1.0;
                }
            }
            drawnet_update_peers_list(artos);
            break;
        }

        case DRAWNET_MSG_KICK: {
            if (!payload) break;
            drawnet_msg_kick_t *kick = (drawnet_msg_kick_t *)payload;
            uint32_t kicked_id = ntohl(kick->peer_id);

            if (kicked_id == artos->drawnet.local_id) {
                /* We got kicked */
                drawnet_add_chat_message(artos, "[System]", "You have been kicked from the session");
                artos_drawnet_leave_session(artos);
            } else if (artos->drawnet.is_host) {
                /* Forward kick to target */
                artos_drawnet_peer_t *p = drawnet_find_peer(artos, kicked_id);
                if (p && p->socket_fd >= 0) {
                    drawnet_send_packet(p->socket_fd, DRAWNET_MSG_KICK,
                                       artos->drawnet.local_id, 0, kick, sizeof(*kick));
                    drawnet_remove_peer(artos, kicked_id);
                    drawnet_update_peers_list(artos);
                    drawnet_broadcast_peer_list(artos);
                }
            }
            break;
        }

        case DRAWNET_MSG_LEAVE: {
            artos_drawnet_peer_t *p = drawnet_find_peer(artos, sender_id);
            if (p) {
                char msg[256];
                snprintf(msg, sizeof(msg), "%s left the session", p->name);
                drawnet_add_chat_message(artos, "[System]", msg);
                drawnet_remove_peer(artos, sender_id);
                drawnet_update_peers_list(artos);
                if (artos->drawnet.is_host) {
                    drawnet_broadcast_peer_list(artos);
                }
            }
            break;
        }

        default:
            printf("[DrawNet] Unknown message type: %d\n", msg_type);
            break;
    }
}

/* Enable/disable DrawNet */
void artos_drawnet_enable(phantom_artos_t *artos, int enable) {
    if (!artos) return;

    artos->drawnet.enabled = enable;

    if (!enable && artos->drawnet.state != DRAWNET_STATE_DISCONNECTED) {
        artos_drawnet_leave_session(artos);
    }

    drawnet_update_status(artos);
}

/* Host a new DrawNet session */
int artos_drawnet_host_session(phantom_artos_t *artos, const char *name) {
    if (!artos || !artos->drawnet.enabled) return -1;

    /* Check Governor capability for hosting a network session */
    if (!artos_drawnet_check_capability(artos, "host_session")) {
        printf("[DrawNet] Cannot host session - Governor denied network capability\n");
        artos->drawnet.state = DRAWNET_STATE_ERROR;
        drawnet_update_status(artos);
        drawnet_add_chat_message(artos, "[System]",
                                 "Governor denied network access. Enable CAP_NETWORK to host sessions.");
        return -1;
    }

    /* Generate session code */
    drawnet_generate_session_code(artos->drawnet.session_id,
                                  sizeof(artos->drawnet.session_id));

    /* Set session name */
    if (name && *name) {
        strncpy(artos->drawnet.config.session_name, name,
                sizeof(artos->drawnet.config.session_name) - 1);
    } else {
        snprintf(artos->drawnet.config.session_name,
                 sizeof(artos->drawnet.config.session_name),
                 "%s's Canvas", artos->drawnet.local_name);
    }

    artos->drawnet.is_host = 1;
    artos->drawnet.session_start = time(NULL);

    /* Create listen socket */
    artos->drawnet.listen_port = DRAWNET_DEFAULT_PORT;
    artos->drawnet.listen_socket = drawnet_create_listen_socket(artos->drawnet.listen_port);

    /* Try alternate ports if default is busy */
    if (artos->drawnet.listen_socket < 0) {
        for (uint16_t p = DRAWNET_DEFAULT_PORT + 1; p < DRAWNET_DEFAULT_PORT + 10; p++) {
            artos->drawnet.listen_socket = drawnet_create_listen_socket(p);
            if (artos->drawnet.listen_socket >= 0) {
                artos->drawnet.listen_port = p;
                break;
            }
        }
    }

    if (artos->drawnet.listen_socket < 0) {
        printf("[DrawNet] Failed to create listen socket\n");
        artos->drawnet.state = DRAWNET_STATE_ERROR;
        drawnet_update_status(artos);
        drawnet_add_chat_message(artos, "[System]", "Failed to start server - port in use?");
        return -1;
    }

    /* Setup GLib IO watch for accepting connections */
    artos->drawnet.listen_channel = g_io_channel_unix_new(artos->drawnet.listen_socket);
    g_io_channel_set_flags(artos->drawnet.listen_channel, G_IO_FLAG_NONBLOCK, NULL);
    artos->drawnet.listen_watch = g_io_add_watch(artos->drawnet.listen_channel,
                                                  G_IO_IN | G_IO_ERR,
                                                  drawnet_on_accept, artos);

    artos->drawnet.state = DRAWNET_STATE_CONNECTED;

    /* Create local peer entry for ourselves */
    artos_drawnet_peer_t *self = calloc(1, sizeof(artos_drawnet_peer_t));
    if (self) {
        self->peer_id = artos->drawnet.local_id;
        strncpy(self->name, artos->drawnet.local_name, sizeof(self->name) - 1);
        self->cursor_color = artos->drawnet.local_cursor_color;
        self->permission = DRAWNET_PERM_ADMIN;
        self->connected = 1;
        self->last_seen = time(NULL);
        self->show_cursor = 1;
        self->cursor_opacity = 1.0;
        self->next = artos->drawnet.peers;
        artos->drawnet.peers = self;
        artos->drawnet.peer_count++;
    }

    /* Start ping timer */
    artos->drawnet.ping_timer = g_timeout_add(DRAWNET_PING_INTERVAL,
                                               drawnet_ping_timer, artos);

    /* Start cursor broadcast timer */
    if (artos->drawnet.config.share_cursor) {
        artos->drawnet.cursor_timer = g_timeout_add(DRAWNET_CURSOR_INTERVAL,
                                                     drawnet_cursor_timer, artos);
    }

    /* Update UI */
    drawnet_update_status(artos);
    drawnet_update_peers_list(artos);

    /* Add system message */
    drawnet_add_chat_message(artos, "[System]",
                             "Session started. Share the code to invite others!");

    return 0;
}

/* Join a session by code */
int artos_drawnet_join_session(phantom_artos_t *artos, const char *session_code) {
    if (!artos || !artos->drawnet.enabled || !session_code) return -1;

    /* Check Governor capability for joining a network session */
    if (!artos_drawnet_check_capability(artos, "join_session")) {
        printf("[DrawNet] Cannot join session - Governor denied network capability\n");
        artos->drawnet.state = DRAWNET_STATE_ERROR;
        drawnet_update_status(artos);
        drawnet_add_chat_message(artos, "[System]",
                                 "Governor denied network access. Enable CAP_NETWORK to join sessions.");
        return -1;
    }

    strncpy(artos->drawnet.session_id, session_code,
            sizeof(artos->drawnet.session_id) - 1);

    artos->drawnet.is_host = 0;
    artos->drawnet.state = DRAWNET_STATE_CONNECTING;

    drawnet_update_status(artos);

    /* Simulate connection (in real implementation, would connect to peer) */
    /* For demo purposes, immediately "connect" */
    artos->drawnet.state = DRAWNET_STATE_CONNECTED;
    artos->drawnet.session_start = time(NULL);

    /* Create local peer entry */
    artos_drawnet_peer_t *self = calloc(1, sizeof(artos_drawnet_peer_t));
    if (self) {
        self->peer_id = artos->drawnet.local_id;
        strncpy(self->name, artos->drawnet.local_name, sizeof(self->name) - 1);
        self->cursor_color = artos->drawnet.local_cursor_color;
        self->permission = artos->drawnet.config.default_perm;
        self->connected = 1;
        self->last_seen = time(NULL);
        self->show_cursor = 1;
        self->cursor_opacity = 1.0;
        self->next = artos->drawnet.peers;
        artos->drawnet.peers = self;
        artos->drawnet.peer_count++;
    }

    /* Start timers */
    artos->drawnet.ping_timer = g_timeout_add(DRAWNET_PING_INTERVAL,
                                               drawnet_ping_timer, artos);
    if (artos->drawnet.config.share_cursor) {
        artos->drawnet.cursor_timer = g_timeout_add(DRAWNET_CURSOR_INTERVAL,
                                                     drawnet_cursor_timer, artos);
    }

    drawnet_update_status(artos);
    drawnet_update_peers_list(artos);

    drawnet_add_chat_message(artos, "[System]", "Joined session!");

    return 0;
}

/* Join by direct IP */
int artos_drawnet_join_direct(phantom_artos_t *artos, const char *host, uint16_t port) {
    if (!artos || !artos->drawnet.enabled || !host) return -1;

    /* Check Governor capability */
    if (!artos_drawnet_check_capability(artos, "join_direct")) {
        printf("[DrawNet] Cannot join session - Governor denied network capability\n");
        artos->drawnet.state = DRAWNET_STATE_ERROR;
        drawnet_update_status(artos);
        return -1;
    }

    artos->drawnet.is_host = 0;
    artos->drawnet.state = DRAWNET_STATE_CONNECTING;
    drawnet_update_status(artos);

    /* Connect to host */
    artos->drawnet.tcp_socket = drawnet_connect_to_peer(host, port ? port : DRAWNET_DEFAULT_PORT);
    if (artos->drawnet.tcp_socket < 0) {
        artos->drawnet.state = DRAWNET_STATE_ERROR;
        snprintf(artos->drawnet.last_error, sizeof(artos->drawnet.last_error),
                 "Failed to connect to %s:%d", host, port ? port : DRAWNET_DEFAULT_PORT);
        drawnet_update_status(artos);
        drawnet_add_chat_message(artos, "[System]", artos->drawnet.last_error);
        return -1;
    }

    /* Create host peer entry */
    artos_drawnet_peer_t *host_peer = calloc(1, sizeof(artos_drawnet_peer_t));
    if (host_peer) {
        host_peer->peer_id = 0;  /* Will be set after handshake */
        host_peer->socket_fd = artos->drawnet.tcp_socket;
        strncpy(host_peer->ip_address, host, sizeof(host_peer->ip_address) - 1);
        host_peer->port = port ? port : DRAWNET_DEFAULT_PORT;
        host_peer->connected = 0;  /* Not yet */
        strncpy(host_peer->name, "Host", sizeof(host_peer->name) - 1);
        host_peer->last_seen = time(NULL);
        host_peer->show_cursor = 1;
        host_peer->cursor_opacity = 1.0;

        /* Setup IO channel */
        drawnet_setup_peer_channel(artos, host_peer);

        host_peer->next = artos->drawnet.peers;
        artos->drawnet.peers = host_peer;
    }

    /* Create local peer entry */
    artos_drawnet_peer_t *self = calloc(1, sizeof(artos_drawnet_peer_t));
    if (self) {
        self->peer_id = artos->drawnet.local_id;
        strncpy(self->name, artos->drawnet.local_name, sizeof(self->name) - 1);
        self->cursor_color = artos->drawnet.local_cursor_color;
        self->permission = artos->drawnet.config.default_perm;
        self->connected = 1;
        self->socket_fd = -1;
        self->last_seen = time(NULL);
        self->show_cursor = 1;
        self->cursor_opacity = 1.0;
        self->next = artos->drawnet.peers;
        artos->drawnet.peers = self;
        artos->drawnet.peer_count++;
    }

    /* Send HELLO message */
    drawnet_msg_hello_t hello;
    memset(&hello, 0, sizeof(hello));
    strncpy(hello.session_id, artos->drawnet.session_id, sizeof(hello.session_id) - 1);
    strncpy(hello.name, artos->drawnet.local_name, sizeof(hello.name) - 1);
    hello.color_rgba = htonl(drawnet_color_to_rgba(&artos->drawnet.local_cursor_color));
    hello.capabilities = 0;

    drawnet_send_packet(artos->drawnet.tcp_socket, DRAWNET_MSG_HELLO,
                       artos->drawnet.local_id, ++artos->drawnet.local_seq,
                       &hello, sizeof(hello));

    artos->drawnet.session_start = time(NULL);

    /* Start timers */
    artos->drawnet.ping_timer = g_timeout_add(DRAWNET_PING_INTERVAL,
                                               drawnet_ping_timer, artos);
    if (artos->drawnet.config.share_cursor) {
        artos->drawnet.cursor_timer = g_timeout_add(DRAWNET_CURSOR_INTERVAL,
                                                     drawnet_cursor_timer, artos);
    }

    drawnet_update_status(artos);
    drawnet_add_chat_message(artos, "[System]", "Connecting to host...");

    return 0;
}

/* Leave current session */
void artos_drawnet_leave_session(phantom_artos_t *artos) {
    if (!artos) return;

    /* Stop timers */
    if (artos->drawnet.ping_timer) {
        g_source_remove(artos->drawnet.ping_timer);
        artos->drawnet.ping_timer = 0;
    }
    if (artos->drawnet.cursor_timer) {
        g_source_remove(artos->drawnet.cursor_timer);
        artos->drawnet.cursor_timer = 0;
    }

    /* Close sockets */
    if (artos->drawnet.tcp_socket >= 0) {
        close(artos->drawnet.tcp_socket);
        artos->drawnet.tcp_socket = -1;
    }
    if (artos->drawnet.udp_socket >= 0) {
        close(artos->drawnet.udp_socket);
        artos->drawnet.udp_socket = -1;
    }
    if (artos->drawnet.listen_socket >= 0) {
        close(artos->drawnet.listen_socket);
        artos->drawnet.listen_socket = -1;
    }

    /* Free peers */
    artos_drawnet_peer_t *peer = artos->drawnet.peers;
    while (peer) {
        artos_drawnet_peer_t *next = peer->next;
        if (peer->avatar) cairo_surface_destroy(peer->avatar);
        free(peer);
        peer = next;
    }
    artos->drawnet.peers = NULL;
    artos->drawnet.peer_count = 0;

    /* Reset state */
    artos->drawnet.is_host = 0;
    artos->drawnet.state = DRAWNET_STATE_DISCONNECTED;
    memset(artos->drawnet.session_id, 0, sizeof(artos->drawnet.session_id));

    /* Reset Governor approval so next session requires new approval */
    artos->drawnet.governor_approved = 0;
    memset(artos->drawnet.governor_approval_scope, 0, sizeof(artos->drawnet.governor_approval_scope));

    drawnet_update_status(artos);
    drawnet_update_peers_list(artos);
}

/* Start scanning for peers */
void artos_drawnet_scan_start(phantom_artos_t *artos) {
    if (!artos || !artos->drawnet.enabled) return;

    /* Check Governor capability for network scanning */
    if (!artos_drawnet_check_capability(artos, "scan_network")) {
        printf("[DrawNet] Cannot scan - Governor denied network capability\n");
        return;
    }

    artos->drawnet.state = DRAWNET_STATE_DISCOVERING;
    artos->drawnet.discovered_count = 0;

    /* Simulate finding some peers for demo */
    /* In real implementation, would use mDNS/Avahi */
    strncpy(artos->drawnet.discovered[0].name, "Art Studio (Local)",
            sizeof(artos->drawnet.discovered[0].name) - 1);
    strncpy(artos->drawnet.discovered[0].host, "192.168.1.100",
            sizeof(artos->drawnet.discovered[0].host) - 1);
    artos->drawnet.discovered[0].port = DRAWNET_DEFAULT_PORT;
    artos->drawnet.discovered[0].active = 1;
    artos->drawnet.discovered_count = 1;

    drawnet_update_status(artos);

    /* Update discovered list in UI */
    if (artos->drawnet_discovered_store) {
        gtk_list_store_clear(artos->drawnet_discovered_store);
        for (int i = 0; i < artos->drawnet.discovered_count; i++) {
            if (artos->drawnet.discovered[i].active) {
                GtkTreeIter iter;
                gtk_list_store_append(artos->drawnet_discovered_store, &iter);
                gtk_list_store_set(artos->drawnet_discovered_store, &iter,
                                   0, artos->drawnet.discovered[i].name,
                                   1, artos->drawnet.discovered[i].host,
                                   2, artos->drawnet.discovered[i].port,
                                   -1);
            }
        }
    }

    /* Auto-stop after a few seconds */
    artos->drawnet.discovery_timer = g_timeout_add(3000,
        (GSourceFunc)artos_drawnet_scan_stop, artos);
}

/* Stop scanning */
void artos_drawnet_scan_stop(phantom_artos_t *artos) {
    if (!artos) return;

    if (artos->drawnet.state == DRAWNET_STATE_DISCOVERING) {
        artos->drawnet.state = DRAWNET_STATE_DISCONNECTED;
    }

    if (artos->drawnet.discovery_timer) {
        g_source_remove(artos->drawnet.discovery_timer);
        artos->drawnet.discovery_timer = 0;
    }

    drawnet_update_status(artos);
}

/* Set username */
void artos_drawnet_set_username(phantom_artos_t *artos, const char *name) {
    if (!artos || !name) return;

    strncpy(artos->drawnet.local_name, name,
            sizeof(artos->drawnet.local_name) - 1);

    /* Update our peer entry */
    artos_drawnet_peer_t *peer = artos->drawnet.peers;
    while (peer) {
        if (peer->peer_id == artos->drawnet.local_id) {
            strncpy(peer->name, name, sizeof(peer->name) - 1);
            break;
        }
        peer = peer->next;
    }

    drawnet_update_peers_list(artos);
}

/* Set sync mode */
void artos_drawnet_set_sync_mode(phantom_artos_t *artos, artos_drawnet_sync_t mode) {
    if (!artos) return;
    artos->drawnet.config.sync_mode = mode;
}

/* Set permission for a peer */
void artos_drawnet_set_permission(phantom_artos_t *artos, uint32_t peer_id,
                                   artos_drawnet_perm_t perm) {
    if (!artos) return;

    artos_drawnet_peer_t *peer = artos->drawnet.peers;
    while (peer) {
        if (peer->peer_id == peer_id) {
            peer->permission = perm;
            break;
        }
        peer = peer->next;
    }

    drawnet_update_peers_list(artos);
}

/* Broadcast stroke start */
void artos_drawnet_broadcast_stroke_start(phantom_artos_t *artos) {
    if (!artos || artos->drawnet.state != DRAWNET_STATE_CONNECTED) return;

    artos->drawnet.current_stroke_id = (uint32_t)time(NULL) ^ artos->drawnet.local_id;

    drawnet_msg_stroke_start_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.stroke_id = htonl(artos->drawnet.current_stroke_id);
    msg.color_rgba = htonl(drawnet_color_to_rgba(&artos->foreground_color));
    msg.brush_size = artos->current_brush.size;
    msg.tool = htonl((uint32_t)artos->current_tool);
    msg.layer_index = htonl(artos->document ? artos->document->active_layer : 0);

    drawnet_broadcast_packet(artos, DRAWNET_MSG_STROKE_START, &msg, sizeof(msg));
}

/* Broadcast stroke point */
void artos_drawnet_broadcast_stroke_point(phantom_artos_t *artos,
                                           double x, double y, double pressure) {
    if (!artos || artos->drawnet.state != DRAWNET_STATE_CONNECTED) return;
    if (artos->drawnet.current_stroke_id == 0) return;

    /* Rate limit based on sync mode */
    if (artos->drawnet.config.sync_mode != DRAWNET_SYNC_REALTIME) {
        return; /* Would queue for later */
    }

    drawnet_msg_stroke_point_t msg;
    msg.stroke_id = htonl(artos->drawnet.current_stroke_id);
    msg.x = x;
    msg.y = y;
    msg.pressure = pressure;

    drawnet_broadcast_packet(artos, DRAWNET_MSG_STROKE_POINT, &msg, sizeof(msg));
}

/* Broadcast stroke end */
void artos_drawnet_broadcast_stroke_end(phantom_artos_t *artos) {
    if (!artos || artos->drawnet.state != DRAWNET_STATE_CONNECTED) return;

    drawnet_msg_stroke_end_t msg;
    msg.stroke_id = htonl(artos->drawnet.current_stroke_id);
    msg.point_count = 0;  /* Could track this */

    drawnet_broadcast_packet(artos, DRAWNET_MSG_STROKE_END, &msg, sizeof(msg));
    artos->drawnet.current_stroke_id = 0;
}

/* Broadcast cursor position */
void artos_drawnet_broadcast_cursor(phantom_artos_t *artos, double x, double y) {
    if (!artos || artos->drawnet.state != DRAWNET_STATE_CONNECTED) return;
    if (!artos->drawnet.config.share_cursor) return;

    /* Update our own peer cursor position */
    artos_drawnet_peer_t *peer = artos->drawnet.peers;
    while (peer) {
        if (peer->peer_id == artos->drawnet.local_id) {
            peer->cursor_x = x;
            peer->cursor_y = y;
            peer->last_seen = time(NULL);
            break;
        }
        peer = peer->next;
    }

    drawnet_msg_cursor_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.x = x;
    msg.y = y;
    msg.is_drawing = (artos->drawnet.current_stroke_id != 0) ? 1 : 0;

    drawnet_broadcast_packet(artos, DRAWNET_MSG_CURSOR, &msg, sizeof(msg));
}

/* Broadcast tool change */
void artos_drawnet_broadcast_tool_change(phantom_artos_t *artos) {
    if (!artos || artos->drawnet.state != DRAWNET_STATE_CONNECTED) return;
    if (!artos->drawnet.config.share_tool) return;

    /* Update our peer entry */
    artos_drawnet_peer_t *peer = artos->drawnet.peers;
    while (peer) {
        if (peer->peer_id == artos->drawnet.local_id) {
            peer->current_tool = artos->current_tool;
            peer->current_color = artos->foreground_color;
            peer->brush_size = artos->current_brush.size;
            break;
        }
        peer = peer->next;
    }

    drawnet_msg_tool_change_t msg;
    msg.tool = htonl((uint32_t)artos->current_tool);
    msg.color_rgba = htonl(drawnet_color_to_rgba(&artos->foreground_color));
    msg.brush_size = artos->current_brush.size;

    drawnet_broadcast_packet(artos, DRAWNET_MSG_TOOL_CHANGE, &msg, sizeof(msg));
}

/* Send chat message */
void artos_drawnet_send_chat(phantom_artos_t *artos, const char *message) {
    if (!artos || !message || !*message) return;
    if (artos->drawnet.state != DRAWNET_STATE_CONNECTED) return;

    /* Add to local chat */
    drawnet_add_chat_message(artos, artos->drawnet.local_name, message);

    drawnet_msg_chat_t msg;
    memset(&msg, 0, sizeof(msg));
    strncpy(msg.message, message, sizeof(msg.message) - 1);

    drawnet_broadcast_packet(artos, DRAWNET_MSG_CHAT, &msg, sizeof(msg));
}

/* Send reaction emoji */
void artos_drawnet_send_reaction(phantom_artos_t *artos, const char *emoji) {
    if (!artos || !emoji) return;
    if (artos->drawnet.state != DRAWNET_STATE_CONNECTED) return;

    /* In real implementation, would send DRAWNET_MSG_REACTION */
    char msg[256];
    snprintf(msg, sizeof(msg), "%s reacted: %s", artos->drawnet.local_name, emoji);
    drawnet_add_chat_message(artos, "[System]", msg);
}

/* Request canvas from peer */
void artos_drawnet_request_canvas(phantom_artos_t *artos, uint32_t peer_id) {
    if (!artos || artos->drawnet.state != DRAWNET_STATE_CONNECTED) return;

    artos->drawnet.state = DRAWNET_STATE_SYNCING;
    artos->drawnet.canvas_sync_pending = 1;
    artos->drawnet.canvas_chunk_current = 0;
    artos->drawnet.canvas_chunk_total = 0;

    drawnet_update_status(artos);

    /* In real implementation, would send DRAWNET_MSG_CANVAS_REQUEST */
}

/* PNG write callback for memory stream */
static cairo_status_t drawnet_png_write_callback(void *closure, const unsigned char *data,
                                                  unsigned int length) {
    GByteArray *array = (GByteArray *)closure;
    g_byte_array_append(array, data, length);
    return CAIRO_STATUS_SUCCESS;
}

/* Send canvas to peer */
void artos_drawnet_send_canvas(phantom_artos_t *artos, uint32_t peer_id) {
    if (!artos || artos->drawnet.state != DRAWNET_STATE_CONNECTED) return;
    if (!artos->document || !artos->document->composite) return;

    /* Find peer */
    artos_drawnet_peer_t *peer = drawnet_find_peer(artos, peer_id);
    if (!peer || peer->socket_fd < 0) {
        /* If no specific peer, broadcast to all */
        if (peer_id == 0) {
            peer = artos->drawnet.peers;
        } else {
            return;
        }
    }

    /* Serialize canvas to PNG */
    GByteArray *png_data = g_byte_array_new();
    cairo_status_t status = cairo_surface_write_to_png_stream(
        artos->document->composite, drawnet_png_write_callback, png_data);

    if (status != CAIRO_STATUS_SUCCESS || png_data->len == 0) {
        printf("[DrawNet] Failed to serialize canvas to PNG\n");
        g_byte_array_free(png_data, TRUE);
        return;
    }

    printf("[DrawNet] Sending canvas: %u bytes\n", png_data->len);

    /* Calculate chunks */
    uint32_t total_chunks = (png_data->len + DRAWNET_CHUNK_SIZE - 1) / DRAWNET_CHUNK_SIZE;

    /* Send to specific peer or broadcast */
    while (peer) {
        if (peer->peer_id != artos->drawnet.local_id && peer->socket_fd >= 0) {
            /* Send chunks */
            for (uint32_t i = 0; i < total_chunks; i++) {
                size_t offset = i * DRAWNET_CHUNK_SIZE;
                size_t chunk_size = png_data->len - offset;
                if (chunk_size > DRAWNET_CHUNK_SIZE) chunk_size = DRAWNET_CHUNK_SIZE;

                /* Build chunk packet */
                size_t pkt_size = sizeof(drawnet_msg_canvas_chunk_t) + chunk_size;
                uint8_t *pkt = malloc(pkt_size);
                if (!pkt) continue;

                drawnet_msg_canvas_chunk_t *chunk = (drawnet_msg_canvas_chunk_t *)pkt;
                chunk->chunk_index = htonl(i);
                chunk->total_chunks = htonl(total_chunks);
                chunk->total_size = htobe64(png_data->len);
                chunk->chunk_size = htonl((uint32_t)chunk_size);
                memcpy(pkt + sizeof(drawnet_msg_canvas_chunk_t),
                       png_data->data + offset, chunk_size);

                drawnet_send_packet(peer->socket_fd, DRAWNET_MSG_CANVAS_DATA,
                                   artos->drawnet.local_id, ++artos->drawnet.local_seq,
                                   pkt, pkt_size);
                free(pkt);
            }
        }

        /* If specific peer, stop after one */
        if (peer_id != 0) break;
        peer = peer->next;
    }

    g_byte_array_free(png_data, TRUE);
}

/* Kick a peer (host only) */
void artos_drawnet_kick_peer(phantom_artos_t *artos, uint32_t peer_id) {
    if (!artos || !artos->drawnet.is_host) return;
    if (peer_id == artos->drawnet.local_id) return; /* Can't kick self */

    /* Find and remove peer */
    artos_drawnet_peer_t **pp = &artos->drawnet.peers;
    while (*pp) {
        if ((*pp)->peer_id == peer_id) {
            artos_drawnet_peer_t *to_remove = *pp;

            /* Send KICK packet before disconnecting */
            if (to_remove->socket_fd >= 0) {
                drawnet_msg_kick_t kick_msg;
                memset(&kick_msg, 0, sizeof(kick_msg));
                kick_msg.peer_id = peer_id;
                strncpy(kick_msg.reason, "Removed by host", sizeof(kick_msg.reason) - 1);

                drawnet_send_packet(to_remove->socket_fd, DRAWNET_MSG_KICK,
                                    artos->drawnet.local_id, artos->drawnet.local_seq++,
                                    &kick_msg, sizeof(kick_msg));

                /* Cleanup network resources */
                if (to_remove->channel_watch > 0) {
                    g_source_remove(to_remove->channel_watch);
                }
                if (to_remove->channel) {
                    g_io_channel_shutdown(to_remove->channel, FALSE, NULL);
                    g_io_channel_unref(to_remove->channel);
                }
                close(to_remove->socket_fd);
            }

            *pp = to_remove->next;

            char msg[128];
            snprintf(msg, sizeof(msg), "%s was removed from the session", to_remove->name);
            drawnet_add_chat_message(artos, "[System]", msg);

            if (to_remove->avatar) cairo_surface_destroy(to_remove->avatar);
            if (to_remove->recv_buffer) free(to_remove->recv_buffer);
            free(to_remove);
            artos->drawnet.peer_count--;
            break;
        }
        pp = &(*pp)->next;
    }

    drawnet_update_peers_list(artos);

    /* Broadcast peer list update to remaining peers */
    /* TODO: Implement PEER_LIST broadcast */
}

/* Get peer list */
artos_drawnet_peer_t *artos_drawnet_get_peers(phantom_artos_t *artos) {
    return artos ? artos->drawnet.peers : NULL;
}

/* Get peer count */
int artos_drawnet_get_peer_count(phantom_artos_t *artos) {
    return artos ? artos->drawnet.peer_count : 0;
}

/* Get session code */
const char *artos_drawnet_get_session_code(phantom_artos_t *artos) {
    if (!artos || artos->drawnet.state == DRAWNET_STATE_DISCONNECTED) {
        return NULL;
    }
    return artos->drawnet.session_id;
}

/* Internal: Ping timer callback */
static gboolean drawnet_ping_timer(gpointer data) {
    phantom_artos_t *artos = (phantom_artos_t *)data;
    if (!artos || artos->drawnet.state != DRAWNET_STATE_CONNECTED) {
        return G_SOURCE_REMOVE;
    }

    /* Update peer timeouts */
    time_t now = time(NULL);
    artos_drawnet_peer_t *peer = artos->drawnet.peers;
    while (peer) {
        if (peer->peer_id != artos->drawnet.local_id) {
            if (now - peer->last_seen > DRAWNET_TIMEOUT / 1000) {
                peer->connected = 0;
            }
        }
        peer = peer->next;
    }

    /* Update statistics display */
    if (artos->drawnet_stats_label) {
        char stats[128];
        time_t uptime = now - artos->drawnet.session_start;
        snprintf(stats, sizeof(stats),
                 "Sent: %d pkts | Recv: %d pkts | Uptime: %ld:%02ld",
                 artos->drawnet.packets_sent,
                 artos->drawnet.packets_received,
                 uptime / 60, uptime % 60);
        gtk_label_set_text(GTK_LABEL(artos->drawnet_stats_label), stats);
    }

    return G_SOURCE_CONTINUE;
}

/* Internal: Cursor broadcast timer */
static gboolean drawnet_cursor_timer(gpointer data) {
    phantom_artos_t *artos = (phantom_artos_t *)data;
    if (!artos || artos->drawnet.state != DRAWNET_STATE_CONNECTED) {
        return G_SOURCE_REMOVE;
    }

    /* Broadcast cursor if enabled */
    if (artos->drawnet.config.share_cursor) {
        artos_drawnet_broadcast_cursor(artos, artos->last_x, artos->last_y);
    }

    /* Fade peer cursors that haven't moved */
    artos_drawnet_peer_t *peer = artos->drawnet.peers;
    while (peer) {
        if (peer->peer_id != artos->drawnet.local_id) {
            time_t now = time(NULL);
            if (now - peer->last_seen > 2) {
                peer->cursor_opacity *= 0.95;
                if (peer->cursor_opacity < 0.1) {
                    peer->show_cursor = 0;
                }
            }
        }
        peer = peer->next;
    }

    /* Redraw mini preview if visible */
    if (artos->drawnet_canvas_area && gtk_widget_is_visible(artos->drawnet_canvas_area)) {
        gtk_widget_queue_draw(artos->drawnet_canvas_area);
    }

    return G_SOURCE_CONTINUE;
}

/* Internal: Update status display */
static void drawnet_update_status(phantom_artos_t *artos) {
    if (!artos || !artos->drawnet_status_label) return;

    const char *state_str = artos_drawnet_get_state_string(artos->drawnet.state);
    char status[256];

    if (artos->drawnet.state == DRAWNET_STATE_CONNECTED) {
        snprintf(status, sizeof(status), "%s | %d peer(s)",
                 state_str, artos->drawnet.peer_count);
    } else {
        snprintf(status, sizeof(status), "%s", state_str);
    }

    gtk_label_set_text(GTK_LABEL(artos->drawnet_status_label), status);

    /* Update code label */
    if (artos->drawnet_code_label) {
        if (artos->drawnet.state == DRAWNET_STATE_CONNECTED && artos->drawnet.is_host) {
            char code_text[64];
            snprintf(code_text, sizeof(code_text), "Code: %s", artos->drawnet.session_id);
            gtk_label_set_text(GTK_LABEL(artos->drawnet_code_label), code_text);
        } else {
            gtk_label_set_text(GTK_LABEL(artos->drawnet_code_label), "");
        }
    }

    /* Update button sensitivity */
    if (artos->drawnet_host_btn) {
        gtk_widget_set_sensitive(artos->drawnet_host_btn,
                                 artos->drawnet.state == DRAWNET_STATE_DISCONNECTED);
    }
    if (artos->drawnet_join_btn) {
        gtk_widget_set_sensitive(artos->drawnet_join_btn,
                                 artos->drawnet.state == DRAWNET_STATE_DISCONNECTED);
    }
    if (artos->drawnet_scan_btn) {
        gtk_widget_set_sensitive(artos->drawnet_scan_btn,
                                 artos->drawnet.state == DRAWNET_STATE_DISCONNECTED ||
                                 artos->drawnet.state == DRAWNET_STATE_DISCOVERING);
    }
}

/* Internal: Update peers list */
static void drawnet_update_peers_list(phantom_artos_t *artos) {
    if (!artos || !artos->drawnet_peers_store) return;

    gtk_list_store_clear(artos->drawnet_peers_store);

    artos_drawnet_peer_t *peer = artos->drawnet.peers;
    while (peer) {
        GtkTreeIter iter;
        gtk_list_store_append(artos->drawnet_peers_store, &iter);

        const char *perm_str = "View";
        switch (peer->permission) {
            case DRAWNET_PERM_VIEW:  perm_str = "View"; break;
            case DRAWNET_PERM_DRAW:  perm_str = "Draw"; break;
            case DRAWNET_PERM_EDIT:  perm_str = "Edit"; break;
            case DRAWNET_PERM_ADMIN: perm_str = "Admin"; break;
        }

        const char *status = peer->connected ? "Online" : "Offline";
        char latency[64];
        snprintf(latency, sizeof(latency), "%.0fms", peer->latency_ms);

        gtk_list_store_set(artos->drawnet_peers_store, &iter,
                           0, peer->name,
                           1, perm_str,
                           2, status,
                           3, latency,
                           -1);

        peer = peer->next;
    }
}

/* Internal: Add chat message */
static void drawnet_add_chat_message(phantom_artos_t *artos, const char *name, const char *msg) {
    if (!artos || !artos->drawnet_chat_buffer) return;

    GtkTextIter end;
    gtk_text_buffer_get_end_iter(artos->drawnet_chat_buffer, &end);

    /* Get timestamp */
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char timestamp[16];
    strftime(timestamp, sizeof(timestamp), "%H:%M", tm);

    /* Format message */
    char formatted[512];
    snprintf(formatted, sizeof(formatted), "[%s] %s: %s\n", timestamp, name, msg);

    gtk_text_buffer_insert(artos->drawnet_chat_buffer, &end, formatted, -1);

    /* Auto-scroll to bottom */
    if (artos->drawnet_chat_view) {
        GtkTextMark *mark = gtk_text_buffer_get_insert(artos->drawnet_chat_buffer);
        gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(artos->drawnet_chat_view),
                                     mark, 0.0, FALSE, 0, 0);
    }
}

/* Callback: Toggle DrawNet */
static void on_drawnet_toggle(GtkToggleButton *button, phantom_artos_t *artos) {
    artos_drawnet_enable(artos, gtk_toggle_button_get_active(button));
}

/* Callback: Host button */
static void on_drawnet_host_clicked(GtkButton *button, phantom_artos_t *artos) {
    (void)button;
    if (artos->drawnet.state != DRAWNET_STATE_DISCONNECTED) {
        artos_drawnet_leave_session(artos);
    }
    artos_drawnet_host_session(artos, NULL);
}

/* Callback: Join button */
static void on_drawnet_join_clicked(GtkButton *button, phantom_artos_t *artos) {
    (void)button;
    if (artos->drawnet.state != DRAWNET_STATE_DISCONNECTED) {
        artos_drawnet_leave_session(artos);
    }

    const char *code = gtk_entry_get_text(GTK_ENTRY(artos->drawnet_session_entry));
    if (code && *code) {
        artos_drawnet_join_session(artos, code);
    }
}

/* Callback: Scan button */
static void on_drawnet_scan_clicked(GtkButton *button, phantom_artos_t *artos) {
    (void)button;
    if (artos->drawnet.state == DRAWNET_STATE_DISCOVERING) {
        artos_drawnet_scan_stop(artos);
    } else {
        artos_drawnet_scan_start(artos);
    }
}

/* Callback: Send chat */
static void on_drawnet_send_clicked(GtkButton *button, phantom_artos_t *artos) {
    (void)button;
    const char *msg = gtk_entry_get_text(GTK_ENTRY(artos->drawnet_chat_entry));
    if (msg && *msg) {
        artos_drawnet_send_chat(artos, msg);
        gtk_entry_set_text(GTK_ENTRY(artos->drawnet_chat_entry), "");
    }
}

/* Callback: Name changed */
static void on_drawnet_name_changed(GtkEntry *entry, phantom_artos_t *artos) {
    const char *name = gtk_entry_get_text(entry);
    if (name && *name) {
        artos_drawnet_set_username(artos, name);
    }
}

/* Callback: Sync mode changed */
static void on_drawnet_sync_changed(GtkComboBox *combo, phantom_artos_t *artos) {
    int active = gtk_combo_box_get_active(combo);
    if (active >= 0) {
        artos_drawnet_set_sync_mode(artos, (artos_drawnet_sync_t)active);
    }
}

/* Callback: Cursor sharing toggled */
static void on_drawnet_cursor_toggled(GtkToggleButton *button, phantom_artos_t *artos) {
    artos->drawnet.config.share_cursor = gtk_toggle_button_get_active(button);
}

/* Callback: Discovered session selected */
static void on_drawnet_discovered_select(GtkTreeSelection *sel, phantom_artos_t *artos) {
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (gtk_tree_selection_get_selected(sel, &model, &iter)) {
        char *host;
        int port;
        gtk_tree_model_get(model, &iter, 1, &host, 2, &port, -1);

        if (host) {
            gtk_entry_set_text(GTK_ENTRY(artos->drawnet_ip_entry), host);
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(artos->drawnet_port_spin), port);
            g_free(host);
        }
    }
}

/* Mini preview draw callback */
static gboolean on_drawnet_preview_draw(GtkWidget *widget, cairo_t *cr, phantom_artos_t *artos) {
    (void)widget;

    /* Draw background */
    cairo_set_source_rgb(cr, 0.2, 0.2, 0.25);
    cairo_paint(cr);

    /* Get preview dimensions */
    GtkAllocation alloc;
    gtk_widget_get_allocation(widget, &alloc);

    if (!artos->document) return FALSE;

    double scale_x = (double)alloc.width / artos->document->width;
    double scale_y = (double)alloc.height / artos->document->height;
    double scale = fmin(scale_x, scale_y);

    /* Draw peer cursors */
    artos_drawnet_peer_t *peer = artos->drawnet.peers;
    while (peer) {
        if (peer->show_cursor && peer->cursor_opacity > 0.1) {
            double x = peer->cursor_x * scale;
            double y = peer->cursor_y * scale;

            /* Cursor circle */
            cairo_set_source_rgba(cr, peer->cursor_color.r, peer->cursor_color.g,
                                  peer->cursor_color.b, peer->cursor_opacity);
            cairo_arc(cr, x, y, 5, 0, 2 * M_PI);
            cairo_fill(cr);

            /* Name label */
            if (peer->peer_id != artos->drawnet.local_id) {
                cairo_set_font_size(cr, 8);
                cairo_move_to(cr, x + 8, y + 3);
                cairo_show_text(cr, peer->name);
            }
        }
        peer = peer->next;
    }

    return FALSE;
}

/* Create DrawNet UI panel */
GtkWidget *artos_create_drawnet_panel(phantom_artos_t *artos) {
    GtkWidget *frame = gtk_frame_new("DrawNet - Multi-User Drawing");
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(box), 10);

    /* Initialize DrawNet */
    artos_drawnet_init(artos);

    /* Enable toggle */
    artos->drawnet_toggle = gtk_toggle_button_new_with_label("Enable DrawNet");
    g_signal_connect(artos->drawnet_toggle, "toggled", G_CALLBACK(on_drawnet_toggle), artos);
    gtk_box_pack_start(GTK_BOX(box), artos->drawnet_toggle, FALSE, FALSE, 0);

    /* Username entry */
    GtkWidget *name_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget *name_label = gtk_label_new("Your name:");
    gtk_box_pack_start(GTK_BOX(name_box), name_label, FALSE, FALSE, 0);
    artos->drawnet_name_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(artos->drawnet_name_entry), "Artist");
    g_signal_connect(artos->drawnet_name_entry, "changed", G_CALLBACK(on_drawnet_name_changed), artos);
    gtk_box_pack_start(GTK_BOX(name_box), artos->drawnet_name_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), name_box, FALSE, FALSE, 0);

    /* Separator */
    gtk_box_pack_start(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 5);

    /* Host/Join/Scan buttons */
    GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    artos->drawnet_host_btn = gtk_button_new_with_label("Host");
    artos->drawnet_join_btn = gtk_button_new_with_label("Join");
    artos->drawnet_scan_btn = gtk_button_new_with_label("Scan");
    g_signal_connect(artos->drawnet_host_btn, "clicked", G_CALLBACK(on_drawnet_host_clicked), artos);
    g_signal_connect(artos->drawnet_join_btn, "clicked", G_CALLBACK(on_drawnet_join_clicked), artos);
    g_signal_connect(artos->drawnet_scan_btn, "clicked", G_CALLBACK(on_drawnet_scan_clicked), artos);
    gtk_box_pack_start(GTK_BOX(btn_box), artos->drawnet_host_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(btn_box), artos->drawnet_join_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(btn_box), artos->drawnet_scan_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), btn_box, FALSE, FALSE, 0);

    /* Session code entry */
    GtkWidget *code_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget *code_label = gtk_label_new("Session:");
    gtk_box_pack_start(GTK_BOX(code_box), code_label, FALSE, FALSE, 0);
    artos->drawnet_session_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(artos->drawnet_session_entry), "Enter code...");
    gtk_entry_set_max_length(GTK_ENTRY(artos->drawnet_session_entry), 8);
    gtk_box_pack_start(GTK_BOX(code_box), artos->drawnet_session_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), code_box, FALSE, FALSE, 0);

    /* Direct IP connection */
    GtkWidget *ip_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget *ip_label = gtk_label_new("Direct IP:");
    gtk_box_pack_start(GTK_BOX(ip_box), ip_label, FALSE, FALSE, 0);
    artos->drawnet_ip_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(artos->drawnet_ip_entry), "192.168.1.x");
    gtk_box_pack_start(GTK_BOX(ip_box), artos->drawnet_ip_entry, TRUE, TRUE, 0);
    artos->drawnet_port_spin = gtk_spin_button_new_with_range(1024, 65535, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(artos->drawnet_port_spin), DRAWNET_DEFAULT_PORT);
    gtk_box_pack_start(GTK_BOX(ip_box), artos->drawnet_port_spin, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), ip_box, FALSE, FALSE, 0);

    /* Status labels */
    artos->drawnet_status_label = gtk_label_new("Disconnected");
    gtk_widget_set_halign(artos->drawnet_status_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), artos->drawnet_status_label, FALSE, FALSE, 0);

    artos->drawnet_code_label = gtk_label_new("");
    gtk_widget_set_halign(artos->drawnet_code_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), artos->drawnet_code_label, FALSE, FALSE, 0);

    artos->drawnet_stats_label = gtk_label_new("");
    gtk_widget_set_halign(artos->drawnet_stats_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), artos->drawnet_stats_label, FALSE, FALSE, 0);

    /* Sync progress bar */
    artos->drawnet_progress_bar = gtk_progress_bar_new();
    gtk_widget_set_no_show_all(artos->drawnet_progress_bar, TRUE);
    gtk_box_pack_start(GTK_BOX(box), artos->drawnet_progress_bar, FALSE, FALSE, 0);

    /* Separator */
    gtk_box_pack_start(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 5);

    /* Settings */
    GtkWidget *settings_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);

    /* Sync mode combo */
    GtkWidget *sync_label = gtk_label_new("Sync:");
    gtk_box_pack_start(GTK_BOX(settings_box), sync_label, FALSE, FALSE, 0);
    artos->drawnet_sync_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(artos->drawnet_sync_combo), "Realtime");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(artos->drawnet_sync_combo), "Stroke");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(artos->drawnet_sync_combo), "Interval");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(artos->drawnet_sync_combo), "Manual");
    gtk_combo_box_set_active(GTK_COMBO_BOX(artos->drawnet_sync_combo), 0);
    g_signal_connect(artos->drawnet_sync_combo, "changed", G_CALLBACK(on_drawnet_sync_changed), artos);
    gtk_box_pack_start(GTK_BOX(settings_box), artos->drawnet_sync_combo, FALSE, FALSE, 0);

    /* Cursor sharing checkbox */
    artos->drawnet_cursor_check = gtk_check_button_new_with_label("Cursors");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(artos->drawnet_cursor_check), TRUE);
    g_signal_connect(artos->drawnet_cursor_check, "toggled", G_CALLBACK(on_drawnet_cursor_toggled), artos);
    gtk_box_pack_start(GTK_BOX(settings_box), artos->drawnet_cursor_check, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(box), settings_box, FALSE, FALSE, 0);

    /* Separator */
    gtk_box_pack_start(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 5);

    /* Discovered sessions list */
    GtkWidget *disc_label = gtk_label_new("Discovered Sessions:");
    gtk_widget_set_halign(disc_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), disc_label, FALSE, FALSE, 0);

    artos->drawnet_discovered_store = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT);
    artos->drawnet_discovered_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(artos->drawnet_discovered_store));

    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes("Name", renderer, "text", 0, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(artos->drawnet_discovered_tree), col);
    col = gtk_tree_view_column_new_with_attributes("Host", renderer, "text", 1, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(artos->drawnet_discovered_tree), col);

    GtkTreeSelection *disc_sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(artos->drawnet_discovered_tree));
    g_signal_connect(disc_sel, "changed", G_CALLBACK(on_drawnet_discovered_select), artos);

    GtkWidget *disc_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(disc_scroll), 50);
    gtk_container_add(GTK_CONTAINER(disc_scroll), artos->drawnet_discovered_tree);
    gtk_box_pack_start(GTK_BOX(box), disc_scroll, FALSE, FALSE, 0);

    /* Separator */
    gtk_box_pack_start(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 5);

    /* Connected peers list */
    GtkWidget *peers_label = gtk_label_new("Connected Peers:");
    gtk_widget_set_halign(peers_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), peers_label, FALSE, FALSE, 0);

    artos->drawnet_peers_store = gtk_list_store_new(4, G_TYPE_STRING, G_TYPE_STRING,
                                                     G_TYPE_STRING, G_TYPE_STRING);
    artos->drawnet_peers_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(artos->drawnet_peers_store));

    col = gtk_tree_view_column_new_with_attributes("Name", renderer, "text", 0, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(artos->drawnet_peers_tree), col);
    col = gtk_tree_view_column_new_with_attributes("Perm", renderer, "text", 1, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(artos->drawnet_peers_tree), col);
    col = gtk_tree_view_column_new_with_attributes("Status", renderer, "text", 2, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(artos->drawnet_peers_tree), col);
    col = gtk_tree_view_column_new_with_attributes("Ping", renderer, "text", 3, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(artos->drawnet_peers_tree), col);

    GtkWidget *peers_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(peers_scroll), 60);
    gtk_container_add(GTK_CONTAINER(peers_scroll), artos->drawnet_peers_tree);
    gtk_box_pack_start(GTK_BOX(box), peers_scroll, FALSE, FALSE, 0);

    /* Mini cursor preview */
    artos->drawnet_canvas_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(artos->drawnet_canvas_area, -1, 60);
    g_signal_connect(artos->drawnet_canvas_area, "draw", G_CALLBACK(on_drawnet_preview_draw), artos);
    gtk_box_pack_start(GTK_BOX(box), artos->drawnet_canvas_area, FALSE, FALSE, 0);

    /* Separator */
    gtk_box_pack_start(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 5);

    /* Chat */
    GtkWidget *chat_label = gtk_label_new("Chat:");
    gtk_widget_set_halign(chat_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), chat_label, FALSE, FALSE, 0);

    artos->drawnet_chat_buffer = gtk_text_buffer_new(NULL);
    artos->drawnet_chat_view = gtk_text_view_new_with_buffer(artos->drawnet_chat_buffer);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(artos->drawnet_chat_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(artos->drawnet_chat_view), GTK_WRAP_WORD);

    GtkWidget *chat_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(chat_scroll), 80);
    gtk_container_add(GTK_CONTAINER(chat_scroll), artos->drawnet_chat_view);
    gtk_box_pack_start(GTK_BOX(box), chat_scroll, TRUE, TRUE, 0);

    /* Chat input */
    GtkWidget *chat_input_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    artos->drawnet_chat_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(artos->drawnet_chat_entry), "Type message...");
    gtk_box_pack_start(GTK_BOX(chat_input_box), artos->drawnet_chat_entry, TRUE, TRUE, 0);

    artos->drawnet_send_btn = gtk_button_new_with_label("Send");
    g_signal_connect(artos->drawnet_send_btn, "clicked", G_CALLBACK(on_drawnet_send_clicked), artos);
    gtk_box_pack_start(GTK_BOX(chat_input_box), artos->drawnet_send_btn, FALSE, FALSE, 0);

    /* Reaction buttons */
    GtkWidget *react_btn = gtk_button_new_with_label("👍");
    g_signal_connect_swapped(react_btn, "clicked",
                             G_CALLBACK(artos_drawnet_send_reaction), artos);
    g_object_set_data(G_OBJECT(react_btn), "emoji", "👍");
    gtk_box_pack_start(GTK_BOX(chat_input_box), react_btn, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(box), chat_input_box, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(frame), box);
    artos->drawnet_panel = frame;

    return frame;
}

/* ==============================================================================
 * Creative Journal Implementation
 * ============================================================================== */

int artos_journal_init(phantom_artos_t *artos) {
    if (!artos) return 0;

    memset(&artos->journal, 0, sizeof(artos_journal_t));
    artos->journal.enabled = 1;
    artos->journal.auto_log = 1;
    artos->journal.idle_timeout_secs = 300;  /* 5 minutes */
    artos->journal.next_session_id = 1;

    return 1;
}

void artos_journal_cleanup(phantom_artos_t *artos) {
    if (!artos) return;

    /* Free session history */
    artos_journal_session_t *session = artos->journal.sessions;
    while (session) {
        artos_journal_session_t *next = session->next;
        if (session->thumbnail) {
            cairo_surface_destroy(session->thumbnail);
        }
        free(session);
        session = next;
    }

    /* Free entry log */
    artos_journal_entry_t *entry = artos->journal.entries;
    while (entry) {
        artos_journal_entry_t *next = entry->next;
        free(entry);
        entry = next;
    }

    /* Free snapshots */
    for (int i = 0; i < artos->journal.snapshot_count; i++) {
        if (artos->journal.snapshots[i].thumbnail) {
            cairo_surface_destroy(artos->journal.snapshots[i].thumbnail);
        }
    }

    memset(&artos->journal, 0, sizeof(artos_journal_t));
}

void artos_journal_enable(phantom_artos_t *artos, int enable) {
    if (!artos) return;
    artos->journal.enabled = enable;
}

void artos_journal_start_session(phantom_artos_t *artos) {
    if (!artos || !artos->journal.enabled) return;

    /* End any existing session */
    if (artos->journal.current_session) {
        artos_journal_end_session(artos);
    }

    /* Create new session */
    artos_journal_session_t *session = calloc(1, sizeof(artos_journal_session_t));
    if (!session) return;

    session->session_id = artos->journal.next_session_id++;
    session->start_time = time(NULL);
    artos->journal.session_start = session->start_time;

    artos->journal.current_session = session;
    artos->journal.last_activity = session->start_time;

    /* Log session start */
    artos_journal_entry_t *entry = calloc(1, sizeof(artos_journal_entry_t));
    if (entry) {
        entry->type = JOURNAL_ENTRY_SESSION_START;
        entry->timestamp = session->start_time;
        entry->session_id = session->session_id;
        entry->next = artos->journal.entries;
        artos->journal.entries = entry;
        artos->journal.entry_count++;
    }

    printf("[Journal] Session %u started\n", session->session_id);
}

void artos_journal_end_session(phantom_artos_t *artos) {
    if (!artos || !artos->journal.current_session) return;

    artos_journal_session_t *session = artos->journal.current_session;
    session->end_time = time(NULL);
    session->duration_secs = difftime(session->end_time, session->start_time);

    /* Create thumbnail */
    session->thumbnail = artos_journal_get_thumbnail(artos);

    /* Add to session list */
    session->next = artos->journal.sessions;
    artos->journal.sessions = session;
    artos->journal.session_count++;
    artos->journal.total_sessions++;
    artos->journal.total_hours += session->duration_secs / 3600.0;
    artos->journal.total_strokes += session->stroke_count;

    /* Log session end */
    artos_journal_entry_t *entry = calloc(1, sizeof(artos_journal_entry_t));
    if (entry) {
        entry->type = JOURNAL_ENTRY_SESSION_END;
        entry->timestamp = session->end_time;
        entry->session_id = session->session_id;
        entry->next = artos->journal.entries;
        artos->journal.entries = entry;
        artos->journal.entry_count++;
    }

    printf("[Journal] Session %u ended - %d strokes, %.1f minutes\n",
           session->session_id, session->stroke_count, session->duration_secs / 60.0);

    artos->journal.current_session = NULL;
    artos->journal.modified = 1;
}

void artos_journal_log_stroke(phantom_artos_t *artos) {
    if (!artos || !artos->journal.enabled || !artos->journal.current_session) return;

    artos->journal.current_session->stroke_count++;
    artos->journal.last_activity = time(NULL);

    /* Log every 10th stroke to avoid excessive entries */
    if (artos->journal.current_session->stroke_count % 10 == 0) {
        artos_journal_entry_t *entry = calloc(1, sizeof(artos_journal_entry_t));
        if (entry) {
            entry->type = JOURNAL_ENTRY_STROKE;
            entry->timestamp = time(NULL);
            entry->session_id = artos->journal.current_session->session_id;
            entry->data.stroke.stroke_count = artos->journal.current_session->stroke_count;
            entry->next = artos->journal.entries;
            artos->journal.entries = entry;
            artos->journal.entry_count++;
        }
    }
}

void artos_journal_log_tool_change(phantom_artos_t *artos, artos_tool_t old_tool, artos_tool_t new_tool) {
    if (!artos || !artos->journal.enabled || !artos->journal.current_session) return;

    artos->journal.current_session->tool_changes++;
    artos->journal.last_activity = time(NULL);

    artos_journal_entry_t *entry = calloc(1, sizeof(artos_journal_entry_t));
    if (entry) {
        entry->type = JOURNAL_ENTRY_TOOL_CHANGE;
        entry->timestamp = time(NULL);
        entry->session_id = artos->journal.current_session->session_id;
        entry->data.tool_change.old_tool = old_tool;
        entry->data.tool_change.new_tool = new_tool;
        entry->next = artos->journal.entries;
        artos->journal.entries = entry;
        artos->journal.entry_count++;
    }
}

void artos_journal_log_color_change(phantom_artos_t *artos, artos_color_t *old_color, artos_color_t *new_color) {
    if (!artos || !artos->journal.enabled || !artos->journal.current_session) return;

    artos->journal.current_session->color_changes++;
    artos->journal.last_activity = time(NULL);

    artos_journal_entry_t *entry = calloc(1, sizeof(artos_journal_entry_t));
    if (entry) {
        entry->type = JOURNAL_ENTRY_COLOR_CHANGE;
        entry->timestamp = time(NULL);
        entry->session_id = artos->journal.current_session->session_id;
        if (old_color) entry->data.color_change.old_color = *old_color;
        if (new_color) entry->data.color_change.new_color = *new_color;
        entry->next = artos->journal.entries;
        artos->journal.entries = entry;
        artos->journal.entry_count++;
    }
}

void artos_journal_log_layer_op(phantom_artos_t *artos, const char *operation, int layer_index) {
    if (!artos || !artos->journal.enabled || !artos->journal.current_session) return;

    artos->journal.current_session->layers_created++;
    artos->journal.last_activity = time(NULL);

    artos_journal_entry_t *entry = calloc(1, sizeof(artos_journal_entry_t));
    if (entry) {
        entry->type = JOURNAL_ENTRY_LAYER_OP;
        entry->timestamp = time(NULL);
        entry->session_id = artos->journal.current_session->session_id;
        strncpy(entry->data.layer_op.operation, operation, sizeof(entry->data.layer_op.operation) - 1);
        entry->data.layer_op.layer_index = layer_index;
        entry->next = artos->journal.entries;
        artos->journal.entries = entry;
        artos->journal.entry_count++;
    }
}

void artos_journal_add_note(phantom_artos_t *artos, const char *note) {
    if (!artos || !artos->journal.enabled || !note) return;

    artos_journal_entry_t *entry = calloc(1, sizeof(artos_journal_entry_t));
    if (entry) {
        entry->type = JOURNAL_ENTRY_NOTE;
        entry->timestamp = time(NULL);
        if (artos->journal.current_session) {
            entry->session_id = artos->journal.current_session->session_id;
        }
        strncpy(entry->data.note.note, note, sizeof(entry->data.note.note) - 1);
        entry->next = artos->journal.entries;
        artos->journal.entries = entry;
        artos->journal.entry_count++;
    }

    /* Also add to current session notes */
    if (artos->journal.current_session) {
        size_t len = strlen(artos->journal.current_session->notes);
        if (len < JOURNAL_MAX_NOTES - 2) {
            if (len > 0) {
                strcat(artos->journal.current_session->notes, "\n");
                len++;
            }
            strncat(artos->journal.current_session->notes, note,
                    JOURNAL_MAX_NOTES - len - 1);
        }
    }

    artos->journal.modified = 1;
    printf("[Journal] Note added: %s\n", note);
}

void artos_journal_mark_milestone(phantom_artos_t *artos, const char *name) {
    if (!artos || !artos->journal.enabled) return;

    artos_journal_entry_t *entry = calloc(1, sizeof(artos_journal_entry_t));
    if (entry) {
        entry->type = JOURNAL_ENTRY_MILESTONE;
        entry->timestamp = time(NULL);
        if (artos->journal.current_session) {
            entry->session_id = artos->journal.current_session->session_id;
            artos->journal.current_session->has_milestone = 1;
            strncpy(artos->journal.current_session->milestone_name, name,
                    sizeof(artos->journal.current_session->milestone_name) - 1);
        }
        strncpy(entry->data.note.note, name, sizeof(entry->data.note.note) - 1);
        entry->next = artos->journal.entries;
        artos->journal.entries = entry;
        artos->journal.entry_count++;
    }

    /* Create a snapshot at milestone */
    artos_journal_create_snapshot(artos, name);

    artos->journal.modified = 1;
    printf("[Journal] Milestone marked: %s\n", name);
}

void artos_journal_create_snapshot(phantom_artos_t *artos, const char *description) {
    if (!artos || artos->journal.snapshot_count >= 100) return;

    int idx = artos->journal.snapshot_count;
    artos->journal.snapshots[idx].timestamp = time(NULL);
    strncpy(artos->journal.snapshots[idx].description, description,
            sizeof(artos->journal.snapshots[idx].description) - 1);
    artos->journal.snapshots[idx].thumbnail = artos_journal_get_thumbnail(artos);

    /* Generate simple hash from thumbnail data */
    snprintf(artos->journal.snapshots[idx].hash, 65, "snapshot_%d_%ld",
             idx, (long)artos->journal.snapshots[idx].timestamp);

    artos->journal.snapshot_count++;
    printf("[Journal] Snapshot created: %s\n", description);
}

cairo_surface_t *artos_journal_get_thumbnail(phantom_artos_t *artos) {
    if (!artos || !artos->document || !artos->document->composite) return NULL;

    int src_w = artos->document->width;
    int src_h = artos->document->height;

    /* Calculate thumbnail size maintaining aspect ratio */
    double scale = (double)JOURNAL_THUMBNAIL_SIZE / fmax(src_w, src_h);
    int thumb_w = (int)(src_w * scale);
    int thumb_h = (int)(src_h * scale);

    cairo_surface_t *thumbnail = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, thumb_w, thumb_h);
    cairo_t *cr = cairo_create(thumbnail);

    /* Scale and draw composite */
    cairo_scale(cr, scale, scale);
    artos_update_composite(artos->document);
    cairo_set_source_surface(cr, artos->document->composite, 0, 0);
    cairo_paint(cr);

    cairo_destroy(cr);
    return thumbnail;
}

artos_journal_session_t *artos_journal_get_sessions(phantom_artos_t *artos) {
    if (!artos) return NULL;
    return artos->journal.sessions;
}

int artos_journal_get_session_count(phantom_artos_t *artos) {
    if (!artos) return 0;
    return artos->journal.session_count;
}

/* Journal panel callbacks */
static void on_journal_toggle(GtkToggleButton *button, phantom_artos_t *artos) {
    artos_journal_enable(artos, gtk_toggle_button_get_active(button));
}

static void on_journal_note_activate(GtkEntry *entry, phantom_artos_t *artos) {
    const char *note = gtk_entry_get_text(entry);
    if (note && strlen(note) > 0) {
        artos_journal_add_note(artos, note);
        gtk_entry_set_text(entry, "");
    }
}

static void on_journal_milestone_clicked(GtkButton *button, phantom_artos_t *artos) {
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Mark Milestone",
        GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(button))),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Mark", GTK_RESPONSE_OK,
        NULL);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Milestone name...");
    gtk_box_pack_start(GTK_BOX(content), entry, TRUE, TRUE, 10);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        const char *name = gtk_entry_get_text(GTK_ENTRY(entry));
        if (name && strlen(name) > 0) {
            artos_journal_mark_milestone(artos, name);
        }
    }
    gtk_widget_destroy(dialog);
}

static gboolean on_journal_thumbnail_draw(GtkWidget *widget, cairo_t *cr, phantom_artos_t *artos) {
    if (!artos->journal.current_session || !artos->journal.current_session->thumbnail) {
        /* Draw placeholder */
        cairo_set_source_rgb(cr, 0.3, 0.3, 0.3);
        cairo_paint(cr);
        cairo_set_source_rgb(cr, 0.6, 0.6, 0.6);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 12);
        cairo_move_to(cr, 10, 30);
        cairo_show_text(cr, "No preview");
        return TRUE;
    }

    cairo_surface_t *thumb = artos->journal.current_session->thumbnail;
    int w = cairo_image_surface_get_width(thumb);
    int h = cairo_image_surface_get_height(thumb);

    GtkAllocation alloc;
    gtk_widget_get_allocation(widget, &alloc);
    double scale = fmin((double)alloc.width / w, (double)alloc.height / h);

    cairo_translate(cr, (alloc.width - w * scale) / 2, (alloc.height - h * scale) / 2);
    cairo_scale(cr, scale, scale);
    cairo_set_source_surface(cr, thumb, 0, 0);
    cairo_paint(cr);

    return TRUE;
}

static void artos_journal_refresh_stats(phantom_artos_t *artos) {
    if (!artos || !artos->journal_stats_label) return;

    char stats[256];
    snprintf(stats, sizeof(stats),
             "Sessions: %d | Strokes: %d | Time: %.1fh",
             artos->journal.total_sessions,
             artos->journal.total_strokes,
             artos->journal.total_hours);

    gtk_label_set_text(GTK_LABEL(artos->journal_stats_label), stats);
}

GtkWidget *artos_create_journal_panel(phantom_artos_t *artos) {
    GtkWidget *frame = gtk_frame_new("Creative Journal");
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(box), 5);

    /* Enable toggle */
    artos->journal_toggle = gtk_check_button_new_with_label("Enable Journal");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(artos->journal_toggle), artos->journal.enabled);
    g_signal_connect(artos->journal_toggle, "toggled", G_CALLBACK(on_journal_toggle), artos);
    gtk_box_pack_start(GTK_BOX(box), artos->journal_toggle, FALSE, FALSE, 0);

    /* Statistics label */
    artos->journal_stats_label = gtk_label_new("Sessions: 0 | Strokes: 0 | Time: 0h");
    gtk_widget_set_halign(artos->journal_stats_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), artos->journal_stats_label, FALSE, FALSE, 0);

    /* Note entry */
    GtkWidget *note_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    artos->journal_note_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(artos->journal_note_entry), "Add note...");
    g_signal_connect(artos->journal_note_entry, "activate", G_CALLBACK(on_journal_note_activate), artos);
    gtk_box_pack_start(GTK_BOX(note_box), artos->journal_note_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), note_box, FALSE, FALSE, 0);

    /* Milestone button */
    artos->journal_milestone_btn = gtk_button_new_with_label("Mark Milestone");
    g_signal_connect(artos->journal_milestone_btn, "clicked", G_CALLBACK(on_journal_milestone_clicked), artos);
    gtk_box_pack_start(GTK_BOX(box), artos->journal_milestone_btn, FALSE, FALSE, 0);

    /* Thumbnail preview */
    artos->journal_thumbnail_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(artos->journal_thumbnail_area, -1, 100);
    g_signal_connect(artos->journal_thumbnail_area, "draw", G_CALLBACK(on_journal_thumbnail_draw), artos);
    gtk_box_pack_start(GTK_BOX(box), artos->journal_thumbnail_area, FALSE, FALSE, 0);

    /* Session history list */
    GtkWidget *sessions_label = gtk_label_new("Session History:");
    gtk_widget_set_halign(sessions_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), sessions_label, FALSE, FALSE, 0);

    artos->journal_sessions_store = gtk_list_store_new(4, G_TYPE_STRING, G_TYPE_STRING,
                                                        G_TYPE_STRING, G_TYPE_INT);
    artos->journal_sessions_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(artos->journal_sessions_store));

    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes("Date", renderer, "text", 0, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(artos->journal_sessions_tree), col);
    col = gtk_tree_view_column_new_with_attributes("Duration", renderer, "text", 1, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(artos->journal_sessions_tree), col);
    col = gtk_tree_view_column_new_with_attributes("Strokes", renderer, "text", 2, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(artos->journal_sessions_tree), col);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll), 100);
    gtk_container_add(GTK_CONTAINER(scroll), artos->journal_sessions_tree);
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);

    /* Export button */
    artos->journal_export_btn = gtk_button_new_with_label("Export Journal");
    gtk_box_pack_start(GTK_BOX(box), artos->journal_export_btn, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(frame), box);
    artos->journal_panel = frame;

    return frame;
}

/* ==============================================================================
 * Voice Commands Implementation
 * ============================================================================== */

/* Default voice command actions */
static void voicecmd_action_undo(phantom_artos_t *artos, const char *params) {
    (void)params;
    artos_undo(artos);
}

static void voicecmd_action_redo(phantom_artos_t *artos, const char *params) {
    (void)params;
    artos_redo(artos);
}

static void voicecmd_action_zoom_in(phantom_artos_t *artos, const char *params) {
    (void)params;
    artos_zoom_in(artos);
}

static void voicecmd_action_zoom_out(phantom_artos_t *artos, const char *params) {
    (void)params;
    artos_zoom_out(artos);
}

static void voicecmd_action_zoom_fit(phantom_artos_t *artos, const char *params) {
    (void)params;
    artos_zoom_fit(artos);
}

static void voicecmd_action_tool_brush(phantom_artos_t *artos, const char *params) {
    (void)params;
    artos_set_tool(artos, ARTOS_TOOL_BRUSH);
}

static void voicecmd_action_tool_pencil(phantom_artos_t *artos, const char *params) {
    (void)params;
    artos_set_tool(artos, ARTOS_TOOL_PENCIL);
}

static void voicecmd_action_tool_eraser(phantom_artos_t *artos, const char *params) {
    (void)params;
    artos_set_tool(artos, ARTOS_TOOL_ERASER);
}

static void voicecmd_action_tool_pen(phantom_artos_t *artos, const char *params) {
    (void)params;
    artos_set_tool(artos, ARTOS_TOOL_PEN);
}

static void voicecmd_action_tool_bucket(phantom_artos_t *artos, const char *params) {
    (void)params;
    artos_set_tool(artos, ARTOS_TOOL_BUCKET);
}

static void voicecmd_action_tool_eyedropper(phantom_artos_t *artos, const char *params) {
    (void)params;
    artos_set_tool(artos, ARTOS_TOOL_EYEDROPPER);
}

static void voicecmd_action_new_layer(phantom_artos_t *artos, const char *params) {
    (void)params;
    if (artos->document) {
        artos_layer_add(artos->document, "New Layer");
        artos_refresh_layer_list(artos);
    }
}

static void voicecmd_action_swap_colors(phantom_artos_t *artos, const char *params) {
    (void)params;
    artos_swap_colors(artos);
}

static void voicecmd_action_select_all(phantom_artos_t *artos, const char *params) {
    (void)params;
    artos_select_all(artos);
}

static void voicecmd_action_deselect(phantom_artos_t *artos, const char *params) {
    (void)params;
    artos_select_none(artos);
}

static void voicecmd_action_brush_bigger(phantom_artos_t *artos, const char *params) {
    (void)params;
    artos->current_brush.size = fmin(artos->current_brush.size + 5, 200);
    if (artos->brush_size_scale) {
        gtk_range_set_value(GTK_RANGE(artos->brush_size_scale), artos->current_brush.size);
    }
}

static void voicecmd_action_brush_smaller(phantom_artos_t *artos, const char *params) {
    (void)params;
    artos->current_brush.size = fmax(artos->current_brush.size - 5, 1);
    if (artos->brush_size_scale) {
        gtk_range_set_value(GTK_RANGE(artos->brush_size_scale), artos->current_brush.size);
    }
}

int artos_voicecmd_init(phantom_artos_t *artos) {
    if (!artos) return 0;

    memset(&artos->voice_commands, 0, sizeof(artos_voice_commands_t));
    artos->voice_commands.confidence_threshold = 0.6;
    strcpy(artos->voice_commands.wake_word, "hey artos");
    artos->voice_commands.require_wake_word = 0;
    artos->voice_commands.audio_feedback = 1;
    artos->voice_commands.visual_feedback = 1;

    /* Register default commands */
    artos_voicecmd_init_defaults(artos);

    return 1;
}

void artos_voicecmd_cleanup(phantom_artos_t *artos) {
    if (!artos) return;

    /* Free command list */
    artos_voice_command_t *cmd = artos->voice_commands.commands;
    while (cmd) {
        artos_voice_command_t *next = cmd->next;
        free(cmd);
        cmd = next;
    }

    /* Free custom commands */
    cmd = artos->voice_commands.custom_commands;
    while (cmd) {
        artos_voice_command_t *next = cmd->next;
        free(cmd);
        cmd = next;
    }

    memset(&artos->voice_commands, 0, sizeof(artos_voice_commands_t));
}

void artos_voicecmd_enable(phantom_artos_t *artos, int enable) {
    if (!artos) return;
    artos->voice_commands.enabled = enable;
}

void artos_voicecmd_register(phantom_artos_t *artos, const char *phrase,
                             artos_voice_cmd_category_t category,
                             void (*action)(phantom_artos_t*, const char*),
                             const char *params, const char *feedback) {
    if (!artos || !phrase || !action) return;

    artos_voice_command_t *cmd = calloc(1, sizeof(artos_voice_command_t));
    if (!cmd) return;

    strncpy(cmd->phrase, phrase, sizeof(cmd->phrase) - 1);
    cmd->category = category;
    cmd->action = action;
    if (params) strncpy(cmd->params, params, sizeof(cmd->params) - 1);
    if (feedback) strncpy(cmd->feedback, feedback, sizeof(cmd->feedback) - 1);
    cmd->beep_on_recognize = 1;

    cmd->next = artos->voice_commands.commands;
    artos->voice_commands.commands = cmd;
    artos->voice_commands.command_count++;
}

void artos_voicecmd_register_alias(phantom_artos_t *artos, const char *phrase, const char *alias) {
    if (!artos || !phrase || !alias) return;

    /* Find the command */
    artos_voice_command_t *cmd = artos->voice_commands.commands;
    while (cmd) {
        if (strcasecmp(cmd->phrase, phrase) == 0) {
            if (cmd->alias_count < 4) {
                strncpy(cmd->aliases[cmd->alias_count], alias,
                        sizeof(cmd->aliases[0]) - 1);
                cmd->alias_count++;
            }
            return;
        }
        cmd = cmd->next;
    }
}

void artos_voicecmd_init_defaults(phantom_artos_t *artos) {
    if (!artos) return;

    /* Tool commands */
    artos_voicecmd_register(artos, "brush", VOICE_CMD_TOOL, voicecmd_action_tool_brush, NULL, "Brush selected");
    artos_voicecmd_register(artos, "pencil", VOICE_CMD_TOOL, voicecmd_action_tool_pencil, NULL, "Pencil selected");
    artos_voicecmd_register(artos, "eraser", VOICE_CMD_TOOL, voicecmd_action_tool_eraser, NULL, "Eraser selected");
    artos_voicecmd_register(artos, "pen", VOICE_CMD_TOOL, voicecmd_action_tool_pen, NULL, "Pen selected");
    artos_voicecmd_register(artos, "fill", VOICE_CMD_TOOL, voicecmd_action_tool_bucket, NULL, "Fill tool selected");
    artos_voicecmd_register(artos, "eyedropper", VOICE_CMD_TOOL, voicecmd_action_tool_eyedropper, NULL, "Eyedropper selected");

    /* Add aliases */
    artos_voicecmd_register_alias(artos, "brush", "paintbrush");
    artos_voicecmd_register_alias(artos, "eraser", "erase");
    artos_voicecmd_register_alias(artos, "fill", "bucket");
    artos_voicecmd_register_alias(artos, "eyedropper", "color picker");

    /* Action commands */
    artos_voicecmd_register(artos, "undo", VOICE_CMD_ACTION, voicecmd_action_undo, NULL, "Undone");
    artos_voicecmd_register(artos, "redo", VOICE_CMD_ACTION, voicecmd_action_redo, NULL, "Redone");
    artos_voicecmd_register(artos, "new layer", VOICE_CMD_LAYER, voicecmd_action_new_layer, NULL, "Layer created");
    artos_voicecmd_register(artos, "swap colors", VOICE_CMD_COLOR, voicecmd_action_swap_colors, NULL, "Colors swapped");
    artos_voicecmd_register(artos, "select all", VOICE_CMD_SELECTION, voicecmd_action_select_all, NULL, "All selected");
    artos_voicecmd_register(artos, "deselect", VOICE_CMD_SELECTION, voicecmd_action_deselect, NULL, "Deselected");

    /* View commands */
    artos_voicecmd_register(artos, "zoom in", VOICE_CMD_VIEW, voicecmd_action_zoom_in, NULL, "Zoomed in");
    artos_voicecmd_register(artos, "zoom out", VOICE_CMD_VIEW, voicecmd_action_zoom_out, NULL, "Zoomed out");
    artos_voicecmd_register(artos, "zoom fit", VOICE_CMD_VIEW, voicecmd_action_zoom_fit, NULL, "Fit to window");

    /* Brush size commands */
    artos_voicecmd_register(artos, "bigger", VOICE_CMD_BRUSH, voicecmd_action_brush_bigger, NULL, "Brush bigger");
    artos_voicecmd_register(artos, "smaller", VOICE_CMD_BRUSH, voicecmd_action_brush_smaller, NULL, "Brush smaller");

    artos_voicecmd_register_alias(artos, "bigger", "larger");
    artos_voicecmd_register_alias(artos, "smaller", "thinner");

    printf("[VoiceCmd] Registered %d default commands\n", artos->voice_commands.command_count);
}

int artos_voicecmd_process(phantom_artos_t *artos, const char *phrase, double confidence) {
    if (!artos || !phrase || !artos->voice_commands.enabled) return 0;

    if (confidence < artos->voice_commands.confidence_threshold) {
        return 0;
    }

    /* Convert phrase to lowercase for matching */
    char lower[128];
    strncpy(lower, phrase, sizeof(lower) - 1);
    for (char *p = lower; *p; p++) *p = tolower(*p);

    /* Search for matching command */
    artos_voice_command_t *cmd = artos->voice_commands.commands;
    while (cmd) {
        /* Check main phrase */
        if (strstr(lower, cmd->phrase) != NULL) {
            cmd->action(artos, cmd->params);
            artos->voice_commands.last_command = cmd;
            strncpy(artos->voice_commands.last_phrase, phrase, sizeof(artos->voice_commands.last_phrase) - 1);
            artos->voice_commands.last_confidence = confidence;
            printf("[VoiceCmd] Executed: %s (%.0f%% confidence)\n", cmd->phrase, confidence * 100);
            return 1;
        }

        /* Check aliases */
        for (int i = 0; i < cmd->alias_count; i++) {
            if (strstr(lower, cmd->aliases[i]) != NULL) {
                cmd->action(artos, cmd->params);
                artos->voice_commands.last_command = cmd;
                strncpy(artos->voice_commands.last_phrase, phrase, sizeof(artos->voice_commands.last_phrase) - 1);
                artos->voice_commands.last_confidence = confidence;
                printf("[VoiceCmd] Executed (via alias): %s (%.0f%% confidence)\n", cmd->phrase, confidence * 100);
                return 1;
            }
        }

        cmd = cmd->next;
    }

    printf("[VoiceCmd] Unrecognized: %s\n", phrase);
    return 0;
}

/* Voice command panel callbacks */
static void on_voicecmd_toggle(GtkToggleButton *button, phantom_artos_t *artos) {
    artos_voicecmd_enable(artos, gtk_toggle_button_get_active(button));
}

static void on_voicecmd_threshold_changed(GtkRange *range, phantom_artos_t *artos) {
    artos->voice_commands.confidence_threshold = gtk_range_get_value(range);
}

static void artos_voicecmd_refresh_list(phantom_artos_t *artos) {
    if (!artos || !artos->voicecmd_commands_store) return;

    gtk_list_store_clear(artos->voicecmd_commands_store);

    artos_voice_command_t *cmd = artos->voice_commands.commands;
    while (cmd) {
        GtkTreeIter iter;
        gtk_list_store_append(artos->voicecmd_commands_store, &iter);

        const char *cat_name = "Custom";
        switch (cmd->category) {
            case VOICE_CMD_TOOL: cat_name = "Tool"; break;
            case VOICE_CMD_ACTION: cat_name = "Action"; break;
            case VOICE_CMD_VIEW: cat_name = "View"; break;
            case VOICE_CMD_COLOR: cat_name = "Color"; break;
            case VOICE_CMD_BRUSH: cat_name = "Brush"; break;
            case VOICE_CMD_LAYER: cat_name = "Layer"; break;
            case VOICE_CMD_SELECTION: cat_name = "Select"; break;
            case VOICE_CMD_FILE: cat_name = "File"; break;
            default: break;
        }

        gtk_list_store_set(artos->voicecmd_commands_store, &iter,
                           0, cmd->phrase,
                           1, cat_name,
                           2, cmd->feedback,
                           -1);
        cmd = cmd->next;
    }
}

GtkWidget *artos_create_voicecmd_panel(phantom_artos_t *artos) {
    GtkWidget *frame = gtk_frame_new("Voice Commands");
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(box), 5);

    /* Enable toggle */
    artos->voicecmd_toggle = gtk_check_button_new_with_label("Enable Voice Commands");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(artos->voicecmd_toggle), artos->voice_commands.enabled);
    g_signal_connect(artos->voicecmd_toggle, "toggled", G_CALLBACK(on_voicecmd_toggle), artos);
    gtk_box_pack_start(GTK_BOX(box), artos->voicecmd_toggle, FALSE, FALSE, 0);

    /* Status labels */
    artos->voicecmd_status_label = gtk_label_new("Status: Ready");
    gtk_widget_set_halign(artos->voicecmd_status_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), artos->voicecmd_status_label, FALSE, FALSE, 0);

    artos->voicecmd_phrase_label = gtk_label_new("Last: -");
    gtk_widget_set_halign(artos->voicecmd_phrase_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), artos->voicecmd_phrase_label, FALSE, FALSE, 0);

    /* Confidence threshold */
    GtkWidget *thresh_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(thresh_box), gtk_label_new("Threshold:"), FALSE, FALSE, 0);
    artos->voicecmd_threshold_scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0.3, 1.0, 0.05);
    gtk_range_set_value(GTK_RANGE(artos->voicecmd_threshold_scale), artos->voice_commands.confidence_threshold);
    g_signal_connect(artos->voicecmd_threshold_scale, "value-changed", G_CALLBACK(on_voicecmd_threshold_changed), artos);
    gtk_box_pack_start(GTK_BOX(thresh_box), artos->voicecmd_threshold_scale, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), thresh_box, FALSE, FALSE, 0);

    /* Confidence bar */
    artos->voicecmd_confidence_bar = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(artos->voicecmd_confidence_bar), TRUE);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(artos->voicecmd_confidence_bar), "Confidence");
    gtk_box_pack_start(GTK_BOX(box), artos->voicecmd_confidence_bar, FALSE, FALSE, 0);

    /* Commands list */
    GtkWidget *cmd_label = gtk_label_new("Available Commands:");
    gtk_widget_set_halign(cmd_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), cmd_label, FALSE, FALSE, 0);

    artos->voicecmd_commands_store = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    artos->voicecmd_commands_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(artos->voicecmd_commands_store));

    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes("Phrase", renderer, "text", 0, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(artos->voicecmd_commands_tree), col);
    col = gtk_tree_view_column_new_with_attributes("Category", renderer, "text", 1, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(artos->voicecmd_commands_tree), col);
    col = gtk_tree_view_column_new_with_attributes("Feedback", renderer, "text", 2, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(artos->voicecmd_commands_tree), col);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll), 150);
    gtk_container_add(GTK_CONTAINER(scroll), artos->voicecmd_commands_tree);
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);

    /* Refresh command list */
    artos_voicecmd_refresh_list(artos);

    gtk_container_add(GTK_CONTAINER(frame), box);
    artos->voicecmd_panel = frame;

    return frame;
}

/* ==============================================================================
 * AI Smart Features Implementation
 * ============================================================================== */

void artos_ai_color_suggest_init(phantom_artos_t *artos) {
    if (!artos) return;
    memset(&artos->ai_color_suggest, 0, sizeof(artos_ai_color_suggest_t));
    artos->ai_color_suggest.enabled = 0;
}

void artos_ai_color_suggest_enable(phantom_artos_t *artos, int enable) {
    if (!artos) return;
    artos->ai_color_suggest.enabled = enable;
    if (enable) {
        artos_ai_color_suggest_analyze(artos);
    }
}

void artos_ai_color_suggest_analyze(phantom_artos_t *artos) {
    if (!artos || !artos->document || !artos->ai_color_suggest.enabled) return;

    /* Analyze current canvas colors */
    artos_update_composite(artos->document);
    cairo_surface_t *surface = artos->document->composite;
    if (!surface) return;

    int width = cairo_image_surface_get_width(surface);
    int height = cairo_image_surface_get_height(surface);
    unsigned char *data = cairo_image_surface_get_data(surface);
    int stride = cairo_image_surface_get_stride(surface);

    /* Simple color histogram analysis */
    int color_counts[8][8][8] = {{{0}}};  /* RGB buckets */
    int total_pixels = 0;

    for (int y = 0; y < height; y += 4) {  /* Sample every 4th pixel */
        for (int x = 0; x < width; x += 4) {
            unsigned char *pixel = data + y * stride + x * 4;
            int r = pixel[2] / 32;  /* Reduce to 8 levels */
            int g = pixel[1] / 32;
            int b = pixel[0] / 32;
            color_counts[r][g][b]++;
            total_pixels++;
        }
    }

    /* Find dominant colors */
    artos->ai_color_suggest.dominant_count = 0;

    for (int i = 0; i < 8 && artos->ai_color_suggest.dominant_count < 8; i++) {
        int max_count = 0;
        int max_r = 0, max_g = 0, max_b = 0;

        for (int r = 0; r < 8; r++) {
            for (int g = 0; g < 8; g++) {
                for (int b = 0; b < 8; b++) {
                    if (color_counts[r][g][b] > max_count) {
                        max_count = color_counts[r][g][b];
                        max_r = r; max_g = g; max_b = b;
                    }
                }
            }
        }

        if (max_count > total_pixels / 100) {  /* At least 1% */
            artos_color_t *c = &artos->ai_color_suggest.dominant_colors[artos->ai_color_suggest.dominant_count];
            c->r = (max_r * 32 + 16) / 255.0;
            c->g = (max_g * 32 + 16) / 255.0;
            c->b = (max_b * 32 + 16) / 255.0;
            c->a = 1.0;
            artos->ai_color_suggest.dominant_count++;

            /* Clear this bucket so we find the next dominant */
            color_counts[max_r][max_g][max_b] = 0;
        } else {
            break;
        }
    }

    /* Generate suggestions based on dominant colors */
    artos->ai_color_suggest.suggestion_count = 0;

    for (int i = 0; i < artos->ai_color_suggest.dominant_count && artos->ai_color_suggest.suggestion_count < 12; i++) {
        artos_color_t *base = &artos->ai_color_suggest.dominant_colors[i];
        double h, s, v;
        artos_color_to_hsv(base, &h, &s, &v);

        /* Complementary color */
        artos_color_t *sugg = &artos->ai_color_suggest.suggestions[artos->ai_color_suggest.suggestion_count];
        artos_color_from_hsv(sugg, fmod(h + 180, 360), s, v);
        sugg->a = 1.0;
        strcpy(artos->ai_color_suggest.suggestion_reasons[artos->ai_color_suggest.suggestion_count], "Complementary");
        artos->ai_color_suggest.suggestion_count++;

        /* Analogous colors */
        if (artos->ai_color_suggest.suggestion_count < 12) {
            sugg = &artos->ai_color_suggest.suggestions[artos->ai_color_suggest.suggestion_count];
            artos_color_from_hsv(sugg, fmod(h + 30, 360), s, v);
            sugg->a = 1.0;
            strcpy(artos->ai_color_suggest.suggestion_reasons[artos->ai_color_suggest.suggestion_count], "Analogous");
            artos->ai_color_suggest.suggestion_count++;
        }
    }

    /* Calculate temperature */
    double warm_sum = 0, total = 0;
    for (int i = 0; i < artos->ai_color_suggest.dominant_count; i++) {
        artos_color_t *c = &artos->ai_color_suggest.dominant_colors[i];
        warm_sum += c->r - c->b;  /* More red = warmer */
        total++;
    }
    artos->ai_color_suggest.warm_ratio = (total > 0) ? (warm_sum / total + 1) / 2 : 0.5;

    printf("[AI Color] Analyzed: %d dominant colors, %d suggestions, %.0f%% warm\n",
           artos->ai_color_suggest.dominant_count,
           artos->ai_color_suggest.suggestion_count,
           artos->ai_color_suggest.warm_ratio * 100);
}

void artos_ai_perspective_init(phantom_artos_t *artos) {
    if (!artos) return;
    memset(&artos->ai_perspective, 0, sizeof(artos_ai_perspective_t));
    artos->ai_perspective.guide_opacity = 0.5;
    artos->ai_perspective.guide_color.r = 0.0;
    artos->ai_perspective.guide_color.g = 0.7;
    artos->ai_perspective.guide_color.b = 1.0;
    artos->ai_perspective.guide_color.a = 1.0;
}

void artos_ai_perspective_enable(phantom_artos_t *artos, int enable) {
    if (!artos) return;
    artos->ai_perspective.enabled = enable;
    if (artos->canvas_area) {
        gtk_widget_queue_draw(artos->canvas_area);
    }
}

void artos_ai_perspective_add_vanishing_point(phantom_artos_t *artos, double x, double y) {
    if (!artos || artos->ai_perspective.point_count >= 3) return;

    int idx = artos->ai_perspective.point_count;
    artos->ai_perspective.vanishing_points[idx].x = x;
    artos->ai_perspective.vanishing_points[idx].y = y;
    artos->ai_perspective.vanishing_points[idx].confidence = 1.0;
    artos->ai_perspective.vanishing_points[idx].active = 1;
    artos->ai_perspective.point_count++;
    artos->ai_perspective.detected = 1;

    /* Generate guide lines from this point */
    if (artos->document) {
        int w = artos->document->width;
        int h = artos->document->height;

        for (int i = 0; i < 8 && artos->ai_perspective.guide_count < 32; i++) {
            double angle = (i * M_PI / 4);
            double len = sqrt(w * w + h * h);
            int g = artos->ai_perspective.guide_count;
            artos->ai_perspective.guide_lines[g].x1 = x;
            artos->ai_perspective.guide_lines[g].y1 = y;
            artos->ai_perspective.guide_lines[g].x2 = x + cos(angle) * len;
            artos->ai_perspective.guide_lines[g].y2 = y + sin(angle) * len;
            artos->ai_perspective.guide_lines[g].opacity = 0.3;
            artos->ai_perspective.guide_count++;
        }
    }

    if (artos->canvas_area) {
        gtk_widget_queue_draw(artos->canvas_area);
    }
}

void artos_ai_perspective_draw_guides(phantom_artos_t *artos, cairo_t *cr) {
    if (!artos || !cr || !artos->ai_perspective.enabled || !artos->ai_perspective.show_guides) return;

    cairo_save(cr);

    /* Draw guide lines */
    for (int i = 0; i < artos->ai_perspective.guide_count; i++) {
        cairo_set_source_rgba(cr,
                              artos->ai_perspective.guide_color.r,
                              artos->ai_perspective.guide_color.g,
                              artos->ai_perspective.guide_color.b,
                              artos->ai_perspective.guide_lines[i].opacity * artos->ai_perspective.guide_opacity);
        cairo_set_line_width(cr, 1);
        cairo_move_to(cr, artos->ai_perspective.guide_lines[i].x1,
                          artos->ai_perspective.guide_lines[i].y1);
        cairo_line_to(cr, artos->ai_perspective.guide_lines[i].x2,
                          artos->ai_perspective.guide_lines[i].y2);
        cairo_stroke(cr);
    }

    /* Draw vanishing points */
    for (int i = 0; i < artos->ai_perspective.point_count; i++) {
        if (!artos->ai_perspective.vanishing_points[i].active) continue;

        double x = artos->ai_perspective.vanishing_points[i].x;
        double y = artos->ai_perspective.vanishing_points[i].y;

        cairo_set_source_rgba(cr, 1.0, 0.3, 0.3, 0.8);
        cairo_arc(cr, x, y, 8, 0, 2 * M_PI);
        cairo_fill(cr);

        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.8);
        cairo_arc(cr, x, y, 5, 0, 2 * M_PI);
        cairo_fill(cr);
    }

    /* Draw horizon line if detected */
    if (artos->ai_perspective.horizon_detected && artos->document) {
        cairo_set_source_rgba(cr, 0.3, 1.0, 0.3, 0.5);
        cairo_set_line_width(cr, 2);

        double y = artos->ai_perspective.horizon_y;
        double angle = artos->ai_perspective.horizon_angle * M_PI / 180;
        double w = artos->document->width;

        cairo_move_to(cr, 0, y - tan(angle) * w / 2);
        cairo_line_to(cr, w, y + tan(angle) * w / 2);
        cairo_stroke(cr);
    }

    cairo_restore(cr);
}

void artos_ai_sketch_cleanup_init(phantom_artos_t *artos) {
    if (!artos) return;
    memset(&artos->ai_sketch_cleanup, 0, sizeof(artos_ai_sketch_cleanup_t));
}

void artos_ai_sketch_cleanup_enable(phantom_artos_t *artos, int enable) {
    if (!artos) return;
    artos->ai_sketch_cleanup.enabled = enable;
    if (enable) {
        artos_ai_sketch_cleanup_analyze(artos);
    }
}

void artos_ai_sketch_cleanup_analyze(phantom_artos_t *artos) {
    if (!artos || !artos->document || !artos->ai_sketch_cleanup.enabled) return;

    artos->ai_sketch_cleanup.analyzing = 1;
    artos->ai_sketch_cleanup.issue_count = 0;

    /* Analyze recent strokes for issues */
    artos_stroke_t *stroke = artos->document->undo_stack;
    int analyzed = 0;
    double total_deviation = 0;
    int deviation_count = 0;

    while (stroke && analyzed < 10) {  /* Analyze last 10 strokes */
        if (stroke->point_count >= 3) {
            /* Check for line steadiness */
            for (int i = 1; i < stroke->point_count - 1; i++) {
                double x0 = stroke->points[i-1].x;
                double y0 = stroke->points[i-1].y;
                double x1 = stroke->points[i].x;
                double y1 = stroke->points[i].y;
                double x2 = stroke->points[i+1].x;
                double y2 = stroke->points[i+1].y;

                /* Calculate deviation from straight line */
                double dx = x2 - x0;
                double dy = y2 - y0;
                double len = sqrt(dx * dx + dy * dy);
                if (len > 0) {
                    double deviation = fabs((x1 - x0) * dy - (y1 - y0) * dx) / len;
                    total_deviation += deviation;
                    deviation_count++;
                }
            }
        }
        stroke = stroke->next;
        analyzed++;
    }

    /* Calculate steadiness score */
    if (deviation_count > 0) {
        double avg_deviation = total_deviation / deviation_count;
        artos->ai_sketch_cleanup.line_steadiness = 1.0 / (1.0 + avg_deviation * 0.1);
    } else {
        artos->ai_sketch_cleanup.line_steadiness = 1.0;
    }

    /* Generate recommendations */
    artos->ai_sketch_cleanup.suggest_stabilizer = (artos->ai_sketch_cleanup.line_steadiness < 0.7);
    artos->ai_sketch_cleanup.suggest_strength = (int)((1.0 - artos->ai_sketch_cleanup.line_steadiness) * 10);
    if (artos->ai_sketch_cleanup.suggest_strength > 10) artos->ai_sketch_cleanup.suggest_strength = 10;
    if (artos->ai_sketch_cleanup.suggest_strength < 1) artos->ai_sketch_cleanup.suggest_strength = 1;

    artos->ai_sketch_cleanup.analyzing = 0;

    printf("[AI Sketch] Steadiness: %.1f%%, suggest stabilizer: %s (strength %d)\n",
           artos->ai_sketch_cleanup.line_steadiness * 100,
           artos->ai_sketch_cleanup.suggest_stabilizer ? "yes" : "no",
           artos->ai_sketch_cleanup.suggest_strength);
}

/* AI Smart panel callbacks */
static void on_ai_color_suggest_toggle(GtkToggleButton *button, phantom_artos_t *artos) {
    artos_ai_color_suggest_enable(artos, gtk_toggle_button_get_active(button));
}

static void on_ai_perspective_toggle(GtkToggleButton *button, phantom_artos_t *artos) {
    artos_ai_perspective_enable(artos, gtk_toggle_button_get_active(button));
}

static void on_ai_sketch_toggle(GtkToggleButton *button, phantom_artos_t *artos) {
    artos_ai_sketch_cleanup_enable(artos, gtk_toggle_button_get_active(button));
}

static gboolean on_ai_color_suggest_draw(GtkWidget *widget, cairo_t *cr, phantom_artos_t *artos) {
    if (!artos) return TRUE;

    GtkAllocation alloc;
    gtk_widget_get_allocation(widget, &alloc);

    /* Background */
    cairo_set_source_rgb(cr, 0.2, 0.2, 0.2);
    cairo_paint(cr);

    if (artos->ai_color_suggest.suggestion_count == 0) {
        cairo_set_source_rgb(cr, 0.6, 0.6, 0.6);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 10);
        cairo_move_to(cr, 5, 15);
        cairo_show_text(cr, "Enable to see suggestions");
        return TRUE;
    }

    /* Draw color suggestions as swatches */
    int cols = 6;
    int size = alloc.width / cols;
    for (int i = 0; i < artos->ai_color_suggest.suggestion_count; i++) {
        int x = (i % cols) * size;
        int y = (i / cols) * size;

        artos_color_t *c = &artos->ai_color_suggest.suggestions[i];
        cairo_set_source_rgb(cr, c->r, c->g, c->b);
        cairo_rectangle(cr, x + 2, y + 2, size - 4, size - 4);
        cairo_fill(cr);
    }

    return TRUE;
}

GtkWidget *artos_create_ai_smart_panel(phantom_artos_t *artos) {
    GtkWidget *frame = gtk_frame_new("AI Smart Features");
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(box), 5);

    /* Color suggestion section */
    artos->ai_color_suggest_toggle = gtk_check_button_new_with_label("Color Suggestions");
    g_signal_connect(artos->ai_color_suggest_toggle, "toggled", G_CALLBACK(on_ai_color_suggest_toggle), artos);
    gtk_box_pack_start(GTK_BOX(box), artos->ai_color_suggest_toggle, FALSE, FALSE, 0);

    artos->ai_color_suggest_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(artos->ai_color_suggest_area, -1, 50);
    g_signal_connect(artos->ai_color_suggest_area, "draw", G_CALLBACK(on_ai_color_suggest_draw), artos);
    gtk_box_pack_start(GTK_BOX(box), artos->ai_color_suggest_area, FALSE, FALSE, 0);

    /* Separator */
    gtk_box_pack_start(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 5);

    /* Perspective guides section */
    artos->ai_perspective_toggle = gtk_check_button_new_with_label("Perspective Guides");
    g_signal_connect(artos->ai_perspective_toggle, "toggled", G_CALLBACK(on_ai_perspective_toggle), artos);
    gtk_box_pack_start(GTK_BOX(box), artos->ai_perspective_toggle, FALSE, FALSE, 0);

    GtkWidget *persp_info = gtk_label_new("Click canvas to add vanishing points");
    gtk_widget_set_halign(persp_info, GTK_ALIGN_START);
    PangoAttrList *attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs, pango_attr_scale_new(0.85));
    gtk_label_set_attributes(GTK_LABEL(persp_info), attrs);
    pango_attr_list_unref(attrs);
    gtk_box_pack_start(GTK_BOX(box), persp_info, FALSE, FALSE, 0);

    /* Separator */
    gtk_box_pack_start(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 5);

    /* Sketch cleanup section */
    artos->ai_sketch_toggle = gtk_check_button_new_with_label("Sketch Cleanup Hints");
    g_signal_connect(artos->ai_sketch_toggle, "toggled", G_CALLBACK(on_ai_sketch_toggle), artos);
    gtk_box_pack_start(GTK_BOX(box), artos->ai_sketch_toggle, FALSE, FALSE, 0);

    artos->ai_sketch_issues_label = gtk_label_new("Steadiness: - | Stabilizer: -");
    gtk_widget_set_halign(artos->ai_sketch_issues_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), artos->ai_sketch_issues_label, FALSE, FALSE, 0);

    artos->ai_sketch_apply_btn = gtk_button_new_with_label("Apply Stabilizer Suggestion");
    gtk_widget_set_sensitive(artos->ai_sketch_apply_btn, FALSE);
    gtk_box_pack_start(GTK_BOX(box), artos->ai_sketch_apply_btn, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(frame), box);
    artos->ai_smart_panel = frame;

    return frame;
}
