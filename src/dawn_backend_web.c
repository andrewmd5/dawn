// dawn_backend_web.c - Web/Emscripten Platform Backend
//! Canvas-based implementation of the platform abstraction layer
//! Uses Emscripten + Embind for browser integration

#ifdef __EMSCRIPTEN__

#include "dawn_types.h"
#include "dawn_wrap.h"  // for utf8_display_width

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>  // for strcasecmp
#include <time.h>
#include <emscripten.h>
#include <emscripten/html5.h>

// stb_image for image decoding
// When building for web, we compile dawn_backend_web.c (not dawn_backend_posix.c),
// so we need to define the implementation here
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_GIF
#define STBI_ONLY_BMP
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#include "stb_image.h"


#define CELL_WIDTH  10   // Pixels per character column
#define CELL_HEIGHT 20   // Pixels per character row
#define MAX_COLS    500
#define MAX_ROWS    200


static struct {
    bool initialized;
    uint32_t capabilities;
    int32_t cols;
    int32_t rows;
    int32_t cursor_col;      // Current write position (auto-advances)
    int32_t cursor_row;
    int32_t draw_cursor_col; // Where to draw visible cursor (set by set_cursor)
    int32_t draw_cursor_row;
    bool cursor_visible;

    // Current text attributes
    uint8_t fg_r, fg_g, fg_b;
    uint8_t bg_r, bg_g, bg_b;
    bool bold;
    bool italic;
    bool dim;
    bool underline;
    bool strikethrough;
    int32_t underline_style;
    uint8_t underline_r, underline_g, underline_b;

    int32_t pending_key;
    int32_t last_mouse_col;
    int32_t last_mouse_row;
    bool resize_needed;
    bool quit_requested;

    int32_t key_queue[64];
    int32_t key_queue_head;
    int32_t key_queue_tail;
} web_state = {0};


EM_JS(void, js_init_canvas, (), {
    let canvas = document.getElementById('dawn-canvas');
    if (!canvas) {
        canvas = document.createElement('canvas');
        canvas.id = 'dawn-canvas';
        document.body.appendChild(canvas);
    }

    document.body.style.margin = '0';
    document.body.style.padding = '0';
    document.body.style.overflow = 'hidden';
    document.body.style.backgroundDawnColor = '#1a1a2e';

    canvas.style.display = 'block';

    window.dawnDPR = window.devicePixelRatio || 1;
    window.dawnCtx = canvas.getContext('2d');
    window.dawnFontSize = 14;

    const updateSize = () => {
        const dpr = window.dawnDPR;
        const width = window.innerWidth;
        const height = window.innerHeight;

        canvas.width = width * dpr;
        canvas.height = height * dpr;
        canvas.style.width = width + 'px';
        canvas.style.height = height + 'px';

        window.dawnCtx.setTransform(dpr, 0, 0, dpr, 0, 0);

        const font = window.dawnFontSize + 'px "SF Mono", "Monaco", "Menlo", "Consolas", "DejaVu Sans Mono", monospace';
        window.dawnCtx.font = font;
        window.dawnCtx.textBaseline = 'top';

        const metrics = window.dawnCtx.measureText('M');
        window.dawnCellWidth = Math.ceil(metrics.width);
        window.dawnCellHeight = window.dawnFontSize + 4;

        window.dawnCols = Math.floor(width / window.dawnCellWidth);
        window.dawnRows = Math.floor(height / window.dawnCellHeight);

        if (Module._web_on_resize) {
            Module._web_on_resize(window.dawnCols, window.dawnRows);
        }
    };

    window.addEventListener('resize', updateSize);
    updateSize();
});

EM_JS(void, js_get_size, (int32_t* cols, int32_t* rows), {
    setValue(cols, window.dawnCols || 80, 'i32');
    setValue(rows, window.dawnRows || 24, 'i32');
});

EM_JS(void, js_clear_screen, (int32_t r, int32_t g, int32_t b), {
    const ctx = window.dawnCtx;
    ctx.fillStyle = `rgb(${r},${g},${b})`;
    ctx.fillRect(0, 0, window.innerWidth, window.innerHeight);
});

EM_JS(void, js_set_font, (int32_t bold, int32_t italic), {
    const style = (italic ? "italic " : "") + (bold ? "bold " : "");
    window.dawnCtx.font = style + window.dawnFontSize + "px 'SF Mono', 'Monaco', 'Menlo', 'Consolas', 'DejaVu Sans Mono', monospace";
});

EM_JS(void, js_draw_text_scaled, (int32_t col, int32_t row, const char* text, int32_t scale,
                                   int32_t fg_r, int32_t fg_g, int32_t fg_b,
                                   int32_t bg_r, int32_t bg_g, int32_t bg_b,
                                   int32_t bold, int32_t italic, int32_t dim), {
    const ctx = window.dawnCtx;
    const str = UTF8ToString(text);
    const x = (col - 1) * window.dawnCellWidth;
    const y = (row - 1) * window.dawnCellHeight;

    const scaledFontSize = window.dawnFontSize * scale;
    const width = str.length * scale * window.dawnCellWidth;
    const height = scale * window.dawnCellHeight;

    if (bg_r >= 0) {
        ctx.fillStyle = `rgb(${bg_r},${bg_g},${bg_b})`;
        ctx.fillRect(x, y, width, height);
    }

    const style = (italic ? "italic " : "") + (bold ? "bold " : "");
    ctx.font = style + scaledFontSize + "px 'SF Mono', 'Monaco', 'Menlo', 'Consolas', 'DejaVu Sans Mono', monospace";

    let alpha = dim ? 0.6 : 1.0;
    ctx.fillStyle = `rgba(${fg_r},${fg_g},${fg_b},${alpha})`;
    ctx.fillText(str, x, y + 2);

    ctx.font = window.dawnFontSize + "px 'SF Mono', 'Monaco', 'Menlo', 'Consolas', 'DejaVu Sans Mono', monospace";
});

