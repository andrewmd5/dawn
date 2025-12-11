// dawn_block.c

#include "dawn_block.h"
#include "dawn_gap.h"
#include "dawn_wrap.h"
#include "dawn_tex.h"
#include "dawn_image.h"
#include "dawn_utils.h"

// Forward declarations for internal helpers
static Block *block_cache_add(BlockCache *bc);
static void block_free(Block *block);
static bool is_at_line_start(const GapBuffer *gb, size_t pos);
static size_t find_line_end(const GapBuffer *gb, size_t pos);
static bool try_parse_image(Block *block, const GapBuffer *gb, size_t pos);
static bool try_parse_code_block(Block *block, const GapBuffer *gb, size_t pos);
static bool try_parse_block_math(Block *block, const GapBuffer *gb, size_t pos);
static bool try_parse_table(Block *block, const GapBuffer *gb, size_t pos);
static bool try_parse_hr(Block *block, const GapBuffer *gb, size_t pos);
static bool try_parse_header(Block *block, const GapBuffer *gb, size_t pos, int32_t wrap_width);
static bool try_parse_footnote_def(Block *block, const GapBuffer *gb, size_t pos);
static void parse_inline_content(InlineParseResult *result, const GapBuffer *gb,
                                  size_t start, size_t end);
static bool try_parse_blockquote(Block *block, const GapBuffer *gb, size_t pos);
static bool try_parse_list_item(Block *block, const GapBuffer *gb, size_t pos);
static void parse_paragraph(Block *block, const GapBuffer *gb, size_t pos, int32_t wrap_width);
static int32_t calculate_block_vrows(const Block *block, const GapBuffer *gb, int32_t wrap_width, int32_t text_height);
static bool is_block_start(const GapBuffer *gb, size_t pos);

// #region Block Cache Management

void block_cache_init(BlockCache *bc) {
    bc->blocks = NULL;
    bc->count = 0;
    bc->capacity = 0;
    bc->text_len = 0;
    bc->total_vrows = 0;
    bc->valid = false;
    bc->wrap_width = 0;
}

void block_cache_free(BlockCache *bc) {
    if (bc->blocks) {
        for (uint32_t i = 0; i < bc->count; i++) {
            block_free(&bc->blocks[i]);
        }
        free(bc->blocks);
    }
    bc->blocks = NULL;
    bc->count = 0;
    bc->capacity = 0;
    bc->valid = false;
}

void block_cache_invalidate(BlockCache *bc) {
    // Free cached resources but keep structure
    if (bc->blocks) {
        for (uint32_t i = 0; i < bc->count; i++) {
            block_free(&bc->blocks[i]);
        }
    }
    bc->count = 0;
    bc->valid = false;
}

//! Add a new block to the cache, growing capacity if needed
static Block *block_cache_add(BlockCache *bc) {
    if (bc->count >= bc->capacity) {
        int32_t new_capacity = bc->capacity == 0 ? BLOCK_CACHE_INITIAL_CAPACITY : bc->capacity * 2;
        Block *new_blocks = realloc(bc->blocks, sizeof(Block) * (size_t)new_capacity);
        if (!new_blocks) return NULL;
        bc->blocks = new_blocks;
        bc->capacity = new_capacity;
    }

    Block *block = &bc->blocks[bc->count++];
    memset(block, 0, sizeof(Block));
    return block;
}

//! Free table data arrays
static void block_free_table_data(Block *block) {
    if (block->data.table.cell_starts) {
        for (int32_t i = 0; i < block->data.table.row_count; i++) {
            free(block->data.table.cell_starts[i]);
        }
        free(block->data.table.cell_starts);
    }
    if (block->data.table.cell_lens) {
        for (int32_t i = 0; i < block->data.table.row_count; i++) {
            free(block->data.table.cell_lens[i]);
        }
        free(block->data.table.cell_lens);
    }
    free(block->data.table.align);
    free(block->data.table.row_starts);
    free(block->data.table.row_lens);
    free(block->data.table.row_cell_counts);
    memset(&block->data.table, 0, sizeof(block->data.table));
}

//! Allocate table data arrays. Returns false on allocation failure.
static bool block_alloc_table_data(Block *block, int32_t row_count, int32_t col_count) {
    block->data.table.row_count = row_count;
    block->data.table.col_count = col_count;

    block->data.table.align = calloc((size_t)col_count, sizeof(MdAlign));
    block->data.table.row_starts = calloc((size_t)row_count, sizeof(size_t));
    block->data.table.row_lens = calloc((size_t)row_count, sizeof(size_t));
    block->data.table.row_cell_counts = calloc((size_t)row_count, sizeof(int32_t));
    block->data.table.cell_starts = calloc((size_t)row_count, sizeof(size_t *));
    block->data.table.cell_lens = calloc((size_t)row_count, sizeof(size_t *));

    if (!block->data.table.align || !block->data.table.row_starts ||
        !block->data.table.row_lens || !block->data.table.row_cell_counts ||
        !block->data.table.cell_starts || !block->data.table.cell_lens) {
        block_free_table_data(block);
        return false;
    }

    for (int32_t i = 0; i < row_count; i++) {
        block->data.table.cell_starts[i] = calloc((size_t)col_count, sizeof(size_t));
        block->data.table.cell_lens[i] = calloc((size_t)col_count, sizeof(size_t));
        if (!block->data.table.cell_starts[i] || !block->data.table.cell_lens[i]) {
            block_free_table_data(block);
            return false;
        }
    }

    return true;
}

//! Free resources owned by a block
static void block_free(Block *block) {
    // Always free inline runs - they're allocated for PARAGRAPH, LIST_ITEM,
    // BLOCKQUOTE, and FOOTNOTE_DEF (safe to call even if NULL)
    block_free_inline_runs(block);

    switch (block->type) {
        case BLOCK_CODE:
            free(block->data.code.highlighted);
            block->data.code.highlighted = NULL;
            break;

        case BLOCK_MATH:
            if (block->data.math.tex_sketch) {
                tex_sketch_free(block->data.math.tex_sketch);
                block->data.math.tex_sketch = NULL;
            }
            break;

        case BLOCK_IMAGE:
            free(block->data.image.resolved_path);
            block->data.image.resolved_path = NULL;
            break;

        case BLOCK_TABLE:
            block_free_table_data(block);
            break;

        default:
            break;
    }
}

// #endregion

// #region Block Parsing

