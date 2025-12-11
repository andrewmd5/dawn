// dawn_block.h

#ifndef DAWN_BLOCK_H
#define DAWN_BLOCK_H

#include "dawn_types.h"
#include "dawn_md.h"

// #region Block Types

//! Block element types in the document model
DAWN_ENUM(uint8_t) {
    BLOCK_PARAGRAPH,    //!< Regular text with inline formatting
    BLOCK_HEADER,       //!< H1-H6 header
    BLOCK_CODE,         //!< Fenced code block (```lang...```)
    BLOCK_MATH,         //!< Block math ($$...$$)
    BLOCK_TABLE,        //!< Markdown table
    BLOCK_IMAGE,        //!< Standalone image (![alt](path))
    BLOCK_HR,           //!< Horizontal rule (---, ***, ___)
    BLOCK_BLOCKQUOTE,   //!< Block quote (> prefix)
    BLOCK_LIST_ITEM,    //!< List item (-, *, +, 1.)
    BLOCK_FOOTNOTE_DEF, //!< Footnote definition ([^id]: content)
} BlockType;

// #endregion

// #region Inline Run Types

//! Inline run types for paragraph content
DAWN_ENUM(uint8_t) {
    RUN_TEXT,           //!< Plain styled text
    RUN_LINK,           //!< Link [text](url)
    RUN_FOOTNOTE_REF,   //!< Footnote reference [^id]
    RUN_INLINE_MATH,    //!< Inline math $...$
    RUN_EMOJI,          //!< Emoji shortcode :name:
    RUN_HEADING_ID,     //!< Heading ID {#id}
    RUN_AUTOLINK,       //!< Autolink <https://...> or <email@domain>
    RUN_ENTITY,         //!< HTML entity &nbsp; &#123; etc
    RUN_ESCAPE,         //!< Backslash escape \* \[ etc
    RUN_DELIM,          //!< Style delimiter (**, *, `, ~~, ==, etc.)
} InlineRunType;

//! Inline run - a styled span within a paragraph
//! Optimized for size: 24 bytes (down from ~56)
typedef struct {
    uint32_t byte_start;    //!< Start position in document (max 4GB)
    uint32_t byte_end;      //!< End position (exclusive)
    uint16_t style;         //!< Combined style flags (MdStyle)
    InlineRunType type;     //!< Run type
    uint8_t flags;          //!< Packed flags: bit0=is_email/is_open

    //! Type-specific data
    union {
        struct {
            uint32_t text_start;  //!< Link text start position
            uint16_t text_len;    //!< Link text length (max 64K)
            uint16_t url_len;     //!< URL length
            uint32_t url_start;   //!< URL start position
        } link;

        struct {
            uint32_t id_start;    //!< Footnote ID start
            uint16_t id_len;      //!< Footnote ID length
        } footnote;

        struct {
            uint32_t content_start; //!< Math content start
            uint16_t content_len;   //!< Math content length
        } math;

        struct {
            const char *emoji;  //!< Resolved emoji string (pointer to static data)
        } emoji;

        struct {
            uint32_t id_start;    //!< ID start position
            uint16_t id_len;      //!< ID length
        } heading_id;

        struct {
            uint32_t url_start;   //!< URL start position
            uint16_t url_len;     //!< URL length
        } autolink;

        struct {
            char utf8[8];       //!< Decoded UTF-8 bytes
            uint8_t utf8_len;   //!< Length of decoded content
        } entity;

        struct {
            char escaped_char;  //!< The escaped character
        } escape;

        struct {
            uint16_t delim_style; //!< The style this delimiter controls (MdStyle)
            uint8_t dlen;         //!< Delimiter length (1 for *, 2 for **, etc.)
        } delim;
    } data;
} InlineRun;

//! InlineRun flag bits
#define INLINE_FLAG_IS_OPEN   0x01  //!< For delim: opening delimiter
#define INLINE_FLAG_IS_EMAIL  0x01  //!< For autolink: is email

// #endregion

// #region Block Structure

