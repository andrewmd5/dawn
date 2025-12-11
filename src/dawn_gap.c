// dawn_gap.c

#include "dawn_gap.h"

// #region Core Operations

void gap_init(GapBuffer *gb, size_t size) {
    gb->buffer_size = size;
    gb->buffer = malloc(size);
    gb->gap_start = 0;
    gb->gap_end = size;
}

void gap_free(GapBuffer *gb) {
    free(gb->buffer);
    gb->buffer = NULL;
}

size_t gap_len(const GapBuffer *gb) {
    return gb->buffer_size - (gb->gap_end - gb->gap_start);
}

//! Expand buffer to accommodate more text
//! @param gb gap buffer to expand
//! @param need minimum additional bytes needed
static void gap_expand(GapBuffer *gb, size_t need) {
    size_t after = gb->buffer_size - gb->gap_end;
    size_t new_size = gb->buffer_size + need + GAP_BUFFER_GAP_SIZE;
    char *new_buf = malloc(new_size);

    memcpy(new_buf, gb->buffer, gb->gap_start);
    size_t new_gap_end = new_size - after;
    memcpy(new_buf + new_gap_end, gb->buffer + gb->gap_end, after);

    free(gb->buffer);
    gb->buffer = new_buf;
    gb->gap_end = new_gap_end;
    gb->buffer_size = new_size;
}

//! Move gap to specified position
//! @param gb gap buffer to modify
//! @param pos target position for gap
static void gap_move(GapBuffer *gb, size_t pos) {
    size_t len = gap_len(gb);
    if (pos > len) pos = len;

    if (pos < gb->gap_start) {
        size_t n = gb->gap_start - pos;
        memmove(gb->buffer + gb->gap_end - n, gb->buffer + pos, n);
        gb->gap_start = pos;
        gb->gap_end -= n;
    } else if (pos > gb->gap_start) {
        size_t n = pos - gb->gap_start;
        memmove(gb->buffer + gb->gap_start, gb->buffer + gb->gap_end, n);
        gb->gap_start += n;
        gb->gap_end += n;
    }
}

void gap_insert(GapBuffer *gb, size_t pos, char c) {
    gap_move(gb, pos);
    if (gb->gap_start >= gb->gap_end) gap_expand(gb, 1);
    gb->buffer[gb->gap_start++] = c;
}

void gap_insert_str(GapBuffer *gb, size_t pos, const char *s, size_t n) {
    gap_move(gb, pos);
    if (gb->gap_end - gb->gap_start < n) gap_expand(gb, n);
    memcpy(gb->buffer + gb->gap_start, s, n);
    gb->gap_start += n;
}

void gap_delete(GapBuffer *gb, size_t pos, size_t n) {
    size_t len = gap_len(gb);
    if (pos >= len) return;
    if (pos + n > len) n = len - pos;
    gap_move(gb, pos);
    gb->gap_end += n;
}

char gap_at(const GapBuffer *gb, size_t pos) {
    if (pos >= gap_len(gb)) return '\0';
    return pos < gb->gap_start ? gb->buffer[pos] : gb->buffer[gb->gap_end + pos - gb->gap_start];
}

char *gap_to_str(const GapBuffer *gb) {
    size_t len = gap_len(gb);
    char *s = malloc(len + 1);
    memcpy(s, gb->buffer, gb->gap_start);
    memcpy(s + gb->gap_start, gb->buffer + gb->gap_end, gb->buffer_size - gb->gap_end);
    s[len] = '\0';
    return s;
}

char *gap_substr(const GapBuffer *gb, size_t start, size_t end) {
    size_t len = gap_len(gb);
    if (start > len) start = len;
    if (end > len) end = len;
    if (start > end) { size_t t = start; start = end; end = t; }

    size_t n = end - start;
    char *s = malloc(n + 1);

    // Optimize: use memcpy for contiguous regions instead of byte-by-byte
    if (end <= gb->gap_start) {
        // Entire range is before the gap
        memcpy(s, gb->buffer + start, n);
    } else if (start >= gb->gap_start) {
        // Entire range is after the gap
        size_t offset = gb->gap_end - gb->gap_start;
        memcpy(s, gb->buffer + start + offset, n);
    } else {
        // Range spans the gap - copy in two parts
        size_t before_gap = gb->gap_start - start;
        memcpy(s, gb->buffer + start, before_gap);
        memcpy(s + before_gap, gb->buffer + gb->gap_end, n - before_gap);
    }

    s[n] = '\0';
    return s;
}

void gap_copy_to(const GapBuffer *gb, size_t start, size_t count, char *dest) {
    size_t end = start + count;

    // Optimize: use memcpy for contiguous regions instead of byte-by-byte
    if (end <= gb->gap_start) {
        // Entire range is before the gap
        memcpy(dest, gb->buffer + start, count);
    } else if (start >= gb->gap_start) {
        // Entire range is after the gap
        size_t offset = gb->gap_end - gb->gap_start;
        memcpy(dest, gb->buffer + start + offset, count);
    } else {
        // Range spans the gap - copy in two parts
        size_t before_gap = gb->gap_start - start;
        memcpy(dest, gb->buffer + start, before_gap);
        memcpy(dest + before_gap, gb->buffer + gb->gap_end, count - before_gap);
    }
}

// #endregion

// #region UTF-8 Operations

size_t gap_utf8_prev(const GapBuffer *gb, size_t pos) {
    if (pos == 0) return 0;
    pos--;
    // Walk back past continuation bytes (10xxxxxx)
    while (pos > 0 && (gap_at(gb, pos) & 0xC0) == 0x80) {
        pos--;
    }
    return pos;
}

size_t gap_utf8_next(const GapBuffer *gb, size_t pos) {
    size_t len = gap_len(gb);
    if (pos >= len) return len;

    uint8_t c = (uint8_t)gap_at(gb, pos);
    int32_t char_len = utf8proc_utf8class[c];
    if (char_len < 1) char_len = 1;

    size_t new_pos = pos + (size_t)char_len;
    return new_pos > len ? len : new_pos;
}

int32_t gap_utf8_at(const GapBuffer *gb, size_t pos, size_t *char_len) {
    size_t len = gap_len(gb);
    if (pos >= len) {
        if (char_len) *char_len = 0;
        return -1;
    }

    // Optimized: avoid memcpy when bytes are contiguous in buffer
    size_t available = len - pos;
    size_t to_read = available < 4 ? available : 4;

    const utf8proc_uint8_t *ptr;
    utf8proc_uint8_t buf[4];

    if (pos < gb->gap_start) {
        // Before gap - check if all bytes are contiguous
        size_t before_gap = gb->gap_start - pos;
        if (before_gap >= to_read) {
            // All bytes before gap - use direct pointer (no copy)
            ptr = (const utf8proc_uint8_t *)(gb->buffer + pos);
        } else {
            // Spans gap - must copy
            memcpy(buf, gb->buffer + pos, before_gap);
            memcpy(buf + before_gap, gb->buffer + gb->gap_end, to_read - before_gap);
            ptr = buf;
        }
    } else {
        // After gap - use direct pointer (no copy)
        ptr = (const utf8proc_uint8_t *)(gb->buffer + gb->gap_end + (pos - gb->gap_start));
    }

    utf8proc_int32_t codepoint;
    utf8proc_ssize_t bytes = utf8proc_iterate(ptr, (utf8proc_ssize_t)to_read, &codepoint);

    if (bytes < 0) {
        if (char_len) *char_len = 1;
        return (uint8_t)*ptr;
    }

    if (char_len) *char_len = (size_t)bytes;
    return codepoint;
}

// #endregion
