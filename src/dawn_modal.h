// dawn_modal.h - Declarative modal form rendering macros

#ifndef DAWN_MODAL_H
#define DAWN_MODAL_H

#include "dawn_types.h"
#include "dawn_theme.h"
#include <stdio.h>

// #region Modal Rendering Macros

//! Begin a modal form at the given position
//! Sets up content area variables: content_left, content_top, content_width
#define MODAL_BEGIN(title, box_w, box_h) \
    int32_t _modal_box_width = (box_w); \
    int32_t _modal_box_height = (box_h); \
    int32_t _modal_top, _modal_left; \
    render_popup_box(_modal_box_width, _modal_box_height, &_modal_top, &_modal_left); \
    int32_t _modal_content_left = _modal_left + 2; \
    int32_t _modal_content_top = _modal_top + 1; \
    int32_t _modal_content_width = _modal_box_width - 4; \
    int32_t _modal_field_row = _modal_content_top + 2; \
    int32_t _modal_cursor_row = _modal_field_row; \
    int32_t _modal_cursor_col = _modal_content_left; \
    (void)_modal_content_width; \
    set_bg(get_modal_bg()); \
    move_to(_modal_content_top, _modal_content_left); \
    set_fg(get_dim()); \
    _modal_write_str(title)

//! Render a text field
//! @param row_offset Rows below field start
//! @param label Field label (should be padded to consistent width)
//! @param buf Character buffer
//! @param buf_len Length of content in buffer
//! @param max_display Maximum characters to display
//! @param field_idx Field index for active highlighting
#define MODAL_TEXT_FIELD(row_offset, label, buf, buf_len, max_display, field_idx) \
    do { \
        int32_t _row = _modal_field_row + (row_offset); \
        move_to(_row, _modal_content_left); \
        set_fg(app.block_edit.field == (field_idx) ? get_accent() : get_dim()); \
        _modal_write_str(label); \
        set_fg(get_fg()); \
        size_t _len = (buf_len); \
        int32_t _max = (max_display); \
        for (size_t _i = 0; _i < _len && (int32_t)_i < _max; _i++) \
            _modal_write_char((buf)[_i]); \
        if (app.block_edit.field == (field_idx)) { \
            _modal_cursor_row = _row; \
            _modal_cursor_col = _modal_content_left + (int32_t)strlen(label) + (int32_t)_len; \
        } \
    } while(0)

//! Render a size field with unit suffix (px or %)
//! @param row_offset Rows below field start
//! @param label Field label (should be padded to consistent width)
//! @param buf Character buffer with numeric value
//! @param buf_len Length of content in buffer
//! @param is_pct true for %, false for px
//! @param field_idx Field index for active highlighting
#define MODAL_SIZE_FIELD(row_offset, label, buf, buf_len, is_pct, field_idx) \
    do { \
        int32_t _row = _modal_field_row + (row_offset); \
        move_to(_row, _modal_content_left); \
        set_fg(app.block_edit.field == (field_idx) ? get_accent() : get_dim()); \
        _modal_write_str(label); \
        set_fg(get_fg()); \
        size_t _len = (buf_len); \
        for (size_t _i = 0; _i < _len; _i++) \
            _modal_write_char((buf)[_i]); \
        set_fg(get_dim()); \
        _modal_write_str((is_pct) ? "%" : "px"); \
        if (app.block_edit.field == (field_idx)) { \
            _modal_cursor_row = _row; \
            _modal_cursor_col = _modal_content_left + (int32_t)strlen(label) + (int32_t)_len; \
        } \
    } while(0)

//! Render a number field with value display
//! @param row_offset Rows below field start
//! @param label Field label
//! @param value Current numeric value
//! @param field_idx Field index for active highlighting
#define MODAL_NUMBER(row_offset, label, value, field_idx) \
    do { \
        int32_t _row = _modal_field_row + (row_offset); \
        move_to(_row, _modal_content_left); \
        set_fg(app.block_edit.field == (field_idx) ? get_accent() : get_dim()); \
        _modal_write_str(label); \
        set_fg(get_fg()); \
        char _num_buf[16]; \
        snprintf(_num_buf, sizeof(_num_buf), "%d", (int)(value)); \
        _modal_write_str(_num_buf); \
        if (app.block_edit.field == (field_idx)) { \
            set_fg(get_dim()); \
            _modal_write_str("  [-/+]"); \
            _modal_cursor_row = _row; \
            _modal_cursor_col = _modal_content_left + (int32_t)strlen(label); \
        } \
    } while(0)

