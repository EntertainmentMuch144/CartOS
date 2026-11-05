#ifndef CARTOS_NEON_COMPAT_H
#define CARTOS_NEON_COMPAT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void *neon_memcpy(void *destination, const void *source, size_t size);

#define CARTOS_DECLARE_SCALER(X, Y, MODE, TYPE) \
    void scale##X##x##Y##_##MODE##TYPE(void *source, void *destination, \
                                       uint32_t width, uint32_t height, \
                                       uint32_t source_pitch, uint32_t destination_pitch)
#define CARTOS_DECLARE_SCALE_ROW(X, Y) \
    CARTOS_DECLARE_SCALER(X, Y, n, 16); \
    CARTOS_DECLARE_SCALER(X, Y, n, 32); \
    CARTOS_DECLARE_SCALER(X, Y, c, 16); \
    CARTOS_DECLARE_SCALER(X, Y, c, 32)
CARTOS_DECLARE_SCALE_ROW(1, 1);
CARTOS_DECLARE_SCALE_ROW(1, 2);
CARTOS_DECLARE_SCALE_ROW(1, 3);
CARTOS_DECLARE_SCALE_ROW(1, 4);
CARTOS_DECLARE_SCALE_ROW(2, 1);
CARTOS_DECLARE_SCALE_ROW(2, 2);
CARTOS_DECLARE_SCALE_ROW(2, 3);
CARTOS_DECLARE_SCALE_ROW(2, 4);
CARTOS_DECLARE_SCALE_ROW(4, 1);
CARTOS_DECLARE_SCALE_ROW(4, 2);
CARTOS_DECLARE_SCALE_ROW(4, 3);
CARTOS_DECLARE_SCALE_ROW(4, 4);
#undef CARTOS_DECLARE_SCALE_ROW
#undef CARTOS_DECLARE_SCALER

void blend_solid_n32(void *destination, uint32_t color, uint32_t width, uint32_t height, uint32_t pitch);
void blend_solid_c32(void *destination, uint32_t color, uint32_t width, uint32_t height, uint32_t pitch);
void add_solid_n32(void *destination, uint32_t color, uint32_t width, uint32_t height, uint32_t pitch);
void add_solid_c32(void *destination, uint32_t color, uint32_t width, uint32_t height, uint32_t pitch);

#define DOWNSCALE_AREA_MAX_DW 1024
#define BILINEAR_SCALE_MAX_DW 1024

#define CARTOS_DECLARE_RESIZER(NAME) \
    void NAME(void *source, void *destination, uint32_t source_width, uint32_t source_height, \
              uint32_t source_pitch, uint32_t destination_pitch, \
              uint32_t destination_width, uint32_t destination_height)
#define CARTOS_DECLARE_RESIZER_RANGE(NAME) \
    void NAME(void *source, void *destination, uint32_t source_width, uint32_t source_height, \
              uint32_t source_pitch, uint32_t destination_pitch, \
              uint32_t destination_width, uint32_t destination_height, \
              uint32_t first_row, uint32_t last_row)
CARTOS_DECLARE_RESIZER(downscale_area_n32);
CARTOS_DECLARE_RESIZER(downscale_area_c32);
CARTOS_DECLARE_RESIZER_RANGE(downscale_area_n32_range);
CARTOS_DECLARE_RESIZER_RANGE(downscale_area_c32_range);
CARTOS_DECLARE_RESIZER(upscale_area_n32);
CARTOS_DECLARE_RESIZER(upscale_area_c32);
CARTOS_DECLARE_RESIZER(bilinear_scale_n32);
CARTOS_DECLARE_RESIZER(bilinear_scale_c32);
CARTOS_DECLARE_RESIZER_RANGE(bilinear_scale_n32_range);
CARTOS_DECLARE_RESIZER_RANGE(bilinear_scale_c32_range);
#undef CARTOS_DECLARE_RESIZER
#undef CARTOS_DECLARE_RESIZER_RANGE

void rotate90_n32(void *source, void *destination, uint32_t source_width, uint32_t source_height, uint32_t source_pitch, uint32_t destination_pitch);
void rotate90_c32(void *source, void *destination, uint32_t source_width, uint32_t source_height, uint32_t source_pitch, uint32_t destination_pitch);
void rotate270_n32(void *source, void *destination, uint32_t source_width, uint32_t source_height, uint32_t source_pitch, uint32_t destination_pitch);
void rotate270_c32(void *source, void *destination, uint32_t source_width, uint32_t source_height, uint32_t source_pitch, uint32_t destination_pitch);

#ifdef __cplusplus
}
#endif
#endif
