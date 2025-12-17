// dawn_crdt.c - Generic LWW-Element-Map CRDT

#include "dawn_crdt.h"
#include "cJSON.h"
#include "dawn_types.h"
#include "dawn_utils.h"
#include <stdlib.h>
#include <string.h>

#define CRDT_VERSION 2

// #region Helpers

static void generate_node_id(char* out)
{
    uint64_t r = 0;
    int64_t ms = DAWN_BACKEND(app)->clock(DAWN_CLOCK_MS);
    r ^= (uint64_t)ms;
    r ^= ((uint64_t)ms << 17) | ((uint64_t)ms >> 47);
    r *= 0x9E3779B97F4A7C15ULL;

    for (int i = 0; i < CRDT_NODE_ID_LEN; i++) {
        int nibble = (int)((r >> (i * 4)) & 0xF);
        out[i] = "0123456789abcdef"[nibble];
    }
    out[CRDT_NODE_ID_LEN] = '\0';
}

static CrdtTombstone* find_tombstone(const CrdtState* state, const char* key)
{
    for (int32_t i = 0; i < state->tombstone_count; i++) {
        if (strcmp(state->tombstones[i].key, key) == 0)
            return &state->tombstones[i];
    }
    return NULL;
}

static CrdtEntry* find_entry_internal(const CrdtState* state, const char* key)
{
    for (int32_t i = 0; i < state->entry_count; i++) {
        if (strcmp(state->entries[i].key, key) == 0)
            return &state->entries[i];
    }
    return NULL;
}

// #endregion

// #region Core

int64_t crdt_timestamp(void)
{
    return DAWN_BACKEND(app)->clock(DAWN_CLOCK_SEC) * 1000;
}

int crdt_compare(int64_t ts_a, const char* node_a, int64_t ts_b, const char* node_b)
{
    if (ts_a != ts_b)
        return (ts_a > ts_b) ? 1 : -1;
    return strcmp(node_a, node_b);
}

// #endregion

// #region Lifecycle

CrdtState* crdt_create(void)
{
    CrdtState* state = calloc(1, sizeof(CrdtState));
    generate_node_id(state->node);
    return state;
}

CrdtState* crdt_parse(const char* json, size_t len)
{
    if (!json || len == 0)
        return NULL;

    cJSON* root = cJSON_ParseWithLength(json, len);
    if (!root)
        return NULL;

    CrdtState* state = calloc(1, sizeof(CrdtState));

    cJSON* version = cJSON_GetObjectItem(root, "version");
    if (!version || !cJSON_IsNumber(version) || version->valueint != CRDT_VERSION) {
        cJSON_Delete(root);
        free(state);
        return NULL;
    }

    cJSON* node = cJSON_GetObjectItem(root, "node");
    if (node && cJSON_IsString(node)) {
        dawn_strncpy(state->node, node->valuestring, CRDT_NODE_ID_LEN);
    }

    cJSON* entries = cJSON_GetObjectItem(root, "entries");
    if (entries && cJSON_IsObject(entries)) {
        int count = cJSON_GetArraySize(entries);
        if (count > 0) {
            state->entries = calloc((size_t)count, sizeof(CrdtEntry));
            cJSON* entry;
            cJSON_ArrayForEach(entry, entries)
            {
                const char* key = entry->string;
                cJSON* value_j = cJSON_GetObjectItem(entry, "value");
                cJSON* ts_j = cJSON_GetObjectItem(entry, "ts");
                cJSON* node_j = cJSON_GetObjectItem(entry, "node");

                CrdtEntry* e = &state->entries[state->entry_count];
                e->key = strdup(key);
                e->value = (value_j && cJSON_IsString(value_j)) ? strdup(value_j->valuestring) : NULL;
                e->timestamp = (ts_j && cJSON_IsNumber(ts_j)) ? (int64_t)ts_j->valuedouble : 0;
                if (node_j && cJSON_IsString(node_j)) {
                    dawn_strncpy(e->node, node_j->valuestring, CRDT_NODE_ID_LEN);
                }
                state->entry_count++;
            }
        }
    }

    cJSON* tombstones = cJSON_GetObjectItem(root, "tombstones");
    if (tombstones && cJSON_IsObject(tombstones)) {
        int count = cJSON_GetArraySize(tombstones);
        if (count > 0) {
            state->tombstones = calloc((size_t)count, sizeof(CrdtTombstone));
            cJSON* tomb;
            cJSON_ArrayForEach(tomb, tombstones)
            {
                const char* key = tomb->string;
                cJSON* ts_j = cJSON_GetObjectItem(tomb, "ts");
                cJSON* node_j = cJSON_GetObjectItem(tomb, "node");

                CrdtTombstone* t = &state->tombstones[state->tombstone_count];
                t->key = strdup(key);
                t->timestamp = (ts_j && cJSON_IsNumber(ts_j)) ? (int64_t)ts_j->valuedouble : 0;
                if (node_j && cJSON_IsString(node_j)) {
                    dawn_strncpy(t->node, node_j->valuestring, CRDT_NODE_ID_LEN);
                }
                state->tombstone_count++;
            }
        }
    }

    cJSON_Delete(root);
    return state;
}

