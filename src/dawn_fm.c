// dawn_fm.c - YAML frontmatter parsing and serialization

#include "dawn_fm.h"
#include <cyaml.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// #region Types

// String cache entry for scalar values
typedef struct StringCacheEntry {
    cyaml_node_t* node;
    char* str;
    struct StringCacheEntry* next;
} StringCacheEntry;

struct Frontmatter {
    cyaml_doc_t* doc;
    char* backing_buf; // Original YAML string (cyaml keeps references for parsed docs)
    StringCacheEntry* string_cache; // Cache of scalar strings
};

// #endregion

// #region String Cache Helpers

static void cache_free(StringCacheEntry* cache)
{
    while (cache) {
        StringCacheEntry* next = cache->next;
        free(cache->str);
        free(cache);
        cache = next;
    }
}

static const char* cache_get(Frontmatter* fm, cyaml_node_t* node)
{
    for (StringCacheEntry* e = fm->string_cache; e; e = e->next) {
        if (e->node == node)
            return e->str;
    }
    return NULL;
}

static const char* cache_put(Frontmatter* fm, cyaml_node_t* node, char* str)
{
    StringCacheEntry* e = malloc(sizeof(StringCacheEntry));
    if (!e) {
        free(str);
        return NULL;
    }
    e->node = node;
    e->str = str;
    e->next = fm->string_cache;
    fm->string_cache = e;
    return str;
}

// Get or create cached string for a scalar node
static const char* get_cached_scalar(Frontmatter* fm, cyaml_node_t* node)
{
    if (!node || !cyaml_is_scalar(node))
        return NULL;

    const char* cached = cache_get(fm, node);
    if (cached)
        return cached;

    char* str = cyaml_scalar_str(fm->doc, node);
    if (!str)
        return NULL;

    return cache_put(fm, node, str);
}

// #endregion

// #region Lifecycle

Frontmatter* fm_parse(const char* content, size_t len, size_t* consumed)
{
    if (!content || len < 4)
        return NULL;
    if (consumed)
        *consumed = 0;

    // Check for opening delimiter
    if (strncmp(content, "---", 3) != 0)
        return NULL;
    if (content[3] != '\n' && content[3] != '\r')
        return NULL;

    // Find closing delimiter
    const char* start = content + 4;
    const char* end = NULL;

    // Search for \n--- or \r\n---
    for (const char* p = start; p < content + len - 3; p++) {
        if (*p == '\n' && strncmp(p + 1, "---", 3) == 0) {
            // Check that --- is followed by newline or EOF
            const char* after = p + 4;
            if (after >= content + len || *after == '\n' || *after == '\r') {
                end = p;
                break;
            }
        }
    }

    if (!end)
        return NULL;

    // Extract YAML content between delimiters
    size_t yaml_len = (size_t)(end - start);
    if (yaml_len == 0) {
        // Empty frontmatter
        if (consumed) {
            *consumed = (size_t)((end + 4) - content);
            if (*consumed < len && content[*consumed] == '\n')
                (*consumed)++;
        }
        return fm_create();
    }

    // Copy the string since cyaml borrows the source for parsed documents
    char* yaml_copy = malloc(yaml_len + 1);
    if (!yaml_copy)
        return NULL;
    memcpy(yaml_copy, start, yaml_len);
    yaml_copy[yaml_len] = '\0';

    cyaml_error_t err;
    cyaml_doc_t* parsed = cyaml_parse(yaml_copy, yaml_len, NULL, &err);
    if (!parsed) {
        free(yaml_copy);
        return NULL;
    }

    // Verify root is a mapping
    cyaml_node_t* parsed_root = cyaml_root(parsed);
    if (!parsed_root || !cyaml_is_map(parsed_root)) {
        cyaml_free(parsed);
        free(yaml_copy);
        return NULL;
    }

    // Convert to a built document so we can modify it
    // (parsed docs are read-only - cyaml_new_* only works on built docs)
    cyaml_doc_t* doc = cyaml_doc_new();
    if (!doc) {
        cyaml_free(parsed);
        free(yaml_copy);
        return NULL;
    }

    cyaml_node_t* root = cyaml_node_copy(doc, parsed, parsed_root);
    cyaml_free(parsed);
    free(yaml_copy);

    if (!root) {
        cyaml_free(doc);
        return NULL;
    }
    cyaml_set_root(doc, root);

    Frontmatter* fm = malloc(sizeof(Frontmatter));
    if (!fm) {
        cyaml_free(doc);
        return NULL;
    }
    fm->doc = doc;
    fm->backing_buf = NULL; // Built docs own their string data
    fm->string_cache = NULL;

    // Calculate consumed bytes
    if (consumed) {
        *consumed = (size_t)((end + 4) - content);
        // Skip trailing newlines after ---
        while (*consumed < len && content[*consumed] == '\n')
            (*consumed)++;
    }

    return fm;
}

