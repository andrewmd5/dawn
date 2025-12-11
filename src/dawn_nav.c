// dawn_nav.c

#include "dawn_nav.h"
#include "dawn_gap.h"
#include "dawn_wrap.h"
#include "dawn_md.h"
#include "dawn_utils.h"

// #region Line Navigation

size_t nav_line_start(size_t pos) {
    while (pos > 0 && gap_at(&app.text, pos - 1) != '\n') pos--;
    return pos;
}

size_t nav_line_end(size_t pos) {
    size_t len = gap_len(&app.text);
    while (pos < len && gap_at(&app.text, pos) != '\n') pos++;
    return pos;
}

size_t nav_move_line(size_t pos, int32_t delta) {
    size_t col = pos - nav_line_start(pos);

    if (delta < 0) {
        for (int32_t i = 0; i < -delta && pos > 0; i++) {
            pos = nav_line_start(pos);
            if (pos > 0) pos--;
            pos = nav_line_start(pos);
        }
    } else {
        size_t len = gap_len(&app.text);
        for (int32_t i = 0; i < delta && pos < len; i++) {
            pos = nav_line_end(pos);
            if (pos < len) pos++;
        }
    }

    size_t end = nav_line_end(pos);
    size_t line_len = end - pos;
    if (col > line_len) col = line_len;
    return pos + col;
}


//! Skip leading whitespace
static size_t skip_leading_space_nav(size_t pos, size_t end) {
    while (pos < end) {
        size_t char_len;
        utf8proc_int32_t cp = gap_utf8_at(&app.text, pos, &char_len);
        if (cp != ' ') break;
        pos += char_len;
    }
    return pos;
}

size_t nav_move_visual_line(size_t pos, int32_t delta, int32_t text_width) {
    if (text_width <= 0) return pos;

    size_t len = gap_len(&app.text);
    if (len == 0) return 0;

    size_t line_start = nav_line_start(pos);
    size_t line_end = nav_line_end(pos);

    size_t seg_start = line_start;
    size_t seg_end = line_end;
    int32_t seg_num = 0;
    int32_t col_in_seg = 0;

    while (seg_start < line_end) {
        int32_t seg_width;
        seg_end = gap_find_wrap_point(&app.text, seg_start, line_end, text_width, &seg_width);

        if (pos >= seg_start && pos < seg_end) {
            col_in_seg = gap_display_width(&app.text, seg_start, pos);
            break;
        }
        if (seg_end >= line_end) break;

        seg_num++;
        seg_start = skip_leading_space_nav(seg_end, line_end);
    }

    if (delta < 0) {
        for (int32_t i = 0; i < -delta; i++) {
            if (seg_num > 0) {
                seg_start = line_start;
                int32_t target_seg = seg_num - 1;
                for (int32_t s = 0; s < target_seg && seg_start < line_end; s++) {
                    int32_t sw;
                    size_t se = gap_find_wrap_point(&app.text,seg_start, line_end, text_width, &sw);
                    if (se >= line_end) break;
                    seg_start = skip_leading_space_nav(se, line_end);
                }
                seg_end = gap_find_wrap_point(&app.text,seg_start, line_end, text_width, NULL);
                seg_num--;
            } else if (line_start > 0) {
                line_end = line_start - 1;
                line_start = nav_line_start(line_end);
                seg_start = line_start;
                seg_num = 0;
                while (seg_start < line_end) {
                    int32_t sw;
                    size_t se = gap_find_wrap_point(&app.text,seg_start, line_end, text_width, &sw);
                    if (se >= line_end) break;
                    seg_num++;
                    seg_start = skip_leading_space_nav(se, line_end);
                }
                seg_end = line_end;
            } else {
                return 0;
            }
        }
    } else {
        for (int32_t i = 0; i < delta; i++) {
            size_t next_seg_start = skip_leading_space_nav(seg_end, line_end);

            if (next_seg_start < line_end) {
                seg_start = next_seg_start;
                seg_end = gap_find_wrap_point(&app.text,seg_start, line_end, text_width, NULL);
                seg_num++;
            } else if (line_end < len) {
                line_start = line_end + 1;
                line_end = nav_line_end(line_start);
                seg_start = line_start;
                seg_end = gap_find_wrap_point(&app.text,seg_start, line_end, text_width, NULL);
                seg_num = 0;
            } else {
                // At last line - go to end of line instead of end of document
                return line_end;
            }
        }
    }

    size_t result = seg_start;
    int32_t width = 0;
    while (result < seg_end && result < len) {
        size_t next;
        int32_t gw = gap_grapheme_width(&app.text, result, &next);
        if (width + gw > col_in_seg) break;
        width += gw;
        result = next;
    }

    return result;
}