//! Block - a top-level document element
//! Optimized for size: ~64 bytes (down from ~120)
typedef struct Block {
    // Position in document (byte offsets into GapBuffer, max 4GB)
    uint32_t start;             //!< First byte of block
    uint32_t end;               //!< Last byte + 1 (exclusive)
    uint32_t blank_start;       //!< Byte position where leading blank lines start

    // Cached virtual row info
    int32_t vrow_start;         //!< Virtual row where block starts
    int16_t vrow_count;         //!< Number of virtual rows this block occupies
    int16_t inline_run_count;   //!< Number of inline runs

    // Inline runs (for blocks with inline content)
    InlineRun *inline_runs;     //!< Array of inline runs (NULL if not parsed)

    int16_t inline_run_capacity; //!< Allocated capacity
    BlockType type;             //!< Block type
    uint8_t leading_blank_lines; //!< Blank lines before this block (max 255)

    //! Type-specific data
    union {
        //! BLOCK_HEADER data
        struct {
            uint32_t content_start;   //!< Content start (after "# ")
            uint32_t id_start;        //!< Heading ID start, or 0 if none
            uint16_t id_len;          //!< Heading ID length
            uint8_t level;            //!< Header level (1-6)
        } header;

        //! BLOCK_CODE data
        struct {
            uint32_t lang_start;      //!< Language name start
            uint32_t content_start;   //!< Code content start
            uint32_t content_len;     //!< Code content length
            char *highlighted;        //!< Cached highlighted output (NULL if not computed)
            uint32_t highlighted_len; //!< Length of highlighted output
            uint16_t lang_len;        //!< Language name length
        } code;

        //! BLOCK_MATH data
        struct {
            uint32_t content_start;   //!< LaTeX content start
            uint32_t content_len;     //!< LaTeX content length
            void *tex_sketch;         //!< TexSketch* cached render (NULL if not computed)
        } math;

        //! BLOCK_TABLE data
        struct {
            MdAlign *align;           //!< Column alignments [col_count]
            uint32_t *row_starts;     //!< Start position of each row [row_count]
            uint16_t *row_lens;       //!< Length of each row [row_count]
            uint8_t *row_cell_counts; //!< Number of cells per row [row_count]
            uint32_t **cell_starts;   //!< Cell start positions [row_count][col_count]
            uint16_t **cell_lens;     //!< Cell lengths [row_count][col_count]
            uint8_t col_count;        //!< Number of columns (max 255)
            uint8_t row_count;        //!< Number of rows (max 255)
        } table;

        //! BLOCK_IMAGE data
        struct {
            uint32_t alt_start;       //!< Alt text start
            uint32_t path_start;      //!< Image path start
            uint32_t title_start;     //!< Title text start (0 if no title)
            char *resolved_path;      //!< Cached resolved path (NULL if not computed)
            int16_t width;            //!< Parsed width (negative = percentage)
            int16_t height;           //!< Parsed height (negative = percentage)
            int16_t display_rows;     //!< Calculated rows
            uint16_t alt_len;         //!< Alt text length
            uint16_t path_len;        //!< Image path length
            uint16_t title_len;       //!< Title text length (0 if no title)
        } image;

        //! BLOCK_HR data
        struct {
            uint16_t rule_len;        //!< Length of rule syntax
        } hr;

        //! BLOCK_BLOCKQUOTE data
        struct {
            uint32_t content_start;   //!< Content start (after "> ")
            uint8_t level;            //!< Nesting level (1 = >, 2 = >>, etc.)
        } quote;

        //! BLOCK_LIST_ITEM data
        struct {
            uint32_t content_start;   //!< Content start
            uint8_t list_type;        //!< 1 = unordered, 2 = ordered
            uint8_t indent;           //!< Leading space count (max 255)
            uint8_t task_state;       //!< 0 = not task, 1 = unchecked [ ], 2 = checked [x]
        } list;

        //! BLOCK_FOOTNOTE_DEF data
        struct {
            uint32_t id_start;        //!< Footnote ID start
            uint32_t content_start;   //!< Definition content start
            uint16_t id_len;          //!< Footnote ID length
        } footnote;
    } data;
} Block;

// #endregion

// #region Block Cache

//! Initial block array capacity
#define BLOCK_CACHE_INITIAL_CAPACITY 64

//! Block cache - the parsed document model
typedef struct {
    Block *blocks;          //!< Array of blocks
    uint32_t text_len;      //!< Document length when parsed (max 4GB)
    int32_t total_vrows;    //!< Total virtual rows
    uint32_t count;         //!< Number of blocks
    uint32_t capacity;      //!< Allocated capacity
    int16_t wrap_width;     //!< Text width used for vrow calculation
    int16_t text_height;    //!< Text area height for image scaling
    bool valid;             //!< Cache is valid
} BlockCache;

// #endregion

// #region Block Cache API

//! Initialize a block cache
//! @param bc block cache to initialize
void block_cache_init(BlockCache *bc);

//! Free a block cache and all contained resources
//! @param bc block cache to free
void block_cache_free(BlockCache *bc);

