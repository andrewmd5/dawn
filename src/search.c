// search.c - Web search and AI tools implementation
//! Provides web search via DuckDuckGo and tool callbacks for AI

#include "search.h"
#include "dawn_utils.h"
#include "cJSON.h"
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// #region Curl Helpers

typedef struct {
    char* data;
    size_t len;
} CurlBuffer;

static size_t curl_write_cb(void* contents, size_t size, size_t nmemb, void* userp)
{
    size_t realsize = size * nmemb;
    CurlBuffer* buf = (CurlBuffer*)userp;
    buf->data = realloc(buf->data, buf->len + realsize + 1);
    if (!buf->data)
        return 0;
    memcpy(buf->data + buf->len, contents, realsize);
    buf->len += realsize;
    buf->data[buf->len] = '\0';
    return realsize;
}

// #endregion

// #region Initialization

void search_tool_init(void)
{
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

void search_tool_cleanup(void)
{
    curl_global_cleanup();
}

// #endregion

// #region HTML Parsing

//! Extract text content, stripping HTML tags
static char* extract_text(const char* html, size_t len, size_t* pos)
{
    char* result = malloc(4096);
    size_t rpos = 0;
    bool in_tag = false;

    while (*pos < len && rpos < 4000) {
        char c = html[*pos];
        if (c == '<') {
            in_tag = true;
            // Check for </a> or </div> to stop
            if (*pos + 4 < len && (strncmp(html + *pos, "</a>", 4) == 0 || strncmp(html + *pos, "</div", 5) == 0)) {
                break;
            }
        } else if (c == '>') {
            in_tag = false;
        } else if (!in_tag && c != '\n' && c != '\r') {
            result[rpos++] = c;
        }
        (*pos)++;
    }
    result[rpos] = '\0';
    return result;
}

// #endregion

// #region Search API

char* search_web(const char* query)
{
    CURL* curl = curl_easy_init();
    if (!curl)
        return dawn_strdup("Search failed: could not initialize");

    // First try DuckDuckGo instant answer API for direct answers
    char* encoded = curl_easy_escape(curl, query, 0);
    char url[2048];
    snprintf(url, sizeof(url),
        "https://api.duckduckgo.com/?q=%s&format=json&no_html=1&skip_disambig=1",
        encoded);

    CurlBuffer buf = { .data = malloc(1), .len = 0 };
    buf.data[0] = '\0';

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7)");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        curl_free(encoded);
        curl_easy_cleanup(curl);
        free(buf.data);
        char* err = malloc(256);
        snprintf(err, 256, "Search failed: %s", curl_easy_strerror(res));
        return err;
    }

    // Parse DuckDuckGo JSON response
    cJSON* json = cJSON_Parse(buf.data);
    free(buf.data);

    char* result = malloc(8192);
    result[0] = '\0';
    size_t rpos = 0;
    bool found_answer = false;

    if (json) {
        // Check for instant answers
        cJSON* abstract = cJSON_GetObjectItem(json, "Abstract");
        if (abstract && abstract->valuestring && strlen(abstract->valuestring) > 0) {
            cJSON* heading = cJSON_GetObjectItem(json, "Heading");
            if (heading && heading->valuestring) {
                rpos += snprintf(result + rpos, 8192 - rpos, "**%s**\n\n", heading->valuestring);
            }
            rpos += snprintf(result + rpos, 8192 - rpos, "%s\n\n", abstract->valuestring);
            cJSON* source = cJSON_GetObjectItem(json, "AbstractSource");
            if (source && source->valuestring && strlen(source->valuestring) > 0) {
                rpos += snprintf(result + rpos, 8192 - rpos, "Source: %s\n", source->valuestring);
            }
            found_answer = true;
        }

        cJSON* answer = cJSON_GetObjectItem(json, "Answer");
        if (answer && answer->valuestring && strlen(answer->valuestring) > 0) {
            rpos += snprintf(result + rpos, 8192 - rpos, "**Answer:** %s\n\n", answer->valuestring);
            found_answer = true;
        }

        // Related topics as fallback
        if (!found_answer) {
            cJSON* related = cJSON_GetObjectItem(json, "RelatedTopics");
            if (related && cJSON_IsArray(related) && cJSON_GetArraySize(related) > 0) {
                rpos += snprintf(result + rpos, 8192 - rpos, "**Related information:**\n");
                int32_t count = 0;
                cJSON* topic;
                cJSON_ArrayForEach(topic, related)
                {
                    if (count >= 5)
                        break;
                    cJSON* text = cJSON_GetObjectItem(topic, "Text");
                    if (text && text->valuestring && strlen(text->valuestring) > 0) {
                        rpos += snprintf(result + rpos, 8192 - rpos, "- %s\n", text->valuestring);
                        count++;
                        found_answer = true;
                    }
                }
            }
        }

        cJSON_Delete(json);
    }

    // If no instant answer, try HTML search for snippets
    if (!found_answer) {
        buf.data = malloc(1);
        buf.data[0] = '\0';
        buf.len = 0;

        snprintf(url, sizeof(url), "https://html.duckduckgo.com/html/?q=%s", encoded);

        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);

        res = curl_easy_perform(curl);

        if (res == CURLE_OK && buf.data) {
            // Extract search result snippets from HTML
            const char* snippet_marker = "class=\"result__snippet\"";
            char* ptr = buf.data;
            int32_t count = 0;

            rpos += snprintf(result + rpos, 8192 - rpos, "**Search results for \"%s\":**\n\n", query);

            while ((ptr = strstr(ptr, snippet_marker)) != NULL && count < 5) {
                ptr += strlen(snippet_marker);
                char* start = strchr(ptr, '>');
                if (!start)
                    break;
                start++;

                size_t extract_pos = start - buf.data;
                char* snippet = extract_text(buf.data, buf.len, &extract_pos);

                if (strlen(snippet) > 20) {
                    rpos += snprintf(result + rpos, 8192 - rpos, "- %s\n\n", snippet);
                    count++;
                    found_answer = true;
                }
                free(snippet);
                ptr = buf.data + extract_pos;
            }
        }
        free(buf.data);
    }

    curl_free(encoded);
    curl_easy_cleanup(curl);

    if (!found_answer || rpos == 0) {
        snprintf(result, 8192,
            "I couldn't find specific information about \"%s\". "
            "This might be because it's a very specific topic or the search didn't return useful results.",
            query);
    }

    return result;
}

