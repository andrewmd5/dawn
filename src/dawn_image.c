// dawn_image.c

#include "dawn_image.h"
#include <string.h>

bool image_is_supported(const char *path) {
    return DAWN_BACKEND(app)->img_supported(path);
}

int32_t image_display_at(const char *path, int32_t row, int32_t col, int32_t max_cols, int32_t max_rows) {
    return DAWN_BACKEND(app)->img_display(path, row, col, max_cols, max_rows);
}

int32_t image_display_at_cropped(const char *path, int32_t row, int32_t col, int32_t max_cols,
                               int32_t crop_top_rows, int32_t visible_rows) {
    return DAWN_BACKEND(app)->img_display_cropped(path, row, col, max_cols, crop_top_rows, visible_rows);
}

int32_t image_display(const char *path, int32_t max_cols, int32_t max_rows) {
    return image_display_at(path, 0, 0, max_cols, max_rows);
}

void image_frame_start(void) {
    DAWN_BACKEND(app)->img_frame_start();
}

void image_frame_end(void) {
    DAWN_BACKEND(app)->img_frame_end();
}

void image_mask_region(int32_t col, int32_t row, int32_t cols, int32_t rows, DawnColor bg) {
    DAWN_BACKEND(app)->img_mask(col, row, cols, rows, bg);
}

void image_clear_all(void) {
    DAWN_BACKEND(app)->img_clear_all();
}

void image_cache_invalidate(const char *path) {
    DAWN_BACKEND(app)->img_invalidate(path);
}

bool image_get_size(const char *path, int32_t *width, int32_t *height) {
    return DAWN_BACKEND(app)->img_size(path, width, height);
}

int32_t image_calc_rows(int32_t pixel_width, int32_t pixel_height, int32_t max_cols, int32_t max_rows) {
    return DAWN_BACKEND(app)->img_calc_rows(pixel_width, pixel_height, max_cols, max_rows);
}

bool image_resolve_and_cache_to(const char *raw_path, const char *base_dir, char *out, size_t out_size) {
    return DAWN_BACKEND(app)->img_resolve(raw_path, base_dir, out, out_size);
}
