// dawn_history.c - Session history management

#include "dawn_history.h"
#include "cJSON.h"
#include "dawn_crdt.h"
#include "dawn_date.h"
#include "dawn_file.h"
#include "dawn_fm.h"
#include "dawn_types.h"
#include "dawn_utils.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static CrdtState* hist_state = NULL;

// #region Helpers

static char* sessions_file_path(void)
{
    static char path[PATH_MAX];
#ifdef _WIN32
    snprintf(path, sizeof(path), "%s\\.sessions", history_dir());
#else
    snprintf(path, sizeof(path), "%s/.sessions", history_dir());
#endif
    return path;
}

static char* legacy_history_path(void)
{
    static char path[PATH_MAX];
#ifdef _WIN32
    snprintf(path, sizeof(path), "%s\\.history", history_dir());
#else
    snprintf(path, sizeof(path), "%s/.history", history_dir());
#endif
    return path;
}

static char* normalize_path(const char* path)
{
    if (!path)
        return NULL;
    char* result = dawn_strdup(path);
#ifdef _WIN32
    for (char* p = result; *p; p++) {
        if (*p == '/')
            *p = '\\';
    }
#else
    for (char* p = result; *p; p++) {
        if (*p == '\\')
            *p = '/';
    }
#endif
    return result;
}

static void format_date(int64_t timestamp, char* buf, size_t len)
{
    if (timestamp == 0) {
        snprintf(buf, len, "Unknown");
        return;
    }

    DawnTime lt;
    DAWN_BACKEND(app)->localtime_from(&lt, timestamp / 1000);
    dawn_format_human_time(&lt, buf, len);
}

static void rebuild_history_array(void)
{
    hist_free();

    if (!hist_state)
        return;

    int32_t count;
    CrdtEntry** live = crdt_get_live(hist_state, &count);
    if (!live || count == 0) {
        free(live);
        return;
    }

    app.history = malloc(sizeof(HistoryEntry) * (size_t)count);
    app.hist_count = 0;

    for (int32_t i = 0; i < count; i++) {
        CrdtEntry* e = live[i];

        if (!DAWN_BACKEND(app)->file_exists(e->key))
            continue;

        HistoryEntry* entry = &app.history[app.hist_count];
        entry->path = dawn_strdup(e->key);
        entry->title = e->value ? dawn_strdup(e->value) : NULL;

        char date_buf[64];
        format_date(e->timestamp, date_buf, sizeof(date_buf));
        entry->date_str = dawn_strdup(date_buf);

        int64_t cursor_val = 0;
        if (crdt_meta_get_int(e, "cursor", &cursor_val))
            entry->cursor = (size_t)cursor_val;
        else
            entry->cursor = 0;

        app.hist_count++;
    }

    free(live);
}

static void normalize_crdt_keys(CrdtState* state)
{
    if (!state)
        return;
    for (int32_t i = 0; i < state->entry_count; i++) {
        char* norm = normalize_path(state->entries[i].key);
        free(state->entries[i].key);
        state->entries[i].key = norm;
    }
    for (int32_t i = 0; i < state->tombstone_count; i++) {
        char* norm = normalize_path(state->tombstones[i].key);
        free(state->tombstones[i].key);
        state->tombstones[i].key = norm;
    }
}

static CrdtState* load_disk_state(void)
{
    size_t len;
    char* content = DAWN_BACKEND(app)->read_file(sessions_file_path(), &len);
    if (!content)
        return NULL;

    CrdtState* state = crdt_parse(content, len);
    free(content);
    normalize_crdt_keys(state);
    return state;
}

static CrdtState* migrate_v1_to_crdt(const char* json, size_t len)
{
    cJSON* root = cJSON_ParseWithLength(json, len);
    if (!root || !cJSON_IsArray(root)) {
        cJSON_Delete(root);
        return NULL;
    }

    CrdtState* state = crdt_create();

    cJSON* item;
    cJSON_ArrayForEach(item, root)
    {
        cJSON* path_j = cJSON_GetObjectItem(item, "path");
        cJSON* title_j = cJSON_GetObjectItem(item, "title");
        cJSON* modified_j = cJSON_GetObjectItem(item, "modified");

        if (!path_j || !cJSON_IsString(path_j))
            continue;

        char* norm_path = normalize_path(path_j->valuestring);

        if (!DAWN_BACKEND(app)->file_exists(norm_path)) {
            free(norm_path);
            continue;
        }

        const char* title = (title_j && cJSON_IsString(title_j)) ? title_j->valuestring : NULL;

        crdt_upsert(state, norm_path, title);

        if (modified_j && cJSON_IsNumber(modified_j)) {
            CrdtEntry* e = crdt_find(state, norm_path);
            if (e)
                e->timestamp = (int64_t)(modified_j->valuedouble * 1000);
        }
        free(norm_path);
    }

    cJSON_Delete(root);
    return state;
}

// #endregion