// #endregion

// #region AI Tool Callbacks

char* search_tool_callback(const char* params_json, void* user_data)
{
    (void)user_data;

    cJSON* params = cJSON_Parse(params_json);
    if (!params)
        return dawn_strdup("{\"error\": \"Invalid parameters\"}");

    cJSON* query = cJSON_GetObjectItem(params, "query");
    if (!query || !query->valuestring) {
        cJSON_Delete(params);
        return dawn_strdup("{\"error\": \"Missing query parameter\"}");
    }

    char* result = search_web(query->valuestring);
    cJSON_Delete(params);

    cJSON* response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "result", result);
    free(result);

    char* json_str = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);
    return json_str;
}

char* time_tool_callback(const char* params_json, void* user_data)
{
    (void)params_json;
    (void)user_data;

    time_t now = time(NULL);
    struct tm* tm = localtime(&now);

    cJSON* response = cJSON_CreateObject();

    char time_str[64];
    strftime(time_str, sizeof(time_str), "%I:%M %p", tm);
    cJSON_AddStringToObject(response, "time", time_str);

    char date_str[64];
    strftime(date_str, sizeof(date_str), "%A, %B %d, %Y", tm);
    cJSON_AddStringToObject(response, "date", date_str);

    char full_str[128];
    strftime(full_str, sizeof(full_str), "%I:%M %p on %A, %B %d, %Y", tm);
    cJSON_AddStringToObject(response, "full", full_str);

    cJSON_AddNumberToObject(response, "timestamp", (double)now);

    char* json_str = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);
    return json_str;
}

#include <dirent.h>
#include <sys/stat.h>

char* sessions_tool_callback(const char* params_json, void* user_data)
{
    const char* history_dir = (const char*)user_data;
    if (!history_dir) {
        return dawn_strdup("{\"error\": \"History directory not configured\"}");
    }

    cJSON* params = cJSON_Parse(params_json);
    const char* action = "list";
    const char* filename = NULL;

    if (params) {
        cJSON* action_obj = cJSON_GetObjectItem(params, "action");
        if (action_obj && action_obj->valuestring) {
            action = action_obj->valuestring;
        }
        cJSON* file_obj = cJSON_GetObjectItem(params, "filename");
        if (file_obj && file_obj->valuestring) {
            filename = file_obj->valuestring;
        }
    }

    cJSON* response = cJSON_CreateObject();

    if (strcmp(action, "list") == 0) {
        // List all sessions
        DIR* dir = opendir(history_dir);
        if (!dir) {
            cJSON_AddStringToObject(response, "error", "Could not open history directory");
        } else {
            cJSON* sessions = cJSON_CreateArray();
            struct dirent* entry;
            while ((entry = readdir(dir)) != NULL) {
                if (entry->d_name[0] == '.')
                    continue;
                size_t len = strlen(entry->d_name);
                if (len > 3 && strcmp(entry->d_name + len - 3, ".md") == 0) {
                    cJSON* session = cJSON_CreateObject();
                    cJSON_AddStringToObject(session, "filename", entry->d_name);

                    char filepath[1024];
                    snprintf(filepath, sizeof(filepath), "%s/%s", history_dir, entry->d_name);
                    struct stat st;
                    if (stat(filepath, &st) == 0) {
                        char date[64];
                        strftime(date, sizeof(date), "%Y-%m-%d %H:%M", localtime(&st.st_mtime));
                        cJSON_AddStringToObject(session, "modified", date);
                        cJSON_AddNumberToObject(session, "size", (double)st.st_size);
                    }
                    cJSON_AddItemToArray(sessions, session);
                }
            }
            closedir(dir);
            cJSON_AddItemToObject(response, "sessions", sessions);
        }
    } else if (strcmp(action, "read") == 0 && filename) {
        // Read a specific session
        char filepath[1024];
        snprintf(filepath, sizeof(filepath), "%s/%s", history_dir, filename);

        // Security: prevent path traversal
        if (strstr(filename, "..") || filename[0] == '/') {
            cJSON_AddStringToObject(response, "error", "Invalid filename");
        } else {
            FILE* f = fopen(filepath, "r");
            if (!f) {
                cJSON_AddStringToObject(response, "error", "Could not open file");
            } else {
                fseek(f, 0, SEEK_END);
                long size = ftell(f);
                fseek(f, 0, SEEK_SET);

                // Limit to 32KB
                if (size > 32768)
                    size = 32768;

                char* content = malloc(size + 1);
                size_t nread = fread(content, 1, size, f);
                content[nread] = '\0';
                fclose(f);

                cJSON_AddStringToObject(response, "filename", filename);
                cJSON_AddStringToObject(response, "content", content);
                free(content);
            }
        }
    } else {
        cJSON_AddStringToObject(response, "error", "Unknown action. Use 'list' or 'read'");
    }

    if (params)
        cJSON_Delete(params);

    char* json_str = cJSON_PrintUnformatted(response);
    cJSON_Delete(response);
    return json_str;
}

// #endregion