void block_cache_parse(BlockCache *bc, const GapBuffer *gb, int32_t wrap_width, int32_t text_height) {
    // Clear existing blocks
    block_cache_invalidate(bc);

    bc->text_len = gap_len(gb);
    bc->wrap_width = wrap_width;
    bc->text_height = text_height;
    bc->total_vrows = 0;

    size_t pos = 0;
    size_t len = bc->text_len;

    while (pos < len) {
        // Skip blank lines (lines containing only whitespace)
        int32_t blank_lines = 0;
        size_t blank_start = pos;
        while (pos < len) {
            size_t line_start = pos;
            // Skip whitespace on this line
            while (pos < len && gap_at(gb, pos) != '\n' && ISBLANK_(gap_at(gb, pos))) {
                pos++;
            }
            // If we hit newline or EOF after only whitespace, this was a blank line
            if (pos >= len || gap_at(gb, pos) == '\n') {
                if (pos < len) pos++;  // Skip the newline
                blank_lines++;
                // Continue looking for more blank lines
            } else {
                // Non-blank line found, restore position to line start
                pos = line_start;
                break;
            }
        }

        if (pos >= len) break;  // All remaining content was blank lines

        Block *block = block_cache_add(bc);
        if (!block) break;  // Out of memory

        block->blank_start = blank_start;
        block->start = pos;
        block->leading_blank_lines = blank_lines;
        block->vrow_start = bc->total_vrows + blank_lines;
        bc->total_vrows += blank_lines;  // Account for skipped blank lines in total

        // Try each block type in priority order
        if (try_parse_image(block, gb, pos)) {
            pos = block->end;
        }
        else if (try_parse_code_block(block, gb, pos)) {
            pos = block->end;
        }
        else if (try_parse_block_math(block, gb, pos)) {
            pos = block->end;
        }
        else if (try_parse_table(block, gb, pos)) {
            pos = block->end;
        }
        else if (try_parse_hr(block, gb, pos)) {
            pos = block->end;
        }
        else if (try_parse_header(block, gb, pos, wrap_width)) {
            pos = block->end;
        }
        else if (try_parse_footnote_def(block, gb, pos)) {
            pos = block->end;
        }
        else if (try_parse_blockquote(block, gb, pos)) {
            pos = block->end;
        }
        else if (try_parse_list_item(block, gb, pos)) {
            pos = block->end;
        }
        else {
            // Default: paragraph (extends to blank line or block element)
            parse_paragraph(block, gb, pos, wrap_width);
            pos = block->end;
        }

        // Calculate virtual rows for this block
        block->vrow_count = calculate_block_vrows(block, gb, wrap_width, text_height);
        bc->total_vrows += block->vrow_count;
    }

    bc->valid = true;
}

// #endregion

// #region Block Detection Helpers

//! Check if position is at the start of a line
static bool is_at_line_start(const GapBuffer *gb, size_t pos) {
    if (pos == 0) return true;
    return gap_at(gb, pos - 1) == '\n';
}

//! Find end of line (newline position or end of buffer)
static size_t find_line_end(const GapBuffer *gb, size_t pos) {
    size_t len = gap_len(gb);
    while (pos < len && gap_at(gb, pos) != '\n') {
        pos++;
    }
    return pos;
}

//! Check if position starts a block element
static bool is_block_start(const GapBuffer *gb, size_t pos) {
    if (!is_at_line_start(gb, pos)) return false;

    size_t len = gap_len(gb);
    if (pos >= len) return false;

    char c = gap_at(gb, pos);

    // Image: ![
    if (c == '!' && pos + 1 < len && gap_at(gb, pos + 1) == '[') return true;

    // Code fence: ```
    if (c == '`' && pos + 2 < len &&
        gap_at(gb, pos + 1) == '`' && gap_at(gb, pos + 2) == '`') return true;

    // Block math: $$
    if (c == '$' && pos + 1 < len && gap_at(gb, pos + 1) == '$') return true;

    // Table: |
    if (c == '|') return true;

    // HR: ---, ***, ___
    if (c == '-' || c == '*' || c == '_') {
        size_t rule_len;
        if (md_check_hr(gb, pos, &rule_len)) return true;
    }

    // Header: #
    if (c == '#') return true;

    // Footnote definition: [^
    if (c == '[' && pos + 1 < len && gap_at(gb, pos + 1) == '^') return true;

    // Blockquote: >
    if (c == '>') return true;

    // List: -, *, +
    if (c == '-' || c == '*' || c == '+') {
        if (pos + 1 < len && gap_at(gb, pos + 1) == ' ') return true;
    }

    // Ordered list: 1-9 digits followed by . or ) and space
    if (ISDIGIT_(c)) {
        size_t p = pos;
        int32_t digits = 0;
        while (p < len && ISDIGIT_(gap_at(gb, p)) && digits < 10) {
            digits++;
            p++;
        }
        // CommonMark: max 9 digits for ordered list number
        if (digits >= 1 && digits <= 9 && p < len && (gap_at(gb, p) == '.' || gap_at(gb, p) == ')')) {
            if (p + 1 < len && gap_at(gb, p + 1) == ' ') return true;
        }
    }

    return false;
}

// #endregion

// #region Block Type Parsers

static bool try_parse_image(Block *block, const GapBuffer *gb, size_t pos) {
    if (!is_at_line_start(gb, pos)) return false;

    MdImageAttrs img;
    if (!md_check_image(gb, pos, &img)) {
        return false;
    }

    // For block images, the image must be alone on its line
    // Check that after the image syntax there's only whitespace until newline/EOF
    size_t check_pos = pos + img.total_len;
    size_t len = gap_len(gb);
    while (check_pos < len && gap_at(gb, check_pos) == ' ') {
        check_pos++;
    }
    // If there's non-whitespace text after the image (before newline), it's not a block image
    if (check_pos < len && gap_at(gb, check_pos) != '\n') {
        return false;
    }

    block->type = BLOCK_IMAGE;
    block->end = pos + img.total_len;

    // Include trailing whitespace and newline
    while (block->end < len && gap_at(gb, block->end) == ' ') {
        block->end++;
    }
    if (block->end < len && gap_at(gb, block->end) == '\n') {
        block->end++;
    }

    block->data.image.alt_start = (uint32_t)img.alt_start;
    block->data.image.alt_len = (uint16_t)img.alt_len;
    block->data.image.path_start = (uint32_t)img.path_start;
    block->data.image.path_len = (uint16_t)img.path_len;
    block->data.image.title_start = (uint32_t)img.title_start;
    block->data.image.title_len = (uint16_t)img.title_len;
    block->data.image.width = (int16_t)img.width;
    block->data.image.height = (int16_t)img.height;
    block->data.image.display_rows = 0;  // Calculated later
    block->data.image.resolved_path = NULL;

    return true;
}

static bool try_parse_code_block(Block *block, const GapBuffer *gb, size_t pos) {
    if (!is_at_line_start(gb, pos)) return false;

    MdMatch2 code;
    if (!md_check_code_block(gb, pos, &code)) {
        return false;
    }

    block->type = BLOCK_CODE;
    block->end = pos + code.total_len;

    block->data.code.lang_start = code.spans[1].start;
    block->data.code.lang_len = code.spans[1].len;
    block->data.code.content_start = code.spans[0].start;
    block->data.code.content_len = code.spans[0].len;
    block->data.code.highlighted = NULL;
    block->data.code.highlighted_len = 0;

    return true;
}

