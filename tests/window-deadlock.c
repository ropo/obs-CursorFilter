#include "../src/filter.c"

static DWORD WINAPI search_thread(void *param)
{
	struct window_search search = {.title = "cursor filter test window"};
	find_window((HWND)param, (LPARAM)&search);
	return search.result ? 1 : 0;
}

int main(int argc, char **argv)
{
	HWND window = CreateWindowExW(0, L"STATIC", L"cursor filter test window", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
				      -32000, -32000, 100, 100, NULL, NULL, GetModuleHandleW(NULL), NULL);
	if (!window)
		return 1;
	HANDLE thread = CreateThread(NULL, 0, search_thread, window, 0, NULL);
	if (!thread)
		return 2;
	DWORD result = WaitForSingleObject(thread, 2000);
	DWORD code = 1;
	if (result == WAIT_OBJECT_0)
		GetExitCodeThread(thread, &code);
	CloseHandle(thread);
	DestroyWindow(window);
	if (result != WAIT_OBJECT_0 || code != 0)
		return 3;
	if (argc > 1) {
		char *class_name = NULL, *title = NULL, *exe = NULL;
		ms_build_window_strings(argv[1], &class_name, &title, &exe);
		struct window_search search = {.title = title, .class_name = class_name, .exe = exe};
		if (title && class_name && exe) EnumWindows(find_window, (LPARAM)&search);
		bfree(class_name); bfree(title); bfree(exe);
		if (!search.result) return 4;
	}
	return 0;
}
