/*
 * ══════════════════════════════════════════════════════════════════════════════
 *                         PHANTOM GUI ENTRY POINT
 *                       "To Create, Not To Destroy"
 * ══════════════════════════════════════════════════════════════════════════════
 *
 * Main entry point for the PhantomOS graphical interface.
 * Initializes the kernel, VFS, and GUI components.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>

#include "gui.h"
#include "phantom.h"
#include "vfs.h"
#include "init.h"
#include "governor.h"
#include "phantom_user.h"
#include "phantom_dnauth.h"
#include "phantom_qrnet.h"
#include "../geofs.h"

/* External filesystem types */
extern struct vfs_fs_type procfs_fs_type;
extern struct vfs_fs_type devfs_fs_type;
extern struct vfs_fs_type geofs_vfs_type;
extern void procfs_set_kernel(struct vfs_superblock *sb, struct phantom_kernel *kernel,
                              struct vfs_context *vfs);
extern vfs_error_t geofs_vfs_mount_volume(struct vfs_context *ctx,
                                           geofs_volume_t *volume,
                                           const char *mount_path);

int main(int argc, char *argv[]) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════╗\n");
    printf("║              PHANTOM GUI STARTING                     ║\n");
    printf("║            \"To Create, Not To Destroy\"                ║\n");
    printf("╚═══════════════════════════════════════════════════════╝\n");
    printf("\n");

    /* Initialize GTK */
    gtk_init(&argc, &argv);

    /* Initialize kernel */
    struct phantom_kernel kernel;
    if (phantom_init(&kernel, "phantom.geo") != PHANTOM_OK) {
        fprintf(stderr, "Failed to initialize Phantom kernel\n");
        return 1;
    }

    /* Restore processes from previous run */
    phantom_process_restore_all(&kernel);

    /* Initialize VFS */
    struct vfs_context vfs;
    vfs_init(&vfs);

    /* Register filesystems */
    vfs_register_fs(&vfs, &procfs_fs_type);
    vfs_register_fs(&vfs, &devfs_fs_type);
    vfs_register_fs(&vfs, &geofs_vfs_type);

    /* Mount pseudo-filesystems */
    vfs_mount(&vfs, "procfs", NULL, "/proc", 0);
    vfs_mount(&vfs, "devfs", NULL, "/dev", 0);

    /* Mount GeoFS for persistent storage */
    if (kernel.geofs_volume) {
        geofs_vfs_mount_volume(&vfs, kernel.geofs_volume, "/geo");
        printf("  [kernel] Mounted GeoFS at /geo for persistent storage\n");
    }

    /* Set kernel reference for procfs */
    struct vfs_mount *mount = vfs.mounts;
    while (mount) {
        if (strcmp(mount->mount_path, "/proc") == 0 && mount->sb) {
            procfs_set_kernel(mount->sb, &kernel, &vfs);
        }
        mount = mount->next;
    }

    /* Create directories */
    vfs_mkdir(&vfs, 1, "/home", 0755);
    vfs_mkdir(&vfs, 1, "/tmp", 0755);
    vfs_mkdir(&vfs, 1, "/var", 0755);

    if (kernel.geofs_volume) {
        vfs_mkdir(&vfs, 1, "/geo/home", 0755);
        vfs_mkdir(&vfs, 1, "/geo/data", 0755);
        vfs_mkdir(&vfs, 1, "/geo/var", 0755);
        vfs_mkdir(&vfs, 1, "/geo/var/log", 0755);
        vfs_mkdir(&vfs, 1, "/geo/var/log/governor", 0755);
    }

    /* Initialize Governor */
    phantom_governor_t gov;
    governor_init(&gov, &kernel);
    kernel.governor = &gov;

    /* Initialize DNAuth System */
    dnauth_system_t *dnauth = dnauth_init("/tmp/dnauth");
    if (dnauth) {
        dnauth_evolution_init(dnauth);
        dnauth_set_governor(dnauth, &gov);
        kernel.dnauth = dnauth;
        printf("  [kernel] DNAuth system initialized with Governor integration\n");
    } else {
        printf("  [kernel] Warning: DNAuth initialization failed\n");
        kernel.dnauth = NULL;
    }

    /* Initialize QRNet System */
    qrnet_system_t *qrnet = qrnet_init("/tmp/qrnet");
    if (qrnet) {
        qrnet_set_governor(qrnet, &gov);
        if (dnauth) {
            qrnet_set_dnauth(qrnet, dnauth);
        }
        /* Create local node for QR code creation */
        if (qrnet_create_local_node(qrnet, "phantom_local") == QRNET_OK) {
            printf("  [kernel] QRNet local node created\n");
        }
        /* Sync governor state */
        qrnet_sync_governor_state(qrnet);
        kernel.qrnet = qrnet;
        printf("  [kernel] QRNet system initialized with Governor and DNAuth integration\n");
    } else {
        printf("  [kernel] Warning: QRNet initialization failed\n");
        kernel.qrnet = NULL;
    }

    /* Initialize User System */
    phantom_user_system_t user_sys;
    phantom_user_system_init(&user_sys, &kernel);

    /* Initialize Init System */
    phantom_init_t init;
    init_create(&init, &kernel, &vfs);
    kernel.init = &init;
    init_start(&init);

    /* Create GUI process */
    const char *gui_code = "int main() { phantom_gui_run(); }";
    phantom_pid_t gui_pid;
    phantom_process_create(&kernel, gui_code, strlen(gui_code), "phantom-gui", &gui_pid);

    /* Initialize and run GUI */
    phantom_gui_t gui;
    if (phantom_gui_init(&gui, &kernel, &vfs) != 0) {
        fprintf(stderr, "Failed to initialize GUI\n");
        init_shutdown(&init);
        governor_shutdown(&gov);
        vfs_shutdown(&vfs);
        phantom_shutdown(&kernel);
        return 1;
    }

    /* Set user system - no login required */
    phantom_gui_set_user_system(&gui, &user_sys);

    printf("  [gui] PhantomOS GUI initialized\n");
    printf("  [gui] Starting main interface...\n\n");

    /* Run GTK main loop */
    phantom_gui_run(&gui);

    /* Cleanup */
    printf("\n  [gui] Shutting down...\n");

    phantom_gui_shutdown(&gui);
    init_shutdown(&init);
    kernel.init = NULL;
    phantom_user_system_shutdown(&user_sys);
    if (kernel.qrnet) {
        qrnet_cleanup((qrnet_system_t *)kernel.qrnet);
        kernel.qrnet = NULL;
    }
    if (kernel.dnauth) {
        dnauth_cleanup((dnauth_system_t *)kernel.dnauth);
        kernel.dnauth = NULL;
    }
    governor_shutdown(&gov);
    kernel.governor = NULL;
    vfs_shutdown(&vfs);
    phantom_shutdown(&kernel);

    return 0;
}
