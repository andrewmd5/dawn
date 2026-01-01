// dawn_utils.c

#include "dawn_utils.h"
#include "dawn_gap.h"
#include "dawn_md.h"
#include "dawn_theme.h"
#include "dawn_wrap.h"

// #region String Utilities

void dawn_strcpy(char* dest, const char* src)
{
    size_t len = strlen(src);
    memcpy(dest, src, len + 1);
}

char* dawn_strdup(const char* string)
{
    size_t length = 0;
    char* copy = NULL;

    if (string == NULL) {
        return NULL;
    }

    length = strlen((const char*)string) + sizeof("");
    copy = malloc(length);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, string, length);

    return copy;
}

void dawn_strncpy(char* dest, const char* src, size_t n)
{
    size_t len = strlen(src);
    if (len > n)
        len = n;
    memcpy(dest, src, len);
    dest[len] = '\0';
}

// #endregion

// #region UTF-8 String Navigation

int32_t str_utf8_prev(const char* str, int32_t pos)
{
    if (pos <= 0)
        return 0;
    pos--;
    while (pos > 0 && (str[pos] & 0xC0) == 0x80)
        pos--;
    return pos;
}

int32_t str_utf8_next(const char* str, int32_t pos, int32_t len)
{
    if (pos >= len)
        return len;
    pos++;
    while (pos < len && (str[pos] & 0xC0) == 0x80)
        pos++;
    return pos;
}

bool str_append_codepoint(char* buf, size_t buf_size, size_t* len, int32_t codepoint)
{
    uint8_t utf8_buf[4];
    utf8proc_ssize_t utf8_len = utf8proc_encode_char((utf8proc_int32_t)codepoint, utf8_buf);
    if (utf8_len <= 0 || *len + (size_t)utf8_len >= buf_size)
        return false;
    memcpy(buf + *len, utf8_buf, (size_t)utf8_len);
    *len += (size_t)utf8_len;
    buf[*len] = '\0';
    return true;
}

bool str_insert_codepoint(char* buf, size_t buf_size, size_t* len, size_t* cursor, int32_t codepoint)
{
    uint8_t utf8_buf[4];
    utf8proc_ssize_t utf8_len = utf8proc_encode_char((utf8proc_int32_t)codepoint, utf8_buf);
    if (utf8_len <= 0 || *len + (size_t)utf8_len >= buf_size)
        return false;
    memmove(buf + *cursor + utf8_len, buf + *cursor, *len - *cursor);
    memcpy(buf + *cursor, utf8_buf, (size_t)utf8_len);
    *len += (size_t)utf8_len;
    *cursor += (size_t)utf8_len;
    return true;
}

// #endregion

//! Current text scale for output (1-7, 1=normal)
int32_t current_text_scale = 1;

//! Current fractional scale numerator (0-15, 0 = no fraction)
int32_t current_frac_num = 0;

//! Current fractional scale denominator (0-15)
int32_t current_frac_denom = 0;

// #region Path Utilities

void get_chat_path(const char* session_path, char* chat_path, size_t bufsize)
{
    size_t len = strlen(session_path);
    if (len > 3 && strcmp(session_path + len - 3, ".md") == 0) {
        // Replace .md with .chat.json
        size_t base_len = len - 3;
        snprintf(chat_path, bufsize, "%.*s.chat.json", (int32_t)base_len, session_path);
    } else {
        snprintf(chat_path, bufsize, "%s.chat.json", session_path);
    }
}

// #endregion

// #region Text Utilities

size_t normalize_line_endings(char* buf, size_t len)
{
    size_t read = 0, write = 0;
    while (read < len) {
        if (buf[read] == '\r') {
            // CRLF -> LF, or standalone CR -> LF
            buf[write++] = '\n';
            read++;
            if (read < len && buf[read] == '\n')
                read++; // skip LF after CR
        } else {
            buf[write++] = buf[read++];
        }
    }
    return write;
}

// Word count cache - avoids rescanning entire document every frame
static struct {
    int32_t count; // Cached word count
    size_t text_len; // Text length when cache was computed
    bool valid; // Cache validity
} word_cache = { 0, 0, false };

void word_count_invalidate(void)
{
    word_cache.valid = false;
}

int32_t count_words(const GapBuffer* gb)
{
    size_t len = gap_len(gb);

    // Check if cache is valid
    if (word_cache.valid && word_cache.text_len == len) {
        return word_cache.count;
    }

    // Recompute word count
    int32_t words = 0;
    bool in_word = false;

    for (size_t i = 0; i < len; i++) {
        char c = gap_at(gb, i);
        if (ISSPACE_(c)) {
            in_word = false;
        } else if (!in_word) {
            in_word = true;
            words++;
        }
    }

    // Update cache
    word_cache.count = words;
    word_cache.text_len = len;
    word_cache.valid = true;

    return words;
}

// #endregion

// #region Grapheme Output

