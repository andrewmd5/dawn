// dawn_wrap.c
//! Based on algorithms from Avalonia WordWrap

#include "dawn_wrap.h"
#include "dawn_gap.h"

// #region Configuration

WrapConfig wrap_config_default(void) {
    return (WrapConfig){
        .tab_size = WRAP_DEFAULT_TAB_SIZE,
        .trim_whitespace = false,
        .split_words = true,
        .keep_dash_with_word = true,
    };
}

// #endregion

// #region Wrap Result Management

void wrap_init(WrapResult *wr) {
    wr->capacity = 64;
    wr->lines = malloc(sizeof(WrapLine) * wr->capacity);
    wr->count = 0;
    wr->config = wrap_config_default();
    wr->limit = 0;
}

void wrap_free(WrapResult *wr) {
    free(wr->lines);
    wr->lines = NULL;
    wr->count = 0;
    wr->capacity = 0;
}

//! Add a line to wrap result, expanding if needed
static void wrap_add_line(WrapResult *wr, size_t start, size_t end, int32_t width,
                          int32_t segment, bool hard_break, bool ends_split) {
    if (wr->count >= wr->capacity) {
        wr->capacity *= 2;
        wr->lines = realloc(wr->lines, sizeof(WrapLine) * wr->capacity);
    }
    wr->lines[wr->count].start = start;
    wr->lines[wr->count].end = end;
    wr->lines[wr->count].display_width = width;
    wr->lines[wr->count].segment_in_orig = segment;
    wr->lines[wr->count].is_hard_break = hard_break;
    wr->lines[wr->count].ends_with_split = ends_split;
    wr->count++;
}

// #endregion

// #region Codepoint Utilities

//! Get display width of a single Unicode codepoint
static int32_t codepoint_width(utf8proc_int32_t cp) {
    if (cp < 0) return 0;
    if (cp < 32) return 0;  // Control chars

    int32_t w = utf8proc_charwidth(cp);
    // utf8proc returns -1 for control chars, 0 for non-printing, 1 or 2 for others
    if (w < 0) return 0;
    return w;
}

//! Check if codepoint is a word-break character
bool is_break_char(utf8proc_int32_t cp) {
    return cp == ' ' || cp == '\t' || cp == '-';
}

//! Check if codepoint is whitespace (excluding NBSP)
static bool is_whitespace(utf8proc_int32_t cp) {
    if (cp == NBSP) return false;  // Non-breaking space is not breakable whitespace
    return cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' ||
           cp == '\v' || cp == '\f' ||
           cp == 0x0085 ||  // NEL
           cp == 0x2028 ||  // Line separator
           cp == 0x2029;    // Paragraph separator
}

//! Check if codepoint is a newline
static bool is_newline(utf8proc_int32_t cp) {
    return cp == '\n' || cp == '\r' ||
           cp == 0x0085 ||  // NEL
           cp == 0x2028 ||  // Line separator
           cp == 0x2029;    // Paragraph separator
}

// #endregion

// #region Grapheme Utilities

//! Check if grapheme is "wordy" (letter or number)
bool grapheme_is_wordy(const char *text, size_t len) {
    if (len == 0) return false;

    utf8proc_int32_t cp;
    utf8proc_iterate((const utf8proc_uint8_t *)text, len, &cp);
    if (cp < 0) return false;

    utf8proc_category_t cat = utf8proc_category(cp);
    // Letters: Lu, Ll, Lt, Lm, Lo
    // Numbers: Nd, Nl, No
    return (cat >= UTF8PROC_CATEGORY_LU && cat <= UTF8PROC_CATEGORY_LO) ||
           (cat >= UTF8PROC_CATEGORY_ND && cat <= UTF8PROC_CATEGORY_NO);
}

// #endregion

// #region String Grapheme Operations

