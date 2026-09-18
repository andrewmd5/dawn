// dawn_vim.c - Neovim-style modal editing (motions + modes)
//
// Commit 2: navigation, mode switching, visual selection, command-line stub.
// Operators (d/y/c), registers and repeat arrive in the next commit.

#include "dawn_vim.h"
#include "dawn_app.h"
#include "dawn_clipboard.h"
#include "dawn_gap.h"
#include "dawn_nav.h"
#include "dawn_search.h"
#include "dawn_settings.h"
#include "dawn_utils.h"

#include <ctype.h>
#include <stdio.h>

// #region Small helpers

static bool vim_is_space(char c) { return c == ' ' || c == '\t' || c == '\n'; }

static bool vim_is_word_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

//! Classify char for w/b/e motions: 0=space, 1=word, 2=punct
static int vim_char_class(char c)
{
    if (c == '\0' || vim_is_space(c))
        return 0;
    if (vim_is_word_char(c))
        return 1;
    return 2;
}

static int vim_count_or_one(void) { return app.vim.count > 0 ? app.vim.count : 1; }

// Operator-pending flags (defined in the operators section below)
static bool pending_quote;
static bool pending_replace;
static char pending_obj;
static char pending_gcase;
static bool pending_at;

// Forward declarations (defined in later sections)
static void vim_move(size_t new_pos);
static void vim_cmd_enter(bool search);
static bool vim_cmd_execute(void);

static void vim_clear_pending(void)
{
    app.vim.pending_op = VIM_OP_NONE;
    app.vim.pending_g = false;
    app.vim.count = 0;
    app.vim.count_op = 0;
    app.vim.find_op = 0;
    pending_quote = false;
    pending_replace = false;
    pending_obj = 0;
    pending_at = false;
    pending_gcase = 0;
}

static void vim_set_status(const char* msg)
{
    if (!msg) {
        app.vim.status[0] = '\0';
        return;
    }
    snprintf(app.vim.status, sizeof(app.vim.status), "%s", msg);
}

static void vim_save_undo(void)
{
    // Mirror save_undo_state() in dawn.c (kept local to avoid new coupling)
    if (app.undo_pos < app.undo_count - 1) {
        for (int32_t i = app.undo_pos + 1; i < app.undo_count; i++)
            free(app.undo_stack[i].text);
        app.undo_count = app.undo_pos + 1;
    }
    if (app.undo_count >= MAX_UNDO) {
        free(app.undo_stack[0].text);
        memmove(&app.undo_stack[0], &app.undo_stack[1], (MAX_UNDO - 1) * sizeof(app.undo_stack[0]));
        app.undo_count--;
        app.undo_pos--;
    }
    size_t text_len = gap_len(&app.text);
    char* saved = malloc(text_len ? text_len : 1);
    if (saved) {
        if (text_len)
            gap_copy_to(&app.text, 0, text_len, saved);
        app.undo_stack[app.undo_count].text = saved;
        app.undo_stack[app.undo_count].text_len = text_len;
        app.undo_stack[app.undo_count].cursor = app.cursor;
        app.undo_count++;
        app.undo_pos = app.undo_count - 1;
    }
}

void vim_clamp_cursor(void)
{
    size_t len = gap_len(&app.text);
    if (app.cursor > len)
        app.cursor = len;
    if (app.vim.mode == VIM_NORMAL || app.vim.mode == VIM_VISUAL || app.vim.mode == VIM_VISUAL_LINE) {
        // Never rest past end of line in normal/visual (vim behavior)
        size_t le = nav_line_end(app.cursor);
        size_t ls = nav_line_start(app.cursor);
        if (app.cursor > ls && app.cursor >= le && le > ls && app.cursor < len && gap_at(&app.text, app.cursor) == '\n') {
            // cursor on newline itself: step back one grapheme
            app.cursor = gap_utf8_prev(&app.text, app.cursor);
        } else if (app.cursor == len && len > 0 && gap_at(&app.text, len - 1) != '\n') {
            // end of file without trailing newline: allow last char, not past it
            // (cursor == len is one past last char; vim keeps it on last char)
            // Keep len only when buffer ends with newline.
            app.cursor = gap_utf8_prev(&app.text, len);
        }
    }
}

// #endregion

// #region Motions

size_t vim_word_forward(size_t pos, int32_t count)
{
    size_t len = gap_len(&app.text);
    for (int32_t n = 0; n < count && pos < len; n++) {
        int cls = vim_char_class(gap_at(&app.text, pos));
        // Skip current class, then spaces, land on next word start
        while (pos < len && vim_char_class(gap_at(&app.text, pos)) == cls && cls != 0)
            pos++;
        while (pos < len && vim_char_class(gap_at(&app.text, pos)) == 0)
            pos++;
    }
    return pos > len ? len : pos;
}

size_t vim_WORD_forward(size_t pos, int32_t count)
{
    size_t len = gap_len(&app.text);
    for (int32_t n = 0; n < count && pos < len; n++) {
        while (pos < len && !vim_is_space(gap_at(&app.text, pos)))
            pos++;
        while (pos < len && vim_is_space(gap_at(&app.text, pos)))
            pos++;
    }
    return pos > len ? len : pos;
}

size_t vim_word_back(size_t pos, int32_t count)
{
    for (int32_t n = 0; n < count && pos > 0; n++) {
        pos--;
        while (pos > 0 && vim_char_class(gap_at(&app.text, pos)) == 0)
            pos--;
        int cls = vim_char_class(gap_at(&app.text, pos));
        while (pos > 0 && vim_char_class(gap_at(&app.text, pos - 1)) == cls && cls != 0)
            pos--;
    }
    return pos;
}

size_t vim_WORD_back(size_t pos, int32_t count)
{
    for (int32_t n = 0; n < count && pos > 0; n++) {
        pos--;
        while (pos > 0 && vim_is_space(gap_at(&app.text, pos)))
            pos--;
        while (pos > 0 && !vim_is_space(gap_at(&app.text, pos - 1)))
            pos--;
    }
    return pos;
}

size_t vim_word_end(size_t pos, int32_t count)
{
    size_t len = gap_len(&app.text);
    for (int32_t n = 0; n < count && pos < len; n++) {
        if (pos + 1 < len)
            pos++;
        else
            break;
        while (pos < len && vim_char_class(gap_at(&app.text, pos)) == 0)
            pos++;
        if (pos >= len)
            break;
        int cls = vim_char_class(gap_at(&app.text, pos));
        while (pos + 1 < len && vim_char_class(gap_at(&app.text, pos + 1)) == cls && cls != 0)
            pos++;
    }
    return pos > len ? len : pos;
}

size_t vim_WORD_end(size_t pos, int32_t count)
{
    size_t len = gap_len(&app.text);
    for (int32_t n = 0; n < count && pos < len; n++) {
        if (pos + 1 < len)
            pos++;
        else
            break;
        while (pos < len && vim_is_space(gap_at(&app.text, pos)))
            pos++;
        if (pos >= len)
            break;
        while (pos + 1 < len && !vim_is_space(gap_at(&app.text, pos + 1)))
            pos++;
    }
    return pos > len ? len : pos;
}

size_t vim_find_char(size_t pos, char target, bool forward, bool till, int32_t count)
{
    size_t len = gap_len(&app.text);
    size_t ls = nav_line_start(pos);
    size_t le = nav_line_end(pos);
    for (int32_t n = 0; n < count; n++) {
        bool found = false;
        if (forward) {
            size_t p = pos + 1;
            while (p < le && p < len) {
                if (gap_at(&app.text, p) == target) {
                    pos = till && p > ls ? p - 1 : p;
                    found = true;
                    break;
                }
                p++;
            }
        } else {
            if (pos == 0 || pos <= ls)
                return pos;
            size_t p = pos - 1;
            for (;;) {
                if (gap_at(&app.text, p) == target) {
                    pos = till ? p + 1 : p;
                    if (pos >= len)
                        pos = len ? len - 1 : 0;
                    found = true;
                    break;
                }
                if (p == ls)
                    break;
                p--;
            }
        }
        if (!found)
            break;
    }
    return pos;
}

size_t vim_match_bracket(size_t pos)
{
    size_t len = gap_len(&app.text);
    if (pos >= len)
        return pos;
    char c = gap_at(&app.text, pos);
    char open = 0, close = 0;
    int dir = 0;
    switch (c) {
    case '(': open = '('; close = ')'; dir = 1; break;
    case '[': open = '['; close = ']'; dir = 1; break;
    case '{': open = '{'; close = '}'; dir = 1; break;
    case ')': open = '('; close = ')'; dir = -1; break;
    case ']': open = '['; close = ']'; dir = -1; break;
    case '}': open = '{'; close = '}'; dir = -1; break;
    default: return pos;
    }
    int depth = 0;
    if (dir > 0) {
        for (size_t p = pos; p < len; p++) {
            char ch = gap_at(&app.text, p);
            if (ch == open)
                depth++;
            else if (ch == close) {
                depth--;
                if (depth == 0)
                    return p;
            }
        }
    } else {
        for (size_t p = pos + 1; p > 0; p--) {
            char ch = gap_at(&app.text, p - 1);
            if (ch == close)
                depth++;
            else if (ch == open) {
                depth--;
                if (depth == 0)
                    return p - 1;
            }
        }
    }
    return pos;
}

