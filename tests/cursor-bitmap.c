#include <windows.h>
#include <obs.h>
#include <stdio.h>

static int texture_attempts;
static int texture_marker;

static gs_texture_t *test_texture_create(uint32_t width, uint32_t height, enum gs_color_format format,
					uint32_t levels, const uint8_t **data, uint32_t flags)
{
	UNUSED_PARAMETER(width); UNUSED_PARAMETER(height); UNUSED_PARAMETER(format);
	UNUSED_PARAMETER(levels); UNUSED_PARAMETER(data); UNUSED_PARAMETER(flags);
	return ++texture_attempts == 1 ? NULL : (gs_texture_t *)&texture_marker;
}

static void test_texture_set_image(gs_texture_t *texture, const uint8_t *data, uint32_t stride, bool flip)
{
	UNUSED_PARAMETER(texture); UNUSED_PARAMETER(data); UNUSED_PARAMETER(stride); UNUSED_PARAMETER(flip);
}

#define gs_texture_create test_texture_create
#define gs_texture_set_image test_texture_set_image
#include "../src/cursor-capture.c"

static bool test_monochrome_stride(void)
{
	/* 幅17の行末には15ビットの余白がある。 */
	uint8_t masks[24] = {0};
	masks[12] = 0x80;
	masks[18] = 0x80;
	masks[21] = 0x80;
	ICONINFO ii = {0};
	ii.hbmMask = CreateBitmap(17, 6, 1, 1, masks);
	uint32_t width = 0, height = 0;
	bool monochrome = false;
	uint8_t *pixels = cursor_capture_icon_bitmap(&ii, &width, &height, &monochrome);
	bool ok = pixels && monochrome && width == 17 && height == 3;
	for (uint32_t y = 0; ok && y < height; y++) {
		for (uint32_t x = 0; ok && x < width; x++) {
			bool white = x == (y == 0 ? 0U : y == 1 ? 16U : 8U);
			uint32_t expected = white ? 0x00FFFFFF : 0xFF000000;
			ok = ((uint32_t *)pixels)[y * width + x] == expected;
		}
	}
	bfree(pixels);
	DeleteObject(ii.hbmMask);
	return ok;
}

static bool test_color_bitmap(WORD depth, bool with_alpha, bool through_icon)
{
	BITMAPINFO info = {0};
	info.bmiHeader.biSize = sizeof(info.bmiHeader);
	info.bmiHeader.biWidth = 3;
	info.bmiHeader.biHeight = -2;
	info.bmiHeader.biPlanes = 1;
	info.bmiHeader.biBitCount = depth;
	uint8_t *bits = NULL;
	ICONINFO ii = {0};
	ii.hbmColor = CreateDIBSection(NULL, &info, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
	uint8_t masks[] = {0x40, 0, 0x20, 0};
	ii.hbmMask = CreateBitmap(3, 2, 1, 1, masks);
	if (!bits || !ii.hbmMask) {
		DeleteObject(ii.hbmColor); DeleteObject(ii.hbmMask); return false;
	}
	memset(bits, 0, 24);
	for (uint32_t y = 0; y < 2; y++) {
		for (uint32_t x = 0; x < 3; x++) {
			uint8_t *p = bits + y * 12 + x * (depth / 8);
			p[0] = (uint8_t)(20 + x); p[1] = (uint8_t)(40 + y); p[2] = 200;
			if (with_alpha) p[3] = 128;
		}
	}
	if (through_icon) {
		HICON icon = CreateIconIndirect(&ii);
		DeleteObject(ii.hbmColor); DeleteObject(ii.hbmMask);
		if (!icon) return false;
		BOOL valid = GetIconInfo(icon, &ii);
		DestroyIcon(icon);
		if (!valid) return false;
	}
	uint32_t width = 0, height = 0;
	bool monochrome = false;
	uint8_t *pixels = cursor_capture_icon_bitmap(&ii, &width, &height, &monochrome);
	bool ok = pixels && !monochrome && width == 3 && height == 2;
	for (uint32_t y = 0; ok && y < height; y++) {
		for (uint32_t x = 0; ok && x < width; x++) {
			uint8_t *p = pixels + (y * width + x) * 4;
			uint8_t alpha = with_alpha ? 128 : x == y + 1 ? 0 : 255;
			ok = p[0] == 20 + x && p[1] == 40 + y && p[2] == 200 && p[3] == alpha;
		}
	}
	bfree(pixels);
	DeleteObject(ii.hbmColor); DeleteObject(ii.hbmMask);
	return ok;
}

static bool test_texture_retry(void)
{
	struct cursor_data data = {0};
	HCURSOR cursor = LoadCursorW(NULL, IDC_ARROW);
	texture_attempts = 0;
	bool first = cursor_capture_icon(&data, cursor);
	bool second = cursor_capture_icon(&data, cursor);
	bool ok = !first && second && texture_attempts == 2 && data.cached_textures.num == 1;
	/* テスト用のテクスチャにはGPUリソースがない。 */
	da_free(data.cached_textures);
	return ok;
}

int main(void)
{
	bool mono = test_monochrome_stride();
	bool color24 = test_color_bitmap(24, false, false);
	bool color32 = test_color_bitmap(32, true, false);
	bool mask32 = test_color_bitmap(32, false, false);
	bool icon24 = test_color_bitmap(24, false, true);
	bool icon32 = test_color_bitmap(32, true, true);
	bool retry = test_texture_retry();
	printf("monochrome stride=%d, 24-bit=%d, 32-bit alpha=%d, 32-bit mask=%d, icons=%d/%d, texture retry=%d\n",
	       mono, color24, color32, mask32, icon24, icon32, retry);
	return mono && color24 && color32 && mask32 && icon24 && icon32 && retry ? 0 : 1;
}
