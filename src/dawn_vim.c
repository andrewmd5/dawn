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

static void vim_clear_pending(void)
{
    app.vim.pending_op = VIM_OP_NONE;
    app.vim.pending_g = false;
    app.vim.count = 0;
    app.vim.count_op = 0;
    app.vim.find_op = 0;
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
    if (app.vim.yank_len == 0)
        return;
    vim_save_undo();
    if (app.vim.yank_linewise) {
        size_t ls = nav_line_start(app.cursor);
        size_t le = nav_line_end(app.cursor);
        size_t at = after ? (le < gap_len(&app.text) ? le + 1 : le) : ls;
        if (after && le < gap_len(&app.text) && gap_len(&app.text) > 0) {
            gap_insert_str(&app.text, at, app.vim.yank_buf, app.vim.yank_len);
            app.cursor = at;
        } else {
            gap_insert_str(&app.text, at, app.vim.yank_buf, app.vim.yank_len);
            app.cursor = at;
        }
    } else {
        size_t at = app.cursor;
        if (after && at < gap_len(&app.text))
            at = gap_utf8_next(&app.text, at);
        gap_insert_str(&app.text, at, app.vim.yank_buf, app.vim.yank_len);
        app.cursor = at + app.vim.yank_len - 1;
        if (app.cursor >= gap_len(&app.text) && gap_len(&app.text) > 0)
            app.cursor = gap_len(&app.text) - 1;
    }
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
    char cmd = app.vim.cmdline[0];
    if (cmd == 'w') {
        dawn_save_document();
        vim_set_status("written");
    } else if (cmd == 'q') {
        if (app.vim.cmdline_len >= 2 && app.vim.cmdline[1] == '!') {
            dawn_request_quit();
        } else if (strncmp(app.vim.cmdline, "wq", 2) == 0) {
            dawn_save_document();
            dawn_request_quit();
        } else {
            dawn_save_document();
            app.mode = MODE_WELCOME;
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
    case 'g':
        vim_move(0);
        vim_clear_pending();
        return true;
    case 'j':
        vim_move(nav_move_visual_line(app.cursor, count, 80));
        vim_clear_pending();
        return true;
    case 'k':
        vim_move(nav_move_visual_line(app.cursor, -count, 80));
        vim_clear_pending();
        return true;
    default:
        vim_set_status("d/g: operator pending in next commit");
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
            vim_move(np);
            app.vim.count = 0;
            return true;
        }
        app.vim.find_op = 0;
        return true;
    }

    if (app.vim.pending_g)
        return vim_handle_g_prefix(key);

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
    case DAWN_KEY_LEFT:
        for (int32_t i = 0; i < count; i++)
            app.cursor = gap_utf8_prev(&app.text, app.cursor);
        vim_clamp_cursor();
        app.vim.count = 0;
        return true;
    case 'l':
    case DAWN_KEY_RIGHT:
    case ' ':
        for (int32_t i = 0; i < count; i++) {
            size_t nx = gap_utf8_next(&app.text, app.cursor);
            if (nx != app.cursor)
                app.cursor = nx;
        }
        vim_clamp_cursor();
        app.vim.count = 0;
        return true;
    case 'j':
    case DAWN_KEY_DOWN:
        vim_move(nav_move_line(app.cursor, count));
        app.vim.count = 0;
        return true;
    case 'k':
    case DAWN_KEY_UP:
        vim_move(nav_move_line(app.cursor, -count));
        app.vim.count = 0;
        return true;
    case 'w':
        vim_move(vim_word_forward(app.cursor, count));
        app.vim.count = 0;
        return true;
    case 'W':
        vim_move(vim_WORD_forward(app.cursor, count));
        app.vim.count = 0;
        return true;
    case 'b':
        vim_move(vim_word_back(app.cursor, count));
        app.vim.count = 0;
        return true;
    case 'B':
        vim_move(vim_WORD_back(app.cursor, count));
        app.vim.count = 0;
        return true;
    case 'e':
        vim_move(vim_word_end(app.cursor, count));
        app.vim.count = 0;
        return true;
    case 'E':
        vim_move(vim_WORD_end(app.cursor, count));
        app.vim.count = 0;
        return true;
    case '$':
        vim_move(nav_line_end(app.cursor));
        vim_clamp_cursor();
        // $ lands on last char; clamp keeps it there (not past EOL)
        app.vim.count = 0;
        return true;
    case '^': {
        size_t ls = nav_line_start(app.cursor);
        size_t le = nav_line_end(app.cursor);
        size_t p = ls;
        while (p < le && (gap_at(&app.text, p) == ' ' || gap_at(&app.text, p) == '\t'))
            p++;
        vim_move(p);
        app.vim.count = 0;
        return true;
    }
    case '{':
        vim_move(vim_paragraph_back(app.cursor, count));
        app.vim.count = 0;
        return true;
    case '}':
        vim_move(vim_paragraph_forward(app.cursor, count));
        app.vim.count = 0;
        return true;
    case 'G': {
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
            vim_move(np);
        } else {
            size_t len = gap_len(&app.text);
            vim_move(len ? (gap_at(&app.text, len - 1) == '\n' ? len : len - 1) : 0);
            vim_clamp_cursor();
        }
        app.vim.count = 0;
        return true;
    }
    case '%':
        vim_move(vim_match_bracket(app.cursor));
        app.vim.count = 0;
        return true;
    case ';':
        if (app.vim.last_find) {
            bool fwd = !app.vim.last_find_back;
            vim_move(vim_find_char(app.cursor, app.vim.last_find, fwd, app.vim.last_find_t, count));
        }
        app.vim.count = 0;
        return true;
    case ',':
        if (app.vim.last_find) {
            bool fwd = app.vim.last_find_back;
            vim_move(vim_find_char(app.cursor, app.vim.last_find, fwd, app.vim.last_find_t, count));
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
        return true;
    case 'a': {
        size_t nx = gap_utf8_next(&app.text, app.cursor);
        if (nx != app.cursor && gap_at(&app.text, app.cursor) != '\n')
            app.cursor = nx;
        vim_enter_insert();
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
        return true;
    }
    case 'A':
        vim_move(nav_line_end(app.cursor));
        // In insert mode cursor may be past last char (append at EOL)
        app.vim.mode = VIM_INSERT;
        app.selecting = false;
        vim_clear_pending();
        return true;
    case 'o':
        vim_open_line(true);
        app.vim.count = 0;
        return true;
    case 'O':
        vim_open_line(false);
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
        app.vim.count = 0;
        return true;
    case 'X':
        for (int32_t i = 0; i < count; i++)
            vim_do_delete_char(true);
        app.vim.count = 0;
        return true;
    case 'J':
        vim_do_join();
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

    // Operators land fully in the next commit; acknowledge the keypress.
    case 'd':
    case 'y':
    case 'c':
    case '>':
    case '<':
    case 'r':
    case 's':
    case 'S':
    case 'C':
    case 'D':
    case 'Y':
    case '.':
    case '"':
        vim_set_status("operator/text-object: next commit");
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
    case 'y': {
        size_t s, e;
        get_selection(&s, &e);
        size_t n = e - s;
        if (n > sizeof(app.vim.yank_buf) - 1)
            n = sizeof(app.vim.yank_buf) - 1;
        gap_copy_to(&app.text, s, n, app.vim.yank_buf);
        app.vim.yank_buf[n] = '\0';
        app.vim.yank_len = n;
        app.vim.yank_linewise = (app.vim.mode == VIM_VISUAL_LINE);
        clipboard_copy(app.vim.yank_buf, n);
        app.cursor = s;
        vim_enter_normal();
        return true;
    }
    case 'd':
    case 'x': {
        size_t s, e;
        get_selection(&s, &e);
        size_t n = e - s;
        if (n > 0) {
            vim_save_undo();
            if (n > sizeof(app.vim.yank_buf) - 1)
                n = sizeof(app.vim.yank_buf) - 1;
            gap_copy_to(&app.text, s, n, app.vim.yank_buf);
            app.vim.yank_buf[n] = '\0';
            app.vim.yank_len = n;
            app.vim.yank_linewise = (app.vim.mode == VIM_VISUAL_LINE);
            gap_delete(&app.text, s, e - s);
            app.cursor = s;
        }
        vim_enter_normal();
        return true;
    }
    case ':':
        vim_cmd_enter(false);
        return true;
    case 'u':
        vim_do_undo_redo(false);
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