static bool vim_line_blank(size_t ls, size_t le)
{
    for (size_t p = ls; p < le; p++) {
        char c = gap_at(&app.text, p);
        if (c != ' ' && c != '\t')
            return false;
    }
    return true;
}

size_t vim_paragraph_forward(size_t pos, int32_t count)
{
    size_t len = gap_len(&app.text);
    for (int32_t n = 0; n < count && pos < len; n++) {
        size_t p = nav_line_end(pos);
        if (p < len)
            p++; // start of next line
        else
            break;
        // Skip to next blank-line boundary (vim } goes to next blank line)
        while (p < len) {
            size_t ls = p;
            size_t le = nav_line_end(ls);
            if (vim_line_blank(ls, le)) {
                pos = le < len ? le : len;
                break;
            }
            pos = le < len ? le + 1 : len;
            p = pos;
        }
    }
    return pos;
}

size_t vim_paragraph_back(size_t pos, int32_t count)
{
    for (int32_t n = 0; n < count && pos > 0; n++) {
        size_t p = nav_line_start(pos);
        if (p > 0)
            p--; // end of previous line
        else
            break;
        while (p > 0) {
            size_t ls = nav_line_start(p);
            size_t le = nav_line_end(ls);
            if (vim_line_blank(ls, le)) {
                pos = ls;
                break;
            }
            pos = ls;
            if (ls == 0)
                break;
            p = ls - 1;
        }
        if (p == 0)
            pos = 0;
    }
    return pos;
}

// #endregion

// #region Mode entry

bool vim_active(void) { return app.vim.enabled; }

const char* vim_mode_label(void)
{
    if (!app.vim.enabled)
        return "";
    switch (app.vim.mode) {
    case VIM_INSERT: return "-- INSERT --";
    case VIM_NORMAL: return "-- NORMAL --";
    case VIM_VISUAL: return "-- VISUAL --";
    case VIM_VISUAL_LINE: return "-- V-LINE --";
    case VIM_COMMAND: return "-- COMMAND --";
    default: return "";
    }
}

void vim_enter_insert(void)
{
    app.vim.mode = VIM_INSERT;
    app.selecting = false;
    vim_clear_pending();
    vim_set_status(NULL);
}

void vim_enter_normal(void)
{
    app.vim.mode = VIM_NORMAL;
    app.selecting = false;
    vim_clear_pending();
    vim_clamp_cursor();
}

static void vim_enter_visual(bool linewise)
{
    app.vim.mode = linewise ? VIM_VISUAL_LINE : VIM_VISUAL;
    if (!app.selecting) {
        app.selecting = true;
        app.sel_anchor = app.cursor;
        if (linewise) {
            app.sel_anchor = nav_line_start(app.cursor);
            app.cursor = nav_line_end(app.cursor);
        }
    } else if (linewise) {
        size_t s, e;
        get_selection(&s, &e);
        app.sel_anchor = nav_line_start(s);
        app.cursor = nav_line_end(e);
    }
    vim_clear_pending();
}

// #endregion

// #region Normal-mode single-key edits (x, J, ~, ., u, p)

static void vim_do_delete_char(bool before_cursor)
{
    size_t len = gap_len(&app.text);
    if (len == 0 || app.cursor >= len)
        return;
    // Recorded for . repeat by the caller (see normal 'x'/'X' below).
    vim_save_undo();
    if (before_cursor) {
        if (app.cursor == 0)
            return;
        size_t prev = gap_utf8_prev(&app.text, app.cursor);
        size_t n = app.cursor - prev;
        if (n > sizeof(app.vim.yank_buf) - 1)
            n = sizeof(app.vim.yank_buf) - 1;
        gap_copy_to(&app.text, prev, n, app.vim.yank_buf);
        app.vim.yank_buf[n] = '\0';
        app.vim.yank_len = n;
        app.vim.yank_linewise = false;
        gap_delete(&app.text, prev, n);
        app.cursor = prev;
    } else {
        if (gap_at(&app.text, app.cursor) == '\n')
            return;
        size_t next = gap_utf8_next(&app.text, app.cursor);
        size_t n = next - app.cursor;
        if (n > sizeof(app.vim.yank_buf) - 1)
            n = sizeof(app.vim.yank_buf) - 1;
        gap_copy_to(&app.text, app.cursor, n, app.vim.yank_buf);
        app.vim.yank_buf[n] = '\0';
        app.vim.yank_len = n;
        app.vim.yank_linewise = false;
        gap_delete(&app.text, app.cursor, n);
    }
    vim_clamp_cursor();
}

static void vim_do_join(void)
{
    size_t len = gap_len(&app.text);
    size_t le = nav_line_end(app.cursor);
    if (le >= len)
        return;
    int32_t count = vim_count_or_one();
    vim_save_undo();
    for (int32_t i = 0; i < count && le < gap_len(&app.text); i++) {
        // Replace newline + leading whitespace with single space
        size_t p = le + 1;
        while (p < gap_len(&app.text) && (gap_at(&app.text, p) == ' ' || gap_at(&app.text, p) == '\t'))
            p++;
        size_t del = p - le;
        // Strip trailing whitespace before newline for clean join
        size_t s = le;
        while (s > 0 && (gap_at(&app.text, s - 1) == ' ' || gap_at(&app.text, s - 1) == '\t'))
            s--;
        if (s < le) {
            gap_delete(&app.text, s, le - s);
            app.cursor = s;
            le = s;
            del = p - le;
        }
        gap_delete(&app.text, le, del);
        gap_insert(&app.text, le, ' ');
        le = nav_line_end(le + 1);
    }
}

static void vim_do_toggle_case(void)
{
    size_t len = gap_len(&app.text);
    if (app.cursor >= len)
        return;
    int32_t count = vim_count_or_one();
    vim_save_undo();
    size_t p = app.cursor;
    for (int32_t i = 0; i < count && p < len; i++) {
        char c = gap_at(&app.text, p);
        size_t next = gap_utf8_next(&app.text, p);
        if (c >= 'a' && c <= 'z') {
            gap_delete(&app.text, p, 1);
            gap_insert(&app.text, p, (char)(c - 'a' + 'A'));
        } else if (c >= 'A' && c <= 'Z') {
            gap_delete(&app.text, p, 1);
            gap_insert(&app.text, p, (char)(c - 'A' + 'a'));
        }
        p = next;
    }
    app.cursor = p > len ? len : p;
    vim_clamp_cursor();
}

static void vim_do_put(bool after)
{
    size_t reg_len = 0;
    bool reg_linewise = false;
    const char* reg_text = NULL;
    // Resolve here (vim_fetch_reg is defined in the operators section below)
    size_t unnamed = app.vim.yank_len;
    if (app.vim.yank_reg >= 'a' && app.vim.yank_reg <= 'z') {
        int r = app.vim.yank_reg - 'a';
        if (app.vim.reg_lens[r] > 0) {
            reg_text = app.vim.regs[r];
            reg_len = app.vim.reg_lens[r];
            reg_linewise = false;
        }
    }
    if (!reg_text) {
        reg_text = app.vim.yank_buf;
        reg_len = unnamed;
        reg_linewise = app.vim.yank_linewise;
    }
    (void)reg_linewise;
    if (reg_len == 0)
        return;
    int32_t total = app.vim.count > 0 ? app.vim.count : 1;
    vim_save_undo();
    bool linewise = reg_linewise;
    // A yank ending in newline pastes linewise even from charwise visual yank
    if (!linewise && reg_len > 0 && reg_text[reg_len - 1] == '\n')
        linewise = true;
    for (int32_t i = 0; i < total; i++) {
        if (linewise) {
            size_t ls = nav_line_start(app.cursor);
            size_t le = nav_line_end(app.cursor);
            size_t at = after ? (le < gap_len(&app.text) ? le + 1 : le) : ls;
            gap_insert_str(&app.text, at, reg_text, reg_len);
            app.cursor = at;
        } else {
            size_t at = app.cursor;
            if (after && at < gap_len(&app.text))
                at = gap_utf8_next(&app.text, at);
            gap_insert_str(&app.text, at, reg_text, reg_len);
            app.cursor = at + reg_len - 1;
            if (app.cursor >= gap_len(&app.text) && gap_len(&app.text) > 0)
                app.cursor = gap_len(&app.text) - 1;
        }
    }
    app.vim.count = 0;
    app.vim.yank_reg = 0;
}

static void vim_do_undo_redo(bool redo)
{
    if (redo) {
        if (app.undo_pos < app.undo_count - 1) {
            app.undo_pos++;
            size_t cl = gap_len(&app.text);
            if (cl > 0)
                gap_delete(&app.text, 0, cl);
            gap_insert_str(&app.text, 0, app.undo_stack[app.undo_pos].text, app.undo_stack[app.undo_pos].text_len);
            app.cursor = app.undo_stack[app.undo_pos].cursor;
        }
    } else {
        if (app.undo_pos > 0) {
            app.undo_pos--;
            size_t cl = gap_len(&app.text);
            if (cl > 0)
                gap_delete(&app.text, 0, cl);
            gap_insert_str(&app.text, 0, app.undo_stack[app.undo_pos].text, app.undo_stack[app.undo_pos].text_len);
            app.cursor = app.undo_stack[app.undo_pos].cursor;
        }
    }
    vim_clamp_cursor();
}

// #endregion

