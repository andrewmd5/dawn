// dawn_backend_posix.c - POSIX Backend

#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#else
#define _GNU_SOURCE
#endif

#include "dawn_backend.h"
#include "dawn_wrap.h"
#include "dawn_utils.h"

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include <curl/curl.h>

#ifdef __APPLE__
#include <ApplicationServices/ApplicationServices.h>
#endif

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

// Common terminal definitions
#include "dawn_term_common.h"

// Cache key buffer: path + ":" + mtime (INT64_MAX is 19 digits) + null
#define CACHE_KEY_SIZE (PATH_MAX + 21)

// Output buffer for batching terminal writes
static char* output_buf = NULL;
static size_t output_buf_pos = 0;

static struct {
    struct termios orig_termios;
    bool raw_mode;
    bool initialized;
    uint32_t capabilities;
    int32_t cols;
    int32_t rows;
    int32_t last_mouse_col;
    int32_t last_mouse_row;
    volatile sig_atomic_t resize_needed;
    volatile sig_atomic_t quit_requested;
    bool kitty_keyboard_enabled;
    DawnMode mode; //!< Interactive or print mode
    int32_t tty_fd; //!< File descriptor for terminal queries in print mode (-1 if not used)
    int32_t print_row; //!< Current output row in print mode (1-indexed)
    int32_t print_col; //!< Current output column in print mode (1-indexed)
    DawnColor* print_bg; //!< Default background for print mode margins (NULL if not set)
} posix_state = { 0 };

// Fast output buffer append
static inline void buf_flush(void)
{
    if (output_buf_pos > 0) {
        fwrite(output_buf, 1, output_buf_pos, stdout);
        output_buf_pos = 0;
    }
}