char* crdt_serialize(const CrdtState* state)
{
    if (!state)
        return NULL;

    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "version", CRDT_VERSION);
    cJSON_AddStringToObject(root, "node", state->node);

    cJSON* entries = cJSON_CreateObject();
    for (int32_t i = 0; i < state->entry_count; i++) {
        CrdtEntry* e = &state->entries[i];
        cJSON* entry = cJSON_CreateObject();
        if (e->value)
            cJSON_AddStringToObject(entry, "value", e->value);
        cJSON_AddNumberToObject(entry, "ts", (double)e->timestamp);
        cJSON_AddStringToObject(entry, "node", e->node);
        cJSON_AddItemToObject(entries, e->key, entry);
    }
    cJSON_AddItemToObject(root, "entries", entries);

    cJSON* tombstones = cJSON_CreateObject();
    for (int32_t i = 0; i < state->tombstone_count; i++) {
        CrdtTombstone* t = &state->tombstones[i];
        cJSON* tomb = cJSON_CreateObject();
        cJSON_AddNumberToObject(tomb, "ts", (double)t->timestamp);
        cJSON_AddStringToObject(tomb, "node", t->node);
        cJSON_AddItemToObject(tombstones, t->key, tomb);
    }
    cJSON_AddItemToObject(root, "tombstones", tombstones);

    char* json = cJSON_Print(root);
    cJSON_Delete(root);
    return json;
}

