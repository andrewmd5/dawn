// dawn_render.c

#include "dawn_render.h"
#include "dawn_block.h"
#include "dawn_gap.h"
#include "dawn_image.h"
#include "dawn_modal.h"
#include "dawn_theme.h"
#include "dawn_timer.h"
#include "dawn_utils.h"
#include "dawn_toc.h"
#include "dawn_search.h"
#include <stdio.h>
#include <string.h>

// #region Platform Output Helpers

static void platform_write_str(const char *str) {
    DAWN_BACKEND(app)->write_str(str, strlen(str));
}

static void platform_write_char(char c) {
    DAWN_BACKEND(app)->write_char(c);
}

static void platform_clear_screen(void) {
    DAWN_BACKEND(app)->clear_screen();
}

static void platform_set_cursor_visible(bool visible) {
    DAWN_BACKEND(app)->set_cursor_visible(visible);
}

static void platform_set_bold(bool enabled) {
    DAWN_BACKEND(app)->set_bold(enabled);
}

static void platform_reset_attrs(void) {
    DAWN_BACKEND(app)->reset_attrs();
}

// #endregion

// #region Utility Functions

void render_clear(void) {
    set_bg(get_bg());
    platform_clear_screen();
    for (int32_t r = 0; r < app.rows; r++) {
        move_to(r + 1, 1);
        for (int32_t c = 0; c < app.cols; c++) platform_write_char(' ');
    }
}

void render_center_text(int32_t row, const char *text, DawnColor fg) {
    int32_t len = (int32_t)strlen(text);
    int32_t col = (app.cols - len) / 2;
    if (col < 1) col = 1;
    move_to(row, col);
    set_fg(fg);
    platform_write_str(text);
}

void render_popup_box(int32_t width, int32_t height, int32_t *out_top, int32_t *out_left) {
    int32_t top = (app.rows - height) / 2;
    int32_t left = (app.cols - width) / 2;
    if (top < 1) top = 1;
    if (left < 1) left = 1;

    DawnColor bg = get_modal_bg();
    image_mask_region(left, top, width, height, bg);
    DawnColor border = get_border();

    // Top border
    move_to(top, left);
    set_bg(bg);
    set_fg(border);
    platform_write_str("╭");
    for (int32_t i = 0; i < width - 2; i++) platform_write_str("─");
    platform_write_str("╮");

    // Middle rows
    for (int32_t r = 1; r < height - 1; r++) {
        move_to(top + r, left);
        set_fg(border);
        platform_write_str("│");
        set_fg(get_fg());
        for (int32_t i = 0; i < width - 2; i++) platform_write_char(' ');
        set_fg(border);
        platform_write_str("│");
    }

    // Bottom border
    move_to(top + height - 1, left);
    set_fg(border);
    platform_write_str("╰");
    for (int32_t i = 0; i < width - 2; i++) platform_write_str("─");
    platform_write_str("╯");

    if (out_top) *out_top = top;
    if (out_left) *out_left = left;
}

// #endregion

// #region Screen Renderers

static void render_text_at(int32_t row, int32_t col, const char *text, DawnColor fg) {
    move_to(row, col);
    set_fg(fg);
    platform_write_str(text);
}

