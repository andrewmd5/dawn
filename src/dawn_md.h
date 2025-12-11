// dawn_md.h

#ifndef DAWN_MD_H
#define DAWN_MD_H

#include "dawn_types.h"

// #region Common Types

//! A span in the source text (start position + length)
typedef struct {
    size_t start;
    size_t len;
} MdSpan;

//! Parse result with content span and total length
typedef struct {
    MdSpan span;        //!< Content span
    size_t total_len;   //!< Total length including delimiters
} MdMatch;

//! Parse result with two spans (link: text+url, code: content+lang, etc.)
typedef struct {
    MdSpan spans[2];    //!< [0]=first span, [1]=second span
    size_t total_len;   //!< Total length including delimiters
} MdMatch2;

//! Autolink result (url span + email flag)
typedef struct {
    MdSpan span;
    size_t total_len;
    bool is_email;
} MdAutolink;

// #endregion

// #region Style Flags

//! Combinable style flags for markdown formatting
#define MD_BOLD       (1 << 0)  //!< Bold text (**text** or __text__)
#define MD_ITALIC     (1 << 1)  //!< Italic text (*text* or _text_)
#define MD_UNDERLINE  (1 << 2)  //!< Underlined text (__text__)
#define MD_STRIKE     (1 << 3)  //!< Strikethrough (~~text~~)
#define MD_CODE       (1 << 4)  //!< Inline code (`code`)
#define MD_H1         (1 << 5)  //!< Header level 1 (#)
#define MD_H2         (1 << 6)  //!< Header level 2 (##)
#define MD_H3         (1 << 7)  //!< Header level 3 (###)
#define MD_H4         (1 << 8)  //!< Header level 4 (####)
#define MD_H5         (1 << 9)  //!< Header level 5 (#####)
#define MD_H6         (1 << 10) //!< Header level 6 (######)
#define MD_MARK       (1 << 11) //!< Marked/highlighted text (==text==)
#define MD_SUB        (1 << 12) //!< Subscript text (~text~)
#define MD_SUP        (1 << 13) //!< Superscript text (^text^)

typedef uint32_t MdStyle;

// #endregion

// #region Style Application

//! Apply markdown style to terminal output
//! @param s style flags to apply
void md_apply(MdStyle s);

//! Get text scale factor for a style (for text sizing protocol)
//! @param s style flags
//! @return scale factor (1-7, 1=normal)
int32_t md_get_scale(MdStyle s);

//! Fractional scale info for text sizing protocol
typedef struct {
    int32_t scale;   //!< Integer cell scale (1-7)
    int32_t num;     //!< Fractional numerator (0-15, 0 = no fraction)
    int32_t denom;   //!< Fractional denominator (0-15, must be > num when non-zero)
} MdFracScale;

//! Get fractional scale info for a style (for Kitty text sizing protocol)
//! Effective font size = scale * (num/denom), or just scale if num=0
//! @param s style flags
//! @return fractional scale info
MdFracScale md_get_frac_scale(MdStyle s);

//! Convert header level (1-6) to MdStyle flag
//! @param level header level (1-6)
//! @return corresponding MD_H1..MD_H6 flag, or 0 if invalid
static inline MdStyle md_style_for_header_level(int32_t level) {
    switch (level) {
        case 1: return MD_H1;
        case 2: return MD_H2;
        case 3: return MD_H3;
        case 4: return MD_H4;
        case 5: return MD_H5;
        case 6: return MD_H6;
        default: return 0;
    }
}

// #endregion

// #region Inline Formatting Detection

//! Check for inline delimiter at position (*, **, `, ~~, etc.)
//! @param gb gap buffer to check
//! @param pos byte position
//! @param dlen output: length of delimiter in bytes
//! @return style flags for the delimiter, or 0 if none
MdStyle md_check_delim(const GapBuffer *gb, size_t pos, size_t *dlen);

//! Find matching closing delimiter for a style
//! @param gb gap buffer to search
//! @param pos byte position of opening delimiter
//! @param style the style being searched for (e.g., MD_BOLD)
//! @param dlen length of opening delimiter
//! @return position of closing delimiter, or 0 if not found
size_t md_find_closing(const GapBuffer *gb, size_t pos, MdStyle style, size_t dlen);

//! Check for header at start of line
//! @param gb gap buffer to check
//! @param pos byte position (should be at line start)
//! @return MD_H1, MD_H2, MD_H3, or 0 if not a header
MdStyle md_check_header(const GapBuffer *gb, size_t pos);

//! Get header content start position (after # prefix and space)
//! @param gb gap buffer to check
//! @param pos byte position (should be at line start)
//! @param content_start output: position where content begins
//! @return header level (1-3) or 0 if not a header
int32_t md_check_header_content(const GapBuffer *gb, size_t pos, size_t *content_start);

//! Check for heading ID syntax: {#custom-id} at end of heading line
//! @param gb gap buffer to check
//! @param pos byte position to start searching (content start of heading)
//! @param result output: span = ID, total_len = {#id} syntax length
//! @return true if heading ID found on this line
bool md_check_heading_id(const GapBuffer *gb, size_t pos, MdMatch *result);

