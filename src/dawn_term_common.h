// dawn_term_common.h - Shared terminal code for POSIX and Win32 backends

#ifndef DAWN_TERM_COMMON_H
#define DAWN_TERM_COMMON_H

#include "dawn_backend.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// #region ANSI Escape Sequences

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

// #endregion

// #region Output Buffer

#define OUTPUT_BUF_SIZE (256 * 1024)

// #endregion

// #region Number Formatting

//! Format number into buffer, returns length written
static inline int32_t format_num(char* buf, int32_t n)
{
    if (n < 10) {
        buf[0] = '0' + (char)n;
        return 1;
    } else if (n < 100) {
        buf[0] = '0' + (char)(n / 10);
        buf[1] = '0' + (char)(n % 10);
        return 2;
    } else {
        int32_t len = 0;
        char tmp[12];
        while (n > 0) {
            tmp[len++] = '0' + (char)(n % 10);
            n /= 10;
        }
        for (int32_t i = 0; i < len; i++) {
            buf[i] = tmp[len - 1 - i];
        }
        return len;
    }
}

// #endregion

// #region Color Sequence Builders

//! Build foreground color sequence: \x1b[38;2;r;g;bm
//! @param seq Output buffer (must be at least 24 bytes)
//! @return Length written
static inline int32_t build_fg_seq(char* seq, uint8_t r, uint8_t g, uint8_t b)
{
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
    return pos;
}

//! Build background color sequence: \x1b[48;2;r;g;bm
//! @param seq Output buffer (must be at least 24 bytes)
//! @return Length written
static inline int32_t build_bg_seq(char* seq, uint8_t r, uint8_t g, uint8_t b)
{
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
    return pos;
}

//! Build underline color sequence: \x1b[58:2::r:g:bm
//! @param seq Output buffer (must be at least 24 bytes)
//! @return Length written
static inline int32_t build_underline_color_seq(char* seq, uint8_t r, uint8_t g, uint8_t b)
{
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
    return pos;
}

//! Build cursor position sequence: \x1b[row;colH
//! @param seq Output buffer (must be at least 16 bytes)
//! @return Length written
static inline int32_t build_cursor_seq(char* seq, int32_t row, int32_t col)
{
    seq[0] = '\x1b';
    seq[1] = '[';
    int32_t pos = 2;
    pos += format_num(seq + pos, row);
    seq[pos++] = ';';
    pos += format_num(seq + pos, col);
    seq[pos++] = 'H';
    return pos;
}

// #endregion

// #region Image Cache

typedef struct {
    char* path;
    uint32_t image_id;
    int64_t mtime;
} TransmittedImage;

#define MAX_TRANSMITTED_IMAGES 8

// #endregion

// #region VT Sequence Parsing

