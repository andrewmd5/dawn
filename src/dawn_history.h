// dawn_history.h - Session history management

#ifndef DAWN_HISTORY_H
#define DAWN_HISTORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// #region Types

//! History entry stored in JSON
typedef struct {
    char* path; //!< Full path to .md file
    char* title; //!< Document title
    int64_t created; //!< Creation timestamp (seconds since epoch)
    int64_t modified; //!< Last modified timestamp
} HistEntry;

// #endregion

// #region Lifecycle

//! Load history from .dawn/.history file
//! Populates app.history and app.hist_count
void hist_load(void);

//! Save history to .dawn/.history file
void hist_save(void);

//! Free all history entries
void hist_free(void);

//! Free history and CRDT state (call on shutdown)
void hist_shutdown(void);

// #endregion

// #region Operations

//! Add or update an entry in history
//! @param path Full path to the document
//! @param title Document title (can be NULL)
//! @param cursor Cursor position to save
//! @return true on success
bool hist_upsert(const char* path, const char* title, size_t cursor);

//! Remove an entry from history
//! @param path Full path to remove
//! @return true if entry was found and removed
bool hist_remove(const char* path);

//! Find entry by path
//! @param path Full path to find
//! @return Pointer to entry, or NULL if not found
HistEntry* hist_find(const char* path);

// #endregion

#endif // DAWN_HISTORY_H
