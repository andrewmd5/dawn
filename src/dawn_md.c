// dawn_md.c

#include "dawn_md.h"
#include "dawn_gap.h"
#include "dawn_theme.h"
#include "dawn_utils.h"
#include "dawn_wrap.h"
#include "dawn_backend.h"
#include <limits.h>

// #region Style Application

void md_apply(MdStyle s) {
    DAWN_BACKEND(app)->reset_attrs();
    set_bg(get_bg());
    set_fg(get_fg());

    // Reset text scale (both integer and fractional)
    current_text_scale = 1;
    current_frac_num = 0;
    current_frac_denom = 0;
    
    bool has_scaling = dawn_ctx_has(&app.ctx, DAWN_CAP_TEXT_SIZING);

    if (s & MD_H1) {
        current_text_scale = 2;
        // No fractional part for H1 - clean 2x
        DAWN_BACKEND(app)->set_bold(true);
        if (!has_scaling) set_fg((DawnColor){0xFF, 0x66, 0x66});  // Bright red (fallback)
        return;
    }
    if (s & MD_H2) {
        current_text_scale = 2;
        current_frac_num = 3;
        current_frac_denom = 4;  // 2 * 3/4 = 1.5x
        DAWN_BACKEND(app)->set_bold(true);
        if (!has_scaling) set_fg((DawnColor){0xFF, 0x99, 0x33});  // Orange (fallback)
        return;
    }
    if (s & MD_H3) {
        current_text_scale = 2;
        current_frac_num = 5;
        current_frac_denom = 8;  // 2 * 5/8 = 1.25x
        DAWN_BACKEND(app)->set_bold(true);
        if (!has_scaling) set_fg((DawnColor){0xFF, 0xCC, 0x00});  // Yellow (fallback)
        return;
    }
    if (s & MD_H4) {
        current_text_scale = 1;
        // No fractional part, normal size
        DAWN_BACKEND(app)->set_bold(true);
        if (!has_scaling) set_fg((DawnColor){0xA0, 0xE0, 0x40});  // Lime green (fallback)
        return;
    }
    if (s & MD_H5) {
        current_text_scale = 1;
        // No fractional part, normal size
        DAWN_BACKEND(app)->set_bold(true);
        if (!has_scaling) set_fg((DawnColor){0x40, 0xD0, 0xD0});  // Cyan (fallback)
        return;
    }
    if (s & MD_H6) {
        current_text_scale = 1;
        // No fractional part, normal size
        DAWN_BACKEND(app)->set_bold(true);
        if (!has_scaling) set_fg((DawnColor){0x70, 0xA0, 0xE0});  // Light blue (fallback)
        return;
    }

    // Marked/highlighted text
    if (s & MD_MARK) {
        extern App app;
        set_bg((DawnColor){0xFF, 0xFF, 0x66});  // Yellow background
        // Use theme-appropriate text color (black for light, dark for dark theme)
        if (app.theme == THEME_LIGHT) {
            set_fg((DawnColor){0x00, 0x00, 0x00});  // Black text
        } else {
            set_fg((DawnColor){0x30, 0x30, 0x30});  // Dark gray text
        }
        return;
    }

    // Subscript - use dim since fractional sizing is complex
    if (s & MD_SUB) {
        DAWN_BACKEND(app)->set_dim(true);
        return;
    }

    // Superscript - use dim since fractional sizing is complex
    if (s & MD_SUP) {
        DAWN_BACKEND(app)->set_dim(true);
        return;
    }

    // Inline code - distinct color with subtle background
    if (s & MD_CODE) {
        extern App app;
        // Slightly different background for code
        if (app.theme == THEME_DARK) {
            set_bg((DawnColor){0x3A, 0x3A, 0x3A});  // Slightly lighter than dark bg
            set_fg((DawnColor){0xE0, 0x6C, 0x75});  // Reddish/pink for inline code
        } else {
            set_bg((DawnColor){0xE8, 0xE8, 0xE8});  // Slightly darker than light bg
            set_fg((DawnColor){0xC0, 0x3C, 0x45});  // Darker red for light theme
        }
        return;
    }

    // Combinable styles
    if (s & MD_BOLD) {
        DAWN_BACKEND(app)->set_bold(true);
        // Make bold text brighter for better visibility
        extern App app;
        if (app.theme == THEME_DARK) {
            set_fg((DawnColor){0xFF, 0xFF, 0xFF});  // Pure white for bold in dark mode
        } else {
            set_fg((DawnColor){0x00, 0x00, 0x00});  // Pure black for bold in light mode
        }
    }
    if (s & MD_ITALIC) { DAWN_BACKEND(app)->set_italic(true); }
    if (s & MD_UNDERLINE) { DAWN_BACKEND(app)->set_underline(DAWN_UNDERLINE_SINGLE); }
    if (s & MD_STRIKE) { DAWN_BACKEND(app)->set_strike(true); }
}

int32_t md_get_scale(MdStyle s) {
    // Return the integer cell scale for headers
    // This is used for cell occupation calculations
    if (s & MD_H1) return 2;  // H1: 2 cells → 2.0x
    if (s & MD_H2) return 2;  // H2: 2 cells, font at 3/4 → 1.5x
    if (s & MD_H3) return 2;  // H3: 2 cells, font at 5/8 → 1.25x
    if (s & MD_H4) return 1;  // H4: 1 cell, normal size
    if (s & MD_H5) return 1;  // H5: 1 cell, normal size
    if (s & MD_H6) return 1;  // H6: 1 cell, normal size
    return 1;
}

MdFracScale md_get_frac_scale(MdStyle s) {
    if (s & MD_H1) return (MdFracScale){2, 0, 0};
    if (s & MD_H2) return (MdFracScale){2, 3, 4};
    if (s & MD_H3) return (MdFracScale){2, 5, 8};
    if (s & MD_H4) return (MdFracScale){1, 0, 0};
    if (s & MD_H5) return (MdFracScale){1, 0, 0};
    if (s & MD_H6) return (MdFracScale){1, 0, 0};
    return (MdFracScale){1, 0, 0};  // Default: no scaling
}

// #endregion

// #region Inline Formatting Detection

MdStyle md_check_delim(const GapBuffer *gb, size_t pos, size_t *dlen) {
    *dlen = 0;
    size_t len = gap_len(gb);
    if (pos >= len) return 0;

    char c = gap_at(gb, pos);

    // Asterisk for bold/italic
    if (c == '*') {
        if (pos + 2 < len && gap_at(gb, pos+1) == '*' && gap_at(gb, pos+2) == '*') {
            *dlen = 3; return MD_BOLD | MD_ITALIC;
        }
        if (pos + 1 < len && gap_at(gb, pos+1) == '*') {
            *dlen = 2; return MD_BOLD;
        }
        *dlen = 1; return MD_ITALIC;
    }

    // Double underscore for underline
    if (c == '_' && pos + 1 < len && gap_at(gb, pos+1) == '_') {
        *dlen = 2; return MD_UNDERLINE;
    }

    // Double tilde for strikethrough
    if (c == '~' && pos + 1 < len && gap_at(gb, pos+1) == '~') {
        *dlen = 2; return MD_STRIKE;
    }

    // Triple equals for underline, double equals for highlight
    if (c == '=' && pos + 1 < len && gap_at(gb, pos+1) == '=') {
        if (pos + 2 < len && gap_at(gb, pos+2) == '=') {
            *dlen = 3; return MD_UNDERLINE;
        }
        *dlen = 2; return MD_MARK;
    }

    // Single tilde for subscript (not double ~~ which is strikethrough)
    if (c == '~') {
        if (pos + 1 < len && gap_at(gb, pos+1) == '~') {
            // This is strikethrough, handled above
            return 0;
        }
        *dlen = 1; return MD_SUB;
    }

    // Single caret for superscript
    if (c == '^') {
        *dlen = 1; return MD_SUP;
    }

    // Backtick for inline code
    if (c == '`') {
        // Don't match if part of triple backticks (``` sequence anywhere)
        // Check if this backtick is part of a ``` sequence
        bool in_triple = false;

        // Check if we're at start of ```
        if (pos + 2 < len && gap_at(gb, pos+1) == '`' && gap_at(gb, pos+2) == '`') {
            in_triple = true;
        }
        // Check if we're the second backtick of ```
        if (pos > 0 && gap_at(gb, pos-1) == '`' && pos + 1 < len && gap_at(gb, pos+1) == '`') {
            in_triple = true;
        }
        // Check if we're the third backtick of ```
        if (pos > 1 && gap_at(gb, pos-1) == '`' && gap_at(gb, pos-2) == '`') {
            in_triple = true;
        }

        if (in_triple) {
            return 0;  // Part of ```, not inline code
        }

        *dlen = 1; return MD_CODE;
    }

    return 0;
}