// #region Operators, text objects, registers, repeat

//! Pending register selection ("a etc.)
static bool pending_quote;
//! Pending replace (r + char)
static bool pending_replace;
//! Pending text-object wrapper: 0 = none, 'i' = inner, 'a' = around
static char pending_obj;
//! Pending g-case operator: 0 = none, 'u' = lowercase, 'U' = uppercase, '~' = toggle
static char pending_gcase;
//! Pending @ register (true after @ pressed, awaiting register char)
static bool pending_at;

//! Dot-repeat record for the last buffer-changing command
static bool rep_valid = false;
static VimOperator rep_op = VIM_OP_NONE;
static int32_t rep_motion = 0; //!< motion key that was used (0 = linewise dd-style or pure insert)
static char rep_obj = 0; //!< text-object char (0 = none)
static bool rep_inner = false;
static int32_t rep_count = 1;
static bool rep_linewise = false;
static char rep_case = 0; //!< g-case op for repeat ('u'/'U'/'~')
static char rep_ins[1024];
static size_t rep_ins_len = 0;
//! Insert-repeat capture (for i/a/o/s/c @ Esc)
static bool ins_rec = false;
static size_t ins_start = 0;

static int32_t vim_effective_count(void)
{
    int32_t a = app.vim.count_op > 0 ? app.vim.count_op : 1;
    int32_t b = app.vim.count > 0 ? app.vim.count : 1;
    int32_t total = a * b;
    return total > 0 ? total : 1;
}

//! Store yanked text in unnamed buffer + optional named register + clipboard
static void vim_store_yank(size_t s, size_t e, bool linewise)
{
    size_t n = e > s ? e - s : 0;
    if (n > sizeof(app.vim.yank_buf) - 1)
        n = sizeof(app.vim.yank_buf) - 1;
    if (n > 0)
        gap_copy_to(&app.text, s, n, app.vim.yank_buf);
    app.vim.yank_buf[n] = '\0';
    app.vim.yank_len = n;
    app.vim.yank_linewise = linewise;
    if (n > 0)
        clipboard_copy(app.vim.yank_buf, n);
    if (app.vim.yank_reg >= 'a' && app.vim.yank_reg <= 'z') {
        int r = app.vim.yank_reg - 'a';
        size_t rn = n < sizeof(app.vim.regs[r]) - 1 ? n : sizeof(app.vim.regs[r]) - 1;
        if (rn > 0)
            memcpy(app.vim.regs[r], app.vim.yank_buf, rn);
        app.vim.regs[r][rn] = '\0';
        app.vim.reg_lens[r] = rn;
    }
}

//! Fetch put text for the active register (or unnamed)
static const char* vim_fetch_reg(size_t* out_len, bool* out_linewise)
{
    if (app.vim.yank_reg >= 'a' && app.vim.yank_reg <= 'z') {
        int r = app.vim.yank_reg - 'a';
        if (app.vim.reg_lens[r] > 0) {
            *out_len = app.vim.reg_lens[r];
            *out_linewise = false;
            return app.vim.regs[r];
        }
    }
    *out_len = app.vim.yank_len;
    *out_linewise = app.vim.yank_linewise;
    return app.vim.yank_buf;
}

static void vim_begin_insert_repeat(void)
{
    ins_rec = true;
    ins_start = app.cursor;
}

static void vim_end_insert_repeat(void)
{
    if (!ins_rec)
        return;
    ins_rec = false;
    // Capture forward-typed text (common case); movement during insert limits this.
    if (app.cursor >= ins_start) {
        size_t n = app.cursor - ins_start;
        if (n > sizeof(rep_ins) - 1)
            n = sizeof(rep_ins) - 1;
        if (n > 0)
            gap_copy_to(&app.text, ins_start, n, rep_ins);
        rep_ins_len = n;
        rep_ins[n] = '\0';
    } else {
        rep_ins_len = 0;
        rep_ins[0] = '\0';
    }
    rep_valid = true;
}

static void vim_indent_lines(size_t s, size_t e, int dir, int32_t count)
{
    // Expand to full lines
    size_t ls = nav_line_start(s);
    size_t le = nav_line_end(e);
    if (le < gap_len(&app.text))
        le++; // include newline so line starts stay stable
    vim_save_undo();
    // Walk lines back-to-front so offsets stay valid
    size_t line_ends[1024];
    size_t line_starts[1024];
    int32_t nlines = 0;
    size_t p = ls;
    while (p < le && nlines < 1024) {
        line_starts[nlines] = p;
        size_t lend = nav_line_end(p);
        line_ends[nlines] = lend;
        nlines++;
        p = lend < gap_len(&app.text) ? lend + 1 : gap_len(&app.text);
        if (p >= le)
            break;
    }
    for (int32_t i = nlines - 1; i >= 0; i--) {
        if (dir > 0) {
            for (int32_t k = 0; k < 2 * count; k++)
                gap_insert(&app.text, line_starts[i], ' ');
        } else {
            size_t lend = line_ends[i] + (int32_t)(0); // silence unused in some configs
            (void)lend;
            for (int32_t k = 0; k < 2 * count && line_starts[i] < gap_len(&app.text); k++) {
                if (gap_at(&app.text, line_starts[i]) == ' ')
                    gap_delete(&app.text, line_starts[i], 1);
                else
                    break;
            }
        }
    }
    app.cursor = nav_line_start(ls);
    vim_clamp_cursor();
}

static void vim_case_range(size_t s, size_t e, char kind)
{
    if (e <= s)
        return;
    vim_save_undo();
    size_t len = gap_len(&app.text);
    if (e > len)
        e = len;
    for (size_t p = s; p < e;) {
        size_t next = gap_utf8_next(&app.text, p);
        char c = gap_at(&app.text, p);
        char nc = c;
        if (kind == 'u' && c >= 'A' && c <= 'Z')
            nc = (char)(c - 'A' + 'a');
        else if (kind == 'U' && c >= 'a' && c <= 'z')
            nc = (char)(c - 'a' + 'A');
        else if (kind == '~') {
            if (c >= 'a' && c <= 'z')
                nc = (char)(c - 'a' + 'A');
            else if (c >= 'A' && c <= 'Z')
                nc = (char)(c - 'A' + 'a');
        }
        if (nc != c) {
            gap_delete(&app.text, p, next - p);
            gap_insert(&app.text, p, nc);
        }
        p = p + 1; // ascii case ops advance one byte (multibyte left untouched)
        if (p >= gap_len(&app.text))
            break;
    }
}

//! Resolve a text object around pos. Returns false if no object.
static bool vim_text_object(size_t pos, bool inner, char obj, size_t* out_s, size_t* out_e)
{
    size_t len = gap_len(&app.text);
    if (len == 0)
        return false;
    if (pos >= len)
        pos = len - 1;
    if (obj == 'w' || obj == 'W') {
        bool big = (obj == 'W');
        size_t s = pos, e = pos;
        if (big) {
            while (s > 0 && !vim_is_space(gap_at(&app.text, s - 1)))
                s--;
            while (e < len && !vim_is_space(gap_at(&app.text, e)))
                e++;
        } else {
            int cls = vim_char_class(gap_at(&app.text, pos));
            if (cls == 0)
                return false;
            while (s > 0 && vim_char_class(gap_at(&app.text, s - 1)) == cls)
                s--;
            while (e < len && vim_char_class(gap_at(&app.text, e)) == cls)
                e++;
        }
        if (!inner) {
            while (e < len && vim_is_space(gap_at(&app.text, e)) && gap_at(&app.text, e) != '\n')
                e++;
            if (e == (inner ? e : e) && e < len && gap_at(&app.text, e) == '\n')
                e++;
        }
        *out_s = s;
        *out_e = e;
        return e > s;
    }
    if (obj == 'p') {
        // paragraph object
        size_t s = vim_paragraph_back(pos, 1);
        size_t e = vim_paragraph_forward(pos, 1);
        if (inner) {
            // shrink off surrounding blank lines: use current paragraph core
            s = nav_line_start(pos);
            while (s > 0) {
                size_t ps = nav_line_start(s - 1);
                size_t pe = nav_line_end(ps);
                bool blank = true;
                for (size_t q = ps; q < pe; q++)
                    if (gap_at(&app.text, q) != ' ' && gap_at(&app.text, q) != '\t')
                        blank = false;
                if (blank)
                    break;
                s = ps;
            }
            e = nav_line_end(pos);
            if (e < len)
                e++;
        }
        *out_s = s;
        *out_e = e > s ? e : s + 1;
        return true;
    }
    // Paired delimiters
    char open = 0, close = 0;
    switch (obj) {
    case '(': case ')': case 'b': open = '('; close = ')'; break;
    case '[': case ']': open = '['; close = ']'; break;
    case '{': case '}': case 'B': open = '{'; close = '}'; break;
    case '<': case '>': open = '<'; close = '>'; break;
    case '"': case '\'': case '`': open = close = obj; break;
    default: return false;
    }
    if (open == close) {
        // Quote object: nearest pair around cursor on this line
        size_t ls = nav_line_start(pos), le = nav_line_end(pos);
        size_t l = pos, r = pos;
        bool fl = false, fr = false;
        while (l > ls) {
            l--;
            if (gap_at(&app.text, l) == open) {
                fl = true;
                break;
            }
        }
        while (r < le) {
            if (gap_at(&app.text, r) == open) {
                fr = true;
                break;
            }
            r++;
        }
        if (!fl || !fr)
            return false;
        *out_s = inner ? l + 1 : l;
        *out_e = inner ? r : (r + 1 <= len ? r + 1 : r);
        return *out_e > *out_s;
    }
    // Bracket object: scan for enclosing pair
    int depth = 0;
    size_t l = pos;
    bool found_l = false;
    size_t q = pos + 1;
    while (q > 0) {
        q--;
        char c = gap_at(&app.text, q);
        if (c == close)
            depth++;
        else if (c == open) {
            if (depth == 0) {
                l = q;
                found_l = true;
                break;
            }
            depth--;
        }
        if (q == 0)
            break;
    }
    if (!found_l)
        return false;
    depth = 0;
    size_t r = pos;
    bool found_r = false;
    for (size_t k = l + 1; k < len; k++) {
        char c = gap_at(&app.text, k);
        if (c == open)
            depth++;
        else if (c == close) {
            if (depth == 0) {
                r = k;
                found_r = true;
                break;
            }
            depth--;
        }
    }
    if (!found_r)
        return false;
    *out_s = inner ? l + 1 : l;
    *out_e = inner ? r : (r + 1 <= len ? r + 1 : r);
    return *out_e > *out_s;
}

