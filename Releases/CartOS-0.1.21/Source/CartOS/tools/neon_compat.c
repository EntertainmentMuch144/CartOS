/*
 * CartOS compatibility implementation for the Miyoo SDL2 backend.
 *
 * The backend imports these entry points from libneonarmmiyoo.so. This file
 * supplies portable C implementations of that ABI using only libc. It does
 * not contain code from the separately distributed NEON helper.
 */
#include "neon_compat.h"

#include <string.h>

void *neon_memcpy(void *destination, const void *source, size_t size)
{
    return memcpy(destination, source, size);
}

static void scale_pixels(const void *source, void *destination,
                         uint32_t source_width, uint32_t source_height,
                         uint32_t source_pitch, uint32_t destination_pitch,
                         uint32_t horizontal_scale, uint32_t vertical_scale,
                         size_t bytes_per_pixel)
{
    if (source == NULL || destination == NULL || source_width == 0 || source_height == 0) {
        return;
    }

    const size_t input_row_bytes = (size_t)source_width * bytes_per_pixel;
    const size_t output_row_bytes = input_row_bytes * horizontal_scale;
    const size_t input_pitch = source_pitch != 0 ? source_pitch : input_row_bytes;
    const size_t output_pitch = destination_pitch != 0 ? destination_pitch : output_row_bytes;
    const uint8_t *input = (const uint8_t *)source;
    uint8_t *output = (uint8_t *)destination;

    for (uint32_t y = 0; y < source_height; ++y) {
        const uint8_t *input_row = input + (size_t)y * input_pitch;
        uint8_t *output_row = output + (size_t)y * vertical_scale * output_pitch;
        for (uint32_t x = 0; x < source_width; ++x) {
            const uint8_t *pixel = input_row + (size_t)x * bytes_per_pixel;
            uint8_t *copies = output_row + (size_t)x * horizontal_scale * bytes_per_pixel;
            for (uint32_t copy = 0; copy < horizontal_scale; ++copy) {
                memcpy(copies + (size_t)copy * bytes_per_pixel, pixel, bytes_per_pixel);
            }
        }
        for (uint32_t copy = 1; copy < vertical_scale; ++copy) {
            memcpy(output_row + (size_t)copy * output_pitch, output_row, output_row_bytes);
        }
    }
}

#define CARTOS_DEFINE_SCALER(X, Y, MODE, TYPE, BPP) \
    void scale##X##x##Y##_##MODE##TYPE(void *source, void *destination, \
                                        uint32_t width, uint32_t height, \
                                        uint32_t source_pitch, uint32_t destination_pitch) \
    { \
        scale_pixels(source, destination, width, height, source_pitch, destination_pitch, X, Y, BPP); \
    }
#define CARTOS_DEFINE_SCALE_ROW(X, Y) \
    CARTOS_DEFINE_SCALER(X, Y, n, 16, 2) \
    CARTOS_DEFINE_SCALER(X, Y, n, 32, 4) \
    CARTOS_DEFINE_SCALER(X, Y, c, 16, 2) \
    CARTOS_DEFINE_SCALER(X, Y, c, 32, 4)
CARTOS_DEFINE_SCALE_ROW(1, 1)
CARTOS_DEFINE_SCALE_ROW(1, 2)
CARTOS_DEFINE_SCALE_ROW(1, 3)
CARTOS_DEFINE_SCALE_ROW(1, 4)
CARTOS_DEFINE_SCALE_ROW(2, 1)
CARTOS_DEFINE_SCALE_ROW(2, 2)
CARTOS_DEFINE_SCALE_ROW(2, 3)
CARTOS_DEFINE_SCALE_ROW(2, 4)
CARTOS_DEFINE_SCALE_ROW(4, 1)
CARTOS_DEFINE_SCALE_ROW(4, 2)
CARTOS_DEFINE_SCALE_ROW(4, 3)
CARTOS_DEFINE_SCALE_ROW(4, 4)
#undef CARTOS_DEFINE_SCALE_ROW
#undef CARTOS_DEFINE_SCALER

static uint8_t channel(uint32_t pixel, unsigned shift)
{
    return (uint8_t)((pixel >> shift) & 255U);
}

static uint8_t rounded_div255(uint32_t value)
{
    return (uint8_t)((value + 127U) / 255U);
}

