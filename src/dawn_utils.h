// dawn_utils.h

#ifndef DAWN_UTILS_H
#define DAWN_UTILS_H

#include "dawn_types.h"
#include "dawn_md.h"

// #region Character Classification (ASCII)

#define ISIN_(ch, lo, hi)    ((uint8_t)((ch) - (lo)) <= (hi) - (lo))
#define ISALPHA_(ch)         (ISIN_((ch) | 32, 'a', 'z'))
#define ISDIGIT_(ch)         (ISIN_(ch, '0', '9'))
#define ISALNUM_(ch)         (ISALPHA_(ch) || ISDIGIT_(ch))
#define ISUPPER_(ch)         (ISIN_(ch, 'A', 'Z'))
#define ISLOWER_(ch)         (ISIN_(ch, 'a', 'z'))
#define ISXDIGIT_(ch)        (ISDIGIT_(ch) || ISIN_((ch) | 32, 'a', 'f'))
#define ISBLANK_(ch)         ((ch) == ' ' || (ch) == '\t')
#define ISSPACE_(ch)         (ISBLANK_(ch) || ISIN_(ch, '\n', '\r') || (ch) == '\v' || (ch) == '\f')
#define ISPUNCT_(ch)         (ISIN_(ch, 33, 47) || ISIN_(ch, 58, 64) || ISIN_(ch, 91, 96) || ISIN_(ch, 123, 126))
#define TOLOWER_(ch)         (ISUPPER_(ch) ? (ch) | 32 : (ch))
#define TOUPPER_(ch)         (ISLOWER_(ch) ? (ch) & ~32 : (ch))

// #endregion

// #region Path Utilities

//! Get the chat history path for a session file
//! @param session_path path to the .md session file
//! @param chat_path output buffer for chat path
//! @param bufsize size of chat_path buffer
void get_chat_path(const char *session_path, char *chat_path, size_t bufsize);

// #endregion

// #region Text Utilities

//! Normalize line endings in a buffer (CRLF -> LF, standalone CR -> LF)
//! Modifies buffer in-place, returns new length
//! @param buf buffer to normalize
//! @param len length of buffer
//! @return new length after normalization
size_t normalize_line_endings(char *buf, size_t len);

//! Count words in a gap buffer (uses cache for performance)
//! @param gb gap buffer containing text
//! @return number of words
int32_t count_words(const GapBuffer *gb);

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
int32_t output_grapheme(const GapBuffer *gb, size_t *pos, MdStyle active_style);

//! Output a single grapheme from a string, advancing position
//! @param text string to read from
//! @param len length of string
//! @param pos position pointer (advanced past the grapheme)
//! @return display width of the grapheme
int32_t output_grapheme_str(const char *text, size_t len, size_t *pos);

// #endregion

// #region Text Wrapping

//! Calculate wrap point for chat text
//! @param text text to wrap
//! @param len length of text
//! @param start starting position
//! @param width maximum width
//! @return number of characters that fit, -1 for empty line, 0 for end
int32_t chat_wrap_line(const char *text, size_t len, size_t start, int32_t width);

// #endregion

#endif // DAWN_UTILS_H
