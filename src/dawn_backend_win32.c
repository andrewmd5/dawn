// dawn_backend_win32.c - Windows Backend

#ifdef _WIN32

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#ifndef WINVER
#define WINVER 0x0600
#endif

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "dawn_backend.h"
#include "dawn_wrap.h"

#include <assert.h>
#include <direct.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

// UTF-8 to UTF-16 conversion helper
static wchar_t* utf8_to_wide(const char* utf8)
{
    if (!utf8)
        return NULL;
    int32_t len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    if (len <= 0)
        return NULL;
    wchar_t* wide = malloc(len * sizeof(wchar_t));
    if (!wide)
        return NULL;
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, len);
    return wide;
}

// UTF-16 to UTF-8 conversion helper
static char* wide_to_utf8(const wchar_t* wide)
{
    if (!wide)
        return NULL;
    int32_t len = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
    if (len <= 0)
        return NULL;
    char* utf8 = malloc(len);
    if (!utf8)
        return NULL;
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, len, NULL, NULL);
    return utf8;
}

// stb_image for image support
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_GIF
#define STBI_ONLY_BMP
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// SVG support via dawn_svg
#include "dawn_svg.h"

#define ESC "\x1b"
#define CSI ESC "["

#define CLEAR_SCREEN CSI "2J"
#define CLEAR_LINE CSI "2K"
#define CURSOR_HOME CSI "H"
#define CURSOR_HIDE CSI "?25l"
#define CURSOR_SHOW CSI "?25h"

#define ALT_SCREEN_ON CSI "?1049h"
#define ALT_SCREEN_OFF CSI "?1049l"

#define MOUSE_ON CSI "?1000h" CSI "?1006h"
#define MOUSE_OFF CSI "?1000l" CSI "?1006l"

#define BRACKETED_PASTE_ON CSI "?2004h"
#define BRACKETED_PASTE_OFF CSI "?2004l"

#define SYNC_START CSI "?2026h"
#define SYNC_END CSI "?2026l"

#define KITTY_KBD_PUSH CSI ">1u"
#define KITTY_KBD_POP CSI "<u"

#define UNDERLINE_CURLY CSI "4:3m"
#define UNDERLINE_DOTTED CSI "4:4m"
#define UNDERLINE_DASHED CSI "4:5m"
#define UNDERLINE_OFF CSI "4:0m"

#define TEXT_SIZE_OSC ESC "]66;"
#define TEXT_SIZE_ST ESC "\\"

#define RESET CSI "0m"
#define BOLD CSI "1m"
#define DIM CSI "2m"
#define ITALIC CSI "3m"
#define UNDERLINE CSI "4m"
#define STRIKETHROUGH CSI "9m"

// Output buffer for batching terminal writes
#define OUTPUT_BUF_SIZE (256 * 1024)
static char* output_buf = NULL;
static size_t output_buf_pos = 0;

static struct {
    HANDLE h_stdin;
    HANDLE h_stdout;
    DWORD orig_stdin_mode;
    DWORD orig_stdout_mode;
    UINT orig_input_cp;
    UINT orig_output_cp;
    bool raw_mode;
    bool initialized;
    uint32_t capabilities;
    int32_t cols;
    int32_t rows;
    int32_t last_mouse_col;
    int32_t last_mouse_row;
    volatile LONG resize_needed;
    volatile LONG quit_requested;
    bool kitty_keyboard_enabled;
    DawnMode mode;
    HANDLE h_conin;  //!< Console input handle for queries in print mode
    HANDLE h_conout; //!< Console output handle for queries in print mode
    int32_t print_row;
    int32_t print_col;
    DawnColor* print_bg;
    char home_dir[MAX_PATH];
} win32_state = { 0 };

// Fast output buffer append
static inline void buf_flush(void)
{
    if (output_buf_pos > 0) {
        DWORD written;
        WriteConsoleA(win32_state.h_stdout, output_buf, (DWORD)output_buf_pos, &written, NULL);
        output_buf_pos = 0;
    }
}

static inline void buf_append(const char* s, size_t len)
{
    if (output_buf_pos + len > OUTPUT_BUF_SIZE) {
        buf_flush();
        if (len > OUTPUT_BUF_SIZE) {
            DWORD written;
            WriteConsoleA(win32_state.h_stdout, s, (DWORD)len, &written, NULL);
            return;
        }
    }
    memcpy(output_buf + output_buf_pos, s, len);
    output_buf_pos += len;
}

static inline void buf_append_str(const char* s)
{
    buf_append(s, strlen(s));
}

static inline void buf_append_char(char c)
{
    if (output_buf_pos >= OUTPUT_BUF_SIZE) {
        buf_flush();
    }
    output_buf[output_buf_pos++] = c;
}

// Format number into buffer, returns length
static inline int32_t format_num(char* buf, int32_t n)
{
    if (n < 10) {
        buf[0] = '0' + n;
        return 1;
    } else if (n < 100) {
        buf[0] = '0' + n / 10;
        buf[1] = '0' + n % 10;
        return 2;
    } else {
        int32_t len = 0;
        char tmp[12];
        while (n > 0) {
            tmp[len++] = '0' + n % 10;
            n /= 10;
        }
        for (int32_t i = 0; i < len; i++) {
            buf[i] = tmp[len - 1 - i];
        }
        return len;
    }
}

// Buffered printf-style output
static void buf_printf(const char* fmt, ...)
{
    char tmp[512];
    va_list args;
    va_start(args, fmt);
    int32_t len = vsnprintf(tmp, sizeof(tmp), fmt, args);
    va_end(args);
    if (len > 0 && len < (int32_t)sizeof(tmp)) {
        buf_append(tmp, (size_t)len);
    }
}

// Forward declaration for use in buf_cursor
static inline void buf_bg(uint8_t r, uint8_t g, uint8_t b);

// Fast path: cursor positioning
static inline void buf_cursor(int32_t row, int32_t col)
{
    if (win32_state.mode == DAWN_MODE_PRINT) {
        while (win32_state.print_row < row) {
            buf_append_char('\n');
            win32_state.print_row++;
            win32_state.print_col = 1;
        }
        if (col > win32_state.print_col) {
            if (win32_state.print_bg) {
                buf_bg(win32_state.print_bg->r, win32_state.print_bg->g, win32_state.print_bg->b);
            }
            while (win32_state.print_col < col) {
                buf_append_char(' ');
                win32_state.print_col++;
            }
        } else if (col < win32_state.print_col) {
            buf_append_char('\r');
            win32_state.print_col = 1;
            if (win32_state.print_bg) {
                buf_bg(win32_state.print_bg->r, win32_state.print_bg->g, win32_state.print_bg->b);
            }
            while (win32_state.print_col < col) {
                buf_append_char(' ');
                win32_state.print_col++;
            }
        }
        return;
    }

    char seq[16];
    seq[0] = '\x1b';
    seq[1] = '[';
    int32_t pos = 2;
    pos += format_num(seq + pos, row);
    seq[pos++] = ';';
    pos += format_num(seq + pos, col);
    seq[pos++] = 'H';
    buf_append(seq, (size_t)pos);
}

// Fast path: foreground color
static inline void buf_fg(uint8_t r, uint8_t g, uint8_t b)
{
    char seq[24];
    seq[0] = '\x1b';
    seq[1] = '[';
    seq[2] = '3';
    seq[3] = '8';
    seq[4] = ';';
    seq[5] = '2';
    seq[6] = ';';
    int32_t pos = 7;
    pos += format_num(seq + pos, r);
    seq[pos++] = ';';
    pos += format_num(seq + pos, g);
    seq[pos++] = ';';
    pos += format_num(seq + pos, b);
    seq[pos++] = 'm';
    buf_append(seq, (size_t)pos);
}

// Fast path: background color
static inline void buf_bg(uint8_t r, uint8_t g, uint8_t b)
{
    char seq[24];
    seq[0] = '\x1b';
    seq[1] = '[';
    seq[2] = '4';
    seq[3] = '8';
    seq[4] = ';';
    seq[5] = '2';
    seq[6] = ';';
    int32_t pos = 7;
    pos += format_num(seq + pos, r);
    seq[pos++] = ';';
    pos += format_num(seq + pos, g);
    seq[pos++] = ';';
    pos += format_num(seq + pos, b);
    seq[pos++] = 'm';
    buf_append(seq, (size_t)pos);
}

// Fast path: underline color
static inline void buf_underline_color(uint8_t r, uint8_t g, uint8_t b)
{
    char seq[24];
    seq[0] = '\x1b';
    seq[1] = '[';
    seq[2] = '5';
    seq[3] = '8';
    seq[4] = ':';
    seq[5] = '2';
    seq[6] = ':';
    seq[7] = ':';
    int32_t pos = 8;
    pos += format_num(seq + pos, r);
    seq[pos++] = ':';
    pos += format_num(seq + pos, g);
    seq[pos++] = ':';
    pos += format_num(seq + pos, b);
    seq[pos++] = 'm';
    buf_append(seq, (size_t)pos);
}

// Image cache for Kitty graphics protocol
typedef struct {
    char* path;
    uint32_t image_id;
    int64_t mtime;
} TransmittedImage;

#define MAX_TRANSMITTED_IMAGES 8
static TransmittedImage transmitted_images[MAX_TRANSMITTED_IMAGES];
static int32_t transmitted_count = 0;
static uint32_t next_image_id = 1;

// Forward declarations
static int32_t win32_image_calc_rows(int32_t pixel_width, int32_t pixel_height, int32_t max_cols, int32_t max_rows);
static int64_t win32_get_mtime(const char* path);
static bool win32_file_exists(const char* path);
static char* win32_read_file(const char* path, size_t* out_len);
static bool win32_mkdir_p(const char* path);
static const char* win32_get_home_dir(void);

//! Get handles for terminal queries
static inline HANDLE get_query_write_handle(void)
{
    if (win32_state.mode == DAWN_MODE_PRINT && win32_state.h_conout != INVALID_HANDLE_VALUE) {
        return win32_state.h_conout;
    }
    return win32_state.h_stdout;
}

static inline HANDLE get_query_read_handle(void)
{
    if (win32_state.mode == DAWN_MODE_PRINT && win32_state.h_conin != INVALID_HANDLE_VALUE) {
        return win32_state.h_conin;
    }
    return win32_state.h_stdin;
}

static void query_write(const char* data, size_t len)
{
    HANDLE h = get_query_write_handle();
    DWORD written;
    WriteConsoleA(h, data, (DWORD)len, &written, NULL);
}

static void query_printf(const char* fmt, ...)
{
    char tmp[512];
    va_list args;
    va_start(args, fmt);
    int32_t len = vsnprintf(tmp, sizeof(tmp), fmt, args);
    va_end(args);
    if (len > 0 && len < (int32_t)sizeof(tmp)) {
        query_write(tmp, (size_t)len);
    }
}

