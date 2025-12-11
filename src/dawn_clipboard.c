// dawn_clipboard.c

#include "dawn_clipboard.h"

// #region Clipboard Operations

void clipboard_copy(const char *text, size_t len) {
    DAWN_BACKEND(app)->copy(text, len);
}

char *clipboard_paste(size_t *out_len) {
    return DAWN_BACKEND(app)->paste(out_len);
}

// #endregion
