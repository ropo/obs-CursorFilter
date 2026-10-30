/* OBS Studio 33.0.0 win-capture 由来。GPL-2.0-or-later。 */
#include <windows.h>
#include <limits.h>
#include <obs.h>
#include "cursor-capture.h"
#include "cur-file.h"

static uint8_t *get_bitmap_data(HBITMAP hbmp, BITMAP *bmp)
{
	if (GetObject(hbmp, sizeof(*bmp), bmp) != 0) {
		uint8_t *output;
		if (bmp->bmHeight <= 0 || bmp->bmWidth <= 0 || bmp->bmWidthBytes <= 0)
			return NULL;
		uint64_t size = (uint64_t)bmp->bmHeight * bmp->bmWidthBytes;
		if (size > LONG_MAX) {
			return NULL;
		}

		output = bmalloc((size_t)size);
		if (GetBitmapBits(hbmp, (LONG)size, output) != (LONG)size) {
			bfree(output);
			return NULL;
		}
		return output;
	}

	return NULL;
}

static inline uint8_t bit_to_alpha(uint8_t *data, long pixel, bool invert)
{
	uint8_t pix_byte = data[pixel / 8];
	bool alpha = (pix_byte >> (7 - pixel % 8) & 1) != 0;

	if (invert) {
		return alpha ? 0xFF : 0;
	} else {
		return alpha ? 0 : 0xFF;
	}
}

static inline bool bitmap_has_alpha(uint8_t *data, long num_pixels)
{
	for (long i = 0; i < num_pixels; i++) {
		if (data[i * 4 + 3] != 0) {
			return true;
		}
	}

	return false;
}

static inline void apply_mask(uint8_t *color, uint8_t *mask, BITMAP *bmp_mask)
{
	for (long y = 0; y < bmp_mask->bmHeight; y++) {
		for (long x = 0; x < bmp_mask->bmWidth; x++) {
			color[(y * bmp_mask->bmWidth + x) * 4 + 3] =
				bit_to_alpha(mask + y * bmp_mask->bmWidthBytes, x, false);
		}
	}
}

static inline uint8_t *copy_from_color(ICONINFO *ii, uint32_t *width, uint32_t *height)
{
	BITMAP bmp_color;
	BITMAP bmp_mask;
	uint8_t *color;
	uint8_t *mask;

	if (!GetObject(ii->hbmColor, sizeof(bmp_color), &bmp_color) ||
	    bmp_color.bmWidth <= 0 || bmp_color.bmHeight <= 0 ||
	    (uint64_t)bmp_color.bmWidth * bmp_color.bmHeight > LONG_MAX / 4) {
		return NULL;
	}

	/* 低色数の画像も、上下方向と行幅をそろえた32bit BGRAへ変換する。 */
	BITMAPINFO info = {0};
	info.bmiHeader.biSize = sizeof(info.bmiHeader);
	info.bmiHeader.biWidth = bmp_color.bmWidth;
	info.bmiHeader.biHeight = -bmp_color.bmHeight;
	info.bmiHeader.biPlanes = 1;
	info.bmiHeader.biBitCount = 32;
	color = bzalloc((size_t)bmp_color.bmWidth * bmp_color.bmHeight * 4);
	HDC dc = GetDC(NULL);
	int rows = dc ? GetDIBits(dc, ii->hbmColor, 0, (UINT)bmp_color.bmHeight, color, &info, DIB_RGB_COLORS) : 0;
	if (dc) ReleaseDC(NULL, dc);
	if (rows != bmp_color.bmHeight) {
		bfree(color);
		return NULL;
	}

	long pixels = bmp_color.bmHeight * bmp_color.bmWidth;
	if (bmp_color.bmBitsPixel < 32 || !bitmap_has_alpha(color, pixels)) {
		mask = get_bitmap_data(ii->hbmMask, &bmp_mask);
		if (!mask || bmp_mask.bmBitsPixel != 1 || bmp_mask.bmPlanes != 1 ||
		    bmp_mask.bmWidth != bmp_color.bmWidth || bmp_mask.bmHeight != bmp_color.bmHeight) {
			bfree(mask);
			bfree(color);
			return NULL;
		}
		apply_mask(color, mask, &bmp_mask);
		bfree(mask);
	}

	*width = bmp_color.bmWidth;
	*height = bmp_color.bmHeight;
	return color;
}