//! Compute target of a single-key motion from pos with count. Sets *linewise.
static size_t vim_motion_target(int32_t key, size_t pos, int32_t count, bool* linewise)
{
    *linewise = false;
    switch (key) {
    case 'h': {
        size_t p = pos;
        for (int32_t i = 0; i < count; i++)
            p = gap_utf8_prev(&app.text, p);
        return p;
    }
    case 'l':
    case ' ':
        // inclusive: operator on l includes target char
        for (int32_t i = 0; i < count; i++)
            pos = gap_utf8_next(&app.text, pos);
        return pos;
    case 'j':
        *linewise = true;
        return nav_move_line(pos, count);
    case 'k':
        *linewise = true;
        return nav_move_line(pos, -count);
    case 'w': return vim_word_forward(pos, count);
    case 'W': return vim_WORD_forward(pos, count);
    case 'b': return vim_word_back(pos, count);
    case 'B': return vim_WORD_back(pos, count);
    case 'e': return vim_word_end(pos, count);
    case 'E': return vim_WORD_end(pos, count);
    case '0': return nav_line_start(pos);
    case '^': {
        size_t ls = nav_line_start(pos), le = nav_line_end(pos), p = ls;
        while (p < le && (gap_at(&app.text, p) == ' ' || gap_at(&app.text, p) == '\t'))
            p++;
        return p;
    }
    case '$': {
        size_t le = nav_line_end(pos);
        // inclusive motion: operator grabs through EOL char position
        return le;
    }
    case '{': return vim_paragraph_back(pos, count);
    case '}': return vim_paragraph_forward(pos, count);
    case 'G':
        *linewise = true;
        if (count > 0) {
            size_t p = 0;
            for (int32_t i = 1; i < count; i++) {
                size_t le = nav_line_end(p);
                if (le >= gap_len(&app.text))
                    break;
                p = le + 1;
            }
            return nav_line_start(p);
        }
        return gap_len(&app.text);
    case '%': return vim_match_bracket(pos);
    case ';':
    case ',':
        if (app.vim.last_find) {
            bool fwd = (key == ';') ? !app.vim.last_find_back : app.vim.last_find_back;
            return vim_find_char(pos, app.vim.last_find, fwd, app.vim.last_find_t, count);
        }
        return pos;
    default: return pos;
    }
}

//! Delete charwise range [s,e) (e exclusive, except $/e inclusive callers pass e+1)
static void vim_delete_range(size_t s, size_t e)
{
    if (e <= s)
        return;
    vim_save_undo();
    vim_store_yank(s, e, false);
    gap_delete(&app.text, s, e - s);
    app.cursor = s;
    vim_clamp_cursor();
}

//! Delete linewise range covering lines intersecting [s,e)
static void vim_delete_lines(size_t s, size_t e)
{
    size_t ls = nav_line_start(s);
    size_t le = nav_line_end(e);
    size_t len = gap_len(&app.text);
    if (le < len)
        le++; // grab newline
    else if (ls > 0 && gap_at(&app.text, ls - 1) == '\n') {
        // last line without newline: keep cursor sane
    }
    if (le <= ls)
        return;
    vim_save_undo();
    vim_store_yank(ls, le, true);
    gap_delete(&app.text, ls, le - ls);
    app.cursor = ls;
    size_t nlen = gap_len(&app.text);
    if (app.cursor > nlen)
        app.cursor = nlen;
    if (app.cursor < nlen && gap_at(&app.text, app.cursor) == '\n' && app.cursor > 0)
        app.cursor = nav_line_start(app.cursor);
    vim_clamp_cursor();
}

//! Execute pending operator on target. motion_key identifies the motion for repeat.
static void vim_exec_operator(size_t target, bool motion_linewise, int32_t motion_key)
{
    VimOperator op = app.vim.pending_op;
    char gcase = pending_gcase;
    int32_t total = vim_effective_count();
    // Recompute motion with combined count for d2w/2dw equivalence
    if (motion_key && total > 1 && (app.vim.count > 0 || app.vim.count_op > 0)) {
        bool lw = false;
        size_t recomputed = vim_motion_target(motion_key, app.cursor, total, &lw);
        // Only adopt recompute when it moves further (avoids $/0 distortion)
        if (motion_key == 'w' || motion_key == 'W' || motion_key == 'e' || motion_key == 'E' || motion_key == 'j' || motion_key == 'k' || motion_key == 'G') {
            target = recomputed;
            motion_linewise = lw;
        }
    }
    size_t cur = app.cursor;
    bool linewise = motion_linewise;
    if (motion_key == 'G' || motion_key == 'j' || motion_key == 'k')
        linewise = true;

    // Normalize charwise range (exclusive vs inclusive)
    size_t s = cur < target ? cur : target;
    size_t e = cur < target ? target : cur;
    bool inclusive = (motion_key == 'e' || motion_key == 'E' || motion_key == '$' || motion_key == '%' || motion_key == 'l' || motion_key == ' ');
    if (!linewise && e > s && inclusive && e < gap_len(&app.text))
        e = gap_utf8_next(&app.text, e);

    switch (op) {
    case VIM_OP_DELETE:
        if (linewise)
            vim_delete_lines(s, e);
        else
            vim_delete_range(s, e);
        rep_valid = true;
        rep_op = VIM_OP_DELETE;
        rep_motion = motion_key;
        rep_count = total;
        rep_linewise = linewise;
        rep_ins_len = 0;
        break;
    case VIM_OP_YANK: {
        if (linewise) {
            size_t ls = nav_line_start(s), le = nav_line_end(e);
            size_t len = gap_len(&app.text);
            if (le < len)
                le++;
            vim_store_yank(ls, le, true);
            vim_set_status("yanked");
        } else {
            vim_store_yank(s, e, false);
            vim_set_status("yanked");
        }
        app.cursor = s;
        vim_clamp_cursor();
        break;
    }
    case VIM_OP_CHANGE: {
        if (linewise) {
            size_t ls = nav_line_start(s), le = nav_line_end(e);
            size_t len = gap_len(&app.text);
            if (le < len)
                le++;
            vim_save_undo();
            vim_store_yank(ls, le, true);
            gap_delete(&app.text, ls, le - ls);
            app.cursor = ls;
            // Preserve indent of first line for cc-like changes
            rep_op = VIM_OP_CHANGE;
            rep_motion = motion_key;
            rep_count = total;
            rep_linewise = true;
            vim_enter_insert();
            vim_begin_insert_repeat();
        } else {
            if (e <= s) {
                vim_enter_insert();
                vim_begin_insert_repeat();
                rep_op = VIM_OP_CHANGE;
                rep_motion = motion_key;
                rep_count = total;
                rep_linewise = false;
                break;
            }
            vim_save_undo();
            vim_store_yank(s, e, false);
            gap_delete(&app.text, s, e - s);
            app.cursor = s;
            rep_op = VIM_OP_CHANGE;
            rep_motion = motion_key;
            rep_count = total;
            rep_linewise = false;
            rep_case = 0;
            vim_enter_insert();
            vim_begin_insert_repeat();
        }
        break;
    }
    case VIM_OP_INDENT_RIGHT:
    case VIM_OP_INDENT_LEFT: {
        int dir = (op == VIM_OP_INDENT_RIGHT) ? 1 : -1;
        vim_indent_lines(s, e, dir, total);
        rep_valid = true;
        rep_op = op;
        rep_motion = motion_key;
        rep_count = total;
        rep_linewise = true;
        break;
    }
    case VIM_OP_TOGGLE_CASE: {
        char kind = gcase ? gcase : '~';
        if (linewise) {
            size_t ls = nav_line_start(s), le = nav_line_end(e);
            vim_case_range(ls, le, kind);
        } else {
            vim_case_range(s, e, kind);
        }
        app.cursor = s;
        vim_clamp_cursor();
        rep_valid = true;
        rep_op = VIM_OP_TOGGLE_CASE;
        rep_motion = motion_key;
        rep_count = total;
        rep_linewise = linewise;
        rep_case = kind;
        break;
    }
    default:
        break;
    }
    app.vim.yank_reg = 0;
    pending_gcase = 0;
    vim_clear_pending();
    pending_obj = 0;
}