void render_welcome(void) {
    render_clear();

    // Use most of the available space
    int32_t margin_h = app.cols > 100 ? 8 : (app.cols > 60 ? 4 : 2);
    int32_t margin_v = app.rows > 30 ? 3 : 2;
    int32_t content_left = margin_h + 1;
    int32_t content_right = app.cols - margin_h;
    int32_t content_width = content_right - content_left;

    // Vertical layout
    int32_t top_row = margin_v + 1;
    int32_t bottom_row = app.rows - margin_v;
    int32_t center_row = (top_row + bottom_row) / 2;

    // Clean block letter logo
    static const char *logo[] = {
        "█▀▄ ▄▀█ █ █ █ █▄ █",
        "█▄▀ █▀█ ▀▄▀▄▀ █ ▀█",
    };
    int32_t logo_height = 2;
    int32_t logo_width = 19;

    // Center logo vertically - position it above center
    int32_t logo_start = center_row - logo_height - 2;
    if (logo_start < top_row) logo_start = top_row;

    set_fg(get_fg());
    for (int32_t i = 0; i < logo_height; i++) {
        int32_t col = (app.cols - logo_width) / 2;
        if (col < 1) col = 1;
        move_to(logo_start + i, col);
        platform_write_str(logo[i]);
    }

    // Tagline below logo
    render_center_text(logo_start + logo_height + 1, "draft anything, write now", get_dim());

    // Actions grid - positioned below center
    int32_t actions_row = center_row + 2;
    int32_t col1 = content_left + content_width / 4 - 8;
    int32_t col2 = content_left + content_width / 2 + content_width / 4 - 8;
    if (col1 < content_left + 2) col1 = content_left + 2;
    if (col2 < col1 + 20) col2 = col1 + 20;

    int32_t row = actions_row;
    render_text_at(row, col1, "enter", get_accent());
    render_text_at(row, col1 + 6, " write", get_dim());
    render_text_at(row, col2, "h", get_accent());
    render_text_at(row, col2 + 2, " history", get_dim());

    row += 2;
    render_text_at(row, col1, "t", get_accent());
    render_text_at(row, col1 + 6, " timer", get_dim());
    render_text_at(row, col2, "d", get_accent());
    render_text_at(row, col2 + 2, " theme", get_dim());

    row += 2;
    render_text_at(row, col1, "q", get_accent());
    render_text_at(row, col1 + 6, " quit", get_dim());
    render_text_at(row, col2, "?", get_accent());
    render_text_at(row, col2 + 2, " help", get_dim());

    #if HAS_LIBAI
    if (app.ai_ready) {
        row += 2;
        render_center_text(row, "✦ ai ready", get_accent());
    }
    #endif

    // Bottom status bar - like editor status bar
    move_to(bottom_row, content_left);

    // Left: timer setting
    set_fg(get_dim());
    if (app.timer_mins == 0) {
        platform_write_str("no timer");
    } else {
        char timer_str[16];
        snprintf(timer_str, sizeof(timer_str), "%d min", app.timer_mins);
        platform_write_str(timer_str);
    }

    // Right: theme
    const char *theme_str = app.theme == THEME_DARK ? "dark" : "light";
    int32_t theme_col = content_right - (int32_t)strlen(theme_str);
    move_to(bottom_row, theme_col);
    set_fg(get_dim());
    platform_write_str(theme_str);
}

void render_timer_select(void) {
    render_clear();
    int32_t cy = app.rows / 2;

    render_center_text(cy - 5, "select timer", get_fg());

    for (size_t i = 0; i < NUM_PRESETS; i++) {
        char buf[32];
        if (TIMER_PRESETS[i] == 0) {
            snprintf(buf, sizeof(buf), "%s no timer %s",
                     (int32_t)i == app.preset_idx ? ">" : " ",
                     (int32_t)i == app.preset_idx ? "<" : " ");
        } else {
            snprintf(buf, sizeof(buf), "%s %d min %s",
                     (int32_t)i == app.preset_idx ? ">" : " ",
                     TIMER_PRESETS[i],
                     (int32_t)i == app.preset_idx ? "<" : " ");
        }
        render_center_text(cy - 2 + (int32_t)i, buf,
                    (int32_t)i == app.preset_idx ? get_accent() : get_dim());
    }

    render_center_text(app.rows - 2, "[j/k] select   [enter] confirm   [esc] back", get_dim());
}

void render_style_select(void) {
    render_clear();
    int32_t cy = app.rows / 2;

    render_center_text(cy - 4, "select style", get_fg());

    const char *names[] = {"minimal", "typewriter", "elegant"};
    const char *descs[] = {"clean focus", "monospace feel", "italic grace"};

    for (int32_t i = 0; i < 3; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%s %s %s",
                 i == (int32_t)app.style ? ">" : " ",
                 names[i],
                 i == (int32_t)app.style ? "<" : " ");
        render_center_text(cy - 1 + i * 2, buf, i == (int32_t)app.style ? get_accent() : get_dim());
        render_center_text(cy + i * 2, descs[i], get_dim());
    }

    render_center_text(app.rows - 2, "[j/k] select   [enter] confirm   [esc] back", get_dim());
}