static bool try_parse_block_math(Block *block, const GapBuffer *gb, size_t pos) {
    if (!is_at_line_start(gb, pos)) return false;

    MdMatch math;
    if (!md_check_block_math_full(gb, pos, &math)) {
        return false;
    }

    block->type = BLOCK_MATH;
    block->end = pos + math.total_len;

    block->data.math.content_start = math.span.start;
    block->data.math.content_len = math.span.len;
    block->data.math.tex_sketch = NULL;

    return true;
}

static bool try_parse_table(Block *block, const GapBuffer *gb, size_t pos) {
    if (!is_at_line_start(gb, pos)) return false;

    MdTable tbl;
    if (!md_check_table(gb, pos, &tbl)) {
        return false;
    }

    size_t table_end = pos + tbl.total_len;

    // First pass: count rows
    int32_t row_count = 0;
    size_t scan_pos = pos;
    while (scan_pos < table_end) {
        size_t row_end = scan_pos;
        while (row_end < table_end && gap_at(gb, row_end) != '\n') {
            row_end++;
        }
        if (row_end > scan_pos) row_count++;
        scan_pos = row_end + 1;
    }

    if (row_count == 0) return false;

    // Allocate table data
    if (!block_alloc_table_data(block, row_count, tbl.col_count)) {
        return false;
    }

    block->type = BLOCK_TABLE;
    block->end = table_end;
    memcpy(block->data.table.align, tbl.align, (size_t)tbl.col_count * sizeof(MdAlign));

    // Second pass: fill row data
    scan_pos = pos;
    int32_t row_idx = 0;
    while (scan_pos < table_end && row_idx < row_count) {
        size_t row_start = scan_pos;
        size_t row_end = row_start;
        while (row_end < table_end && gap_at(gb, row_end) != '\n') {
            row_end++;
        }
        size_t row_len = row_end - row_start;

        if (row_len > 0) {
            block->data.table.row_starts[row_idx] = row_start;
            block->data.table.row_lens[row_idx] = row_len;

            int32_t cells = md_parse_table_row(gb, row_start, row_len,
                                           block->data.table.cell_starts[row_idx],
                                           block->data.table.cell_lens[row_idx],
                                           tbl.col_count);
            block->data.table.row_cell_counts[row_idx] = cells;
            row_idx++;
        }

        scan_pos = row_end + 1;
    }

    return true;
}

static bool try_parse_hr(Block *block, const GapBuffer *gb, size_t pos) {
    if (!is_at_line_start(gb, pos)) return false;

    size_t rule_len;
    if (!md_check_hr(gb, pos, &rule_len)) {
        return false;
    }

    block->type = BLOCK_HR;
    block->end = pos + rule_len;

    // Include trailing newline
    if (block->end < gap_len(gb) && gap_at(gb, block->end) == '\n') {
        block->end++;
    }

    block->data.hr.rule_len = rule_len;

    return true;
}

static bool try_parse_header(Block *block, const GapBuffer *gb, size_t pos, int32_t wrap_width) {
    if (!is_at_line_start(gb, pos)) return false;

    MdStyle header_style = md_check_header(gb, pos);
    if (!header_style) return false;

    size_t content_start;
    int32_t level = md_check_header_content(gb, pos, &content_start);
    if (level == 0) return false;

    block->type = BLOCK_HEADER;

    // Find end of header line
    size_t end = find_line_end(gb, pos);
    block->end = end;

    // Include trailing newline
    if (block->end < gap_len(gb) && gap_at(gb, block->end) == '\n') {
        block->end++;
    }

    block->data.header.level = level;
    block->data.header.content_start = content_start;

    // Check for heading ID {#id}
    MdMatch heading_id;
    if (md_check_heading_id(gb, content_start, &heading_id)) {
        block->data.header.id_start = heading_id.span.start;
        block->data.header.id_len = heading_id.span.len;
    } else {
        block->data.header.id_start = 0;
        block->data.header.id_len = 0;
    }

    (void)wrap_width;  // Will be used in vrow calculation
    return true;
}

static bool try_parse_footnote_def(Block *block, const GapBuffer *gb, size_t pos) {
    if (!is_at_line_start(gb, pos)) return false;

    MdMatch2 def;
    if (!md_check_footnote_def(gb, pos, &def)) {
        return false;
    }

    block->type = BLOCK_FOOTNOTE_DEF;

    // Find end of footnote (ends at blank line or next footnote def)
    size_t len = gap_len(gb);
    size_t end = def.spans[1].start;  // content start

    while (end < len) {
        // Find end of current line
        while (end < len && gap_at(gb, end) != '\n') end++;

        // Check if this is end of buffer
        if (end >= len) break;

        // Move past newline
        end++;

        // Check if next line is blank or another footnote def
        if (end < len) {
            if (gap_at(gb, end) == '\n') break;  // Blank line
            MdMatch2 next_def;
            if (md_check_footnote_def(gb, end, &next_def)) break;  // Another def
        }
    }

    block->end = end;
    block->data.footnote.id_start = def.spans[0].start;
    block->data.footnote.id_len = def.spans[0].len;
    block->data.footnote.content_start = def.spans[1].start;
    block_parse_inline_runs(block, gb);

    return true;
}

static bool try_parse_blockquote(Block *block, const GapBuffer *gb, size_t pos) {
    if (!is_at_line_start(gb, pos)) return false;

    size_t content_start;
    int32_t level = md_check_blockquote(gb, pos, &content_start);
    if (level == 0) return false;

    block->type = BLOCK_BLOCKQUOTE;

    // Find end of blockquote (continues while lines start with >)
    size_t len = gap_len(gb);
    size_t end = find_line_end(gb, pos);

    while (end < len) {
        // Move past newline
        if (gap_at(gb, end) == '\n') end++;

        // Check if next line continues blockquote
        if (end < len && gap_at(gb, end) == '>') {
            end = find_line_end(gb, end);
        } else {
            break;
        }
    }

    block->end = end;
    block->data.quote.level = level;
    block->data.quote.content_start = content_start;
    block_parse_inline_runs(block, gb);

    return true;
}

static bool try_parse_list_item(Block *block, const GapBuffer *gb, size_t pos) {
    if (!is_at_line_start(gb, pos)) return false;

    size_t content_start;
    int32_t indent;

    // Check for task list first
    int32_t task_state = md_check_task(gb, pos, &content_start, &indent);
    if (task_state > 0) {
        block->type = BLOCK_LIST_ITEM;
        block->end = find_line_end(gb, pos);
        if (block->end < gap_len(gb) && gap_at(gb, block->end) == '\n') {
            block->end++;
        }
        block->data.list.list_type = 1;  // Task lists are unordered
        block->data.list.indent = indent;
        block->data.list.task_state = task_state;
        block->data.list.content_start = content_start;
        block_parse_inline_runs(block, gb);
        return true;
    }

    // Check for regular list
    int32_t list_type = md_check_list(gb, pos, &content_start, &indent);
    if (list_type == 0) return false;

    block->type = BLOCK_LIST_ITEM;
    block->end = find_line_end(gb, pos);
    if (block->end < gap_len(gb) && gap_at(gb, block->end) == '\n') {
        block->end++;
    }

    block->data.list.list_type = list_type;
    block->data.list.indent = indent;
    block->data.list.task_state = 0;
    block->data.list.content_start = content_start;
    block_parse_inline_runs(block, gb);

    return true;
}