//! Operator + motion dispatcher from normal mode. Returns true if consumed.
static bool vim_op_or_move(int32_t motion_key, size_t target, bool linewise)
{
    if (app.vim.pending_op == VIM_OP_NONE && !pending_gcase) {
        vim_move(target);
        app.vim.count = 0;
        return true;
    }
    if (pending_gcase && app.vim.pending_op == VIM_OP_NONE)
        app.vim.pending_op = VIM_OP_TOGGLE_CASE;
    vim_exec_operator(target, linewise, motion_key);
    return true;
}

static void vim_start_operator(VimOperator op)
{
    if (app.vim.pending_op == op) {
        // Doubled operator: linewise on current lines (dd/yy/cc/>>/<<)
        int32_t total = vim_effective_count();
        size_t cur = app.cursor;
        for (int32_t i = 1; i < total; i++) {
            cur = nav_move_line(cur, 1);
        }
        size_t s = nav_line_start(app.cursor);
        size_t e = nav_line_end(cur);
        VimOperator saved = op;
        if (saved == VIM_OP_DELETE) {
            vim_delete_lines(s, e);
            rep_valid = true;
            rep_op = VIM_OP_DELETE;
            rep_motion = 'd';
            rep_count = total;
            rep_linewise = true;
            rep_ins_len = 0;
        } else if (saved == VIM_OP_YANK) {
            size_t len = gap_len(&app.text);
            size_t le = e < len ? e + 1 : e;
            vim_store_yank(s, le, true);
            vim_set_status("yanked");
            app.cursor = s;
            vim_clamp_cursor();
        } else if (saved == VIM_OP_CHANGE) {
            size_t len = gap_len(&app.text);
            size_t le = e < len ? e + 1 : e;
            vim_save_undo();
            vim_store_yank(s, le, true);
            gap_delete(&app.text, s, le - s);
            app.cursor = s;
            rep_op = VIM_OP_CHANGE;
            rep_motion = 'c';
            rep_count = total;
            rep_linewise = true;
            vim_enter_insert();
            vim_begin_insert_repeat();
            return;
        } else if (saved == VIM_OP_INDENT_RIGHT || saved == VIM_OP_INDENT_LEFT) {
            vim_indent_lines(s, e, saved == VIM_OP_INDENT_RIGHT ? 1 : -1, total);
            rep_valid = true;
            rep_op = saved;
            rep_motion = saved == VIM_OP_INDENT_RIGHT ? '>' : '<';
            rep_count = total;
            rep_linewise = true;
        }
        app.vim.yank_reg = 0;
        vim_clear_pending();
        pending_obj = 0;
        return;
    }
    if (app.vim.count > 0)
        app.vim.count_op = app.vim.count;
    app.vim.count = 0;
    app.vim.pending_op = op;
}

//! Replay last change with .
static void vim_repeat_last(void)
{
    if (!rep_valid) {
        vim_set_status("nothing to repeat");
        return;
    }
    if (rep_op == VIM_OP_NONE) {
        if (rep_motion == 'x' || rep_motion == 'X') {
            for (int32_t i = 0; i < rep_count; i++)
                vim_do_delete_char(rep_motion == 'X');
            return;
        }
        if (rep_motion == 'J') {
            for (int32_t i = 0; i < rep_count; i++)
                vim_do_join();
            return;
        }
        if (rep_motion == 'r' && rep_ins_len == 1) {
            char rc = rep_ins[0];
            size_t len = gap_len(&app.text);
            if (app.cursor < len && gap_at(&app.text, app.cursor) != '\n') {
                vim_save_undo();
                for (int32_t i = 0; i < rep_count && app.cursor < gap_len(&app.text); i++) {
                    if (gap_at(&app.text, app.cursor) == '\n')
                        break;
                    size_t nx = gap_utf8_next(&app.text, app.cursor);
                    gap_delete(&app.text, app.cursor, nx - app.cursor);
                    gap_insert(&app.text, app.cursor, rc);
                    app.cursor = gap_utf8_next(&app.text, app.cursor);
                }
                vim_clamp_cursor();
            }
            return;
        }
        // Pure insert repeat (i/a/o)
        if (rep_ins_len == 0) {
            vim_set_status("nothing to repeat");
            return;
        }
        vim_save_undo();
        for (int32_t i = 0; i < rep_count; i++) {
            gap_insert_str(&app.text, app.cursor, rep_ins, rep_ins_len);
            app.cursor += rep_ins_len;
        }
        return;
    }
    // Save current pending state, install repeat descriptor, execute
    VimOperator saved_op = app.vim.pending_op;
    int32_t saved_c = app.vim.count, saved_co = app.vim.count_op;
    char saved_gcase = pending_gcase;
    app.vim.pending_op = rep_op;
    pending_gcase = rep_case;
    app.vim.count = rep_count;
    app.vim.count_op = 0;
    if (rep_obj) {
        size_t s, e;
        if (vim_text_object(app.cursor, rep_inner, rep_obj, &s, &e)) {
            if (rep_op == VIM_OP_DELETE)
                vim_delete_range(s, e);
            else if (rep_op == VIM_OP_CHANGE) {
                vim_save_undo();
                vim_store_yank(s, e, false);
                gap_delete(&app.text, s, e - s);
                app.cursor = s;
                gap_insert_str(&app.text, app.cursor, rep_ins, rep_ins_len);
                app.cursor += rep_ins_len;
                vim_clamp_cursor();
            } else if (rep_op == VIM_OP_YANK) {
                vim_store_yank(s, e, false);
            }
        }
        app.vim.pending_op = saved_op;
        app.vim.count = saved_c;
        app.vim.count_op = saved_co;
        pending_gcase = saved_gcase;
        vim_clear_pending();
        pending_obj = 0;
        return;
    }
    bool lw = false;
    size_t target = rep_motion ? vim_motion_target(rep_motion, app.cursor, rep_count, &lw) : app.cursor;
    if (rep_linewise)
        lw = true;
    // vim_exec_operator clears pending; stash insert text for change first
    char saved_ins[1024];
    size_t saved_ins_len = rep_ins_len;
    if (saved_ins_len)
        memcpy(saved_ins, rep_ins, saved_ins_len);
    VimOperator rop = rep_op;
    vim_exec_operator(target, lw, rep_motion);
    if (rop == VIM_OP_CHANGE && saved_ins_len > 0) {
        // We are now in insert mode from exec; drop back to normal and place text
        gap_insert_str(&app.text, app.cursor, saved_ins, saved_ins_len);
        app.cursor += saved_ins_len;
        ins_rec = false; // don't overwrite repeat with the replay itself
        vim_enter_normal();
        // restore repeat record (vim_enter_normal cleared pending, not rep_*)
        rep_valid = true;
        rep_op = rop;
    }
    (void)saved_op;
}

// #endregion

// #region Insert / open-line helpers (need text-width-free logic)

static void vim_open_line(bool below)
{
    vim_save_undo();
    if (below) {
        size_t le = nav_line_end(app.cursor);
        app.cursor = le;
        gap_insert(&app.text, app.cursor, '\n');
        app.cursor++;
    } else {
        size_t ls = nav_line_start(app.cursor);
        app.cursor = ls;
        gap_insert(&app.text, app.cursor, '\n');
        // cursor stays on the new empty line (which is at ls)
    }
    vim_enter_insert();
}

// #endregion

// #region Command line (: and /)

static void vim_cmd_enter(bool search)
{
    app.vim.mode = VIM_COMMAND;
    app.vim.cmd_search = search;
    app.vim.cmdline_len = 0;
    app.vim.cmdline[0] = '\0';
}

static bool vim_cmd_execute(void)
{
    // Minimal : commands for this commit; full set lands with operators commit.
    app.vim.cmdline[app.vim.cmdline_len] = '\0';
    if (app.vim.cmd_search) {
        // Feed into document search overlay
        if (!app.search_state) {
            app.search_state = malloc(sizeof(SearchState));
            if (app.search_state)
                search_init((SearchState*)app.search_state);
        }
        if (app.search_state && app.vim.cmdline_len > 0) {
            SearchState* s = (SearchState*)app.search_state;
            size_t n = app.vim.cmdline_len < SEARCH_MAX_QUERY - 1 ? app.vim.cmdline_len : SEARCH_MAX_QUERY - 1;
            memcpy(s->query, app.vim.cmdline, n);
            s->query[n] = '\0';
            s->query_len = (int32_t)n;
            s->selected = 0;
            s->scroll = 0;
            search_mark_dirty(s, 0);
            app.mode = MODE_SEARCH;
            app.prev_mode = MODE_WRITING;
        }
        vim_enter_normal();
        return true;
    }
    // : commands
    if (app.vim.cmdline_len == 0) {
        vim_enter_normal();
        return true;
    }
    snprintf(app.vim.last_cmd, sizeof(app.vim.last_cmd), "%s", app.vim.cmdline);
    app.vim.last_cmd_len = app.vim.cmdline_len;
    const char* c = app.vim.cmdline;
    if (strncmp(c, "wq", 2) == 0 || strncmp(c, "x", 1) == 0) {
        dawn_save_document();
        if (c[0] == 'x' || c[2] != '\0' || c[0] == 'w')
            dawn_request_quit();
        else
            vim_set_status("written");
    } else if (c[0] == 'w') {
        dawn_save_document();
        vim_set_status("written");
    } else if (c[0] == 'q') {
        if (app.vim.cmdline_len >= 2 && c[1] == '!') {
            dawn_request_quit();
        } else {
            dawn_save_document();
            app.mode = MODE_WELCOME;
        }
    } else if (strncmp(c, "noh", 3) == 0 || strncmp(c, "nohlsearch", 10) == 0) {
        if (app.search_state) {
            SearchState* s = (SearchState*)app.search_state;
            s->query[0] = '\0';
            s->query_len = 0;
            s->count = 0;
        }
        vim_set_status("search cleared");
    } else if (strncmp(c, "set ", 4) == 0) {
        if (strstr(c + 4, "novim")) {
            app.vim.enabled = false;
            app.vim.mode = VIM_INSERT;
            vim_set_status(NULL);
            settings_save();
            return true;
        } else if (strstr(c + 4, "vim")) {
            vim_set_status("vim on");
        } else {
            vim_set_status("unknown option");
        }
    } else {
        vim_set_status("not an editor command");
    }
    vim_enter_normal();
    // keep status message visible on mode line
    return true;
}

