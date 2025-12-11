// dawn_theme.c

#include "dawn_theme.h"

// #region DawnColor Palettes

//! Light theme - warm paper aesthetic
static const DawnColor LIGHT_BG        = { 252, 250, 245 };  //!< Cream paper background
static const DawnColor LIGHT_FG        = { 45, 45, 45 };     //!< Dark ink text
static const DawnColor LIGHT_DIM       = { 160, 155, 145 };  //!< Muted annotations
static const DawnColor LIGHT_ACCENT    = { 120, 100, 80 };   //!< Sepia accent
static const DawnColor LIGHT_SELECT    = { 255, 245, 200 };  //!< Warm highlight
static const DawnColor LIGHT_AI_BG     = { 245, 243, 238 };  //!< Subtle AI panel
static const DawnColor LIGHT_BORDER    = { 220, 215, 205 };  //!< Soft borders
static const DawnColor LIGHT_CODE_BG   = { 240, 238, 233 };  //!< Code block background
static const DawnColor LIGHT_MODAL_BG  = { 255, 253, 250 };  //!< Modal popup background

//! Dark theme - deep focus aesthetic
static const DawnColor DARK_BG         = { 22, 22, 26 };     //!< Deep charcoal background
static const DawnColor DARK_FG         = { 210, 205, 195 };  //!< Warm white text
static const DawnColor DARK_DIM        = { 90, 85, 80 };     //!< Muted annotations
static const DawnColor DARK_ACCENT     = { 200, 175, 130 };  //!< Golden accent
static const DawnColor DARK_SELECT     = { 60, 55, 45 };     //!< Subtle highlight
static const DawnColor DARK_AI_BG      = { 28, 28, 32 };     //!< Slightly lighter panel
static const DawnColor DARK_BORDER     = { 50, 48, 45 };     //!< Soft borders
static const DawnColor DARK_CODE_BG    = { 30, 30, 34 };     //!< Code block background
static const DawnColor DARK_MODAL_BG   = { 35, 35, 40 };     //!< Modal popup background

// #endregion

// #region Output Primitives

void set_fg(DawnColor c) {
    DAWN_BACKEND(app)->set_fg(c);
}

void set_bg(DawnColor c) {
    DAWN_BACKEND(app)->set_bg(c);
}

void move_to(int32_t r, int32_t c) {
    DAWN_BACKEND(app)->set_cursor(c, r);  // Note: backend uses (col, row) order
}

void out_str(const char *str) {
    DAWN_BACKEND(app)->write_str(str, strlen(str));
}

void out_str_n(const char *str, size_t len) {
    DAWN_BACKEND(app)->write_str(str, len);
}

void out_char(char c) {
    DAWN_BACKEND(app)->write_char(c);
}

void out_spaces(int32_t n) {
    for (int32_t i = 0; i < n; i++) {
        DAWN_BACKEND(app)->write_char(' ');
    }
}

void out_int(int32_t value) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", value);
    out_str(buf);
}

void out_flush(void) {
    DAWN_BACKEND(app)->flush();
}

void clear_screen(void) {
    DAWN_BACKEND(app)->clear_screen();
}

void clear_line(void) {
    DAWN_BACKEND(app)->clear_line();
}

void clear_range(int32_t n) {
    DAWN_BACKEND(app)->clear_range(n);
}

void cursor_visible(bool visible) {
    DAWN_BACKEND(app)->set_cursor_visible(visible);
}

void cursor_home(void) {
    move_to(1, 1);
}

void sync_begin(void) {
    DAWN_BACKEND(app)->sync_begin();
}

void sync_end(void) {
    DAWN_BACKEND(app)->sync_end();
}

void fill_line_end(DawnColor bg) {
    // Only applies in print mode
    if (app.ctx.mode != DAWN_MODE_PRINT) return;

    int32_t cols = 0, rows = 0;
    DAWN_BACKEND(app)->get_size(&cols, &rows);
    if (cols <= 0) cols = 80;

    // Set background and fill to end of line
    set_bg(bg);
    // Use CSI K (erase to end of line) which uses current bg
    DAWN_BACKEND(app)->write_str("\x1b[K", 3);
}

// #endregion

// #region Theme Colors