//! Parse entire document into blocks
//! @param bc block cache to populate
//! @param gb gap buffer containing document text
//! @param wrap_width text width for wrapping calculations
//! @param text_height text area height for image scaling
void block_cache_parse(BlockCache *bc, const GapBuffer *gb, int32_t wrap_width, int32_t text_height);

//! Invalidate the cache (mark for reparse)
//! @param bc block cache to invalidate
void block_cache_invalidate(BlockCache *bc);

// #endregion

// #region Block Query API

//! Find block containing a byte position (binary search)
//! @param bc block cache
//! @param byte_pos byte position to find
//! @return pointer to block, or NULL if not found
Block *block_at_pos(BlockCache *bc, size_t byte_pos);

//! Find block containing a virtual row (binary search)
//! @param bc block cache
//! @param vrow virtual row to find
//! @return pointer to block, or NULL if not found
Block *block_at_vrow(BlockCache *bc, int32_t vrow);

//! Get index of block containing a byte position
//! @param bc block cache
//! @param byte_pos byte position to find
//! @return block index, or -1 if not found
int32_t block_index_at_pos(BlockCache *bc, size_t byte_pos);

//! Calculate cursor virtual row within a single block
//! @param block the block containing the cursor
//! @param gb gap buffer
//! @param cursor cursor position
//! @param wrap_width text width for wrapping
//! @return virtual row offset from block's vrow_start
int32_t calc_cursor_vrow_in_block(const Block *block, const GapBuffer *gb,
                              size_t cursor, int32_t wrap_width);

// #endregion

// #region Inline Run API

//! Parse inline runs for a paragraph block (called during block parsing)
//! @param block paragraph block to parse
//! @param gb gap buffer
void block_parse_inline_runs(Block *block, const GapBuffer *gb);

//! Free inline runs for a paragraph block
//! @param block paragraph block
void block_free_inline_runs(Block *block);

//! Find the run index containing a byte position
//! @param block paragraph block with parsed runs
//! @param pos byte position to find
//! @return run index, or -1 if not in any run
int32_t block_find_run_at_pos(const Block *block, size_t pos);

//! Get the run at a given index
//! @param block paragraph block
//! @param index run index
//! @return pointer to run, or NULL if index out of bounds
const InlineRun *block_get_run(const Block *block, int32_t index);

// #endregion

// #region Standalone Parsing API

//! Result of parsing inline content (for testing/decoupled parsing)
//! Optimized: 16 bytes (down from 24)
typedef struct {
    InlineRun *runs;        //!< Array of inline runs
    int16_t run_count;      //!< Number of runs
    int16_t run_capacity;   //!< Allocated capacity
    uint16_t unclosed_styles; //!< Styles that were opened but not closed (MdStyle)
} InlineParseResult;

//! Parse inline content from a plain string
//! @param text input text to parse
//! @param len length of input text
//! @return parse result (caller must free with block_parse_result_free)
InlineParseResult *block_parse_inline_string(const char *text, size_t len);

//! Free an inline parse result
//! @param result result to free
void block_parse_result_free(InlineParseResult *result);

// #endregion

// #region Element Finding API

//! Find deletable markdown element at cursor position using block infrastructure
//! @param bc block cache
//! @param gb gap buffer
//! @param cursor cursor position
//! @param out_start output: element start
//! @param out_len output: element length
//! @return true if element found
bool block_find_element_at(const BlockCache *bc, const GapBuffer *gb, size_t cursor,
                           size_t *out_start, size_t *out_len);

// #endregion

// #region Table Cell API

//! Parse a table cell's inline content into runs
//! @param block table block
//! @param gb gap buffer
//! @param cell_start start position of cell content
//! @param cell_len length of cell content
//! @return parse result (caller must free with block_parse_result_free)
InlineParseResult *block_parse_table_cell(const Block *block, const GapBuffer *gb,
                                          size_t cell_start, size_t cell_len);

// #endregion

// #region Style Application API

//! Apply markdown style to terminal output
//! @param s style flags to apply
void block_apply_style(MdStyle s);

//! Get text scale factor for a style
//! @param s style flags
//! @return scale factor (1-7, 1=normal)
int32_t block_get_scale(MdStyle s);

//! Get fractional scale info for a style
//! @param s style flags
//! @return fractional scale info
MdFracScale block_get_frac_scale(MdStyle s);

//! Convert header level (1-6) to MdStyle flag
//! @param level header level (1-6)
//! @return corresponding MD_H1..MD_H6 flag, or 0 if invalid
MdStyle block_style_for_header_level(int32_t level);

// #endregion

#endif // DAWN_BLOCK_H