// #endregion

// #region Image Detection

//! Parsed image attributes from ![alt](path "title"){width=X height=Y}
typedef struct {
    size_t alt_start;
    size_t alt_len;
    size_t path_start;
    size_t path_len;
    size_t title_start;    //!< 0 if no title
    size_t title_len;      //!< 0 if no title
    size_t total_len;
    int32_t width;         //!< Pixels, or negative for percentage
    int32_t height;        //!< Pixels, or negative for percentage
} MdImageAttrs;

//! Check for image syntax: ![alt](path "title"){width=X height=Y}
bool md_check_image(const GapBuffer *gb, size_t pos, MdImageAttrs *attrs);

// #endregion

// #region Block Element Detection

//! Check for code block fence (```language)
//! @param gb gap buffer to check
//! @param pos byte position (should be at line start)
//! @param lang output: language span (may be empty)
//! @return true if valid code fence
bool md_check_code_fence(const GapBuffer *gb, size_t pos, MdSpan *lang);

//! Check for complete code block (``` ... ```)
//! @param gb gap buffer to check
//! @param pos byte position (should be at line start)
//! @param result output: spans[0] = content, spans[1] = lang
//! @return true if complete code block found (has closing fence)
bool md_check_code_block(const GapBuffer *gb, size_t pos, MdMatch2 *result);

//! Check for horizontal rule (---, ***, ___)
//! @param gb gap buffer to check
//! @param pos byte position (should be at line start)
//! @param rule_len output: total length of rule syntax
//! @return true if valid horizontal rule
bool md_check_hr(const GapBuffer *gb, size_t pos, size_t *rule_len);

//! Check for setext heading underline (=== for H1, --- for H2)
//! @param gb gap buffer to check
//! @param pos byte position (should be at line start)
//! @param underline_len output: length of underline including newline
//! @return 1 for H1 (===), 2 for H2 (---), 0 if not setext underline
int32_t md_check_setext_underline(const GapBuffer *gb, size_t pos, size_t *underline_len);

//! Check for block quote (> prefix)
//! @param gb gap buffer to check
//! @param pos byte position (should be at line start)
//! @param content_start output: position where content begins
//! @return nesting level (0 = not a quote, 1 = >, 2 = >>, etc.)
int32_t md_check_blockquote(const GapBuffer *gb, size_t pos, size_t *content_start);

//! Check for list item (-, *, +, or 1. 2. etc.)
//! @param gb gap buffer to check
//! @param pos byte position (should be at line start)
//! @param content_start output: position where content begins
//! @param indent output: leading spaces count
//! @return 0 = not a list, 1 = unordered, 2 = ordered
int32_t md_check_list(const GapBuffer *gb, size_t pos, size_t *content_start, int32_t *indent);

//! Check for task list item (- [ ] or - [x])
//! @param gb gap buffer to check
//! @param pos byte position (should be at line start)
//! @param content_start output: position where content begins
//! @param indent output: leading spaces count
//! @return 0 = not a task, 1 = unchecked, 2 = checked
int32_t md_check_task(const GapBuffer *gb, size_t pos, size_t *content_start, int32_t *indent);

// #endregion

// #region Link Detection

//! Check for link syntax: [text](url)
//! @param gb gap buffer to check
//! @param pos byte position
//! @param result output: spans[0] = text, spans[1] = url
//! @return true if valid link syntax found
bool md_check_link(const GapBuffer *gb, size_t pos, MdMatch2 *result);

// #endregion

// #region Footnote Detection

//! Check for footnote reference: [^id]
//! @param gb gap buffer to check
//! @param pos byte position
//! @param result output: span = ID
//! @return true if valid footnote reference
bool md_check_footnote_ref(const GapBuffer *gb, size_t pos, MdMatch *result);

//! Check for footnote definition: [^id]: content
//! @param gb gap buffer to check
//! @param pos byte position (should be at line start)
//! @param result output: spans[0] = ID, spans[1].start = content start
//! @return true if valid footnote definition
bool md_check_footnote_def(const GapBuffer *gb, size_t pos, MdMatch2 *result);

// #endregion

// #region LaTeX Math Detection

//! Check for inline math: $math$ or \(math\)
//! @param gb gap buffer to check
//! @param pos byte position
//! @param result output: span = math content
//! @return true if valid inline math found
bool md_check_inline_math(const GapBuffer *gb, size_t pos, MdMatch *result);

//! Check for block math: $$math$$ or \[math\]
//! @param gb gap buffer to check
//! @param pos byte position (should be at line start)
//! @param result output: span = math content (len may be 0 for opener only)
//! @return true if valid block math delimiter found
bool md_check_block_math(const GapBuffer *gb, size_t pos, MdMatch *result);

//! Check for complete block math with content ($$...$$)
//! @param gb gap buffer to check
//! @param pos byte position (should be at line start)
//! @param result output: span = math content
//! @return true if complete block math found (has closing $$)
bool md_check_block_math_full(const GapBuffer *gb, size_t pos, MdMatch *result);

