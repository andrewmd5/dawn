// dawn_file.c

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wformat-truncation"
#endif

#include "dawn_file.h"
#include "cJSON.h"
#include "dawn_block.h"
#include "dawn_chat.h"
#include "dawn_date.h"
#include "dawn_fm.h"
#include "dawn_gap.h"
#include "dawn_history.h"
#include "dawn_image.h"
#include "dawn_utils.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// #region History Directory

char* history_dir(void)
{
    static char path[PATH_MAX];
    const char* home = DAWN_BACKEND(app)->home_dir();
    DAWN_ASSERT(home, "home_dir() returned NULL");
#ifdef _WIN32
    snprintf(path, sizeof(path), "%s\\%s", home, HISTORY_DIR_NAME);
#else
    snprintf(path, sizeof(path), "%s/%s", home, HISTORY_DIR_NAME);
#endif
    return path;
}

// #endregion

// #region User Info

//! Get current user's display name for document metadata
static const char* get_username(void)
{
    return DAWN_BACKEND(app)->username();
}

// #endregion

// #region Session Persistence

void save_session(void)
{
    if (gap_len(&app.text) == 0 || !app.session_path)
        return;

    char* txt = gap_to_str(&app.text);
    size_t txt_len = strlen(txt);

    // Ensure we have frontmatter
    Frontmatter* fm = app.frontmatter;
    if (!fm) {
        fm = fm_create();
        app.frontmatter = fm;
    }

    // Set default fields if missing
    if (!fm_has_key(fm, "title")) {
        fm_set_string(fm, "title", "Untitled");
    }
    if (!fm_has_key(fm, "author")) {
        fm_set_string(fm, "author", get_username());
    }

    // Always update date with ISO 8601 format
    DawnTime lt;
    DAWN_BACKEND(app)->localtime(&lt);
    char date_buf[32];
    dawn_format_iso_time(&lt, date_buf, sizeof(date_buf));
    fm_set_string(fm, "date", date_buf);

    // Serialize frontmatter
    size_t fm_len = 0;
    char* fm_str = fm_to_string(fm, &fm_len);

    // Build final content
    size_t total = (fm_str ? fm_len : 0) + 1 + txt_len;
    char* content = malloc(total + 1);

    size_t pos = 0;
    if (fm_str) {
        memcpy(content, fm_str, fm_len);
        pos = fm_len;
        // Add blank line only if text doesn't already start with newline
        if (txt_len == 0 || txt[0] != '\n') {
            content[pos++] = '\n';
        }
        free(fm_str);
    }
    memcpy(content + pos, txt, txt_len);
    content[pos + txt_len] = '\0';

    DAWN_BACKEND(app)->write_file(app.session_path, content, pos + txt_len);

    // Update history
    hist_upsert(app.session_path, fm_get_string(fm, "title"), app.cursor);

    free(content);
    free(txt);

    // Save AI chat to companion .chat.json file
    if (app.chat_count > 0) {
        char chat_path[520];
        get_chat_path(app.session_path, chat_path, sizeof(chat_path));

        cJSON* root = cJSON_CreateArray();
        for (int32_t i = 0; i < app.chat_count; i++) {
            ChatMessage* m = &app.chat_msgs[i];
            cJSON* msg = cJSON_CreateObject();
            cJSON_AddStringToObject(msg, "role", m->is_user ? "user" : "assistant");
            cJSON_AddStringToObject(msg, "content", m->text);
            cJSON_AddItemToArray(root, msg);
        }

        char* json = cJSON_Print(root);
        cJSON_Delete(root);

        if (json) {
            DAWN_BACKEND(app)->write_file(chat_path, json, strlen(json));
            free(json);
        }
    }
}

void load_history(void)
{
    hist_load();
}

void load_chat_history(const char* session_path)
{
    char chat_path[520];
    get_chat_path(session_path, chat_path, sizeof(chat_path));

    size_t size;
    char* json_str = DAWN_BACKEND(app)->read_file(chat_path, &size);
    if (!json_str)
        return;

    cJSON* root = cJSON_Parse(json_str);
    free(json_str);
    if (!root)
        return;

    if (cJSON_IsArray(root)) {
        cJSON* msg;
        cJSON_ArrayForEach(msg, root)
        {
            cJSON* role = cJSON_GetObjectItem(msg, "role");
            cJSON* content = cJSON_GetObjectItem(msg, "content");
            if (cJSON_IsString(role) && cJSON_IsString(content)) {
                bool is_user = strcmp(role->valuestring, "user") == 0;
                chat_add(content->valuestring, is_user);
            }
        }
    }

    cJSON_Delete(root);
}

// #endregion

// #region File Operations

//! Load content into editor, parsing frontmatter
//! @param content buffer containing markdown content (will be modified)
//! @param size size of content buffer
//! @param path optional file path (NULL for stdin)
static void load_content(char* content, size_t size, const char* path)
{
    // Free old frontmatter
    fm_free(app.frontmatter);
    app.frontmatter = NULL;

    // Parse frontmatter
    const char* text_start = content;
    size_t consumed = 0;
    Frontmatter* fm = fm_parse(content, size, &consumed);
    if (fm) {
        app.frontmatter = fm;
        text_start = content + consumed;
    }

    // Initialize gap buffer with content (normalize CRLF -> LF)
    gap_free(&app.text);
    gap_init(&app.text, 4096);
    size_t text_len = strlen(text_start);
    if (text_len > 0) {
        // Need mutable copy for normalize
        char* mutable_text = strdup(text_start);
        text_len = normalize_line_endings(mutable_text, text_len);
        gap_insert_str(&app.text, 0, mutable_text, text_len);
        free(mutable_text);
    }

    // Clear image cache when switching documents
    image_clear_all();

    // Reset editor state
    free(app.session_path);
    app.session_path = path ? strdup(path) : NULL;
    app.cursor = 0;
    app.scroll_y = 0;
    app.selecting = false;
    app.timer_done = false;
    app.timer_on = false;
    app.mode = MODE_WRITING;
    app.ai_open = false;
    app.ai_focused = false;
    app.ai_input_len = 0;
    app.ai_input_cursor = 0;
    app.chat_scroll = 0;
    chat_clear();

    // Load associated chat history (only for files)
    if (path) {
        load_chat_history(path);
    }

#if HAS_LIBAI
    if (app.ai_ready && !app.ai_session) {
        ai_init_session();
    }
#endif

    const char* title = fm_get_string(app.frontmatter, "title");
    DAWN_BACKEND(app)->set_title(title);
}

void load_file_for_editing(const char* path)
{
    size_t size;
    char* content = DAWN_BACKEND(app)->read_file(path, &size);
    if (!content)
        return;

    load_content(content, size, path);
    free(content);
}

void load_buffer_for_editing(const char* content, size_t size)
{
    if (!content || size == 0)
        return;

    // Make a mutable copy
    char* buf = malloc(size + 1);
    if (!buf)
        return;
    memcpy(buf, content, size);
    buf[size] = '\0';

    load_content(buf, size, NULL);
    free(buf);
}

void open_in_finder(const char* path)
{
    DAWN_BACKEND(app)->reveal(path);
}

// #endregion