static void drain_input(void)
{
    HANDLE h = get_query_read_handle();
    INPUT_RECORD rec;
    DWORD avail, read;

    while (GetNumberOfConsoleInputEvents(h, &avail) && avail > 0) {
        if (!ReadConsoleInputA(h, &rec, 1, &read) || read == 0)
            break;
    }
}

static size_t read_response(char* buf, size_t buf_size, char terminator, int32_t timeout_ms)
{
    HANDLE h = get_query_read_handle();
    size_t pos = 0;
    DWORD start = GetTickCount();

    while (pos < buf_size - 1) {
        DWORD elapsed = GetTickCount() - start;
        if ((int32_t)elapsed >= timeout_ms)
            break;

        DWORD avail;
        if (!GetNumberOfConsoleInputEvents(h, &avail) || avail == 0) {
            Sleep(1);
            continue;
        }

        INPUT_RECORD rec;
        DWORD read;
        if (!ReadConsoleInputA(h, &rec, 1, &read) || read == 0)
            break;

        if (rec.EventType != KEY_EVENT || !rec.Event.KeyEvent.bKeyDown)
            continue;

        char c = rec.Event.KeyEvent.uChar.AsciiChar;
        if (c == 0)
            continue;

        buf[pos++] = c;

        if (c == terminator)
            break;
        if (pos >= 2 && buf[pos - 2] == '\x1b' && c == '\\')
            break;
    }
    buf[pos] = '\0';
    return pos;
}

static bool query_mode_supported(int32_t mode)
{
    query_printf(CSI "?%d$p", mode);

    char buf[32];
    size_t len = read_response(buf, sizeof(buf), 'y', 100);

    if (len > 0 && strstr(buf, "$y")) {
        char* semi = strchr(buf, ';');
        if (semi && semi[1] != '0')
            return true;
    }
    return false;
}

static bool query_kitty_keyboard(void)
{
    query_write(CSI "?u", sizeof(CSI "?u") - 1);

    char buf[32];
    size_t len = read_response(buf, sizeof(buf), 'u', 100);
    return len > 0 && strchr(buf, '?') != NULL;
}

static bool query_kitty_graphics(void)
{
    query_write(ESC "_Gi=31,s=1,v=1,a=q,t=d,f=24;AAAA" ESC "\\",
        sizeof(ESC "_Gi=31,s=1,v=1,a=q,t=d,f=24;AAAA" ESC "\\") - 1);

    char buf[64];
    size_t len = read_response(buf, sizeof(buf), '\\', 100);
    return len > 0 && strstr(buf, "OK") != NULL;
}

static bool parse_cpr(const char* buf, size_t len, int32_t* row, int32_t* col)
{
    if (len < 6)
        return false;

    const char* p = buf;
    const char* end = buf + len;

    while (p < end - 1 && !(p[0] == '\x1b' && p[1] == '['))
        p++;
    if (p >= end - 1)
        return false;
    p += 2;

    *row = 0;
    while (p < end && *p >= '0' && *p <= '9') {
        *row = *row * 10 + (*p - '0');
        p++;
    }

    if (p >= end || *p != ';')
        return false;
    p++;

    *col = 0;
    while (p < end && *p >= '0' && *p <= '9') {
        *col = *col * 10 + (*p - '0');
        p++;
    }

    if (p >= end || *p != 'R')
        return false;
    return (*row > 0 && *col > 0);
}

static bool query_background_color(DawnColor* out)
{
    if (!out)
        return false;

    drain_input();

    query_write("\x1b]11;?\x1b\\", 8);

    char buf[64];
    size_t len = read_response(buf, sizeof(buf), '\\', 100);

    if (len < 10)
        return false;

    char* rgb = strstr(buf, "rgb:");
    if (!rgb)
        return false;
    rgb += 4;

    uint32_t r, g, b;
    if (sscanf(rgb, "%x/%x/%x", &r, &g, &b) != 3)
        return false;

    out->r = (uint8_t)(r >> 8);
    out->g = (uint8_t)(g >> 8);
    out->b = (uint8_t)(b >> 8);

    return true;
}

static bool query_text_sizing(void)
{
    query_write(CSI "1;1H", sizeof(CSI "1;1H") - 1);
    drain_input();

    query_write(CSI "6n", sizeof(CSI "6n") - 1);

    char buf1[32];
    size_t len1 = read_response(buf1, sizeof(buf1), 'R', 100);

    int32_t row1, col1;
    if (!parse_cpr(buf1, len1, &row1, &col1))
        return false;

    query_write(ESC "]66;w=2; " ESC "\\", sizeof(ESC "]66;w=2; " ESC "\\") - 1);

    query_write(CSI "6n", sizeof(CSI "6n") - 1);

    char buf2[32];
    size_t len2 = read_response(buf2, sizeof(buf2), 'R', 100);

    int32_t row2, col2;
    if (!parse_cpr(buf2, len2, &row2, &col2))
        return false;

    return (row1 == row2 && col2 - col1 == 2);
}

static void detect_capabilities(void)
{
    win32_state.capabilities = DAWN_CAP_NONE;

    // Check for truecolor support via environment
    const char* colorterm = getenv("COLORTERM");
    if (colorterm && (strcmp(colorterm, "truecolor") == 0 || strcmp(colorterm, "24bit") == 0)) {
        win32_state.capabilities |= DAWN_CAP_TRUE_COLOR;
    }

    // Windows Terminal and modern terminals support truecolor
    const char* wt_session = getenv("WT_SESSION");
    if (wt_session) {
        win32_state.capabilities |= DAWN_CAP_TRUE_COLOR;
    }

    // Query for sync output
    if (query_mode_supported(2026)) {
        win32_state.capabilities |= DAWN_CAP_SYNC_OUTPUT;
    }

    // Query for bracketed paste
    if (query_mode_supported(2004)) {
        win32_state.capabilities |= DAWN_CAP_BRACKETED_PASTE;
    }

    // Query for Kitty keyboard protocol (implies styled underlines)
    if (query_kitty_keyboard()) {
        win32_state.capabilities |= DAWN_CAP_STYLED_UNDERLINE;
    }

    // Query for Kitty graphics protocol
    if (query_kitty_graphics()) {
        win32_state.capabilities |= DAWN_CAP_IMAGES;
    }

    // Query for text sizing
    if (query_text_sizing()) {
        win32_state.capabilities |= DAWN_CAP_TEXT_SIZING;
    }

    // Mouse and clipboard always available on Windows
    win32_state.capabilities |= DAWN_CAP_MOUSE;
    win32_state.capabilities |= DAWN_CAP_CLIPBOARD;

    drain_input();
}

static BOOL WINAPI console_ctrl_handler(DWORD ctrl_type)
{
    switch (ctrl_type) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
        InterlockedExchange(&win32_state.quit_requested, 1);
        return TRUE;
    default:
        return FALSE;
    }
}

static bool win32_init(DawnMode mode)
{
    if (win32_state.initialized)
        return true;

    win32_state.mode = mode;
    win32_state.h_conin = INVALID_HANDLE_VALUE;
    win32_state.h_conout = INVALID_HANDLE_VALUE;

    // Allocate output buffer
    if (!output_buf) {
        output_buf = malloc(OUTPUT_BUF_SIZE);
        if (!output_buf)
            return false;
    }

    // Get standard handles
    win32_state.h_stdin = GetStdHandle(STD_INPUT_HANDLE);
    win32_state.h_stdout = GetStdHandle(STD_OUTPUT_HANDLE);

    if (win32_state.h_stdin == INVALID_HANDLE_VALUE ||
        win32_state.h_stdout == INVALID_HANDLE_VALUE) {
        return false;
    }

    // Save original console modes
    GetConsoleMode(win32_state.h_stdin, &win32_state.orig_stdin_mode);
    GetConsoleMode(win32_state.h_stdout, &win32_state.orig_stdout_mode);

    // Set console to UTF-8
    win32_state.orig_input_cp = GetConsoleCP();
    win32_state.orig_output_cp = GetConsoleOutputCP();
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);

    // Set binary mode to prevent CR/LF translation
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);

    if (mode == DAWN_MODE_PRINT) {
        // Print mode: open CONIN$ and CONOUT$ for terminal queries
        win32_state.h_conin = CreateFileA("CONIN$", GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        win32_state.h_conout = CreateFileA("CONOUT$", GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);

        // Enable VT processing on stdout for escape sequences
        DWORD out_mode = win32_state.orig_stdout_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING | ENABLE_PROCESSED_OUTPUT;
        SetConsoleMode(win32_state.h_stdout, out_mode);

        // Set up input mode for capability queries
        if (win32_state.h_conin != INVALID_HANDLE_VALUE) {
            DWORD in_mode = ENABLE_VIRTUAL_TERMINAL_INPUT;
            SetConsoleMode(win32_state.h_conin, in_mode);
        }

        // Query terminal background color
        DawnColor term_bg;
        if (query_background_color(&term_bg)) {
            win32_state.print_bg = malloc(sizeof(DawnColor));
            if (win32_state.print_bg) {
                *win32_state.print_bg = term_bg;
            }
        }

        // Detect capabilities
        detect_capabilities();

        // Get terminal size
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        HANDLE size_handle = (win32_state.h_conout != INVALID_HANDLE_VALUE)
            ? win32_state.h_conout
            : win32_state.h_stdout;
        if (GetConsoleScreenBufferInfo(size_handle, &csbi)) {
            win32_state.cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
            win32_state.rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
        } else {
            win32_state.cols = 80;
            win32_state.rows = 24;
        }

        // Initialize print position
        win32_state.print_row = 1;
        win32_state.print_col = 1;

        win32_state.initialized = true;
        return true;
    }

    // Interactive mode: full terminal setup
    SetConsoleCtrlHandler(console_ctrl_handler, TRUE);

    // Enable VT processing and raw input, disable QuickEdit (which intercepts right-click)
    DWORD in_mode = ENABLE_VIRTUAL_TERMINAL_INPUT | ENABLE_MOUSE_INPUT | ENABLE_WINDOW_INPUT | ENABLE_EXTENDED_FLAGS;
    if (!SetConsoleMode(win32_state.h_stdin, in_mode)) {
        // Fallback for older terminals
        in_mode = ENABLE_MOUSE_INPUT | ENABLE_WINDOW_INPUT | ENABLE_EXTENDED_FLAGS;
        SetConsoleMode(win32_state.h_stdin, in_mode);
    }

    DWORD out_mode = ENABLE_VIRTUAL_TERMINAL_PROCESSING | ENABLE_PROCESSED_OUTPUT | DISABLE_NEWLINE_AUTO_RETURN;
    if (!SetConsoleMode(win32_state.h_stdout, out_mode)) {
        // Fallback without DISABLE_NEWLINE_AUTO_RETURN
        out_mode = ENABLE_VIRTUAL_TERMINAL_PROCESSING | ENABLE_PROCESSED_OUTPUT;
        SetConsoleMode(win32_state.h_stdout, out_mode);
    }

    win32_state.raw_mode = true;

    // Switch to alternate screen
    DWORD written;
    WriteConsoleA(win32_state.h_stdout, ALT_SCREEN_ON, sizeof(ALT_SCREEN_ON) - 1, &written, NULL);

    // Detect capabilities
    detect_capabilities();

    // Enable Kitty keyboard protocol if available
    if (win32_state.capabilities & DAWN_CAP_STYLED_UNDERLINE) {
        WriteConsoleA(win32_state.h_stdout, KITTY_KBD_PUSH, sizeof(KITTY_KBD_PUSH) - 1, &written, NULL);
        win32_state.kitty_keyboard_enabled = true;
    }

    // Enable mouse and bracketed paste, hide cursor, clear screen
    WriteConsoleA(win32_state.h_stdout,
        CURSOR_HIDE MOUSE_ON BRACKETED_PASTE_ON CLEAR_SCREEN CURSOR_HOME,
        sizeof(CURSOR_HIDE MOUSE_ON BRACKETED_PASTE_ON CLEAR_SCREEN CURSOR_HOME) - 1,
        &written, NULL);

    // Get terminal size
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(win32_state.h_stdout, &csbi)) {
        win32_state.cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        win32_state.rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    } else {
        win32_state.cols = 80;
        win32_state.rows = 24;
    }

    win32_state.initialized = true;
    return true;
}

