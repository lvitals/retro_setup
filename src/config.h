#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#include <stddef.h>

#define MAX_PATH_LEN 2048

typedef enum {
    MODE_STANDALONE = 0,
    MODE_STEAM
} RetroSetupMode;

typedef struct {
    RetroSetupMode mode;
    char ra_dir[MAX_PATH_LEN];
    char config_dir[MAX_PATH_LEN];
    char config_file[MAX_PATH_LEN];
    char repo_dir[MAX_PATH_LEN];
    char rom_dir[MAX_PATH_LEN];
    char url_config_file[MAX_PATH_LEN];
    char distro_name[256];
    char distro_id[64];
    bool audio_enabled;
    bool crt_scanlines;
} AppConfig;

extern AppConfig g_config;

void init_config(void);
void detect_environment(void);
void get_home_dir(char* out, size_t size);
bool load_selected_platforms_config(void);
bool save_selected_platforms_config(void);
void set_setup_mode(RetroSetupMode mode);
int get_selected_count(void);

#endif // CONFIG_H
