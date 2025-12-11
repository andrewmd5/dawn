// dawn_image.h

#ifndef DAWN_IMAGE_H
#define DAWN_IMAGE_H

#include "dawn_types.h"

// Check if a file is a supported image format
bool image_is_supported(const char *path);

// Display an image at specified position
// row, col: 1-based coordinates
// max_cols, max_rows: size constraints in cells
// Returns the number of rows the image occupies, or 0 on failure
int32_t image_display_at(const char *path, int32_t row, int32_t col, int32_t max_cols, int32_t max_rows);

// Display image with vertical cropping for smooth scrolling
// crop_top_rows: number of rows to crop from top
// visible_rows: number of rows actually visible
int32_t image_display_at_cropped(const char *path, int32_t row, int32_t col, int32_t max_cols,
                               int32_t crop_top_rows, int32_t visible_rows);

// Display an image at the current cursor position (convenience wrapper)
int32_t image_display(const char *path, int32_t max_cols, int32_t max_rows);

// Call at start of each render frame - clears placement tracking
void image_frame_start(void);

// Call at end of render frame - removes images that weren't placed this frame
void image_frame_end(void);

// Draw an opaque mask over a region (for popup backgrounds over images)
void image_mask_region(int32_t col, int32_t row, int32_t cols, int32_t rows, DawnColor bg);

// Clear all images from the display
void image_clear_all(void);

// Invalidate cache for a specific path (call when file might have changed)
void image_cache_invalidate(const char *path);

// Get image dimensions (width, height in pixels)
// Returns true on success, false if file can't be read
bool image_get_size(const char *path, int32_t *width, int32_t *height);

// Calculate how many display rows an image will occupy given constraints
// pixel_width, pixel_height: actual image dimensions
// max_cols: maximum width in columns (0 = no limit)
// max_rows: maximum height in rows (0 = auto based on aspect ratio)
// Returns estimated rows the image will take
int32_t image_calc_rows(int32_t pixel_width, int32_t pixel_height, int32_t max_cols, int32_t max_rows);

// Resolve image path (absolute, relative, or remote URL) and return resolved path
// raw_path: input path from markdown
// base_dir: base directory for relative paths (or NULL to use session path)
// out: output buffer for resolved path
// out_size: size of output buffer
// Returns true on success
bool image_resolve_and_cache_to(const char *raw_path, const char *base_dir, char *out, size_t out_size);

#endif // DAWN_IMAGE_H