static void win32_shutdown(void)
{
    if (!win32_state.initialized)
        return;

    DWORD written;

    if (win32_state.mode == DAWN_MODE_PRINT) {
        // Print mode cleanup
        if (win32_state.h_conin != INVALID_HANDLE_VALUE) {
            CloseHandle(win32_state.h_conin);
        }
        if (win32_state.h_conout != INVALID_HANDLE_VALUE) {
            CloseHandle(win32_state.h_conout);
        }
        win32_state.h_conin = INVALID_HANDLE_VALUE;
        win32_state.h_conout = INVALID_HANDLE_VALUE;

        if (win32_state.print_bg) {
            free(win32_state.print_bg);
            win32_state.print_bg = NULL;
        }

        // Restore console mode and code page
        SetConsoleMode(win32_state.h_stdout, win32_state.orig_stdout_mode);
        SetConsoleCP(win32_state.orig_input_cp);
        SetConsoleOutputCP(win32_state.orig_output_cp);

        free(output_buf);
        output_buf = NULL;
        output_buf_pos = 0;

        win32_state.initialized = false;
        return;
    }

    // Interactive mode cleanup
    WriteConsoleA(win32_state.h_stdout, ESC "_Ga=d,d=A,q=2" ESC "\\",
        sizeof(ESC "_Ga=d,d=A,q=2" ESC "\\") - 1, &written, NULL);

    if (win32_state.kitty_keyboard_enabled) {
        WriteConsoleA(win32_state.h_stdout, KITTY_KBD_POP, sizeof(KITTY_KBD_POP) - 1, &written, NULL);
    }

    WriteConsoleA(win32_state.h_stdout,
        SYNC_START CURSOR_SHOW MOUSE_OFF BRACKETED_PASTE_OFF ALT_SCREEN_OFF RESET SYNC_END,
        sizeof(SYNC_START CURSOR_SHOW MOUSE_OFF BRACKETED_PASTE_OFF ALT_SCREEN_OFF RESET SYNC_END) - 1,
        &written, NULL);

    // Restore console modes and code page
    SetConsoleMode(win32_state.h_stdin, win32_state.orig_stdin_mode);
    SetConsoleMode(win32_state.h_stdout, win32_state.orig_stdout_mode);
    SetConsoleCP(win32_state.orig_input_cp);
    SetConsoleOutputCP(win32_state.orig_output_cp);
    win32_state.raw_mode = false;

    SetConsoleCtrlHandler(console_ctrl_handler, FALSE);

    for (int32_t i = 0; i < transmitted_count; i++) {
        free(transmitted_images[i].path);
    }
    transmitted_count = 0;

    free(output_buf);
    output_buf = NULL;
    output_buf_pos = 0;

    win32_state.initialized = false;
}

static uint32_t win32_get_capabilities(void)
{
    return win32_state.capabilities;
}

static DawnColor* win32_get_host_bg(void)
{
    if (!win32_state.print_bg)
        return NULL;
    DawnColor* copy = malloc(sizeof(DawnColor));
    if (copy)
        *copy = *win32_state.print_bg;
    return copy;
}

static void win32_get_size(int32_t* out_cols, int32_t* out_rows)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(win32_state.h_stdout, &csbi)) {
        win32_state.cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        win32_state.rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    }
    if (out_cols)
        *out_cols = win32_state.cols;
    if (out_rows)
        *out_rows = win32_state.rows;
}

static void win32_set_cursor(int32_t col, int32_t row)
{
    buf_cursor(row, col);
}

static void win32_set_cursor_visible(bool visible)
{
    buf_append_str(visible ? CURSOR_SHOW : CURSOR_HIDE);
}

static void win32_set_fg(DawnColor color)
{
    buf_fg(color.r, color.g, color.b);
}

static void win32_set_bg(DawnColor color)
{
    if (win32_state.mode == DAWN_MODE_PRINT && win32_state.print_bg) {
        if (color.r == win32_state.print_bg->r &&
            color.g == win32_state.print_bg->g &&
            color.b == win32_state.print_bg->b) {
            return;
        }
    }
    buf_bg(color.r, color.g, color.b);
}

static void win32_reset_attrs(void)
{
    buf_append_str(RESET);
}

static void win32_set_bold(bool enabled)
{
    buf_append_str(enabled ? BOLD : CSI "22m");
}

static void win32_set_italic(bool enabled)
{
    buf_append_str(enabled ? ITALIC : CSI "23m");
}

static void win32_set_dim(bool enabled)
{
    buf_append_str(enabled ? DIM : CSI "22m");
}

static void win32_set_strikethrough(bool enabled)
{
    buf_append_str(enabled ? STRIKETHROUGH : CSI "29m");
}

static void win32_set_underline(DawnUnderline style)
{
    if (win32_state.capabilities & DAWN_CAP_STYLED_UNDERLINE) {
        switch (style) {
        case DAWN_UNDERLINE_SINGLE:
            buf_append_str(UNDERLINE);
            break;
        case DAWN_UNDERLINE_CURLY:
            buf_append_str(UNDERLINE_CURLY);
            break;
        case DAWN_UNDERLINE_DOTTED:
            buf_append_str(UNDERLINE_DOTTED);
            break;
        case DAWN_UNDERLINE_DASHED:
            buf_append_str(UNDERLINE_DASHED);
            break;
        }
    } else {
        buf_append_str(UNDERLINE);
    }
}

static void win32_set_underline_color(DawnColor color)
{
    if (win32_state.capabilities & DAWN_CAP_STYLED_UNDERLINE) {
        buf_underline_color(color.r, color.g, color.b);
    }
}

static void win32_clear_underline(void)
{
    if (win32_state.capabilities & DAWN_CAP_STYLED_UNDERLINE) {
        buf_append_str(UNDERLINE_OFF);
    } else {
        buf_append_str(CSI "24m");
    }
}

static void win32_clear_screen(void)
{
    if (win32_state.mode == DAWN_MODE_PRINT)
        return;
    buf_append_str(CLEAR_SCREEN CURSOR_HOME);
}

static void win32_clear_line(void)
{
    if (win32_state.mode == DAWN_MODE_PRINT)
        return;
    buf_append_str(CLEAR_LINE);
}

static void win32_clear_range(int32_t count)
{
    if (win32_state.mode == DAWN_MODE_PRINT || count <= 0)
        return;
    char buf[16];
    snprintf(buf, sizeof(buf), CSI "%dX", count);
    buf_append_str(buf);
}

static void win32_write_str(const char* str, size_t len)
{
    buf_append(str, len);
    if (win32_state.mode == DAWN_MODE_PRINT) {
        const char* nl = memchr(str, '\n', len);
        if (nl) {
            size_t pos = 0;
            while (pos < len) {
                const char* next_nl = memchr(str + pos, '\n', len - pos);
                if (next_nl) {
                    size_t seg_len = (size_t)(next_nl - (str + pos));
                    if (seg_len > 0) {
                        win32_state.print_col += utf8_display_width(str + pos, seg_len);
                    }
                    win32_state.print_row++;
                    win32_state.print_col = 1;
                    pos = (size_t)(next_nl - str) + 1;
                } else {
                    win32_state.print_col += utf8_display_width(str + pos, len - pos);
                    break;
                }
            }
        } else {
            win32_state.print_col += utf8_display_width(str, len);
        }
    }
}

static void win32_write_char(char c)
{
    buf_append_char(c);
    if (win32_state.mode == DAWN_MODE_PRINT) {
        if (c == '\n') {
            win32_state.print_row++;
            win32_state.print_col = 1;
        } else {
            win32_state.print_col++;
        }
    }
}

static void win32_repeat_char(char c, int32_t n)
{
    if (n <= 0)
        return;
    buf_append_char(c);
    if (n > 1) {
        char seq[16];
        seq[0] = '\x1b';
        seq[1] = '[';
        int32_t pos = 2;
        pos += format_num(seq + pos, n - 1);
        seq[pos++] = 'b';
        buf_append(seq, (size_t)pos);
    }
    if (win32_state.mode == DAWN_MODE_PRINT) {
        win32_state.print_col += n;
    }
}

static void win32_write_scaled(const char* str, size_t len, int32_t scale)
{
    if (scale <= 1 || !(win32_state.capabilities & DAWN_CAP_TEXT_SIZING)) {
        buf_append(str, len);
        if (win32_state.mode == DAWN_MODE_PRINT) {
            win32_state.print_col += utf8_display_width(str, len);
        }
        return;
    }
    if (scale > 7)
        scale = 7;
    buf_printf(TEXT_SIZE_OSC "s=%d;%.*s" TEXT_SIZE_ST, scale, (int32_t)len, str);
    if (win32_state.mode == DAWN_MODE_PRINT) {
        win32_state.print_col += utf8_display_width(str, len) * scale;
    }
}