static inline void buf_append(const char* s, size_t len)
{
    if (output_buf_pos + len > OUTPUT_BUF_SIZE) {
        buf_flush();
        if (len > OUTPUT_BUF_SIZE) {
            // Too large for buffer, write directly
            fwrite(s, 1, len, stdout);
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

// Buffered printf-style output (slow path, avoid in hot loops)
static void buf_printf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
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

// Forward declarations
static inline void buf_bg(uint8_t r, uint8_t g, uint8_t b);
static void posix_install_shutdown_handlers(void);
static void posix_fire_shutdown_callbacks(void);

// Fast path: cursor positioning - \x1b[row;colH
static inline void buf_cursor(int32_t row, int32_t col)
{
    // In print mode, use streaming output (newlines) instead of absolute positioning
    if (posix_state.mode == DAWN_MODE_PRINT) {
        // Advance rows with newlines
        while (posix_state.print_row < row) {
            buf_append_char('\n');
            posix_state.print_row++;
            posix_state.print_col = 1;
        }
        // Position column with spaces (using default bg for margins)
        if (col > posix_state.print_col) {
            if (posix_state.print_bg) {
                buf_bg(posix_state.print_bg->r, posix_state.print_bg->g, posix_state.print_bg->b);
            }
            while (posix_state.print_col < col) {
                buf_append_char(' ');
                posix_state.print_col++;
            }
        } else if (col < posix_state.print_col) {
            // Need to go back - use carriage return then spaces
            buf_append_char('\r');
            posix_state.print_col = 1;
            if (posix_state.print_bg) {
                buf_bg(posix_state.print_bg->r, posix_state.print_bg->g, posix_state.print_bg->b);
            }
            while (posix_state.print_col < col) {
                buf_append_char(' ');
                posix_state.print_col++;
            }
        }
        return;
    }

    // Interactive mode: use absolute cursor positioning
    char seq[16];
    int32_t len = build_cursor_seq(seq, row, col);
    buf_append(seq, (size_t)len);
}

// Fast path: foreground color
static inline void buf_fg(uint8_t r, uint8_t g, uint8_t b)
{
    char seq[24];
    int32_t len = build_fg_seq(seq, r, g, b);
    buf_append(seq, (size_t)len);
}

// Fast path: background color
static inline void buf_bg(uint8_t r, uint8_t g, uint8_t b)
{
    char seq[24];
    int32_t len = build_bg_seq(seq, r, g, b);
    buf_append(seq, (size_t)len);
}

// Fast path: underline color
static inline void buf_underline_color(uint8_t r, uint8_t g, uint8_t b)
{
    char seq[24];
    int32_t len = build_underline_color_seq(seq, r, g, b);
    buf_append(seq, (size_t)len);
}

// Image cache for Kitty graphics protocol
static TransmittedImage transmitted_images[MAX_TRANSMITTED_IMAGES];
static int32_t transmitted_count = 0;
static uint32_t next_image_id = 1;

static int32_t posix_image_calc_rows(int32_t pixel_width, int32_t pixel_height, int32_t max_cols, int32_t max_rows);

static void handle_sigwinch(int32_t sig)
{
    (void)sig;
    posix_state.resize_needed = 1;
}

static void handle_sigterm(int32_t sig)
{
    (void)sig;
    posix_state.quit_requested = 1;
}

//! Get the file descriptor for terminal queries
//! In interactive mode: stdout/stdin
//! In print mode: tty_fd (stderr or /dev/tty)
static inline int32_t get_query_write_fd(void)
{
    if (posix_state.mode == DAWN_MODE_PRINT && posix_state.tty_fd >= 0) {
        return posix_state.tty_fd;
    }
    return STDOUT_FILENO;
}

static inline int32_t get_query_read_fd(void)
{
    if (posix_state.mode == DAWN_MODE_PRINT && posix_state.tty_fd >= 0) {
        return posix_state.tty_fd;
    }
    return STDIN_FILENO;
}

//! Write query to terminal (uses tty_fd in print mode)
static void query_write(const char* data, size_t len)
{
    int32_t fd = get_query_write_fd();
    ssize_t n = write(fd, data, len);
    (void)n;
    // Flush - fsync for tty, fflush for stdout
    if (fd == STDOUT_FILENO) {
        fflush(stdout);
    }
}

static void query_printf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
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
    int32_t fd = get_query_read_fd();
    fd_set fds;
    struct timeval tv = { 0, 1000 };
    char c;

    while (1) {
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        if (select(fd + 1, &fds, NULL, NULL, &tv) <= 0)
            break;
        if (read(fd, &c, 1) != 1)
            break;
    }
}

static size_t read_response(char* buf, size_t buf_size, char terminator, int32_t timeout_ms)
{
    int32_t fd = get_query_read_fd();
    size_t pos = 0;
    fd_set fds;
    struct timeval tv = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };

    while (pos < buf_size - 1) {
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        if (select(fd + 1, &fds, NULL, NULL, &tv) <= 0)
            break;

        char c;
        if (read(fd, &c, 1) != 1)
            break;
        buf[pos++] = c;

        if (c == terminator)
            break;
        if (pos >= 2 && buf[pos - 2] == '\x1b' && c == '\\')
            break;

        tv.tv_sec = 0;
        tv.tv_usec = 10000;
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

// Query terminal background color using OSC 11
static bool query_background_color(DawnColor* out)
{
    if (!out)
        return false;

    drain_input();

    // Query OSC 11 with ST terminator (like: printf '\033]11;?\033\\')
    query_write("\x1b]11;?\x1b\\", 8);

    // Response: \033]11;rgb:RRRR/GGGG/BBBB\033\\ or \007
    char buf[64];
    size_t len = read_response(buf, sizeof(buf), '\\', 100);

    if (len < 10)
        return false;

    // Find "rgb:" in response
    char* rgb = strstr(buf, "rgb:");
    if (!rgb)
        return false;
    rgb += 4;

    // Parse RRRR/GGGG/BBBB (16-bit hex values)
    uint32_t r, g, b;
    if (sscanf(rgb, "%x/%x/%x", &r, &g, &b) != 3)
        return false;

    // Convert 16-bit to 8-bit
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
    if (!term_parse_cpr(buf1, len1, &row1, &col1))
        return false;

    query_write(ESC "]66;w=2; " ESC "\\", sizeof(ESC "]66;w=2; " ESC "\\") - 1);

    query_write(CSI "6n", sizeof(CSI "6n") - 1);

    char buf2[32];
    size_t len2 = read_response(buf2, sizeof(buf2), 'R', 100);

    int32_t row2, col2;
    if (!term_parse_cpr(buf2, len2, &row2, &col2))
        return false;

    query_write(ESC "]66;s=2; " ESC "\\", sizeof(ESC "]66;s=2; " ESC "\\") - 1);

    query_write(CSI "6n", sizeof(CSI "6n") - 1);

    char buf3[32];
    size_t len3 = read_response(buf3, sizeof(buf3), 'R', 100);

    int32_t row3, col3;
    if (!term_parse_cpr(buf3, len3, &row3, &col3))
        return false;

    // Width support: col2 - col1 == 2
    // Scale support: col3 - col2 == 2
    // Both must be supported
    return (row1 == row2 && row2 == row3 && col2 - col1 == 2 && col3 - col2 == 2);
}

static void detect_capabilities(void)
{
    posix_state.capabilities = DAWN_CAP_NONE;

    const char* colorterm = getenv("COLORTERM");
    if (colorterm && (strcmp(colorterm, "truecolor") == 0 || strcmp(colorterm, "24bit") == 0)) {
        posix_state.capabilities |= DAWN_CAP_TRUE_COLOR;
    }

    if (query_mode_supported(2026)) {
        posix_state.capabilities |= DAWN_CAP_SYNC_OUTPUT;
    }

    if (query_mode_supported(2004)) {
        posix_state.capabilities |= DAWN_CAP_BRACKETED_PASTE;
    }

    if (query_kitty_keyboard()) {
        // Implies styled underlines too
        posix_state.capabilities |= DAWN_CAP_STYLED_UNDERLINE;
    }

    if (query_kitty_graphics()) {
        posix_state.capabilities |= DAWN_CAP_IMAGES;
    }

    if (query_text_sizing()) {
        posix_state.capabilities |= DAWN_CAP_TEXT_SIZING;
    }

    // Mouse and clipboard always available on POSIX
    posix_state.capabilities |= DAWN_CAP_MOUSE;
    posix_state.capabilities |= DAWN_CAP_CLIPBOARD;

    drain_input();
}

static bool posix_init(DawnMode mode)
{
    if (posix_state.initialized)
        return true;

    posix_state.mode = mode;
    posix_state.tty_fd = -1;

    posix_install_shutdown_handlers();

    // Allocate output buffer
    if (!output_buf) {
        output_buf = malloc(OUTPUT_BUF_SIZE);
        if (!output_buf)
            return false;
    }

    if (mode == DAWN_MODE_PRINT) {
        // Print mode: use stderr for terminal queries, stdout for output
        // Open /dev/tty for bidirectional terminal communication
        posix_state.tty_fd = open("/dev/tty", O_RDWR);
        if (posix_state.tty_fd < 0) {
            // Fall back to stderr if /dev/tty not available
            if (isatty(STDERR_FILENO)) {
                posix_state.tty_fd = STDERR_FILENO;
            }
        }

        // Set up raw mode on tty_fd for capability queries (temporary)
        if (posix_state.tty_fd >= 0) {
            struct termios tty_termios;
            if (tcgetattr(posix_state.tty_fd, &posix_state.orig_termios) == 0) {
                tty_termios = posix_state.orig_termios;
                tty_termios.c_lflag &= ~(ECHO | ICANON);
                tty_termios.c_cc[VMIN] = 0;
                tty_termios.c_cc[VTIME] = 1;
                tcsetattr(posix_state.tty_fd, TCSAFLUSH, &tty_termios);
                posix_state.raw_mode = true;
            }
        }

        // Query terminal background color via OSC 11
        DawnColor term_bg;
        if (query_background_color(&term_bg)) {
            posix_state.print_bg = malloc(sizeof(DawnColor));
            if (posix_state.print_bg) {
                *posix_state.print_bg = term_bg;
            }
        }

        // Detect capabilities via tty
        detect_capabilities();

        // Restore terminal settings after capability detection
        if (posix_state.tty_fd >= 0 && posix_state.raw_mode) {
            tcsetattr(posix_state.tty_fd, TCSAFLUSH, &posix_state.orig_termios);
            posix_state.raw_mode = false;
        }

        // Get terminal size from tty
        struct winsize ws;
        int32_t size_fd = (posix_state.tty_fd >= 0) ? posix_state.tty_fd : STDERR_FILENO;
        if (ioctl(size_fd, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
            posix_state.cols = ws.ws_col;
            posix_state.rows = ws.ws_row;
        } else {
            posix_state.cols = 80;
            posix_state.rows = 24;
        }

        // Initialize print position tracking (1-indexed)
        posix_state.print_row = 1;
        posix_state.print_col = 1;

        // No alternate screen, mouse, or keyboard setup in print mode
        posix_state.initialized = true;
        return true;
    }

    // Interactive mode: full terminal setup
    // Install signal handlers
    signal(SIGWINCH, handle_sigwinch);
    signal(SIGINT, handle_sigterm);
    signal(SIGTERM, handle_sigterm);

    // Enable raw mode
    if (tcgetattr(STDIN_FILENO, &posix_state.orig_termios) == -1) {
        return false;
    }

    struct termios raw = posix_state.orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= CS8;
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        return false;
    }
    posix_state.raw_mode = true;

    // Switch to alternate screen
    printf(ALT_SCREEN_ON);
    fflush(stdout);

    // Detect capabilities
    detect_capabilities();

    // Enable Kitty keyboard protocol if available
    if (posix_state.capabilities & DAWN_CAP_STYLED_UNDERLINE) {
        printf(KITTY_KBD_PUSH);
        posix_state.kitty_keyboard_enabled = true;
    }

    // Enable mouse and bracketed paste
    printf(MOUSE_ON BRACKETED_PASTE_ON);
    printf(CLEAR_SCREEN CURSOR_HOME);
    fflush(stdout);

    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        posix_state.cols = ws.ws_col;
        posix_state.rows = ws.ws_row;
    } else {
        posix_state.cols = 80;
        posix_state.rows = 24;
    }

    posix_state.initialized = true;
    return true;
}

static void posix_shutdown(void)
{
    if (!posix_state.initialized)
        return;

    posix_fire_shutdown_callbacks();

    if (posix_state.mode == DAWN_MODE_PRINT) {
        // Print mode cleanup: just close tty_fd if we opened it
        if (posix_state.tty_fd >= 0 && posix_state.tty_fd != STDERR_FILENO) {
            close(posix_state.tty_fd);
        }
        posix_state.tty_fd = -1;

        if (posix_state.print_bg) {
            free(posix_state.print_bg);
            posix_state.print_bg = NULL;
        }

        free(output_buf);
        output_buf = NULL;
        output_buf_pos = 0;

        posix_state.initialized = false;
        return;
    }

    // Interactive mode cleanup
    printf(ESC "_Ga=d,d=A,q=2" ESC "\\");

    if (posix_state.kitty_keyboard_enabled) {
        printf(KITTY_KBD_POP);
    }

    printf(SYNC_START CURSOR_SHOW MOUSE_OFF BRACKETED_PASTE_OFF ALT_SCREEN_OFF RESET SYNC_END);
    fflush(stdout);

    if (posix_state.raw_mode) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &posix_state.orig_termios);
        posix_state.raw_mode = false;
    }

    for (int32_t i = 0; i < transmitted_count; i++) {
        free(transmitted_images[i].path);
    }
    transmitted_count = 0;

    free(output_buf);
    output_buf = NULL;
    output_buf_pos = 0;

    posix_state.initialized = false;
}