// Fractional scaling version - scale is cell scale, font_scale is actual font multiplier
EM_JS(void, js_draw_text_scaled_frac, (int32_t col, int32_t row, const char* text, int32_t cell_scale, double font_scale,
                                        int32_t fg_r, int32_t fg_g, int32_t fg_b,
                                        int32_t bg_r, int32_t bg_g, int32_t bg_b,
                                        int32_t bold, int32_t italic, int32_t dim), {
    const ctx = window.dawnCtx;
    const str = UTF8ToString(text);
    const x = (col - 1) * window.dawnCellWidth;
    const y = (row - 1) * window.dawnCellHeight;

    // Cell space is determined by cell_scale, font size by font_scale
    const scaledFontSize = window.dawnFontSize * font_scale;
    const width = str.length * cell_scale * window.dawnCellWidth;
    const height = cell_scale * window.dawnCellHeight;

    if (bg_r >= 0) {
        ctx.fillStyle = `rgb(${bg_r},${bg_g},${bg_b})`;
        ctx.fillRect(x, y, width, height);
    }

    const style = (italic ? "italic " : "") + (bold ? "bold " : "");
    ctx.font = style + scaledFontSize + "px 'SF Mono', 'Monaco', 'Menlo', 'Consolas', 'DejaVu Sans Mono', monospace";

    let alpha = dim ? 0.6 : 1.0;
    ctx.fillStyle = `rgba(${fg_r},${fg_g},${fg_b},${alpha})`;
    ctx.fillText(str, x, y + 2);

    ctx.font = window.dawnFontSize + "px 'SF Mono', 'Monaco', 'Menlo', 'Consolas', 'DejaVu Sans Mono', monospace";
});

EM_JS(void, js_draw_text, (int32_t col, int32_t row, const char* text, int32_t num_cols,
                           int32_t fg_r, int32_t fg_g, int32_t fg_b,
                           int32_t bg_r, int32_t bg_g, int32_t bg_b,
                           int32_t bold, int32_t italic, int32_t dim,
                           int32_t underline, int32_t strikethrough), {
    const ctx = window.dawnCtx;
    const str = UTF8ToString(text);
    const x = (col - 1) * window.dawnCellWidth;
    const y = (row - 1) * window.dawnCellHeight;
    const width = num_cols * window.dawnCellWidth;

    if (bg_r >= 0) {
        ctx.fillStyle = `rgb(${bg_r},${bg_g},${bg_b})`;
        ctx.fillRect(x, y, width, window.dawnCellHeight);
    }

    const style = (italic ? "italic " : "") + (bold ? "bold " : "");
    ctx.font = style + window.dawnFontSize + "px 'SF Mono', 'Monaco', 'Menlo', 'Consolas', 'DejaVu Sans Mono', monospace";

    let alpha = dim ? 0.6 : 1.0;
    ctx.fillStyle = `rgba(${fg_r},${fg_g},${fg_b},${alpha})`;
    ctx.fillText(str, x, y + 2);

    if (underline) {
        ctx.strokeStyle = ctx.fillStyle;
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.moveTo(x, y + window.dawnCellHeight - 2);
        ctx.lineTo(x + width, y + window.dawnCellHeight - 2);
        ctx.stroke();
    }

    if (strikethrough) {
        ctx.strokeStyle = ctx.fillStyle;
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.moveTo(x, y + window.dawnCellHeight / 2);
        ctx.lineTo(x + width, y + window.dawnCellHeight / 2);
        ctx.stroke();
    }
});

EM_JS(void, js_draw_cursor, (int32_t col, int32_t row, int32_t r, int32_t g, int32_t b), {
    const ctx = window.dawnCtx;
    const x = (col - 1) * window.dawnCellWidth;
    const y = (row - 1) * window.dawnCellHeight;

    if (window.dawnDebug) {
        ctx.fillStyle = 'rgba(0,0,0,0.8)';
        ctx.fillRect(5, 5, 120, 40);
        ctx.fillStyle = 'rgba(0,255,0,0.9)';
        ctx.font = '12px monospace';
        ctx.fillText('R' + row + ' C' + col, 10, 22);
        ctx.fillText('Rows:' + window.dawnRows, 10, 38);
    }

    ctx.fillStyle = `rgb(${r},${g},${b})`;
    ctx.fillRect(x, y, 2, window.dawnCellHeight);
});

EM_JS(void, js_clear_rect, (int32_t col, int32_t row, int32_t width, int32_t height, int32_t r, int32_t g, int32_t b), {
    const ctx = window.dawnCtx;
    const x = (col - 1) * window.dawnCellWidth;
    const y = (row - 1) * window.dawnCellHeight;

    ctx.fillStyle = `rgb(${r},${g},${b})`;
    ctx.fillRect(x, y, width * window.dawnCellWidth, height * window.dawnCellHeight);
});

EM_JS(void, js_setup_input, (), {
    const canvas = document.getElementById('dawn-canvas');

    canvas.tabIndex = 0;
    canvas.focus();

    canvas.addEventListener('keydown', (e) => {
        e.preventDefault();

        let key = 0;

        if (e.key === 'ArrowUp') key = e.shiftKey ? 1009 : 1000;
        else if (e.key === 'ArrowDown') key = e.shiftKey ? 1010 : 1001;
        else if (e.key === 'ArrowRight') key = e.shiftKey ? 1012 : 1002;
        else if (e.key === 'ArrowLeft') key = e.shiftKey ? 1011 : 1003;
        else if (e.key === 'Home') key = 1004;
        else if (e.key === 'End') key = 1005;
        else if (e.key === 'PageUp') key = 1006;
        else if (e.key === 'PageDown') key = 1007;
        else if (e.key === 'Delete') key = 1008;
        else if (e.key === 'Tab' && e.shiftKey) key = 1023;
        else if (e.key === 'Tab') key = 9;
        else if (e.key === 'Backspace') key = 127;
        else if (e.key === 'Enter') key = 13;
        else if (e.key === 'Escape') key = 27;
        else if (e.ctrlKey && e.key.length === 1) {
            const code = e.key.toLowerCase().charCodeAt(0);
            if (code >= 97 && code <= 122) {
                key = code - 96;
            }
        }
        else if (e.key.length === 1) {
            key = e.key.charCodeAt(0);
        }

        if (e.altKey || e.metaKey) {
            if (e.key === 'ArrowLeft') key = e.shiftKey ? 1019 : 1017;
            else if (e.key === 'ArrowRight') key = e.shiftKey ? 1020 : 1018;
        }

        if (e.ctrlKey) {
            if (e.key === 'ArrowLeft') key = e.shiftKey ? 1015 : 1013;
            else if (e.key === 'ArrowRight') key = e.shiftKey ? 1016 : 1014;
        }

        if (key > 0) {
            Module._web_on_key(key);
        }
    });

    canvas.addEventListener('mousedown', (e) => {
        const col = Math.floor(e.offsetX / window.dawnCellWidth) + 1;
        const row = Math.floor(e.offsetY / window.dawnCellHeight) + 1;
        Module._web_on_mouse(col, row, e.button, 1);
    });

    canvas.addEventListener('mouseup', (e) => {
        const col = Math.floor(e.offsetX / window.dawnCellWidth) + 1;
        const row = Math.floor(e.offsetY / window.dawnCellHeight) + 1;
        Module._web_on_mouse(col, row, e.button, 0);
    });

    let lastWheelTime = 0;
    let wheelAccum = 0;
    canvas.addEventListener('wheel', (e) => {
        e.preventDefault();

        const now = Date.now();
        let delta = e.deltaY;
        if (e.deltaMode === 1) delta *= 20;
        if (e.deltaMode === 2) delta *= 100;

        wheelAccum += delta;

        if (now - lastWheelTime < 16) return;
        lastWheelTime = now;

        const lines = Math.round(wheelAccum / 40);
        if (lines === 0) return;
        wheelAccum = 0;

        const key = lines < 0 ? 1021 : 1022;
        const count = Math.min(Math.abs(lines), 5);
        for (let i = 0; i < count; i++) {
            Module._web_on_key(key);
        }
    }, { passive: false });

    canvas.addEventListener('blur', () => {
        setTimeout(() => canvas.focus(), 10);
    });
});