int32_t output_grapheme(const GapBuffer* gb, size_t* pos, MdStyle active_style)
{
    size_t len = gap_len(gb);
    if (*pos >= len)
        return 0;

    // Determine if we need any scaling
    bool needs_scaling = (current_text_scale > 1 || (current_frac_num > 0 && current_frac_denom > 0))
        && dawn_ctx_has(&app.ctx, DAWN_CAP_TEXT_SIZING);
    bool has_frac = (current_frac_num > 0 && current_frac_denom > current_frac_num);

    // Check for typographic replacements first (skipped inside inline code)
    size_t consumed = 0;
    const char* replacement = md_check_typo_replacement(gb, *pos, &consumed, active_style);
    if (replacement) {
        if (needs_scaling) {
            if (has_frac) {
                print_scaled_frac_str(replacement, strlen(replacement),
                    current_text_scale, current_frac_num, current_frac_denom);
            } else {
                print_scaled_str(replacement, strlen(replacement), current_text_scale);
            }
            *pos += consumed;
            return current_text_scale;
        } else {
            DAWN_BACKEND(app)->write_str(replacement, strlen(replacement));
            *pos += consumed;
            return 1;
        }
    }

    // Read first byte
    char first = gap_at(gb, *pos);

    // ASCII fast path
    if ((first & 0x80) == 0) {
        if (needs_scaling) {
            if (has_frac) {
                print_scaled_frac_char(first, current_text_scale, current_frac_num, current_frac_denom);
            } else {
                print_scaled_char(first, current_text_scale);
            }
            (*pos)++;
            return current_text_scale;
        } else {
            DAWN_BACKEND(app)->write_char(first);
            (*pos)++;
            return 1;
        }
    }

    // Multi-byte UTF-8: collect bytes
    uint8_t bytes[8];
    int32_t expected = utf8proc_utf8class[(uint8_t)first];
    if (expected < 1)
        expected = 1;

    int32_t n = 0;
    size_t bytepos = *pos;
    while (bytepos < len && n < expected && n < 7) {
        bytes[n++] = (uint8_t)gap_at(gb, bytepos);
        bytepos++;
    }
    bytes[n] = 0;

    // Get grapheme properties for width
    utf8proc_int32_t codepoint;
    utf8proc_iterate(bytes, n, &codepoint);
    int32_t width = utf8proc_charwidth(codepoint);
    if (width < 0)
        width = 1;

    // Output the bytes with text sizing if needed
    if (needs_scaling) {
        if (has_frac) {
            print_scaled_frac_str((const char*)bytes, (size_t)n,
                current_text_scale, current_frac_num, current_frac_denom);
        } else {
            print_scaled_str((const char*)bytes, (size_t)n, current_text_scale);
        }
        *pos += (size_t)n;
        return width * current_text_scale;
    } else {
        DAWN_BACKEND(app)->write_str((const char*)bytes, (size_t)n);
        *pos += (size_t)n;
        return width;
    }
}

int32_t output_grapheme_str(const char* text, size_t len, size_t* pos)
{
    if (*pos >= len)
        return 0;

    uint8_t first = (uint8_t)text[*pos];

    // ASCII fast path
    if ((first & 0x80) == 0) {
        DAWN_BACKEND(app)->write_char((char)first);
        (*pos)++;
        return 1;
    }

    // Multi-byte UTF-8
    int32_t n = utf8proc_utf8class[first];
    if (n < 1)
        n = 1;
    if (*pos + (size_t)n > len)
        n = (int32_t)(len - *pos);

    // Get width
    utf8proc_int32_t codepoint;
    utf8proc_iterate((const utf8proc_uint8_t*)&text[*pos], n, &codepoint);
    int32_t width = utf8proc_charwidth(codepoint);
    if (width < 0)
        width = 1;

    // Output
    DAWN_BACKEND(app)->write_str(&text[*pos], (size_t)n);
    *pos += (size_t)n;

    return width;
}

// #endregion

// #region Text Wrapping

int32_t chat_wrap_line(const char* text, size_t len, size_t start, int32_t width)
{
    if (start >= len)
        return 0;
    if (text[start] == '\n')
        return -1;

    int32_t col = 0;
    size_t pos = start;
    size_t last_break = start;

    while (pos < len) {
        utf8proc_int32_t cp;
        utf8proc_ssize_t bytes = utf8proc_iterate((const utf8proc_uint8_t*)&text[pos], len - pos, &cp);
        if (bytes <= 0) {
            pos++;
            continue;
        }

        if (cp == '\n')
            return (int32_t)(pos - start);

        int32_t gw = utf8proc_charwidth(cp);
        if (gw < 0)
            gw = 1;
        if (gw == 0)
            gw = 1;

        if (col + gw > width && col > 0) {
            if (last_break > start) {
                return (int32_t)(last_break - start);
            }
            return (int32_t)(pos - start);
        }

        col += gw;
        pos += (size_t)bytes;

        if (cp == ' ' || cp == '-') {
            last_break = pos;
        }
    }

    return (int32_t)(pos - start);
}

// #endregion