static uint32_t posix_get_capabilities(void)
{
    return posix_state.capabilities;
}

static DawnColor* posix_get_host_bg(void)
{
    if (!posix_state.print_bg)
        return NULL;
    DawnColor* copy = malloc(sizeof(DawnColor));
    if (copy)
        *copy = *posix_state.print_bg;
    return copy;
}

static void posix_get_size(int32_t* out_cols, int32_t* out_rows)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        posix_state.cols = ws.ws_col;
        posix_state.rows = ws.ws_row;
    }
    if (out_cols)
        *out_cols = posix_state.cols;
    if (out_rows)
        *out_rows = posix_state.rows;
}

static void posix_set_cursor(int32_t col, int32_t row)
{
    buf_cursor(row, col);
}

static void posix_set_cursor_visible(bool visible)
{
    buf_append_str(visible ? CURSOR_SHOW : CURSOR_HIDE);
}

static void posix_set_fg(DawnColor color)
{
    buf_fg(color.r, color.g, color.b);
}

static void posix_set_bg(DawnColor color)
{
    // In print mode, skip setting bg if it matches the captured terminal bg
    // This lets the terminal's native background show through
    if (posix_state.mode == DAWN_MODE_PRINT && posix_state.print_bg) {
        if (color.r == posix_state.print_bg->r && color.g == posix_state.print_bg->g && color.b == posix_state.print_bg->b) {
            return;
        }
    }
    buf_bg(color.r, color.g, color.b);
}

static void posix_reset_attrs(void)
{
    buf_append_str(RESET);
}

static void posix_set_bold(bool enabled)
{
    buf_append_str(enabled ? BOLD : CSI "22m");
}

static void posix_set_italic(bool enabled)
{
    buf_append_str(enabled ? ITALIC : CSI "23m");
}

static void posix_set_dim(bool enabled)
{
    buf_append_str(enabled ? DIM : CSI "22m");
}

static void posix_set_strikethrough(bool enabled)
{
    buf_append_str(enabled ? STRIKETHROUGH : CSI "29m");
}