static bool vim_handle_command_key(int32_t key)
{
    switch (key) {
    case 0x1b: // Esc cancels
        vim_enter_normal();
        return true;
    case '\r':
    case '\n':
        return vim_cmd_execute();
    case 127:
    case 8:
        if (app.vim.cmdline_len > 0) {
            app.vim.cmdline_len = (size_t)str_utf8_prev(app.vim.cmdline, (int32_t)app.vim.cmdline_len);
            app.vim.cmdline[app.vim.cmdline_len] = '\0';
        } else {
            vim_enter_normal();
        }
        return true;
    default:
        if (key >= 32 && key < 127 && app.vim.cmdline_len < VIM_CMDLINE_MAX - 1) {
            app.vim.cmdline[app.vim.cmdline_len++] = (char)key;
            app.vim.cmdline[app.vim.cmdline_len] = '\0';
        }
        return true;
    }
}

// #endregion

// #region Normal / visual dispatch

static void vim_move(size_t new_pos)
{
    app.cursor = new_pos;
    vim_clamp_cursor();
}

static bool vim_handle_g_prefix(int32_t key)
{
    // Called when pending_g is set. Returns true if key consumed as g-motion.
    int32_t count = vim_count_or_one();
    switch (key) {
    case 'g': {
        bool lw = true;
        if (app.vim.pending_op != VIM_OP_NONE || pending_gcase)
            return vim_op_or_move('g', 0, lw);
        vim_move(0);
        vim_clear_pending();
        return true;
    }
    case 'j': {
        size_t t = nav_move_line(app.cursor, count);
        if (app.vim.pending_op != VIM_OP_NONE || pending_gcase)
            return vim_op_or_move('j', t, true);
        vim_move(t);
        vim_clear_pending();
        return true;
    }
    case 'k': {
        size_t t = nav_move_line(app.cursor, -count);
        if (app.vim.pending_op != VIM_OP_NONE || pending_gcase)
            return vim_op_or_move('k', t, true);
        vim_move(t);
        vim_clear_pending();
        return true;
    }
    case 'u':
    case 'U':
    case '~':
        // g-case operator: awaits motion (guw, gUap, g~~ not needed; ~~ handled via ~)
        pending_gcase = (char)key;
        if (app.vim.pending_op == VIM_OP_NONE)
            app.vim.pending_op = VIM_OP_TOGGLE_CASE;
        app.vim.pending_g = false;
        return true;
    default:
        vim_set_status("unknown g motion");
        app.vim.pending_g = false;
        return true;
    }
}

