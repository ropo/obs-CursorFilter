#include <windows.h>
#include <shellscalingapi.h>
#include <obs-module.h>
#include <util/platform.h>
#include <util/windows/window-helpers.h>
#include "cursor-capture.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-cursor-filter", "en-US")
MODULE_EXPORT const char *obs_module_description(void) { return obs_module_text("Description"); }

struct filter_data {
	obs_source_t *source;
	char *window_title;
	double scale;
	DWORD cursor_base_size;
	ULONGLONG next_size_check;
	bool foreground_only;
	bool use_source_window;
	struct cursor_data cursor;
};

struct window_search { const char *title; HWND result; const char *class_name; const char *exe; };

static bool is_external_window(HWND hwnd)
{
	DWORD process_id = 0;
	GetWindowThreadProcessId(hwnd, &process_id);
	return process_id && process_id != GetCurrentProcessId();
}

static BOOL CALLBACK find_window(HWND hwnd, LPARAM param)
{
	struct window_search *search = (struct window_search *)param;
	if (!is_external_window(hwnd) || !IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER))
		return TRUE;
	wchar_t title[1024];
	char utf8[4096];
	if (!GetWindowTextW(hwnd, title, 1024))
		return TRUE;
	if (!WideCharToMultiByte(CP_UTF8, 0, title, -1, utf8, sizeof(utf8), NULL, NULL))
		return TRUE;
	if (strcmp(utf8, search->title) == 0) {
		struct dstr class_name = {0}, exe = {0};
		if (search->class_name) {
			ms_get_window_class(&class_name, hwnd);
			bool match = dstr_cmpi(&class_name, search->class_name) == 0;
			dstr_free(&class_name);
			if (!match) return TRUE;
		}
		if (search->exe) {
			bool match = ms_get_window_exe(&exe, hwnd) && dstr_cmpi(&exe, search->exe) == 0;
			dstr_free(&exe);
			if (!match) return TRUE;
		}
		search->result = hwnd;
		return FALSE;
	}
	return TRUE;
}

static BOOL CALLBACK add_window(HWND hwnd, LPARAM param)
{
	if (!is_external_window(hwnd) || !IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER))
		return TRUE;
	wchar_t title[1024];
	char utf8[4096];
	if (GetWindowTextW(hwnd, title, 1024) &&
	    WideCharToMultiByte(CP_UTF8, 0, title, -1, utf8, sizeof(utf8), NULL, NULL))
		obs_property_list_add_string((obs_property_t *)param, utf8, utf8);
	return TRUE;
}