static uint8_t clamp_byte(uint32_t value)
{
    return (uint8_t)(value > 255U ? 255U : value);
}

static uint32_t read_pixel32(const uint8_t *address)
{
    uint32_t pixel;
    memcpy(&pixel, address, sizeof(pixel));
    return pixel;
}

static void write_pixel32(uint8_t *address, uint32_t pixel)
{
    memcpy(address, &pixel, sizeof(pixel));
}

static uint32_t blend_pixel(uint32_t destination, uint32_t source)
{
    const uint32_t alpha = channel(source, 24);
    const uint32_t inverse = 255U - alpha;
    const uint32_t red = rounded_div255((uint32_t)channel(source, 16) * alpha + (uint32_t)channel(destination, 16) * inverse);
    const uint32_t green = rounded_div255((uint32_t)channel(source, 8) * alpha + (uint32_t)channel(destination, 8) * inverse);
    const uint32_t blue = rounded_div255((uint32_t)channel(source, 0) * alpha + (uint32_t)channel(destination, 0) * inverse);
    const uint32_t out_alpha = alpha + rounded_div255((uint32_t)channel(destination, 24) * inverse);
    return (out_alpha << 24) | (red << 16) | (green << 8) | blue;
}

static uint32_t add_pixel(uint32_t destination, uint32_t source)
{
    const uint32_t alpha = channel(source, 24);
    const uint32_t red = clamp_byte((uint32_t)channel(destination, 16) + rounded_div255((uint32_t)channel(source, 16) * alpha));
    const uint32_t green = clamp_byte((uint32_t)channel(destination, 8) + rounded_div255((uint32_t)channel(source, 8) * alpha));
    const uint32_t blue = clamp_byte((uint32_t)channel(destination, 0) + rounded_div255((uint32_t)channel(source, 0) * alpha));
    return (destination & 0xff000000U) | (red << 16) | (green << 8) | blue;
}

static void fill_solid(void *destination, uint32_t color, uint32_t width,
                       uint32_t height, uint32_t pitch, int additive)
{
    if (destination == NULL || width == 0 || height == 0) {
        return;
    }
    uint8_t *bytes = (uint8_t *)destination;
    const size_t row_pitch = pitch != 0 ? pitch : (size_t)width * 4U;
    for (uint32_t y = 0; y < height; ++y) {
        uint8_t *row = bytes + (size_t)y * row_pitch;
        for (uint32_t x = 0; x < width; ++x) {
            uint8_t *address = row + (size_t)x * 4U;
            const uint32_t destination_pixel = read_pixel32(address);
            const uint32_t output_pixel = additive ? add_pixel(destination_pixel, color)
                                                   : blend_pixel(destination_pixel, color);
            write_pixel32(address, output_pixel);
        }
    }
}

void blend_solid_n32(void *destination, uint32_t color, uint32_t width, uint32_t height, uint32_t pitch)
{ fill_solid(destination, color, width, height, pitch, 0); }
void blend_solid_c32(void *destination, uint32_t color, uint32_t width, uint32_t height, uint32_t pitch)
{ fill_solid(destination, color, width, height, pitch, 0); }
void add_solid_n32(void *destination, uint32_t color, uint32_t width, uint32_t height, uint32_t pitch)
{ fill_solid(destination, color, width, height, pitch, 1); }
void add_solid_c32(void *destination, uint32_t color, uint32_t width, uint32_t height, uint32_t pitch)
{ fill_solid(destination, color, width, height, pitch, 1); }

static void resize_nearest(void *source, void *destination,
                           uint32_t source_width, uint32_t source_height,
                           uint32_t source_pitch, uint32_t destination_pitch,
                           uint32_t destination_width, uint32_t destination_height,
                           uint32_t first_row, uint32_t last_row)
{
    if (source == NULL || destination == NULL || source_width == 0 || source_height == 0 ||
        destination_width == 0 || destination_height == 0) {
        return;
    }
    if (first_row > destination_height) first_row = destination_height;
    if (last_row > destination_height) last_row = destination_height;
    if (last_row < first_row) return;
    const size_t sp = source_pitch != 0 ? source_pitch : (size_t)source_width * 4U;
    const size_t dp = destination_pitch != 0 ? destination_pitch : (size_t)destination_width * 4U;
    const uint8_t *src = (const uint8_t *)source;
    uint8_t *dst = (uint8_t *)destination;
    for (uint32_t y = first_row; y < last_row; ++y) {
        const uint32_t sy = (uint32_t)((uint64_t)y * source_height / destination_height);
        const uint8_t *source_row = src + (size_t)sy * sp;
        uint8_t *destination_row = dst + (size_t)y * dp;
        for (uint32_t x = 0; x < destination_width; ++x) {
            const uint32_t sx = (uint32_t)((uint64_t)x * source_width / destination_width);
            const uint32_t pixel = read_pixel32(source_row + (size_t)sx * 4U);
            write_pixel32(destination_row + (size_t)x * 4U, pixel);
        }
    }
}