static void parse_paragraph(Block *block, const GapBuffer *gb, size_t pos, int32_t wrap_width) {
    block->type = BLOCK_PARAGRAPH;
    block->inline_runs = NULL;
    block->inline_run_count = 0;
    block->inline_run_capacity = 0;

    size_t len = gap_len(gb);
    size_t end = pos;
    size_t last_line_start = pos;
    bool has_content = false;  // Track if we have non-blank content

    // Check if first line starts with 4+ spaces (indented code - can't be setext)
    bool first_line_indented = false;
    {
        int32_t indent = 0;
        size_t p = pos;
        while (p < len && ISBLANK_(gap_at(gb, p))) {
            if (gap_at(gb, p) == '\t') indent += 4;
            else indent++;
            p++;
        }
        first_line_indented = (indent >= 4);
    }

    while (end < len) {
        char c = gap_at(gb, end);

        // Track if we have any non-whitespace content
        if (!ISSPACE_(c)) {
            has_content = true;
        }

        if (c == '\n') {
            // Check for setext heading underline on next line
            // Only if we have content and first line isn't indented code
            if (has_content && !first_line_indented && end + 1 < len) {
                size_t underline_len;
                int32_t setext_level = md_check_setext_underline(gb, end + 1, &underline_len);
                if (setext_level > 0) {
                    // This is a setext heading!
                    block->type = BLOCK_HEADER;
                    block->data.header.level = setext_level;
                    block->data.header.content_start = pos;
                    block->data.header.id_start = 0;
                    block->data.header.id_len = 0;
                    // End includes the underline
                    block->end = end + 1 + underline_len;
                    return;
                }
            }

            // Check for blank line (paragraph end)
            if (end + 1 < len && gap_at(gb, end + 1) == '\n') {
                end++;  // Include first newline
                break;
            }

            // Check if next line starts a block element
            if (end + 1 < len && is_block_start(gb, end + 1)) {
                break;
            }

            last_line_start = end + 1;
        }
        end++;
    }

    // Include trailing newline if present
    if (end < len && gap_at(gb, end) == '\n') {
        end++;
    }

    block->end = end;

    // Parse inline runs eagerly
    block_parse_inline_runs(block, gb);

    (void)wrap_width;  // Used in vrow calculation
    (void)last_line_start;
}

// #endregion

// #region Virtual Row Calculation

static int32_t calculate_block_vrows(const Block *block, const GapBuffer *gb, int32_t wrap_width, int32_t text_height) {
    if (wrap_width <= 0) wrap_width = 80;
    if (text_height <= 0) text_height = 24;

    switch (block->type) {
        case BLOCK_HR:
            return 1;

        case BLOCK_IMAGE: {
            // Use cached display_rows if already calculated
            if (block->data.image.display_rows > 0) {
                return block->data.image.display_rows;
            }

            // Calculate image rows from dimensions
            int32_t img_w = block->data.image.width;
            int32_t img_h = block->data.image.height;

            // Extract raw path
            char raw_path[512];
            size_t plen = block->data.image.path_len;
            if (plen > sizeof(raw_path) - 1) plen = sizeof(raw_path) - 1;
            for (size_t i = 0; i < plen; i++) {
                raw_path[i] = gap_at(gb, block->data.image.path_start + i);
            }
            raw_path[plen] = '\0';

            // Resolve and cache the image (handles URLs, relative paths, etc.)
            char cached_path[512];
            if (!image_resolve_and_cache_to(raw_path, NULL, cached_path, sizeof(cached_path))) {
                return 1;
            }

            if (!image_is_supported(cached_path)) {
                return 1;
            }

            // Calculate display dimensions
            int32_t img_cols = 0, img_rows_spec = 0;

            if (img_w < 0) img_cols = wrap_width * (-img_w) / 100;
            else if (img_w > 0) img_cols = img_w;
            if (img_cols > wrap_width) img_cols = wrap_width;
            if (img_cols <= 0) img_cols = wrap_width / 2;

            if (img_h < 0) img_rows_spec = text_height * (-img_h) / 100;
            else if (img_h > 0) img_rows_spec = img_h;

            int32_t pixel_w, pixel_h;
            if (image_get_size(cached_path, &pixel_w, &pixel_h)) {
                int32_t rows = image_calc_rows(pixel_w, pixel_h, img_cols, img_rows_spec);
                // Cache for later
                ((Block *)block)->data.image.display_rows = rows > 0 ? rows : 1;
                return rows > 0 ? rows : 1;
            }
            return 1;
        }

        case BLOCK_HEADER: {
            // Headers may use text scaling
            int32_t level = block->data.header.level;
            int32_t scale = 1;
            if (level == 1) scale = 2;
            else if (level == 2) scale = 1;  // 1.5x rounds to 2 rows for 1 line

            // Count content width and calculate wrapped lines
            size_t content_start = block->data.header.content_start;
            size_t end = block->end;
            if (end > 0 && gap_at(gb, end - 1) == '\n') end--;

            int32_t total_width = 0;
            for (size_t p = content_start; p < end; ) {
                size_t next;
                total_width += gap_grapheme_width(gb, p, &next);
                p = next;
            }

            int32_t available = wrap_width / scale;
            if (available < 1) available = 1;

            int32_t lines = (total_width + available - 1) / available;
            if (lines < 1) lines = 1;

            return lines * scale;
        }

        case BLOCK_CODE: {
            // Count newlines in code content
            int32_t lines = 1;
            for (size_t p = block->data.code.content_start;
                 p < block->data.code.content_start + block->data.code.content_len; p++) {
                if (gap_at(gb, p) == '\n') lines++;
            }
            return lines;
        }

        case BLOCK_MATH: {
            // Use cached tex_sketch if available
            TexSketch *sketch = (TexSketch *)block->data.math.tex_sketch;
            if (sketch) {
                return sketch->height > 0 ? sketch->height : 1;
            }

            // Render TeX to determine height and cache it
            size_t clen = block->data.math.content_len;
            size_t cstart = block->data.math.content_start;
            char *latex = malloc(clen + 1);
            if (latex) {
                for (size_t i = 0; i < clen; i++) {
                    latex[i] = gap_at(gb, cstart + i);
                }
                latex[clen] = '\0';
                sketch = tex_render_string(latex, clen, true);
                free(latex);
                if (sketch) {
                    // Cache for later use during rendering
                    ((Block *)block)->data.math.tex_sketch = sketch;
                    return sketch->height > 0 ? sketch->height : 1;
                }
            }
            return 1;
        }

        case BLOCK_TABLE: {
            // Calculate actual table vrows matching render_table_element logic
            // Need to parse table structure and calculate wrapped cell heights
            int32_t vrows = 0;

            // Calculate column widths (same as render)
            int32_t col_widths[MD_TABLE_MAX_COLS];
            int32_t total_col_width = wrap_width - (block->data.table.col_count + 1);  // Account for borders
            int32_t base_width = total_col_width / block->data.table.col_count;
            for (int32_t ci = 0; ci < block->data.table.col_count; ci++) {
                col_widths[ci] = base_width > 0 ? base_width : 1;
            }

            // Parse all rows like render does
            size_t row_starts[64], row_lens[64];
            int32_t row_count = 0;
            size_t scan_pos = block->start;
            size_t block_end = block->start + (size_t)(block->end - block->start);

            while (scan_pos < block_end && row_count < 64) {
                int32_t scan_cols = 0;
                size_t scan_len = 0;

                if (row_count == 1) {
                    // Delimiter row
                    MdAlign dummy_align[MD_TABLE_MAX_COLS];
                    if (md_check_table_delimiter(gb, scan_pos, &scan_cols, dummy_align, &scan_len)) {
                        row_starts[row_count] = scan_pos;
                        row_lens[row_count] = scan_len;
                        row_count++;
                        scan_pos += scan_len;
                        continue;
                    }
                }

                if (md_check_table_header(gb, scan_pos, &scan_cols, &scan_len)) {
                    row_starts[row_count] = scan_pos;
                    row_lens[row_count] = scan_len;
                    row_count++;
                    scan_pos += scan_len;
                } else {
                    break;
                }
            }

            // Top border
            vrows++;

            // Calculate row heights and dividers
            for (int32_t ri = 0; ri < row_count; ri++) {
                if (ri == 1) {
                    // Delimiter row
                    vrows++;
                } else {
                    // Parse cells and calculate max wrapped height
                    uint32_t cell_starts[MD_TABLE_MAX_COLS];
                    uint16_t cell_lens[MD_TABLE_MAX_COLS];
                    int32_t cells = md_parse_table_row(gb, row_starts[ri], row_lens[ri],
                                                   cell_starts, cell_lens, MD_TABLE_MAX_COLS);
                    int32_t max_lines = 1;
                    for (int32_t ci = 0; ci < cells && ci < block->data.table.col_count; ci++) {
                        // Calculate wrapped lines for cell
                        int32_t lines = 1, line_width = 0;
                        size_t p = cell_starts[ci], end = cell_starts[ci] + cell_lens[ci];
                        while (p < end) {
                            size_t dlen = 0;
                            MdStyle delim = md_check_delim(gb, p, &dlen);
                            if (delim != 0 && dlen > 0) { p += dlen; continue; }
                            size_t next;
                            int32_t gw = gap_grapheme_width(gb, p, &next);
                            if (line_width + gw > col_widths[ci] && line_width > 0) {
                                lines++; line_width = gw;
                            } else {
                                line_width += gw;
                            }
                            p = next;
                        }
                        if (lines > max_lines) max_lines = lines;
                    }
                    vrows += max_lines;

                    // Row divider between data rows (not after header, not after last row)
                    if (ri < row_count - 1 && ri != 0) {
                        vrows++;
                    }
                }
            }

            // Bottom border
            vrows++;

            return vrows;
        }

        case BLOCK_BLOCKQUOTE:
        case BLOCK_LIST_ITEM:
        case BLOCK_FOOTNOTE_DEF:
        case BLOCK_PARAGRAPH:
        default: {
            // Count wrapped lines
            int32_t vrows = 0;
            size_t pos = block->start;
            size_t end = block->end;

            while (pos < end) {
                // Find end of logical line
                size_t line_end = pos;
                while (line_end < end && gap_at(gb, line_end) != '\n') {
                    line_end++;
                }

                // Calculate wrapped lines for this logical line
                int32_t line_width = 0;
                int32_t line_vrows = 1;
                for (size_t p = pos; p < line_end; ) {
                    size_t next;
                    int32_t gw = gap_grapheme_width(gb, p, &next);
                    if (line_width + gw > wrap_width && line_width > 0) {
                        line_vrows++;
                        line_width = gw;
                    } else {
                        line_width += gw;
                    }
                    p = next;
                }

                vrows += line_vrows;

                // Move past newline
                pos = line_end;
                if (pos < end && gap_at(gb, pos) == '\n') {
                    pos++;
                }
            }

            return vrows > 0 ? vrows : 1;
        }
    }
}