size_t md_find_closing(const GapBuffer *gb, size_t pos, MdStyle style, size_t dlen) {
    size_t len = gap_len(gb);
    size_t p = pos + dlen;

    // Inline code should not span newlines
    bool allow_newlines = (style != MD_CODE);

    while (p < len) {
        char c = gap_at(gb, p);
        if (c == '\n' && !allow_newlines) return 0;

        size_t check_dlen = 0;
        MdStyle check_style = md_check_delim(gb, p, &check_dlen);

        if (check_style == style && check_dlen == dlen) {
            return p;
        }

        if (check_dlen > 0) {
            p += check_dlen;
        } else {
            p++;
        }
    }
    return 0;
}

MdStyle md_check_header(const GapBuffer *gb, size_t pos) {
    // Must be at start of line
    if (pos != 0 && gap_at(gb, pos - 1) != '\n') return 0;

    size_t len = gap_len(gb);
    size_t p = pos;

    // Skip 0-3 leading spaces or tabs
    int32_t indent = 0;
    while (p < len && indent < 4) {
        char c = gap_at(gb, p);
        if (c == ' ') { indent++; p++; }
        else if (c == '\t') { indent += 4; p++; }  // Tab counts as up to 4 spaces
        else break;
    }
    if (indent >= 4) return 0;  // 4+ spaces = indented code block

    if (p >= len || gap_at(gb, p) != '#') return 0;

    int32_t count = 1;
    p++;

    // Count consecutive # characters (up to 6 for H1-H6)
    while (p < len && gap_at(gb, p) == '#' && count < 6) {
        count++;
        p++;
    }

    // Must be followed by space, tab, or newline
    if (p < len && gap_at(gb, p) != ' ' && gap_at(gb, p) != '\t' && gap_at(gb, p) != '\n') {
        return 0;  // Not a valid header
    }

    switch (count) {
        case 1: return MD_H1;
        case 2: return MD_H2;
        case 3: return MD_H3;
        case 4: return MD_H4;
        case 5: return MD_H5;
        case 6: return MD_H6;
        default: return 0;
    }
}

int32_t md_check_header_content(const GapBuffer *gb, size_t pos, size_t *content_start) {
    // Must be at start of line
    if (pos != 0 && gap_at(gb, pos - 1) != '\n') return 0;

    size_t len = gap_len(gb);
    size_t p = pos;

    // Skip 0-3 leading spaces or tabs
    int32_t indent = 0;
    while (p < len && indent < 4) {
        char c = gap_at(gb, p);
        if (c == ' ') { indent++; p++; }
        else if (c == '\t') { indent += 4; p++; }
        else break;
    }
    if (indent >= 4) return 0;

    if (p >= len || gap_at(gb, p) != '#') return 0;

    int32_t count = 1;
    p++;

    // Count consecutive # characters (up to 6 for H1-H6)
    while (p < len && gap_at(gb, p) == '#' && count < 6) {
        count++;
        p++;
    }

    // Must be followed by space, tab, or newline
    if (p < len && gap_at(gb, p) != ' ' && gap_at(gb, p) != '\t' && gap_at(gb, p) != '\n') {
        return 0;  // Not a valid header
    }

    // Skip the space/tab after #
    while (p < len && (gap_at(gb, p) == ' ' || gap_at(gb, p) == '\t')) {
        p++;
    }

    *content_start = p;
    return count;
}

bool md_check_heading_id(const GapBuffer *gb, size_t pos, MdMatch *result) {
    size_t len = gap_len(gb);
    size_t p = pos;

    // Scan to end of line looking for {#
    while (p < len && gap_at(gb, p) != '\n') {
        if (gap_at(gb, p) == '{' && p + 1 < len && gap_at(gb, p + 1) == '#') {
            // Found {# - now find the closing }
            size_t start = p;
            size_t id_s = p + 2;  // After {#
            p += 2;

            // Scan for closing }
            while (p < len && gap_at(gb, p) != '}' && gap_at(gb, p) != '\n') {
                p++;
            }

            if (p < len && gap_at(gb, p) == '}') {
                // Valid heading ID found
                result->span.start = id_s;
                result->span.len = p - id_s;
                result->total_len = p - start + 1;  // Include closing }
                return true;
            }
            // No closing }, not a valid heading ID
            return false;
        }
        p++;
    }
    return false;
}

// #endregion

// #region Parsing Utilities

//! Parse an integer from gap buffer
//! @param gb gap buffer to read from
//! @param pos starting position
//! @param value output: parsed value
//! @return number of characters consumed
static int32_t parse_int_from_gap(const GapBuffer *gb, size_t pos, int32_t *value) {
    size_t len = gap_len(gb);
    int32_t v = 0;
    int32_t chars = 0;
    while (pos + chars < len) {
        char c = gap_at(gb, pos + chars);
        if (c >= '0' && c <= '9') {
            v = v * 10 + (c - '0');
            chars++;
        } else {
            break;
        }
    }
    if (chars > 0) *value = v;
    return chars;
}

// #endregion

// #region Image Detection

