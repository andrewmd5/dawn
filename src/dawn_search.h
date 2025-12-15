// dawn_search.h

#ifndef DAWN_SEARCH_H
#define DAWN_SEARCH_H

#include "dawn_types.h"

// #region Constants

//! Maximum number of search results
#define SEARCH_MAX_RESULTS 512

//! Context characters before/after match
#define SEARCH_CONTEXT_CHARS 40

//! Maximum search query length
#define SEARCH_MAX_QUERY 128

//! Debounce delay in milliseconds
#define SEARCH_DEBOUNCE_MS 150

// #endregion

// #region Types

//! A single search result
typedef struct {
    size_t pos;                         //!< Byte position of match
    size_t len;                         //!< Length of match
    int32_t line_num;                       //!< Line number (1-indexed)
    char context[SEARCH_CONTEXT_CHARS * 2 + SEARCH_MAX_QUERY + 8]; //!< Context with match
    int32_t context_len;                    //!< Context length
    int32_t match_start;                    //!< Match start within context
    int32_t match_len;                      //!< Match length in context
} SearchResult;

//! Search state
typedef struct {
    SearchResult results[SEARCH_MAX_RESULTS];  //!< All results
    int32_t count;                                 //!< Number of results
    int32_t selected;                              //!< Selected result index
    char query[SEARCH_MAX_QUERY];              //!< Search query
    int32_t query_len;                             //!< Query length
    int32_t query_cursor;                          //!< Query cursor position
    int32_t scroll;                                //!< Scroll offset
    bool case_sensitive;                       //!< Case sensitivity
    int64_t last_change_time;                  //!< Timestamp of last query change (ms)
    bool dirty;                                //!< Query changed, needs re-search
} SearchState;

// #endregion

// #region Search Operations

//! Mark query as changed (for debounce)
//! @param state search state
//! @param now_ms current timestamp in milliseconds
void search_mark_dirty(SearchState *state, int64_t now_ms);

//! Perform search on document (with debounce)
//! @param gb gap buffer containing document
//! @param state search state (uses state->query)
//! @param now_ms current timestamp in milliseconds
//! @return true if search was performed, false if debounced/skipped
bool search_find(const GapBuffer *gb, SearchState *state, int64_t now_ms);

//! Get currently selected result (or NULL if none)
//! @param state search state
//! @return pointer to selected result or NULL
const SearchResult *search_get_selected(const SearchState *state);

//! Initialize search state
//! @param state search state to initialize
void search_init(SearchState *state);

//! Jump to next match from current cursor position
//! @param gb gap buffer
//! @param state search state
//! @param cursor current cursor position
//! @return position of next match, or cursor if none
size_t search_next(const GapBuffer *gb, const SearchState *state, size_t cursor);

//! Jump to previous match from current cursor position
//! @param gb gap buffer
//! @param state search state
//! @param cursor current cursor position
//! @return position of previous match, or cursor if none
size_t search_prev(const GapBuffer *gb, const SearchState *state, size_t cursor);

// #endregion

#endif // DAWN_SEARCH_H