// #endregion

// #region Query Functions

Block *block_at_pos(BlockCache *bc, size_t byte_pos) {
    int32_t idx = block_index_at_pos(bc, byte_pos);
    return idx >= 0 ? &bc->blocks[idx] : NULL;
}

Block *block_at_vrow(BlockCache *bc, int32_t vrow) {
    if (!bc->valid || bc->count == 0) return NULL;

    // Binary search for block containing vrow
    uint32_t lo = 0, hi = bc->count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        Block *b = &bc->blocks[mid];
        if (vrow < b->vrow_start) {
            hi = mid;
        } else if (vrow >= b->vrow_start + b->vrow_count) {
            lo = mid + 1;
        } else {
            return b;
        }
    }

    // If not found exactly, return the last block before this vrow
    if (lo > 0 && lo <= bc->count) {
        return &bc->blocks[lo - 1];
    }

    return bc->count > 0 ? &bc->blocks[0] : NULL;
}

int32_t block_index_at_pos(BlockCache *bc, size_t byte_pos) {
    if (!bc->valid || bc->count == 0) return -1;

    // Binary search for block containing byte_pos (including blank region)
    uint32_t lo = 0, hi = bc->count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        Block *b = &bc->blocks[mid];
        // Check blank region: [blank_start, start) belongs to this block
        if (byte_pos < b->blank_start) {
            hi = mid;
        } else if (byte_pos >= b->end) {
            lo = mid + 1;
        } else {
            return (int32_t)mid;  // In blank region or block content
        }
    }

    // Position is at end of document
    if (lo > 0 && byte_pos >= bc->blocks[lo - 1].end) {
        return (int32_t)(lo - 1);
    }

    return lo < bc->count ? (int32_t)lo : (int32_t)(bc->count - 1);
}

