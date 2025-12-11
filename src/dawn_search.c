// dawn_search.c

#include "dawn_search.h"
#include "dawn_gap.h"
#include "dawn_utils.h"
#include <string.h>

// #region Initialization

void search_init(SearchState *state) {
    memset(state, 0, sizeof(SearchState));
    state->case_sensitive = false;
}

// #endregion

// #region Case-Insensitive Matching

//! Case-insensitive character comparison
static inline bool char_eq(char a, char b, bool case_sensitive) {
    if (case_sensitive) return a == b;
    return TOLOWER_(a) == TOLOWER_(b);
}

//! Find needle in gap buffer starting at pos
//! @return position of match or SIZE_MAX if not found
static size_t find_match(const GapBuffer *gb, size_t start, const char *needle, int32_t needle_len, bool case_sensitive) {
    size_t len = gap_len(gb);
    if (needle_len == 0 || start + (size_t)needle_len > len) return SIZE_MAX;

    for (size_t pos = start; pos + (size_t)needle_len <= len; pos++) {
        bool found = true;
        for (int32_t i = 0; i < needle_len; i++) {
            if (!char_eq(gap_at(gb, pos + i), needle[i], case_sensitive)) {
                found = false;
                break;
            }
        }
        if (found) return pos;
    }
    return SIZE_MAX;
}

// #endregion

// #region Context Building

//! Build context string around a match
static void build_context(const GapBuffer *gb, SearchResult *r) {
    size_t len = gap_len(gb);
    size_t ctx_start = r->pos;
    size_t ctx_end = r->pos + r->len;

    int32_t chars_before = 0;
    while (ctx_start > 0 && chars_before < SEARCH_CONTEXT_CHARS) {
        ctx_start--;
        char c = gap_at(gb, ctx_start);
        if (c == '\n') {
            ctx_start++;
            break;
        }
        chars_before++;
    }

    int32_t chars_after = 0;
    while (ctx_end < len && chars_after < SEARCH_CONTEXT_CHARS) {
        char c = gap_at(gb, ctx_end);
        if (c == '\n') break;
        ctx_end++;
        chars_after++;
    }

    int32_t ci = 0;

    if (ctx_start > 0 && gap_at(gb, ctx_start - 1) != '\n') {
        r->context[ci++] = '.';
        r->context[ci++] = '.';
        r->context[ci++] = '.';
    }

    r->match_start = ci + (int32_t)(r->pos - ctx_start);

    for (size_t p = ctx_start; p < ctx_end && ci < (int32_t)sizeof(r->context) - 4; p++) {
        char c = gap_at(gb, p);
        r->context[ci++] = (c == '\t') ? ' ' : c;
    }

    r->match_len = (int32_t)r->len;

    if (ctx_end < len && gap_at(gb, ctx_end) != '\n') {
        r->context[ci++] = '.';
        r->context[ci++] = '.';
        r->context[ci++] = '.';
    }

    r->context[ci] = '\0';
    r->context_len = ci;
}

//! Count line number at position
static int32_t count_line_at(const GapBuffer *gb, size_t pos) {
    int32_t line = 1;
    for (size_t p = 0; p < pos; p++) {
        if (gap_at(gb, p) == '\n') line++;
    }
    return line;
}

// #endregion

// #region Search Operations

void search_find(const GapBuffer *gb, SearchState *state) {
    state->count = 0;
    state->selected = 0;
    state->scroll = 0;

    if (state->query_len == 0) return;

    size_t pos = 0;

    while (state->count < SEARCH_MAX_RESULTS) {
        pos = find_match(gb, pos, state->query, state->query_len, state->case_sensitive);
        if (pos == SIZE_MAX) break;

        SearchResult *r = &state->results[state->count];
        r->pos = pos;
        r->len = (size_t)state->query_len;
        r->line_num = count_line_at(gb, pos);

        build_context(gb, r);

        state->count++;
        pos++;
    }
}

const SearchResult *search_get_selected(const SearchState *state) {
    if (state->count == 0) return NULL;
    return &state->results[state->selected];
}

size_t search_next(const GapBuffer *gb, const SearchState *state, size_t cursor) {
    if (state->query_len == 0) return cursor;

    size_t pos = find_match(gb, cursor + 1, state->query, state->query_len, state->case_sensitive);
    if (pos != SIZE_MAX) return pos;

    pos = find_match(gb, 0, state->query, state->query_len, state->case_sensitive);
    if (pos != SIZE_MAX) return pos;

    return cursor;
}

size_t search_prev(const GapBuffer *gb, const SearchState *state, size_t cursor) {
    if (state->query_len == 0) return cursor;

    size_t best = SIZE_MAX;
    size_t pos = 0;
    while (pos < cursor) {
        pos = find_match(gb, pos, state->query, state->query_len, state->case_sensitive);
        if (pos == SIZE_MAX || pos >= cursor) break;
        best = pos;
        pos++;
    }

    if (best != SIZE_MAX) return best;

    size_t len = gap_len(gb);
    pos = cursor;
    while (pos < len) {
        pos = find_match(gb, pos, state->query, state->query_len, state->case_sensitive);
        if (pos == SIZE_MAX) break;
        best = pos;
        pos++;
    }

    return (best != SIZE_MAX) ? best : cursor;
}

// #endregion