Frontmatter* fm_create(void)
{
    cyaml_doc_t* doc = cyaml_doc_new();
    if (!doc)
        return NULL;

    // Create empty mapping as root
    cyaml_node_t* root = cyaml_new_map(doc);
    if (!root) {
        cyaml_free(doc);
        return NULL;
    }
    cyaml_set_root(doc, root);

    Frontmatter* fm = malloc(sizeof(Frontmatter));
    if (!fm) {
        cyaml_free(doc);
        return NULL;
    }
    fm->doc = doc;
    fm->backing_buf = NULL;
    fm->string_cache = NULL;
    return fm;
}

void fm_free(Frontmatter* fm)
{
    if (!fm)
        return;
    cache_free(fm->string_cache);
    if (fm->doc)
        cyaml_free(fm->doc);
    free(fm->backing_buf);
    free(fm);
}

// #endregion

// #region Accessors

const char* fm_get_string(const Frontmatter* fm, const char* key)
{
    if (!fm || !fm->doc || !key)
        return NULL;

    cyaml_node_t* root = cyaml_root(fm->doc);
    if (!root)
        return NULL;

    cyaml_node_t* node = cyaml_get(fm->doc, root, key);
    if (!node || !cyaml_is_scalar(node))
        return NULL;

    return get_cached_scalar((Frontmatter*)fm, node);
}

int fm_get_int(const Frontmatter* fm, const char* key, int default_val)
{
    const char* str = fm_get_string(fm, key);
    if (!str)
        return default_val;

    char* endptr;
    long val = strtol(str, &endptr, 10);
    if (endptr == str || *endptr != '\0')
        return default_val;

    return (int)val;
}

bool fm_get_bool(const Frontmatter* fm, const char* key, bool default_val)
{
    const char* str = fm_get_string(fm, key);
    if (!str)
        return default_val;

    if (strcmp(str, "true") == 0 || strcmp(str, "yes") == 0 || strcmp(str, "on") == 0 || strcmp(str, "1") == 0) {
        return true;
    }
    if (strcmp(str, "false") == 0 || strcmp(str, "no") == 0 || strcmp(str, "off") == 0 || strcmp(str, "0") == 0) {
        return false;
    }
    return default_val;
}

bool fm_has_key(const Frontmatter* fm, const char* key)
{
    if (!fm || !fm->doc || !key)
        return false;

    cyaml_node_t* root = cyaml_root(fm->doc);
    if (!root)
        return false;

    return cyaml_has(fm->doc, root, key);
}

//! Convert cyaml scalar kind to FmType
static FmType scalar_kind_to_fm_type(cyaml_scalar_kind_t kind)
{
    switch (kind) {
    case CYAML_KIND_NULL:
        return FM_NULL;
    case CYAML_KIND_BOOL:
        return FM_BOOL;
    case CYAML_KIND_INT:
        return FM_INT;
    case CYAML_KIND_FLOAT:
        return FM_FLOAT;
    case CYAML_KIND_STRING:
    default:
        return FM_STRING;
    }
}