int32_t calc_cursor_vrow_in_block(const Block *block, const GapBuffer *gb,
                              size_t cursor, int32_t wrap_width) {
    // Handle cursor in blank region before block content
    if (cursor >= block->blank_start && cursor < block->start) {
        // Count newlines from blank_start to cursor to find which blank line
        int32_t line = 0;
        for (size_t p = block->blank_start; p < cursor; p++) {
            if (gap_at(gb, p) == '\n') line++;
        }
        // Offset from vrow_start: blank lines are at negative offsets
        // First blank line is at vrow_start - leading_blank_lines
        return line - block->leading_blank_lines;
    }

    if (cursor < block->blank_start || cursor > block->end) {
        return 0;
    }

    if (wrap_width <= 0) wrap_width = 80;

    // For simple blocks, cursor is at vrow 0
    switch (block->type) {
        case BLOCK_HR:
            return 0;

        case BLOCK_IMAGE: {
            // When cursor is in image, it renders as raw wrapped text
            // Count newlines and wrapping from block start to cursor
            int32_t vrow = 0;
            int32_t col = 0;
            for (size_t p = block->start; p < cursor && p < block->end; ) {
                char c = gap_at(gb, p);
                if (c == '\n') {
                    vrow++;
                    col = 0;
                    p++;
                } else {
                    size_t next;
                    int32_t gw = gap_grapheme_width(gb, p, &next);
                    if (col + gw > wrap_width && col > 0) {
                        vrow++;
                        col = gw;
                    } else {
                        col += gw;
                    }
                    p = next;
                }
            }
            return vrow;
        }

        case BLOCK_HEADER: {
            // Calculate cursor position in scaled header
            int32_t level = block->data.header.level;
            int32_t scale = (level == 1) ? 2 : 1;
            int32_t available = wrap_width / scale;
            if (available < 1) available = 1;

            int32_t char_col = 0;
            int32_t row = 0;
            for (size_t p = block->start; p < cursor && p < block->end; ) {
                if (gap_at(gb, p) == '\n') break;
                size_t next;
                int32_t gw = gap_grapheme_width(gb, p, &next);
                char_col += gw;
                if (char_col > available) {
                    row++;
                    char_col = gw;
                }
                p = next;
            }
            return row * scale;
        }

        case BLOCK_CODE:
        case BLOCK_MATH:
        case BLOCK_TABLE: {
            // Count newlines from block start to cursor
            int32_t vrow = 0;
            for (size_t p = block->start; p < cursor && p < block->end; p++) {
                if (gap_at(gb, p) == '\n') vrow++;
            }
            return vrow;
        }

        default: {
            // General wrapping calculation
            int32_t vrow = 0;
            size_t pos = block->start;

            while (pos < cursor && pos < block->end) {
                // Find end of logical line
                size_t line_end = pos;
                while (line_end < block->end && gap_at(gb, line_end) != '\n') {
                    line_end++;
                }

                // Calculate wrapped position within line
                int32_t line_width = 0;
                for (size_t p = pos; p < cursor && p < line_end; ) {
                    size_t next;
                    int32_t gw = gap_grapheme_width(gb, p, &next);
                    if (line_width + gw > wrap_width && line_width > 0) {
                        vrow++;
                        line_width = gw;
                    } else {
                        line_width += gw;
                    }
                    p = next;
                }

                // If cursor is within this line, we're done
                if (cursor <= line_end) break;

                // Count the remaining wrapped portions of this line
                for (size_t p = (cursor > pos ? cursor : pos); p < line_end; ) {
                    size_t next;
                    int32_t gw = gap_grapheme_width(gb, p, &next);
                    if (line_width + gw > wrap_width && line_width > 0) {
                        vrow++;
                        line_width = gw;
                    } else {
                        line_width += gw;
                    }
                    p = next;
                }

                vrow++;  // For the newline
                pos = line_end + 1;
            }

            return vrow;
        }
    }
}

// #endregion

// #region Inline Run Parsing

//! Initial capacity for inline runs array
#define INLINE_RUN_INITIAL_CAPACITY 16

//! Check if block type has inline content
static bool block_has_inline_content(BlockType type) {
    return type == BLOCK_PARAGRAPH || type == BLOCK_LIST_ITEM ||
           type == BLOCK_BLOCKQUOTE || type == BLOCK_FOOTNOTE_DEF;
}

void block_parse_inline_runs(Block *block, const GapBuffer *gb) {
    if (!block_has_inline_content(block->type)) return;

    // Free existing runs
    block_free_inline_runs(block);

    // Use centralized parsing function
    InlineParseResult result = {0};
    parse_inline_content(&result, gb, block->start, block->end);

    // Transfer ownership of runs to block
    block->inline_runs = result.runs;
    block->inline_run_count = result.run_count;
    block->inline_run_capacity = result.run_capacity;
}

void block_free_inline_runs(Block *block) {
    free(block->inline_runs);
    block->inline_runs = NULL;
    block->inline_run_count = 0;
    block->inline_run_capacity = 0;
}

int32_t block_find_run_at_pos(const Block *block, size_t pos) {
    if (!block->inline_runs) return -1;

    // Binary search for the run containing pos
    int32_t lo = 0, hi = block->inline_run_count - 1;
    while (lo <= hi) {
        int32_t mid = (lo + hi) / 2;
        const InlineRun *run = &block->inline_runs[mid];
        if (pos < run->byte_start) {
            hi = mid - 1;
        } else if (pos >= run->byte_end) {
            lo = mid + 1;
        } else {
            return mid;  // pos is within this run
        }
    }
    return -1;  // Not found in any run
}

const InlineRun *block_get_run(const Block *block, int32_t index) {
    if (index < 0 || index >= block->inline_run_count) return NULL;
    return &block->inline_runs[index];
}

// #endregion

// #region Standalone Parsing API

//! Add an inline run to a parse result
static InlineRun *result_add_run(InlineParseResult *result) {
    if (result->run_count >= result->run_capacity) {
        int32_t new_cap = result->run_capacity == 0
            ? INLINE_RUN_INITIAL_CAPACITY
            : result->run_capacity * 2;
        InlineRun *new_runs = realloc(result->runs, sizeof(InlineRun) * (size_t)new_cap);
        if (!new_runs) return NULL;
        result->runs = new_runs;
        result->run_capacity = new_cap;
    }

    InlineRun *run = &result->runs[result->run_count++];
    memset(run, 0, sizeof(InlineRun));
    return run;
}