// #region Lifecycle

void hist_load(void)
{
    if (hist_state) {
        crdt_free(hist_state);
        hist_state = NULL;
    }
    hist_free();

    if (DAWN_BACKEND(app)->file_exists(legacy_history_path())) {
        size_t len;
        char* content = DAWN_BACKEND(app)->read_file(legacy_history_path(), &len);
        if (content) {
            CrdtState* migrated = migrate_v1_to_crdt(content, len);
            free(content);
            if (migrated) {
                char* json = crdt_serialize(migrated);
                if (json) {
                    DAWN_BACKEND(app)->mkdir_p(history_dir());
                    DAWN_BACKEND(app)->write_file(sessions_file_path(), json, strlen(json));
                    free(json);
                }
                crdt_free(migrated);
            }
            remove(legacy_history_path());
        }
    }

    size_t len;
    char* content = DAWN_BACKEND(app)->read_file(sessions_file_path(), &len);
    if (!content)
        return;

    hist_state = crdt_parse(content, len);
    normalize_crdt_keys(hist_state);
    free(content);

    if (hist_state)
        rebuild_history_array();
}

void hist_save(void)
{
    if (!hist_state)
        hist_state = crdt_create();

    CrdtState* disk_state = load_disk_state();

    if (disk_state) {
        CrdtState* merged = crdt_merge(hist_state, disk_state);
        crdt_free(disk_state);
        crdt_free(hist_state);
        hist_state = merged;
    }

    char* json = crdt_serialize(hist_state);
    if (json) {
        DAWN_BACKEND(app)->mkdir_p(history_dir());
        DAWN_BACKEND(app)->write_file(sessions_file_path(), json, strlen(json));
        free(json);
    }

    rebuild_history_array();
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

void hist_shutdown(void)
{
    hist_free();
    hist_free_deleted();
    app.hist_show_deleted = false;
    app.hist_confirm = false;
    if (hist_state) {
        crdt_free(hist_state);
        hist_state = NULL;
    }
}

// #endregion

// #region Operations

bool hist_upsert(const char* path, const char* title, size_t cursor)
{
    if (!path)
        return false;

    if (!hist_state)
        hist_load();
    if (!hist_state)
        hist_state = crdt_create();

    char* norm_path = normalize_path(path);
    crdt_upsert(hist_state, norm_path, title);

    CrdtEntry* entry = crdt_find(hist_state, norm_path);
    if (entry)
        crdt_meta_set_int(entry, "cursor", (int64_t)cursor);

    free(norm_path);

    hist_save();
    return true;
}

bool hist_remove(const char* path)
{
    if (!path || !hist_state)
        return false;

    char* norm_path = normalize_path(path);
    CrdtEntry* entry = crdt_find(hist_state, norm_path);
    if (!entry) {
        free(norm_path);
        return false;
    }

    crdt_remove(hist_state, norm_path);
    free(norm_path);

    hist_save();
    return true;
}

HistEntry* hist_find(const char* path)
{
    if (!path)
        return NULL;

    char* norm_path = normalize_path(path);
    for (int32_t i = 0; i < app.hist_count; i++) {
        if (strcmp(app.history[i].path, norm_path) == 0) {
            free(norm_path);
            return (HistEntry*)&app.history[i];
        }
    }
    free(norm_path);
    return NULL;
}

// #endregion

// #region Restore

//! Find a CRDT entry even if tombstoned (NULL if never seen)
static CrdtEntry* find_entry_any(const char* norm_path)
{
    if (!hist_state || !norm_path)
        return NULL;
    for (int32_t i = 0; i < hist_state->entry_count; i++) {
        if (strcmp(hist_state->entries[i].key, norm_path) == 0)
            return &hist_state->entries[i];
    }
    return NULL;
}

//! Read the title from a session .md file's frontmatter
//! @return Allocated title string or NULL (caller must free)
static char* read_title_from_file(const char* path)
{
    size_t len = 0;
    char* content = DAWN_BACKEND(app)->read_file(path, &len);
    if (!content)
        return NULL;

    char* title = NULL;
    size_t consumed = 0;
    Frontmatter* fm = fm_parse(content, len, &consumed);
    if (fm) {
        const char* t = fm_get_string(fm, "title");
        if (t)
            title = dawn_strdup(t);
        fm_free(fm);
    }
    free(content);
    return title;
}

static bool deleted_contains(const char* norm_path)
{
    for (int32_t i = 0; i < app.hist_deleted_count; i++) {
        if (strcmp(app.hist_deleted[i].path, norm_path) == 0)
            return true;
    }
    return false;
}

static void push_deleted(const char* path, const char* title, int64_t timestamp_ms, size_t cursor)
{
    HistoryEntry* grown
        = realloc(app.hist_deleted, sizeof(HistoryEntry) * (size_t)(app.hist_deleted_count + 1));
    if (!grown)
        return;
    app.hist_deleted = grown;

    HistoryEntry* entry = &app.hist_deleted[app.hist_deleted_count];
    entry->path = dawn_strdup(path);
    entry->title = title ? dawn_strdup(title) : NULL;

    char date_buf[64];
    format_date(timestamp_ms, date_buf, sizeof(date_buf));
    entry->date_str = dawn_strdup(date_buf);
    entry->cursor = cursor;

    app.hist_deleted_count++;
}

bool hist_restore(const char* path)
{
    if (!path)
        return false;

    char* norm_path = normalize_path(path);
    if (!DAWN_BACKEND(app)->file_exists(norm_path)) {
        free(norm_path);
        return false;
    }

    if (!hist_state)
        hist_load();
    if (!hist_state)
        hist_state = crdt_create();

    CrdtEntry* stored = find_entry_any(norm_path);
    char* title = NULL;
    size_t cursor = 0;
    bool title_owned = false;

    if (stored) {
        if (stored->value)
            title = stored->value;
        int64_t cursor_val = 0;
        if (stored->meta && crdt_meta_get_int(stored, "cursor", &cursor_val) && cursor_val > 0)
            cursor = (size_t)cursor_val;
    }
    if (!title) {
        title = read_title_from_file(norm_path);
        title_owned = (title != NULL);
    }

    crdt_upsert(hist_state, norm_path, title);

    CrdtEntry* revived = crdt_find(hist_state, norm_path);
    if (revived && cursor > 0)
        crdt_meta_set_int(revived, "cursor", (int64_t)cursor);

    if (title_owned)
        free(title);
    free(norm_path);

    hist_save();
    hist_refresh_deleted();
    return true;
}

void hist_refresh_deleted(void)
{
    hist_free_deleted();

    if (!hist_state)
        hist_load();

    // Tombstoned entries whose .md file still exists
    if (hist_state) {
        for (int32_t i = 0; i < hist_state->tombstone_count; i++) {
            const char* key = hist_state->tombstones[i].key;
            if (!key)
                continue;
            if (crdt_find(hist_state, key))
                continue; // resurrected, not deleted
            if (!DAWN_BACKEND(app)->file_exists(key))
                continue;

            CrdtEntry* stored = find_entry_any(key);
            const char* title = stored && stored->value ? stored->value : NULL;
            char* file_title = NULL;
            if (!title) {
                file_title = read_title_from_file(key);
                title = file_title;
            }

            int64_t ts = stored ? stored->timestamp : 0;
            size_t cursor = 0;
            if (stored) {
                int64_t cursor_val = 0;
                if (stored->meta && crdt_meta_get_int(stored, "cursor", &cursor_val)
                    && cursor_val > 0)
                    cursor = (size_t)cursor_val;
            }
            if (ts == 0) {
                int64_t mtime = DAWN_BACKEND(app)->mtime(key);
                if (mtime > 0)
                    ts = mtime * 1000;
            }

            push_deleted(key, title, ts, cursor);
            free(file_title);
        }
    }

    // Orphan .md files in the history dir (never tracked or entry pruned)
    char** names = NULL;
    int32_t name_count = 0;
    if (DAWN_BACKEND(app)->list_dir(history_dir(), &names, &name_count)) {
        for (int32_t i = 0; i < name_count; i++) {
            const char* name = names[i];
            if (!name)
                continue;
            size_t nlen = strlen(name);
            if (nlen <= 3 || strcmp(name + nlen - 3, ".md") != 0)
                continue;

            char full[PATH_MAX];
            snprintf(full, sizeof(full), "%s/%s", history_dir(), name);
            char* norm = normalize_path(full);

            if (hist_find(norm) || deleted_contains(norm)) {
                free(norm);
                continue;
            }
            if (!DAWN_BACKEND(app)->file_exists(norm)) {
                free(norm);
                continue;
            }

            char* title = read_title_from_file(norm);
            int64_t mtime = DAWN_BACKEND(app)->mtime(norm);
            push_deleted(norm, title, mtime > 0 ? mtime * 1000 : 0, 0);
            free(title);
            free(norm);
        }
        for (int32_t i = 0; i < name_count; i++)
            free(names[i]);
        free(names);
    }

    if (app.hist_deleted_sel >= app.hist_deleted_count)
        app.hist_deleted_sel = app.hist_deleted_count > 0 ? app.hist_deleted_count - 1 : 0;
    if (app.hist_deleted_sel < 0)
        app.hist_deleted_sel = 0;
}

void hist_free_deleted(void)
{
    if (app.hist_deleted) {
        for (int32_t i = 0; i < app.hist_deleted_count; i++) {
            free(app.hist_deleted[i].path);
            free(app.hist_deleted[i].title);
            free(app.hist_deleted[i].date_str);
        }
        free(app.hist_deleted);
        app.hist_deleted = NULL;
    }
    app.hist_deleted_count = 0;
    app.hist_deleted_sel = 0;
}

// #endregion