CrdtState* crdt_merge(const CrdtState* a, const CrdtState* b)
{
    if (!a && !b)
        return NULL;
    if (!a)
        return crdt_merge(b, b);
    if (!b)
        return crdt_merge(a, a);

    CrdtState* result = calloc(1, sizeof(CrdtState));
    dawn_strncpy(result->node, a->node, CRDT_NODE_ID_LEN);

    int32_t max_entries = a->entry_count + b->entry_count;
    int32_t max_tombs = a->tombstone_count + b->tombstone_count;

    result->entries = max_entries > 0 ? calloc((size_t)max_entries, sizeof(CrdtEntry)) : NULL;
    result->tombstones = max_tombs > 0 ? calloc((size_t)max_tombs, sizeof(CrdtTombstone)) : NULL;

    char** all_keys = calloc((size_t)(max_entries + max_tombs), sizeof(char*));
    int32_t key_count = 0;

    for (int32_t i = 0; i < a->entry_count; i++) {
        bool found = false;
        for (int32_t j = 0; j < key_count; j++) {
            if (strcmp(all_keys[j], a->entries[i].key) == 0) {
                found = true;
                break;
            }
        }
        if (!found)
            all_keys[key_count++] = a->entries[i].key;
    }
    for (int32_t i = 0; i < b->entry_count; i++) {
        bool found = false;
        for (int32_t j = 0; j < key_count; j++) {
            if (strcmp(all_keys[j], b->entries[i].key) == 0) {
                found = true;
                break;
            }
        }
        if (!found)
            all_keys[key_count++] = b->entries[i].key;
    }
    for (int32_t i = 0; i < a->tombstone_count; i++) {
        bool found = false;
        for (int32_t j = 0; j < key_count; j++) {
            if (strcmp(all_keys[j], a->tombstones[i].key) == 0) {
                found = true;
                break;
            }
        }
        if (!found)
            all_keys[key_count++] = a->tombstones[i].key;
    }
    for (int32_t i = 0; i < b->tombstone_count; i++) {
        bool found = false;
        for (int32_t j = 0; j < key_count; j++) {
            if (strcmp(all_keys[j], b->tombstones[i].key) == 0) {
                found = true;
                break;
            }
        }
        if (!found)
            all_keys[key_count++] = b->tombstones[i].key;
    }

    for (int32_t i = 0; i < key_count; i++) {
        const char* key = all_keys[i];

        CrdtEntry* entry_a = find_entry_internal(a, key);
        CrdtEntry* entry_b = find_entry_internal(b, key);
        CrdtTombstone* tomb_a = find_tombstone(a, key);
        CrdtTombstone* tomb_b = find_tombstone(b, key);

        CrdtEntry* best_entry = NULL;
        if (entry_a && entry_b) {
            best_entry = (crdt_compare(entry_a->timestamp, entry_a->node, entry_b->timestamp, entry_b->node) >= 0) ? entry_a : entry_b;
        } else {
            best_entry = entry_a ? entry_a : entry_b;
        }

        CrdtTombstone* best_tomb = NULL;
        if (tomb_a && tomb_b) {
            best_tomb = (crdt_compare(tomb_a->timestamp, tomb_a->node, tomb_b->timestamp, tomb_b->node) >= 0) ? tomb_a : tomb_b;
        } else {
            best_tomb = tomb_a ? tomb_a : tomb_b;
        }

        if (best_entry && best_tomb) {
            int cmp = crdt_compare(best_entry->timestamp, best_entry->node, best_tomb->timestamp, best_tomb->node);
            if (cmp >= 0) {
                CrdtEntry* e = &result->entries[result->entry_count++];
                e->key = strdup(best_entry->key);
                e->value = best_entry->value ? strdup(best_entry->value) : NULL;
                e->timestamp = best_entry->timestamp;
                dawn_strncpy(e->node, best_entry->node, CRDT_NODE_ID_LEN);
            } else {
                CrdtTombstone* t = &result->tombstones[result->tombstone_count++];
                t->key = strdup(best_tomb->key);
                t->timestamp = best_tomb->timestamp;
                dawn_strncpy(t->node, best_tomb->node, CRDT_NODE_ID_LEN);
            }
        } else if (best_entry) {
            CrdtEntry* e = &result->entries[result->entry_count++];
            e->key = strdup(best_entry->key);
            e->value = best_entry->value ? strdup(best_entry->value) : NULL;
            e->timestamp = best_entry->timestamp;
            dawn_strncpy(e->node, best_entry->node, CRDT_NODE_ID_LEN);
        } else if (best_tomb) {
            CrdtTombstone* t = &result->tombstones[result->tombstone_count++];
            t->key = strdup(best_tomb->key);
            t->timestamp = best_tomb->timestamp;
            dawn_strncpy(t->node, best_tomb->node, CRDT_NODE_ID_LEN);
        }
    }

    free(all_keys);
    return result;
}