//! Get the next grapheme cluster boundary in a string
//! @param text string to scan
//! @param len length of string
//! @param pos current position
//! @param state grapheme break state (pass NULL to start, or pointer to maintain state)
//! @return byte offset of next grapheme start, or len if at end
static size_t next_grapheme_stateful(const char *text, size_t len, size_t pos, utf8proc_int32_t *state) {
    if (pos >= len) return len;

    utf8proc_int32_t cp1, cp2;
    utf8proc_ssize_t bytes;

    // Get current codepoint
    bytes = utf8proc_iterate((const utf8proc_uint8_t *)text + pos, len - pos, &cp1);
    if (bytes < 0) return pos + 1;  // Invalid UTF-8, advance one byte

    size_t next = pos + bytes;

    // Keep going while not at grapheme boundary
    while (next < len) {
        bytes = utf8proc_iterate((const utf8proc_uint8_t *)text + next, len - next, &cp2);
        if (bytes < 0) break;

        // Check if there's a grapheme break between cp1 and cp2
        // Use stateful version for proper Unicode 9+ support (emoji ZWJ, regional indicators)
        if (utf8proc_grapheme_break_stateful(cp1, cp2, state)) {
            break;
        }

        cp1 = cp2;
        next += bytes;
    }

    return next;
}

//! Get the next grapheme cluster boundary (convenience wrapper, no state)
static size_t next_grapheme(const char *text, size_t len, size_t pos) {
    utf8proc_int32_t state = 0;
    return next_grapheme_stateful(text, len, pos, &state);
}

//! Get display width of grapheme at position, advancing position
static int32_t grapheme_width_and_advance(const char *text, size_t len, size_t *pos) {
    if (*pos >= len) return 0;

    size_t start = *pos;
    size_t end = next_grapheme(text, len, start);
    *pos = end;

    // Calculate width of all codepoints in grapheme cluster
    // For most cases, only the base character contributes width
    int32_t width = 0;
    size_t p = start;
    bool first = true;

    while (p < end) {
        utf8proc_int32_t cp;
        utf8proc_ssize_t bytes = utf8proc_iterate((const utf8proc_uint8_t *)text + p, end - p, &cp);
        if (bytes < 0) break;

        if (first) {
            // Base character determines width
            width = codepoint_width(cp);
            first = false;
        }
        // Combining characters don't add width

        p += bytes;
    }

    return width;
}

//! Get codepoint at position without advancing
static utf8proc_int32_t codepoint_at(const char *text, size_t len, size_t pos) {
    if (pos >= len) return -1;
    utf8proc_int32_t cp;
    utf8proc_iterate((const utf8proc_uint8_t *)text + pos, len - pos, &cp);
    return cp;
}

// #endregion

// #region Display Width

int32_t utf8_display_width(const char *text, size_t len) {
    int32_t width = 0;
    size_t pos = 0;

    while (pos < len) {
        width += grapheme_width_and_advance(text, len, &pos);
    }

    return width;
}

// #endregion

// #region Word Buffer State

//! State for accumulating a word during wrapping
typedef struct {
    size_t start;           //!< Byte offset where word started
    size_t end;             //!< Byte offset where word ends (exclusive)
    int32_t width;              //!< Display width of word
    bool has_nbsp;          //!< Word contains non-breaking space (don't split)
} WordBuffer;

//! State for the current line being built
typedef struct {
    size_t start;           //!< Byte offset where line started
    int32_t width;              //!< Current display width
    size_t last_break_pos;  //!< Position of last break opportunity
    int32_t width_at_break;     //!< Width at last break opportunity
    int32_t segment;            //!< Segment number within original line
} LineState;

// #endregion

// #region String Wrapping (Enhanced)