// Clipboard
EM_JS(void, js_clipboard_copy, (const char* text), {
    const str = UTF8ToString(text);
    navigator.clipboard.writeText(str).catch(err => {
        console.error('Failed to copy:', err);
    });
});

EM_ASYNC_JS(char*, js_clipboard_paste, (), {
    try {
        const text = await navigator.clipboard.readText();
        const len = lengthBytesUTF8(text) + 1;
        const ptr = _malloc(len);
        stringToUTF8(text, ptr, len);
        return ptr;
    } catch (err) {
        console.error('Failed to paste:', err);
        return 0;
    }
});

// File system (using IndexedDB via Emscripten's IDBFS)
EM_JS(void, js_init_filesystem, (), {
    // Create /dawn directory if it doesn't exist
    try {
        FS.mkdir('/dawn');
    } catch (e) {
        // Directory may already exist
    }

    // Mount IDBFS for persistent storage
    try {
        FS.mount(IDBFS, {}, '/dawn');
    } catch (e) {
        console.log('IDBFS mount:', e.message);
    }

    // Sync from IndexedDB to memory
    FS.syncfs(true, (err) => {
        if (err) console.error('FS sync error:', err);
        else console.log('Filesystem synced from IndexedDB');
    });
});

EM_JS(void, js_sync_filesystem, (), {
    // Sync from memory to IndexedDB
    FS.syncfs(false, (err) => {
        if (err) console.error('FS sync error:', err);
    });
});

// Images
EM_JS(int32_t, js_display_image, (const char* path, int32_t row, int32_t col, int32_t maxWidth, int32_t maxHeight), {
    const pathStr = UTF8ToString(path);

    // Create image element if not exists
    let img = window.dawnImages?.[pathStr];
    if (!img) {
        window.dawnImages = window.dawnImages || {};
        img = new Image();
        img.src = pathStr;
        window.dawnImages[pathStr] = img;

        // Return 0 while loading
        if (!img.complete) return 0;
    }

    if (!img.complete || !img.naturalWidth) return 0;

    const ctx = window.dawnCtx;
    const x = (col - 1) * window.dawnCellWidth;
    const y = (row - 1) * window.dawnCellHeight;

    // Calculate scaled dimensions
    const maxW = maxWidth * window.dawnCellWidth;
    const maxH = maxHeight > 0 ? maxHeight * window.dawnCellHeight : img.naturalHeight;

    let w = img.naturalWidth;
    let h = img.naturalHeight;

    if (w > maxW) {
        h = h * maxW / w;
        w = maxW;
    }
    if (h > maxH) {
        w = w * maxH / h;
        h = maxH;
    }

    ctx.drawImage(img, x, y, w, h);

    // Return number of rows occupied
    return Math.ceil(h / window.dawnCellHeight);
});

EM_JS(int32_t, js_get_image_size, (const char* path, int32_t* width, int32_t* height), {
    const pathStr = UTF8ToString(path);

    let img = window.dawnImages?.[pathStr];
    if (!img) {
        window.dawnImages = window.dawnImages || {};
        img = new Image();
        img.src = pathStr;
        window.dawnImages[pathStr] = img;
    }

    if (!img.complete || !img.naturalWidth) {
        setValue(width, 0, 'i32');
        setValue(height, 0, 'i32');
        return 0;
    }

    setValue(width, img.naturalWidth, 'i32');
    setValue(height, img.naturalHeight, 'i32');
    return 1;
});

// Time
EM_JS(double, js_time_now_double, (), {
    return Date.now() / 1000.0;
});

EM_JS(double, js_time_now_ms_double, (), {
    return Date.now();
});

EM_JS(void, js_get_local_time, (int32_t* year, int32_t* mon, int32_t* mday,
                                 int32_t* hour, int32_t* min, int32_t* sec, int32_t* wday), {
    const d = new Date();
    setValue(year, d.getFullYear(), 'i32');
    setValue(mon, d.getMonth(), 'i32');
    setValue(mday, d.getDate(), 'i32');
    setValue(hour, d.getHours(), 'i32');
    setValue(min, d.getMinutes(), 'i32');
    setValue(sec, d.getSeconds(), 'i32');
    setValue(wday, d.getDay(), 'i32');
});

EM_JS(const char*, js_get_username, (), {
    // Try to get from localStorage or return default
    const name = localStorage.getItem('dawn_username') || 'Writer';
    const len = lengthBytesUTF8(name) + 1;
    const ptr = _malloc(len);
    stringToUTF8(name, ptr, len);
    return ptr;
});


// Called from JS when window resizes
EMSCRIPTEN_KEEPALIVE
void web_on_resize(int32_t cols, int32_t rows) {
    web_state.cols = cols;
    web_state.rows = rows;
    web_state.resize_needed = true;
}

// Called from JS on key press
EMSCRIPTEN_KEEPALIVE
void web_on_key(int32_t key) {
    // Add to key queue
    int32_t next_tail = (web_state.key_queue_tail + 1) % 64;
    if (next_tail != web_state.key_queue_head) {
        web_state.key_queue[web_state.key_queue_tail] = key;
        web_state.key_queue_tail = next_tail;
    }
}