bool md_check_image(const GapBuffer *gb, size_t pos, MdImageAttrs *attrs) {
    size_t len = gap_len(gb);

    // Initialize attrs
    *attrs = (MdImageAttrs){0};

    // Must start with ![
    if (pos + 4 >= len) return false;  // Minimum: ![](x)
    if (gap_at(gb, pos) != '!' || gap_at(gb, pos + 1) != '[') return false;

    // Find closing ]
    size_t p = pos + 2;
    attrs->alt_start = p;
    while (p < len && gap_at(gb, p) != ']' && gap_at(gb, p) != '\n') p++;
    if (p >= len || gap_at(gb, p) != ']') return false;
    attrs->alt_len = p - attrs->alt_start;

    // Must be followed by (
    p++;
    if (p >= len || gap_at(gb, p) != '(') return false;

    // Parse URL (may have optional title after space)
    p++;
    attrs->path_start = p;

    // Find end of URL (space, quote, or closing paren)
    while (p < len && gap_at(gb, p) != ' ' && gap_at(gb, p) != ')' &&
           gap_at(gb, p) != '"' && gap_at(gb, p) != '\n') p++;

    attrs->path_len = p - attrs->path_start;
    if (attrs->path_len == 0) return false;  // Empty path

    // Parse optional title: "title" or 'title'
    while (p < len && gap_at(gb, p) == ' ') p++;
    if (p < len && (gap_at(gb, p) == '"' || gap_at(gb, p) == '\'')) {
        char quote = gap_at(gb, p);
        p++;  // Skip opening quote
        attrs->title_start = p;
        while (p < len && gap_at(gb, p) != quote && gap_at(gb, p) != '\n') p++;
        attrs->title_len = p - attrs->title_start;
        if (p < len && gap_at(gb, p) == quote) p++;  // Skip closing quote
        while (p < len && gap_at(gb, p) == ' ') p++;  // Skip trailing spaces
    }

    // Must end with )
    if (p >= len || gap_at(gb, p) != ')') return false;
    p++;  // Move past )

    // Check for optional { width=250px height=100px } or { width=50% } attributes
    while (p < len && (gap_at(gb, p) == ' ' || gap_at(gb, p) == '\t')) p++;

    if (p < len && gap_at(gb, p) == '{') {
        p++;  // Skip {

        // Parse attributes until }
        while (p < len && gap_at(gb, p) != '}' && gap_at(gb, p) != '\n') {
            // Skip whitespace
            while (p < len && (gap_at(gb, p) == ' ' || gap_at(gb, p) == '\t')) p++;
            if (p >= len || gap_at(gb, p) == '}') break;

            // Check for width=
            if (p + 6 <= len &&
                gap_at(gb, p) == 'w' && gap_at(gb, p+1) == 'i' &&
                gap_at(gb, p+2) == 'd' && gap_at(gb, p+3) == 't' &&
                gap_at(gb, p+4) == 'h' && gap_at(gb, p+5) == '=') {
                p += 6;  // Skip "width="

                // Parse number
                int32_t w = 0;
                int32_t consumed = parse_int_from_gap(gb, p, &w);
                if (consumed > 0) {
                    p += consumed;

                    // Check for % or px
                    if (p < len && gap_at(gb, p) == '%') {
                        attrs->width = -w;  // Negative for percentage
                        p++;
                    } else if (p + 1 < len && gap_at(gb, p) == 'p' && gap_at(gb, p+1) == 'x') {
                        attrs->width = w;  // Positive for pixels
                        p += 2;
                    } else {
                        attrs->width = w;  // Default to pixels
                    }
                }
                continue;
            }

            // Check for height=
            if (p + 7 <= len &&
                gap_at(gb, p) == 'h' && gap_at(gb, p+1) == 'e' &&
                gap_at(gb, p+2) == 'i' && gap_at(gb, p+3) == 'g' &&
                gap_at(gb, p+4) == 'h' && gap_at(gb, p+5) == 't' &&
                gap_at(gb, p+6) == '=') {
                p += 7;  // Skip "height="

                // Parse number
                int32_t h = 0;
                int32_t consumed = parse_int_from_gap(gb, p, &h);
                if (consumed > 0) {
                    p += consumed;

                    // Check for % or px
                    if (p < len && gap_at(gb, p) == '%') {
                        attrs->height = -h;  // Negative for percentage
                        p++;
                    } else if (p + 1 < len && gap_at(gb, p) == 'p' && gap_at(gb, p+1) == 'x') {
                        attrs->height = h;  // Positive for pixels
                        p += 2;
                    } else {
                        attrs->height = h;  // Default to pixels
                    }
                }
                continue;
            }

            // Unknown attribute - skip to next space or }
            while (p < len && gap_at(gb, p) != ' ' && gap_at(gb, p) != '\t' &&
                   gap_at(gb, p) != '}' && gap_at(gb, p) != '\n') p++;
        }

        if (p < len && gap_at(gb, p) == '}') {
            p++;  // Include closing }
        }
    }

    attrs->total_len = p - pos;
    return true;
}

// #endregion

// #region Block Element Detection

bool md_check_code_fence(const GapBuffer *gb, size_t pos, MdSpan *lang) {
    size_t len = gap_len(gb);
    lang->start = 0;
    lang->len = 0;

    // Must be at start of line
    if (pos > 0 && gap_at(gb, pos - 1) != '\n') return false;

    // Skip leading whitespace
    size_t start = pos;
    while (start < len && (gap_at(gb, start) == ' ' || gap_at(gb, start) == '\t')) {
        start++;
    }

    if (start + 2 >= len) return false;

    // Check for ```
    if (gap_at(gb, start) != '`' || gap_at(gb, start + 1) != '`' || gap_at(gb, start + 2) != '`') {
        return false;
    }

    size_t p = start + 3;

    // Optional language identifier
    if (p < len && gap_at(gb, p) != '\n' && gap_at(gb, p) != ' ') {
        lang->start = p;
        while (p < len && gap_at(gb, p) != '\n' && gap_at(gb, p) != ' ') p++;
        lang->len = p - lang->start;
    }

    return true;
}

bool md_check_code_block(const GapBuffer *gb, size_t pos, MdMatch2 *result) {
    size_t len = gap_len(gb);

    // Use local variables
    MdSpan lang = {0, 0};
    size_t content_start = 0, content_len = 0;
    size_t total_len = 0;

    // Fast-path: quick first-character check before expensive parsing
    // Skip leading whitespace and check if first non-space is backtick
    size_t quick = pos;
    while (quick < len) {
        char c = gap_at(gb, quick);
        if (c == '`') break;  // Possible fence
        if (c != ' ' && c != '\t') return false;  // Not a fence
        quick++;
    }
    if (quick + 2 >= len) return false;  // Not enough chars for ```

    // Check for opening fence
    if (!md_check_code_fence(gb, pos, &lang)) return false;

    // Find end of opening fence line (skip leading whitespace first)
    size_t p = pos;
    while (p < len && (gap_at(gb, p) == ' ' || gap_at(gb, p) == '\t')) p++;
    p += 3 + lang.len;  // Skip ``` and language
    while (p < len && gap_at(gb, p) != '\n') p++;
    if (p < len) p++;  // Skip newline

    content_start = p;

    // Find closing fence (allowing leading whitespace)
    while (p < len) {
        // Check if at start of line
        if (p == 0 || gap_at(gb, p - 1) == '\n') {
            // Skip leading whitespace
            size_t close_start = p;
            while (close_start < len && (gap_at(gb, close_start) == ' ' || gap_at(gb, close_start) == '\t')) {
                close_start++;
            }
            // Check for ```
            if (close_start + 2 < len &&
                gap_at(gb, close_start) == '`' && gap_at(gb, close_start + 1) == '`' && gap_at(gb, close_start + 2) == '`') {
                // Found closing fence
                content_len = p - content_start;
                // Skip to end of closing fence line
                size_t end = close_start + 3;
                while (end < len && gap_at(gb, end) != '\n') end++;
                if (end < len) end++;  // Include newline
                total_len = end - pos;

                // Write outputs: [0]=content, [1]=lang
                result->spans[0].start = content_start;
                result->spans[0].len = content_len;
                result->spans[1] = lang;
                result->total_len = total_len;
                return true;
            }
        }
        p++;
    }

    // No closing fence found
    return false;
}