int32_t wrap_string_config(const char *text, size_t len, int32_t width, WrapConfig config, WrapResult *out) {
    out->count = 0;
    out->config = config;
    out->limit = width;

    if (len == 0 || width <= 0) {
        wrap_add_line(out, 0, 0, 0, 0, false, false);
        return 1;
    }

    // Need at least width 2 for word splitting with hyphen
    if (width < 2) width = 2;

    LineState line = {0};
    WordBuffer word = {0};
    size_t pos = 0;

    while (pos < len) {
        utf8proc_int32_t cp = codepoint_at(text, len, pos);
        if (cp < 0) {
            pos++;
            continue;
        }

        // Handle newlines (hard breaks)
        if (is_newline(cp)) {
            // Flush word to line
            if (word.width > 0) {
                line.width += word.width;
                word = (WordBuffer){0};
            }

            // Trim trailing whitespace if configured
            size_t end = pos;
            if (config.trim_whitespace && line.width > 0) {
                // Scan back to find last non-whitespace
                size_t scan = pos;
                while (scan > line.start) {
                    size_t prev = scan - 1;
                    while (prev > line.start && (text[prev] & 0xC0) == 0x80) prev--;
                    utf8proc_int32_t prev_cp = codepoint_at(text, len, prev);
                    if (!is_whitespace(prev_cp)) break;
                    scan = prev;
                    line.width--;  // Approximate - could recalculate
                }
                end = scan;
            }

            wrap_add_line(out, line.start, end, line.width, line.segment, true, false);

            pos++;
            // Skip \r\n as single newline
            if (cp == '\r' && pos < len && text[pos] == '\n') pos++;

            line = (LineState){ .start = pos, .segment = 0 };
            continue;
        }

        // Handle tab
        if (cp == '\t') {
            // Flush current word first
            if (word.width > 0) {
                // Check if word fits
                if (line.width + word.width > width && line.width > 0) {
                    wrap_add_line(out, line.start, word.start, line.width, line.segment, false, false);
                    line.segment++;
                    line.start = word.start;
                    line.width = 0;
                }
                line.width += word.width;
                word = (WordBuffer){0};
            }

            // Calculate tab expansion
            int32_t tab_width = config.tab_size - (line.width % config.tab_size);
            if (tab_width == 0) tab_width = config.tab_size;

            // Check if tab fits
            if (line.width + tab_width > width && line.width > 0) {
                wrap_add_line(out, line.start, pos, line.width, line.segment, false, false);
                line.segment++;
                line.start = pos;
                line.width = 0;
                // Recalculate tab from start of line
                tab_width = config.tab_size;
            }

            // Handle leading whitespace trimming
            if (config.trim_whitespace && line.width == 0) {
                line.start = pos + 1;
            } else {
                line.width += tab_width;
                line.last_break_pos = pos + 1;
                line.width_at_break = line.width;
            }

            pos++;
            continue;
        }

        // Handle space
        if (cp == ' ') {
            // Flush current word first
            if (word.width > 0) {
                // Check if word fits
                if (line.width + word.width > width && line.width > 0) {
                    wrap_add_line(out, line.start, word.start, line.width, line.segment, false, false);
                    line.segment++;
                    line.start = word.start;
                    line.width = 0;
                }
                line.width += word.width;
                word = (WordBuffer){0};
            }

            // Handle leading whitespace trimming
            if (config.trim_whitespace && line.width == 0) {
                line.start = pos + 1;
            } else {
                // Check if space fits
                if (line.width + 1 > width) {
                    wrap_add_line(out, line.start, pos, line.width, line.segment, false, false);
                    line.segment++;
                    line.start = pos + 1;
                    line.width = 0;
                } else {
                    line.width += 1;
                    line.last_break_pos = pos + 1;
                    line.width_at_break = line.width;
                }
            }

            pos++;
            continue;
        }

        // Handle non-breaking space (treat as part of word)
        if (cp == NBSP) {
            word.has_nbsp = true;
        }

        // Handle dash - keep with preceding word if configured
        if (cp == '-' && config.keep_dash_with_word) {
            // Add dash to current word
            size_t next_pos = pos;
            int32_t gw = grapheme_width_and_advance(text, len, &next_pos);

            if (word.width == 0) {
                word.start = pos;
            }
            word.end = next_pos;
            word.width += gw;
            pos = next_pos;

            // Dash is a break opportunity after
            if (line.width + word.width <= width) {
                line.width += word.width;
                line.last_break_pos = pos;
                line.width_at_break = line.width;
                word = (WordBuffer){0};
            }
            // else word will be flushed when we hit next char
            continue;
        }

        // Regular grapheme - add to word buffer
        size_t next_pos = pos;
        int32_t gw = grapheme_width_and_advance(text, len, &next_pos);

        if (word.width == 0) {
            word.start = pos;
        }
        word.end = next_pos;
        word.width += gw;
        pos = next_pos;

        // Check if we need to flush the word
        if (line.width + word.width > width) {
            // Word doesn't fit on current line

            if (line.width > 0) {
                // We have content on current line - wrap first
                wrap_add_line(out, line.start, word.start, line.width, line.segment, false, false);
                line.segment++;
                line.start = word.start;
                line.width = 0;
            }

            // Check if word itself is too long (needs splitting)
            if (word.width > width && config.split_words && !word.has_nbsp) {
                // Split the word with hyphen
                // We need to find grapheme boundaries within the word
                size_t word_pos = word.start;
                int32_t accum_width = 0;
                size_t last_grapheme_end = word_pos;
                bool prev_wordy = false;

                while (word_pos < word.end) {
                    size_t grapheme_start = word_pos;
                    size_t grapheme_end = next_grapheme(text, len, word_pos);
                    int32_t gwidth = utf8_display_width(text + grapheme_start, grapheme_end - grapheme_start);
                    bool curr_wordy = grapheme_is_wordy(text + grapheme_start, grapheme_end - grapheme_start);

                    // Would this grapheme + hyphen exceed limit?
                    // Reserve 1 char for hyphen if both sides are wordy
                    int32_t hyphen_width = (prev_wordy && curr_wordy) ? 1 : 0;

                    if (accum_width + gwidth + hyphen_width > width && accum_width > 0) {
                        // Output line up to last_grapheme_end
                        bool needs_hyphen = prev_wordy && curr_wordy;
                        wrap_add_line(out, line.start, last_grapheme_end,
                                     accum_width + (needs_hyphen ? 1 : 0),
                                     line.segment, false, needs_hyphen);
                        line.segment++;
                        line.start = last_grapheme_end;
                        accum_width = 0;
                    }

                    accum_width += gwidth;
                    last_grapheme_end = grapheme_end;
                    prev_wordy = curr_wordy;
                    word_pos = grapheme_end;
                }

                // Update word buffer with remaining portion
                word.start = line.start;
                word.width = accum_width;
                word.end = last_grapheme_end;
                line.width = 0;
            }
        }
    }

    // Flush remaining word and line
    if (word.width > 0) {
        if (line.width + word.width > width && line.width > 0) {
            wrap_add_line(out, line.start, word.start, line.width, line.segment, false, false);
            line.segment++;
            line.start = word.start;
            line.width = 0;
        }
        line.width += word.width;
    }

    // Add final line
    if (line.width > 0 || out->count == 0) {
        size_t end = len;

        // Trim trailing whitespace if configured
        if (config.trim_whitespace && line.width > 0) {
            while (end > line.start) {
                size_t prev = end - 1;
                while (prev > line.start && (text[prev] & 0xC0) == 0x80) prev--;
                utf8proc_int32_t prev_cp = codepoint_at(text, len, prev);
                if (!is_whitespace(prev_cp)) break;
                end = prev;
            }
            line.width = utf8_display_width(text + line.start, end - line.start);
        }

        wrap_add_line(out, line.start, end, line.width, line.segment, false, false);
    }

    return out->count;
}

