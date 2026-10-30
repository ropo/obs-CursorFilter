#include "cur-file.h"
#define NOMINMAX
#include <windows.h>
#include <wincodec.h>
#include <shlwapi.h>
#include <util/bmem.h>
#include <algorithm>
#include <vector>

static uint16_t read16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t read32(const uint8_t *p) { return (uint32_t)read16(p) | ((uint32_t)read16(p + 2) << 16); }
static void write16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void write32(uint8_t *p, uint32_t v) { write16(p, (uint16_t)v); write16(p + 2, (uint16_t)(v >> 16)); }

bool load_cur_file(const wchar_t *path, uint32_t target_size, uint8_t **pixels, uint32_t *width,
		   uint32_t *height, uint32_t *hotspot_x, uint32_t *hotspot_y)
{
	*pixels = nullptr;
	HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
				  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) return false;
	LARGE_INTEGER length;
	if (!GetFileSizeEx(file, &length) || length.QuadPart < 22 || length.QuadPart > 64 * 1024 * 1024) {
		CloseHandle(file); return false;
	}
	std::vector<uint8_t> bytes((size_t)length.QuadPart);
	DWORD read = 0;
	bool valid = ReadFile(file, bytes.data(), (DWORD)bytes.size(), &read, nullptr) && read == bytes.size();
	CloseHandle(file);
	if (!valid || read16(bytes.data()) != 0 || read16(bytes.data() + 2) != 2) return false;
	uint16_t count = read16(bytes.data() + 4);
	if (!count || 6ULL + 16ULL * count > bytes.size()) return false;
	const uint8_t *best = nullptr;
	uint32_t best_distance = UINT32_MAX, best_size = 0;
	for (uint16_t i = 0; i < count; ++i) {
		const uint8_t *entry = bytes.data() + 6 + 16 * i;
		uint32_t w = entry[0] ? entry[0] : 256, h = entry[1] ? entry[1] : 256;
		uint32_t offset = read32(entry + 12), size = read32(entry + 8);
		if (!size || (uint64_t)offset + size > bytes.size()) continue;
		uint32_t dimension = std::max(w, h);
		uint32_t distance = dimension > target_size ? dimension - target_size : target_size - dimension;
		if (!best || distance < best_distance || (distance == best_distance && dimension > best_size)) {
			best = entry; best_distance = distance; best_size = dimension;
		}
	}
	if (!best) return false;
	uint32_t size = read32(best + 8), offset = read32(best + 12);
	uint32_t hot_x = read16(best + 4), hot_y = read16(best + 6);
	std::vector<uint8_t> ico(22ULL + size);
	write16(ico.data(), 0); write16(ico.data() + 2, 1); write16(ico.data() + 4, 1);
	memcpy(ico.data() + 6, best, 16);
	write16(ico.data() + 10, 1); write16(ico.data() + 12, 32); write32(ico.data() + 18, 22);
	memcpy(ico.data() + 22, bytes.data() + offset, size);

	HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	if (FAILED(init) && init != RPC_E_CHANGED_MODE) return false;
	IWICImagingFactory *factory = nullptr;
	IWICBitmapDecoder *decoder = nullptr;
	IWICBitmapFrameDecode *frame = nullptr;
	IWICFormatConverter *converter = nullptr;
	IStream *stream = SHCreateMemStream(ico.data(), (UINT)ico.size());
	HRESULT hr = stream ? CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
						IID_PPV_ARGS(&factory)) : E_OUTOFMEMORY;
	if (SUCCEEDED(hr)) hr = factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder);
	if (SUCCEEDED(hr)) hr = decoder->GetFrame(0, &frame);
	if (SUCCEEDED(hr)) hr = factory->CreateFormatConverter(&converter);
	if (SUCCEEDED(hr)) hr = converter->Initialize(frame, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone,
						      nullptr, 0, WICBitmapPaletteTypeCustom);
	UINT w = 0, h = 0;
	if (SUCCEEDED(hr)) hr = converter->GetSize(&w, &h);
	if (SUCCEEDED(hr) && w && h && w <= 256 && h <= 256 && hot_x < w && hot_y < h) {
		uint8_t *data = (uint8_t *)bmalloc((size_t)w * h * 4);
		hr = converter->CopyPixels(nullptr, w * 4, w * h * 4, data);
		if (SUCCEEDED(hr)) {
			*pixels = data; *width = w; *height = h; *hotspot_x = hot_x; *hotspot_y = hot_y;
		} else bfree(data);
	}
	if (converter) converter->Release();
	if (frame) frame->Release();
	if (decoder) decoder->Release();
	if (factory) factory->Release();
	if (stream) stream->Release();
	if (SUCCEEDED(init)) CoUninitialize();
	return *pixels != nullptr;
}