bool md_check_hr(const GapBuffer *gb, size_t pos, size_t *rule_len) {
    size_t len = gap_len(gb);
    *rule_len = 0;

    // Must be at start of line
    if (pos > 0 && gap_at(gb, pos - 1) != '\n') return false;

    size_t p = pos;

    // Skip 0-3 leading spaces or tabs
    int32_t indent = 0;
    while (p < len && indent < 4) {
        char c = gap_at(gb, p);
        if (c == ' ') { indent++; p++; }
        else if (c == '\t') { indent += 4; p++; }
        else break;
    }
    if (indent >= 4) return false;

    if (p + 2 >= len) return false;

    char c = gap_at(gb, p);
    if (c != '-' && c != '*' && c != '_') return false;

    // Count marker characters (can have spaces/tabs between)
    int32_t count = 0;
    while (p < len) {
        char ch = gap_at(gb, p);
        if (ch == c) {
            count++;
            p++;
        } else if (ch == ' ' || ch == '\t') {
            p++;
        } else if (ch == '\n') {
            break;
        } else {
            return false;
        }
    }

    // Need at least 3 marker characters
    if (count < 3) return false;

    *rule_len = p - pos;
    return true;
}

int32_t md_check_setext_underline(const GapBuffer *gb, size_t pos, size_t *underline_len) {
    size_t len = gap_len(gb);
    *underline_len = 0;

    // Must be at start of line
    if (pos > 0 && gap_at(gb, pos - 1) != '\n') return 0;

    size_t p = pos;

    // Skip 0-3 leading spaces
    int32_t indent = 0;
    while (p < len && indent < 4 && gap_at(gb, p) == ' ') {
        indent++;
        p++;
    }
    if (indent >= 4) return 0;

    if (p >= len) return 0;

    char c = gap_at(gb, p);
    if (c != '=' && c != '-') return 0;

    // Count consecutive = or - characters
    int32_t count = 0;
    while (p < len && gap_at(gb, p) == c) {
        count++;
        p++;
    }

    // Must have at least 1 character (spec says 1+)
    if (count < 1) return 0;

    // Rest of line must be only spaces until newline or end
    while (p < len && gap_at(gb, p) == ' ') p++;

    if (p < len && gap_at(gb, p) != '\n') return 0;

    // Include newline in length
    if (p < len && gap_at(gb, p) == '\n') p++;

    *underline_len = p - pos;
    return (c == '=') ? 1 : 2;  // 1 = H1, 2 = H2
}

int32_t md_check_blockquote(const GapBuffer *gb, size_t pos, size_t *content_start) {
    size_t len = gap_len(gb);
    *content_start = pos;

    // Must be at start of line
    if (pos > 0 && gap_at(gb, pos - 1) != '\n') return 0;

    int32_t level = 0;
    size_t p = pos;

    // Count > characters (with optional spaces between)
    while (p < len) {
        while (p < len && gap_at(gb, p) == ' ') p++;

        if (p < len && gap_at(gb, p) == '>') {
            level++;
            p++;
        } else {
            break;
        }
    }

    if (level == 0) return 0;

    // Skip optional space after >
    if (p < len && gap_at(gb, p) == ' ') p++;

    *content_start = p;
    return level;
}

int32_t md_check_list(const GapBuffer *gb, size_t pos, size_t *content_start, int32_t *indent) {
    size_t len = gap_len(gb);
    *content_start = pos;
    *indent = 0;

    // Must be at start of line
    if (pos > 0 && gap_at(gb, pos - 1) != '\n') return 0;

    size_t p = pos;

    // Count leading spaces (for nested lists)
    while (p < len && gap_at(gb, p) == ' ') {
        (*indent)++;
        p++;
    }

    if (p >= len) return 0;

    char c = gap_at(gb, p);

    // Unordered: - * +
    // Marker can be followed by space+content, or newline (empty item), or end of buffer
    if (c == '-' || c == '*' || c == '+') {
        size_t marker_pos = p;
        p++;  // Skip the marker
        if (p >= len || gap_at(gb, p) == '\n') {
            // Empty list item (marker at end of line or buffer)
            *content_start = p;
            return 1;
        }
        if (gap_at(gb, p) == ' ') {
            *content_start = p + 1;
            return 1;
        }
        // Not a list (e.g., "-abc" without space)
        p = marker_pos;  // Reset
    }

    // Ordered: 1. 2. etc (CommonMark: 1-9 digits max)
    if (ISDIGIT_(c)) {
        int32_t digits = 0;
        while (p < len && ISDIGIT_(gap_at(gb, p)) && digits < 10) {
            digits++;
            p++;
        }
        // CommonMark specifies max 9 digits
        if (digits >= 1 && digits <= 9 && p < len &&
            (gap_at(gb, p) == '.' || gap_at(gb, p) == ')')) {
            p++;  // Skip the . or )
            if (p >= len || gap_at(gb, p) == '\n') {
                // Empty ordered list item
                *content_start = p;
                return 2;
            }
            if (gap_at(gb, p) == ' ') {
                *content_start = p + 1;
                return 2;
            }
        }
    }

    return 0;
}

int32_t md_check_task(const GapBuffer *gb, size_t pos, size_t *content_start, int32_t *indent) {
    size_t len = gap_len(gb);
    *content_start = pos;
    *indent = 0;

    // Must be at start of line
    if (pos > 0 && gap_at(gb, pos - 1) != '\n') return 0;

    size_t p = pos;

    // Count leading spaces
    while (p < len && gap_at(gb, p) == ' ') {
        (*indent)++;
        p++;
    }

    // Must have - [ ] or - [x]
    if (p + 5 > len) return 0;
    if (gap_at(gb, p) != '-') return 0;
    if (gap_at(gb, p + 1) != ' ') return 0;
    if (gap_at(gb, p + 2) != '[') return 0;

    char check = gap_at(gb, p + 3);
    if (gap_at(gb, p + 4) != ']') return 0;

    // Space after ]
    if (p + 5 < len && gap_at(gb, p + 5) == ' ') {
        *content_start = p + 6;
    } else {
        *content_start = p + 5;
    }

    if (check == ' ') return 1;  // Unchecked
    if (check == 'x' || check == 'X') return 2;  // Checked

    return 0;
}

// #endregion

// #region Link Detection

bool md_check_link(const GapBuffer *gb, size_t pos, MdMatch2 *result) {
    size_t len = gap_len(gb);

    // Must start with [
    if (pos >= len || gap_at(gb, pos) != '[') return false;

    // Don't match image syntax ![
    if (pos > 0 && gap_at(gb, pos - 1) == '!') return false;

    // Find closing ]
    size_t p = pos + 1;
    size_t t_start = p;
    while (p < len && gap_at(gb, p) != ']' && gap_at(gb, p) != '\n') p++;
    if (p >= len || gap_at(gb, p) != ']') return false;
    size_t t_len = p - t_start;

    // Must be followed by (
    p++;
    if (p >= len || gap_at(gb, p) != '(') return false;

    // Find closing )
    p++;
    size_t u_start = p;
    while (p < len && gap_at(gb, p) != ')' && gap_at(gb, p) != '\n') p++;
    if (p >= len || gap_at(gb, p) != ')') return false;
    size_t u_len = p - u_start;

    p++; // Include )

    // [0]=text, [1]=url
    result->spans[0].start = t_start;
    result->spans[0].len = t_len;
    result->spans[1].start = u_start;
    result->spans[1].len = u_len;
    result->total_len = p - pos;
    return true;
}

// #endregion

// #region Footnote Detection

