// dawn_crdt.h - Generic LWW-Element-Map CRDT

#ifndef DAWN_CRDT_H
#define DAWN_CRDT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CRDT_NODE_ID_LEN 16

// #region Types

//! CRDT entry with key-value pair, metadata, and vector clock
typedef struct {
    char* key;
    char* value;
    struct cJSON* meta; //!< Arbitrary key-value metadata (owned, can be NULL)
    int64_t timestamp;
    char node[CRDT_NODE_ID_LEN + 1];
} CrdtEntry;

//! Tombstone marker for deleted entries
typedef struct {
    char* key;
    int64_t timestamp;
    char node[CRDT_NODE_ID_LEN + 1];
} CrdtTombstone;

//! LWW-Element-Map state container
typedef struct {
    char node[CRDT_NODE_ID_LEN + 1]; //!< This node's unique ID
    CrdtEntry* entries;
    int32_t entry_count;
    CrdtTombstone* tombstones;
    int32_t tombstone_count;
} CrdtState;

// #endregion

// #region Core

//! Get current timestamp (epoch milliseconds)
int64_t crdt_timestamp(void);

//! Compare two (timestamp, node) pairs for LWW ordering
//! @return <0 if a < b, 0 if equal, >0 if a > b
int crdt_compare(int64_t ts_a, const char* node_a, int64_t ts_b, const char* node_b);

// #endregion

// #region Lifecycle

//! Create empty CRDT state with generated node ID
CrdtState* crdt_create(void);

//! Parse CRDT state from JSON
//! @param json JSON string to parse
//! @param len Length of JSON string
//! @return Parsed state or NULL on error
CrdtState* crdt_parse(const char* json, size_t len);

//! Serialize CRDT state to JSON
//! @return Allocated JSON string (caller must free)
char* crdt_serialize(const CrdtState* state);

//! Merge two CRDT states using LWW semantics
//! @return New merged state (caller must free)
CrdtState* crdt_merge(const CrdtState* a, const CrdtState* b);

//! Free CRDT state and all entries
void crdt_free(CrdtState* state);

// #endregion

// #region Operations

//! Add or update an entry
//! @param key Entry key (must not be NULL)
//! @param value Entry value (can be NULL)
void crdt_upsert(CrdtState* state, const char* key, const char* value);

//! Mark an entry as deleted (creates tombstone)
void crdt_remove(CrdtState* state, const char* key);

//! Find entry by key
//! @return Entry pointer or NULL if deleted/not found
CrdtEntry* crdt_find(const CrdtState* state, const char* key);

//! Get all live entries sorted by timestamp descending
//! @param count Output: number of entries returned
//! @return Array of entry pointers (caller must free array, not entries)
CrdtEntry** crdt_get_live(const CrdtState* state, int32_t* count);

// #endregion

// #region Metadata

//! Set a string metadata value on an entry
//! @param entry Entry to modify
//! @param meta_key Metadata key
//! @param meta_value String value (will be copied)
void crdt_meta_set_str(CrdtEntry* entry, const char* meta_key, const char* meta_value);

//! Set an integer metadata value on an entry
//! @param entry Entry to modify
//! @param meta_key Metadata key
//! @param meta_value Integer value
void crdt_meta_set_int(CrdtEntry* entry, const char* meta_key, int64_t meta_value);

//! Get a string metadata value from an entry
//! @param entry Entry to read
//! @param meta_key Metadata key
//! @return String value or NULL if not found/not string
const char* crdt_meta_get_str(const CrdtEntry* entry, const char* meta_key);

//! Get an integer metadata value from an entry
//! @param entry Entry to read
//! @param meta_key Metadata key
//! @param out Output value
//! @return true if found and is number, false otherwise
bool crdt_meta_get_int(const CrdtEntry* entry, const char* meta_key, int64_t* out);

// #endregion

#endif // DAWN_CRDT_H
