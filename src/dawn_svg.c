// dawn_svg.c - SVG parsing and rasterization

#include "dawn_svg.h"

#include <stdlib.h>
#include <string.h>

#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"

bool svg_is_svg_file(const char* path)
{
    if (!path)
        return false;

    const char* ext = strrchr(path, '.');
    if (!ext)
        return false;
    ext++;

    return (ext[0] == 's' || ext[0] == 'S') && (ext[1] == 'v' || ext[1] == 'V') && (ext[2] == 'g' || ext[2] == 'G') && ext[3] == '\0';
}

bool svg_rasterize(char* svg_data, uint8_t** out_pixels, int32_t* out_width, int32_t* out_height)
{
    if (!svg_data || !out_pixels || !out_width || !out_height)
        return false;

    NSVGimage* svg = nsvgParse(svg_data, "px", 96.0f);
    if (!svg)
        return false;

    int32_t w = (int32_t)(svg->width + 0.5f);
    int32_t h = (int32_t)(svg->height + 0.5f);
    if (w <= 0)
        w = 100;
    if (h <= 0)
        h = 100;

    float scale = 1.0f;
    int32_t max_dim = w > h ? w : h;
    if (max_dim < 256)
        scale = 256.0f / max_dim;
    else if (max_dim > 2048)
        scale = 2048.0f / max_dim;

    int32_t raster_w = (int32_t)(w * scale + 0.5f);
    int32_t raster_h = (int32_t)(h * scale + 0.5f);

    uint8_t* pixels = malloc((size_t)raster_w * raster_h * 4);
    if (!pixels) {
        nsvgDelete(svg);
        return false;
    }

    memset(pixels, 0, (size_t)raster_w * raster_h * 4);

    NSVGrasterizer* rast = nsvgCreateRasterizer();
    if (!rast) {
        free(pixels);
        nsvgDelete(svg);
        return false;
    }

    nsvgRasterize(rast, svg, 0, 0, scale, pixels, raster_w, raster_h, raster_w * 4);

    nsvgDeleteRasterizer(rast);
    nsvgDelete(svg);

    *out_pixels = pixels;
    *out_width = raster_w;
    *out_height = raster_h;
    return true;
}
