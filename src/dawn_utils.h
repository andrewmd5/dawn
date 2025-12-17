// dawn_utils.h

#ifndef DAWN_UTILS_H
#define DAWN_UTILS_H

#include "dawn_md.h"
#include "dawn_types.h"
#include <stdio.h>
#include <stdlib.h>

// #region Assert

#ifdef _MSC_VER
#define DAWN_ASSERT(cond, fmt, ...)                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            fprintf(stderr, "fatal: " fmt " (%s:%d)\n", __VA_ARGS__, __FILE__, __LINE__);                              \
            abort();                                                                                                   \
        }                                                                                                              \
    } while (0)
#else
#define DAWN_ASSERT(cond, fmt, ...)                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            fprintf(stderr, "fatal: " fmt " (%s:%d)\n", ##__VA_ARGS__, __FILE__, __LINE__);                            \
            abort();                                                                                                   \
        }                                                                                                              \
    } while (0)
#endif

// #endregion

// #region String Utilities

//! Safe string copy using memcpy, always null-terminates
//! @param dest destination buffer (must be at least strlen(src)+1 bytes)
//! @param src source string
static inline void dawn_strcpy(char* dest, const char* src)
{
    size_t len = strlen(src);
    memcpy(dest, src, len + 1);
}

//! Safe string copy with max length, always null-terminates
//! @param dest destination buffer (must be at least n+1 bytes)
//! @param src source string
//! @param n max bytes to copy (not including null terminator)
static inline void dawn_strncpy(char* dest, const char* src, size_t n)
{
    size_t len = strlen(src);
    if (len > n)
        len = n;
    memcpy(dest, src, len);
    dest[len] = '\0';
}

// #endregion

// #region Character Classification (ASCII)

#define ISIN_(ch, lo, hi) ((uint8_t)((ch) - (lo)) <= (hi) - (lo))
#define ISALPHA_(ch) (ISIN_((ch) | 32, 'a', 'z'))
#define ISDIGIT_(ch) (ISIN_(ch, '0', '9'))
#define ISALNUM_(ch) (ISALPHA_(ch) || ISDIGIT_(ch))
#define ISUPPER_(ch) (ISIN_(ch, 'A', 'Z'))
#define ISLOWER_(ch) (ISIN_(ch, 'a', 'z'))
#define ISXDIGIT_(ch) (ISDIGIT_(ch) || ISIN_((ch) | 32, 'a', 'f'))
#define ISBLANK_(ch) ((ch) == ' ' || (ch) == '\t')
#define ISSPACE_(ch) (ISBLANK_(ch) || ISIN_(ch, '\n', '\r') || (ch) == '\v' || (ch) == '\f')
#define ISPUNCT_(ch) (ISIN_(ch, 33, 47) || ISIN_(ch, 58, 64) || ISIN_(ch, 91, 96) || ISIN_(ch, 123, 126))
#define TOLOWER_(ch) (ISUPPER_(ch) ? (ch) | 32 : (ch))
#define TOUPPER_(ch) (ISLOWER_(ch) ? (ch) & ~32 : (ch))

// #endregion

// #region Path Utilities

//! Get the chat history path for a session file
//! @param session_path path to the .md session file
//! @param chat_path output buffer for chat path
//! @param bufsize size of chat_path buffer
void get_chat_path(const char* session_path, char* chat_path, size_t bufsize);

// #endregion

// #region Text Utilities

//! Normalize line endings in a buffer (CRLF -> LF, standalone CR -> LF)
//! Modifies buffer in-place, returns new length
//! @param buf buffer to normalize
//! @param len length of buffer
//! @return new length after normalization
size_t normalize_line_endings(char* buf, size_t len);

//! Count words in a gap buffer (uses cache for performance)
//! @param gb gap buffer containing text
//! @return number of words
int32_t count_words(const GapBuffer* gb);

//! Invalidate word count cache (call when text changes)
void word_count_invalidate(void);

// #endregion

// #region Grapheme Output

//! Current text scale for output (1-7, 1=normal)
//! Set by md_apply for headers, used by output_grapheme
extern int32_t current_text_scale;

//! Current fractional scale numerator (0-15, 0 = no fraction)
//! Set by md_apply for headers using fractional scaling
extern int32_t current_frac_num;

//! Current fractional scale denominator (0-15, must be > num when non-zero)
//! Set by md_apply for headers using fractional scaling
extern int32_t current_frac_denom;

//! Output a single grapheme from a gap buffer, advancing position
//! Uses current_text_scale for text sizing if terminal supports it
//! @param gb gap buffer to read from
//! @param pos position pointer (advanced past the grapheme)
//! @param active_style current inline style (passed to typo replacement check)
//! @return display width of the grapheme (multiplied by scale)
int32_t output_grapheme(const GapBuffer* gb, size_t* pos, MdStyle active_style);

//! Output a single grapheme from a string, advancing position
//! @param text string to read from
//! @param len length of string
//! @param pos position pointer (advanced past the grapheme)
//! @return display width of the grapheme
int32_t output_grapheme_str(const char* text, size_t len, size_t* pos);

// #endregion

// #region UTF-8 String Navigation

//! Find start of previous UTF-8 character in a string
//! @param str string buffer
//! @param pos current byte position
//! @return byte position of previous character start (0 if at beginning)
static inline int32_t str_utf8_prev(const char* str, int32_t pos)
{
    if (pos <= 0)
        return 0;
    pos--;
    while (pos > 0 && (str[pos] & 0xC0) == 0x80)
        pos--;
    return pos;
}

//! Find start of next UTF-8 character in a string
//! @param str string buffer
//! @param pos current byte position
//! @param len total string length
//! @return byte position after current character (len if at end)
static inline int32_t str_utf8_next(const char* str, int32_t pos, int32_t len)
{
    if (pos >= len)
        return len;
    pos++;
    while (pos < len && (str[pos] & 0xC0) == 0x80)
        pos++;
    return pos;
}

//! Append a Unicode codepoint (as UTF-8) to a string buffer
//! @param buf string buffer
//! @param buf_size total buffer size
//! @param len pointer to current string length (updated on success)
//! @param codepoint Unicode codepoint to append
//! @return true if appended, false if buffer full
static inline bool str_append_codepoint(char* buf, size_t buf_size, size_t* len, int32_t codepoint)
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

//! Insert a Unicode codepoint (as UTF-8) into a string buffer at cursor position
//! @param buf string buffer
//! @param buf_size total buffer size
//! @param len pointer to current string length (updated on success)
//! @param cursor pointer to cursor position (updated on success)
//! @param codepoint Unicode codepoint to insert
//! @return true if inserted, false if buffer full
static inline bool str_insert_codepoint(char* buf, size_t buf_size, size_t* len, size_t* cursor, int32_t codepoint)
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

// #region Text Wrapping

//! Calculate wrap point for chat text
//! @param text text to wrap
//! @param len length of text
//! @param start starting position
//! @param width maximum width
//! @return number of characters that fit, -1 for empty line, 0 for end
int32_t chat_wrap_line(const char* text, size_t len, size_t start, int32_t width);

// #endregion

#endif // DAWN_UTILS_H