// Called from JS on mouse event
EMSCRIPTEN_KEEPALIVE
void web_on_mouse(int32_t col, int32_t row, int32_t button, int32_t pressed) {
    web_state.last_mouse_col = col;
    web_state.last_mouse_row = row;

    // Queue a click event on mouse down (left button only)
    if (pressed && button == 0) {
        int32_t next_tail = (web_state.key_queue_tail + 1) % 64;
        if (next_tail != web_state.key_queue_head) {
            web_state.key_queue[web_state.key_queue_tail] = DAWN_KEY_MOUSE_CLICK;
            web_state.key_queue_tail = next_tail;
        }
    }
}


static bool web_init(DawnMode mode) {
    (void)mode;  // Web platform only supports interactive mode
    if (web_state.initialized) return true;

    js_init_canvas();
    js_setup_input();
    js_init_filesystem();

    // Set default colors
    web_state.fg_r = 212; web_state.fg_g = 212; web_state.fg_b = 212;
    web_state.bg_r = 26;  web_state.bg_g = 26;  web_state.bg_b = 46;

    // Query initial size
    js_get_size(&web_state.cols, &web_state.rows);

    web_state.cursor_col = 1;
    web_state.cursor_row = 1;
    web_state.draw_cursor_col = 1;
    web_state.draw_cursor_row = 1;
    web_state.cursor_visible = true;

    web_state.capabilities =
        DAWN_CAP_TRUE_COLOR |
        DAWN_CAP_SYNC_OUTPUT |
        DAWN_CAP_STYLED_UNDERLINE |
        DAWN_CAP_TEXT_SIZING |
        DAWN_CAP_IMAGES |
        DAWN_CAP_MOUSE |
        DAWN_CAP_BRACKETED_PASTE |
        DAWN_CAP_CLIPBOARD;

    web_state.initialized = true;
    return true;
}

static void web_shutdown(void) {
    js_sync_filesystem();
    web_state.initialized = false;
}

static uint32_t web_get_capabilities(void) {
    return web_state.capabilities;
}

static void web_get_size(int32_t *out_cols, int32_t *out_rows) {
    js_get_size(&web_state.cols, &web_state.rows);
    if (out_cols) *out_cols = web_state.cols;
    if (out_rows) *out_rows = web_state.rows;
}

static void web_set_cursor(int32_t col, int32_t row) {
    web_state.cursor_col = col;
    web_state.cursor_row = row;
    // Also update draw position - this is where cursor will be drawn
    web_state.draw_cursor_col = col;
    web_state.draw_cursor_row = row;
}

static void web_set_cursor_visible(bool visible) {
    web_state.cursor_visible = visible;
}

static void web_set_fg(DawnColor color) {
    web_state.fg_r = color.r;
    web_state.fg_g = color.g;
    web_state.fg_b = color.b;
}

static void web_set_bg(DawnColor color) {
    web_state.bg_r = color.r;
    web_state.bg_g = color.g;
    web_state.bg_b = color.b;
}

static void web_reset_attrs(void) {
    web_state.bold = false;
    web_state.italic = false;
    web_state.dim = false;
    web_state.underline = false;
    web_state.strikethrough = false;
    web_state.fg_r = 212; web_state.fg_g = 212; web_state.fg_b = 212;
    web_state.bg_r = 26;  web_state.bg_g = 26;  web_state.bg_b = 46;
}

static void web_set_bold(bool enabled) {
    web_state.bold = enabled;
}

static void web_set_italic(bool enabled) {
    web_state.italic = enabled;
}

static void web_set_dim(bool enabled) {
    web_state.dim = enabled;
}

static void web_set_strikethrough(bool enabled) {
    web_state.strikethrough = enabled;
}

static void web_set_underline(DawnUnderline style) {
    web_state.underline = true;
    web_state.underline_style = style;
}

static void web_set_underline_color(DawnColor color) {
    web_state.underline_r = color.r;
    web_state.underline_g = color.g;
    web_state.underline_b = color.b;
}

static void web_clear_underline(void) {
    web_state.underline = false;
}

static void web_clear_screen(void) {
    js_clear_screen(web_state.bg_r, web_state.bg_g, web_state.bg_b);
}

static void web_clear_line(void) {
    js_clear_rect(1, web_state.cursor_row, web_state.cols, 1,
                  web_state.bg_r, web_state.bg_g, web_state.bg_b);
}

static void web_clear_range(int32_t count) {
    if (count <= 0) return;
    js_clear_rect(web_state.cursor_col, web_state.cursor_row, count, 1,
                  web_state.bg_r, web_state.bg_g, web_state.bg_b);
}