static void posix_set_underline(DawnUnderline style)
{
    if (posix_state.capabilities & DAWN_CAP_STYLED_UNDERLINE) {
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

static void posix_set_underline_color(DawnColor color)
{
    if (posix_state.capabilities & DAWN_CAP_STYLED_UNDERLINE) {
        buf_underline_color(color.r, color.g, color.b);
    }
}

static void posix_clear_underline(void)
{
    if (posix_state.capabilities & DAWN_CAP_STYLED_UNDERLINE) {
        buf_append_str(UNDERLINE_OFF);
    } else {
        buf_append_str(CSI "24m");
    }
}

static void posix_clear_screen(void)
{
    // No-op in print mode - we're streaming output
    if (posix_state.mode == DAWN_MODE_PRINT)
        return;
    buf_append_str(CLEAR_SCREEN CURSOR_HOME);
}

static void posix_clear_line(void)
{
    // No-op in print mode - we're streaming output
    if (posix_state.mode == DAWN_MODE_PRINT)
        return;
    buf_append_str(CLEAR_LINE);
}

static void posix_clear_range(int32_t count)
{
    // No-op in print mode or invalid count
    if (posix_state.mode == DAWN_MODE_PRINT || count <= 0)
        return;
    // ECH (Erase Character) - erases N chars at cursor using current bg
    char buf[16];
    snprintf(buf, sizeof(buf), CSI "%dX", count);
    buf_append_str(buf);
}

static void posix_write_str(const char* str, size_t len)
{
    buf_append(str, len);
    // Track column position in print mode
    if (posix_state.mode == DAWN_MODE_PRINT) {
        // Handle newlines specially, otherwise use display width
        const char* nl = memchr(str, '\n', len);
        if (nl) {
            // String contains newlines - process segments
            size_t pos = 0;
            while (pos < len) {
                const char* next_nl = memchr(str + pos, '\n', len - pos);
                if (next_nl) {
                    size_t seg_len = (size_t)(next_nl - (str + pos));
                    if (seg_len > 0) {
                        posix_state.print_col += utf8_display_width(str + pos, seg_len);
                    }
                    posix_state.print_row++;
                    posix_state.print_col = 1;
                    pos = (size_t)(next_nl - str) + 1;
                } else {
                    posix_state.print_col += utf8_display_width(str + pos, len - pos);
                    break;
                }
            }
        } else {
            posix_state.print_col += utf8_display_width(str, len);
        }
    }
}

static void posix_write_char(char c)
{
    buf_append_char(c);
    // Track column position in print mode
    if (posix_state.mode == DAWN_MODE_PRINT) {
        if (c == '\n') {
            posix_state.print_row++;
            posix_state.print_col = 1;
        } else {
            posix_state.print_col++;
        }
    }
}

static void posix_repeat_char(char c, int32_t n)
{
    if (n <= 0)
        return;
    buf_append_char(c);
    if (n > 1) {
        // REP sequence: CSI n b - repeat previous char n times
        char seq[16];
        seq[0] = '\x1b';
        seq[1] = '[';
        int32_t pos = 2;
        pos += format_num(seq + pos, n - 1);
        seq[pos++] = 'b';
        buf_append(seq, (size_t)pos);
    }
    if (posix_state.mode == DAWN_MODE_PRINT) {
        posix_state.print_col += n;
    }
}

static void posix_write_scaled(const char* str, size_t len, int32_t scale)
{
    if (scale <= 1 || !(posix_state.capabilities & DAWN_CAP_TEXT_SIZING)) {
        buf_append(str, len);
        if (posix_state.mode == DAWN_MODE_PRINT) {
            posix_state.print_col += utf8_display_width(str, len);
        }
        return;
    }
    if (scale > 7)
        scale = 7;
    buf_printf(TEXT_SIZE_OSC "s=%d;%.*s" TEXT_SIZE_ST, scale, (int32_t)len, str);
    // Track column position (scaled text takes more columns)
    if (posix_state.mode == DAWN_MODE_PRINT) {
        posix_state.print_col += utf8_display_width(str, len) * scale;
    }
}

static void posix_write_scaled_frac(const char* str, size_t len, int32_t scale, int32_t num, int32_t denom)
{
    if (!(posix_state.capabilities & DAWN_CAP_TEXT_SIZING)) {
        buf_append(str, len);
        if (posix_state.mode == DAWN_MODE_PRINT) {
            posix_state.print_col += utf8_display_width(str, len);
        }
        return;
    }
    // Clamp values to valid ranges
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

    // No fractional scaling - use simple scaled output
    if (num == 0 || denom == 0 || num >= denom) {
        if (scale <= 1) {
            buf_append(str, len);
            if (posix_state.mode == DAWN_MODE_PRINT) {
                posix_state.print_col += utf8_display_width(str, len);
            }
        } else {
            buf_printf(TEXT_SIZE_OSC "s=%d;%.*s" TEXT_SIZE_ST, scale, (int32_t)len, str);
            if (posix_state.mode == DAWN_MODE_PRINT) {
                posix_state.print_col += utf8_display_width(str, len) * scale;
            }
        }
        return;
    }

    // Fractional scaling: s=scale:n=num:d=denom
    buf_printf(TEXT_SIZE_OSC "s=%d:n=%d:d=%d;%.*s" TEXT_SIZE_ST,
        scale, num, denom, (int32_t)len, str);
    if (posix_state.mode == DAWN_MODE_PRINT) {
        // Approximate column width for fractional scaling
        posix_state.print_col += utf8_display_width(str, len) * scale;
    }
}

static void posix_flush(void)
{
    buf_flush();
    fflush(stdout);
}

static void posix_sync_begin(void)
{
    if (posix_state.capabilities & DAWN_CAP_SYNC_OUTPUT) {
        buf_append_str(SYNC_START);
    }
}

static void posix_sync_end(void)
{
    if (posix_state.capabilities & DAWN_CAP_SYNC_OUTPUT) {
        buf_append_str(SYNC_END);
    }
}

static void posix_set_title(const char* title)
{
    // OSC 0 sets window title: ESC ] 0 ; title BEL
    if (title && *title) {
        buf_append_str("\x1b]0;");
        buf_append_str(title);
        buf_append_char('\x07'); // BEL terminator
    } else {
        // Reset to empty/default
        buf_append_str("\x1b]0;\x07");
    }
}

static void posix_link_begin(const char* url)
{
    // OSC 8 hyperlink: ESC ] 8 ; ; url ST
    if (url && *url) {
        buf_append_str("\x1b]8;;");
        buf_append_str(url);
        buf_append_str("\x1b\\");
    }
}

static void posix_link_end(void)
{
    // Close OSC 8 hyperlink
    buf_append_str("\x1b]8;;\x1b\\");
}

static void drain_escape_sequence(void)
{
    char c;
    struct termios t;
    tcgetattr(STDIN_FILENO, &t);
    t.c_cc[VTIME] = 0;
    t.c_cc[VMIN] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &t);

    while (read(STDIN_FILENO, &c, 1) == 1) {
    }

    t.c_cc[VTIME] = 1;
    tcsetattr(STDIN_FILENO, TCSANOW, &t);
}

static int32_t posix_read_key(void)
{
    char c;
    if (read(STDIN_FILENO, &c, 1) <= 0)
        return DAWN_KEY_NONE;

    if (c == '\x1b') {
        char seq[8];
        struct termios t;
        tcgetattr(STDIN_FILENO, &t);
        cc_t old_vtime = t.c_cc[VTIME];
        t.c_cc[VTIME] = 0;
        t.c_cc[VMIN] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &t);

        ssize_t nread = read(STDIN_FILENO, &seq[0], 1);

        if (nread != 1) {
            t.c_cc[VTIME] = old_vtime;
            tcsetattr(STDIN_FILENO, TCSANOW, &t);
            return '\x1b';
        }

        nread = read(STDIN_FILENO, &seq[1], 1);
        t.c_cc[VTIME] = old_vtime;
        tcsetattr(STDIN_FILENO, TCSANOW, &t);

        if (nread != 1) {
            drain_escape_sequence();
            return DAWN_KEY_NONE;
        }

        if (seq[0] == '[') {
            // SGR mouse events
            if (seq[1] == '<') {
                char mouse_buf[32];
                int32_t mi = 0;

                while (mi < 30) {
                    if (read(STDIN_FILENO, &mouse_buf[mi], 1) != 1)
                        break;
                    if (mouse_buf[mi] == 'M' || mouse_buf[mi] == 'm') {
                        mouse_buf[mi + 1] = '\0';
                        break;
                    }
                    mi++;
                }
                int32_t btn = 0, mx = 0, my = 0;
                if (sscanf(mouse_buf, "%d;%d;%d", &btn, &mx, &my) == 3) {
                    posix_state.last_mouse_col = mx;
                    posix_state.last_mouse_row = my;
                    if (btn == 64)
                        return DAWN_KEY_MOUSE_SCROLL_UP;
                    if (btn == 65)
                        return DAWN_KEY_MOUSE_SCROLL_DOWN;
                    // Left button click (btn 0 = press, check for 'M' terminator)
                    if (btn == 0)
                        return DAWN_KEY_MOUSE_CLICK;
                }
                return DAWN_KEY_NONE;
            }

            // Kitty keyboard protocol or legacy sequences
            if (seq[1] >= '0' && seq[1] <= '9') {
                char peek[32];
                peek[0] = seq[1];
                int32_t pi = 1;
                bool is_kitty = false;

                struct termios t2;
                tcgetattr(STDIN_FILENO, &t2);
                cc_t old = t2.c_cc[VTIME];
                t2.c_cc[VTIME] = 0;
                t2.c_cc[VMIN] = 0;
                tcsetattr(STDIN_FILENO, TCSANOW, &t2);

                while (pi < 30) {
                    if (read(STDIN_FILENO, &peek[pi], 1) != 1)
                        break;
                    if (peek[pi] == 'u') {
                        is_kitty = true;
                        peek[pi + 1] = '\0';
                        break;
                    }
                    if (peek[pi] == '~' || peek[pi] == 'A' || peek[pi] == 'B' || peek[pi] == 'C' || peek[pi] == 'D' || peek[pi] == 'H' || peek[pi] == 'F' || peek[pi] == 'M' || peek[pi] == 'm') {
                        peek[pi + 1] = '\0';
                        break;
                    }
                    pi++;
                }

                t2.c_cc[VTIME] = old;
                tcsetattr(STDIN_FILENO, TCSANOW, &t2);

                if (is_kitty) {
                    int32_t keycode = 0, mods = 1;
                    char term = 0;
                    if (sscanf(peek, "%d;%d%c", &keycode, &mods, &term) >= 2 || sscanf(peek, "%d%c", &keycode, &term) >= 1) {

                        bool shift = (mods - 1) & 1;
                        bool alt = (mods - 1) & 2;
                        bool ctrl = (mods - 1) & 4;

                        switch (keycode) {
                        case 57352: // Up
                            if (shift)
                                return DAWN_KEY_SHIFT_UP;
                            return DAWN_KEY_UP;
                        case 57353: // Down
                            if (shift)
                                return DAWN_KEY_SHIFT_DOWN;
                            return DAWN_KEY_DOWN;
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
                        case 57360: // Home
                            if (ctrl)
                                return DAWN_KEY_CTRL_HOME;
                            return DAWN_KEY_HOME;
                        case 57367: // End
                            if (ctrl)
                                return DAWN_KEY_CTRL_END;
                            return DAWN_KEY_END;
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

                        // Handle printable characters (ASCII and Unicode)
                        if (keycode >= 32 && keycode != 127) {
                            if (ctrl && keycode == '/') {
                                return 31; // Ctrl+/
                            }
                            if (ctrl && keycode >= 'a' && keycode <= 'z') {
                                return keycode - 'a' + 1;
                            }
                            if (ctrl && keycode >= 'A' && keycode <= 'Z') {
                                return keycode - 'A' + 1;
                            }
                            return keycode;
                        }
                    }
                    return DAWN_KEY_NONE;
                }

                // Legacy sequence
                if (peek[pi] == '~') {
                    int32_t num = 0;
                    sscanf(peek, "%d", &num);
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

                int32_t num1 = 0, num2 = 0;
                char termchar = 0;
                if (sscanf(peek, "%d;%d%c", &num1, &num2, &termchar) == 3) {
                    bool shift = (num2 == 2 || num2 == 4 || num2 == 6 || num2 == 8 || num2 == 10 || num2 == 12 || num2 == 14 || num2 == 16);
                    bool ctrl = (num2 == 5 || num2 == 6 || num2 == 7 || num2 == 8 || num2 == 13 || num2 == 14 || num2 == 15 || num2 == 16);
                    bool alt = (num2 == 3 || num2 == 4 || num2 == 7 || num2 == 8 || num2 == 11 || num2 == 12 || num2 == 15 || num2 == 16);

                    switch (termchar) {
                    case 'A': // Up
                        if (shift)
                            return DAWN_KEY_SHIFT_UP;
                        return DAWN_KEY_UP;
                    case 'B': // Down
                        if (shift)
                            return DAWN_KEY_SHIFT_DOWN;
                        return DAWN_KEY_DOWN;
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
                    case 'H': // Home
                        if (ctrl)
                            return DAWN_KEY_CTRL_HOME;
                        return DAWN_KEY_HOME;
                    case 'F': // End
                        if (ctrl)
                            return DAWN_KEY_CTRL_END;
                        return DAWN_KEY_END;
                    }
                }
                return DAWN_KEY_NONE;
            }

            switch (seq[1]) {
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
            drain_escape_sequence();
            return DAWN_KEY_NONE;
        } else if (seq[0] == 'O') {
            switch (seq[1]) {
            case 'H':
                return DAWN_KEY_HOME;
            case 'F':
                return DAWN_KEY_END;
            }
            return DAWN_KEY_NONE;
        } else if (seq[0] == 'b') {
            return DAWN_KEY_ALT_LEFT;
        } else if (seq[0] == 'f') {
            return DAWN_KEY_ALT_RIGHT;
        }
        return DAWN_KEY_NONE;
    }

    uint8_t first = (uint8_t)c;
    if (first < 0x80) {
        return first;
    }
    int32_t expected = utf8proc_utf8class[first];
    if (expected < 2 || expected > 4) {
        return DAWN_KEY_NONE;
    }
    uint8_t buf[4];
    buf[0] = first;
    for (int32_t i = 1; i < expected; i++) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1) {
            return DAWN_KEY_NONE;
        }
        if ((buf[i] & 0xC0) != 0x80) {
            return DAWN_KEY_NONE;
        }
    }
    utf8proc_int32_t codepoint;
    if (utf8proc_iterate(buf, expected, &codepoint) != expected) {
        return DAWN_KEY_NONE;
    }

    return codepoint;
}

static int32_t posix_get_last_mouse_col(void)
{
    return posix_state.last_mouse_col;
}

static int32_t posix_get_last_mouse_row(void)
{
    return posix_state.last_mouse_row;
}

static bool posix_check_resize(void)
{
    if (posix_state.resize_needed) {
        posix_state.resize_needed = 0;
        return true;
    }
    return false;
}

static bool posix_check_quit(void)
{
    return posix_state.quit_requested;
}

static bool posix_input_available(float timeout_ms)
{
    struct timeval tv;
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);

    if (timeout_ms < 0) {
        // Block forever
        return select(STDIN_FILENO + 1, &fds, NULL, NULL, NULL) > 0;
    } else {
        // Convert float ms to seconds + microseconds
        long total_us = (long)(timeout_ms * 1000.0f);
        tv.tv_sec = total_us / 1000000;
        tv.tv_usec = total_us % 1000000;
        return select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0;
    }
}