//! Render a number field with suffix (e.g., "min", "sec")
//! @param row_offset Rows below field start
//! @param label Field label
//! @param value Current numeric value
//! @param suffix Unit suffix to display
//! @param field_idx Field index for active highlighting
#define MODAL_NUMBER_SUFFIX(row_offset, label, value, suffix, field_idx) \
    do { \
        int32_t _row = _modal_field_row + (row_offset); \
        move_to(_row, _modal_content_left); \
        set_fg(app.block_edit.field == (field_idx) ? get_accent() : get_dim()); \
        _modal_write_str(label); \
        set_fg(get_fg()); \
        char _num_buf[16]; \
        snprintf(_num_buf, sizeof(_num_buf), "%d", (int)(value)); \
        _modal_write_str(_num_buf); \
        set_fg(get_dim()); \
        _modal_write_str(suffix); \
        if (app.block_edit.field == (field_idx)) { \
            _modal_write_str("  [-/+]"); \
            _modal_cursor_row = _row; \
            _modal_cursor_col = _modal_content_left + (int32_t)strlen(label); \
        } \
    } while(0)

//! Render a toggle/boolean field
//! @param row_offset Rows below field start
//! @param label Field label
//! @param is_on Current boolean state
//! @param field_idx Field index for active highlighting
#define MODAL_TOGGLE(row_offset, label, is_on, field_idx) \
    do { \
        int32_t _row = _modal_field_row + (row_offset); \
        move_to(_row, _modal_content_left); \
        set_fg(app.block_edit.field == (field_idx) ? get_accent() : get_dim()); \
        _modal_write_str(label); \
        set_fg(get_fg()); \
        _modal_write_str((is_on) ? "[●] On " : "[○] Off"); \
        if (app.block_edit.field == (field_idx)) { \
            _modal_cursor_row = _row; \
            _modal_cursor_col = _modal_content_left + (int32_t)strlen(label); \
        } \
    } while(0)

//! Render a select/dropdown field with current selection
//! @param row_offset Rows below field start
//! @param label Field label
//! @param options Array of option strings
//! @param option_count Number of options
//! @param selected_idx Currently selected index
//! @param field_idx Field index for active highlighting
#define MODAL_SELECT(row_offset, label, options, option_count, selected_idx, field_idx) \
    do { \
        int32_t _row = _modal_field_row + (row_offset); \
        move_to(_row, _modal_content_left); \
        set_fg(app.block_edit.field == (field_idx) ? get_accent() : get_dim()); \
        _modal_write_str(label); \
        set_fg(get_fg()); \
        _modal_write_str("< "); \
        int32_t _sel = (selected_idx); \
        if (_sel >= 0 && _sel < (int32_t)(option_count)) \
            _modal_write_str((options)[_sel]); \
        _modal_write_str(" >"); \
        if (app.block_edit.field == (field_idx)) { \
            _modal_cursor_row = _row; \
            _modal_cursor_col = _modal_content_left + (int32_t)strlen(label); \
        } \
    } while(0)

//! Render a static label (non-editable info)
//! @param row_offset Rows below field start
//! @param label Field label
//! @param value Display value
#define MODAL_LABEL(row_offset, label, value) \
    do { \
        int32_t _row = _modal_field_row + (row_offset); \
        move_to(_row, _modal_content_left); \
        set_fg(get_dim()); \
        _modal_write_str(label); \
        set_fg(get_fg()); \
        _modal_write_str(value); \
    } while(0)

//! Render a horizontal divider line
//! @param row_offset Rows below field start
//! @param width Width of divider (0 for full content width)
#define MODAL_DIVIDER(row_offset, width) \
    do { \
        int32_t _row = _modal_field_row + (row_offset); \
        int32_t _w = (width) > 0 ? (width) : _modal_content_width; \
        move_to(_row, _modal_content_left); \
        set_fg(get_border()); \
        for (int32_t _i = 0; _i < _w; _i++) \
            _modal_write_str("─"); \
    } while(0)

//! Render a section header
//! @param row_offset Rows below field start
//! @param text Section title
#define MODAL_SECTION(row_offset, text) \
    do { \
        int32_t _row = _modal_field_row + (row_offset); \
        move_to(_row, _modal_content_left); \
        set_fg(get_accent()); \
        _modal_write_str(text); \
    } while(0)

//! Render help text at bottom of modal
//! @param row_offset Rows below content start
//! @param text Help text to display
#define MODAL_HELP(row_offset, text) \
    do { \
        move_to(_modal_content_top + (row_offset), _modal_content_left); \
        set_fg(get_dim()); \
        _modal_write_str(text); \
    } while(0)

//! End modal form and show cursor at active field
#define MODAL_END() \
    do { \
        move_to(_modal_cursor_row, _modal_cursor_col); \
        _modal_set_cursor_visible(true); \
    } while(0)

//! End modal form without showing cursor (for non-text modals)
#define MODAL_END_NO_CURSOR() \
    do { \
        _modal_set_cursor_visible(false); \
    } while(0)

// #endregion

// #region Internal Helpers (implemented in dawn_modal.c)

void _modal_write_str(const char *str);
void _modal_write_char(char c);
void _modal_set_cursor_visible(bool visible);

// #endregion

#endif // DAWN_MODAL_H