// Parse ANSI SGR (Select Graphic Rendition) parameters and update state
static void parse_ansi_sgr(const char *params, size_t len) {
    // Parse semicolon-separated numbers
    int32_t nums[16];
    int32_t num_count = 0;
    int32_t current = 0;
    bool has_current = false;

    for (size_t i = 0; i <= len && num_count < 16; i++) {
        char c = (i < len) ? params[i] : ';';
        if (c >= '0' && c <= '9') {
            current = current * 10 + (c - '0');
            has_current = true;
        } else if (c == ';' || c == 'm') {
            if (has_current || i == 0 || params[i-1] == ';') {
                nums[num_count++] = has_current ? current : 0;
            }
            current = 0;
            has_current = false;
        }
    }

    // Process SGR codes
    for (int32_t i = 0; i < num_count; i++) {
        int32_t code = nums[i];
        switch (code) {
            case 0:  // Reset
                web_state.bold = false;
                web_state.italic = false;
                web_state.dim = false;
                web_state.underline = false;
                web_state.strikethrough = false;
                web_state.fg_r = 212; web_state.fg_g = 212; web_state.fg_b = 212;
                web_state.bg_r = 26;  web_state.bg_g = 26;  web_state.bg_b = 46;
                break;
            case 1: web_state.bold = true; break;
            case 2: web_state.dim = true; break;
            case 3: web_state.italic = true; break;
            case 4: web_state.underline = true; break;
            case 9: web_state.strikethrough = true; break;
            case 22: web_state.bold = false; web_state.dim = false; break;
            case 23: web_state.italic = false; break;
            case 24: web_state.underline = false; break;
            case 29: web_state.strikethrough = false; break;

            // Standard foreground colors (30-37)
            case 30: web_state.fg_r = 0;   web_state.fg_g = 0;   web_state.fg_b = 0;   break;  // Black
            case 31: web_state.fg_r = 205; web_state.fg_g = 49;  web_state.fg_b = 49;  break;  // Red
            case 32: web_state.fg_r = 13;  web_state.fg_g = 188; web_state.fg_b = 121; break;  // Green
            case 33: web_state.fg_r = 229; web_state.fg_g = 192; web_state.fg_b = 123; break;  // Yellow
            case 34: web_state.fg_r = 97;  web_state.fg_g = 175; web_state.fg_b = 239; break;  // Blue
            case 35: web_state.fg_r = 198; web_state.fg_g = 120; web_state.fg_b = 221; break;  // Magenta
            case 36: web_state.fg_r = 86;  web_state.fg_g = 182; web_state.fg_b = 194; break;  // Cyan
            case 37: web_state.fg_r = 212; web_state.fg_g = 212; web_state.fg_b = 212; break;  // White
            case 39: web_state.fg_r = 212; web_state.fg_g = 212; web_state.fg_b = 212; break;  // Default

            // Bright foreground colors (90-97)
            case 90: web_state.fg_r = 102; web_state.fg_g = 102; web_state.fg_b = 102; break;  // Bright black
            case 91: web_state.fg_r = 255; web_state.fg_g = 85;  web_state.fg_b = 85;  break;  // Bright red
            case 92: web_state.fg_r = 85;  web_state.fg_g = 255; web_state.fg_b = 85;  break;  // Bright green
            case 93: web_state.fg_r = 255; web_state.fg_g = 255; web_state.fg_b = 85;  break;  // Bright yellow
            case 94: web_state.fg_r = 85;  web_state.fg_g = 85;  web_state.fg_b = 255; break;  // Bright blue
            case 95: web_state.fg_r = 255; web_state.fg_g = 85;  web_state.fg_b = 255; break;  // Bright magenta
            case 96: web_state.fg_r = 85;  web_state.fg_g = 255; web_state.fg_b = 255; break;  // Bright cyan
            case 97: web_state.fg_r = 255; web_state.fg_g = 255; web_state.fg_b = 255; break;  // Bright white

            // 256-color and 24-bit color
            case 38:
                if (i + 2 < num_count && nums[i + 1] == 5) {
                    // 256-color: 38;5;n
                    int32_t n = nums[i + 2];
                    i += 2;
                    if (n < 16) {
                        // Standard colors - use the 30-37/90-97 mapping
                        static const uint8_t std_colors[16][3] = {
                            {0, 0, 0}, {205, 49, 49}, {13, 188, 121}, {229, 192, 123},
                            {97, 175, 239}, {198, 120, 221}, {86, 182, 194}, {212, 212, 212},
                            {102, 102, 102}, {255, 85, 85}, {85, 255, 85}, {255, 255, 85},
                            {85, 85, 255}, {255, 85, 255}, {85, 255, 255}, {255, 255, 255}
                        };
                        web_state.fg_r = std_colors[n][0];
                        web_state.fg_g = std_colors[n][1];
                        web_state.fg_b = std_colors[n][2];
                    } else if (n < 232) {
                        // 216-color cube (16-231)
                        n -= 16;
                        web_state.fg_r = (n / 36) * 51;
                        web_state.fg_g = ((n / 6) % 6) * 51;
                        web_state.fg_b = (n % 6) * 51;
                    } else {
                        // Grayscale (232-255)
                        int32_t gray = (n - 232) * 10 + 8;
                        web_state.fg_r = gray;
                        web_state.fg_g = gray;
                        web_state.fg_b = gray;
                    }
                } else if (i + 4 < num_count && nums[i + 1] == 2) {
                    // 24-bit color: 38;2;r;g;b
                    web_state.fg_r = nums[i + 2];
                    web_state.fg_g = nums[i + 3];
                    web_state.fg_b = nums[i + 4];
                    i += 4;
                }
                break;

            // Background colors (40-47, 100-107, 48)
            case 40: web_state.bg_r = 0;   web_state.bg_g = 0;   web_state.bg_b = 0;   break;
            case 41: web_state.bg_r = 205; web_state.bg_g = 49;  web_state.bg_b = 49;  break;
            case 42: web_state.bg_r = 13;  web_state.bg_g = 188; web_state.bg_b = 121; break;
            case 43: web_state.bg_r = 229; web_state.bg_g = 192; web_state.bg_b = 123; break;
            case 44: web_state.bg_r = 97;  web_state.bg_g = 175; web_state.bg_b = 239; break;
            case 45: web_state.bg_r = 198; web_state.bg_g = 120; web_state.bg_b = 221; break;
            case 46: web_state.bg_r = 86;  web_state.bg_g = 182; web_state.bg_b = 194; break;
            case 47: web_state.bg_r = 212; web_state.bg_g = 212; web_state.bg_b = 212; break;
            case 49: web_state.bg_r = 26;  web_state.bg_g = 26;  web_state.bg_b = 46;  break;  // Default

            case 48:
                if (i + 2 < num_count && nums[i + 1] == 5) {
                    // 256-color background
                    int32_t n = nums[i + 2];
                    i += 2;
                    if (n < 16) {
                        static const uint8_t std_colors[16][3] = {
                            {0, 0, 0}, {205, 49, 49}, {13, 188, 121}, {229, 192, 123},
                            {97, 175, 239}, {198, 120, 221}, {86, 182, 194}, {212, 212, 212},
                            {102, 102, 102}, {255, 85, 85}, {85, 255, 85}, {255, 255, 85},
                            {85, 85, 255}, {255, 85, 255}, {85, 255, 255}, {255, 255, 255}
                        };
                        web_state.bg_r = std_colors[n][0];
                        web_state.bg_g = std_colors[n][1];
                        web_state.bg_b = std_colors[n][2];
                    } else if (n < 232) {
                        n -= 16;
                        web_state.bg_r = (n / 36) * 51;
                        web_state.bg_g = ((n / 6) % 6) * 51;
                        web_state.bg_b = (n % 6) * 51;
                    } else {
                        int32_t gray = (n - 232) * 10 + 8;
                        web_state.bg_r = gray;
                        web_state.bg_g = gray;
                        web_state.bg_b = gray;
                    }
                } else if (i + 4 < num_count && nums[i + 1] == 2) {
                    // 24-bit background
                    web_state.bg_r = nums[i + 2];
                    web_state.bg_g = nums[i + 3];
                    web_state.bg_b = nums[i + 4];
                    i += 4;
                }
                break;
        }
    }
}