void render_help(void) {
    int32_t width = 44;
    int32_t height = 26;
    int32_t top, left;
    render_popup_box(width, height, &top, &left);

    int32_t col1 = left + 4;
    int32_t col2 = left + 20;

    set_bg(get_modal_bg());

    // Title
    move_to(top + 2, left + width / 2 - 9);
    set_fg(get_fg());
    platform_set_bold(true);
    platform_write_str("KEYBOARD SHORTCUTS");
    platform_reset_attrs();
    set_bg(get_modal_bg());

    int32_t cy = top + 4;
    move_to(cy++, col1);
    set_fg(get_accent());
    platform_set_bold(true);
    platform_write_str("NAVIGATION");
    platform_reset_attrs();
    set_bg(get_modal_bg());
    set_fg(get_dim());

    move_to(cy, col1); platform_write_str("arrows");
    move_to(cy++, col2); platform_write_str("move cursor");
    move_to(cy, col1); platform_write_str("opt+arrows");
    move_to(cy++, col2); platform_write_str("word jump");
    move_to(cy, col1); platform_write_str("pgup/pgdn");
    move_to(cy++, col2); platform_write_str("scroll page");
    move_to(cy, col1); platform_write_str("^L");
    move_to(cy++, col2); platform_write_str("table of contents");
    move_to(cy, col1); platform_write_str("^S");
    move_to(cy++, col2); platform_write_str("search document");

    cy++;
    move_to(cy++, col1);
    set_fg(get_accent());
    platform_set_bold(true);
    platform_write_str("EDITING");
    platform_reset_attrs();
    set_bg(get_modal_bg());
    set_fg(get_dim());

    move_to(cy, col1); platform_write_str("^C ^X ^V");
    move_to(cy++, col2); platform_write_str("copy/cut/paste");
    move_to(cy, col1); platform_write_str("^Z ^Y");
    move_to(cy++, col2); platform_write_str("undo/redo");
    move_to(cy, col1); platform_write_str("^W ^D");
    move_to(cy++, col2); platform_write_str("delete word/elem");
    move_to(cy, col1); platform_write_str("tab shift+tab");
    move_to(cy++, col2); platform_write_str("indent list");

    cy++;
    move_to(cy++, col1);
    set_fg(get_accent());
    platform_set_bold(true);
    platform_write_str("FEATURES");
    platform_reset_attrs();
    set_bg(get_modal_bg());
    set_fg(get_dim());

    move_to(cy, col1); platform_write_str("^F");
    move_to(cy++, col2); platform_write_str("focus mode");
    move_to(cy, col1); platform_write_str("^R");
    move_to(cy++, col2); platform_write_str("plain text mode");
    move_to(cy, col1); platform_write_str("^G ^E");
    move_to(cy++, col2); platform_write_str("edit title/image");
    move_to(cy, col1); platform_write_str("^P ^T");
    move_to(cy++, col2); platform_write_str("pause/timer");
    #if HAS_LIBAI
    move_to(cy, col1); platform_write_str("^/");
    move_to(cy++, col2); platform_write_str("AI chat");
    #endif

    // Footer
    move_to(top + height - 2, left + (width - 22) / 2);
    set_fg(get_dim());
    platform_write_str("press any key to close");
}