DawnColor get_bg(void) {
    if (app.ctx.mode == DAWN_MODE_PRINT && app.ctx.host_bg) {
        return *app.ctx.host_bg;
    }
    return app.theme == THEME_DARK ? DARK_BG : LIGHT_BG;
}
DawnColor get_fg(void) { return app.theme == THEME_DARK ? DARK_FG : LIGHT_FG; }
DawnColor get_dim(void) { return app.theme == THEME_DARK ? DARK_DIM : LIGHT_DIM; }
DawnColor get_accent(void) { return app.theme == THEME_DARK ? DARK_ACCENT : LIGHT_ACCENT; }
DawnColor get_select(void) { return app.theme == THEME_DARK ? DARK_SELECT : LIGHT_SELECT; }
DawnColor get_ai_bg(void) { return app.theme == THEME_DARK ? DARK_AI_BG : LIGHT_AI_BG; }
DawnColor get_border(void) { return app.theme == THEME_DARK ? DARK_BORDER : LIGHT_BORDER; }
DawnColor get_code_bg(void) { return app.theme == THEME_DARK ? DARK_CODE_BG : LIGHT_CODE_BG; }
DawnColor get_modal_bg(void) { return app.theme == THEME_DARK ? DARK_MODAL_BG : LIGHT_MODAL_BG; }

// #endregion

// #region DawnColor Utilities

DawnColor color_lerp(DawnColor a, DawnColor b, float t) {
    return (DawnColor){
        (uint8_t)(a.r + (b.r - a.r) * t),
        (uint8_t)(a.g + (b.g - a.g) * t),
        (uint8_t)(a.b + (b.b - a.b) * t)
    };
}

// #endregion

// #region Text Attributes

void set_bold(bool on) {
    DAWN_BACKEND(app)->set_bold(on);
}

void set_italic(bool on) {
    DAWN_BACKEND(app)->set_italic(on);
}

void set_dim(bool on) {
    DAWN_BACKEND(app)->set_dim(on);
}

void set_strikethrough(bool on) {
    DAWN_BACKEND(app)->set_strike(on);
}

void reset_attrs(void) {
    DAWN_BACKEND(app)->reset_attrs();
}

// #endregion

// #region Styled Text

void set_underline(UnderlineStyle style) {
    DAWN_BACKEND(app)->set_underline(style);
}

void set_underline_color(DawnColor c) {
    DAWN_BACKEND(app)->set_underline_color(c);
}

void clear_underline(void) {
    DAWN_BACKEND(app)->clear_underline();
}

// #endregion

// #region Text Sizing

void print_scaled_char(char c, int32_t scale) {
    if (scale <= 1 || !dawn_ctx_has(&app.ctx, DAWN_CAP_TEXT_SIZING)) {
        DAWN_BACKEND(app)->write_char(c);
        return;
    }
    char str[2] = {c, '\0'};
    DAWN_BACKEND(app)->write_scaled(str, 1, scale);
}

void print_scaled_str(const char *str, size_t len, int32_t scale) {
    if (scale <= 1 || !dawn_ctx_has(&app.ctx, DAWN_CAP_TEXT_SIZING)) {
        DAWN_BACKEND(app)->write_str(str, len);
        return;
    }
    DAWN_BACKEND(app)->write_scaled(str, len, scale);
}

void print_scaled_frac_char(char c, int32_t scale, int32_t num, int32_t denom) {
    // No scaling needed if scale is 1 with no fractional part, or no text sizing support
    if ((scale <= 1 && (num == 0 || denom == 0)) || !dawn_ctx_has(&app.ctx, DAWN_CAP_TEXT_SIZING)) {
        DAWN_BACKEND(app)->write_char(c);
        return;
    }
    if (DAWN_BACKEND(app)->write_scaled_frac) {
        char str[2] = {c, '\0'};
        DAWN_BACKEND(app)->write_scaled_frac(str, 1, scale, num, denom);
    } else if (scale > 1) {
        // Fallback to integer scaling if fractional not supported
        char str[2] = {c, '\0'};
        DAWN_BACKEND(app)->write_scaled(str, 1, scale);
    } else {
        DAWN_BACKEND(app)->write_char(c);
    }
}

void print_scaled_frac_str(const char *str, size_t len, int32_t scale, int32_t num, int32_t denom) {
    // No scaling needed if scale is 1 with no fractional part, or no text sizing support
    if ((scale <= 1 && (num == 0 || denom == 0)) || !dawn_ctx_has(&app.ctx, DAWN_CAP_TEXT_SIZING)) {
        DAWN_BACKEND(app)->write_str(str, len);
        return;
    }
    if (DAWN_BACKEND(app)->write_scaled_frac) {
        DAWN_BACKEND(app)->write_scaled_frac(str, len, scale, num, denom);
    } else if (scale > 1) {
        // Fallback to integer scaling if fractional not supported
        DAWN_BACKEND(app)->write_scaled(str, len, scale);
    } else {
        DAWN_BACKEND(app)->write_str(str, len);
    }
}

// #endregion