// Output a single text segment (no ANSI codes)
static void web_output_text(const char *str, size_t len) {
    if (len == 0) return;

    char *buf = malloc(len + 1);
    if (!buf) return;
    memcpy(buf, str, len);
    buf[len] = '\0';

    int32_t display_width = utf8_display_width(str, len);

    js_draw_text(web_state.cursor_col, web_state.cursor_row, buf, display_width,
                 web_state.fg_r, web_state.fg_g, web_state.fg_b,
                 web_state.bg_r, web_state.bg_g, web_state.bg_b,
                 web_state.bold, web_state.italic, web_state.dim,
                 web_state.underline, web_state.strikethrough);

    web_state.cursor_col += display_width;
    free(buf);
}

static void web_write_str(const char *str, size_t len) {
    if (len == 0) return;

    // Parse and handle ANSI escape sequences
    size_t i = 0;
    size_t text_start = 0;

    while (i < len) {
        // Check for ESC (0x1b)
        if (str[i] == '\x1b' && i + 1 < len && str[i + 1] == '[') {
            // Output any pending text before the escape sequence
            if (i > text_start) {
                web_output_text(str + text_start, i - text_start);
            }

            // Find end of CSI sequence (ends with letter)
            size_t seq_start = i + 2;  // After ESC[
            size_t seq_end = seq_start;
            while (seq_end < len && !((str[seq_end] >= 'A' && str[seq_end] <= 'Z') ||
                                       (str[seq_end] >= 'a' && str[seq_end] <= 'z'))) {
                seq_end++;
            }

            if (seq_end < len) {
                char cmd = str[seq_end];
                if (cmd == 'm') {
                    // SGR sequence - parse color/style codes
                    parse_ansi_sgr(str + seq_start, seq_end - seq_start);
                }
                // Skip other CSI sequences (cursor movement, etc.)
                i = seq_end + 1;
                text_start = i;
            } else {
                // Incomplete sequence, output as-is
                i++;
            }
        } else {
            i++;
        }
    }

    // Output remaining text
    if (i > text_start) {
        web_output_text(str + text_start, i - text_start);
    }
}

static void web_write_char(char c) {
    char buf[2] = {c, '\0'};
    js_draw_text(web_state.cursor_col, web_state.cursor_row, buf, 1,
                 web_state.fg_r, web_state.fg_g, web_state.fg_b,
                 web_state.bg_r, web_state.bg_g, web_state.bg_b,
                 web_state.bold, web_state.italic, web_state.dim,
                 web_state.underline, web_state.strikethrough);
    web_state.cursor_col++;
}

static void web_repeat_char(char c, int32_t n) {
    if (n <= 0) return;
    if (c == ' ') {
        js_clear_rect(web_state.cursor_col, web_state.cursor_row, n, 1,
                      web_state.bg_r, web_state.bg_g, web_state.bg_b);
    } else {
        for (int32_t i = 0; i < n; i++) {
            char buf[2] = {c, '\0'};
            js_draw_text(web_state.cursor_col + i, web_state.cursor_row, buf, 1,
                         web_state.fg_r, web_state.fg_g, web_state.fg_b,
                         web_state.bg_r, web_state.bg_g, web_state.bg_b,
                         web_state.bold, web_state.italic, web_state.dim,
                         web_state.underline, web_state.strikethrough);
        }
    }
    web_state.cursor_col += n;
}

static void web_write_scaled(const char *str, size_t len, int32_t scale) {
    if (scale <= 1) {
        web_write_str(str, len);
        return;
    }

    // Make null-terminated copy
    char *buf = malloc(len + 1);
    if (!buf) return;
    memcpy(buf, str, len);
    buf[len] = '\0';

    // Calculate display width (each char takes scale cells)
    int32_t display_width = utf8_display_width(str, len) * scale;

    js_draw_text_scaled(web_state.cursor_col, web_state.cursor_row, buf, scale,
                        web_state.fg_r, web_state.fg_g, web_state.fg_b,
                        web_state.bg_r, web_state.bg_g, web_state.bg_b,
                        web_state.bold, web_state.italic, web_state.dim);

    // Auto-advance cursor by scaled width
    web_state.cursor_col += display_width;

    free(buf);
}

static void web_write_scaled_frac(const char *str, size_t len, int32_t scale, int32_t num, int32_t denom) {
    // No scaling needed
    if (scale <= 1 && (num == 0 || denom == 0)) {
        web_write_str(str, len);
        return;
    }

    // Make null-terminated copy
    char *buf = malloc(len + 1);
    if (!buf) return;
    memcpy(buf, str, len);
    buf[len] = '\0';

    // Calculate display width (each char takes cell_scale cells)
    int32_t display_width = utf8_display_width(str, len) * scale;

    // Calculate effective font scale: scale * (num / denom)
    double font_scale = (double)scale;
    if (num > 0 && denom > 0 && num < denom) {
        font_scale = (double)scale * (double)num / (double)denom;
    }

    js_draw_text_scaled_frac(web_state.cursor_col, web_state.cursor_row, buf,
                              scale, font_scale,
                              web_state.fg_r, web_state.fg_g, web_state.fg_b,
                              web_state.bg_r, web_state.bg_g, web_state.bg_b,
                              web_state.bold, web_state.italic, web_state.dim);

    // Auto-advance cursor by cell scale width
    web_state.cursor_col += display_width;

    free(buf);
}

static void web_flush(void) {
    // Draw cursor at the position set by set_cursor (not the auto-advancing cursor_col)
    if (web_state.cursor_visible) {
        js_draw_cursor(web_state.draw_cursor_col, web_state.draw_cursor_row,
                       web_state.fg_r, web_state.fg_g, web_state.fg_b);
    }
}

static void web_sync_begin(void) {
    // Canvas doesn't need sync - double buffering is automatic
}

static void web_sync_end(void) {
    // No-op
}

static void web_set_title(const char *title) {
    EM_ASM({
        document.title = UTF8ToString($0);
    }, title);
}

static void web_link_begin(const char *url) {
    // TODO: Track current link URL for click handling
    (void)url;
}

static void web_link_end(void) {
    // TODO: Clear current link URL
}

static int32_t web_read_key(void) {
    
    if (web_state.key_queue_head == web_state.key_queue_tail) {
        return DAWN_KEY_NONE;
    }

    int32_t key = web_state.key_queue[web_state.key_queue_head];
    web_state.key_queue_head = (web_state.key_queue_head + 1) % 64;
    return key;
}

static int32_t web_get_last_mouse_col(void) {
    return web_state.last_mouse_col;
}