void render_history(void) {
    render_clear();

    if (app.hist_count == 0) {
        render_center_text(app.rows / 2, "no history yet", get_dim());
        render_center_text(app.rows / 2 + 2, "[esc] back", get_dim());
        return;
    }

    move_to(2, 4);
    set_fg(get_fg());
    platform_write_str("history");

    int32_t visible = app.rows - 6;
    int32_t start = 0;
    if (app.hist_sel >= visible) start = app.hist_sel - visible + 1;

    for (int32_t i = 0; i < visible && start + i < app.hist_count; i++) {
        int32_t idx = start + i;
        HistoryEntry *entry = &app.history[idx];

        move_to(4 + i, 4);
        if (idx == app.hist_sel) {
            set_fg(get_accent());
            platform_write_str("> ");
        } else {
            set_fg(get_dim());
            platform_write_str("  ");
        }

        // Display title (or "Untitled") followed by date
        const char *title = entry->title ? entry->title : "Untitled";
        char title_buf[64];
        snprintf(title_buf, sizeof(title_buf), "%-30.30s  ", title);
        platform_write_str(title_buf);
        set_fg(get_dim());
        platform_write_str(entry->date_str);
    }

    move_to(app.rows - 1, 4);
    set_fg(get_dim());
    platform_write_str("[j/k] select   [o] open   [t] title   [d] delete   [e] finder   [esc] back");
}

void render_finished(void) {
    render_clear();
    int32_t cy = app.rows / 2;

    render_center_text(cy - 3, "done.", get_fg());
    render_center_text(cy - 1, "your writing is saved.", get_dim());

    int32_t words = count_words(&app.text);

    char stats[64];
    if (app.timer_start > 0) {
        int64_t now = DAWN_BACKEND(app)->clock(DAWN_CLOCK_SEC);
        int64_t elapsed_secs;
        if (app.timer_paused) {
            elapsed_secs = app.timer_mins * 60 - app.timer_paused_at;
        } else {
            elapsed_secs = now - app.timer_start;
        }
        int32_t elapsed_mins = (int32_t)(elapsed_secs / 60);
        if (elapsed_mins < 1) elapsed_mins = 1;
        snprintf(stats, sizeof(stats), "%d words in %d min", words, elapsed_mins);
    } else {
        snprintf(stats, sizeof(stats), "%d words", words);
    }
    render_center_text(cy + 1, stats, get_accent());

    render_center_text(cy + 4, "[c] continue   [enter] new   [esc] menu", get_dim());
    render_center_text(cy + 5, "[o] finder   [q] quit", get_dim());
    #if HAS_LIBAI
    if (app.ai_ready) {
        render_center_text(cy + 7, "[/] reflect with ai", get_dim());
    }
    #endif
}

void render_title_edit(void) {
    int32_t box_width = 50;
    int32_t box_height = 7;
    int32_t top, left;

    render_popup_box(box_width, box_height, &top, &left);

    int32_t content_left = left + 2;
    int32_t content_top = top + 1;

    set_bg(get_modal_bg());

    // Title
    move_to(content_top, content_left);
    set_fg(get_dim());
    platform_write_str("Set Title");

    // Input field
    int32_t input_row = content_top + 2;
    move_to(input_row, content_left);
    set_fg(get_accent());
    platform_write_str("> ");
    set_fg(get_fg());
    for (size_t i = 0; i < app.title_edit_len; i++) {
        platform_write_char(app.title_edit_buf[i]);
    }

    // Help text
    move_to(content_top + 4, content_left);
    set_fg(get_dim());
    platform_write_str("enter:save  esc:cancel");

    // Position cursor
    move_to(input_row, content_left + 2 + (int32_t)app.title_edit_cursor);
    platform_set_cursor_visible(true);
}

static void render_block_edit_image(void) {
    MODAL_BEGIN("Edit Image", 60, 13);

    MODAL_TEXT_FIELD(0, "Alt:    ", app.block_edit.image.alt,
                     app.block_edit.image.alt_len, _modal_content_width - 10, 0);
    MODAL_TEXT_FIELD(1, "Title:  ", app.block_edit.image.title,
                     app.block_edit.image.title_len, _modal_content_width - 10, 1);

    MODAL_SIZE_FIELD(3, "Width:  ", app.block_edit.image.width,
                     app.block_edit.image.width_len, app.block_edit.image.width_pct, 2);
    MODAL_SIZE_FIELD(4, "Height: ", app.block_edit.image.height,
                     app.block_edit.image.height_len, app.block_edit.image.height_pct, 3);

    MODAL_HELP(9, "tab:field  p:%/px  enter:save  esc:cancel");

    MODAL_END();
}