static void win32_write_scaled_frac(const char* str, size_t len, int32_t scale, int32_t num, int32_t denom)
{
    if (!(win32_state.capabilities & DAWN_CAP_TEXT_SIZING)) {
        buf_append(str, len);
        if (win32_state.mode == DAWN_MODE_PRINT) {
            win32_state.print_col += utf8_display_width(str, len);
        }
        return;
    }
    if (scale < 1)
        scale = 1;
    if (scale > 7)
        scale = 7;
    if (num < 0)
        num = 0;
    if (num > 15)
        num = 15;
    if (denom < 0)
        denom = 0;
    if (denom > 15)
        denom = 15;

    if (num == 0 || denom == 0 || num >= denom) {
        if (scale <= 1) {
            buf_append(str, len);
            if (win32_state.mode == DAWN_MODE_PRINT) {
                win32_state.print_col += utf8_display_width(str, len);
            }
        } else {
            buf_printf(TEXT_SIZE_OSC "s=%d;%.*s" TEXT_SIZE_ST, scale, (int32_t)len, str);
            if (win32_state.mode == DAWN_MODE_PRINT) {
                win32_state.print_col += utf8_display_width(str, len) * scale;
            }
        }
        return;
    }

    buf_printf(TEXT_SIZE_OSC "s=%d:n=%d:d=%d;%.*s" TEXT_SIZE_ST,
        scale, num, denom, (int32_t)len, str);
    if (win32_state.mode == DAWN_MODE_PRINT) {
        win32_state.print_col += utf8_display_width(str, len) * scale;
    }
}

static void win32_flush(void)
{
    buf_flush();
}

static void win32_sync_begin(void)
{
    if (win32_state.capabilities & DAWN_CAP_SYNC_OUTPUT) {
        buf_append_str(SYNC_START);
    }
}

static void win32_sync_end(void)
{
    if (win32_state.capabilities & DAWN_CAP_SYNC_OUTPUT) {
        buf_append_str(SYNC_END);
    }
}

static void win32_set_title(const char* title)
{
    if (title && *title) {
        buf_append_str("\x1b]0;");
        buf_append_str(title);
        buf_append_char('\x07');
    } else {
        buf_append_str("\x1b]0;\x07");
    }
}

static void win32_link_begin(const char* url)
{
    if (url && *url) {
        buf_append_str("\x1b]8;;");
        buf_append_str(url);
        buf_append_str("\x1b\\");
    }
}

static void win32_link_end(void)
{
    buf_append_str("\x1b]8;;\x1b\\");
}

// Input parsing state for VT sequences
static char vt_buf[64];
static int32_t vt_buf_len = 0;

static int32_t parse_vt_sequence(void)
{
    if (vt_buf_len < 2 || vt_buf[0] != '\x1b')
        return DAWN_KEY_NONE;

    if (vt_buf[1] == '[') {
        // SGR mouse events
        if (vt_buf_len >= 3 && vt_buf[2] == '<') {
            char* end = memchr(vt_buf + 3, 'M', vt_buf_len - 3);
            if (!end)
                end = memchr(vt_buf + 3, 'm', vt_buf_len - 3);
            if (end) {
                int32_t btn = 0, mx = 0, my = 0;
                if (sscanf(vt_buf + 3, "%d;%d;%d", &btn, &mx, &my) == 3) {
                    win32_state.last_mouse_col = mx;
                    win32_state.last_mouse_row = my;
                    if (btn == 64)
                        return DAWN_KEY_MOUSE_SCROLL_UP;
                    if (btn == 65)
                        return DAWN_KEY_MOUSE_SCROLL_DOWN;
                    if (btn == 0)
                        return DAWN_KEY_MOUSE_CLICK;
                }
            }
            return DAWN_KEY_NONE;
        }

        // Kitty keyboard protocol
        if (vt_buf_len >= 3 && vt_buf[2] >= '0' && vt_buf[2] <= '9') {
            char* u_pos = memchr(vt_buf + 2, 'u', vt_buf_len - 2);
            if (u_pos) {
                int32_t keycode = 0, mods = 1;
                sscanf(vt_buf + 2, "%d;%d", &keycode, &mods);

                bool shift = (mods - 1) & 1;
                bool alt = (mods - 1) & 2;
                bool ctrl = (mods - 1) & 4;

                switch (keycode) {
                case 57352:
                    return shift ? DAWN_KEY_SHIFT_UP : DAWN_KEY_UP;
                case 57353:
                    return shift ? DAWN_KEY_SHIFT_DOWN : DAWN_KEY_DOWN;
                case 57351:
                    if (alt && shift)
                        return DAWN_KEY_ALT_SHIFT_RIGHT;
                    if (alt)
                        return DAWN_KEY_ALT_RIGHT;
                    if (ctrl && shift)
                        return DAWN_KEY_CTRL_SHIFT_RIGHT;
                    if (ctrl)
                        return DAWN_KEY_CTRL_RIGHT;
                    if (shift)
                        return DAWN_KEY_SHIFT_RIGHT;
                    return DAWN_KEY_RIGHT;
                case 57350:
                    if (alt && shift)
                        return DAWN_KEY_ALT_SHIFT_LEFT;
                    if (alt)
                        return DAWN_KEY_ALT_LEFT;
                    if (ctrl && shift)
                        return DAWN_KEY_CTRL_SHIFT_LEFT;
                    if (ctrl)
                        return DAWN_KEY_CTRL_LEFT;
                    if (shift)
                        return DAWN_KEY_SHIFT_LEFT;
                    return DAWN_KEY_LEFT;
                case 57360:
                    return ctrl ? DAWN_KEY_CTRL_HOME : DAWN_KEY_HOME;
                case 57367:
                    return ctrl ? DAWN_KEY_CTRL_END : DAWN_KEY_END;
                case 57362:
                    return DAWN_KEY_DEL;
                case 57365:
                    return DAWN_KEY_PGUP;
                case 57366:
                    return DAWN_KEY_PGDN;
                case 9:
                    return shift ? DAWN_KEY_BTAB : '\t';
                case 13:
                    return '\r';
                case 27:
                    return '\x1b';
                case 127:
                    return 127;
                }

                if (keycode >= 32 && keycode < 127) {
                    if (ctrl && keycode == '/')
                        return 31;
                    if (ctrl && keycode >= 'a' && keycode <= 'z')
                        return keycode - 'a' + 1;
                    if (ctrl && keycode >= 'A' && keycode <= 'Z')
                        return keycode - 'A' + 1;
                    return keycode;
                }
                return DAWN_KEY_NONE;
            }

            // Legacy CSI sequences
            char* tilde = memchr(vt_buf + 2, '~', vt_buf_len - 2);
            if (tilde) {
                int32_t num = 0;
                sscanf(vt_buf + 2, "%d", &num);
                switch (num) {
                case 1:
                    return DAWN_KEY_HOME;
                case 3:
                    return DAWN_KEY_DEL;
                case 4:
                    return DAWN_KEY_END;
                case 5:
                    return DAWN_KEY_PGUP;
                case 6:
                    return DAWN_KEY_PGDN;
                }
                return DAWN_KEY_NONE;
            }

            // Modified arrow keys
            int32_t num1 = 0, num2 = 0;
            char termchar = 0;
            if (sscanf(vt_buf + 2, "%d;%d%c", &num1, &num2, &termchar) == 3) {
                bool shift = (num2 == 2 || num2 == 4 || num2 == 6 || num2 == 8);
                bool ctrl = (num2 == 5 || num2 == 6 || num2 == 7 || num2 == 8);
                bool alt = (num2 == 3 || num2 == 4 || num2 == 7 || num2 == 8);

                switch (termchar) {
                case 'A':
                    return shift ? DAWN_KEY_SHIFT_UP : DAWN_KEY_UP;
                case 'B':
                    return shift ? DAWN_KEY_SHIFT_DOWN : DAWN_KEY_DOWN;
                case 'C':
                    if (alt && shift)
                        return DAWN_KEY_ALT_SHIFT_RIGHT;
                    if (alt)
                        return DAWN_KEY_ALT_RIGHT;
                    if (ctrl && shift)
                        return DAWN_KEY_CTRL_SHIFT_RIGHT;
                    if (ctrl)
                        return DAWN_KEY_CTRL_RIGHT;
                    if (shift)
                        return DAWN_KEY_SHIFT_RIGHT;
                    return DAWN_KEY_RIGHT;
                case 'D':
                    if (alt && shift)
                        return DAWN_KEY_ALT_SHIFT_LEFT;
                    if (alt)
                        return DAWN_KEY_ALT_LEFT;
                    if (ctrl && shift)
                        return DAWN_KEY_CTRL_SHIFT_LEFT;
                    if (ctrl)
                        return DAWN_KEY_CTRL_LEFT;
                    if (shift)
                        return DAWN_KEY_SHIFT_LEFT;
                    return DAWN_KEY_LEFT;
                case 'H':
                    return ctrl ? DAWN_KEY_CTRL_HOME : DAWN_KEY_HOME;
                case 'F':
                    return ctrl ? DAWN_KEY_CTRL_END : DAWN_KEY_END;
                }
            }
        }

        // Simple arrow keys
        if (vt_buf_len == 3) {
            switch (vt_buf[2]) {
            case 'A':
                return DAWN_KEY_UP;
            case 'B':
                return DAWN_KEY_DOWN;
            case 'C':
                return DAWN_KEY_RIGHT;
            case 'D':
                return DAWN_KEY_LEFT;
            case 'H':
                return DAWN_KEY_HOME;
            case 'F':
                return DAWN_KEY_END;
            case 'Z':
                return DAWN_KEY_BTAB;
            }
        }
    } else if (vt_buf[1] == 'O' && vt_buf_len == 3) {
        switch (vt_buf[2]) {
        case 'H':
            return DAWN_KEY_HOME;
        case 'F':
            return DAWN_KEY_END;
        }
    } else if (vt_buf_len == 2) {
        // Alt+key
        if (vt_buf[1] == 'b')
            return DAWN_KEY_ALT_LEFT;
        if (vt_buf[1] == 'f')
            return DAWN_KEY_ALT_RIGHT;
    }

    return DAWN_KEY_NONE;
}