int32_t wrap_string(const char *text, size_t len, int32_t width, WrapResult *out) {
    return wrap_string_config(text, len, width, wrap_config_default(), out);
}

// #endregion

// #region Gap Buffer Grapheme Operations

//! Get the next grapheme cluster boundary in gap buffer (stateful version)
//! @param gb gap buffer to navigate
//! @param pos current byte position
//! @param state grapheme break state (pass pointer to maintain state across calls)
//! @return byte position of next grapheme start
static size_t gap_grapheme_next_stateful(const GapBuffer *gb, size_t pos, utf8proc_int32_t *state) {
    size_t len = gap_len(gb);
    if (pos >= len) return len;

    utf8proc_int32_t cp1, cp2;
    size_t char_len;

    // Get current codepoint
    cp1 = gap_utf8_at(gb, pos, &char_len);
    if (cp1 < 0) return pos + 1;

    size_t next = pos + char_len;

    while (next < len) {
        cp2 = gap_utf8_at(gb, next, &char_len);
        if (cp2 < 0) break;

        // Use stateful version for proper Unicode 9+ support
        if (utf8proc_grapheme_break_stateful(cp1, cp2, state)) {
            break;
        }

        cp1 = cp2;
        next += char_len;
    }

    return next;
}

size_t gap_grapheme_next(const GapBuffer *gb, size_t pos) {
    utf8proc_int32_t state = 0;
    return gap_grapheme_next_stateful(gb, pos, &state);
}