// #endregion

// #region Word Navigation

size_t nav_word_left(size_t pos) {
    if (pos == 0) return 0;
    pos--;
    while (pos > 0 && ISSPACE_(gap_at(&app.text, pos))) pos--;
    while (pos > 0 && !ISSPACE_(gap_at(&app.text, pos - 1))) pos--;
    return pos;
}

size_t nav_word_right(size_t pos) {
    size_t len = gap_len(&app.text);
    while (pos < len && !ISSPACE_(gap_at(&app.text, pos))) pos++;
    while (pos < len && ISSPACE_(gap_at(&app.text, pos))) pos++;
    return pos;
}

// #endregion

// #region Block-Aware Navigation

//! Check if position is at start of a block element and get its total length
//! @param pos position to check (should be at line start)
//! @param total_len output: total length of block element
//! @return true if position is at a block element
static bool nav_check_block_at(size_t pos, size_t *total_len) {
    MdTable tbl;
    if (md_check_table(&app.text, pos, &tbl)) {
        *total_len = tbl.total_len;
        return true;
    }

    MdMatch2 code_block;
    if (md_check_code_block(&app.text, pos, &code_block)) {
        *total_len = code_block.total_len;
        return true;
    }

    MdImageAttrs img;
    if (md_check_image(&app.text, pos, &img)) {
        *total_len = img.total_len;
        return true;
    }

    return false;
}

size_t nav_skip_block_forward(size_t pos) {
    size_t line_start = nav_line_start(pos);
    size_t block_len;

    if (nav_check_block_at(line_start, &block_len)) {
        size_t block_end = line_start + block_len;
        size_t len = gap_len(&app.text);
        if (block_end < len && gap_at(&app.text, block_end) == '\n') {
            block_end++;
        }
        return block_end < len ? block_end : len;
    }
    return pos;
}

size_t nav_skip_block_backward(size_t pos) {
    if (pos == 0) return 0;

    size_t line_start = nav_line_start(pos);
    size_t block_len;

    if (nav_check_block_at(line_start, &block_len)) {
        return line_start > 0 ? line_start - 1 : 0;
    }
    return pos;
}

size_t nav_move_visual_line_block_aware(size_t pos, int32_t delta, int32_t text_width, bool skip_blocks) {
    if (!skip_blocks) {
        return nav_move_visual_line(pos, delta, text_width);
    }

    size_t len = gap_len(&app.text);
    if (len == 0) return 0;

    size_t line_start = nav_line_start(pos);
    size_t block_len;

    if (nav_check_block_at(line_start, &block_len)) {
        if (delta > 0) {
            size_t block_end = line_start + block_len;
            if (block_end < len && gap_at(&app.text, block_end) == '\n') {
                block_end++;
            }
            pos = block_end < len ? block_end : len;
            delta--;
        } else if (delta < 0) {
            pos = line_start > 0 ? line_start - 1 : 0;
            delta++;
        }
    }

    for (int32_t i = 0; i < (delta > 0 ? delta : -delta); i++) {
        size_t new_pos = nav_move_visual_line(pos, delta > 0 ? 1 : -1, text_width);

        if (new_pos == pos) break;

        size_t new_line_start = nav_line_start(new_pos);
        if (nav_check_block_at(new_line_start, &block_len)) {
            if (delta > 0) {
                size_t block_end = new_line_start + block_len;
                if (block_end < len && gap_at(&app.text, block_end) == '\n') {
                    block_end++;
                }
                new_pos = block_end < len ? block_end : len;
            } else {
                new_pos = new_line_start > 0 ? new_line_start - 1 : 0;
            }
        }

        pos = new_pos;
    }

    return pos;
}

// #endregion

// #region Selection

void get_selection(size_t *start, size_t *end) {
    if (!app.selecting) {
        *start = *end = app.cursor;
        return;
    }
    if (app.sel_anchor < app.cursor) {
        *start = app.sel_anchor;
        *end = app.cursor;
    } else {
        *start = app.cursor;
        *end = app.sel_anchor;
    }
}

bool has_selection(void) {
    size_t s, e;
    get_selection(&s, &e);
    return s != e;
}

// #endregion