//! Internal parsing function - parses inline content into an InlineParseResult
static void parse_inline_content(InlineParseResult *result, const GapBuffer *gb,
                                  size_t start, size_t end) {
    size_t pos = start;

    // Style stack for tracking nested formatting
    struct {
        MdStyle style;
        size_t dlen;
        size_t close_pos;  //!< Position of closing delimiter
    } style_stack[8];
    int32_t style_depth = 0;
    MdStyle active_style = 0;

    // Current run state
    size_t run_start = pos;
    MdStyle run_style = 0;

    while (pos < end) {
        char c = gap_at(gb, pos);

        // Check for newline (ends current run but continues)
        if (c == '\n') {
            if (pos > run_start) {
                InlineRun *run = result_add_run(result);
                if (run) {
                    run->byte_start = run_start;
                    run->byte_end = pos;
                    run->style = run_style;
                    run->type = RUN_TEXT;
                }
            }
            pos++;
            run_start = pos;
            run_style = active_style;
            continue;
        }

        // Check for backslash escape
        if (c == '\\' && pos + 1 < end) {
            char next = gap_at(gb, pos + 1);
            // CommonMark escapable: ASCII punctuation !"#$%&'()*+,-./:;<=>?@[\]^_`{|}~
            // Plus \n for hard line breaks
            if ((next >= '!' && next <= '/') ||  // !"#$%&'()*+,-./
                (next >= ':' && next <= '@') ||  // :;<=>?@
                (next >= '[' && next <= '`') ||  // [\]^_`
                (next >= '{' && next <= '~') ||  // {|}~
                next == '\n') {
                // End current text run
                if (pos > run_start) {
                    InlineRun *run = result_add_run(result);
                    if (run) {
                        run->byte_start = run_start;
                        run->byte_end = pos;
                        run->style = run_style;
                        run->type = RUN_TEXT;
                    }
                }

                // Add escape run
                InlineRun *esc_run = result_add_run(result);
                if (esc_run) {
                    esc_run->byte_start = pos;
                    esc_run->byte_end = pos + 2;
                    esc_run->style = active_style;
                    esc_run->type = RUN_ESCAPE;
                    esc_run->data.escape.escaped_char = next;
                }

                pos += 2;
                run_start = pos;
                run_style = active_style;
                continue;
            }
        }

        // Check for autolink <https://...> or <email@domain>
        if (c == '<') {
            MdAutolink autolink;
            if (md_check_autolink(gb, pos, &autolink)) {
                // End current text run
                if (pos > run_start) {
                    InlineRun *run = result_add_run(result);
                    if (run) {
                        run->byte_start = run_start;
                        run->byte_end = pos;
                        run->style = run_style;
                        run->type = RUN_TEXT;
                    }
                }

                // Add autolink run
                InlineRun *auto_run = result_add_run(result);
                if (auto_run) {
                    auto_run->byte_start = (uint32_t)pos;
                    auto_run->byte_end = (uint32_t)(pos + autolink.total_len);
                    auto_run->style = active_style;
                    auto_run->type = RUN_AUTOLINK;
                    auto_run->flags = autolink.is_email ? INLINE_FLAG_IS_EMAIL : 0;
                    auto_run->data.autolink.url_start = (uint32_t)autolink.span.start;
                    auto_run->data.autolink.url_len = (uint16_t)autolink.span.len;
                }

                pos += autolink.total_len;
                run_start = pos;
                run_style = active_style;
                continue;
            }
        }

        // Check for HTML entity &nbsp; &#123; etc (skip inside code spans)
        if (c == '&' && !(active_style & MD_CODE)) {
            char utf8_buf[8];
            size_t entity_total;
            int32_t utf8_len = md_check_entity(gb, pos, utf8_buf, &entity_total);
            if (utf8_len > 0) {
                // End current text run
                if (pos > run_start) {
                    InlineRun *run = result_add_run(result);
                    if (run) {
                        run->byte_start = run_start;
                        run->byte_end = pos;
                        run->style = run_style;
                        run->type = RUN_TEXT;
                    }
                }

                // Add entity run
                InlineRun *ent_run = result_add_run(result);
                if (ent_run) {
                    ent_run->byte_start = pos;
                    ent_run->byte_end = pos + entity_total;
                    ent_run->style = active_style;
                    ent_run->type = RUN_ENTITY;
                    memcpy(ent_run->data.entity.utf8, utf8_buf, 8);
                    ent_run->data.entity.utf8_len = utf8_len;
                }

                pos += entity_total;
                run_start = pos;
                run_style = active_style;
                continue;
            }
        }

        // Check for link [text](url)
        MdMatch2 link;
        if (md_check_link(gb, pos, &link)) {
            // End current text run
            if (pos > run_start) {
                InlineRun *run = result_add_run(result);
                if (run) {
                    run->byte_start = run_start;
                    run->byte_end = pos;
                    run->style = run_style;
                    run->type = RUN_TEXT;
                }
            }

            // Add link run
            InlineRun *link_run = result_add_run(result);
            if (link_run) {
                link_run->byte_start = pos;
                link_run->byte_end = pos + link.total_len;
                link_run->style = active_style;
                link_run->type = RUN_LINK;
                link_run->data.link.text_start = link.spans[0].start;
                link_run->data.link.text_len = link.spans[0].len;
                link_run->data.link.url_start = link.spans[1].start;
                link_run->data.link.url_len = link.spans[1].len;
            }

            pos += link.total_len;
            run_start = pos;
            run_style = active_style;
            continue;
        }

        // Check for footnote reference [^id]
        MdMatch fn_ref;
        if (md_check_footnote_ref(gb, pos, &fn_ref)) {
            // End current text run
            if (pos > run_start) {
                InlineRun *run = result_add_run(result);
                if (run) {
                    run->byte_start = run_start;
                    run->byte_end = pos;
                    run->style = run_style;
                    run->type = RUN_TEXT;
                }
            }

            // Add footnote run
            InlineRun *fn_run = result_add_run(result);
            if (fn_run) {
                fn_run->byte_start = pos;
                fn_run->byte_end = pos + fn_ref.total_len;
                fn_run->style = active_style;
                fn_run->type = RUN_FOOTNOTE_REF;
                fn_run->data.footnote.id_start = fn_ref.span.start;
                fn_run->data.footnote.id_len = fn_ref.span.len;
            }

            pos += fn_ref.total_len;
            run_start = pos;
            run_style = active_style;
            continue;
        }

        // Check for inline math $...$
        MdMatch inline_math;
        if (md_check_inline_math(gb, pos, &inline_math)) {
            // End current text run
            if (pos > run_start) {
                InlineRun *run = result_add_run(result);
                if (run) {
                    run->byte_start = run_start;
                    run->byte_end = pos;
                    run->style = run_style;
                    run->type = RUN_TEXT;
                }
            }

            // Add math run
            InlineRun *math_run = result_add_run(result);
            if (math_run) {
                math_run->byte_start = pos;
                math_run->byte_end = pos + inline_math.total_len;
                math_run->style = active_style;
                math_run->type = RUN_INLINE_MATH;
                math_run->data.math.content_start = inline_math.span.start;
                math_run->data.math.content_len = inline_math.span.len;
            }

            pos += inline_math.total_len;
            run_start = pos;
            run_style = active_style;
            continue;
        }

        // Check for heading ID {#id}
        if (c == '{') {
            MdMatch hid;
            if (md_check_heading_id(gb, pos, &hid)) {
                // End current text run
                if (pos > run_start) {
                    InlineRun *run = result_add_run(result);
                    if (run) {
                        run->byte_start = run_start;
                        run->byte_end = pos;
                        run->style = run_style;
                        run->type = RUN_TEXT;
                    }
                }

                // Add heading ID run
                InlineRun *hid_run = result_add_run(result);
                if (hid_run) {
                    hid_run->byte_start = pos;
                    hid_run->byte_end = pos + hid.total_len;
                    hid_run->style = active_style;
                    hid_run->type = RUN_HEADING_ID;
                    hid_run->data.heading_id.id_start = hid.span.start;
                    hid_run->data.heading_id.id_len = hid.span.len;
                }

                pos += hid.total_len;
                run_start = pos;
                run_style = active_style;
                continue;
            }
        }

        // Check for emoji :shortcode: (skip inside code spans)
        if (!(active_style & MD_CODE)) {
            MdMatch emoji_match;
            const char *emoji_str = md_check_emoji(gb, pos, &emoji_match);
            if (emoji_str) {
                // End current text run
                if (pos > run_start) {
                    InlineRun *run = result_add_run(result);
                    if (run) {
                        run->byte_start = run_start;
                        run->byte_end = pos;
                        run->style = run_style;
                        run->type = RUN_TEXT;
                    }
                }

                // Add emoji run
                InlineRun *emoji_run = result_add_run(result);
                if (emoji_run) {
                    emoji_run->byte_start = pos;
                    emoji_run->byte_end = pos + emoji_match.total_len;
                    emoji_run->style = active_style;
                    emoji_run->type = RUN_EMOJI;
                    emoji_run->data.emoji.emoji = emoji_str;
                }

                pos += emoji_match.total_len;
                run_start = pos;
                run_style = active_style;
                continue;
            }
        }

        // Check for style delimiter (*, **, `, ~~, ==, etc.)
        size_t dlen;
        MdStyle delim = md_check_delim(gb, pos, &dlen);
        if (delim && dlen > 0) {
            // Check if this is a closing delimiter for an open style
            int32_t close_idx = -1;
            for (int32_t i = style_depth - 1; i >= 0; i--) {
                if (style_stack[i].style == delim && style_stack[i].dlen == dlen &&
                    pos == style_stack[i].close_pos) {
                    close_idx = i;
                    break;
                }
            }

            if (close_idx >= 0) {
                // This is a closing delimiter
                // End current text run
                if (pos > run_start) {
                    InlineRun *run = result_add_run(result);
                    if (run) {
                        run->byte_start = run_start;
                        run->byte_end = pos;
                        run->style = run_style;
                        run->type = RUN_TEXT;
                    }
                }

                // Add closing delimiter run
                InlineRun *delim_run = result_add_run(result);
                if (delim_run) {
                    delim_run->byte_start = pos;
                    delim_run->byte_end = (uint32_t)(pos + dlen);
                    delim_run->style = 0;
                    delim_run->type = RUN_DELIM;
                    delim_run->flags = 0;  // closing delimiter
                    delim_run->data.delim.delim_style = delim;
                    delim_run->data.delim.dlen = (uint8_t)dlen;
                }

                // Pop styles up to and including this one
                for (int32_t i = style_depth - 1; i >= close_idx; i--) {
                    active_style &= ~style_stack[i].style;
                }
                style_depth = close_idx;

                pos += dlen;
                run_start = pos;
                run_style = active_style;
                continue;
            }

            // Not a closing delimiter - check if we can open a new style
            if (!(active_style & delim) && style_depth < 8) {
                size_t close_pos = md_find_closing(gb, pos, delim, dlen);
                if (close_pos > 0) {
                    // End current text run
                    if (pos > run_start) {
                        InlineRun *run = result_add_run(result);
                        if (run) {
                            run->byte_start = run_start;
                            run->byte_end = pos;
                            run->style = run_style;
                            run->type = RUN_TEXT;
                        }
                    }

                    // Add opening delimiter run
                    InlineRun *delim_run = result_add_run(result);
                    if (delim_run) {
                        delim_run->byte_start = (uint32_t)pos;
                        delim_run->byte_end = (uint32_t)(pos + dlen);
                        delim_run->style = 0;
                        delim_run->type = RUN_DELIM;
                        delim_run->flags = INLINE_FLAG_IS_OPEN;
                        delim_run->data.delim.delim_style = delim;
                        delim_run->data.delim.dlen = (uint8_t)dlen;
                    }

                    // Push style onto stack
                    style_stack[style_depth].style = delim;
                    style_stack[style_depth].dlen = dlen;
                    style_stack[style_depth].close_pos = close_pos;
                    style_depth++;
                    active_style |= delim;

                    pos += dlen;
                    run_start = pos;
                    run_style = active_style;
                    continue;
                }
            }
        }

        // Regular character - continue current run
        pos++;
    }

    // End final run if any content remains
    if (pos > run_start) {
        InlineRun *run = result_add_run(result);
        if (run) {
            run->byte_start = run_start;
            run->byte_end = pos;
            run->style = run_style;
            run->type = RUN_TEXT;
        }
    }

    // Track unclosed styles
    for (int32_t i = 0; i < style_depth; i++) {
        result->unclosed_styles |= style_stack[i].style;
    }
}