FmType fm_get_type(const Frontmatter* fm, const char* key)
{
    if (!fm || !fm->doc || !key)
        return FM_NULL;

    cyaml_node_t* root = cyaml_root(fm->doc);
    if (!root)
        return FM_NULL;

    cyaml_node_t* node = cyaml_get(fm->doc, root, key);
    if (!node)
        return FM_NULL;

    if (cyaml_is_map(node))
        return FM_MAPPING;
    if (cyaml_is_seq(node))
        return FM_SEQUENCE;
    if (cyaml_is_null(node))
        return FM_NULL;

    return scalar_kind_to_fm_type(cyaml_scalar_kind(fm->doc, node));
}

const char* fm_type_name(FmType type)
{
    switch (type) {
    case FM_NULL:
        return "null";
    case FM_BOOL:
        return "bool";
    case FM_INT:
        return "int";
    case FM_FLOAT:
        return "float";
    case FM_STRING:
        return "string";
    case FM_SEQUENCE:
        return "list";
    case FM_MAPPING:
        return "object";
    }
    return "unknown";
}

// #endregion

// #region Mutators

bool fm_set_string(Frontmatter* fm, const char* key, const char* value)
{
    if (!fm || !fm->doc || !key)
        return false;

    cyaml_node_t* root = cyaml_root(fm->doc);
    if (!root || !cyaml_is_map(root))
        return false;

    cyaml_node_t* val_node;
    if (value) {
        val_node = cyaml_new_cstr(fm->doc, value);
    } else {
        val_node = cyaml_new_null(fm->doc);
    }

    if (!val_node)
        return false;

    return cyaml_map_set(fm->doc, root, key, val_node);
}

bool fm_set_int(Frontmatter* fm, const char* key, int value)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", value);
    return fm_set_string(fm, key, buf);
}

bool fm_set_bool(Frontmatter* fm, const char* key, bool value)
{
    return fm_set_string(fm, key, value ? "true" : "false");
}

bool fm_remove(Frontmatter* fm, const char* key)
{
    if (!fm || !fm->doc || !key)
        return false;

    // Use path-based deletion
    char path[256];
    snprintf(path, sizeof(path), "/%s", key);
    return cyaml_delete_at(fm->doc, path);
}

bool fm_set_sequence(Frontmatter* fm, const char* key, const char** items, int count, bool flow_style)
{
    if (!fm || !fm->doc || !key)
        return false;

    cyaml_node_t* root = cyaml_root(fm->doc);
    if (!root || !cyaml_is_map(root))
        return false;

    // Create sequence
    cyaml_node_t* seq_node = cyaml_new_seq(fm->doc);
    if (!seq_node)
        return false;

    // Set flow style if requested
    if (flow_style) {
        seq_node->style = (cyaml_style_t)CYAML_FLOW;
    }

    // Add items to the sequence
    for (int i = 0; i < count; i++) {
        cyaml_node_t* item_node;
        if (items[i]) {
            item_node = cyaml_new_cstr(fm->doc, items[i]);
        } else {
            item_node = cyaml_new_null(fm->doc);
        }
        if (!item_node) {
            return false;
        }
        if (!cyaml_seq_push(seq_node, item_node)) {
            return false;
        }
    }

    return cyaml_map_set(fm->doc, root, key, seq_node);
}

int fm_get_sequence_count(const Frontmatter* fm, const char* key)
{
    if (!fm || !fm->doc || !key)
        return 0;

    cyaml_node_t* root = cyaml_root(fm->doc);
    if (!root)
        return 0;

    cyaml_node_t* node = cyaml_get(fm->doc, root, key);
    if (!node || !cyaml_is_seq(node))
        return 0;

    return (int)cyaml_seq_len(node);
}