//! Parse a VT escape sequence and return the corresponding key
//! @param buf The escape sequence buffer (starting with \x1b)
//! @param len Length of data in buffer
//! @param mouse_col Pointer to store mouse column (updated on mouse events)
//! @param mouse_row Pointer to store mouse row (updated on mouse events)
//! @return DawnKey value or DAWN_KEY_NONE
static inline int32_t term_parse_vt(const char* buf, int32_t len, int32_t* mouse_col, int32_t* mouse_row)
{
    if (len < 2 || buf[0] != '\x1b')
        return DAWN_KEY_NONE;

    if (buf[1] == '[') {
        // SGR mouse events: \x1b[<btn;x;yM or \x1b[<btn;x;ym
        if (len >= 3 && buf[2] == '<') {
            const char* end = memchr(buf + 3, 'M', (size_t)(len - 3));
            if (!end)
                end = memchr(buf + 3, 'm', (size_t)(len - 3));
            if (end) {
                int32_t btn = 0, mx = 0, my = 0;
                if (sscanf(buf + 3, "%d;%d;%d", &btn, &mx, &my) == 3) {
                    if (mouse_col)
                        *mouse_col = mx;
                    if (mouse_row)
                        *mouse_row = my;
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

        // Kitty keyboard protocol: \x1b[keycode;modsu
        if (len >= 3 && buf[2] >= '0' && buf[2] <= '9') {
            const char* u_pos = memchr(buf + 2, 'u', (size_t)(len - 2));
            if (u_pos) {
                int32_t keycode = 0, mods = 1;
                sscanf(buf + 2, "%d;%d", &keycode, &mods);

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

            // Legacy CSI sequences: \x1b[n~
            const char* tilde = memchr(buf + 2, '~', (size_t)(len - 2));
            if (tilde) {
                int32_t num = 0;
                sscanf(buf + 2, "%d", &num);
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
                case 200: // Bracketed paste start - ignore
                case 201: // Bracketed paste end - ignore
                    return DAWN_KEY_NONE;
                }
                return DAWN_KEY_NONE;
            }

            // Modified arrow keys: \x1b[1;modX
            int32_t num1 = 0, num2 = 0;
            char termchar = 0;
            if (sscanf(buf + 2, "%d;%d%c", &num1, &num2, &termchar) == 3) {
                bool shift = (num2 == 2 || num2 == 4 || num2 == 6 || num2 == 8 || num2 == 10 || num2 == 12 || num2 == 14 || num2 == 16);
                bool ctrl = (num2 == 5 || num2 == 6 || num2 == 7 || num2 == 8 || num2 == 13 || num2 == 14 || num2 == 15 || num2 == 16);
                bool alt = (num2 == 3 || num2 == 4 || num2 == 7 || num2 == 8 || num2 == 11 || num2 == 12 || num2 == 15 || num2 == 16);

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

        // Simple arrow keys: \x1b[X
        if (len == 3) {
            switch (buf[2]) {
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
    } else if (buf[1] == 'O' && len == 3) {
        // SS3 sequences: \x1bOX
        switch (buf[2]) {
        case 'H':
            return DAWN_KEY_HOME;
        case 'F':
            return DAWN_KEY_END;
        }
    } else if (len == 2) {
        // Alt+key: \x1bk
        if (buf[1] == 'b')
            return DAWN_KEY_ALT_LEFT;
        if (buf[1] == 'f')
            return DAWN_KEY_ALT_RIGHT;
    }

    return DAWN_KEY_NONE;
}

// #endregion

// #region Base64 Encoding

static const char term_b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

//! Base64 encode data
//! @param data Input data
//! @param input_len Length of input data
//! @param output_len Pointer to store output length
//! @return Allocated base64 string (caller must free) or NULL on failure
static inline char* term_base64_encode(const uint8_t* data, size_t input_len, size_t* output_len)
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
        encoded[j++] = term_b64_table[(triple >> 18) & 0x3F];
        encoded[j++] = term_b64_table[(triple >> 12) & 0x3F];
        encoded[j++] = term_b64_table[(triple >> 6) & 0x3F];
        encoded[j++] = term_b64_table[triple & 0x3F];
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

// #endregion

// #region CPR Parsing

//! Parse Cursor Position Report (CPR) response
//! @param buf Response buffer
//! @param len Length of response
//! @param row Pointer to store row (1-indexed)
//! @param col Pointer to store column (1-indexed)
//! @return true if successfully parsed
static inline bool term_parse_cpr(const char* buf, size_t len, int32_t* row, int32_t* col)
{
    if (len < 6)
        return false;

    const char* p = buf;
    const char* end = buf + len;

    // Find ESC [
    while (p < end - 1 && !(p[0] == '\x1b' && p[1] == '['))
        p++;
    if (p >= end - 1)
        return false;
    p += 2;

    // Parse row
    *row = 0;
    while (p < end && *p >= '0' && *p <= '9') {
        *row = *row * 10 + (*p - '0');
        p++;
    }

    if (p >= end || *p != ';')
        return false;
    p++;

    // Parse col
    *col = 0;
    while (p < end && *p >= '0' && *p <= '9') {
        *col = *col * 10 + (*p - '0');
        p++;
    }

    if (p >= end || *p != 'R')
        return false;
    return (*row > 0 && *col > 0);
}

// #endregion

// #region URL Helpers

//! Check if path is a remote URL (http:// or https://)
static inline bool term_is_remote_url(const char* path)
{
    if (!path)
        return false;
    return (strncmp(path, "http://", 7) == 0 || strncmp(path, "https://", 8) == 0);
}

//! Hash string to hex (djb2 algorithm)
//! @param str Input string
//! @param out_hex Output buffer (must be at least 17 bytes for 16 hex chars + null)
static inline void term_hash_to_hex(const char* str, char* out_hex)
{
    uint64_t hash = 5381;
    int32_t c;
    while ((c = (uint8_t)*str++)) {
        hash = ((hash << 5) + hash) + (uint64_t)c;
    }
    snprintf(out_hex, 17, "%016llx", (unsigned long long)hash);
}

// #endregion

// #region Image Helpers

//! Check if image format is supported based on file extension
static inline bool term_image_is_supported(const char* path)
{
    if (!path)
        return false;
    const char* ext = strrchr(path, '.');
    if (!ext)
        return false;
    ext++;

    // Convert to lowercase for comparison
    char lower[16];
    size_t i;
    for (i = 0; i < sizeof(lower) - 1 && ext[i]; i++) {
        lower[i] = (ext[i] >= 'A' && ext[i] <= 'Z') ? (char)(ext[i] | 32) : ext[i];
    }
    lower[i] = '\0';

    return strcmp(lower, "png") == 0 || strcmp(lower, "jpg") == 0 ||
           strcmp(lower, "jpeg") == 0 || strcmp(lower, "gif") == 0 ||
           strcmp(lower, "bmp") == 0 || strcmp(lower, "svg") == 0;
}

// #endregion

#endif // DAWN_TERM_COMMON_H