bool md_check_footnote_ref(const GapBuffer *gb, size_t pos, MdMatch *result) {
    size_t len = gap_len(gb);

    if (pos + 3 >= len) return false;
    if (gap_at(gb, pos) != '[' || gap_at(gb, pos + 1) != '^') return false;

    size_t p = pos + 2;
    size_t i_start = p;

    // ID can be alphanumeric and -_
    while (p < len) {
        char c = gap_at(gb, p);
        if (c == ']') break;
        if (c == '\n' || c == ' ') return false;
        p++;
    }

    if (p >= len || gap_at(gb, p) != ']') return false;
    if (p == i_start) return false;  // Empty ID

    // Make sure it's not a definition (no : after ])
    if (p + 1 < len && gap_at(gb, p + 1) == ':') return false;

    result->span.start = i_start;
    result->span.len = p - i_start;
    result->total_len = p + 1 - pos;
    return true;
}

bool md_check_footnote_def(const GapBuffer *gb, size_t pos, MdMatch2 *result) {
    size_t len = gap_len(gb);

    // Must be at start of line
    if (pos > 0 && gap_at(gb, pos - 1) != '\n') return false;

    if (pos + 4 >= len) return false;
    if (gap_at(gb, pos) != '[' || gap_at(gb, pos + 1) != '^') return false;

    size_t p = pos + 2;
    size_t i_start = p;

    while (p < len) {
        char c = gap_at(gb, p);
        if (c == ']') break;
        if (c == '\n' || c == ' ') return false;
        p++;
    }

    if (p >= len || gap_at(gb, p) != ']') return false;
    if (p == i_start) return false;

    size_t i_len = p - i_start;
    p++;  // Skip ]

    // Must have :
    if (p >= len || gap_at(gb, p) != ':') return false;
    p++;

    // Skip optional space
    if (p < len && gap_at(gb, p) == ' ') p++;

    // [0]=id, [1]=content (start only)
    result->spans[0].start = i_start;
    result->spans[0].len = i_len;
    result->spans[1].start = p;
    result->spans[1].len = 0;  // Content length not tracked

    // Total length to end of line
    while (p < len && gap_at(gb, p) != '\n') p++;
    result->total_len = p - pos;

    return true;
}

// #endregion

// #region LaTeX Math Detection

bool md_check_inline_math(const GapBuffer *gb, size_t pos, MdMatch *result) {
    size_t len = gap_len(gb);

    if (pos >= len) return false;
    char c = gap_at(gb, pos);

    // Check for $`math`$ (GitHub-flavored)
    if (c == '$' && pos + 1 < len && gap_at(gb, pos + 1) == '`') {
        // Check for escaped \$ - don't parse as math
        if (pos > 0 && gap_at(gb, pos - 1) == '\\') return false;
        size_t p = pos + 2;
        size_t c_start = p;

        // Find closing `$
        while (p + 1 < len) {
            if (gap_at(gb, p) == '\n') return false;
            if (gap_at(gb, p) == '`' && gap_at(gb, p + 1) == '$') {
                result->span.start = c_start;
                result->span.len = p - c_start;
                result->total_len = p + 2 - pos;
                return true;
            }
            p++;
        }
        return false;
    }

    // Check for $math$
    if (c == '$') {
        // Make sure it's not $$
        if (pos + 1 < len && gap_at(gb, pos + 1) == '$') return false;

        // Check for escaped \$ - don't parse as math
        if (pos > 0 && gap_at(gb, pos - 1) == '\\') return false;

        size_t p = pos + 1;
        size_t c_start = p;

        // Find closing $ (but skip escaped \$)
        while (p < len) {
            char ch = gap_at(gb, p);
            if (ch == '\n') return false;  // Inline math can't span lines
            if (ch == '\\' && p + 1 < len) {
                p += 2;  // Skip escaped character
                continue;
            }
            if (ch == '$') {
                result->span.start = c_start;
                result->span.len = p - c_start;
                result->total_len = p + 1 - pos;
                return true;
            }
            p++;
        }
        return false;
    }

    // Check for \(math\)
    if (c == '\\' && pos + 1 < len && gap_at(gb, pos + 1) == '(') {
        size_t p = pos + 2;
        size_t c_start = p;

        // Find closing \)
        while (p + 1 < len) {
            if (gap_at(gb, p) == '\n') return false;
            if (gap_at(gb, p) == '\\' && gap_at(gb, p + 1) == ')') {
                result->span.start = c_start;
                result->span.len = p - c_start;
                result->total_len = p + 2 - pos;
                return true;
            }
            p++;
        }
        return false;
    }

    return false;
}

bool md_check_block_math(const GapBuffer *gb, size_t pos, MdMatch *result) {
    size_t len = gap_len(gb);

    // Must be at start of line
    if (pos > 0 && gap_at(gb, pos - 1) != '\n') return false;

    if (pos + 1 >= len) return false;
    char c = gap_at(gb, pos);
    char c1 = gap_at(gb, pos + 1);

    // Check for $$
    if (c == '$' && c1 == '$') {
        result->span.start = pos + 2;
        result->span.len = 0;
        // Skip to end of line
        size_t p = pos + 2;
        while (p < len && gap_at(gb, p) != '\n') p++;
        result->total_len = p - pos;
        return true;
    }

    // Check for \[
    if (c == '\\' && c1 == '[') {
        result->span.start = pos + 2;
        result->span.len = 0;
        size_t p = pos + 2;
        while (p < len && gap_at(gb, p) != '\n') p++;
        result->total_len = p - pos;
        return true;
    }

    return false;
}

bool md_check_block_math_full(const GapBuffer *gb, size_t pos, MdMatch *result) {
    size_t len = gap_len(gb);

    // Must be at start of line
    if (pos > 0 && gap_at(gb, pos - 1) != '\n') return false;

    // Skip leading whitespace
    size_t start = pos;
    while (start < len && (gap_at(gb, start) == ' ' || gap_at(gb, start) == '\t')) {
        start++;
    }

    if (start + 1 >= len) return false;
    char c = gap_at(gb, start);
    char c1 = gap_at(gb, start + 1);

    // Check for $$...$$ (possibly multi-line or single-line)
    if (c == '$' && c1 == '$') {
        size_t c_start = start + 2;

        // Check if content is on same line (single-line format: $$content$$)
        size_t p = c_start;
        while (p + 1 < len && gap_at(gb, p) != '\n') {
            if (gap_at(gb, p) == '$' && gap_at(gb, p + 1) == '$') {
                // Found closing $$ on same line
                result->span.start = c_start;
                result->span.len = p - c_start;
                size_t close_end = p + 2;
                while (close_end < len && gap_at(gb, close_end) != '\n') close_end++;
                if (close_end < len) close_end++;
                result->total_len = close_end - pos;
                return true;
            }
            p++;
        }

        // Multi-line format: $$\ncontent\n$$
        // Skip to newline after opening $$
        while (c_start < len && gap_at(gb, c_start) != '\n') c_start++;
        if (c_start < len) c_start++; // skip the newline

        // Find closing $$ at start of line (allowing leading whitespace)
        p = c_start;
        while (p < len) {
            // Check if at start of line
            if (p == 0 || gap_at(gb, p - 1) == '\n') {
                // Skip leading whitespace
                size_t close_start = p;
                while (close_start < len && (gap_at(gb, close_start) == ' ' || gap_at(gb, close_start) == '\t')) {
                    close_start++;
                }
                if (close_start + 1 < len &&
                    gap_at(gb, close_start) == '$' && gap_at(gb, close_start + 1) == '$') {
                    size_t close_end = close_start + 2;
                    while (close_end < len && gap_at(gb, close_end) != '\n') close_end++;
                    if (close_end < len) close_end++;

                    result->span.start = c_start;
                    result->span.len = p - c_start;
                    if (result->span.len > 0 && gap_at(gb, p - 1) == '\n') {
                        result->span.len--;
                    }
                    result->total_len = close_end - pos;
                    return true;
                }
            }
            p++;
        }
        return false;
    }

    // Check for \[...\]
    if (c == '\\' && c1 == '[') {
        size_t c_start = pos + 2;
        // Skip whitespace
        while (c_start < len && (gap_at(gb, c_start) == ' ' || gap_at(gb, c_start) == '\t')) {
            c_start++;
        }
        if (c_start < len && gap_at(gb, c_start) == '\n') c_start++;

        // Find closing \]
        size_t p = c_start;
        while (p + 1 < len) {
            if (gap_at(gb, p) == '\\' && gap_at(gb, p + 1) == ']') {
                result->span.start = c_start;
                result->span.len = p - c_start;
                // Skip to end of line after \]
                size_t close_end = p + 2;
                while (close_end < len && gap_at(gb, close_end) != '\n') close_end++;
                if (close_end < len) close_end++;
                result->total_len = close_end - pos;
                return true;
            }
            p++;
        }
        return false;
    }

    return false;
}

