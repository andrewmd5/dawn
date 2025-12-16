// dawn_history.c - Session history management

#include "dawn_history.h"
#include "cJSON.h"
#include "dawn_date.h"
#include "dawn_file.h"
#include "dawn_fm.h"
#include "dawn_types.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// #region Helpers

static char* history_file_path(void)
{
    static char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/.history", history_dir());
    return path;
}

static void format_date(int64_t timestamp, char* buf, size_t len)
{
    if (timestamp == 0) {
        snprintf(buf, len, "Unknown");
        return;
    }

    DawnTime lt;
    DAWN_BACKEND(app)->localtime_from(&lt, timestamp);
    dawn_format_human_time(&lt, buf, len);
}

// #endregion

// #region Lifecycle

void hist_load(void)
{
    hist_free();

    size_t len;
    char* content = DAWN_BACKEND(app)->read_file(history_file_path(), &len);
    if (!content) {
        // Try migrating from legacy
        hist_migrate_legacy();
        return;
    }

    cJSON* root = cJSON_ParseWithLength(content, len);
    free(content);

    if (!root || !cJSON_IsArray(root)) {
        cJSON_Delete(root);
        hist_migrate_legacy();
        return;
    }

    int32_t count = cJSON_GetArraySize(root);
    if (count == 0) {
        cJSON_Delete(root);
        return;
    }

    app.history = malloc(sizeof(HistoryEntry) * (size_t)count);
    app.hist_count = 0;

    cJSON* item;
    cJSON_ArrayForEach(item, root)
    {
        cJSON* path_j = cJSON_GetObjectItem(item, "path");
        cJSON* title_j = cJSON_GetObjectItem(item, "title");
        cJSON* modified_j = cJSON_GetObjectItem(item, "modified");

        if (!path_j || !cJSON_IsString(path_j))
            continue;

        // Check file still exists
        size_t dummy;
        char* check = DAWN_BACKEND(app)->read_file(path_j->valuestring, &dummy);
        if (!check)
            continue; // File deleted, skip
        free(check);

        HistoryEntry* entry = &app.history[app.hist_count];
        entry->path = strdup(path_j->valuestring);
        entry->title = (title_j && cJSON_IsString(title_j)) ? strdup(title_j->valuestring) : NULL;

        int64_t modified = 0;
        if (modified_j && cJSON_IsNumber(modified_j)) {
            modified = (int64_t)modified_j->valuedouble;
        }

        char date_buf[64];
        format_date(modified, date_buf, sizeof(date_buf));
        entry->date_str = strdup(date_buf);

        app.hist_count++;
    }

    cJSON_Delete(root);
}

void hist_save(void)
{
    cJSON* root = cJSON_CreateArray();

    for (int32_t i = 0; i < app.hist_count; i++) {
        HistoryEntry* entry = &app.history[i];

        cJSON* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "path", entry->path);
        if (entry->title) {
            cJSON_AddStringToObject(item, "title", entry->title);
        }

        // Get file modification time
        int64_t mtime = DAWN_BACKEND(app)->mtime(entry->path);
        cJSON_AddNumberToObject(item, "modified", (double)mtime);

        cJSON_AddItemToArray(root, item);
    }

    char* json = cJSON_Print(root);
    cJSON_Delete(root);

    if (json) {
        DAWN_BACKEND(app)->mkdir_p(history_dir());
        DAWN_BACKEND(app)->write_file(history_file_path(), json, strlen(json));
        free(json);
    }
}

void hist_free(void)
{
    if (app.history) {
        for (int32_t i = 0; i < app.hist_count; i++) {
            free(app.history[i].path);
            free(app.history[i].title);
            free(app.history[i].date_str);
        }
        free(app.history);
        app.history = NULL;
    }
    app.hist_count = 0;
    app.hist_sel = 0;
}

// #endregion

// #region Operations

