// dawn_modal.c - Modal form rendering helpers

#include "dawn_modal.h"
#include <string.h>

void _modal_write_str(const char *str) {
    DAWN_BACKEND(app)->write_str(str, strlen(str));
}

void _modal_write_char(char c) {
    DAWN_BACKEND(app)->write_char(c);
}

void _modal_set_cursor_visible(bool visible) {
    DAWN_BACKEND(app)->set_cursor_visible(visible);
}