// #endregion

// #region Table Detection

//! Check for table delimiter line: |---|---| or |:---|---:| etc.
bool md_check_table_delimiter(const GapBuffer *gb, size_t pos,
                              int32_t *col_count, MdAlign *align, size_t *line_len) {
    size_t len = gap_len(gb);
    *col_count = 0;
    *line_len = 0;

    // Must be at start of line
    if (pos > 0 && gap_at(gb, pos - 1) != '\n') return false;
    if (pos >= len) return false;

    size_t p = pos;

    // Skip leading whitespace
    while (p < len && (gap_at(gb, p) == ' ' || gap_at(gb, p) == '\t')) p++;

    // Must start with | (optional leading pipe)
    bool has_leading_pipe = false;
    if (p < len && gap_at(gb, p) == '|') {
        has_leading_pipe = true;
        p++;
    }

    int32_t cols = 0;

    while (p < len && cols < MD_TABLE_MAX_COLS) {
        // Skip whitespace before cell
        while (p < len && gap_at(gb, p) == ' ') p++;

        if (p >= len || gap_at(gb, p) == '\n') break;

        // Parse alignment specification: optional ':' + dashes + optional ':'
        bool left_colon = false;
        bool right_colon = false;
        int32_t dash_count = 0;

        // Check for left colon
        if (p < len && gap_at(gb, p) == ':') {
            left_colon = true;
            p++;
        }

        // Count dashes (minimum 1 required, typically 3+)
        while (p < len && gap_at(gb, p) == '-') {
            dash_count++;
            p++;
        }

        if (dash_count == 0) {
            // Not a valid delimiter cell
            return false;
        }

        // Check for right colon
        if (p < len && gap_at(gb, p) == ':') {
            right_colon = true;
            p++;
        }

        // Skip whitespace after cell
        while (p < len && gap_at(gb, p) == ' ') p++;

        // Determine alignment from colons
        // :--- = left, ---: = right, :---: = center, --- = default
        int32_t align_index = (left_colon ? 1 : 0) | (right_colon ? 2 : 0);
        static const MdAlign align_map[] = {
            MD_ALIGN_DEFAULT,  // 0: no colons
            MD_ALIGN_LEFT,     // 1: left colon only
            MD_ALIGN_RIGHT,    // 2: right colon only
            MD_ALIGN_CENTER    // 3: both colons
        };
        align[cols] = align_map[align_index];
        cols++;

        // Expect pipe or end of line
        if (p < len && gap_at(gb, p) == '|') {
            p++;
            // Check if this is the trailing pipe before newline
            size_t check = p;
            while (check < len && gap_at(gb, check) == ' ') check++;
            if (check >= len || gap_at(gb, check) == '\n') {
                // Trailing pipe, end here
                p = check;
                break;
            }
            // More cells to come
        } else if (p < len && gap_at(gb, p) == '\n') {
            break;
        } else if (p >= len) {
            break;
        } else if (!has_leading_pipe) {
            // Without leading pipe, we might have cells without pipe separators
            // but this is unusual; require pipes
            return false;
        }
    }

    // Must have at least 1 column
    if (cols == 0) return false;

    // Calculate line length (to newline or end)
    size_t end = p;
    while (end < len && gap_at(gb, end) != '\n') end++;
    *line_len = end - pos;
    if (end < len) (*line_len)++;  // Include newline

    *col_count = cols;
    return true;
}

//! Check for table header/body row line: | cell | cell |
bool md_check_table_header(const GapBuffer *gb, size_t pos,
                           int32_t *col_count, size_t *line_len) {
    size_t len = gap_len(gb);
    *col_count = 0;
    *line_len = 0;

    // Must be at start of line
    if (pos > 0 && gap_at(gb, pos - 1) != '\n') return false;
    if (pos >= len) return false;

    size_t p = pos;

    // Skip leading whitespace
    while (p < len && (gap_at(gb, p) == ' ' || gap_at(gb, p) == '\t')) p++;

    // Must have a pipe somewhere in the line for it to be a table row
    bool has_pipe = false;
    size_t scan = p;
    while (scan < len && gap_at(gb, scan) != '\n') {
        if (gap_at(gb, scan) == '|') {
            has_pipe = true;
            break;
        }
        scan++;
    }
    if (!has_pipe) return false;

    // Count columns by counting pipes (excluding leading/trailing)
    int32_t pipes = 0;
    size_t end = p;

    while (end < len && gap_at(gb, end) != '\n') {
        if (gap_at(gb, end) == '|') {
            pipes++;
        }
        end++;
    }

    // Check for leading and trailing pipes
    p = pos;
    while (p < len && (gap_at(gb, p) == ' ' || gap_at(gb, p) == '\t')) p++;

    bool leading_pipe = (p < len && gap_at(gb, p) == '|');
    bool trailing_pipe = false;

    // Check if last non-space is a pipe
    size_t check_trailing = end;
    while (check_trailing > p && gap_at(gb, check_trailing - 1) == ' ') {
        check_trailing--;
    }
    if (check_trailing > p && gap_at(gb, check_trailing - 1) == '|') {
        trailing_pipe = true;
    }

    // Calculate columns
    int32_t cols;
    if (leading_pipe && trailing_pipe) {
        cols = pipes - 1;  // | a | b | c | has 4 pipes, 3 cols
    } else if (leading_pipe || trailing_pipe) {
        cols = pipes;      // | a | b | c or a | b | c | has pipes equal to cols
    } else {
        cols = pipes + 1;  // a | b | c has 2 pipes, 3 cols
    }

    if (cols < 1) return false;

    *col_count = cols;
    *line_len = end - pos;
    if (end < len) (*line_len)++;  // Include newline

    return true;
}

