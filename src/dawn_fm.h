// dawn_fm.h - YAML frontmatter parsing and serialization

#ifndef DAWN_FM_H
#define DAWN_FM_H

#include <stddef.h>
#include <stdbool.h>

// Forward declare libfyaml types to avoid exposing header in public interface
struct fy_document;

// #region Frontmatter Types

//! Opaque frontmatter handle
typedef struct Frontmatter Frontmatter;

//! Value types for frontmatter entries
typedef enum {
    FM_NULL,        //!< null/~ value
    FM_BOOL,        //!< true/false/yes/no
    FM_INT,         //!< integer number
    FM_FLOAT,       //!< floating point number
    FM_STRING,      //!< text string
    FM_SEQUENCE,    //!< array/list
    FM_MAPPING,     //!< nested object
} FmType;

// #endregion

// #region Lifecycle

//! Parse YAML frontmatter from markdown content
//! @param content The full markdown content (starting with ---)
//! @param len Length of content
//! @param consumed Output: number of bytes consumed (frontmatter + delimiters)
//! @return Frontmatter handle, or NULL if no frontmatter or parse error
Frontmatter *fm_parse(const char *content, size_t len, size_t *consumed);

//! Create an empty frontmatter document
//! @return New empty frontmatter handle
Frontmatter *fm_create(void);

//! Free frontmatter resources
//! @param fm Frontmatter handle (safe to pass NULL)
void fm_free(Frontmatter *fm);

// #endregion

// #region Accessors

//! Get a string value from frontmatter
//! @param fm Frontmatter handle
//! @param key Key to look up (e.g., "title", "author")
//! @return String value (owned by frontmatter, do not free), or NULL if not found
const char *fm_get_string(const Frontmatter *fm, const char *key);

//! Get an integer value from frontmatter
//! @param fm Frontmatter handle
//! @param key Key to look up
//! @param default_val Value to return if key not found
//! @return Integer value, or default_val if not found
int fm_get_int(const Frontmatter *fm, const char *key, int default_val);

//! Get a boolean value from frontmatter
//! @param fm Frontmatter handle
//! @param key Key to look up
//! @param default_val Value to return if key not found
//! @return Boolean value, or default_val if not found
bool fm_get_bool(const Frontmatter *fm, const char *key, bool default_val);

//! Check if a key exists in frontmatter
//! @param fm Frontmatter handle
//! @param key Key to check
//! @return true if key exists
bool fm_has_key(const Frontmatter *fm, const char *key);

//! Get the type of a value in frontmatter
//! @param fm Frontmatter handle
//! @param key Key to check
//! @return Type of the value, or FM_NULL if not found
FmType fm_get_type(const Frontmatter *fm, const char *key);

//! Get type name as string (for debugging/display)
//! @param type The type to get name for
//! @return Static string like "string", "int", "bool", etc.
const char *fm_type_name(FmType type);

// #endregion

// #region Mutators

//! Set a string value in frontmatter
//! @param fm Frontmatter handle
//! @param key Key to set
//! @param value String value (will be copied)
//! @return true on success
bool fm_set_string(Frontmatter *fm, const char *key, const char *value);

//! Set an integer value in frontmatter
//! @param fm Frontmatter handle
//! @param key Key to set
//! @param value Integer value
//! @return true on success
bool fm_set_int(Frontmatter *fm, const char *key, int value);

//! Set a boolean value in frontmatter
//! @param fm Frontmatter handle
//! @param key Key to set
//! @param value Boolean value
//! @return true on success
bool fm_set_bool(Frontmatter *fm, const char *key, bool value);

//! Remove a key from frontmatter
//! @param fm Frontmatter handle
//! @param key Key to remove
//! @return true if key was removed, false if not found
bool fm_remove(Frontmatter *fm, const char *key);

//! Set a sequence (array) value in frontmatter
//! @param fm Frontmatter handle
//! @param key Key to set
//! @param items Array of string items
//! @param count Number of items
//! @param flow_style true for ["a","b"] style, false for block style (- a)
//! @return true on success
bool fm_set_sequence(Frontmatter *fm, const char *key, const char **items, int count, bool flow_style);

//! Get number of items in a sequence
//! @param fm Frontmatter handle
//! @param key Key to look up
//! @return Number of items, or 0 if not a sequence
int fm_get_sequence_count(const Frontmatter *fm, const char *key);

//! Get an item from a sequence by index
//! @param fm Frontmatter handle
//! @param key Key to look up
//! @param index Item index (0-based)
//! @return String value (owned by frontmatter), or NULL if not found
const char *fm_get_sequence_item(const Frontmatter *fm, const char *key, int index);

//! Check if a sequence uses flow style (["a","b"]) vs block style (- a)
//! @param fm Frontmatter handle
//! @param key Key to look up
//! @return true if flow style, false if block style or not a sequence
bool fm_is_sequence_flow(const Frontmatter *fm, const char *key);

// #endregion

// #region Serialization

//! Serialize frontmatter to YAML string with delimiters
//! @param fm Frontmatter handle
//! @param len Output: length of returned string (excluding null terminator)
//! @return Allocated string "---\n...\n---\n", caller must free. NULL on error.
char *fm_to_string(const Frontmatter *fm, size_t *len);

// #endregion

// #region Iteration

//! Frontmatter key-value pair for iteration
typedef struct {
    const char *key;
    const char *value;  //!< String representation of value (NULL for sequences/mappings)
    FmType type;        //!< Type of the value
} FmEntry;

//! Iterator callback
//! @param entry Current key-value pair
//! @param user_data User data passed to fm_iterate
//! @return true to continue iteration, false to stop
typedef bool (*FmIterCb)(const FmEntry *entry, void *user_data);

//! Iterate over all frontmatter entries
//! @param fm Frontmatter handle
//! @param cb Callback function
//! @param user_data User data passed to callback
void fm_iterate(const Frontmatter *fm, FmIterCb cb, void *user_data);

//! Count number of entries in frontmatter
//! @param fm Frontmatter handle
//! @return Number of top-level keys
int fm_count(const Frontmatter *fm);

// #endregion

#endif // DAWN_FM_H