void render_block_edit(void) {
    switch (app.block_edit.type) {
        case BLOCK_IMAGE: render_block_edit_image(); break;
        // Future: case BLOCK_CODE: render_block_edit_code(); break;
        default: break;
    }
}

void render_toc(void) {
    TocState *toc = (TocState *)app.toc_state;
    if (!toc) return;

    // Calculate dimensions
    int32_t width = app.cols > 80 ? 70 : app.cols - 6;
    int32_t max_height = app.rows - 6;
    int32_t list_height = max_height - 7;  // Space for header, filter, footer
    if (list_height < 3) list_height = 3;
    int32_t height = list_height + 7;

    int32_t top, left;
    render_popup_box(width, height, &top, &left);

    int32_t content_left = left + 3;
    int32_t content_right = left + width - 3;
    int32_t content_width = content_right - content_left;

    set_bg(get_modal_bg());

    // Title
    move_to(top + 2, left + width / 2 - 8);
    set_fg(get_fg());
    platform_set_bold(true);
    platform_write_str("TABLE OF CONTENTS");
    platform_reset_attrs();
    set_bg(get_modal_bg());

    // Filter input
    int32_t filter_row = top + 4;
    move_to(filter_row, content_left);
    set_fg(get_dim());
    platform_write_str("filter: ");
    set_fg(get_accent());
    for (int32_t i = 0; i < toc->filter_len && i < content_width - 10; i++) {
        platform_write_char(toc->filter[i]);
    }
    // Cursor indicator
    set_fg(get_fg());
    platform_write_char('_');

    // Results count
    char count_str[32];
    snprintf(count_str, sizeof(count_str), "%d/%d", toc->filtered_count, toc->count);
    move_to(filter_row, content_right - (int32_t)strlen(count_str));
    set_fg(get_dim());
    platform_write_str(count_str);

    // Separator
    move_to(top + 5, content_left);
    set_fg(get_border());
    for (int32_t i = 0; i < content_width; i++) platform_write_str("─");

    // TOC entries
    int32_t list_start = top + 6;
    int32_t visible = list_height;

    // Adjust scroll to keep selection visible
    if (toc->selected < toc->scroll) toc->scroll = toc->selected;
    if (toc->selected >= toc->scroll + visible) toc->scroll = toc->selected - visible + 1;

    for (int32_t i = 0; i < visible; i++) {
        int32_t idx = toc->scroll + i;
        if (idx >= toc->filtered_count) break;

        int32_t entry_idx = toc->filtered[idx];
        TocEntry *entry = &toc->entries[entry_idx];

        move_to(list_start + i, content_left);

        // Selection indicator
        if (idx == toc->selected) {
            set_fg(get_accent());
            platform_write_str("▸ ");
        } else {
            platform_write_str("  ");
        }

        // Indentation based on hierarchy depth
        int32_t indent = entry->depth * 2;
        for (int32_t j = 0; j < indent && j < 12; j++) platform_write_char(' ');

        // Header text
        set_fg(idx == toc->selected ? get_fg() : get_dim());
        if (idx == toc->selected) platform_set_bold(true);

        // Truncate if needed
        int32_t max_text = content_width - 4 - indent;
        int32_t text_len = entry->text_len;
        if (text_len > max_text) text_len = max_text;

        for (int32_t j = 0; j < text_len; j++) {
            platform_write_char(entry->text[j]);
        }
        if (entry->text_len > max_text) {
            set_fg(get_dim());
            platform_write_str("...");
        }

        platform_reset_attrs();
        set_bg(get_modal_bg());
    }

    // Scroll indicators
    if (toc->scroll > 0) {
        move_to(list_start, content_right);
        set_fg(get_dim());
        platform_write_str("↑");
    }
    if (toc->scroll + visible < toc->filtered_count) {
        move_to(list_start + visible - 1, content_right);
        set_fg(get_dim());
        platform_write_str("↓");
    }

    // Footer
    move_to(top + height - 2, content_left);
    set_fg(get_dim());
    platform_write_str("↑↓:nav  enter:jump  esc:close");

    // Position cursor at filter
    move_to(filter_row, content_left + 8 + toc->filter_len);
    platform_set_cursor_visible(true);
}