static int32_t win32_read_key(void)
{
    INPUT_RECORD rec;
    DWORD read;

    while (1) {
        // Check if input is available (non-blocking)
        DWORD avail;
        if (!GetNumberOfConsoleInputEvents(win32_state.h_stdin, &avail) || avail == 0)
            return DAWN_KEY_NONE;

        if (!ReadConsoleInputA(win32_state.h_stdin, &rec, 1, &read) || read == 0)
            return DAWN_KEY_NONE;

        // Window resize event
        if (rec.EventType == WINDOW_BUFFER_SIZE_EVENT) {
            InterlockedExchange(&win32_state.resize_needed, 1);
            continue;
        }

        // Mouse event
        if (rec.EventType == MOUSE_EVENT) {
            MOUSE_EVENT_RECORD* mer = &rec.Event.MouseEvent;
            win32_state.last_mouse_col = mer->dwMousePosition.X + 1;
            win32_state.last_mouse_row = mer->dwMousePosition.Y + 1;

            if (mer->dwEventFlags == MOUSE_WHEELED) {
                int32_t delta = (int16_t)HIWORD(mer->dwButtonState);
                return delta > 0 ? DAWN_KEY_MOUSE_SCROLL_UP : DAWN_KEY_MOUSE_SCROLL_DOWN;
            }
            if (mer->dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED) {
                return DAWN_KEY_MOUSE_CLICK;
            }
            continue;
        }

        if (rec.EventType != KEY_EVENT || !rec.Event.KeyEvent.bKeyDown)
            continue;

        KEY_EVENT_RECORD* ker = &rec.Event.KeyEvent;
        char c = ker->uChar.AsciiChar;
        WORD vk = ker->wVirtualKeyCode;
        DWORD ctrl_state = ker->dwControlKeyState;
        bool shift = (ctrl_state & SHIFT_PRESSED) != 0;
        bool ctrl = (ctrl_state & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
        bool alt = (ctrl_state & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) != 0;

        // Handle VT sequences from terminal emulators
        if (c == '\x1b') {
            vt_buf[0] = c;
            vt_buf_len = 1;

            // Try to read more of the sequence
            DWORD start = GetTickCount();
            while (vt_buf_len < (int32_t)sizeof(vt_buf) - 1) {
                DWORD avail;
                if (!GetNumberOfConsoleInputEvents(win32_state.h_stdin, &avail) || avail == 0) {
                    if (GetTickCount() - start > 50)
                        break;
                    Sleep(1);
                    continue;
                }

                if (!ReadConsoleInputA(win32_state.h_stdin, &rec, 1, &read) || read == 0)
                    break;

                if (rec.EventType != KEY_EVENT || !rec.Event.KeyEvent.bKeyDown)
                    continue;

                char nc = rec.Event.KeyEvent.uChar.AsciiChar;
                if (nc == 0)
                    break;

                vt_buf[vt_buf_len++] = nc;

                // Check for sequence terminators
                if (nc >= 'A' && nc <= 'Z')
                    break;
                if (nc >= 'a' && nc <= 'z')
                    break;
                if (nc == '~')
                    break;
                if (nc == '\\' && vt_buf_len >= 2 && vt_buf[vt_buf_len - 2] == '\x1b')
                    break;
            }

            if (vt_buf_len > 1) {
                int32_t key = parse_vt_sequence();
                if (key != DAWN_KEY_NONE)
                    return key;
            }
            return '\x1b';
        }

        // Direct Win32 key handling
        switch (vk) {
        case VK_UP:
            if (shift)
                return DAWN_KEY_SHIFT_UP;
            return DAWN_KEY_UP;
        case VK_DOWN:
            if (shift)
                return DAWN_KEY_SHIFT_DOWN;
            return DAWN_KEY_DOWN;
        case VK_RIGHT:
            if (alt && shift)
                return DAWN_KEY_ALT_SHIFT_RIGHT;
            if (alt)
                return DAWN_KEY_ALT_RIGHT;
            if (ctrl && shift)
                return DAWN_KEY_CTRL_SHIFT_RIGHT;
            if (ctrl)
                return DAWN_KEY_CTRL_RIGHT;
            if (shift)
                return DAWN_KEY_SHIFT_RIGHT;
            return DAWN_KEY_RIGHT;
        case VK_LEFT:
            if (alt && shift)
                return DAWN_KEY_ALT_SHIFT_LEFT;
            if (alt)
                return DAWN_KEY_ALT_LEFT;
            if (ctrl && shift)
                return DAWN_KEY_CTRL_SHIFT_LEFT;
            if (ctrl)
                return DAWN_KEY_CTRL_LEFT;
            if (shift)
                return DAWN_KEY_SHIFT_LEFT;
            return DAWN_KEY_LEFT;
        case VK_HOME:
            return ctrl ? DAWN_KEY_CTRL_HOME : DAWN_KEY_HOME;
        case VK_END:
            return ctrl ? DAWN_KEY_CTRL_END : DAWN_KEY_END;
        case VK_PRIOR:
            return DAWN_KEY_PGUP;
        case VK_NEXT:
            return DAWN_KEY_PGDN;
        case VK_DELETE:
            return DAWN_KEY_DEL;
        case VK_TAB:
            return shift ? DAWN_KEY_BTAB : '\t';
        case VK_RETURN:
            return '\r';
        case VK_BACK:
            return 127;
        case VK_ESCAPE:
            return '\x1b';
        }

        // Ctrl+key combinations
        if (ctrl && !alt && vk >= 'A' && vk <= 'Z') {
            return vk - 'A' + 1;
        }
        if (ctrl && vk == VK_OEM_2) { // Ctrl+/
            return 31;
        }

        // Regular character
        if (c != 0) {
            return (uint8_t)c;
        }
    }
}

static int32_t win32_get_last_mouse_col(void)
{
    return win32_state.last_mouse_col;
}

static int32_t win32_get_last_mouse_row(void)
{
    return win32_state.last_mouse_row;
}

static bool win32_check_resize(void)
{
    if (InterlockedExchange(&win32_state.resize_needed, 0)) {
        return true;
    }
    return false;
}

static bool win32_check_quit(void)
{
    return win32_state.quit_requested != 0;
}

static bool win32_input_available(float timeout_ms)
{
    DWORD timeout = (timeout_ms < 0) ? INFINITE : (DWORD)timeout_ms;
    DWORD result = WaitForSingleObject(win32_state.h_stdin, timeout);
    if (result == WAIT_OBJECT_0) {
        DWORD avail;
        if (GetNumberOfConsoleInputEvents(win32_state.h_stdin, &avail) && avail > 0) {
            return true;
        }
    }
    return false;
}

static void (*user_resize_callback)(int32_t) = NULL;
static void (*user_quit_callback)(int32_t) = NULL;

static void win32_register_signals(void (*on_resize)(int32_t), void (*on_quit)(int32_t))
{
    user_resize_callback = on_resize;
    user_quit_callback = on_quit;
}

static void win32_clipboard_copy(const char* text, size_t len)
{
    if (!OpenClipboard(NULL))
        return;

    EmptyClipboard();

    // Convert UTF-8 to wide string
    int32_t wlen = MultiByteToWideChar(CP_UTF8, 0, text, (int32_t)len, NULL, 0);
    if (wlen <= 0) {
        CloseClipboard();
        return;
    }

    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)(wlen + 1) * sizeof(wchar_t));
    if (!hMem) {
        CloseClipboard();
        return;
    }

    wchar_t* pMem = GlobalLock(hMem);
    MultiByteToWideChar(CP_UTF8, 0, text, (int32_t)len, pMem, wlen);
    pMem[wlen] = 0;
    GlobalUnlock(hMem);

    SetClipboardData(CF_UNICODETEXT, hMem);
    CloseClipboard();
}

static char* win32_clipboard_paste(size_t* out_len)
{
    *out_len = 0;

    if (!OpenClipboard(NULL))
        return NULL;

    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (!hData) {
        CloseClipboard();
        return NULL;
    }

    wchar_t* pData = GlobalLock(hData);
    if (!pData) {
        CloseClipboard();
        return NULL;
    }

    int32_t len = WideCharToMultiByte(CP_UTF8, 0, pData, -1, NULL, 0, NULL, NULL);
    if (len <= 0) {
        GlobalUnlock(hData);
        CloseClipboard();
        return NULL;
    }

    char* result = malloc((size_t)len);
    if (!result) {
        GlobalUnlock(hData);
        CloseClipboard();
        return NULL;
    }

    WideCharToMultiByte(CP_UTF8, 0, pData, -1, result, len, NULL, NULL);
    *out_len = strlen(result);

    GlobalUnlock(hData);
    CloseClipboard();

    return result;
}

static const char* win32_get_home_dir(void)
{
    if (win32_state.home_dir[0])
        return win32_state.home_dir;

    const char* userprofile = getenv("USERPROFILE");
    if (!userprofile) {
        return NULL;
    }

    strncpy(win32_state.home_dir, userprofile, MAX_PATH - 1);
    win32_state.home_dir[MAX_PATH - 1] = '\0';
    return win32_state.home_dir;
}

static bool win32_mkdir_p(const char* path)
{
    char tmp[MAX_PATH];
    strncpy(tmp, path, MAX_PATH - 1);
    tmp[MAX_PATH - 1] = '\0';

    // Convert forward slashes to backslashes
    for (char* p = tmp; *p; p++) {
        if (*p == '/')
            *p = '\\';
    }

    for (char* p = tmp + 1; *p; p++) {
        if (*p == '\\') {
            *p = '\0';
            if (_mkdir(tmp) != 0 && errno != EEXIST) {
                *p = '\\';
                // Check if it's a drive letter like "C:"
                if (p - tmp != 2 || tmp[1] != ':') {
                    return false;
                }
            }
            *p = '\\';
        }
    }
    return _mkdir(tmp) == 0 || errno == EEXIST;
}

static bool win32_file_exists(const char* path)
{
    wchar_t* wpath = utf8_to_wide(path);
    if (!wpath)
        return false;
    DWORD attrs = GetFileAttributes(wpath);
    free(wpath);
    return attrs != INVALID_FILE_ATTRIBUTES;
}

static char* win32_read_file(const char* path, size_t* out_len)
{
    *out_len = 0;
    FILE* f = fopen(path, "rb");
    if (!f)
        return NULL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size < 0 || size > 100 * 1024 * 1024) {
        fclose(f);
        return NULL;
    }

    char* data = malloc((size_t)size + 1);
    if (!data) {
        fclose(f);
        return NULL;
    }

    size_t read_size = fread(data, 1, (size_t)size, f);
    fclose(f);

    data[read_size] = '\0';
    *out_len = read_size;
    return data;
}

static bool win32_write_file(const char* path, const char* data, size_t len)
{
    FILE* f = fopen(path, "wb");
    if (!f)
        return false;

    size_t written = fwrite(data, 1, len, f);
    fclose(f);

    return written == len;
}