InlineParseResult *block_parse_inline_string(const char *text, size_t len) {
    InlineParseResult *result = calloc(1, sizeof(InlineParseResult));
    if (!result) return NULL;

    // Create temporary gap buffer from string
    GapBuffer gb;
    gap_init(&gb, len + 16);
    gap_insert_str(&gb, 0, text, len);

    // Parse inline content
    parse_inline_content(result, &gb, 0, len);

    // Clean up
    gap_free(&gb);

    return result;
}

void block_parse_result_free(InlineParseResult *result) {
    if (!result) return;
    free(result->runs);
    free(result);
}

// #endregion

// #region Element Finding API

bool block_find_element_at(const BlockCache *bc, const GapBuffer *gb, size_t cursor,
                           size_t *out_start, size_t *out_len) {
    (void)gb;  // Not needed - we use block infrastructure
    if (!bc || !bc->valid || cursor == 0) return false;

    // Find block containing cursor
    Block *block = block_at_pos((BlockCache *)bc, cursor - 1);
    if (!block) return false;

    // For image blocks, cursor anywhere in block deletes whole image
    if (block->type == BLOCK_IMAGE) {
        if (cursor >= block->start && cursor <= block->end) {
            *out_start = block->start;
            *out_len = block->end - block->start;
            return true;
        }
    }

    // For blocks with inline runs, check if cursor is within any deletable element
    if (block->inline_runs && block->inline_run_count > 0) {
        for (int32_t i = 0; i < block->inline_run_count; i++) {
            const InlineRun *run = &block->inline_runs[i];
            // Check if cursor is within this element's range
            if (cursor >= run->byte_start && cursor <= run->byte_end) {
                switch (run->type) {
                    case RUN_LINK:
                    case RUN_FOOTNOTE_REF:
                    case RUN_INLINE_MATH:
                    case RUN_EMOJI:
                    case RUN_AUTOLINK:
                        *out_start = run->byte_start;
                        *out_len = run->byte_end - run->byte_start;
                        return true;
                    default:
                        break;
                }
            }
        }
    }

    return false;
}

// #endregion

// #region Table Cell API

InlineParseResult *block_parse_table_cell(const Block *block, const GapBuffer *gb,
                                          size_t cell_start, size_t cell_len) {
    if (!block || block->type != BLOCK_TABLE || cell_len == 0) return NULL;

    InlineParseResult *result = calloc(1, sizeof(InlineParseResult));
    if (!result) return NULL;

    // Parse inline content for this cell range
    parse_inline_content(result, gb, cell_start, cell_start + cell_len);

    return result;
}

// #endregion

// #region Style Application API

void block_apply_style(MdStyle s) {
    md_apply(s);
}

int32_t block_get_scale(MdStyle s) {
    return md_get_scale(s);
}

MdFracScale block_get_frac_scale(MdStyle s) {
    return md_get_frac_scale(s);
}

MdStyle block_style_for_header_level(int32_t level) {
    return md_style_for_header_level(level);
}

// #endregion