void render_search(void) {
    SearchState *search = (SearchState *)app.search_state;
    if (!search) return;

    // Calculate dimensions
    int32_t width = app.cols > 90 ? 80 : app.cols - 6;
    int32_t max_height = app.rows - 6;
    int32_t list_height = max_height - 8;
    if (list_height < 3) list_height = 3;
    int32_t height = list_height + 8;

    int32_t top, left;
    render_popup_box(width, height, &top, &left);

    int32_t content_left = left + 3;
    int32_t content_right = left + width - 3;
    int32_t content_width = content_right - content_left;

    set_bg(get_modal_bg());

    // Title
    move_to(top + 2, left + width / 2 - 3);
    set_fg(get_fg());
    platform_set_bold(true);
    platform_write_str("SEARCH");
    platform_reset_attrs();
    set_bg(get_modal_bg());

    // Search input
    int32_t search_row = top + 4;
    move_to(search_row, content_left);
    set_fg(get_dim());
    platform_write_str("find: ");
    set_fg(get_accent());
    for (int32_t i = 0; i < search->query_len && i < content_width - 8; i++) {
        platform_write_char(search->query[i]);
    }
    set_fg(get_fg());
    platform_write_char('_');

    // Results count
    char count_str[32];
    if (search->count >= SEARCH_MAX_RESULTS) {
        snprintf(count_str, sizeof(count_str), "%d+ matches", search->count);
    } else {
        snprintf(count_str, sizeof(count_str), "%d match%s", search->count, search->count == 1 ? "" : "es");
    }
    move_to(search_row, content_right - (int32_t)strlen(count_str));
    set_fg(get_dim());
    platform_write_str(count_str);

    // Separator
    move_to(top + 5, content_left);
    set_fg(get_border());
    for (int32_t i = 0; i < content_width; i++) platform_write_str("─");

    // Search results with context
    int32_t list_start = top + 6;
    int32_t visible = list_height;

    // Adjust scroll
    if (search->selected < search->scroll) search->scroll = search->selected;
    if (search->selected >= search->scroll + visible) search->scroll = search->selected - visible + 1;

    for (int32_t i = 0; i < visible; i++) {
        int32_t idx = search->scroll + i;
        if (idx >= search->count) break;

        SearchResult *r = &search->results[idx];

        move_to(list_start + i, content_left);

        // Selection indicator
        if (idx == search->selected) {
            set_fg(get_accent());
            platform_write_str("▸ ");
        } else {
            platform_write_str("  ");
        }

        // Line number
        char line_str[16];
        snprintf(line_str, sizeof(line_str), "%4d: ", r->line_num);
        set_fg(get_dim());
        platform_write_str(line_str);

        // Context with highlighted match
        int32_t max_ctx = content_width - 10;

        for (int32_t j = 0; j < r->context_len && j < max_ctx; j++) {
            // Highlight the match
            if (j >= r->match_start && j < r->match_start + r->match_len) {
                set_fg(get_accent());
                if (idx == search->selected) platform_set_bold(true);
            } else {
                set_fg(idx == search->selected ? get_fg() : get_dim());
            }
            platform_write_char(r->context[j]);
            platform_reset_attrs();
            set_bg(get_modal_bg());
        }

        if (r->context_len > max_ctx) {
            set_fg(get_dim());
            platform_write_str("...");
        }
    }

    // Scroll indicators
    if (search->scroll > 0) {
        move_to(list_start, content_right);
        set_fg(get_dim());
        platform_write_str("↑");
    }
    if (search->scroll + visible < search->count) {
        move_to(list_start + visible - 1, content_right);
        set_fg(get_dim());
        platform_write_str("↓");
    }

    // Footer
    move_to(top + height - 2, content_left);
    set_fg(get_dim());
    platform_write_str("↑↓:nav  enter:jump  ^n/^p:next/prev  esc:close");

    // Position cursor at search
    move_to(search_row, content_left + 6 + search->query_len);
    platform_set_cursor_visible(true);
}

// #endregion