size_t gap_grapheme_prev(const GapBuffer *gb, size_t pos) {
    if (pos == 0) return 0;

    // Move back one UTF-8 character
    size_t prev = gap_utf8_prev(gb, pos);
    if (prev == 0) return 0;

    // Keep moving back while we're not at a grapheme boundary
    // Note: For backwards iteration, we need to check breaks from start
    // This is more complex with stateful breaks, but we iterate forward
    // from a known start to find the correct boundary
    while (prev > 0) {
        size_t before_prev = gap_utf8_prev(gb, prev);

        size_t dummy;
        utf8proc_int32_t cp1 = gap_utf8_at(gb, before_prev, &dummy);
        utf8proc_int32_t cp2 = gap_utf8_at(gb, prev, &dummy);

        if (cp1 < 0 || cp2 < 0) break;

        // Use stateful version for proper Unicode 9+ support
        utf8proc_int32_t state = 0;
        if (utf8proc_grapheme_break_stateful(cp1, cp2, &state)) {
            break;
        }

        prev = before_prev;
    }

    return prev;
}

int32_t gap_grapheme_width(const GapBuffer *gb, size_t pos, size_t *next_pos) {
    size_t len = gap_len(gb);
    if (pos >= len) {
        if (next_pos) *next_pos = len;
        return 0;
    }

    size_t end = gap_grapheme_next(gb, pos);
    if (next_pos) *next_pos = end;

    // Get width of base character
    size_t char_len;
    utf8proc_int32_t cp = gap_utf8_at(gb, pos, &char_len);

    return codepoint_width(cp);
}

int32_t gap_display_width(const GapBuffer *gb, size_t start, size_t end) {
    int32_t width = 0;
    size_t pos = start;

    while (pos < end) {
        size_t next;
        width += gap_grapheme_width(gb, pos, &next);
        pos = next;
    }

    return width;
}

size_t gap_find_wrap_point(const GapBuffer *gb, size_t start, size_t end, int32_t width, int32_t *out_width) {
    size_t len = gap_len(gb);
    if (start >= len || start >= end) {
        if (out_width) *out_width = 0;
        return start;
    }

    size_t pos = start;
    int32_t cw = 0;
    size_t last_break = start;
    int32_t width_at_break = 0;

    while (pos < end && pos < len) {
        size_t char_len;
        utf8proc_int32_t cp = gap_utf8_at(gb, pos, &char_len);
        if (cp < 0 || cp == '\n') break;

        size_t next_pos;
        int32_t gw = gap_grapheme_width(gb, pos, &next_pos);

        if (cw + gw > width && cw > 0) {
            if (last_break > start && width_at_break > 0) {
                if (out_width) *out_width = width_at_break;
                return last_break;
            }
            if (out_width) *out_width = cw;
            return pos;
        }

        cw += gw;

        if (cp == ' ') {
            last_break = next_pos;
            width_at_break = cw;
        } else if (cp == '-') {
            last_break = next_pos;
            width_at_break = cw;
        }

        pos = next_pos;
    }

    if (out_width) *out_width = cw;
    return pos;
}

