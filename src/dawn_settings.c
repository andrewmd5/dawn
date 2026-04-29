// dawn_settings.c

#include "dawn_settings.h"
#include "cJSON.h"
#include "dawn_file.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// #region Helpers

#ifdef _WIN32
#define DAWN_SEP "\\"
#else
#define DAWN_SEP "/"
#endif

static char* settings_path(void)
{
    static char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s" DAWN_SEP "settings.json", config_dir());
    return path;
}

static void apply_preset_for_minutes(int32_t mins)
{
    for (size_t i = 0; i < NUM_PRESETS; i++) {
        if (TIMER_PRESETS[i] == mins) {
            app.preset_idx = (int32_t)i;
            return;
        }
    }
}

// #endregion

// #region Lifecycle

void settings_load(void)
{
    size_t len;
    char* json = DAWN_BACKEND(app)->read_file(settings_path(), &len);
    if (!json)
        return;

    cJSON* root = cJSON_ParseWithLength(json, len);
    free(json);
    if (!root)
        return;

    cJSON* theme_j = cJSON_GetObjectItem(root, "theme");
    if (cJSON_IsString(theme_j)) {
        if (strcmp(theme_j->valuestring, "light") == 0)
            app.theme = THEME_LIGHT;
        else if (strcmp(theme_j->valuestring, "dark") == 0)
            app.theme = THEME_DARK;
    }

    cJSON* timer_j = cJSON_GetObjectItem(root, "timer_mins");
    if (cJSON_IsNumber(timer_j)) {
        int32_t mins = (int32_t)timer_j->valuedouble;
        if (mins < 0)
            mins = 0;
        app.timer_mins = mins;
        apply_preset_for_minutes(mins);
    }

    cJSON_Delete(root);
}

void settings_save(void)
{
    cJSON* root = cJSON_CreateObject();
    if (!root)
        return;

    cJSON_AddStringToObject(root, "theme", app.theme == THEME_DARK ? "dark" : "light");
    cJSON_AddNumberToObject(root, "timer_mins", (double)app.timer_mins);

    char* json = cJSON_Print(root);
    cJSON_Delete(root);
    if (!json)
        return;

    DAWN_BACKEND(app)->mkdir_p(config_dir());
    DAWN_BACKEND(app)->write_file(settings_path(), json, strlen(json));
    free(json);
}

// #endregion