static int32_t web_get_last_mouse_row(void) {
    return web_state.last_mouse_row;
}

static bool web_check_resize(void) {
    if (web_state.resize_needed) {
        web_state.resize_needed = false;
        return true;
    }
    return false;
}

static bool web_check_quit(void) {
    return web_state.quit_requested;
}

static bool web_input_available(float timeout_ms) {
    // In web, we don't block - just check if there's input
    (void)timeout_ms;
    return web_state.key_queue_head != web_state.key_queue_tail;
}

static void web_register_signals(void (*on_resize)(int32_t), void (*on_quit)(int32_t)) {
    // Not used in web - we handle events via JS callbacks
    (void)on_resize;
    (void)on_quit;
}

static void web_clipboard_copy(const char *text, size_t len) {
    char *buf = malloc(len + 1);
    if (!buf) return;
    memcpy(buf, text, len);
    buf[len] = '\0';
    js_clipboard_copy(buf);
    free(buf);
}

static char *web_clipboard_paste(size_t *out_len) {
    char *text = js_clipboard_paste();
    if (text && out_len) {
        *out_len = strlen(text);
    }
    return text;
}

static const char *web_get_home_dir(void) {
    return "/dawn";
}

static bool web_mkdir_p(const char *path) {
    // Emscripten's FS handles this
    return EM_ASM_INT({
        try {
            const path = UTF8ToString($0);
            const parts = path.split('/').filter(p => p);
            let current = "";
            for (const part of parts) {
                current += '/' + part;
                try {
                    FS.mkdir(current);
                } catch (e) {
                    // EEXIST (errno 20) is fine - directory already exists
                    if (e.errno !== 20) {
                        // Only log unexpected errors
                        // console.error('mkdir_p error:', e);
                    }
                }
            }
            return 1;
        } catch (e) {
            return 0;
        }
    }, path);
}

static bool web_file_exists(const char *path) {
    return EM_ASM_INT({
        try {
            const path = UTF8ToString($0);
            FS.stat(path);
            return 1;
        } catch (e) {
            return 0;
        }
    }, path);
}

static char *web_read_file(const char *path, size_t *out_len) {
    char *result = (char*)EM_ASM_PTR({
        try {
            const path = UTF8ToString($0);
            const data = FS.readFile(path, { encoding: 'utf8' });
            const len = lengthBytesUTF8(data) + 1;
            const ptr = _malloc(len);
            stringToUTF8(data, ptr, len);
            setValue($1, len - 1, 'i32');
            return ptr;
        } catch (e) {
            setValue($1, 0, 'i32');
            return 0;
        }
    }, path, out_len);
    return result;
}

static bool web_write_file(const char *path, const char *data, size_t len) {
    (void)len;

    // Ensure parent directory exists
    char parent[512];
    strncpy(parent, path, sizeof(parent) - 1);
    parent[sizeof(parent) - 1] = '\0';
    char *last_slash = strrchr(parent, '/');
    if (last_slash && last_slash != parent) {
        *last_slash = '\0';
        web_mkdir_p(parent);
    }

    bool success = EM_ASM_INT({
        try {
            const path = UTF8ToString($0);
            const data = UTF8ToString($1);
            FS.writeFile(path, data);
            return 1;
        } catch (e) {
            return 0;
        }
    }, path, data);

    if (success) {
        js_sync_filesystem();  // Persist to IndexedDB
    }

    return success;
}

static bool web_list_dir(const char *path, char ***out_names, int32_t *out_count) {
    *out_names = NULL;
    *out_count = 0;

    int32_t count = EM_ASM_INT({
        try {
            const path = UTF8ToString($0);
            const entries = FS.readdir(path).filter(e => e !== '.' && e !== '..');
            window.dawnDirEntries = entries;
            return entries.length;
        } catch (e) {
            window.dawnDirEntries = [];
            return 0;
        }
    }, path);

    if (count == 0) return true;

    *out_names = malloc(count * sizeof(char*));
    if (!*out_names) return false;

    for (int32_t i = 0; i < count; i++) {
        (*out_names)[i] = (char*)EM_ASM_PTR({
            const name = window.dawnDirEntries[$0];
            const len = lengthBytesUTF8(name) + 1;
            const ptr = _malloc(len);
            stringToUTF8(name, ptr, len);
            return ptr;
        }, i);
    }

    *out_count = count;
    return true;
}

static int64_t web_get_mtime(const char *path) {
    return (int64_t)EM_ASM_DOUBLE({
        try {
            const path = UTF8ToString($0);
            const stat = FS.stat(path);
            return stat.mtime.getTime() / 1000.0;
        } catch (e) {
            return 0.0;
        }
    }, path);
}

static bool web_delete_file(const char *path) {
    bool success = EM_ASM_INT({
        try {
            const path = UTF8ToString($0);
            FS.unlink(path);
            return 1;
        } catch (e) {
            return 0;
        }
    }, path);

    if (success) {
        js_sync_filesystem();
    }

    return success;
}

static void web_reveal_in_finder(const char *path) {
    // Open in new tab - won't work for local files, but could work for URLs
    EM_ASM({
        const path = UTF8ToString($0);
        // For web, we might download the file instead
        console.log('Reveal in finder:', path);
    }, path);
}

static int64_t web_clock(DawnClock kind) {
    if (kind == DAWN_CLOCK_MS) {
        return (int64_t)js_time_now_ms_double();
    }
    return (int64_t)js_time_now_double();
}

static void web_sleep_ms(int32_t ms) {
    // Can't really block in web - this is a no-op
    // Use emscripten_sleep for async version
    (void)ms;
}

static void web_get_local_time(DawnTime *out) {
    int32_t year, mon, mday, hour, min, sec, wday;
    js_get_local_time(&year, &mon, &mday, &hour, &min, &sec, &wday);
    out->year = year;
    out->mon = mon;
    out->mday = mday;
    out->hour = hour;
    out->min = min;
    out->sec = sec;
    out->wday = wday;
}

static void web_get_local_time_from(DawnTime *out, int64_t timestamp) {
    if (!out) return;
    time_t ts = (time_t)timestamp;
    struct tm *t = localtime(&ts);
    if (!t) {
        memset(out, 0, sizeof(*out));
        return;
    }
    out->year = t->tm_year + 1900;
    out->mon = t->tm_mon;
    out->mday = t->tm_mday;
    out->hour = t->tm_hour;
    out->min = t->tm_min;
    out->sec = t->tm_sec;
    out->wday = t->tm_wday;
}

static char *web_username_buf = NULL;