// #endregion

// #region Gap Buffer Wrapping (Enhanced)

int32_t wrap_text_config(const GapBuffer *gb, int32_t width, WrapConfig config, WrapResult *out) {
    out->count = 0;
    out->config = config;
    out->limit = width;

    size_t len = gap_len(gb);
    if (len == 0 || width <= 0) {
        wrap_add_line(out, 0, 0, 0, 0, false, false);
        return 1;
    }

    // Need at least width 2 for word splitting with hyphen
    if (width < 2) width = 2;

    LineState line = {0};
    WordBuffer word = {0};
    size_t pos = 0;

    while (pos < len) {
        size_t char_len;
        utf8proc_int32_t cp = gap_utf8_at(gb, pos, &char_len);
        if (cp < 0) {
            pos++;
            continue;
        }

        // Handle newlines (hard breaks)
        if (is_newline(cp)) {
            // Flush word to line
            if (word.width > 0) {
                line.width += word.width;
                word = (WordBuffer){0};
            }

            size_t end = pos;
            // Optionally trim trailing whitespace
            if (config.trim_whitespace && line.width > 0) {
                size_t scan = pos;
                while (scan > line.start) {
                    size_t prev = gap_utf8_prev(gb, scan);
                    utf8proc_int32_t prev_cp = gap_utf8_at(gb, prev, &char_len);
                    if (!is_whitespace(prev_cp)) break;
                    scan = prev;
                }
                end = scan;
                line.width = gap_display_width(gb, line.start, end);
            }

            wrap_add_line(out, line.start, end, line.width, line.segment, true, false);

            pos += char_len;
            // Skip \r\n as single newline
            if (cp == '\r' && pos < len) {
                utf8proc_int32_t next_cp = gap_utf8_at(gb, pos, &char_len);
                if (next_cp == '\n') pos += char_len;
            }

            line = (LineState){ .start = pos, .segment = 0 };
            continue;
        }

        // Handle tab
        if (cp == '\t') {
            if (word.width > 0) {
                if (line.width + word.width > width && line.width > 0) {
                    wrap_add_line(out, line.start, word.start, line.width, line.segment, false, false);
                    line.segment++;
                    line.start = word.start;
                    line.width = 0;
                }
                line.width += word.width;
                word = (WordBuffer){0};
            }

            int32_t tab_width = config.tab_size - (line.width % config.tab_size);
            if (tab_width == 0) tab_width = config.tab_size;

            if (line.width + tab_width > width && line.width > 0) {
                wrap_add_line(out, line.start, pos, line.width, line.segment, false, false);
                line.segment++;
                line.start = pos;
                line.width = 0;
                tab_width = config.tab_size;
            }

            if (config.trim_whitespace && line.width == 0) {
                line.start = pos + char_len;
            } else {
                line.width += tab_width;
                line.last_break_pos = pos + char_len;
                line.width_at_break = line.width;
            }

            pos += char_len;
            continue;
        }

        // Handle space
        if (cp == ' ') {
            if (word.width > 0) {
                if (line.width + word.width > width && line.width > 0) {
                    wrap_add_line(out, line.start, word.start, line.width, line.segment, false, false);
                    line.segment++;
                    line.start = word.start;
                    line.width = 0;
                }
                line.width += word.width;
                word = (WordBuffer){0};
            }

            if (config.trim_whitespace && line.width == 0) {
                line.start = pos + char_len;
            } else {
                if (line.width + 1 > width) {
                    wrap_add_line(out, line.start, pos, line.width, line.segment, false, false);
                    line.segment++;
                    line.start = pos + char_len;
                    line.width = 0;
                } else {
                    line.width += 1;
                    line.last_break_pos = pos + char_len;
                    line.width_at_break = line.width;
                }
            }

            pos += char_len;
            continue;
        }

        // Handle non-breaking space
        if (cp == NBSP) {
            word.has_nbsp = true;
        }

        // Handle dash
        if (cp == '-' && config.keep_dash_with_word) {
            size_t next_pos;
            int32_t gw = gap_grapheme_width(gb, pos, &next_pos);

            if (word.width == 0) {
                word.start = pos;
            }
            word.end = next_pos;
            word.width += gw;
            pos = next_pos;

            if (line.width + word.width <= width) {
                line.width += word.width;
                line.last_break_pos = pos;
                line.width_at_break = line.width;
                word = (WordBuffer){0};
            }
            continue;
        }

        // Regular grapheme
        size_t next_pos;
        int32_t gw = gap_grapheme_width(gb, pos, &next_pos);

        if (word.width == 0) {
            word.start = pos;
        }
        word.end = next_pos;
        word.width += gw;
        pos = next_pos;

        // Check if we need to flush
        if (line.width + word.width > width) {
            if (line.width > 0) {
                wrap_add_line(out, line.start, word.start, line.width, line.segment, false, false);
                line.segment++;
                line.start = word.start;
                line.width = 0;
            }

            // Word splitting for long words
            if (word.width > width && config.split_words && !word.has_nbsp) {
                size_t word_pos = word.start;
                int32_t accum_width = 0;
                size_t last_grapheme_end = word_pos;
                bool prev_wordy = false;

                while (word_pos < word.end) {
                    size_t grapheme_end;
                    int32_t gwidth = gap_grapheme_width(gb, word_pos, &grapheme_end);

                    // Check if current grapheme is wordy
                    size_t dummy;
                    utf8proc_int32_t gcp = gap_utf8_at(gb, word_pos, &dummy);
                    utf8proc_category_t cat = utf8proc_category(gcp);
                    bool curr_wordy = (cat >= UTF8PROC_CATEGORY_LU && cat <= UTF8PROC_CATEGORY_LO) ||
                                     (cat >= UTF8PROC_CATEGORY_ND && cat <= UTF8PROC_CATEGORY_NO);

                    int32_t hyphen_width = (prev_wordy && curr_wordy) ? 1 : 0;

                    if (accum_width + gwidth + hyphen_width > width && accum_width > 0) {
                        bool needs_hyphen = prev_wordy && curr_wordy;
                        wrap_add_line(out, line.start, last_grapheme_end,
                                     accum_width + (needs_hyphen ? 1 : 0),
                                     line.segment, false, needs_hyphen);
                        line.segment++;
                        line.start = last_grapheme_end;
                        accum_width = 0;
                    }

                    accum_width += gwidth;
                    last_grapheme_end = grapheme_end;
                    prev_wordy = curr_wordy;
                    word_pos = grapheme_end;
                }

                word.start = line.start;
                word.width = accum_width;
                word.end = last_grapheme_end;
                line.width = 0;
            }
        }
    }

    // Flush remaining
    if (word.width > 0) {
        if (line.width + word.width > width && line.width > 0) {
            wrap_add_line(out, line.start, word.start, line.width, line.segment, false, false);
            line.segment++;
            line.start = word.start;
            line.width = 0;
        }
        line.width += word.width;
    }

    if (line.width > 0 || out->count == 0) {
        size_t end = len;

        if (config.trim_whitespace && line.width > 0) {
            while (end > line.start) {
                size_t prev = gap_utf8_prev(gb, end);
                size_t dummy;
                utf8proc_int32_t prev_cp = gap_utf8_at(gb, prev, &dummy);
                if (!is_whitespace(prev_cp)) break;
                end = prev;
            }
            line.width = gap_display_width(gb, line.start, end);
        }

        wrap_add_line(out, line.start, end, line.width, line.segment, false, false);
    }

    return out->count;
}

int32_t wrap_text(const GapBuffer *gb, int32_t width, WrapResult *out) {
    return wrap_text_config(gb, width, wrap_config_default(), out);
}

// #endregion