static bool vim_handle_normal_key(int32_t key)
{
    // Numeric prefix
    if (key >= '0' && key <= '9') {
        if (key == '0' && app.vim.count == 0) {
            vim_move(nav_line_start(app.cursor));
            return true;
        }
        app.vim.count = app.vim.count * 10 + (key - '0');
        if (app.vim.count > 9999)
            app.vim.count = 9999;
        return true;
    }

    // Pending f/t target
    if (app.vim.find_op) {
        if (key >= 32 && key < 127) {
            char op = app.vim.find_op;
            app.vim.find_op = 0;
            bool fwd = (op == 'f' || op == 't');
            bool till = (op == 't' || op == 'T');
            int32_t count = vim_count_or_one();
            size_t np = vim_find_char(app.cursor, (char)key, fwd, till, count);
            app.vim.last_find = (char)key;
            app.vim.last_find_t = till;
            app.vim.last_find_back = !fwd;
            if (app.vim.pending_op != VIM_OP_NONE || pending_gcase)
                return vim_op_or_move((int32_t)op, np, false);
            vim_move(np);
            app.vim.count = 0;
            return true;
        }
        app.vim.find_op = 0;
        return true;
    }

    if (app.vim.pending_g)
        return vim_handle_g_prefix(key);

    // @ register: only @: (repeat last : command) is supported
    if (pending_at) {
        pending_at = false;
        if (key == ':') {
            if (app.vim.last_cmd_len > 0) {
                size_t n = app.vim.last_cmd_len < VIM_CMDLINE_MAX - 1 ? app.vim.last_cmd_len : VIM_CMDLINE_MAX - 1;
                memcpy(app.vim.cmdline, app.vim.last_cmd, n);
                app.vim.cmdline[n] = '\0';
                app.vim.cmdline_len = n;
                app.vim.cmd_search = false;
                return vim_cmd_execute();
            }
            vim_set_status("nothing to repeat");
        } else {
            vim_set_status("only @: supported");
        }
        vim_clear_pending();
        return true;
    }

    // Register selection: "a — next key names the register
    if (pending_quote) {
        pending_quote = false;
        if ((key >= 'a' && key <= 'z') || key == '"') {
            app.vim.yank_reg = key == '"' ? 0 : (char)key;
            vim_set_status(NULL);
        } else {
            vim_set_status("invalid register");
        }
        return true;
    }

    // Replace: r + char
    if (pending_replace) {
        pending_replace = false;
        if (key >= 32 && key < 127) {
            int32_t total = vim_effective_count();
            size_t len = gap_len(&app.text);
            if (app.cursor < len && gap_at(&app.text, app.cursor) != '\n') {
                vim_save_undo();
                for (int32_t i = 0; i < total && app.cursor < gap_len(&app.text); i++) {
                    if (gap_at(&app.text, app.cursor) == '\n')
                        break;
                    size_t nx = gap_utf8_next(&app.text, app.cursor);
                    gap_delete(&app.text, app.cursor, nx - app.cursor);
                    char rc = (char)key;
                    gap_insert(&app.text, app.cursor, rc);
                    app.cursor = gap_utf8_next(&app.text, app.cursor);
                }
                vim_clamp_cursor();
                rep_valid = true;
                rep_op = VIM_OP_NONE;
                rep_motion = 'r';
                rep_count = total;
                rep_ins_len = 1;
                rep_ins[0] = (char)key;
                rep_ins[1] = '\0';
            }
        }
        vim_clear_pending();
        return true;
    }

    // Text object after operator: i/a + object
    if (pending_obj) {
        char mode = pending_obj;
        pending_obj = 0;
        size_t s, e;
        if (vim_text_object(app.cursor, mode == 'i', (char)key, &s, &e)) {
            VimOperator op = app.vim.pending_op;
            if (pending_gcase && op == VIM_OP_NONE)
                op = VIM_OP_TOGGLE_CASE;
            int32_t total = vim_effective_count();
            (void)total;
            if (op == VIM_OP_DELETE) {
                vim_delete_range(s, e);
                rep_valid = true;
                rep_op = VIM_OP_DELETE;
                rep_motion = 0;
                rep_obj = (char)key;
                rep_inner = (mode == 'i');
                rep_count = total;
                rep_ins_len = 0;
                vim_clear_pending();
            } else if (op == VIM_OP_YANK) {
                vim_store_yank(s, e, false);
                vim_set_status("yanked");
                app.cursor = s;
                vim_clamp_cursor();
                vim_clear_pending();
            } else if (op == VIM_OP_CHANGE) {
                vim_save_undo();
                vim_store_yank(s, e, false);
                gap_delete(&app.text, s, e - s);
                app.cursor = s;
                rep_op = VIM_OP_CHANGE;
                rep_motion = 0;
                rep_obj = (char)key;
                rep_inner = (mode == 'i');
                rep_count = total;
                vim_enter_insert();
                vim_begin_insert_repeat();
                return true;
            } else if (op == VIM_OP_TOGGLE_CASE) {
                vim_case_range(s, e, pending_gcase ? pending_gcase : '~');
                app.cursor = s;
                vim_clamp_cursor();
                rep_valid = true;
                rep_op = VIM_OP_TOGGLE_CASE;
                rep_obj = (char)key;
                rep_inner = (mode == 'i');
                rep_case = pending_gcase ? pending_gcase : '~';
                vim_clear_pending();
                pending_gcase = 0;
                return true;
            } else {
                vim_clear_pending();
            }
        } else {
            vim_set_status("no text object");
            vim_clear_pending();
            pending_gcase = 0;
        }
        return true;
    }

    // i/a after pending operator starts a text object
    if (app.vim.pending_op != VIM_OP_NONE && (key == 'i' || key == 'a')) {
        pending_obj = (char)key;
        return true;
    }

    // f/F/t/T pending acquisition
    if (key == 'f' || key == 'F' || key == 't' || key == 'T') {
        app.vim.find_op = (char)key;
        return true;
    }

    int32_t count = vim_count_or_one();

    switch (key) {
    case 0x1b:
        vim_clear_pending();
        vim_set_status(NULL);
        return true;
    case 'g':
        app.vim.pending_g = true;
        return true;
    case 'h':
    case DAWN_KEY_LEFT: {
        size_t t = app.cursor;
        for (int32_t i = 0; i < count; i++)
            t = gap_utf8_prev(&app.text, t);
        return vim_op_or_move('h', t, false);
    }
    case 'l':
    case DAWN_KEY_RIGHT:
    case ' ': {
        size_t t = app.cursor;
        for (int32_t i = 0; i < count; i++) {
            size_t nx = gap_utf8_next(&app.text, t);
            if (nx != t)
                t = nx;
        }
        return vim_op_or_move('l', t, false);
    }
    case 'j':
    case DAWN_KEY_DOWN:
        return vim_op_or_move('j', nav_move_line(app.cursor, count), true);
    case 'k':
    case DAWN_KEY_UP:
        return vim_op_or_move('k', nav_move_line(app.cursor, -count), true);
    case 'w':
        return vim_op_or_move('w', vim_word_forward(app.cursor, count), false);
    case 'W':
        return vim_op_or_move('W', vim_WORD_forward(app.cursor, count), false);
    case 'b':
        return vim_op_or_move('b', vim_word_back(app.cursor, count), false);
    case 'B':
        return vim_op_or_move('B', vim_WORD_back(app.cursor, count), false);
    case 'e':
        return vim_op_or_move('e', vim_word_end(app.cursor, count), false);
    case 'E':
        return vim_op_or_move('E', vim_WORD_end(app.cursor, count), false);
    case '$':
        // $ lands on last char; clamp keeps it there (not past EOL)
        return vim_op_or_move('$', nav_line_end(app.cursor), false);
    case '^': {
        size_t ls = nav_line_start(app.cursor);
        size_t le = nav_line_end(app.cursor);
        size_t p = ls;
        while (p < le && (gap_at(&app.text, p) == ' ' || gap_at(&app.text, p) == '\t'))
            p++;
        return vim_op_or_move('^', p, false);
    }
    case '{':
        return vim_op_or_move('{', vim_paragraph_back(app.cursor, count), false);
    case '}':
        return vim_op_or_move('}', vim_paragraph_forward(app.cursor, count), false);
    case 'G': {
        size_t target;
        if (app.vim.count > 0) {
            // [count]G: go to line count
            size_t p = 0;
            for (int32_t i = 1; i < count; i++) {
                size_t le = nav_line_end(p);
                if (le >= gap_len(&app.text))
                    break;
                p = le + 1;
            }
            size_t ls = nav_line_start(p);
            // keep column roughly: use first non-blank
            size_t le = nav_line_end(ls);
            size_t np = ls;
            while (np < le && (gap_at(&app.text, np) == ' ' || gap_at(&app.text, np) == '\t'))
                np++;
            target = np;
        } else {
            size_t len = gap_len(&app.text);
            target = len ? (gap_at(&app.text, len - 1) == '\n' ? len : len - 1) : 0;
        }
        return vim_op_or_move('G', target, true);
    }
    case '%':
        return vim_op_or_move('%', vim_match_bracket(app.cursor), false);
    case ';':
        if (app.vim.last_find) {
            bool fwd = !app.vim.last_find_back;
            return vim_op_or_move(';', vim_find_char(app.cursor, app.vim.last_find, fwd, app.vim.last_find_t, count), false);
        }
        app.vim.count = 0;
        return true;
    case ',':
        if (app.vim.last_find) {
            bool fwd = app.vim.last_find_back;
            return vim_op_or_move(',', vim_find_char(app.cursor, app.vim.last_find, fwd, app.vim.last_find_t, count), false);
        }
        app.vim.count = 0;
        return true;
    case 'n':
    case 'N': {
        // Repeat last / search via search overlay state
        if (app.search_state) {
            SearchState* s = (SearchState*)app.search_state;
            if (s->count > 0) {
                if (key == 'n')
                    s->selected = (s->selected + count) % s->count;
                else {
                    s->selected -= count;
                    while (s->selected < 0)
                        s->selected += s->count;
                }
                const SearchResult* r = search_get_selected(s);
                if (r)
                    vim_move(r->pos);
            }
        }
        app.vim.count = 0;
        return true;
    }

    // Mode switches to insert
    case 'i':
        vim_enter_insert();
        rep_op = VIM_OP_NONE;
        rep_motion = 'i';
        rep_count = vim_count_or_one() > 0 ? vim_count_or_one() : 1;
        vim_begin_insert_repeat();
        app.vim.count = 0;
        return true;
    case 'a': {
        size_t nx = gap_utf8_next(&app.text, app.cursor);
        if (nx != app.cursor && gap_at(&app.text, app.cursor) != '\n')
            app.cursor = nx;
        vim_enter_insert();
        rep_op = VIM_OP_NONE;
        rep_motion = 'a';
        rep_count = 1;
        vim_begin_insert_repeat();
        app.vim.count = 0;
        return true;
    }
    case 'I': {
        size_t ls = nav_line_start(app.cursor);
        size_t le = nav_line_end(app.cursor);
        size_t p = ls;
        while (p < le && (gap_at(&app.text, p) == ' ' || gap_at(&app.text, p) == '\t'))
            p++;
        app.cursor = p;
        vim_enter_insert();
        rep_op = VIM_OP_NONE;
        rep_motion = 'I';
        rep_count = 1;
        vim_begin_insert_repeat();
        app.vim.count = 0;
        return true;
    }
    case 'A':
        vim_move(nav_line_end(app.cursor));
        // In insert mode cursor may be past last char (append at EOL)
        app.vim.mode = VIM_INSERT;
        app.selecting = false;
        vim_clear_pending();
        rep_op = VIM_OP_NONE;
        rep_motion = 'A';
        rep_count = 1;
        vim_begin_insert_repeat();
        return true;
    case 'o':
        rep_op = VIM_OP_NONE;
        rep_motion = 'o';
        rep_count = vim_count_or_one();
        vim_open_line(true);
        vim_begin_insert_repeat();
        app.vim.count = 0;
        return true;
    case 'O':
        rep_op = VIM_OP_NONE;
        rep_motion = 'O';
        rep_count = 1;
        vim_open_line(false);
        vim_begin_insert_repeat();
        app.vim.count = 0;
        return true;
    case 'v':
        vim_enter_visual(false);
        return true;
    case 'V':
        vim_enter_visual(true);
        return true;
    case ':':
        vim_cmd_enter(false);
        return true;
    case '/':
        vim_cmd_enter(true);
        return true;

    // Single-key edits available in this commit
    case 'x':
        for (int32_t i = 0; i < count; i++)
            vim_do_delete_char(false);
        rep_valid = true;
        rep_op = VIM_OP_NONE;
        rep_motion = 'x';
        rep_count = count;
        rep_ins_len = 0;
        app.vim.count = 0;
        return true;
    case 'X':
        for (int32_t i = 0; i < count; i++)
            vim_do_delete_char(true);
        rep_valid = true;
        rep_op = VIM_OP_NONE;
        rep_motion = 'X';
        rep_count = count;
        rep_ins_len = 0;
        app.vim.count = 0;
        return true;
    case 'J':
        vim_do_join();
        rep_valid = true;
        rep_op = VIM_OP_NONE;
        rep_motion = 'J';
        rep_count = count;
        rep_ins_len = 0;
        app.vim.count = 0;
        return true;
    case '~':
        vim_do_toggle_case();
        app.vim.count = 0;
        return true;
    case 'p':
        vim_do_put(true);
        app.vim.count = 0;
        return true;
    case 'P':
        vim_do_put(false);
        app.vim.count = 0;
        return true;
    case 'u':
        vim_do_undo_redo(false);
        app.vim.count = 0;
        return true;
    case 18: // Ctrl+R redo
        vim_do_undo_redo(true);
        app.vim.count = 0;
        return true;

    // Operators
    case 'd':
        vim_start_operator(VIM_OP_DELETE);
        return true;
    case 'y':
        vim_start_operator(VIM_OP_YANK);
        return true;
    case 'c':
        vim_start_operator(VIM_OP_CHANGE);
        return true;
    case '>':
        vim_start_operator(VIM_OP_INDENT_RIGHT);
        return true;
    case '<':
        vim_start_operator(VIM_OP_INDENT_LEFT);
        return true;
    case 'r':
        pending_replace = true;
        return true;
    case 's': {
        int32_t total = vim_effective_count();
        vim_save_undo();
        size_t p = app.cursor;
        size_t len = gap_len(&app.text);
        size_t e = p;
        for (int32_t i = 0; i < total && e < len; i++) {
            if (gap_at(&app.text, e) == '\n')
                break;
            e = gap_utf8_next(&app.text, e);
        }
        if (e > p) {
            vim_store_yank(p, e, false);
            gap_delete(&app.text, p, e - p);
        }
        app.cursor = p;
        rep_op = VIM_OP_CHANGE;
        rep_motion = 's';
        rep_count = total;
        rep_linewise = false;
        vim_enter_insert();
        vim_begin_insert_repeat();
        app.vim.count = 0;
        app.vim.count_op = 0;
        return true;
    }
    case 'S': {
        int32_t total = vim_effective_count();
        size_t cur = app.cursor;
        for (int32_t i = 1; i < total; i++)
            cur = nav_move_line(cur, 1);
        vim_delete_lines(nav_line_start(app.cursor), nav_line_end(cur));
        rep_op = VIM_OP_CHANGE;
        rep_motion = 'S';
        rep_count = total;
        rep_linewise = true;
        vim_enter_insert();
        vim_begin_insert_repeat();
        app.vim.count = 0;
        app.vim.count_op = 0;
        return true;
    }
    case 'C': {
        size_t le = nav_line_end(app.cursor);
        size_t s = app.cursor;
        if (le > s) {
            vim_save_undo();
            vim_store_yank(s, le, false);
            gap_delete(&app.text, s, le - s);
        }
        app.cursor = s;
        rep_op = VIM_OP_CHANGE;
        rep_motion = 'C';
        rep_count = 1;
        rep_linewise = false;
        app.vim.mode = VIM_INSERT;
        app.selecting = false;
        vim_clear_pending();
        vim_begin_insert_repeat();
        return true;
    }
    case 'D': {
        size_t le = nav_line_end(app.cursor);
        size_t s = app.cursor;
        if (le > s) {
            vim_save_undo();
            vim_store_yank(s, le, false);
            gap_delete(&app.text, s, le - s);
        }
        vim_clamp_cursor();
        rep_valid = true;
        rep_op = VIM_OP_DELETE;
        rep_motion = 'D';
        rep_count = 1;
        rep_ins_len = 0;
        vim_clear_pending();
        return true;
    }
    case 'Y': {
        size_t le = nav_line_end(app.cursor);
        size_t s = app.cursor;
        size_t len = gap_len(&app.text);
        size_t e = le < len ? le + 1 : le;
        if (e > s)
            vim_store_yank(s, e, true);
        vim_set_status("yanked");
        vim_clear_pending();
        return true;
    }
    case '.':
        vim_repeat_last();
        vim_clear_pending();
        return true;
    case '"':
        pending_quote = true;
        return true;
    case '@':
        // @: repeats last : command (only : supported)
        pending_at = true;
        vim_set_status("@: then :");
        return true;
    default:
        // Preserve app-level Ctrl shortcuts (focus, TOC, search, undo...) by
        // letting them fall through to the default handler.
        if (key < 32 || key >= DAWN_KEY_UP)
            return false;
        vim_set_status(NULL);
        app.vim.count = 0;
        return true;
    }
}