// #endregion

// #region Autolinks

//! Check for autolink syntax: <https://...> or <email@domain.com>
//! @param gb gap buffer to check
//! @param pos byte position (should be at '<')
//! @param result output: span = url, is_email flag
//! @return true if valid autolink found
bool md_check_autolink(const GapBuffer *gb, size_t pos, MdAutolink *result);

// #endregion

// #region HTML Entity References

//! Check for HTML entity reference: &name; or &#123; or &#x1F;
//! @param gb gap buffer to check
//! @param pos byte position (should be at '&')
//! @param utf8_out output buffer for decoded UTF-8 (must be at least 8 bytes)
//! @param total_len output: total length of entity reference consumed
//! @return length of UTF-8 output, or 0 if not a valid entity
int32_t md_check_entity(const GapBuffer *gb, size_t pos,
                    char *utf8_out, size_t *total_len);

// #endregion

// #region Typographic Replacements

//! Check for typographic replacement at position
//! @param gb gap buffer to check
//! @param pos byte position
//! @param consumed output: number of source chars replaced
//! @param active_style current inline style (skips replacement if MD_CODE is set)
//! @return UTF-8 replacement string, or NULL if no replacement
const char *md_check_typo_replacement(const GapBuffer *gb, size_t pos, size_t *consumed, MdStyle active_style);

// #endregion

// #region Emoji Shortcodes

//! Check for emoji shortcode syntax: :shortcode:
//! @param gb gap buffer to check
//! @param pos byte position
//! @param result output: span = shortcode (without colons)
//! @return UTF-8 emoji string, or NULL if not a valid emoji shortcode
const char *md_check_emoji(const GapBuffer *gb, size_t pos, MdMatch *result);

// #endregion

// #region Table Detection

//! Column alignment values
DAWN_ENUM(uint8_t) {
    MD_ALIGN_DEFAULT = 0,  //!< No explicit alignment (left)
    MD_ALIGN_LEFT    = 1,  //!< :--- left aligned
    MD_ALIGN_RIGHT   = 2,  //!< ---: right aligned
    MD_ALIGN_CENTER  = 3   //!< :---: center aligned
} MdAlign;

//! Maximum number of columns in a table
#define MD_TABLE_MAX_COLS 32
#define MD_TABLE_MAX_ROWS 64

//! Table structure containing parsed information
typedef struct {
    int32_t col_count;                      //!< Number of columns
    int32_t row_count;                      //!< Total rows (header + body)
    MdAlign align[MD_TABLE_MAX_COLS];   //!< Alignment for each column
    size_t total_len;                   //!< Total length of table in source
} MdTable;

//! Check for table delimiter line (|---|---|)
//! @param gb gap buffer to check
//! @param pos byte position (should be at line start)
//! @param col_count output: number of columns
//! @param align output: alignment array (must have space for col_count)
//! @param line_len output: length of delimiter line
//! @return true if valid table delimiter line
bool md_check_table_delimiter(const GapBuffer *gb, size_t pos,
                              int32_t *col_count, MdAlign *align, size_t *line_len);

//! Check for table header line (| header | header |)
//! @param gb gap buffer to check
//! @param pos byte position (should be at line start)
//! @param col_count output: number of columns (pipe-separated)
//! @param line_len output: length of header line
//! @return true if valid table header line
bool md_check_table_header(const GapBuffer *gb, size_t pos,
                           int32_t *col_count, size_t *line_len);

//! Check for complete table starting at position
//! A table requires: header line, delimiter line, and optionally body rows
//! @param gb gap buffer to check
//! @param pos byte position (should be at line start)
//! @param table output: parsed table structure
//! @return true if valid complete table found
bool md_check_table(const GapBuffer *gb, size_t pos, MdTable *table);

//! Parse a table row into cell boundaries
//! @param gb gap buffer to check
//! @param pos byte position at start of row
//! @param line_len length of the row line
//! @param cell_starts output: array of cell content start positions (uint32_t)
//! @param cell_lens output: array of cell content lengths (uint16_t)
//! @param max_cells maximum number of cells to parse
//! @return number of cells found
int32_t md_parse_table_row(const GapBuffer *gb, size_t pos, size_t line_len,
                       uint32_t *cell_starts, uint16_t *cell_lens, int32_t max_cells);

//! Get display width of table cell content (for alignment padding)
//! @param gb gap buffer
//! @param start cell content start
//! @param len cell content length
//! @return display width in columns
int32_t md_table_cell_width(const GapBuffer *gb, size_t start, size_t len);

// #endregion

// #region Element Finding

//! Find a markdown element (image, link, footnote, inline math) containing the given position
//! Searches backwards up to 100 bytes to find element boundaries
//! @param gb gap buffer to check
//! @param cursor byte position to check
//! @param out_start output: start position of element
//! @param out_len output: length of element
//! @return true if cursor is inside an element
bool md_find_element_at(const GapBuffer *gb, size_t cursor, size_t *out_start, size_t *out_len);

// #endregion

#endif // DAWN_MD_H