//! Check for complete table
bool md_check_table(const GapBuffer *gb, size_t pos, MdTable *table) {
    size_t len = gap_len(gb);

    // Must be at start of line
    if (pos > 0 && gap_at(gb, pos - 1) != '\n') return false;

    // Initialize output
    table->col_count = 0;
    table->row_count = 0;
    table->total_len = 0;
    for (int32_t i = 0; i < MD_TABLE_MAX_COLS; i++) {
        table->align[i] = MD_ALIGN_DEFAULT;
    }

    // Check for header row
    int32_t header_cols = 0;
    size_t header_len = 0;
    if (!md_check_table_header(gb, pos, &header_cols, &header_len)) {
        return false;
    }

    // Check for delimiter row immediately after header
    size_t delim_pos = pos + header_len;
    if (delim_pos >= len) return false;

    int32_t delim_cols = 0;
    MdAlign delim_align[MD_TABLE_MAX_COLS];
    size_t delim_len = 0;
    if (!md_check_table_delimiter(gb, delim_pos, &delim_cols, delim_align, &delim_len)) {
        return false;
    }

    // Column counts must match
    if (header_cols != delim_cols) return false;

    table->col_count = delim_cols;
    for (int32_t i = 0; i < delim_cols && i < MD_TABLE_MAX_COLS; i++) {
        table->align[i] = delim_align[i];
    }

    // Count body rows
    size_t p = delim_pos + delim_len;
    int32_t body_rows = 0;

    while (p < len) {
        int32_t row_cols = 0;
        size_t row_len = 0;
        if (!md_check_table_header(gb, p, &row_cols, &row_len)) {
            break;  // Not a valid table row, end of table
        }
        // Row must have same or fewer columns (extra columns ignored)
        body_rows++;
        p += row_len;
    }

    table->row_count = 1 + body_rows;  // Header + body rows
    table->total_len = p - pos;

    return true;
}

//! Parse a table row into cell boundaries
int32_t md_parse_table_row(const GapBuffer *gb, size_t pos, size_t line_len,
                       uint32_t *cell_starts, uint16_t *cell_lens, int32_t max_cells) {
    size_t len = gap_len(gb);
    size_t end = pos + line_len;
    if (end > len) end = len;

    // Skip trailing newline from line_len
    while (end > pos && gap_at(gb, end - 1) == '\n') end--;

    size_t p = pos;
    int32_t cells = 0;

    // Skip leading whitespace
    while (p < end && (gap_at(gb, p) == ' ' || gap_at(gb, p) == '\t')) p++;

    // Skip leading pipe if present
    if (p < end && gap_at(gb, p) == '|') p++;

    while (p < end && cells < max_cells) {
        // Skip whitespace at start of cell
        while (p < end && gap_at(gb, p) == ' ') p++;

        size_t cell_start = p;

        // Find end of cell (next pipe or end of line)
        while (p < end && gap_at(gb, p) != '|') p++;

        size_t cell_end = p;

        // Trim trailing whitespace from cell
        while (cell_end > cell_start && gap_at(gb, cell_end - 1) == ' ') {
            cell_end--;
        }

        // Skip the empty cell after trailing pipe
        if (cell_start == cell_end && p >= end) break;

        cell_starts[cells] = (uint32_t)cell_start;
        cell_lens[cells] = (uint16_t)(cell_end - cell_start);
        cells++;

        // Skip pipe
        if (p < end && gap_at(gb, p) == '|') p++;
    }

    return cells;
}

//! Get display width of table cell content
int32_t md_table_cell_width(const GapBuffer *gb, size_t start, size_t len) {
    int32_t width = 0;
    size_t p = start;
    size_t end = start + len;
    size_t gb_len = gap_len(gb);
    if (end > gb_len) end = gb_len;

    while (p < end) {
        size_t next_pos;
        int32_t gw = gap_grapheme_width(gb, p, &next_pos);
        if (next_pos > end) break;
        width += gw;
        p = next_pos;
    }

    return width;
}

// #endregion

// #region Autolinks

// Check if character is valid in URI (excluding < > and space)
static bool is_uri_char(char c) {
    // URI chars: anything except ASCII control, space, <, >
    if (c < 0x21) return false;  // Control chars and space
    if (c == '<' || c == '>') return false;
    return true;
}

// Check if character is valid in email local part
static bool is_email_local_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') ||
           c == '.' || c == '!' || c == '#' || c == '$' || c == '%' ||
           c == '&' || c == '\'' || c == '*' || c == '+' || c == '/' ||
           c == '=' || c == '?' || c == '^' || c == '_' || c == '`' ||
           c == '{' || c == '|' || c == '}' || c == '~' || c == '-';
}

// Check if character is valid in email domain
static bool is_email_domain_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '.';
}

bool md_check_autolink(const GapBuffer *gb, size_t pos, MdAutolink *result) {
    size_t len = gap_len(gb);
    if (pos >= len || gap_at(gb, pos) != '<') return false;

    size_t p = pos + 1;
    if (p >= len) return false;

    // Check for URI scheme or email
    // URI: scheme (2-32 chars starting with letter) followed by :
    // Email: local@domain

    size_t content_start = p;

    // Try to detect scheme (letter followed by letters/digits/+/./-)
    char first = gap_at(gb, p);
    if ((first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z')) {
        // Potential URI scheme
        size_t scheme_start = p;
        p++;
        while (p < len && p - scheme_start < 32) {
            char c = gap_at(gb, p);
            if (c == ':') {
                // Found scheme separator
                size_t scheme_len = p - scheme_start;
                if (scheme_len >= 2) {
                    // Valid scheme, now scan for >
                    p++;  // skip :
                    while (p < len) {
                        char c2 = gap_at(gb, p);
                        if (c2 == '>') {
                            // Found end
                            result->span.start = content_start;
                            result->span.len = p - content_start;
                            result->total_len = p - pos + 1;
                            result->is_email = false;
                            return true;
                        }
                        if (!is_uri_char(c2)) break;
                        p++;
                    }
                }
                break;
            }
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '+' || c == '.' || c == '-')) {
                break;
            }
            p++;
        }
    }

    // Try email: local-part@domain
    p = content_start;
    size_t local_start = p;

    // Scan local part
    while (p < len && is_email_local_char(gap_at(gb, p))) {
        p++;
    }

    if (p == local_start || p >= len) return false;  // Empty local part
    if (gap_at(gb, p) != '@') return false;

    p++;  // skip @
    if (p >= len) return false;

    // Domain must start with alphanumeric
    first = gap_at(gb, p);
    if (!((first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z') ||
          (first >= '0' && first <= '9'))) {
        return false;
    }

    // Scan domain
    size_t domain_start = p;
    while (p < len && is_email_domain_char(gap_at(gb, p))) {
        p++;
    }

    // Domain must end with alphanumeric (not . or -)
    if (p == domain_start) return false;
    char last = gap_at(gb, p - 1);
    if (last == '.' || last == '-') return false;

    // Must have at least one dot in domain
    bool has_dot = false;
    for (size_t i = domain_start; i < p; i++) {
        if (gap_at(gb, i) == '.') {
            has_dot = true;
            break;
        }
    }
    if (!has_dot) return false;

    // Must end with >
    if (p >= len || gap_at(gb, p) != '>') return false;

    result->span.start = content_start;
    result->span.len = p - content_start;
    result->total_len = p - pos + 1;
    result->is_email = true;
    return true;
}

// #endregion

// #region HTML Entity References

#include "html_entities.h"