#define CARTOS_NEAREST(NAME) \
    void NAME(void *source, void *destination, uint32_t sw, uint32_t sh, \
              uint32_t sp, uint32_t dp, uint32_t dw, uint32_t dh) \
    { resize_nearest(source, destination, sw, sh, sp, dp, dw, dh, 0, dh); }
#define CARTOS_NEAREST_RANGE(NAME) \
    void NAME(void *source, void *destination, uint32_t sw, uint32_t sh, \
              uint32_t sp, uint32_t dp, uint32_t dw, uint32_t dh, \
              uint32_t first_row, uint32_t last_row) \
    { resize_nearest(source, destination, sw, sh, sp, dp, dw, dh, first_row, last_row); }
CARTOS_NEAREST(downscale_area_n32)
CARTOS_NEAREST(downscale_area_c32)
CARTOS_NEAREST_RANGE(downscale_area_n32_range)
CARTOS_NEAREST_RANGE(downscale_area_c32_range)
CARTOS_NEAREST(upscale_area_n32)
CARTOS_NEAREST(upscale_area_c32)
#undef CARTOS_NEAREST_RANGE
#undef CARTOS_NEAREST

static uint32_t interpolate(uint32_t top_left, uint32_t top_right,
                            uint32_t bottom_left, uint32_t bottom_right,
                            uint32_t x_fraction, uint32_t y_fraction)
{
    const uint32_t x_inverse = 256U - x_fraction;
    const uint32_t y_inverse = 256U - y_fraction;
    uint32_t out = 0;
    for (unsigned shift = 0; shift <= 24; shift += 8) {
        const uint32_t top = (uint32_t)channel(top_left, shift) * x_inverse +
                             (uint32_t)channel(top_right, shift) * x_fraction;
        const uint32_t bottom = (uint32_t)channel(bottom_left, shift) * x_inverse +
                                (uint32_t)channel(bottom_right, shift) * x_fraction;
        const uint32_t value = (top * y_inverse + bottom * y_fraction + 32768U) >> 16;
        out |= value << shift;
    }
    return out;
}

static int64_t source_coordinate_q8(uint32_t destination_position,
                                    uint32_t source_size,
                                    uint32_t destination_size)
{
    const int64_t center = (int64_t)(2U * (uint64_t)destination_position + 1U) * source_size * 128 / destination_size;
    return center - 128;
}