static void (*user_resize_callback)(int32_t) = NULL;
static void (*user_quit_callback)(int32_t) = NULL;

static void posix_sigwinch_handler(int32_t sig)
{
    posix_state.resize_needed = 1;
    if (user_resize_callback) {
        user_resize_callback(sig);
    }
}

static void posix_sigquit_handler(int32_t sig)
{
    posix_state.quit_requested = 1;
    if (user_quit_callback) {
        user_quit_callback(sig);
    }
}

static void posix_register_signals(void (*on_resize)(int32_t), void (*on_quit)(int32_t))
{
    user_resize_callback = on_resize;
    user_quit_callback = on_quit;

    signal(SIGWINCH, posix_sigwinch_handler);
    signal(SIGINT, posix_sigquit_handler);
    signal(SIGTERM, posix_sigquit_handler);
}

#ifdef __APPLE__
#define UTF8_PLAIN_TEXT_TYPE CFSTR("public.utf8-plain-text")
#define PLAIN_TEXT_TYPE CFSTR("com.apple.traditional-mac-plain-text")

static void posix_clipboard_copy(const char* text, size_t len)
{
    PasteboardRef pasteboard;
    if (PasteboardCreate(kPasteboardClipboard, &pasteboard) != noErr) {
        return;
    }

    PasteboardClear(pasteboard);

    CFDataRef data = CFDataCreate(kCFAllocatorDefault, (const UInt8*)text, (CFIndex)len);
    if (data) {
        PasteboardPutItemFlavor(pasteboard, (PasteboardItemID)1, UTF8_PLAIN_TEXT_TYPE, data, 0);
        CFRelease(data);
    }

    CFRelease(pasteboard);
}

static char* posix_clipboard_paste(size_t* out_len)
{
    PasteboardRef pasteboard;
    *out_len = 0;

    if (PasteboardCreate(kPasteboardClipboard, &pasteboard) != noErr) {
        return NULL;
    }

    PasteboardSynchronize(pasteboard);

    ItemCount item_count;
    if (PasteboardGetItemCount(pasteboard, &item_count) != noErr || item_count < 1) {
        CFRelease(pasteboard);
        return NULL;
    }

    PasteboardItemID item_id;
    if (PasteboardGetItemIdentifier(pasteboard, 1, &item_id) != noErr) {
        CFRelease(pasteboard);
        return NULL;
    }

    CFDataRef clipboard_data;
    OSStatus status = PasteboardCopyItemFlavorData(pasteboard, item_id, UTF8_PLAIN_TEXT_TYPE, &clipboard_data);
    if (status != noErr) {
        status = PasteboardCopyItemFlavorData(pasteboard, item_id, PLAIN_TEXT_TYPE, &clipboard_data);
        if (status != noErr) {
            CFRelease(pasteboard);
            return NULL;
        }
    }

    char* result = NULL;
    CFStringRef clipboard_string = CFStringCreateFromExternalRepresentation(
        kCFAllocatorDefault, clipboard_data, kCFStringEncodingUTF8);
    if (clipboard_string) {
        CFIndex string_length = CFStringGetLength(clipboard_string);
        CFIndex max_size = CFStringGetMaximumSizeForEncoding(string_length, kCFStringEncodingUTF8) + 1;

        result = malloc((size_t)max_size);
        if (result) {
            if (CFStringGetCString(clipboard_string, result, max_size, kCFStringEncodingUTF8)) {
                *out_len = strlen(result);
            } else {
                free(result);
                result = NULL;
            }
        }
        CFRelease(clipboard_string);
    }

    CFRelease(clipboard_data);
    CFRelease(pasteboard);

    return result;
}

