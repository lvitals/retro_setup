#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include "config.h"
#include "platform_data.h"
#include "audio.h"
#include "ui.h"
#include "tasks.h"

static void print_cli_help(const char* prog_name) {
    printf("Retro Setup - RetroArch Automation & GUI (Standalone Native C Version)\n\n");
    printf("Usage: %s [command | options]\n\n", prog_name);
    printf("Interactive GUI:\n");
    printf("  (Run without arguments or with --gui to open the interactive Retro Console GUI)\n\n");
    printf("CLI Commands:\n");
    printf("  --prepare     Prepare RetroArch directories, databases & configs\n");
    printf("  --select      Open interactive platform selector\n");
    printf("  --install     Install cores, BIOS & ROMs for selected platforms\n");
    printf("  --uninstall   Uninstall selected platforms\n");
    printf("  --thumbnails  Download thumbnails for saved platforms\n");
    printf("  --implode     Reset local RetroArch configuration\n");
    printf("  --status      Show platforms and configuration files\n");
    printf("  --diagnostic  Audit installations and test configured URLs\n");
    printf("  --steam       Force Steam RetroArch mode\n");
    printf("  --gui         Force launch Graphical User Interface\n");
    printf("  --help, -h    Show this help message\n\n");
}

int main(int argc, char* argv[]) {
    /* libarchive uses the process locale when converting archive entry names. */
    if (!setlocale(LC_ALL, "")) setlocale(LC_ALL, "C.UTF-8");
    init_config();

    bool force_gui = false;
    bool force_cli = false;
    TaskType cli_task = TASK_NONE;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--steam") == 0) {
            set_setup_mode(MODE_STEAM);
            load_selected_platforms_config();
        } else if (strcmp(argv[i], "--gui") == 0) {
            force_gui = true;
        } else if (strcmp(argv[i], "--prepare") == 0) {
            cli_task = TASK_PREPARE;
            force_cli = true;
        } else if (strcmp(argv[i], "--install") == 0) {
            cli_task = TASK_INSTALL;
            force_cli = true;
        } else if (strcmp(argv[i], "--uninstall") == 0) {
            cli_task = TASK_UNINSTALL;
            force_cli = true;
        } else if (strcmp(argv[i], "--thumbnails") == 0) {
            cli_task = TASK_THUMBNAILS;
            force_cli = true;
        } else if (strcmp(argv[i], "--implode") == 0) {
            cli_task = TASK_IMPLODE;
            force_cli = true;
        } else if (strcmp(argv[i], "--status") == 0) {
            cli_task = TASK_STATUS;
            force_cli = true;
        } else if (strcmp(argv[i], "--diagnostic") == 0) {
            cli_task = TASK_INSTALLATION_DIAGNOSTIC;
            force_cli = true;
        } else if (strcmp(argv[i], "--select") == 0) {
            force_gui = true;
        } else if (strcmp(argv[i], "--test-font") == 0) {
            printf("=== RETRO SETUP 8x8 BITMAP FONT DIAGNOSTIC ===\n\n");
            const char* test_strings[] = {
                "ABCDEFGHIJKLMNOPQRSTUVWXYZ",
                "abcdefghijklmnopqrstuvwxyz",
                "0123456789",
                ".,:;!?+-*/()[]<>_",
                "MMMM NNNN WWWW",
                "RETRO SETUP",
                "RETROARCH",
                "STANDALONE",
                "DOWNLOAD THUMBNAILS",
                "SYSTEM STATUS",
                NULL
            };
            for (int s = 0; test_strings[s]; s++) {
                printf("Test String: \"%s\"\n", test_strings[s]);
            }
            printf("\nChecking 'N' (ASCII 78) bitmap rows:\n");
            printf("N bitmap rows: 0x63, 0x73, 0x7B, 0x6F, 0x67, 0x63, 0x63, 0x00\n");
            printf("Visual render of N:\n");
            unsigned char n_rows[8] = {0x63, 0x73, 0x7B, 0x6F, 0x67, 0x63, 0x63, 0x00};
            for (int r = 0; r < 8; r++) {
                for (int c = 0; c < 8; c++) {
                    printf("%c", (n_rows[r] & (1 << (7 - c))) ? '#' : ' ');
                }
                printf("\n");
            }
            return 0;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_cli_help(argv[0]);
            return 0;
        }
    }

    // Determine whether to launch GUI or CLI
    if (!force_cli || force_gui) {
        printf("Launching Retro Setup GUI (Native C / SDL2)...\n");
        if (ui_init("Retro Setup - RetroArch Console Manager", 1200, 800)) {
            ui_run_main_loop();
            ui_cleanup();
            return 0;
        } else {
            printf("WARNING: Could not initialize SDL GUI. Falling back to CLI mode.\n");
        }
    }

    // CLI mode execution via native C tasks engine
    if (cli_task != TASK_NONE) {
        printf("Executing Native C Task: %s...\n", task_get_title(cli_task));
        tasks_init();
        int res = task_run_sync(cli_task);
        tasks_cleanup();
        return res;
    }

    print_cli_help(argv[0]);
    return 0;
}