static inline uint8_t *copy_from_mask(ICONINFO *ii, uint32_t *width, uint32_t *height)
{
	uint8_t *output;
	uint8_t *mask;
	long pixels;
	long bottom;
	BITMAP bmp;

	mask = get_bitmap_data(ii->hbmMask, &bmp);
	if (!mask) {
		return NULL;
	}

	if (bmp.bmBitsPixel != 1 || bmp.bmPlanes != 1 || bmp.bmHeight % 2 != 0 ||
	    (uint64_t)bmp.bmWidth * (bmp.bmHeight / 2) > LONG_MAX / 4) {
		bfree(mask);
		return NULL;
	}
	bmp.bmHeight /= 2;

	pixels = bmp.bmHeight * bmp.bmWidth;
	if (!pixels) {
		bfree(mask);
		return NULL;
	}
	output = bzalloc(pixels * 4);

	bottom = bmp.bmWidthBytes * bmp.bmHeight;

	for (long i = 0; i < pixels; i++) {
		long x = i % bmp.bmWidth, y = i / bmp.bmWidth;
		uint8_t andMask = bit_to_alpha(mask + y * bmp.bmWidthBytes, x, true);
		uint8_t xorMask = bit_to_alpha(mask + bottom + y * bmp.bmWidthBytes, x, true);

		if (!andMask) {
			// black in the AND mask
			*(uint32_t *)&output[i * 4] = !!xorMask ? 0x00FFFFFF /*always white*/
								: 0xFF000000 /*always black*/;
		} else {
			// white in the AND mask
			*(uint32_t *)&output[i * 4] = !!xorMask ? 0xFFFFFFFF /*source inverted*/
								: 0 /*transparent*/;
		}
	}

	bfree(mask);

	*width = bmp.bmWidth;
	*height = bmp.bmHeight;
	return output;
}

static inline uint8_t *cursor_capture_icon_bitmap(ICONINFO *ii, uint32_t *width, uint32_t *height, bool *monochrome)
{
	*monochrome = ii->hbmColor == NULL;
	return *monochrome ? copy_from_mask(ii, width, height) : copy_from_color(ii, width, height);
}

static gs_texture_t *get_cached_texture(struct cursor_data *data, uint32_t cx, uint32_t cy)
{
	struct cached_cursor cc;

	for (size_t i = 0; i < data->cached_textures.num; i++) {
		struct cached_cursor *pcc = &data->cached_textures.array[i];

		if (pcc->cx == cx && pcc->cy == cy)
			return pcc->texture;
	}

	cc.texture = gs_texture_create(cx, cy, GS_BGRA, 1, NULL, GS_DYNAMIC);
	if (!cc.texture)
		return NULL;
	cc.cx = cx;
	cc.cy = cy;
	da_push_back(data->cached_textures, &cc);
	return cc.texture;
}

static inline bool cursor_capture_icon(struct cursor_data *data, HICON icon)
{
	uint8_t *bitmap;
	uint32_t height;
	uint32_t width;
	ICONINFO ii;

	if (!icon) {
		return false;
	}
	if (!GetIconInfo(icon, &ii)) {
		return false;
	}

	bitmap = cursor_capture_icon_bitmap(&ii, &width, &height, &data->monochrome);
	bool success = false;
	if (bitmap) {
		if (!data->texture || data->last_cx != width || data->last_cy != height) {
			data->texture = get_cached_texture(data, width, height);
			data->last_cx = width;
			data->last_cy = height;
		}
		if (data->texture) {
			gs_texture_set_image(data->texture, bitmap, width * 4, false);
			success = true;
		}
		bfree(bitmap);

		data->x_hotspot = ii.xHotspot;
		data->y_hotspot = ii.yHotspot;
	}

	DeleteObject(ii.hbmColor);
	DeleteObject(ii.hbmMask);
	return success;
}

static const wchar_t *cursor_setting_name(HCURSOR cursor)
{
	static const struct { LPCWSTR id; const wchar_t *name; } cursors[] = {
		{IDC_ARROW, L"Arrow"}, {IDC_IBEAM, L"IBeam"}, {IDC_WAIT, L"Wait"},
		{IDC_CROSS, L"Crosshair"}, {IDC_UPARROW, L"UpArrow"}, {IDC_SIZENWSE, L"SizeNWSE"},
		{IDC_SIZENESW, L"SizeNESW"}, {IDC_SIZEWE, L"SizeWE"}, {IDC_SIZENS, L"SizeNS"},
		{IDC_SIZEALL, L"SizeAll"}, {IDC_NO, L"No"}, {IDC_HAND, L"Hand"},
		{IDC_APPSTARTING, L"AppStarting"}, {IDC_HELP, L"Help"},
	};
	for (size_t i = 0; i < sizeof(cursors) / sizeof(cursors[0]); i++)
		if (cursor == LoadCursorW(NULL, cursors[i].id))
			return cursors[i].name;
	return NULL;
}