const char* fm_get_sequence_item(const Frontmatter* fm, const char* key, int index)
{
    if (!fm || !fm->doc || !key || index < 0)
        return NULL;

    cyaml_node_t* root = cyaml_root(fm->doc);
    if (!root)
        return NULL;

    cyaml_node_t* node = cyaml_get(fm->doc, root, key);
    if (!node || !cyaml_is_seq(node))
        return NULL;

    cyaml_node_t* item = cyaml_seq_get(node, (uint32_t)index);
    if (!item || !cyaml_is_scalar(item))
        return NULL;

    return get_cached_scalar((Frontmatter*)fm, item);
}

bool fm_is_sequence_flow(const Frontmatter* fm, const char* key)
{
    if (!fm || !fm->doc || !key)
        return false;

    cyaml_node_t* root = cyaml_root(fm->doc);
    if (!root)
        return false;

    cyaml_node_t* node = cyaml_get(fm->doc, root, key);
    if (!node || !cyaml_is_seq(node))
        return false;

    return node->style == (cyaml_style_t)CYAML_FLOW;
}

// #endregion

// #region Serialization

char* fm_to_string(const Frontmatter* fm, size_t* len)
{
    if (!fm || !fm->doc) {
        if (len)
            *len = 0;
        return NULL;
    }

    // Check if empty
    if (fm_count(fm) == 0) {
        if (len)
            *len = 0;
        return NULL;
    }

    // Emit YAML content, preserving original styles (flow vs block)
    cyaml_emit_opts_t opts = CYAML_EMIT_DEFAULT;
    opts.preserve_style = true;
    size_t yaml_len;
    char* yaml = cyaml_emit(fm->doc, &opts, &yaml_len);
    if (!yaml) {
        if (len)
            *len = 0;
        return NULL;
    }

    // Remove trailing newline if present (we'll add our own formatting)
    while (yaml_len > 0 && yaml[yaml_len - 1] == '\n') {
        yaml_len--;
        yaml[yaml_len] = '\0';
    }

    // Build full frontmatter with delimiters: "---\n" + yaml + "\n---\n"
    size_t total = 4 + yaml_len + 5;
    char* result = malloc(total + 1);
    if (!result) {
        free(yaml);
        if (len)
            *len = 0;
        return NULL;
    }

    memcpy(result, "---\n", 4);
    memcpy(result + 4, yaml, yaml_len);
    memcpy(result + 4 + yaml_len, "\n---\n", 5);
    result[total] = '\0';

    free(yaml);
    if (len)
        *len = total;
    return result;
}

// #endregion

// #region Iteration

//! Get type for a node
static FmType get_node_type(Frontmatter* fm, cyaml_node_t* node)
{
    if (!node || cyaml_is_null(node))
        return FM_NULL;
    if (cyaml_is_map(node))
        return FM_MAPPING;
    if (cyaml_is_seq(node))
        return FM_SEQUENCE;
    return scalar_kind_to_fm_type(cyaml_scalar_kind(fm->doc, node));
}

void fm_iterate(const Frontmatter* fm, FmIterCb cb, void* user_data)
{
    if (!fm || !fm->doc || !cb)
        return;

    cyaml_node_t* root = cyaml_root(fm->doc);
    if (!root || !cyaml_is_map(root))
        return;

    cyaml_pair_t* pair;
    CYAML_EACH_MAP(root, pair, _i)
    {
        cyaml_node_t* k = pair->key;
        cyaml_node_t* v = pair->val;

        const char* key_str = get_cached_scalar((Frontmatter*)fm, k);
        const char* val_str = NULL;
        FmType type = get_node_type((Frontmatter*)fm, v);

        if (v && cyaml_is_scalar(v)) {
            val_str = get_cached_scalar((Frontmatter*)fm, v);
        }

        if (key_str) {
            FmEntry entry = { .key = key_str, .value = val_str, .type = type };
            if (!cb(&entry, user_data))
                break;
        }
    }
}

int fm_count(const Frontmatter* fm)
{
    if (!fm || !fm->doc)
        return 0;

    cyaml_node_t* root = cyaml_root(fm->doc);
    if (!root || !cyaml_is_map(root))
        return 0;

    return (int)cyaml_map_len(root);
}

// #endregion