static bool win32_list_dir(const char* path, char*** out_names, int32_t* out_count)
{
    *out_names = NULL;
    *out_count = 0;

    wchar_t* wpath = utf8_to_wide(path);
    if (!wpath)
        return false;

    wchar_t wpattern[MAX_PATH];
    swprintf(wpattern, MAX_PATH, L"%s\\*", wpath);
    free(wpath);

    WIN32_FIND_DATA fd;
    HANDLE hFind = FindFirstFile(wpattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE)
        return false;

    int32_t cap = 64;
    char** names = malloc(sizeof(char*) * (size_t)cap);
    int32_t count = 0;

    do {
        if (fd.cFileName[0] == L'.')
            continue;

        if (count >= cap) {
            cap *= 2;
            names = realloc(names, sizeof(char*) * (size_t)cap);
        }
        names[count++] = wide_to_utf8(fd.cFileName);
    } while (FindNextFile(hFind, &fd));

    FindClose(hFind);

    *out_names = names;
    *out_count = count;
    return true;
}

static int64_t win32_get_mtime(const char* path)
{
    wchar_t* wpath = utf8_to_wide(path);
    if (!wpath)
        return 0;

    WIN32_FILE_ATTRIBUTE_DATA fad;
    BOOL ok = GetFileAttributesEx(wpath, GetFileExInfoStandard, &fad);
    free(wpath);
    if (!ok)
        return 0;

    ULARGE_INTEGER ull;
    ull.LowPart = fad.ftLastWriteTime.dwLowDateTime;
    ull.HighPart = fad.ftLastWriteTime.dwHighDateTime;

    // Convert from 100-nanosecond intervals since 1601 to Unix timestamp
    return (int64_t)((ull.QuadPart - 116444736000000000ULL) / 10000000ULL);
}

static bool win32_delete_file(const char* path)
{
    wchar_t* wpath = utf8_to_wide(path);
    if (!wpath)
        return false;
    BOOL ok = DeleteFile(wpath);
    free(wpath);
    return ok != 0;
}

static void win32_reveal_in_explorer(const char* path)
{
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "explorer /select,\"%s\"", path);
    int32_t r = system(cmd);
    (void)r;
}

static int64_t win32_clock(DawnClock kind)
{
    if (kind == DAWN_CLOCK_MS) {
        LARGE_INTEGER freq, counter;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&counter);
        return (int64_t)(counter.QuadPart * 1000 / freq.QuadPart);
    }
    return (int64_t)time(NULL);
}

static void win32_sleep_ms(int32_t ms)
{
    Sleep((DWORD)ms);
}

static void win32_get_local_time(DawnTime* out)
{
    if (!out)
        return;
    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    out->year = (int16_t)(t->tm_year + 1900);
    out->mon = (uint8_t)t->tm_mon;
    out->mday = (uint8_t)t->tm_mday;
    out->hour = (uint8_t)t->tm_hour;
    out->min = (uint8_t)t->tm_min;
    out->sec = (uint8_t)t->tm_sec;
    out->wday = (uint8_t)t->tm_wday;
}

static void win32_get_local_time_from(DawnTime* out, int64_t timestamp)
{
    if (!out)
        return;
    time_t ts = (time_t)timestamp;
    struct tm* t = localtime(&ts);
    if (!t) {
        memset(out, 0, sizeof(*out));
        return;
    }
    out->year = (int16_t)(t->tm_year + 1900);
    out->mon = (uint8_t)t->tm_mon;
    out->mday = (uint8_t)t->tm_mday;
    out->hour = (uint8_t)t->tm_hour;
    out->min = (uint8_t)t->tm_min;
    out->sec = (uint8_t)t->tm_sec;
    out->wday = (uint8_t)t->tm_wday;
}