static const char *web_get_username(void) {
    if (web_username_buf) free(web_username_buf);
    web_username_buf = (char*)js_get_username();
    return web_username_buf;
}

// --- Image functions ---

static bool web_image_is_supported(const char *path) {
    if (!path) return false;
    const char *ext = strrchr(path, '.');
    if (!ext) return false;
    ext++;
    return (strcasecmp(ext, "png") == 0 ||
            strcasecmp(ext, "jpg") == 0 ||
            strcasecmp(ext, "jpeg") == 0 ||
            strcasecmp(ext, "gif") == 0 ||
            strcasecmp(ext, "bmp") == 0 ||
            strcasecmp(ext, "webp") == 0);
}

static bool web_image_get_size(const char *path, int32_t *out_width, int32_t *out_height) {
    return js_get_image_size(path, out_width, out_height);
}

static int32_t web_image_display(const char *path, int32_t row, int32_t col, int32_t max_cols, int32_t max_rows) {
    return js_display_image(path, row, col, max_cols, max_rows);
}

static int32_t web_image_display_cropped(const char *path, int32_t row, int32_t col, int32_t max_cols,
                                      int32_t crop_top_rows, int32_t visible_rows) {
    // Web canvas doesn't support partial image cropping
    (void)crop_top_rows;
    return js_display_image(path, row, col, max_cols, visible_rows);
}

static void web_image_frame_start(void) {
    // No-op for canvas
}

static void web_image_frame_end(void) {
    // No-op for canvas
}

static void web_image_clear_all(void) {
    EM_ASM({
        window.dawnImages = {};
    });
}

static void web_image_mask_region(int32_t col, int32_t row, int32_t cols, int32_t rows, DawnColor bg) {
    js_clear_rect(col, row, cols, rows, bg.r, bg.g, bg.b);
}

static bool web_image_resolve_path(const char *raw_path, const char *base_dir,
                                    char *out, size_t out_size) {
    if (!raw_path || !out) return false;

    // Handle URLs
    if (strncmp(raw_path, "http://", 7) == 0 || strncmp(raw_path, "https://", 8) == 0) {
        strncpy(out, raw_path, out_size - 1);
        out[out_size - 1] = '\0';
        return true;
    }

    // Handle absolute paths
    if (raw_path[0] == '/') {
        strncpy(out, raw_path, out_size - 1);
        out[out_size - 1] = '\0';
        return true;
    }

    // Relative path
    if (base_dir) {
        snprintf(out, out_size, "%s/%s", base_dir, raw_path);
    } else {
        snprintf(out, out_size, "/dawn/%s", raw_path);
    }

    return true;
}

static int32_t web_image_calc_rows(int32_t pixel_width, int32_t pixel_height, int32_t max_cols, int32_t max_rows) {
    if (pixel_width <= 0 || pixel_height <= 0) return 0;

    int32_t max_width_px = max_cols * CELL_WIDTH;
    int32_t max_height_px = max_rows > 0 ? max_rows * CELL_HEIGHT : pixel_height;

    float scale = 1.0f;
    if (pixel_width > max_width_px) {
        scale = (float)max_width_px / pixel_width;
    }

    int32_t scaled_height = (int32_t)(pixel_height * scale);
    if (max_rows > 0 && scaled_height > max_height_px) {
        scaled_height = max_height_px;
    }

    return (scaled_height + CELL_HEIGHT - 1) / CELL_HEIGHT;
}

static void web_image_invalidate(const char *path) {
    EM_ASM({
        const path = UTF8ToString($0);
        if (window.dawnImages) {
            delete window.dawnImages[path];
        }
    }, path);
}


const DawnBackend dawn_backend_web = {
    .name = "web",

    // Lifecycle
    .init = web_init,
    .shutdown = web_shutdown,
    .get_caps = web_get_capabilities,
    .get_host_bg = NULL,

    // Display
    .get_size = web_get_size,
    .set_cursor = web_set_cursor,
    .set_cursor_visible = web_set_cursor_visible,
    .set_fg = web_set_fg,
    .set_bg = web_set_bg,
    .reset_attrs = web_reset_attrs,
    .set_bold = web_set_bold,
    .set_italic = web_set_italic,
    .set_dim = web_set_dim,
    .set_strike = web_set_strikethrough,
    .set_underline = web_set_underline,
    .set_underline_color = web_set_underline_color,
    .clear_underline = web_clear_underline,
    .clear_screen = web_clear_screen,
    .clear_line = web_clear_line,
    .clear_range = web_clear_range,
    .write_str = web_write_str,
    .write_char = web_write_char,
    .repeat_char = web_repeat_char,
    .write_scaled = web_write_scaled,
    .write_scaled_frac = web_write_scaled_frac,
    .flush = web_flush,
    .sync_begin = web_sync_begin,
    .sync_end = web_sync_end,
    .set_title = web_set_title,
    .link_begin = web_link_begin,
    .link_end = web_link_end,

    // Input
    .read_key = web_read_key,
    .mouse_col = web_get_last_mouse_col,
    .mouse_row = web_get_last_mouse_row,
    .check_resize = web_check_resize,
    .check_quit = web_check_quit,
    .poll_jobs = NULL,
    .input_ready = web_input_available,
    .register_signals = web_register_signals,

    // Clipboard
    .copy = web_clipboard_copy,
    .paste = web_clipboard_paste,

    // Filesystem
    .home_dir = web_get_home_dir,
    .mkdir_p = web_mkdir_p,
    .file_exists = web_file_exists,
    .read_file = web_read_file,
    .write_file = web_write_file,
    .list_dir = web_list_dir,
    .mtime = web_get_mtime,
    .rm = web_delete_file,
    .reveal = web_reveal_in_finder,

    // Time
    .clock = web_clock,
    .sleep_ms = web_sleep_ms,
    .localtime = web_get_local_time,
    .localtime_from = web_get_local_time_from,
    .username = web_get_username,

    // Images
    .img_supported = web_image_is_supported,
    .img_size = web_image_get_size,
    .img_display = web_image_display,
    .img_display_cropped = web_image_display_cropped,
    .img_frame_start = web_image_frame_start,
    .img_frame_end = web_image_frame_end,
    .img_clear_all = web_image_clear_all,
    .img_mask = web_image_mask_region,
    .img_resolve = web_image_resolve_path,
    .img_calc_rows = web_image_calc_rows,
    .img_invalidate = web_image_invalidate,
};

#endif // __EMSCRIPTEN__