bool hist_upsert(const char* path, const char* title)
{
    if (!path)
        return false;

    // Look for existing entry
    for (int32_t i = 0; i < app.hist_count; i++) {
        if (strcmp(app.history[i].path, path) == 0) {
            // Update existing
            free(app.history[i].title);
            app.history[i].title = title ? strdup(title) : NULL;

            int64_t mtime = DAWN_BACKEND(app)->mtime(path);
            char date_buf[64];
            format_date(mtime, date_buf, sizeof(date_buf));
            free(app.history[i].date_str);
            app.history[i].date_str = strdup(date_buf);

            // Move to front (most recent)
            if (i > 0) {
                HistoryEntry tmp = app.history[i];
                memmove(&app.history[1], &app.history[0], sizeof(HistoryEntry) * (size_t)i);
                app.history[0] = tmp;
            }

            hist_save();
            return true;
        }
    }

    // Add new entry at front
    app.history = realloc(app.history, sizeof(HistoryEntry) * (size_t)(app.hist_count + 1));
    if (app.hist_count > 0) {
        memmove(&app.history[1], &app.history[0], sizeof(HistoryEntry) * (size_t)app.hist_count);
    }

    app.history[0].path = strdup(path);
    app.history[0].title = title ? strdup(title) : NULL;

    int64_t mt = DAWN_BACKEND(app)->mtime(path);
    char date_buf[64];
    format_date(mt, date_buf, sizeof(date_buf));
    app.history[0].date_str = strdup(date_buf);

    app.hist_count++;
    hist_save();
    return true;
}

bool hist_remove(const char* path)
{
    if (!path)
        return false;

    for (int32_t i = 0; i < app.hist_count; i++) {
        if (strcmp(app.history[i].path, path) == 0) {
            free(app.history[i].path);
            free(app.history[i].title);
            free(app.history[i].date_str);

            // Shift remaining entries
            for (int32_t j = i; j < app.hist_count - 1; j++) {
                app.history[j] = app.history[j + 1];
            }
            app.hist_count--;

            hist_save();
            return true;
        }
    }
    return false;
}

HistEntry* hist_find(const char* path)
{
    if (!path)
        return NULL;

    for (int32_t i = 0; i < app.hist_count; i++) {
        if (strcmp(app.history[i].path, path) == 0) {
            return (HistEntry*)&app.history[i];
        }
    }
    return NULL;
}

// #endregion

// #region Migration

//! Parse frontmatter title from a file (for migration)
static char* parse_frontmatter_title(const char* path)
{
    size_t len;
    char* content = DAWN_BACKEND(app)->read_file(path, &len);
    if (!content)
        return NULL;

    size_t consumed = 0;
    Frontmatter* fm = fm_parse(content, len, &consumed);
    free(content);

    if (!fm)
        return NULL;

    const char* title = fm_get_string(fm, "title");
    char* result = title ? strdup(title) : NULL;
    fm_free(fm);
    return result;
}

void hist_migrate_legacy(void)
{
    char** names;
    int32_t count;
    if (!DAWN_BACKEND(app)->list_dir(history_dir(), &names, &count)) {
        return;
    }

    // Collect valid entries
    typedef struct {
        char* path;
        char* title;
        int64_t mtime;
    } MigEntry;

    MigEntry* entries = malloc(sizeof(MigEntry) * (size_t)count);
    int32_t num_entries = 0;

    for (int32_t i = 0; i < count; i++) {
        size_t nlen = strlen(names[i]);

        // Only .md files, skip .history and .chat.json
        if (nlen < 3 || strcmp(names[i] + nlen - 3, ".md") != 0) {
            free(names[i]);
            continue;
        }

        char full[PATH_MAX];
        snprintf(full, sizeof(full), "%s/%s", history_dir(), names[i]);

        entries[num_entries].path = strdup(full);
        entries[num_entries].title = parse_frontmatter_title(full);
        entries[num_entries].mtime = DAWN_BACKEND(app)->mtime(full);
        num_entries++;

        free(names[i]);
    }
    free(names);

    if (num_entries == 0) {
        free(entries);
        return;
    }

    // Sort by mtime descending (newest first)
    for (int32_t i = 0; i < num_entries - 1; i++) {
        for (int32_t j = i + 1; j < num_entries; j++) {
            if (entries[i].mtime < entries[j].mtime) {
                MigEntry tmp = entries[i];
                entries[i] = entries[j];
                entries[j] = tmp;
            }
        }
    }

    // Build history
    app.history = malloc(sizeof(HistoryEntry) * (size_t)num_entries);
    app.hist_count = num_entries;

    for (int32_t i = 0; i < num_entries; i++) {
        app.history[i].path = entries[i].path;
        app.history[i].title = entries[i].title;

        char date_buf[64];
        format_date(entries[i].mtime, date_buf, sizeof(date_buf));
        app.history[i].date_str = strdup(date_buf);
    }

    free(entries);

    // Save the migrated history
    hist_save();
}

// #endregion
