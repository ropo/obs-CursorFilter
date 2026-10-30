#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif
bool load_cur_file(const wchar_t *path, uint32_t target_size, uint8_t **pixels, uint32_t *width,
		   uint32_t *height, uint32_t *hotspot_x, uint32_t *hotspot_y);
#ifdef __cplusplus
}
#endif