static const char* win32_get_username(void)
{
    static char name[256];
    static bool cached = false;

    if (cached)
        return name;

    wchar_t wname[256];
    DWORD size = 256;
    if (GetUserName(wname, &size) && wname[0]) {
        char* utf8 = wide_to_utf8(wname);
        if (utf8) {
            strncpy(name, utf8, sizeof(name) - 1);
            name[sizeof(name) - 1] = '\0';
            free(utf8);
            cached = true;
            return name;
        }
    }

    const char* user = getenv("USERNAME");
    if (user) {
        strncpy(name, user, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
        cached = true;
        return name;
    }

    strcpy(name, "Unknown");
    cached = true;
    return name;
}

static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static char* base64_encode(const uint8_t* data, size_t input_len, size_t* output_len)
{
    if (input_len > (SIZE_MAX - 2) / 4 * 3)
        return NULL;

    size_t encoded_len = 4 * ((input_len + 2) / 3);
    char* encoded = malloc(encoded_len + 1);
    if (!encoded)
        return NULL;

    size_t i, j;
    for (i = 0, j = 0; i < input_len;) {
        uint32_t octet_a = i < input_len ? data[i++] : 0;
        uint32_t octet_b = i < input_len ? data[i++] : 0;
        uint32_t octet_c = i < input_len ? data[i++] : 0;
        uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;
        encoded[j++] = b64_table[(triple >> 18) & 0x3F];
        encoded[j++] = b64_table[(triple >> 12) & 0x3F];
        encoded[j++] = b64_table[(triple >> 6) & 0x3F];
        encoded[j++] = b64_table[triple & 0x3F];
    }

    int32_t mod = (int32_t)(input_len % 3);
    if (mod > 0) {
        encoded[encoded_len - 1] = '=';
        if (mod == 1)
            encoded[encoded_len - 2] = '=';
    }

    encoded[encoded_len] = '\0';
    *output_len = encoded_len;
    return encoded;
}

static bool win32_image_is_supported(const char* path)
{
    if (!path)
        return false;
    const char* ext = strrchr(path, '.');
    if (!ext)
        return false;
    ext++;

    char lower[16];
    size_t i;
    for (i = 0; i < sizeof(lower) - 1 && ext[i]; i++) {
        lower[i] = (ext[i] >= 'A' && ext[i] <= 'Z') ? (ext[i] | 32) : ext[i];
    }
    lower[i] = '\0';

    return strcmp(lower, "png") == 0 || strcmp(lower, "jpg") == 0 ||
           strcmp(lower, "jpeg") == 0 || strcmp(lower, "gif") == 0 ||
           strcmp(lower, "bmp") == 0 || strcmp(lower, "svg") == 0;
}

static bool win32_image_get_size(const char* path, int32_t* out_width, int32_t* out_height)
{
    if (!path || !out_width || !out_height)
        return false;

    int32_t w, h, channels;
    if (stbi_info(path, &w, &h, &channels)) {
        *out_width = w;
        *out_height = h;
        return true;
    }
    return false;
}

static TransmittedImage* find_transmitted(const char* path)
{
    int64_t current_mtime = win32_get_mtime(path);
    for (int32_t i = 0; i < transmitted_count; i++) {
        if (transmitted_images[i].path && strcmp(transmitted_images[i].path, path) == 0 &&
            transmitted_images[i].mtime == current_mtime) {
            return &transmitted_images[i];
        }
    }
    return NULL;
}

static uint32_t transmit_to_terminal(const char* path)
{
    wchar_t* wpath = utf8_to_wide(path);
    if (!wpath)
        return 0;

    wchar_t wabs_path[MAX_PATH];
    if (!GetFullPathName(wpath, MAX_PATH, wabs_path, NULL)) {
        wcsncpy(wabs_path, wpath, MAX_PATH - 1);
        wabs_path[MAX_PATH - 1] = L'\0';
    }
    free(wpath);

    char* abs_path = wide_to_utf8(wabs_path);
    if (!abs_path)
        return 0;

    size_t path_len = strlen(abs_path);
    size_t b64_len;
    char* b64_path = base64_encode((uint8_t*)abs_path, path_len, &b64_len);
    free(abs_path);
    if (!b64_path)
        return 0;

    uint32_t image_id = next_image_id++;

    buf_printf("\x1b_Ga=t,t=f,f=100,i=%u,q=2;%s\x1b\\", image_id, b64_path);

    free(b64_path);

    TransmittedImage* entry;
    if (transmitted_count < MAX_TRANSMITTED_IMAGES) {
        entry = &transmitted_images[transmitted_count++];
    } else {
        buf_printf("\x1b_Ga=d,d=I,i=%u,q=2\x1b\\", transmitted_images[0].image_id);
        free(transmitted_images[0].path);
        memmove(&transmitted_images[0], &transmitted_images[1],
            sizeof(TransmittedImage) * (MAX_TRANSMITTED_IMAGES - 1));
        entry = &transmitted_images[MAX_TRANSMITTED_IMAGES - 1];
    }
    entry->path = _strdup(path);
    entry->image_id = image_id;
    entry->mtime = win32_get_mtime(path);

    return image_id;
}

static uint32_t ensure_transmitted(const char* path)
{
    TransmittedImage* t = find_transmitted(path);
    if (t)
        return t->image_id;
    return transmit_to_terminal(path);
}

static int32_t win32_image_display(const char* path, int32_t row, int32_t col, int32_t max_cols, int32_t max_rows)
{
    if (!path)
        return 0;
    (void)row;
    (void)col;

    uint32_t image_id = ensure_transmitted(path);
    if (image_id == 0)
        return 0;

    buf_printf("\x1b_Ga=p,i=%u,z=-2,q=2", image_id);
    if (max_cols > 0)
        buf_printf(",c=%d", max_cols);
    if (max_rows > 0)
        buf_printf(",r=%d", max_rows);
    buf_append_str("\x1b\\");

    int32_t rows_used;
    if (max_rows > 0) {
        rows_used = max_rows;
    } else {
        int32_t pixel_w, pixel_h;
        if (win32_image_get_size(path, &pixel_w, &pixel_h)) {
            rows_used = win32_image_calc_rows(pixel_w, pixel_h, max_cols, 0);
        } else {
            rows_used = 1;
        }
    }

    if (win32_state.mode == DAWN_MODE_PRINT) {
        win32_state.print_row += rows_used;
        win32_state.print_col = 1;
    }

    return rows_used;
}

static int32_t win32_image_display_cropped(const char* path, int32_t row, int32_t col, int32_t max_cols,
    int32_t crop_top_rows, int32_t visible_rows)
{
    if (!path)
        return 0;
    (void)row;
    (void)col;

    uint32_t image_id = ensure_transmitted(path);
    if (image_id == 0)
        return 0;

    int32_t pixel_w, pixel_h;
    if (!win32_image_get_size(path, &pixel_w, &pixel_h)) {
        return win32_image_display(path, row, col, max_cols, visible_rows);
    }

    int32_t img_rows = win32_image_calc_rows(pixel_w, pixel_h, max_cols, 0);
    int32_t cell_height_px = pixel_h / (img_rows > 0 ? img_rows : 1);
    if (cell_height_px <= 0)
        cell_height_px = 20;

    int32_t crop_y = crop_top_rows * cell_height_px;
    int32_t crop_h = visible_rows * cell_height_px;

    if (crop_y >= pixel_h)
        return 0;
    if (crop_y + crop_h > pixel_h)
        crop_h = pixel_h - crop_y;

    buf_printf("\x1b_Ga=p,i=%u,z=-2,q=2", image_id);
    if (max_cols > 0)
        buf_printf(",c=%d", max_cols);
    if (visible_rows > 0)
        buf_printf(",r=%d", visible_rows);

    if (crop_top_rows > 0 || visible_rows < img_rows) {
        buf_printf(",x=0,y=%d,w=%d,h=%d", crop_y, pixel_w, crop_h);
    }
    buf_append_str("\x1b\\");

    if (win32_state.mode == DAWN_MODE_PRINT) {
        win32_state.print_row += visible_rows;
        win32_state.print_col = 1;
    }

    return visible_rows;
}

static void win32_image_frame_start(void)
{
    buf_append_str("\x1b_Ga=d,d=a,q=2\x1b\\");
}

static void win32_image_frame_end(void)
{
}

static void win32_image_clear_all(void)
{
    buf_append_str("\x1b_Ga=d,d=A,q=2\x1b\\");
    buf_flush();
    for (int32_t i = 0; i < transmitted_count; i++) {
        free(transmitted_images[i].path);
    }
    transmitted_count = 0;
}

static void win32_image_mask_region(int32_t col, int32_t row, int32_t cols, int32_t rows, DawnColor bg)
{
    if (cols <= 0 || rows <= 0)
        return;

    uint8_t pixel[4] = { bg.r, bg.g, bg.b, 255 };

    size_t b64_len;
    char* b64_data = base64_encode(pixel, 4, &b64_len);
    if (!b64_data)
        return;

    buf_printf(CSI "%d;%dH", row, col);
    buf_printf("\x1b_Ga=T,f=32,s=1,v=1,c=%d,r=%d,z=-1,q=2;%s\x1b\\", cols, rows, b64_data);

    free(b64_data);
}

// Hash function for cache keys
static void hash_to_hex(const char* str, char* out_hex)
{
    uint64_t hash = 5381;
    int32_t c;
    while ((c = (uint8_t)*str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    snprintf(out_hex, 65, "%016llx", (unsigned long long)hash);
}

static bool is_remote_url(const char* path)
{
    if (!path)
        return false;
    return (strncmp(path, "http://", 7) == 0 || strncmp(path, "https://", 8) == 0);
}

// Async image download system using WinHTTP
#define MAX_DOWNLOADS 8
#define MAX_FAILED_URLS 32

typedef struct {
    char* url;
    char* temp_path;
    char* final_path;
    HINTERNET h_session;
    HINTERNET h_connect;
    HINTERNET h_request;
    HANDLE h_file;
    uint8_t* buffer;
    DWORD buffer_size;
    bool request_sent;
    bool headers_received;
} AsyncDownload;

static AsyncDownload downloads[MAX_DOWNLOADS];
static int32_t download_count = 0;
static char* failed_urls[MAX_FAILED_URLS];
static int32_t failed_url_count = 0;

static bool is_failed_url(const char* url)
{
    for (int32_t i = 0; i < failed_url_count; i++) {
        if (failed_urls[i] && strcmp(failed_urls[i], url) == 0)
            return true;
    }
    return false;
}

static void mark_url_failed(const char* url)
{
    if (is_failed_url(url))
        return;
    if (failed_url_count >= MAX_FAILED_URLS) {
        free(failed_urls[0]);
        memmove(&failed_urls[0], &failed_urls[1], sizeof(char*) * (MAX_FAILED_URLS - 1));
        failed_url_count--;
    }
    failed_urls[failed_url_count++] = _strdup(url);
}

static bool is_download_in_progress(const char* url)
{
    for (int32_t i = 0; i < download_count; i++) {
        if (downloads[i].url && strcmp(downloads[i].url, url) == 0)
            return true;
    }
    return false;
}

static bool convert_downloaded_to_png(const char* temp_path, const char* final_path, const char* url)
{
    if (svg_is_svg_file(url)) {
        size_t len;
        char* data = win32_read_file(temp_path, &len);
        if (!data)
            return false;

        uint8_t* pixels;
        int32_t w, h;
        bool ok = svg_rasterize(data, &pixels, &w, &h);
        free(data);
        if (!ok)
            return false;

        ok = stbi_write_png(final_path, w, h, 4, pixels, w * 4) != 0;
        free(pixels);
        return ok;
    }

    int32_t w, h, channels;
    uint8_t* pixels = stbi_load(temp_path, &w, &h, &channels, 4);
    if (!pixels)
        return false;

    bool ok = stbi_write_png(final_path, w, h, 4, pixels, w * 4) != 0;
    stbi_image_free(pixels);
    return ok;
}

static void cleanup_download(AsyncDownload* dl)
{
    if (dl->h_request) {
        WinHttpCloseHandle(dl->h_request);
        dl->h_request = NULL;
    }
    if (dl->h_connect) {
        WinHttpCloseHandle(dl->h_connect);
        dl->h_connect = NULL;
    }
    if (dl->h_session) {
        WinHttpCloseHandle(dl->h_session);
        dl->h_session = NULL;
    }
    if (dl->h_file != INVALID_HANDLE_VALUE && dl->h_file != NULL) {
        CloseHandle(dl->h_file);
        dl->h_file = INVALID_HANDLE_VALUE;
    }
    free(dl->buffer);
    dl->buffer = NULL;
}

static void finalize_download(AsyncDownload* dl, bool success)
{
    cleanup_download(dl);

    if (success && dl->temp_path && dl->final_path) {
        if (!convert_downloaded_to_png(dl->temp_path, dl->final_path, dl->url))
            mark_url_failed(dl->url);
    } else if (dl->url) {
        mark_url_failed(dl->url);
    }

    if (dl->temp_path) {
        wchar_t* wtemp = utf8_to_wide(dl->temp_path);
        if (wtemp) {
            DeleteFile(wtemp);
            free(wtemp);
        }
        free(dl->temp_path);
    }
    free(dl->final_path);
    free(dl->url);
    memset(dl, 0, sizeof(*dl));
}

static void poll_downloads(void)
{
    for (int32_t i = 0; i < download_count; i++) {
        AsyncDownload* dl = &downloads[i];
        if (!dl->h_request)
            continue;

        // Check if data is available (non-blocking)
        DWORD bytes_available = 0;
        if (!WinHttpQueryDataAvailable(dl->h_request, &bytes_available)) {
            DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                continue; // Still waiting
            }
            // Error - finalize with failure
            finalize_download(dl, false);
            memmove(&downloads[i], &downloads[i + 1],
                sizeof(AsyncDownload) * (download_count - i - 1));
            download_count--;
            i--;
            continue;
        }

        if (bytes_available == 0) {
            // Download complete
            CloseHandle(dl->h_file);
            dl->h_file = INVALID_HANDLE_VALUE;
            finalize_download(dl, true);
            memmove(&downloads[i], &downloads[i + 1],
                sizeof(AsyncDownload) * (download_count - i - 1));
            download_count--;
            i--;
            continue;
        }

        // Read available data
        if (bytes_available > dl->buffer_size) {
            dl->buffer = realloc(dl->buffer, bytes_available);
            dl->buffer_size = bytes_available;
        }

        DWORD bytes_read = 0;
        if (WinHttpReadData(dl->h_request, dl->buffer, bytes_available, &bytes_read)) {
            if (bytes_read > 0) {
                DWORD bytes_written;
                WriteFile(dl->h_file, dl->buffer, bytes_read, &bytes_written, NULL);
            }
        }
    }
}

static bool parse_url(const char* url, wchar_t* host, size_t host_len,
    wchar_t* path, size_t path_len, INTERNET_PORT* port, bool* is_https)
{
    // Convert URL to wide string
    int32_t wurl_len = MultiByteToWideChar(CP_UTF8, 0, url, -1, NULL, 0);
    wchar_t* wurl = malloc(wurl_len * sizeof(wchar_t));
    if (!wurl)
        return false;
    MultiByteToWideChar(CP_UTF8, 0, url, -1, wurl, wurl_len);

    URL_COMPONENTS uc = { 0 };
    uc.dwStructSize = sizeof(uc);
    uc.dwHostNameLength = (DWORD)host_len;
    uc.lpszHostName = host;
    uc.dwUrlPathLength = (DWORD)path_len;
    uc.lpszUrlPath = path;

    bool ok = WinHttpCrackUrl(wurl, 0, 0, &uc) != FALSE;
    if (ok) {
        *port = uc.nPort;
        *is_https = (uc.nScheme == INTERNET_SCHEME_HTTPS);
    }
    free(wurl);
    return ok;
}

static bool start_async_download(const char* url, const char* temp_path, const char* final_path)
{
    if (download_count >= MAX_DOWNLOADS)
        return false;

    wchar_t host[256] = { 0 };
    wchar_t path[1024] = { 0 };
    INTERNET_PORT port = 0;
    bool is_https = false;

    if (!parse_url(url, host, 256, path, 1024, &port, &is_https))
        return false;

    HINTERNET h_session = WinHttpOpen(L"Dawn/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (!h_session)
        return false;

    HINTERNET h_connect = WinHttpConnect(h_session, host, port, 0);
    if (!h_connect) {
        WinHttpCloseHandle(h_session);
        return false;
    }

    DWORD flags = is_https ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET h_request = WinHttpOpenRequest(h_connect, L"GET", path,
        NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!h_request) {
        WinHttpCloseHandle(h_connect);
        WinHttpCloseHandle(h_session);
        return false;
    }

    // Send request
    if (!WinHttpSendRequest(h_request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        WinHttpCloseHandle(h_request);
        WinHttpCloseHandle(h_connect);
        WinHttpCloseHandle(h_session);
        return false;
    }

    // Wait for response headers
    if (!WinHttpReceiveResponse(h_request, NULL)) {
        WinHttpCloseHandle(h_request);
        WinHttpCloseHandle(h_connect);
        WinHttpCloseHandle(h_session);
        return false;
    }

    // Check status code
    DWORD status_code = 0;
    DWORD status_size = sizeof(status_code);
    WinHttpQueryHeaders(h_request,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &status_size, WINHTTP_NO_HEADER_INDEX);
    if (status_code >= 400) {
        WinHttpCloseHandle(h_request);
        WinHttpCloseHandle(h_connect);
        WinHttpCloseHandle(h_session);
        return false;
    }

    // Open output file
    wchar_t* wtemp_path = utf8_to_wide(temp_path);
    if (!wtemp_path) {
        WinHttpCloseHandle(h_request);
        WinHttpCloseHandle(h_connect);
        WinHttpCloseHandle(h_session);
        return false;
    }
    HANDLE h_file = CreateFile(wtemp_path, GENERIC_WRITE, 0, NULL,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    free(wtemp_path);
    if (h_file == INVALID_HANDLE_VALUE) {
        WinHttpCloseHandle(h_request);
        WinHttpCloseHandle(h_connect);
        WinHttpCloseHandle(h_session);
        return false;
    }

    AsyncDownload* dl = &downloads[download_count++];
    memset(dl, 0, sizeof(*dl));
    dl->url = _strdup(url);
    dl->temp_path = _strdup(temp_path);
    dl->final_path = _strdup(final_path);
    dl->h_session = h_session;
    dl->h_connect = h_connect;
    dl->h_request = h_request;
    dl->h_file = h_file;
    dl->buffer_size = 8192;
    dl->buffer = malloc(dl->buffer_size);

    return true;
}

static bool download_url_to_cache(const char* url, char* cached_path, size_t path_size)
{
    if (!url || !cached_path)
        return false;

    if (is_failed_url(url))
        return false;

    const char* home = win32_get_home_dir();
    if (!home)
        return false;

    char cache_dir[MAX_PATH];
    snprintf(cache_dir, sizeof(cache_dir), "%s\\.dawn\\image-cache", home);
    win32_mkdir_p(cache_dir);

    char hash_hex[65];
    hash_to_hex(url, hash_hex);

    snprintf(cached_path, path_size, "%s\\%.16s.png", cache_dir, hash_hex);

    if (win32_file_exists(cached_path)) {
        int32_t w, h, channels;
        if (stbi_info(cached_path, &w, &h, &channels))
            return true;
        win32_delete_file(cached_path);
    }

    if (is_download_in_progress(url))
        return false;

    char temp_path[MAX_PATH];
    snprintf(temp_path, sizeof(temp_path), "%s\\%.16s.tmp", cache_dir, hash_hex);
    start_async_download(url, temp_path, cached_path);

    return false;
}

static bool is_png_file(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f)
        return false;
    uint8_t header[8];
    size_t n = fread(header, 1, 8, f);
    fclose(f);
    if (n < 8)
        return false;
    return header[0] == 0x89 && header[1] == 0x50 && header[2] == 0x4E && header[3] == 0x47 &&
           header[4] == 0x0D && header[5] == 0x0A && header[6] == 0x1A && header[7] == 0x0A;
}

static bool ensure_png_cached(const char* src_path, char* out, size_t out_size)
{
    assert(src_path && out && out_size > 0);

    if (is_png_file(src_path)) {
        strncpy(out, src_path, out_size - 1);
        out[out_size - 1] = '\0';
        return true;
    }

    const char* home = win32_get_home_dir();
    assert(home && "Failed to get home directory");

    char cache_dir[MAX_PATH];
    snprintf(cache_dir, sizeof(cache_dir), "%s\\.dawn\\image-cache", home);
    win32_mkdir_p(cache_dir);

    wchar_t* wsrc_path = utf8_to_wide(src_path);
    wchar_t wabs_path[MAX_PATH];
    if (wsrc_path && GetFullPathName(wsrc_path, MAX_PATH, wabs_path, NULL)) {
        free(wsrc_path);
    } else {
        free(wsrc_path);
        wchar_t* tmp = utf8_to_wide(src_path);
        if (tmp) {
            wcsncpy(wabs_path, tmp, MAX_PATH - 1);
            wabs_path[MAX_PATH - 1] = L'\0';
            free(tmp);
        } else {
            return false;
        }
    }
    char* abs_path = wide_to_utf8(wabs_path);
    if (!abs_path)
        return false;

    int64_t mtime = win32_get_mtime(abs_path);

    char key[MAX_PATH + 21];
    snprintf(key, sizeof(key), "%s:%lld", abs_path, (long long)mtime);

    char hash_hex[65];
    hash_to_hex(key, hash_hex);

    snprintf(out, out_size, "%s\\%.16s.png", cache_dir, hash_hex);

    if (win32_file_exists(out)) {
        free(abs_path);
        return true;
    }

    if (svg_is_svg_file(abs_path)) {
        size_t svg_len;
        char* svg_data = win32_read_file(abs_path, &svg_len);
        free(abs_path);
        if (!svg_data)
            return false;

        uint8_t* pixels;
        int32_t w, h;
        bool ok = svg_rasterize(svg_data, &pixels, &w, &h);
        free(svg_data);
        if (!ok)
            return false;

        ok = stbi_write_png(out, w, h, 4, pixels, w * 4) != 0;
        free(pixels);
        return ok;
    }

    free(abs_path);

    int32_t w, h, channels;
    uint8_t* pixels = stbi_load(src_path, &w, &h, &channels, 4);
    if (!pixels) {
        return false;
    }

    int32_t write_ok = stbi_write_png(out, w, h, 4, pixels, w * 4);
    stbi_image_free(pixels);

    return write_ok != 0;
}

static bool win32_image_resolve_path(const char* raw_path, const char* base_dir,
    char* out, size_t out_size)
{
    if (!raw_path || !out || out_size == 0)
        return false;

    if (is_remote_url(raw_path)) {
        return download_url_to_cache(raw_path, out, out_size);
    }

    char resolved[MAX_PATH];

    // Check for drive letter (absolute path on Windows)
    if ((raw_path[0] && raw_path[1] == ':') || raw_path[0] == '\\' || raw_path[0] == '/') {
        if (win32_file_exists(raw_path)) {
            return ensure_png_cached(raw_path, out, out_size);
        }
        return false;
    }

    // Home directory expansion
    if (raw_path[0] == '~') {
        const char* home = win32_get_home_dir();
        if (home) {
            snprintf(resolved, sizeof(resolved), "%s%s", home, raw_path + 1);
            // Convert forward slashes
            for (char* p = resolved; *p; p++) {
                if (*p == '/')
                    *p = '\\';
            }
            if (win32_file_exists(resolved)) {
                return ensure_png_cached(resolved, out, out_size);
            }
        }
        return false;
    }

    // Relative path - try base_dir
    if (base_dir && base_dir[0]) {
        snprintf(resolved, sizeof(resolved), "%s\\%s", base_dir, raw_path);
        // Convert forward slashes
        for (char* p = resolved; *p; p++) {
            if (*p == '/')
                *p = '\\';
        }
        if (win32_file_exists(resolved)) {
            return ensure_png_cached(resolved, out, out_size);
        }
    }

    // Try as-is
    if (win32_file_exists(raw_path)) {
        return ensure_png_cached(raw_path, out, out_size);
    }

    return false;
}

static int32_t win32_image_calc_rows(int32_t pixel_width, int32_t pixel_height, int32_t max_cols, int32_t max_rows)
{
    if (pixel_width <= 0 || pixel_height <= 0)
        return 1;
    if (max_rows > 0)
        return max_rows;
    if (max_cols <= 0)
        max_cols = 40;

    double aspect = (double)pixel_height / (double)pixel_width;
    int32_t rows = (int32_t)(max_cols * aspect * 0.5 + 0.5);

    if (rows < 1)
        rows = 1;
    return rows;
}

static void win32_image_invalidate(const char* path)
{
    for (int32_t i = 0; i < transmitted_count; i++) {
        if (transmitted_images[i].path && strcmp(transmitted_images[i].path, path) == 0) {
            buf_printf("\x1b_Ga=d,d=I,i=%u,q=2\x1b\\", transmitted_images[i].image_id);
            free(transmitted_images[i].path);
            memmove(&transmitted_images[i], &transmitted_images[i + 1],
                sizeof(TransmittedImage) * (transmitted_count - i - 1));
            transmitted_count--;
            i--;
        }
    }
    buf_flush();
}

static void win32_execute_pending_jobs(void)
{
    poll_downloads();
}

const DawnBackend dawn_backend_win32 = {
    .name = "win32",

    // Lifecycle
    .init = win32_init,
    .shutdown = win32_shutdown,
    .get_caps = win32_get_capabilities,
    .get_host_bg = win32_get_host_bg,

    // Display
    .get_size = win32_get_size,
    .set_cursor = win32_set_cursor,
    .set_cursor_visible = win32_set_cursor_visible,
    .set_fg = win32_set_fg,
    .set_bg = win32_set_bg,
    .reset_attrs = win32_reset_attrs,
    .set_bold = win32_set_bold,
    .set_italic = win32_set_italic,
    .set_dim = win32_set_dim,
    .set_strike = win32_set_strikethrough,
    .set_underline = win32_set_underline,
    .set_underline_color = win32_set_underline_color,
    .clear_underline = win32_clear_underline,
    .clear_screen = win32_clear_screen,
    .clear_line = win32_clear_line,
    .clear_range = win32_clear_range,
    .write_str = win32_write_str,
    .write_char = win32_write_char,
    .repeat_char = win32_repeat_char,
    .write_scaled = win32_write_scaled,
    .write_scaled_frac = win32_write_scaled_frac,
    .flush = win32_flush,
    .sync_begin = win32_sync_begin,
    .sync_end = win32_sync_end,
    .set_title = win32_set_title,
    .link_begin = win32_link_begin,
    .link_end = win32_link_end,

    // Input
    .read_key = win32_read_key,
    .mouse_col = win32_get_last_mouse_col,
    .mouse_row = win32_get_last_mouse_row,
    .check_resize = win32_check_resize,
    .check_quit = win32_check_quit,
    .poll_jobs = win32_execute_pending_jobs,
    .input_ready = win32_input_available,
    .register_signals = win32_register_signals,

    // Clipboard
    .copy = win32_clipboard_copy,
    .paste = win32_clipboard_paste,

    // Filesystem
    .home_dir = win32_get_home_dir,
    .mkdir_p = win32_mkdir_p,
    .file_exists = win32_file_exists,
    .read_file = win32_read_file,
    .write_file = win32_write_file,
    .list_dir = win32_list_dir,
    .mtime = win32_get_mtime,
    .rm = win32_delete_file,
    .reveal = win32_reveal_in_explorer,

    // Time
    .clock = win32_clock,
    .sleep_ms = win32_sleep_ms,
    .localtime = win32_get_local_time,
    .localtime_from = win32_get_local_time_from,
    .username = win32_get_username,

    // Images
    .img_supported = win32_image_is_supported,
    .img_size = win32_image_get_size,
    .img_display = win32_image_display,
    .img_display_cropped = win32_image_display_cropped,
    .img_frame_start = win32_image_frame_start,
    .img_frame_end = win32_image_frame_end,
    .img_clear_all = win32_image_clear_all,
    .img_mask = win32_image_mask_region,
    .img_resolve = win32_image_resolve_path,
    .img_calc_rows = win32_image_calc_rows,
    .img_invalidate = win32_image_invalidate,
};

#endif // _WIN32