static bool vim_handle_visual_key(int32_t key)
{
    if (key == 0x1b) {
        vim_enter_normal();
        return true;
    }
    // Motions extend the selection; reuse normal motions then restore visual state.
    VimMode saved = app.vim.mode;
    bool was_selecting = app.selecting;
    size_t anchor = app.sel_anchor;
    (void)saved;

    switch (key) {
    case 'v':
        vim_enter_normal();
        return true;
    case 'V':
        // Toggle linewise
        if (app.vim.mode == VIM_VISUAL_LINE) {
            app.vim.mode = VIM_VISUAL;
        } else {
            size_t s, e;
            get_selection(&s, &e);
            app.vim.mode = VIM_VISUAL_LINE;
            app.sel_anchor = nav_line_start(s);
            app.cursor = nav_line_end(e);
        }
        return true;
    case 'o':
    case 'O': {
        // Swap selection ends (neovim visual 'o')
        size_t tmp = app.cursor;
        app.cursor = app.sel_anchor;
        app.sel_anchor = tmp;
        vim_clamp_cursor();
        return true;
    }
    case 'y': {
        size_t s, e;
        get_selection(&s, &e);
        if (app.vim.mode == VIM_VISUAL_LINE) {
            size_t len = gap_len(&app.text);
            size_t le = nav_line_end(e);
            if (le < len)
                le++;
            e = le;
            s = nav_line_start(s);
        }
        vim_store_yank(s, e, app.vim.mode == VIM_VISUAL_LINE);
        vim_set_status("yanked");
        app.cursor = s;
        vim_enter_normal();
        return true;
    }
    case 'd':
    case 'x': {
        size_t s, e;
        get_selection(&s, &e);
        if (app.vim.mode == VIM_VISUAL_LINE) {
            size_t len = gap_len(&app.text);
            size_t le = nav_line_end(e);
            if (le < len)
                le++;
            e = le;
            s = nav_line_start(s);
        }
        size_t n = e - s;
        if (n > 0) {
            vim_save_undo();
            vim_store_yank(s, e, app.vim.mode == VIM_VISUAL_LINE);
            gap_delete(&app.text, s, e - s);
            app.cursor = s;
            rep_valid = true;
            rep_op = VIM_OP_DELETE;
            rep_motion = 0;
            rep_ins_len = 0;
        }
        vim_enter_normal();
        return true;
    }
    case 'c': {
        size_t s, e;
        get_selection(&s, &e);
        size_t n = e - s;
        if (n > 0) {
            vim_save_undo();
            vim_store_yank(s, e, false);
            gap_delete(&app.text, s, e - s);
        }
        app.cursor = s;
        rep_op = VIM_OP_CHANGE;
        rep_motion = 0;
        rep_count = 1;
        app.vim.mode = VIM_INSERT;
        app.selecting = false;
        vim_clear_pending();
        vim_begin_insert_repeat();
        return true;
    }
    case '>':
    case '<': {
        size_t s, e;
        get_selection(&s, &e);
        vim_indent_lines(s, e, key == '>' ? 1 : -1, app.vim.count > 0 ? app.vim.count : 1);
        app.vim.count = 0;
        // Stay in visual (vim reselects); keep anchor stable
        return true;
    }
    case '~': {
        size_t s, e;
        get_selection(&s, &e);
        vim_case_range(s, e, '~');
        vim_enter_normal();
        return true;
    }
    case 'u': {
        if (app.vim.mode == VIM_VISUAL || app.vim.mode == VIM_VISUAL_LINE) {
            size_t s, e;
            get_selection(&s, &e);
            vim_case_range(s, e, 'u');
            vim_enter_normal();
            return true;
        }
        vim_do_undo_redo(false);
        return true;
    }
    case 'U': {
        size_t s, e;
        get_selection(&s, &e);
        vim_case_range(s, e, 'U');
        vim_enter_normal();
        return true;
    }
    case 'p':
    case 'P': {
        // Paste over selection (neovim: replaced text goes to unnamed register)
        size_t s, e;
        get_selection(&s, &e);
        size_t reg_len = 0;
        bool rl = false;
        const char* rt = vim_fetch_reg(&reg_len, &rl);
        if (reg_len > 0 && e > s) {
            vim_save_undo();
            char replaced[8192];
            size_t rn = e - s < sizeof(replaced) - 1 ? e - s : sizeof(replaced) - 1;
            gap_copy_to(&app.text, s, rn, replaced);
            replaced[rn] = '\0';
            gap_delete(&app.text, s, e - s);
            gap_insert_str(&app.text, s, rt, reg_len);
            memcpy(app.vim.yank_buf, replaced, rn);
            app.vim.yank_buf[rn] = '\0';
            app.vim.yank_len = rn;
            app.vim.yank_linewise = false;
            app.cursor = s + reg_len - 1;
        }
        vim_enter_normal();
        return true;
    }
    case ':':
        vim_cmd_enter(false);
        return true;
    default:
        break;
    }

    // Fall through to motion handling: temporarily drop to a motion-only pass.
    app.vim.mode = VIM_NORMAL;
    app.selecting = true;
    app.sel_anchor = anchor;
    bool consumed = vim_handle_normal_key(key);
    // Restore visual mode identity (vim_handle_normal_key may have changed mode
    // for i/a/o/v/:/etc. — only keep visual if we are still selecting).
    if (app.vim.mode == VIM_NORMAL && app.selecting) {
        app.vim.mode = saved;
    } else if (!app.selecting && saved != VIM_NORMAL) {
        // Motion handler cleared selection (e.g. mode switch); respect it.
    }
    (void)was_selecting;
    return consumed;
}

static bool vim_handle_insert_key(int32_t key)
{
    if (key == 0x1b) {
        vim_end_insert_repeat();
        // Vim Esc: step back one char so cursor rests on text
        if (app.cursor > 0) {
            size_t prev = gap_utf8_prev(&app.text, app.cursor);
            if (gap_at(&app.text, prev) != '\n')
                app.cursor = prev;
        }
        vim_enter_normal();
        return true;
    }
    return false; // let the default editor handle insert-mode keys
}

// #endregion

// #region Entry

bool vim_handle_key(int32_t key)
{
    if (!app.vim.enabled)
        return false;
    // Command line captures everything until executed/cancelled.
    if (app.vim.mode == VIM_COMMAND)
        return vim_handle_command_key(key);
    if (app.vim.mode == VIM_INSERT)
        return vim_handle_insert_key(key);
    if (app.vim.mode == VIM_VISUAL || app.vim.mode == VIM_VISUAL_LINE)
        return vim_handle_visual_key(key);
    return vim_handle_normal_key(key);
}

// #endregion
