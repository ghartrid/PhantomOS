/*
 * MusiKey GUI Test/Demo
 *
 * Tests the GUI components and exports a visual preview
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "phantom_musikey_gui.h"

/* Export framebuffer to PPM image (simple format) */
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
    printf("\n=== MusiKey GUI Test ===\n\n");

    /* Create GUI */
    musikey_gui_t *gui = musikey_gui_create(100, 100);
    if (!gui) {
        printf("Failed to create GUI\n");
        return 1;
    }
    printf("GUI created: %dx%d\n", gui->fb_width, gui->fb_height);

    musikey_gui_set_callbacks(gui, on_enroll, on_auth, NULL);

    /* Test 1: Initial state render */
    printf("\n1. Rendering initial state...\n");
    musikey_gui_render(gui);
    export_ppm("musikey_1_initial.ppm", gui->framebuffer, gui->fb_width, gui->fb_height);

    /* Test 2: With input focus */
    printf("2. Testing input focus...\n");
    gui->username_input.is_focused = true;
    strcpy(gui->username_input.text, "TESTUSER");
    gui->username_input.cursor_pos = 8;
    musikey_gui_render(gui);
    export_ppm("musikey_2_input.ppm", gui->framebuffer, gui->fb_width, gui->fb_height);

    /* Test 3: Button hover */
    printf("3. Testing button hover...\n");
    gui->enroll_btn.is_hovered = true;
    musikey_gui_render(gui);
    export_ppm("musikey_3_hover.ppm", gui->framebuffer, gui->fb_width, gui->fb_height);

    /* Test 4: Piano key press */
    printf("4. Testing piano interaction...\n");
    gui->piano[5].is_pressed = true;
    gui->piano[5].highlight = 1.0f;
    gui->piano[12].is_pressed = true;
    gui->piano[12].highlight = 1.0f;
    gui->visualizer[8].target = 0.9f;
    gui->visualizer[8].height = 0.9f;
    gui->visualizer[20].target = 0.7f;
    gui->visualizer[20].height = 0.7f;
    musikey_gui_render(gui);
    export_ppm("musikey_4_piano.ppm", gui->framebuffer, gui->fb_width, gui->fb_height);

    /* Test 5: Enrollment process */
    printf("5. Testing enrollment...\n");
    gui->piano[5].is_pressed = false;
    gui->piano[12].is_pressed = false;
    strcpy(gui->password_input.text, "mysecretkey123");
    gui->password_input.cursor_pos = 14;
    musikey_gui_start_enroll(gui);
    musikey_gui_render(gui);
    export_ppm("musikey_5_enrolled.ppm", gui->framebuffer, gui->fb_width, gui->fb_height);

    /* Test 6: Authentication success */
    printf("6. Testing authentication...\n");
    if (gui->credential) {
        musikey_gui_start_auth(gui, gui->credential);
        musikey_gui_render(gui);
        export_ppm("musikey_6_auth.ppm", gui->framebuffer, gui->fb_width, gui->fb_height);
    }

    /* Test 7: Playback visualization */
    printf("7. Testing playback visualization...\n");
    if (gui->current_song) {
        musikey_gui_play_preview(gui);

        /* Simulate some playback frames */
        for (int frame = 0; frame < 10; frame++) {
            musikey_gui_update(gui, 100);  /* 100ms per frame */
            musikey_gui_render(gui);
        }
        export_ppm("musikey_7_playback.ppm", gui->framebuffer, gui->fb_width, gui->fb_height);
    }

    /* Test 8: Error state */
    printf("8. Testing error state...\n");
    strcpy(gui->password_input.text, "wrongpassword");
    if (gui->credential) {
        musikey_gui_start_auth(gui, gui->credential);
    }
    musikey_gui_render(gui);
    export_ppm("musikey_8_error.ppm", gui->framebuffer, gui->fb_width, gui->fb_height);

    /* Animation test */
    printf("\n9. Testing animations...\n");
    for (int frame = 0; frame < 20; frame++) {
        musikey_gui_update(gui, 50);
    }
    musikey_gui_render(gui);
    export_ppm("musikey_9_animated.ppm", gui->framebuffer, gui->fb_width, gui->fb_height);

    /* Cleanup */
    musikey_gui_destroy(gui);

    printf("\n=== GUI Test Complete ===\n");
    printf("Generated PPM images can be viewed with any image viewer.\n");
    printf("Convert to PNG: convert musikey_*.ppm musikey_preview.png\n\n");

    return 0;
}