static void get_cursor_file(HCURSOR cursor, wchar_t *path, size_t count)
{
	path[0] = L'\0';
	const wchar_t *name = cursor_setting_name(cursor);
	if (!name) return;
	DWORD bytes = (DWORD)(count * sizeof(wchar_t));
	if (RegGetValueW(HKEY_CURRENT_USER, L"Control Panel\\Cursors", name,
			 RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, NULL, path, &bytes) != ERROR_SUCCESS) {
		path[0] = L'\0';
		return;
	}
	const wchar_t *ext = wcsrchr(path, L'.');
	if (!ext || _wcsicmp(ext, L".cur") != 0)
		path[0] = L'\0';
}

static bool cursor_capture_file(struct cursor_data *data, const wchar_t *path, uint32_t target_size)
{
	uint8_t *pixels = NULL;
	uint32_t width, height, hot_x, hot_y;
	if (!load_cur_file(path, target_size, &pixels, &width, &height, &hot_x, &hot_y))
		return false;
	if (!data->texture || data->last_cx != width || data->last_cy != height) {
		data->texture = get_cached_texture(data, width, height);
		data->last_cx = width;
		data->last_cy = height;
	}
	bool success = data->texture != NULL;
	if (success) {
		gs_texture_set_image(data->texture, pixels, width * 4, false);
		data->x_hotspot = hot_x;
		data->y_hotspot = hot_y;
		data->monochrome = false;
	}
	bfree(pixels);
	return success;
}

void cursor_capture(struct cursor_data *data, uint32_t target_size)
{
	CURSORINFO ci = {0};
	HICON icon;

	ci.cbSize = sizeof(ci);

	if (!GetCursorInfo(&ci)) {
		data->visible = false;
		return;
	}

	memcpy(&data->cursor_pos, &ci.ptScreenPos, sizeof(data->cursor_pos));

	if ((ci.flags & CURSOR_SHOWING) == 0) { data->visible = false; return; }
	wchar_t path[MAX_PATH];
	wcscpy_s(path, MAX_PATH, data->cursor_file);
	ULONGLONG now = GetTickCount64();
	if (data->current_cursor != ci.hCursor || now >= data->next_file_check) {
		get_cursor_file(ci.hCursor, path, MAX_PATH);
		data->next_file_check = now + 1000;
	}
	if (data->current_cursor == ci.hCursor && data->visible && data->requested_size == target_size &&
	    wcscmp(path, data->cursor_file) == 0)
		return;

	data->loaded_scaled = path[0] && target_size > 0 && target_size <= 1024 &&
			      cursor_capture_file(data, path, target_size);
	data->visible = data->loaded_scaled;
	if (!data->visible) {
		icon = CopyIcon(ci.hCursor);
		data->visible = cursor_capture_icon(data, icon);
		if (icon) DestroyIcon(icon);
	}
	data->current_cursor = ci.hCursor;
	data->requested_size = target_size;
	wcscpy_s(data->cursor_file, MAX_PATH, path);
	if ((ci.flags & CURSOR_SHOWING) == 0)
		data->visible = false;
}

void cursor_draw(struct cursor_data *data, long x_offset, long y_offset, long width, long height)
{
	long x = data->cursor_pos.x + x_offset;
	long y = data->cursor_pos.y + y_offset;
	long x_draw = x - data->x_hotspot;
	long y_draw = y - data->y_hotspot;

	if (x < 0 || x > width || y < 0 || y > height)
		return;

	if (data->visible && !!data->texture) {
		gs_blend_state_push();
		enum gs_blend_type blendMode = data->monochrome ? GS_BLEND_INVDSTCOLOR : GS_BLEND_SRCALPHA;
		gs_blend_function_separate(blendMode, /*src_color*/
					   GS_BLEND_INVSRCALPHA /*dest_color*/, GS_BLEND_ONE /*src_alpha*/,
					   GS_BLEND_INVSRCALPHA /*dest_alpha*/);

		gs_matrix_push();
		obs_source_draw(data->texture, x_draw, y_draw, 0, 0, false);
		gs_matrix_pop();

		gs_blend_state_pop();
	}
}

void cursor_data_free(struct cursor_data *data)
{
	for (size_t i = 0; i < data->cached_textures.num; i++) {
		struct cached_cursor *pcc = &data->cached_textures.array[i];
		gs_texture_destroy(pcc->texture);
	}
	da_free(data->cached_textures);
	memset(data, 0, sizeof(*data));
}