#else
// Linux fallback using xclip/xsel
static void posix_clipboard_copy(const char* text, size_t len)
{
    FILE* p = popen("xclip -selection clipboard 2>/dev/null || xsel --clipboard 2>/dev/null", "w");
    if (p) {
        fwrite(text, 1, len, p);
        pclose(p);
    }
}

static char* posix_clipboard_paste(size_t* out_len)
{
    *out_len = 0;
    FILE* p = popen("xclip -selection clipboard -o 2>/dev/null || xsel --clipboard -o 2>/dev/null", "r");
    if (!p)
        return NULL;

    char* result = NULL;
    size_t cap = 0;
    size_t len = 0;
    char buf[1024];

    while (fgets(buf, sizeof(buf), p)) {
        size_t chunk = strlen(buf);
        if (len + chunk >= cap) {
            cap = (len + chunk) * 2 + 1;
            result = realloc(result, cap);
        }
        memcpy(result + len, buf, chunk);
        len += chunk;
    }
    pclose(p);

    if (result) {
        result[len] = '\0';
        *out_len = len;
    }
    return result;
}
#endif

static const char* posix_get_home_dir(void)
{
    const char* home = getenv("HOME");
    if (home)
        return home;

    struct passwd* pw = getpwuid(getuid());
    return pw ? pw->pw_dir : NULL;
}

static bool posix_mkdir_p(const char* path)
{
    char tmp[512];
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    for (char* p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                return false;
            }
            *p = '/';
        }
    }
    return mkdir(tmp, 0755) == 0 || errno == EEXIST;
}

static bool posix_file_exists(const char* path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static char* posix_read_file(const char* path, size_t* out_len)
{
    *out_len = 0;
    FILE* f = fopen(path, "rb");
    if (!f)
        return NULL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size < 0 || size > 100 * 1024 * 1024) { // 100MB limit
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

static bool posix_write_file(const char* path, const char* data, size_t len)
{
    FILE* f = fopen(path, "wb");
    if (!f)
        return false;

    size_t written = fwrite(data, 1, len, f);
    fclose(f);

    return written == len;
}

static bool posix_list_dir(const char* path, char*** out_names, int32_t* out_count)
{
    *out_names = NULL;
    *out_count = 0;

    DIR* d = opendir(path);
    if (!d)
        return false;

    int32_t cap = 64;
    char** names = malloc(sizeof(char*) * (size_t)cap);
    int32_t count = 0;

    struct dirent* e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.')
            continue;

        if (count >= cap) {
            cap *= 2;
            names = realloc(names, sizeof(char*) * (size_t)cap);
        }
        names[count++] = dawn_strdup(e->d_name);
    }
    closedir(d);

    *out_names = names;
    *out_count = count;
    return true;
}

static int64_t posix_get_mtime(const char* path)
{
    struct stat st;
    if (stat(path, &st) != 0)
        return 0;
    return (int64_t)st.st_mtime;
}

static bool posix_delete_file(const char* path)
{
    return unlink(path) == 0;
}

static void posix_reveal_in_finder(const char* path)
{
#ifdef __APPLE__
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "open -R '%s' 2>/dev/null", path);
    int32_t r = system(cmd);
    (void)r;
#else
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "xdg-open '%s' 2>/dev/null &", path);
    int32_t r = system(cmd);
    (void)r;
#endif
}

static int64_t posix_clock(DawnClock kind)
{
    if (kind == DAWN_CLOCK_MS) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    }
    return (int64_t)time(NULL);
}