static void resize_bilinear(void *source, void *destination,
                            uint32_t source_width, uint32_t source_height,
                            uint32_t source_pitch, uint32_t destination_pitch,
                            uint32_t destination_width, uint32_t destination_height,
                            uint32_t first_row, uint32_t last_row)
{
    if (source == NULL || destination == NULL || source_width == 0 || source_height == 0 ||
        destination_width == 0 || destination_height == 0) {
        return;
    }
    if (first_row > destination_height) first_row = destination_height;
    if (last_row > destination_height) last_row = destination_height;
    if (last_row < first_row) return;
    const size_t sp = source_pitch != 0 ? source_pitch : (size_t)source_width * 4U;
    const size_t dp = destination_pitch != 0 ? destination_pitch : (size_t)destination_width * 4U;
    const uint8_t *src = (const uint8_t *)source;
    uint8_t *dst = (uint8_t *)destination;
    const int64_t max_x = (int64_t)(source_width - 1U) * 256;
    const int64_t max_y = (int64_t)(source_height - 1U) * 256;

    for (uint32_t y = first_row; y < last_row; ++y) {
        int64_t fy = source_coordinate_q8(y, source_height, destination_height);
        if (fy < 0) fy = 0;
        if (fy > max_y) fy = max_y;
        const uint32_t y0 = (uint32_t)(fy / 256);
        const uint32_t y1 = y0 + 1U < source_height ? y0 + 1U : y0;
        const uint32_t yf = (uint32_t)(fy % 256);
        const uint8_t *row0 = src + (size_t)y0 * sp;
        const uint8_t *row1 = src + (size_t)y1 * sp;
        uint8_t *out = dst + (size_t)y * dp;
        for (uint32_t x = 0; x < destination_width; ++x) {
            int64_t fx = source_coordinate_q8(x, source_width, destination_width);
            if (fx < 0) fx = 0;
            if (fx > max_x) fx = max_x;
            const uint32_t x0 = (uint32_t)(fx / 256);
            const uint32_t x1 = x0 + 1U < source_width ? x0 + 1U : x0;
            const uint32_t xf = (uint32_t)(fx % 256);
            const uint32_t pixel = interpolate(
                read_pixel32(row0 + (size_t)x0 * 4U),
                read_pixel32(row0 + (size_t)x1 * 4U),
                read_pixel32(row1 + (size_t)x0 * 4U),
                read_pixel32(row1 + (size_t)x1 * 4U), xf, yf);
            write_pixel32(out + (size_t)x * 4U, pixel);
        }
    }
}

#define CARTOS_BILINEAR(NAME) \
    void NAME(void *source, void *destination, uint32_t sw, uint32_t sh, \
              uint32_t sp, uint32_t dp, uint32_t dw, uint32_t dh) \
    { resize_bilinear(source, destination, sw, sh, sp, dp, dw, dh, 0, dh); }
#define CARTOS_BILINEAR_RANGE(NAME) \
    void NAME(void *source, void *destination, uint32_t sw, uint32_t sh, \
              uint32_t sp, uint32_t dp, uint32_t dw, uint32_t dh, \
              uint32_t first_row, uint32_t last_row) \
    { resize_bilinear(source, destination, sw, sh, sp, dp, dw, dh, first_row, last_row); }
CARTOS_BILINEAR(bilinear_scale_n32)
CARTOS_BILINEAR(bilinear_scale_c32)
CARTOS_BILINEAR_RANGE(bilinear_scale_n32_range)
CARTOS_BILINEAR_RANGE(bilinear_scale_c32_range)
#undef CARTOS_BILINEAR_RANGE
#undef CARTOS_BILINEAR

static void rotate_32(void *source, void *destination,
                      uint32_t source_width, uint32_t source_height,
                      uint32_t source_pitch, uint32_t destination_pitch,
                      int clockwise)
{
    if (source == NULL || destination == NULL || source_width == 0 || source_height == 0) return;
    const size_t sp = source_pitch != 0 ? source_pitch : (size_t)source_width * 4U;
    const size_t dp = destination_pitch != 0 ? destination_pitch : (size_t)source_height * 4U;
    const uint8_t *src = (const uint8_t *)source;
    uint8_t *dst = (uint8_t *)destination;
    for (uint32_t y = 0; y < source_height; ++y) {
        for (uint32_t x = 0; x < source_width; ++x) {
            const uint32_t dx = clockwise ? source_height - 1U - y : y;
            const uint32_t dy = clockwise ? x : source_width - 1U - x;
            memcpy(dst + (size_t)dy * dp + (size_t)dx * 4U,
                   src + (size_t)y * sp + (size_t)x * 4U, 4U);
        }
    }
}

void rotate90_n32(void *s, void *d, uint32_t sw, uint32_t sh, uint32_t sp, uint32_t dp)
{ rotate_32(s, d, sw, sh, sp, dp, 0); }
void rotate90_c32(void *s, void *d, uint32_t sw, uint32_t sh, uint32_t sp, uint32_t dp)
{ rotate_32(s, d, sw, sh, sp, dp, 0); }
void rotate270_n32(void *s, void *d, uint32_t sw, uint32_t sh, uint32_t sp, uint32_t dp)
{ rotate_32(s, d, sw, sh, sp, dp, 1); }
void rotate270_c32(void *s, void *d, uint32_t sw, uint32_t sh, uint32_t sp, uint32_t dp)
{ rotate_32(s, d, sw, sh, sp, dp, 1); }
