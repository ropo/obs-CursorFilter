#include "../src/filter.c"
#include <stdio.h>

int main(void)
{
	if (!obs_startup("en-US", NULL, NULL)) return 1;
	struct obs_video_info vi = {0};
	char graphics_module[MAX_PATH];
	if (!GetEnvironmentVariableA("CURSOR_TEST_GRAPHICS", graphics_module, sizeof(graphics_module))) {
		obs_shutdown(); return 4;
	}
	vi.graphics_module = graphics_module;
	vi.fps_num = 30; vi.fps_den = 1;
	vi.base_width = 64; vi.base_height = 64;
	vi.output_width = 64; vi.output_height = 64;
	vi.output_format = VIDEO_FORMAT_RGBA;
	if (obs_reset_video(&vi) != OBS_VIDEO_SUCCESS) { obs_shutdown(); return 2; }
	obs_enter_graphics();
	uint8_t pixels[] = {0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255};
	const uint8_t *planes[] = {pixels};
	struct cursor_data cursor = {0};
	cursor.texture = gs_texture_create(2, 2, GS_BGRA, 1, planes, 0);
	gs_texrender_t *render = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
	gs_stagesurf_t *stage = gs_stagesurface_create(16, 16, GS_BGRA);
	bool ok = cursor.texture && render && stage && gs_texrender_begin(render, 16, 16);
	if (ok) {
		struct vec4 clear = {0};
		gs_clear(GS_CLEAR_COLOR, &clear, 0, 0);
		gs_ortho(0, 16, 0, 16, -100, 100);
		draw_cursor_texture(&cursor, 4, 4, 1, 1, 2);
		gs_texrender_end(render);
		gs_stage_texture(stage, gs_texrender_get_texture(render));
		uint8_t *data = NULL; uint32_t stride = 0;
		ok = gs_stagesurface_map(stage, &data, &stride);
		if (ok) {
			uint8_t *pixel = data + stride * 5 + 5 * 4;
			ok = pixel[2] > 200 && pixel[3] > 200;
			gs_stagesurface_unmap(stage);
		}
	}
	gs_stagesurface_destroy(stage);
	gs_texrender_destroy(render);
	gs_texture_destroy(cursor.texture);
	wchar_t expected_path[MAX_PATH];
	if (ok && GetEnvironmentVariableW(L"CURSOR_TEST_FILE", expected_path, MAX_PATH)) {
		struct cursor_data actual = {0};
		cursor_capture(&actual, 64);
		ok = actual.visible && actual.loaded_scaled && actual.last_cx == 64 && actual.last_cy == 64 &&
		     _wcsicmp(actual.cursor_file, expected_path) == 0;
		printf("カーソル取得: visible=%d file=%d size=%ux%u\n", actual.visible, actual.loaded_scaled, actual.last_cx, actual.last_cy);
		uint8_t raw[64 * 64 * 4];
		wchar_t raw_path[MAX_PATH];
		FILE *expected = NULL;
		if (ok) ok = GetEnvironmentVariableW(L"CURSOR_TEST_BGRA", raw_path, MAX_PATH) &&
			    _wfopen_s(&expected, raw_path, L"rb") == 0 && expected;
		if (ok) ok = fread(raw, 1, sizeof(raw), expected) == sizeof(raw);
		if (expected) fclose(expected);
		gs_stagesurf_t *cursor_stage = ok ? gs_stagesurface_create(64, 64, GS_BGRA) : NULL;
		if (ok) ok = cursor_stage != NULL;
		if (ok) {
			gs_stage_texture(cursor_stage, actual.texture);
			uint8_t *data = NULL; uint32_t stride = 0;
			ok = gs_stagesurface_map(cursor_stage, &data, &stride);
			if (ok) {
				for (size_t y = 0; y < 64 && ok; y++)
					ok = memcmp(raw + y * 64 * 4, data + y * stride, 64 * 4) == 0;
				gs_stagesurface_unmap(cursor_stage);
			}
		}
		gs_texrender_t *cursor_render = ok ? gs_texrender_create(GS_BGRA, GS_ZS_NONE) : NULL;
		if (ok) ok = cursor_render && gs_texrender_begin(cursor_render, 64, 64);
		if (ok) {
			struct vec4 clear = {0};
			gs_clear(GS_CLEAR_COLOR, &clear, 0, 0);
			gs_ortho(0, 64, 0, 64, -100, 100);
			draw_cursor_texture(&actual, (float)actual.x_hotspot, (float)actual.y_hotspot, 1, 1, 1);
			gs_texrender_end(cursor_render);
			gs_stage_texture(cursor_stage, gs_texrender_get_texture(cursor_render));
			uint8_t *data = NULL; uint32_t stride = 0;
			ok = gs_stagesurface_map(cursor_stage, &data, &stride);
			if (ok) {
				int opaque = 0, matched = 0;
				for (size_t y = 0; y < 64; y++) {
					for (size_t x = 0; x < 64; x++) {
						uint8_t *src = raw + (y * 64 + x) * 4;
						uint8_t *dst = data + y * stride + x * 4;
						if (src[3] < 250) continue;
						opaque++;
						if (abs((int)src[0] - dst[0]) <= 2 && abs((int)src[1] - dst[1]) <= 2 &&
						    abs((int)src[2] - dst[2]) <= 2) matched++;
					}
				}
				ok = opaque > 0 && matched * 100 >= opaque * 95;
				printf("不透明画素の一致: %d/%d\n", matched, opaque);
				gs_stagesurface_unmap(cursor_stage);
			}
		}
		if (cursor_render) gs_texrender_destroy(cursor_render);
		if (cursor_stage) gs_stagesurface_destroy(cursor_stage);
		cursor_data_free(&actual);
	}
	obs_leave_graphics();
	obs_shutdown();
	return ok ? 0 : 3;
}