int32_t md_check_entity(const GapBuffer *gb, size_t pos, char *utf8_out, size_t *total_len) {
    size_t len = gap_len(gb);
    if (pos >= len || gap_at(gb, pos) != '&') return 0;

    // Look for semicolon within reasonable distance (max entity name is ~32 chars)
    size_t max_end = pos + 40;
    if (max_end > len) max_end = len;

    size_t p = pos + 1;
    if (p >= len) return 0;

    // Numeric reference: &#...;
    if (gap_at(gb, p) == '#') {
        p++;
        if (p >= len) return 0;

        // Extract the numeric part into a temporary buffer
        char buf[16];
        size_t buf_len = 0;
        while (p < max_end && buf_len < sizeof(buf) - 1) {
            char c = gap_at(gb, p);
            buf[buf_len++] = c;
            if (c == ';') break;
            p++;
        }

        if (buf_len == 0 || buf[buf_len - 1] != ';') return 0;

        size_t consumed;
        int32_t utf8_len = entity_decode_numeric(buf, buf_len, utf8_out, &consumed);
        if (utf8_len > 0) {
            *total_len = 2 + consumed;  // & + # + consumed chars
            return utf8_len;
        }
        return 0;
    }

    // Named reference: &name;
    // Entity names are alphanumeric
    size_t name_start = p;
    while (p < max_end) {
        char c = gap_at(gb, p);
        if (c == ';') break;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9'))) {
            return 0;  // Invalid character in entity name
        }
        p++;
    }

    if (p >= max_end || gap_at(gb, p) != ';') return 0;

    size_t name_len = p - name_start;
    if (name_len == 0 || name_len > 32) return 0;

    // Extract name for lookup
    char name[33];
    for (size_t i = 0; i < name_len; i++) {
        name[i] = gap_at(gb, name_start + i);
    }

    const char *utf8 = entity_lookup(name, name_len);
    if (utf8) {
        size_t utf8_len = strlen(utf8);
        memcpy(utf8_out, utf8, utf8_len);
        *total_len = name_len + 2;  // & + name + ;
        return (int32_t)utf8_len;
    }

    return 0;
}

// #endregion

// #region Typographic Replacements

// Check for typographic replacements at position
// Returns UTF-8 replacement string and number of source chars consumed
// Skips replacement inside inline code (MD_CODE)
const char *md_check_typo_replacement(const GapBuffer *gb, size_t pos, size_t *consumed, MdStyle active_style) {
    // Skip typo replacement inside inline code
    if (active_style & MD_CODE) return NULL;

    size_t len = gap_len(gb);
    if (pos >= len) return NULL;

    char c = gap_at(gb, pos);

    // Three-char sequences
    if (pos + 2 < len) {
        char c1 = gap_at(gb, pos + 1);
        char c2 = gap_at(gb, pos + 2);

        // --- → em dash (—)
        if (c == '-' && c1 == '-' && c2 == '-') {
            *consumed = 3;
            return "—";
        }

        // ... → ellipsis (…)
        if (c == '.' && c1 == '.' && c2 == '.') {
            *consumed = 3;
            return "…";
        }

        // (c) or (C) → copyright (©)
        if (c == '(' && (c1 == 'c' || c1 == 'C') && c2 == ')') {
            *consumed = 3;
            return "©";
        }

        // (r) or (R) → registered (®)
        if (c == '(' && (c1 == 'r' || c1 == 'R') && c2 == ')') {
            *consumed = 3;
            return "®";
        }

        // (p) or (P) → paragraph (§)
        if (c == '(' && (c1 == 'p' || c1 == 'P') && c2 == ')') {
            *consumed = 3;
            return "§";
        }
    }

    // Four-char sequences
    if (pos + 3 < len) {
        char c1 = gap_at(gb, pos + 1);
        char c2 = gap_at(gb, pos + 2);
        char c3 = gap_at(gb, pos + 3);

        // (tm) or (TM) → trademark (™)
        if (c == '(' && (c1 == 't' || c1 == 'T') &&
            (c2 == 'm' || c2 == 'M') && c3 == ')') {
            *consumed = 4;
            return "™";
        }
    }

    // Two-char sequences
    if (pos + 1 < len) {
        char c1 = gap_at(gb, pos + 1);

        // -- → en dash (–)
        // But NOT if followed by another - (that's ---)
        if (c == '-' && c1 == '-') {
            if (pos + 2 < len && gap_at(gb, pos + 2) != '-') {
                *consumed = 2;
                return "–";
            }
        }

        // +- → plus-minus (±)
        if (c == '+' && c1 == '-') {
            *consumed = 2;
            return "±";
        }

        // << → left guillemet («)
        if (c == '<' && c1 == '<') {
            *consumed = 2;
            return "«";
        }

        // >> → right guillemet (»)
        if (c == '>' && c1 == '>') {
            *consumed = 2;
            return "»";
        }
    }

    return NULL;
}

// #endregion

// #region Emoji Shortcodes

#include "emoji_shortcodes.h"

const char *md_check_emoji(const GapBuffer *gb, size_t pos, MdMatch *result) {
    size_t len = gap_len(gb);
    if (pos >= len || gap_at(gb, pos) != ':') return NULL;

    // Look for closing colon
    size_t p = pos + 1;
    size_t start = p;

    // Shortcode must start with alphanumeric
    if (p >= len) return NULL;
    char first = gap_at(gb, p);
    if (!((first >= 'a' && first <= 'z') ||
          (first >= 'A' && first <= 'Z') ||
          (first >= '0' && first <= '9') ||
          first == '+' || first == '-')) return NULL;

    // Scan for closing colon (shortcodes contain alphanumeric, _, -)
    while (p < len) {
        char c = gap_at(gb, p);
        if (c == ':') {
            // Found closing colon
            size_t sc_len = p - start;
            if (sc_len == 0 || sc_len > 64) return NULL;  // Sanity check

            // Extract shortcode string
            char shortcode[65];
            for (size_t i = 0; i < sc_len; i++) {
                shortcode[i] = gap_at(gb, start + i);
            }
            shortcode[sc_len] = '\0';

            // Look up emoji
            const char *emoji = emoji_lookup(shortcode);
            if (emoji) {
                result->span.start = start;
                result->span.len = sc_len;
                result->total_len = sc_len + 2;  // Include both colons
                return emoji;
            }
            return NULL;  // Not a valid emoji shortcode
        }

        // Valid shortcode characters: alphanumeric, _, -, +
        if (!((c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') ||
              c == '_' || c == '-' || c == '+')) {
            return NULL;  // Invalid character, not an emoji shortcode
        }

        // Stop at newline or space
        if (c == '\n' || c == ' ' || c == '\t') return NULL;

        p++;
    }

    return NULL;  // No closing colon found
}

// #endregion

// #region Element Finding

bool md_find_element_at(const GapBuffer *gb, size_t cursor, size_t *out_start, size_t *out_len) {
    size_t len = gap_len(gb);
    size_t scan_start = cursor > 100 ? cursor - 100 : 0;

    // Check image
    for (size_t p = scan_start; p <= cursor && p < len; p++) {
        MdImageAttrs img;
        if (md_check_image(gb, p, &img)) {
            if (cursor >= p && cursor < p + img.total_len) {
                *out_start = p; *out_len = img.total_len; return true;
            }
        }
    }
    // Check link
    for (size_t p = scan_start; p <= cursor && p < len; p++) {
        MdMatch2 link;
        if (md_check_link(gb, p, &link)) {
            if (cursor >= p && cursor < p + link.total_len) {
                *out_start = p; *out_len = link.total_len; return true;
            }
        }
    }
    // Check footnote ref
    for (size_t p = scan_start; p <= cursor && p < len; p++) {
        MdMatch fn_ref;
        if (md_check_footnote_ref(gb, p, &fn_ref)) {
            if (cursor >= p && cursor < p + fn_ref.total_len) {
                *out_start = p; *out_len = fn_ref.total_len; return true;
            }
        }
    }
    // Check inline math
    for (size_t p = scan_start; p <= cursor && p < len; p++) {
        MdMatch math;
        if (md_check_inline_math(gb, p, &math)) {
            if (cursor >= p && cursor < p + math.total_len) {
                *out_start = p; *out_len = math.total_len; return true;
            }
        }
    }
    return false;
}

// #endregion