static const char *filter_name(void *unused) { UNUSED_PARAMETER(unused); return obs_module_text("CursorFilter"); }
static void filter_update(void *data, obs_data_t *settings)
{
	struct filter_data *f = data;
	bfree(f->window_title);
	f->window_title = bstrdup(obs_data_get_string(settings, "window"));
	f->scale = obs_data_get_double(settings, "scale_adjust");
	f->foreground_only = obs_data_get_bool(settings, "foreground_only");
	f->use_source_window = obs_data_get_bool(settings, "use_source_window");
}
static void *filter_create(obs_data_t *settings, obs_source_t *source)
{
	struct filter_data *f = bzalloc(sizeof(*f));
	f->source = source;
	f->cursor_base_size = 32;
	filter_update(f, settings);
	return f;
}
static void filter_destroy(void *data)
{
	struct filter_data *f = data;
	obs_enter_graphics();
	cursor_data_free(&f->cursor);
	obs_leave_graphics();
	bfree(f->window_title);
	bfree(f);
}
static void filter_defaults(obs_data_t *settings)
{
	obs_data_set_default_double(settings, "scale_adjust", 1.0);
	obs_data_set_default_bool(settings, "foreground_only", true);
	obs_data_set_default_bool(settings, "use_source_window", true);
}
static obs_properties_t *filter_properties(void *data)
{
	UNUSED_PARAMETER(data);
	obs_properties_t *p = obs_properties_create();
	obs_properties_add_bool(p, "use_source_window", obs_module_text("UseSourceWindow"));
	obs_property_t *window = obs_properties_add_list(p, "window", obs_module_text("Window"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(window, obs_module_text("SelectWindow"), "");
	EnumWindows(add_window, (LPARAM)window);
	obs_properties_add_float_slider(p, "scale_adjust", obs_module_text("ScaleAdjust"), 0.5, 4.0, 0.1);
	obs_properties_add_bool(p, "foreground_only", obs_module_text("ForegroundOnly"));
	return p;
}
static HWND resolve_window(struct filter_data *f)
{
	struct window_search search = {0};
	char *class_name = NULL, *title = NULL, *exe = NULL;
	if (f->use_source_window) {
		obs_source_t *parent = obs_filter_get_parent(f->source);
		if (!parent || strcmp(obs_source_get_id(parent), "game_capture") != 0)
			return NULL;
		obs_data_t *settings = obs_source_get_settings(parent);
		if (!settings)
			return NULL;
		const char *mode = obs_data_get_string(settings, "capture_mode");
		if (strcmp(mode, "window") == 0)
			ms_build_window_strings(obs_data_get_string(settings, "window"), &class_name, &title, &exe);
		obs_data_release(settings);
		search.title = title;
		search.class_name = class_name;
		search.exe = exe;
	} else {
		search.title = f->window_title;
	}
	if (search.title && *search.title)
		EnumWindows(find_window, (LPARAM)&search);
	bfree(class_name);
	bfree(title);
	bfree(exe);
	return search.result;
}

static void draw_cursor_texture(struct cursor_data *cursor, float x, float y, float sx, float sy, float scale)
{
	gs_blend_state_push();
	gs_blend_function_separate(cursor->monochrome ? GS_BLEND_INVDSTCOLOR : GS_BLEND_SRCALPHA,
				   GS_BLEND_INVSRCALPHA, GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
	gs_matrix_push();
	gs_matrix_translate3f((x - cursor->x_hotspot * scale) * sx, (y - cursor->y_hotspot * scale) * sy, 0);
	gs_matrix_scale3f(scale * sx, scale * sy, 1);
	gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	while (gs_effect_loop(effect, "Draw"))
		obs_source_draw(cursor->texture, 0, 0, 0, 0, false);
	gs_matrix_pop();
	gs_blend_state_pop();
}

static void filter_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct filter_data *f = data;
	obs_source_t *target = obs_filter_get_target(f->source);
	if (!target)
		return;
	uint32_t width = obs_source_get_base_width(target), height = obs_source_get_base_height(target);
	if (!width || !height)
		return;
	if (!obs_source_process_filter_begin(f->source, GS_RGBA, OBS_NO_DIRECT_RENDERING))
		return;
	obs_source_process_filter_end(f->source, obs_get_base_effect(OBS_EFFECT_DEFAULT), width, height);

	HWND hwnd = resolve_window(f);
	if (!hwnd || IsIconic(hwnd))
		return;
	HWND foreground = GetForegroundWindow();
	if (f->foreground_only && foreground != hwnd && !IsChild(hwnd, foreground))
		return;

	DPI_AWARENESS_CONTEXT previous = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	RECT rect;
	POINT origin = {0, 0};
	bool valid = GetClientRect(hwnd, &rect) && ClientToScreen(hwnd, &origin);
	if (!valid || rect.right <= 0 || rect.bottom <= 0) {
		if (previous) SetThreadDpiAwarenessContext(previous);
		return;
	}
	ULONGLONG now = GetTickCount64();
	if (now >= f->next_size_check) {
		DWORD value = 32, bytes = sizeof(value);
		if (RegGetValueW(HKEY_CURRENT_USER, L"Control Panel\\Cursors", L"CursorBaseSize",
				 RRF_RT_REG_DWORD, NULL, &value, &bytes) != ERROR_SUCCESS || value < 32 || value > 256)
			value = 32;
		f->cursor_base_size = value;
		f->next_size_check = now + 1000;
	}
	UINT dpi_x = 96, dpi_y = 96;
	POINT cursor_position;
	if (!GetCursorPos(&cursor_position)) {
		if (previous) SetThreadDpiAwarenessContext(previous);
		return;
	}
	HMONITOR monitor = MonitorFromPoint(cursor_position, MONITOR_DEFAULTTONEAREST);
	if (FAILED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y)))
		dpi_x = GetDpiForSystem();
	UINT system_dpi = GetDpiForSystem();
	int monitor_cursor = GetSystemMetricsForDpi(SM_CXCURSOR, dpi_x);
	int system_cursor = GetSystemMetricsForDpi(SM_CXCURSOR, system_dpi);
	uint32_t target_size = monitor_cursor > 0 ? (uint32_t)MulDiv(monitor_cursor, (int)f->cursor_base_size, 32) : 0;
	cursor_capture(&f->cursor, target_size);
	if (previous) SetThreadDpiAwarenessContext(previous);
	struct cursor_data *c = &f->cursor;
	float x = (float)(c->cursor_pos.x - origin.x);
	float y = (float)(c->cursor_pos.y - origin.y);
	if (!c->visible || !c->texture || x < 0 || y < 0 || x >= rect.right || y >= rect.bottom)
		return;
	float sx = (float)width / (float)rect.right, sy = (float)height / (float)rect.bottom;
	float dpi_scale = system_cursor > 0 && monitor_cursor > 0 ? (float)monitor_cursor / (float)system_cursor : 1.0f;
	float scale = (float)f->scale;
	if (c->loaded_scaled && c->last_cx)
		scale *= (float)target_size / (float)c->last_cx;
	else
		scale *= (float)f->cursor_base_size / 32.0f * dpi_scale;
	draw_cursor_texture(c, x, y, sx, sy, scale);
}

static struct obs_source_info filter_info = {
	.id = "cursor_filter",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO,
	.get_name = filter_name,
	.create = filter_create,
	.destroy = filter_destroy,
	.update = filter_update,
	.get_defaults = filter_defaults,
	.get_properties = filter_properties,
	.video_render = filter_render,
};
bool obs_module_load(void) { obs_register_source(&filter_info); return true; }
