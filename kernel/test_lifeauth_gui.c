/*
 * LifeAuth GUI Test/Demo
 *
 * Tests the GUI components and exports visual previews
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "phantom_lifeauth_gui.h"

/* Export framebuffer to PPM image */
static void export_ppm(const char *filename, uint32_t *fb, int width, int height) {
    FILE *f = fopen(filename, "wb");
    if (!f) {
        printf("Failed to create %s\n", filename);
        return;
    }

    fprintf(f, "P6\n%d %d\n255\n", width, height);

    for (int i = 0; i < width * height; i++) {
        uint32_t pixel = fb[i];
        uint8_t r = (pixel >> 24) & 0xFF;
        uint8_t g = (pixel >> 16) & 0xFF;
        uint8_t b = (pixel >> 8) & 0xFF;
        fputc(r, f);
        fputc(g, f);
        fputc(b, f);
    }

    fclose(f);
    printf("Exported: %s\n", filename);
}

static void on_enroll(bool success, void *data) {
    (void)data;
    printf("Callback: Enrollment %s\n", success ? "SUCCESS" : "FAILED");
}

static void on_auth(bool success, void *data) {
    (void)data;
    printf("Callback: Authentication %s\n", success ? "SUCCESS" : "FAILED");
}

int main(void) {
    printf("\n=== LifeAuth GUI Test ===\n\n");

    /* Create GUI */
    lifeauth_gui_t *gui = lifeauth_gui_create(100, 100);
    if (!gui) {
        printf("Failed to create GUI\n");
        return 1;
    }
    printf("GUI created: %dx%d\n", gui->fb_width, gui->fb_height);

    lifeauth_gui_set_callbacks(gui, on_enroll, on_auth, NULL);

    /* Test 1: Initial state */
    printf("\n1. Rendering initial state...\n");
    lifeauth_gui_render(gui);
    export_ppm("lifeauth_1_initial.ppm", gui->framebuffer, gui->fb_width, gui->fb_height);

    /* Test 2: Collecting sample */
    printf("2. Testing sample collection...\n");
    lifeauth_gui_start_sample(gui);

    /* Animate for a few frames */
    for (int i = 0; i < 20; i++) {
        lifeauth_gui_update(gui, 50);
    }
    lifeauth_gui_render(gui);
    export_ppm("lifeauth_2_sampling.ppm", gui->framebuffer, gui->fb_width, gui->fb_height);

    /* Test 3: With input focus */
    printf("3. Testing input fields...\n");
    gui->username_input.is_focused = true;
    strcpy(gui->username_input.text, "TESTUSER");
    lifeauth_gui_render(gui);
    export_ppm("lifeauth_3_input.ppm", gui->framebuffer, gui->fb_width, gui->fb_height);

    /* Test 4: Button hover */
    printf("4. Testing button hover...\n");
    gui->username_input.is_focused = false;
    gui->password_input.is_focused = true;
    strcpy(gui->password_input.text, "secretpass123");
    gui->enroll_btn.is_hovered = true;
    lifeauth_gui_render(gui);
    export_ppm("lifeauth_4_hover.ppm", gui->framebuffer, gui->fb_width, gui->fb_height);

    /* Test 5: Enrollment */
    printf("5. Testing enrollment...\n");
    gui->enroll_btn.is_hovered = false;
    lifeauth_gui_start_enroll(gui);

    /* Animate */
    for (int i = 0; i < 30; i++) {
        lifeauth_gui_update(gui, 50);
    }
    lifeauth_gui_render(gui);
    export_ppm("lifeauth_5_enrolled.ppm", gui->framebuffer, gui->fb_width, gui->fb_height);

    /* Test 6: Authentication */
    printf("6. Testing authentication...\n");
    if (gui->credential) {
        lifeauth_gui_start_auth(gui, gui->credential);

        /* Animate */
        for (int i = 0; i < 30; i++) {
            lifeauth_gui_update(gui, 50);
        }
        lifeauth_gui_render(gui);
        export_ppm("lifeauth_6_auth.ppm", gui->framebuffer, gui->fb_width, gui->fb_height);
    }

    /* Test 7: Biomarker animation */
    printf("7. Testing biomarker visualization...\n");

    /* Set some target values */
    for (int i = 0; i < LIFEAUTH_VIS_PROTEINS; i++) {
        gui->proteins[i].target = 0.3f + (float)(i * 7 % 10) / 15.0f;
    }
    for (int i = 0; i < LIFEAUTH_VIS_ANTIBODIES; i++) {
        gui->antibodies[i].target = 0.4f + (float)(i * 11 % 10) / 20.0f;
    }
    for (int i = 0; i < LIFEAUTH_VIS_METABOLITES; i++) {
        gui->metabolites[i].target = 0.25f + (float)(i * 13 % 10) / 18.0f;
    }
    for (int i = 0; i < LIFEAUTH_VIS_ENZYMES; i++) {
        gui->enzymes[i].target = 0.35f + (float)(i * 17 % 10) / 22.0f;
    }

    gui->pulse_gauge.target = 0.95f;
    gui->temp_gauge.target = 0.92f;
    gui->spo2_gauge.target = 0.98f;
    gui->activity_gauge.target = 0.85f;

    gui->similarity_target = 0.92f;
    gui->fp_reveal_progress = 1.0f;

    /* Animate */
    for (int i = 0; i < 40; i++) {
        lifeauth_gui_update(gui, 50);
    }
    lifeauth_gui_render(gui);
    export_ppm("lifeauth_7_visualization.ppm", gui->framebuffer, gui->fb_width, gui->fb_height);

    /* Test 8: Health alert */
    printf("8. Testing health alert...\n");
    gui->state = LIFEAUTH_GUI_HEALTH_ALERT;
    lifeauth_gui_show_health_alert(gui, "Glucose levels outside normal range");
    lifeauth_gui_render(gui);
    export_ppm("lifeauth_8_health_alert.ppm", gui->framebuffer, gui->fb_width, gui->fb_height);

    /* Test 9: Failure state */
    printf("9. Testing failure state...\n");
    gui->health_alert.active = false;
    gui->health_alert.fade = 0;
    gui->state = LIFEAUTH_GUI_FAILURE;
    gui->similarity_target = 0.45f;
    lifeauth_gui_set_status(gui, "Authentication failed - profile mismatch", LIFEAUTH_COLOR_ERROR);

    for (int i = 0; i < 20; i++) {
        lifeauth_gui_update(gui, 50);
    }
    lifeauth_gui_render(gui);
    export_ppm("lifeauth_9_failure.ppm", gui->framebuffer, gui->fb_width, gui->fb_height);

    /* Test 10: Locked state */
    printf("10. Testing locked state...\n");
    gui->state = LIFEAUTH_GUI_LOCKED;
    lifeauth_gui_set_status(gui, "Account locked - too many failed attempts", LIFEAUTH_COLOR_ERROR);
    lifeauth_gui_render(gui);
    export_ppm("lifeauth_10_locked.ppm", gui->framebuffer, gui->fb_width, gui->fb_height);

    /* Cleanup */
    lifeauth_gui_destroy(gui);

    printf("\n=== GUI Test Complete ===\n");
    printf("Generated PPM images can be viewed with any image viewer.\n");
    printf("Convert to PNG: convert lifeauth_*.ppm lifeauth_preview.png\n\n");

    return 0;
}
