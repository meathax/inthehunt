#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include "world_border.h"
#include <wincodec.h>
#include <stdlib.h>
#include <string.h>

static uint32_t *pixels;
static unsigned image_width, image_height, light = 100;
static char *image_path;
static HDC cache_dc;
static HBITMAP cache_bitmap;
static HGDIOBJ cache_previous;
static int cache_width, cache_height;
static uint32_t *cache_bits;

static void discard_cache(void)
{
    if (cache_bitmap) {
        SelectObject(cache_dc, cache_previous);
        DeleteObject(cache_bitmap);
        cache_bitmap = NULL;
    }
    cache_width = cache_height = 0;
    cache_bits = NULL;
}

static uint32_t *decode(const wchar_t *path, unsigned *width, unsigned *height)
{
    HRESULT initialized = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) return NULL;
    IWICImagingFactory *factory = NULL;
    IWICBitmapDecoder *decoder = NULL;
    IWICBitmapFrameDecode *frame = NULL;
    IWICFormatConverter *converter = NULL;
    uint32_t *result = NULL;
    UINT w = 0, h = 0;
    HRESULT hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                                  &IID_IWICImagingFactory, (void **)&factory);
    if (SUCCEEDED(hr))
        hr = IWICImagingFactory_CreateDecoderFromFilename(factory, path, NULL, GENERIC_READ,
                                                          WICDecodeMetadataCacheOnLoad, &decoder);
    if (SUCCEEDED(hr)) hr = IWICBitmapDecoder_GetFrame(decoder, 0, &frame);
    if (SUCCEEDED(hr)) hr = IWICBitmapFrameDecode_GetSize(frame, &w, &h);
    /* Bound allocation and the UINT byte count accepted by CopyPixels. */
    if (SUCCEEDED(hr) && (!w || !h || w > 32768 || h > 32768 ||
                         (uint64_t)w * h > 64u * 1024u * 1024u)) hr = E_INVALIDARG;
    if (SUCCEEDED(hr)) hr = IWICImagingFactory_CreateFormatConverter(factory, &converter);
    if (SUCCEEDED(hr))
        hr = IWICFormatConverter_Initialize(converter, (IWICBitmapSource *)frame,
                &GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, NULL, 0, WICBitmapPaletteTypeCustom);
    if (SUCCEEDED(hr)) {
        result = malloc((size_t)w * h * sizeof *result);
        if (!result) hr = E_OUTOFMEMORY;
    }
    if (SUCCEEDED(hr))
        hr = IWICFormatConverter_CopyPixels(converter, NULL, w * 4, w * h * 4, (BYTE *)result);
    /* Premultiplied RGB is already composited against black. Export it as
       opaque ARGB so every presentation backend uses the same pixels. */
    if (SUCCEEDED(hr))
        for (size_t i = 0; i < (size_t)w * h; ++i) result[i] |= 0xff000000u;
    if (converter) IWICFormatConverter_Release(converter);
    if (frame) IWICBitmapFrameDecode_Release(frame);
    if (decoder) IWICBitmapDecoder_Release(decoder);
    if (factory) IWICImagingFactory_Release(factory);
    if (SUCCEEDED(initialized)) CoUninitialize();
    if (FAILED(hr)) { free(result); return NULL; }
    *width = w;
    *height = h;
    return result;
}

int world_border_set(const char *path, unsigned brightness)
{
    if (!path || brightness > 100) return 0;
    if (!*path) {
        free(pixels);
        free(image_path);
        pixels = NULL;
        image_path = NULL;
        image_width = image_height = 0;
    } else if (!image_path || strcmp(path, image_path)) {
        int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
        if (!length) return 0;
        wchar_t *wide = malloc((size_t)length * sizeof *wide);
        char *new_path = malloc(strlen(path) + 1);
        if (!wide || !new_path) { free(wide); free(new_path); return 0; }
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, length);
        unsigned width, height;
        uint32_t *loaded = decode(wide, &width, &height);
        free(wide);
        if (!loaded) { free(new_path); return 0; }
        strcpy(new_path, path);
        free(pixels);
        free(image_path);
        pixels = loaded;
        image_path = new_path;
        image_width = width;
        image_height = height;
    }
    light = brightness;
    discard_cache();
    return 1;
}

const uint32_t *world_border_pixels(unsigned *width, unsigned *height, unsigned *brightness)
{
    if (width) *width = image_width;
    if (height) *height = image_height;
    if (brightness) *brightness = light;
    return pixels;
}

static int build_cache(int width, int height)
{
    discard_cache();
    if (!cache_dc) cache_dc = CreateCompatibleDC(NULL);
    if (!cache_dc || width <= 0 || height <= 0 ||
        (uint64_t)width * height > 64u * 1024u * 1024u) return 0;
    BITMAPINFO info = {0};
    info.bmiHeader.biSize = sizeof info.bmiHeader;
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    uint32_t *destination;
    cache_bitmap = CreateDIBSection(cache_dc, &info, DIB_RGB_COLORS, (void **)&destination, NULL, 0);
    if (!cache_bitmap) return 0;
    cache_previous = SelectObject(cache_dc, cache_bitmap);
    unsigned source_width = image_width, source_height = image_height;
    if ((uint64_t)source_width * height > (uint64_t)source_height * width)
        source_width = (unsigned)((uint64_t)source_height * width / height);
    else
        source_height = (unsigned)((uint64_t)source_width * height / width);
    if (!source_width) source_width = 1;
    if (!source_height) source_height = 1;
    unsigned x = (image_width - source_width) / 2, y = (image_height - source_height) / 2;
    info.bmiHeader.biWidth = image_width;
    info.bmiHeader.biHeight = -(int)image_height;
    SetStretchBltMode(cache_dc, HALFTONE);
    SetBrushOrgEx(cache_dc, 0, 0, NULL);
    int copied = StretchDIBits(cache_dc, 0, 0, width, height, x, y, source_width, source_height,
                               pixels, &info, DIB_RGB_COLORS, SRCCOPY);
    if (!copied || copied == (int)GDI_ERROR) { discard_cache(); return 0; }
    GdiFlush();
    for (size_t i = 0; i < (size_t)width * height; ++i) {
        uint32_t c = destination[i];
        unsigned b = (c & 255) * light / 100;
        unsigned g = ((c >> 8) & 255) * light / 100;
        unsigned r = ((c >> 16) & 255) * light / 100;
        destination[i] = 0xff000000u | (r << 16) | (g << 8) | b;
    }
    cache_width = width;
    cache_height = height;
    cache_bits = destination;
    return 1;
}

void world_border_paint(HDC target, int width, int height)
{
    if (width <= 0 || height <= 0) return;
    if (pixels && light && ((cache_width == width && cache_height == height) || build_cache(width, height))) {
        BitBlt(target, 0, 0, width, height, cache_dc, 0, 0, SRCCOPY);
    } else {
        PatBlt(target, 0, 0, width, height, BLACKNESS);
    }
}

const uint32_t *world_border_scaled(int width, int height)
{
    if (!pixels || !light || width <= 0 || height <= 0) return NULL;
    if ((cache_width != width || cache_height != height) && !build_cache(width, height)) return NULL;
    return cache_bits;
}

void world_border_close(void)
{
    discard_cache();
    if (cache_dc) DeleteDC(cache_dc);
    cache_dc = NULL;
    free(pixels);
    free(image_path);
    pixels = NULL;
    image_path = NULL;
    image_width = image_height = 0;
    light = 100;
}