static void posix_sleep_ms(int32_t ms)
{
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

static void posix_get_local_time(DawnTime* out)
{
    if (!out)
        return;
    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    out->year = t->tm_year + 1900;
    out->mon = t->tm_mon;
    out->mday = t->tm_mday;
    out->hour = t->tm_hour;
    out->min = t->tm_min;
    out->sec = t->tm_sec;
    out->wday = t->tm_wday;
}

static void posix_get_local_time_from(DawnTime* out, int64_t timestamp)
{
    if (!out)
        return;
    time_t ts = (time_t)timestamp;
    struct tm* t = localtime(&ts);
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

static const char* posix_get_username(void)
{
    static char name[256];

    struct passwd* pw = getpwuid(getuid());
    if (pw && pw->pw_gecos && pw->pw_gecos[0]) {
        strncpy(name, pw->pw_gecos, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
        char* comma = strchr(name, ',');
        if (comma)
            *comma = '\0';
        if (name[0])
            return name;
    }

    if (pw && pw->pw_name) {
        strncpy(name, pw->pw_name, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
        return name;
    }

    const char* user = getenv("USER");
    return user ? user : "Unknown";
}

static bool posix_image_get_size(const char* path, int32_t* out_width, int32_t* out_height)
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
    int64_t current_mtime = posix_get_mtime(path);
    for (int32_t i = 0; i < transmitted_count; i++) {
        if (transmitted_images[i].path && strcmp(transmitted_images[i].path, path) == 0 && transmitted_images[i].mtime == current_mtime) {
            return &transmitted_images[i];
        }
    }
    return NULL;
}

static uint32_t transmit_to_terminal(const char* path)
{
    // Resolve to absolute path for file-based transmission
    char abs_path[PATH_MAX];
    char* resolved = realpath(path, abs_path);
    (void)resolved;
    assert(resolved && "realpath failed for image path");

    // Base64 encode the file path for transmission
    size_t path_len = strlen(abs_path);
    size_t b64_len;
    char* b64_path = term_base64_encode((uint8_t*)abs_path, path_len, &b64_len);
    if (!b64_path)
        return 0;

    uint32_t image_id = next_image_id++;

    // Use file-based transmission (t=f) - terminal reads directly from file
    // f=100 means PNG but terminals auto-detect actual format from file magic bytes
    buf_printf("\x1b_Ga=t,t=f,f=100,i=%u,q=2;%s\x1b\\", image_id, b64_path);

    free(b64_path);

    // Cache
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
    entry->path = dawn_strdup(path);
    entry->image_id = image_id;
    entry->mtime = posix_get_mtime(path);

    return image_id;
}

static uint32_t ensure_transmitted(const char* path)
{
    TransmittedImage* t = find_transmitted(path);
    if (t)
        return t->image_id;
    return transmit_to_terminal(path);
}

static int32_t posix_image_display(const char* path, int32_t row, int32_t col, int32_t max_cols, int32_t max_rows)
{
    if (!path)
        return 0;
    (void)row;
    (void)col; // Position set by caller

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
        // Calculate actual rows from image dimensions
        int32_t pixel_w, pixel_h;
        if (posix_image_get_size(path, &pixel_w, &pixel_h)) {
            rows_used = posix_image_calc_rows(pixel_w, pixel_h, max_cols, 0);
        } else {
            rows_used = 1;
        }
    }

    // In print mode, update position tracking after image display
    if (posix_state.mode == DAWN_MODE_PRINT) {
        posix_state.print_row += rows_used;
        posix_state.print_col = 1;
    }

    return rows_used;
}

static int32_t posix_image_display_cropped(const char* path, int32_t row, int32_t col, int32_t max_cols,
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
    if (!posix_image_get_size(path, &pixel_w, &pixel_h)) {
        return posix_image_display(path, row, col, max_cols, visible_rows);
    }

    int32_t img_rows = posix_image_calc_rows(pixel_w, pixel_h, max_cols, 0);
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

    // In print mode, update position tracking after image display
    if (posix_state.mode == DAWN_MODE_PRINT) {
        posix_state.print_row += visible_rows;
        posix_state.print_col = 1;
    }

    return visible_rows;
}

static void posix_image_frame_start(void)
{
    buf_append_str("\x1b_Ga=d,d=a,q=2\x1b\\");
}

static void posix_image_frame_end(void)
{
    // buf_flush();
    // fflush(stdout);
}

static void posix_image_clear_all(void)
{
    buf_append_str("\x1b_Ga=d,d=A,q=2\x1b\\");
    buf_flush();
    fflush(stdout);
    for (int32_t i = 0; i < transmitted_count; i++) {
        free(transmitted_images[i].path);
    }
    transmitted_count = 0;
}

static void posix_image_mask_region(int32_t col, int32_t row, int32_t cols, int32_t rows, DawnColor bg)
{
    if (cols <= 0 || rows <= 0)
        return;

    uint8_t pixel[4] = { bg.r, bg.g, bg.b, 255 };

    size_t b64_len;
    char* b64_data = term_base64_encode(pixel, 4, &b64_len);
    if (!b64_data)
        return;

    buf_printf(CSI "%d;%dH", row, col);
    buf_printf("\x1b_Ga=T,f=32,s=1,v=1,c=%d,r=%d,z=-1,q=2;%s\x1b\\", cols, rows, b64_data);

    free(b64_data);
}

// Async image download system
#define MAX_DOWNLOADS 8
#define MAX_FAILED_URLS 32

typedef struct {
    char* url;
    char* temp_path;
    char* final_path;
    FILE* fp;
    CURL* easy;
} AsyncDownload;

static CURLM* curl_multi_handle = NULL;
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
    failed_urls[failed_url_count++] = dawn_strdup(url);
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
        char* data = posix_read_file(temp_path, &len);
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

static void finalize_download(AsyncDownload* dl, bool success)
{
    if (success && dl->temp_path && dl->final_path) {
        if (!convert_downloaded_to_png(dl->temp_path, dl->final_path, dl->url))
            mark_url_failed(dl->url);
    } else if (dl->url) {
        mark_url_failed(dl->url);
    }

    if (dl->temp_path) {
        unlink(dl->temp_path);
        free(dl->temp_path);
    }
    free(dl->final_path);
    free(dl->url);
    memset(dl, 0, sizeof(*dl));
}

static void poll_downloads(void)
{
    if (!curl_multi_handle || download_count == 0)
        return;

    int32_t running;
    curl_multi_perform(curl_multi_handle, &running);

    CURLMsg* msg;
    int32_t msgs_left;
    while ((msg = curl_multi_info_read(curl_multi_handle, &msgs_left))) {
        if (msg->msg == CURLMSG_DONE) {
            CURL* easy = msg->easy_handle;
            bool success = (msg->data.result == CURLE_OK);

            for (int32_t i = 0; i < download_count; i++) {
                if (downloads[i].easy == easy) {
                    if (downloads[i].fp) {
                        fclose(downloads[i].fp);
                        downloads[i].fp = NULL;
                    }
                    curl_multi_remove_handle(curl_multi_handle, easy);
                    curl_easy_cleanup(easy);
                    finalize_download(&downloads[i], success);
                    memmove(&downloads[i], &downloads[i + 1], sizeof(AsyncDownload) * (download_count - i - 1));
                    download_count--;
                    break;
                }
            }
        }
    }
}

static bool start_async_download(const char* url, const char* temp_path, const char* final_path)
{
    if (download_count >= MAX_DOWNLOADS)
        return false;

    if (!curl_multi_handle) {
        curl_multi_handle = curl_multi_init();
        if (!curl_multi_handle)
            return false;
    }

    FILE* fp = fopen(temp_path, "wb");
    if (!fp)
        return false;

    CURL* easy = curl_easy_init();
    if (!easy) {
        fclose(fp);
        return false;
    }

    curl_easy_setopt(easy, CURLOPT_URL, url);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(easy, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(easy, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(easy, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(easy, CURLOPT_USERAGENT, "Dawn/1.0");

    curl_multi_add_handle(curl_multi_handle, easy);

    AsyncDownload* dl = &downloads[download_count++];
    dl->url = dawn_strdup(url);
    dl->temp_path = dawn_strdup(temp_path);
    dl->final_path = dawn_strdup(final_path);
    dl->fp = fp;
    dl->easy = easy;

    return true;
}

static bool download_url_to_cache(const char* url, char* cached_path, size_t path_size)
{
    if (!url || !cached_path)
        return false;

    if (is_failed_url(url))
        return false;

    const char* home = posix_get_home_dir();
    if (!home)
        return false;

    char cache_dir[512];
    snprintf(cache_dir, sizeof(cache_dir), "%s/.dawn/image-cache", home);
    posix_mkdir_p(cache_dir);

    char hash_hex[17];
    term_hash_to_hex(url, hash_hex);

    snprintf(cached_path, path_size, "%s/%.16s.png", cache_dir, hash_hex);

    // Already cached?
    if (posix_file_exists(cached_path)) {
        int32_t w, h, channels;
        if (stbi_info(cached_path, &w, &h, &channels))
            return true;
        unlink(cached_path);
    }

    // Already downloading?
    if (is_download_in_progress(url))
        return false;

    // Start async download
    char temp_path[1024];
    snprintf(temp_path, sizeof(temp_path), "%s/%.16s.tmp", cache_dir, hash_hex);
    start_async_download(url, temp_path, cached_path);

    return false;
}

// Check if file is already PNG by checking magic bytes
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
    // PNG magic: 89 50 4E 47 0D 0A 1A 0A
    return header[0] == 0x89 && header[1] == 0x50 && header[2] == 0x4E && header[3] == 0x47 && header[4] == 0x0D && header[5] == 0x0A && header[6] == 0x1A && header[7] == 0x0A;
}

// Convert a local image file to PNG in cache if not already PNG
static bool ensure_png_cached(const char* src_path, char* out, size_t out_size)
{
    assert(src_path && out && out_size > 0);

    // If already PNG, just return the original path
    if (is_png_file(src_path)) {
        strncpy(out, src_path, out_size - 1);
        out[out_size - 1] = '\0';
        return true;
    }

    // Need to convert - get cache directory
    const char* home = posix_get_home_dir();
    assert(home && "Failed to get home directory");

    char cache_dir[512];
    snprintf(cache_dir, sizeof(cache_dir), "%s/.dawn/image-cache", home);
    posix_mkdir_p(cache_dir);

    // Use absolute path + mtime as cache key
    char abs_path[PATH_MAX];
    char* resolved = realpath(src_path, abs_path);
    (void)resolved;
    assert(resolved && "realpath failed for image path");

    int64_t mtime = posix_get_mtime(abs_path);

    char key[CACHE_KEY_SIZE];
    snprintf(key, sizeof(key), "%s:%lld", abs_path, (long long)mtime);

    char hash_hex[17];
    term_hash_to_hex(key, hash_hex);

    snprintf(out, out_size, "%s/%.16s.png", cache_dir, hash_hex);

    // Check if cached PNG already exists
    if (posix_file_exists(out)) {
        return true;
    }

    // Handle SVG files via dawn_svg
    if (svg_is_svg_file(abs_path)) {
        size_t svg_len;
        char* svg_data = posix_read_file(abs_path, &svg_len);
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

    // Load raster image and convert to PNG
    int32_t w, h, channels;
    uint8_t* pixels = stbi_load(src_path, &w, &h, &channels, 4);
    if (!pixels) {
        return false;
    }

    int32_t write_ok = stbi_write_png(out, w, h, 4, pixels, w * 4);
    stbi_image_free(pixels);

    return write_ok != 0;
}

static bool posix_image_resolve_path(const char* raw_path, const char* base_dir,
    char* out, size_t out_size)
{
    if (!raw_path || !out || out_size == 0)
        return false;

    if (term_is_remote_url(raw_path)) {
        return download_url_to_cache(raw_path, out, out_size);
    }

    char resolved[PATH_MAX];

    // Absolute path
    if (raw_path[0] == '/') {
        if (posix_file_exists(raw_path)) {
            return ensure_png_cached(raw_path, out, out_size);
        }
        return false;
    }

    // Home directory expansion
    if (raw_path[0] == '~') {
        const char* home = posix_get_home_dir();
        if (home) {
            snprintf(resolved, sizeof(resolved), "%s%s", home, raw_path + 1);
            if (posix_file_exists(resolved)) {
                return ensure_png_cached(resolved, out, out_size);
            }
        }
        return false;
    }

    // Relative path - try base_dir
    if (base_dir && base_dir[0]) {
        snprintf(resolved, sizeof(resolved), "%s/%s", base_dir, raw_path);
        if (posix_file_exists(resolved)) {
            return ensure_png_cached(resolved, out, out_size);
        }
    }

    // Try as-is
    if (posix_file_exists(raw_path)) {
        return ensure_png_cached(raw_path, out, out_size);
    }

    return false;
}

static int32_t posix_image_calc_rows(int32_t pixel_width, int32_t pixel_height, int32_t max_cols, int32_t max_rows)
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

static void posix_image_invalidate(const char* path)
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
    fflush(stdout);
}

static void posix_execute_pending_jobs(void)
{
    poll_downloads();
}

// Shutdown callback system
#define MAX_SHUTDOWN_CALLBACKS 8
static void (*shutdown_callbacks[MAX_SHUTDOWN_CALLBACKS])(void);
static int32_t shutdown_callback_count = 0;

static void posix_on_shutdown(void (*callback)(void))
{
    if (callback && shutdown_callback_count < MAX_SHUTDOWN_CALLBACKS) {
        shutdown_callbacks[shutdown_callback_count++] = callback;
    }
}

static void posix_fire_shutdown_callbacks(void)
{
    for (int32_t i = 0; i < shutdown_callback_count; i++) {
        if (shutdown_callbacks[i])
            shutdown_callbacks[i]();
    }
}

static void posix_shutdown_signal_handler(int32_t sig)
{
    posix_fire_shutdown_callbacks();
    signal(sig, SIG_DFL);
    raise(sig);
}

static void posix_install_shutdown_handlers(void)
{
    static bool installed = false;
    if (installed)
        return;
    installed = true;

    struct sigaction sa;
    sa.sa_handler = posix_shutdown_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESETHAND;

    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
}

const DawnBackend dawn_backend_posix = {
    .name = "posix",

    // Lifecycle
    .init = posix_init,
    .shutdown = posix_shutdown,
    .get_caps = posix_get_capabilities,
    .get_host_bg = posix_get_host_bg,

    // Display
    .get_size = posix_get_size,
    .set_cursor = posix_set_cursor,
    .set_cursor_visible = posix_set_cursor_visible,
    .set_fg = posix_set_fg,
    .set_bg = posix_set_bg,
    .reset_attrs = posix_reset_attrs,
    .set_bold = posix_set_bold,
    .set_italic = posix_set_italic,
    .set_dim = posix_set_dim,
    .set_strike = posix_set_strikethrough,
    .set_underline = posix_set_underline,
    .set_underline_color = posix_set_underline_color,
    .clear_underline = posix_clear_underline,
    .clear_screen = posix_clear_screen,
    .clear_line = posix_clear_line,
    .clear_range = posix_clear_range,
    .write_str = posix_write_str,
    .write_char = posix_write_char,
    .repeat_char = posix_repeat_char,
    .write_scaled = posix_write_scaled,
    .write_scaled_frac = posix_write_scaled_frac,
    .flush = posix_flush,
    .sync_begin = posix_sync_begin,
    .sync_end = posix_sync_end,
    .set_title = posix_set_title,
    .link_begin = posix_link_begin,
    .link_end = posix_link_end,

    // Input
    .read_key = posix_read_key,
    .mouse_col = posix_get_last_mouse_col,
    .mouse_row = posix_get_last_mouse_row,
    .check_resize = posix_check_resize,
    .check_quit = posix_check_quit,
    .poll_jobs = posix_execute_pending_jobs,
    .input_ready = posix_input_available,
    .register_signals = posix_register_signals,

    // Clipboard
    .copy = posix_clipboard_copy,
    .paste = posix_clipboard_paste,

    // Filesystem
    .home_dir = posix_get_home_dir,
    .mkdir_p = posix_mkdir_p,
    .file_exists = posix_file_exists,
    .read_file = posix_read_file,
    .write_file = posix_write_file,
    .list_dir = posix_list_dir,
    .mtime = posix_get_mtime,
    .rm = posix_delete_file,
    .reveal = posix_reveal_in_finder,
    .on_shutdown = posix_on_shutdown,

    // Time
    .clock = posix_clock,
    .sleep_ms = posix_sleep_ms,
    .localtime = posix_get_local_time,
    .localtime_from = posix_get_local_time_from,
    .username = posix_get_username,

    // Images
    .img_supported = term_image_is_supported,
    .img_size = posix_image_get_size,
    .img_display = posix_image_display,
    .img_display_cropped = posix_image_display_cropped,
    .img_frame_start = posix_image_frame_start,
    .img_frame_end = posix_image_frame_end,
    .img_clear_all = posix_image_clear_all,
    .img_mask = posix_image_mask_region,
    .img_resolve = posix_image_resolve_path,
    .img_calc_rows = posix_image_calc_rows,
    .img_invalidate = posix_image_invalidate,
};