void crdt_free(CrdtState* state)
{
    if (!state)
        return;

    for (int32_t i = 0; i < state->entry_count; i++) {
        free(state->entries[i].key);
        free(state->entries[i].value);
    }
    free(state->entries);

    for (int32_t i = 0; i < state->tombstone_count; i++) {
        free(state->tombstones[i].key);
    }
    free(state->tombstones);

    free(state);
}

// #endregion

// #region Operations

void crdt_upsert(CrdtState* state, const char* key, const char* value)
{
    if (!state || !key)
        return;

    int64_t ts = crdt_timestamp();

    for (int32_t i = 0; i < state->tombstone_count; i++) {
        if (strcmp(state->tombstones[i].key, key) == 0) {
            free(state->tombstones[i].key);
            memmove(&state->tombstones[i], &state->tombstones[i + 1],
                sizeof(CrdtTombstone) * (size_t)(state->tombstone_count - i - 1));
            state->tombstone_count--;
            break;
        }
    }

    CrdtEntry* existing = find_entry_internal(state, key);
    if (existing) {
        free(existing->value);
        existing->value = value ? strdup(value) : NULL;
        existing->timestamp = ts;
        dawn_strncpy(existing->node, state->node, CRDT_NODE_ID_LEN);
    } else {
        state->entries = realloc(state->entries, sizeof(CrdtEntry) * (size_t)(state->entry_count + 1));
        CrdtEntry* e = &state->entries[state->entry_count++];
        e->key = strdup(key);
        e->value = value ? strdup(value) : NULL;
        e->timestamp = ts;
        dawn_strncpy(e->node, state->node, CRDT_NODE_ID_LEN);
    }
}

void crdt_remove(CrdtState* state, const char* key)
{
    if (!state || !key)
        return;

    int64_t ts = crdt_timestamp();

    CrdtTombstone* existing = find_tombstone(state, key);
    if (existing) {
        existing->timestamp = ts;
        dawn_strncpy(existing->node, state->node, CRDT_NODE_ID_LEN);
    } else {
        state->tombstones = realloc(state->tombstones, sizeof(CrdtTombstone) * (size_t)(state->tombstone_count + 1));
        CrdtTombstone* t = &state->tombstones[state->tombstone_count++];
        t->key = strdup(key);
        t->timestamp = ts;
        dawn_strncpy(t->node, state->node, CRDT_NODE_ID_LEN);
    }
}

CrdtEntry* crdt_find(const CrdtState* state, const char* key)
{
    if (!state || !key)
        return NULL;

    CrdtTombstone* tomb = find_tombstone(state, key);
    CrdtEntry* entry = find_entry_internal(state, key);

    if (!entry)
        return NULL;
    if (!tomb)
        return entry;

    if (crdt_compare(entry->timestamp, entry->node, tomb->timestamp, tomb->node) >= 0)
        return entry;
    return NULL;
}

static int entry_cmp_desc(const void* a, const void* b)
{
    const CrdtEntry* ea = *(const CrdtEntry**)a;
    const CrdtEntry* eb = *(const CrdtEntry**)b;
    if (ea->timestamp > eb->timestamp)
        return -1;
    if (ea->timestamp < eb->timestamp)
        return 1;
    return strcmp(ea->node, eb->node);
}

CrdtEntry** crdt_get_live(const CrdtState* state, int32_t* count)
{
    if (!state || !count) {
        if (count)
            *count = 0;
        return NULL;
    }

    CrdtEntry** live = calloc((size_t)(state->entry_count + 1), sizeof(CrdtEntry*));
    int32_t n = 0;

    for (int32_t i = 0; i < state->entry_count; i++) {
        CrdtEntry* e = &state->entries[i];
        CrdtTombstone* t = find_tombstone(state, e->key);
        if (!t || crdt_compare(e->timestamp, e->node, t->timestamp, t->node) >= 0) {
            live[n++] = e;
        }
    }

    qsort(live, (size_t)n, sizeof(CrdtEntry*), entry_cmp_desc);
    *count = n;
    return live;
}

// #endregion
