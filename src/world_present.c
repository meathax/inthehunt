#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <stdio.h>
#include <string.h>
#include "world_present.h"

enum { WIDTH = 320, HEIGHT = 240, CONSTANT_BYTES = 256 };

static struct {
    HWND window;
    ID3D11Device *device;
    ID3D11DeviceContext *context;
    IDXGISwapChain *swap;
    ID3D11Texture2D *surface;
    ID3D11RenderTargetView *target;
    ID3D11Texture2D *frame;
    ID3D11ShaderResourceView *frame_view;
    ID3D11Texture2D *background;
    ID3D11ShaderResourceView *background_view;
    ID3D11Texture2D *capture;
    ID3D11VertexShader *vertex;
    ID3D11PixelShader *point;
    ID3D11PixelShader *effect;
    ID3D11PixelShader *backdrop;
    ID3D11SamplerState *nearest;
    ID3D11SamplerState *linear;
    ID3D11Buffer *effect_constants;
    ID3D11Buffer *uv_constants;
    ID3D11Buffer *backdrop_constants;
    unsigned width, height, background_width, background_height, brightness;
    int capture_valid;
    WorldPresentViewport viewport;
} renderer;
static char last_error[1024];

static const char vertex_source[] =
    "cbuffer Geometry:register(b1){float4 UVTransform;}"
    "struct Vertex{float4 position:SV_Position;float2 uv:TEXCOORD0;};"
    "Vertex main(uint id:SV_VertexID){"
    "float2 uv=float2((id<<1)&2,id&2);Vertex v;"
    "v.position=float4(uv*float2(2,-2)+float2(-1,1),0,1);"
    "v.uv=uv*UVTransform.xy+UVTransform.zw;return v;}";
static const char point_source[] =
    "Texture2D Picture:register(t0);SamplerState Nearest:register(s0);"
    "float4 main(float4 p:SV_Position,float2 uv:TEXCOORD0):SV_Target{"
    "return float4(Picture.Sample(Nearest,uv).rgb,1);}";
static const char backdrop_source[] =
    "Texture2D Picture:register(t0);SamplerState Linear:register(s0);"
    "cbuffer Brightness:register(b0){float4 Gain;}"
    "float4 main(float4 p:SV_Position,float2 uv:TEXCOORD0):SV_Target{"
    "float4 c=Picture.Sample(Linear,uv);return float4(c.rgb*c.a*Gain.x,1);}";

#define RELEASE(type, object) do { if (object) { type##_Release(object); object = NULL; } } while (0)

static int fail(const char *operation, HRESULT result)
{
    snprintf(last_error, sizeof last_error, "%s failed (0x%08lx).",
             operation, (unsigned long)result);
    return 0;
}

static ID3DBlob *compile(const char *source, const char *entry, const char *profile)
{
    ID3DBlob *code = NULL, *errors = NULL;
    HRESULT result = D3DCompile(source, strlen(source), NULL, NULL, NULL,
                               entry, profile, D3DCOMPILE_ENABLE_STRICTNESS, 0,
                               &code, &errors);
    if (FAILED(result)) {
        if (errors) {
            size_t length = ID3D10Blob_GetBufferSize(errors);
            if (length >= sizeof last_error) length = sizeof last_error - 1;
            memcpy(last_error, ID3D10Blob_GetBufferPointer(errors), length);
            last_error[length] = 0;
        } else fail("Shader compilation", result);
    }
    RELEASE(ID3D10Blob, errors);
    return code;
}

static int pixel_shader(const char *source, const char *entry, ID3D11PixelShader **shader)
{
    ID3DBlob *code = compile(source, entry, "ps_4_0");
    if (!code) return 0;
    HRESULT result = ID3D11Device_CreatePixelShader(renderer.device,
        ID3D10Blob_GetBufferPointer(code), ID3D10Blob_GetBufferSize(code), NULL, shader);
    RELEASE(ID3D10Blob, code);
    return SUCCEEDED(result) || fail("Pixel shader creation", result);
}

static int constant_buffer(unsigned size, ID3D11Buffer **buffer)
{
    D3D11_BUFFER_DESC desc = {0};
    desc.ByteWidth = size;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    HRESULT result = ID3D11Device_CreateBuffer(renderer.device, &desc, NULL, buffer);
    return SUCCEEDED(result) || fail("Constant buffer creation", result);
}

static int image(const uint32_t *pixels, unsigned width, unsigned height,
                  ID3D11Texture2D **texture, ID3D11ShaderResourceView **view)
{
    D3D11_TEXTURE2D_DESC desc = {0};
    desc.Width = width; desc.Height = height;
    desc.MipLevels = desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = pixels ? D3D11_USAGE_IMMUTABLE : D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data = {0};
    data.pSysMem = pixels;
    data.SysMemPitch = width * sizeof *pixels;
    HRESULT result = ID3D11Device_CreateTexture2D(renderer.device, &desc, pixels ? &data : NULL, texture);
    if (FAILED(result)) return fail("Image texture creation", result);
    result = ID3D11Device_CreateShaderResourceView(renderer.device, (ID3D11Resource *)*texture, NULL, view);
    if (FAILED(result)) {
        RELEASE(ID3D11Texture2D, *texture);
        return fail("Image view creation", result);
    }
    return 1;
}

static void release_surface(void)
{
    ID3D11DeviceContext_OMSetRenderTargets(renderer.context, 0, NULL, NULL);
    RELEASE(ID3D11RenderTargetView, renderer.target);
    RELEASE(ID3D11Texture2D, renderer.surface);
    RELEASE(ID3D11Texture2D, renderer.capture);
    renderer.capture_valid = 0;
}

static int resize(unsigned width, unsigned height)
{
    if (renderer.width == width && renderer.height == height && renderer.target) return 1;
    release_surface();
    HRESULT result = IDXGISwapChain_ResizeBuffers(renderer.swap, 0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(result)) return fail("Swapchain resize", result);
    result = IDXGISwapChain_GetBuffer(renderer.swap, 0, &IID_ID3D11Texture2D, (void **)&renderer.surface);
    if (FAILED(result)) return fail("Swapchain image retrieval", result);
    result = ID3D11Device_CreateRenderTargetView(renderer.device, (ID3D11Resource *)renderer.surface, NULL, &renderer.target);
    if (FAILED(result)) return fail("Render target creation", result);
    renderer.width = width;
    renderer.height = height;
    return 1;
}

void world_present_close(void)
{
    if (renderer.context) {
        ID3D11DeviceContext_ClearState(renderer.context);
        ID3D11DeviceContext_Flush(renderer.context);
    }
    RELEASE(ID3D11Texture2D, renderer.capture);
    RELEASE(ID3D11RenderTargetView, renderer.target);
    RELEASE(ID3D11Texture2D, renderer.surface);
    RELEASE(ID3D11ShaderResourceView, renderer.frame_view);
    RELEASE(ID3D11Texture2D, renderer.frame);
    RELEASE(ID3D11ShaderResourceView, renderer.background_view);
    RELEASE(ID3D11Texture2D, renderer.background);
    RELEASE(ID3D11VertexShader, renderer.vertex);
    RELEASE(ID3D11PixelShader, renderer.point);
    RELEASE(ID3D11PixelShader, renderer.effect);
    RELEASE(ID3D11PixelShader, renderer.backdrop);
    RELEASE(ID3D11SamplerState, renderer.nearest);
    RELEASE(ID3D11SamplerState, renderer.linear);
    RELEASE(ID3D11Buffer, renderer.effect_constants);
    RELEASE(ID3D11Buffer, renderer.uv_constants);
    RELEASE(ID3D11Buffer, renderer.backdrop_constants);
    RELEASE(IDXGISwapChain, renderer.swap);
    RELEASE(ID3D11DeviceContext, renderer.context);
    RELEASE(ID3D11Device, renderer.device);
    memset(&renderer, 0, sizeof renderer);
}

int world_present_open(void *native_window)
{
    world_present_close();
    last_error[0] = 0;
    renderer.window = native_window;
    renderer.brightness = 100;
    DXGI_SWAP_CHAIN_DESC desc = {0};
    desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.OutputWindow = renderer.window;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    HRESULT result = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, sizeof levels / sizeof levels[0],
        D3D11_SDK_VERSION, &desc, &renderer.swap, &renderer.device, NULL, &renderer.context);
    if (FAILED(result))
        result = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_WARP, NULL,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, sizeof levels / sizeof levels[0],
            D3D11_SDK_VERSION, &desc, &renderer.swap, &renderer.device, NULL, &renderer.context);
    if (FAILED(result)) { fail("D3D11 device creation", result); goto error; }

    IDXGIFactory *factory = NULL;
    if (SUCCEEDED(IDXGISwapChain_GetParent(renderer.swap, &IID_IDXGIFactory, (void **)&factory))) {
        IDXGIFactory_MakeWindowAssociation(factory, renderer.window, DXGI_MWA_NO_ALT_ENTER);
        RELEASE(IDXGIFactory, factory);
    }
    ID3DBlob *code = compile(vertex_source, "main", "vs_4_0");
    if (!code) goto error;
    result = ID3D11Device_CreateVertexShader(renderer.device,
        ID3D10Blob_GetBufferPointer(code), ID3D10Blob_GetBufferSize(code), NULL, &renderer.vertex);
    RELEASE(ID3D10Blob, code);
    if (FAILED(result)) { fail("Vertex shader creation", result); goto error; }
    if (!pixel_shader(point_source, "main", &renderer.point) ||
        !pixel_shader(backdrop_source, "main", &renderer.backdrop) ||
        !image(NULL, WIDTH, HEIGHT, &renderer.frame, &renderer.frame_view) ||
        !constant_buffer(CONSTANT_BYTES, &renderer.effect_constants) ||
        !constant_buffer(16, &renderer.uv_constants) ||
        !constant_buffer(16, &renderer.backdrop_constants)) goto error;

    D3D11_SAMPLER_DESC sampler = {0};
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    result = ID3D11Device_CreateSamplerState(renderer.device, &sampler, &renderer.nearest);
    if (FAILED(result)) { fail("Point sampler creation", result); goto error; }
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    result = ID3D11Device_CreateSamplerState(renderer.device, &sampler, &renderer.linear);
    if (FAILED(result)) { fail("Background sampler creation", result); goto error; }
    RECT client;
    GetClientRect(renderer.window, &client);
    if (!resize(client.right > 0 ? client.right : WIDTH, client.bottom > 0 ? client.bottom : HEIGHT)) goto error;
    return 1;
error:
    world_present_close();
    return 0;
}

int world_present_set_constants(const void *constants, size_t size)
{
    if (!renderer.device || size > CONSTANT_BYTES || (size && !constants)) return 0;
    uint8_t data[CONSTANT_BYTES] = {0};
    if (size) memcpy(data, constants, size);
    ID3D11DeviceContext_UpdateSubresource(renderer.context, (ID3D11Resource *)renderer.effect_constants,
                                        0, NULL, data, 0, 0);
    return 1;
}

int world_present_set_shader(const char *source, const char *entry, const void *constants, size_t size)
{
    if (!renderer.device || size > CONSTANT_BYTES || (size && !constants)) return 0;
    ID3D11PixelShader *next = NULL;
    if (source && !pixel_shader(source, entry ? entry : "main", &next)) return 0;
    if (!world_present_set_constants(constants, size)) {
        RELEASE(ID3D11PixelShader, next);
        return 0;
    }
    RELEASE(ID3D11PixelShader, renderer.effect);
    renderer.effect = next;
    return 1;
}

int world_present_set_shader_file(const wchar_t *path, const char *entry,
                                 const void *constants, size_t size)
{
    if (!path || !renderer.device) return 0;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return fail("Shader file opening", HRESULT_FROM_WIN32(GetLastError()));
    LARGE_INTEGER length;
    if (!GetFileSizeEx(file, &length) || length.QuadPart <= 0 || length.QuadPart > MAXDWORD - 1) {
        CloseHandle(file);
        return fail("Shader file size query", E_INVALIDARG);
    }
    char *source = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)length.QuadPart + 1);
    if (!source) { CloseHandle(file); return fail("Shader file allocation", E_OUTOFMEMORY); }
    DWORD read = 0;
    int ok = ReadFile(file, source, (DWORD)length.QuadPart, &read, NULL) && read == length.QuadPart;
    HRESULT result = ok ? S_OK : HRESULT_FROM_WIN32(GetLastError());
    CloseHandle(file);
    if (ok) {
        source[read] = 0;
        ok = world_present_set_shader(source, entry, constants, size);
    } else fail("Shader file reading", result);
    HeapFree(GetProcessHeap(), 0, source);
    return ok;
}

int world_present_set_background(const uint32_t *pixels, unsigned width, unsigned height, unsigned brightness)
{
    if (!renderer.device || (pixels && (!width || !height || width > 16384 || height > 16384))) return 0;
    ID3D11Texture2D *next = NULL;
    ID3D11ShaderResourceView *view = NULL;
    if (pixels && !image(pixels, width, height, &next, &view)) return 0;
    RELEASE(ID3D11ShaderResourceView, renderer.background_view);
    RELEASE(ID3D11Texture2D, renderer.background);
    renderer.background = next;
    renderer.background_view = view;
    renderer.background_width = width;
    renderer.background_height = height;
    world_present_set_background_brightness(brightness);
    return 1;
}

void world_present_set_background_brightness(unsigned brightness)
{
    renderer.brightness = brightness > 100 ? 100 : brightness;
}

static void draw_image(ID3D11ShaderResourceView *image_view, ID3D11PixelShader *shader,
                       ID3D11SamplerState *sampler, ID3D11Buffer *constants,
                       const D3D11_VIEWPORT *viewport, const float uv[4])
{
    ID3D11DeviceContext_UpdateSubresource(renderer.context, (ID3D11Resource *)renderer.uv_constants, 0, NULL, uv, 0, 0);
    ID3D11DeviceContext_RSSetViewports(renderer.context, 1, viewport);
    ID3D11DeviceContext_PSSetShader(renderer.context, shader, NULL, 0);
    ID3D11DeviceContext_PSSetConstantBuffers(renderer.context, 0, 1, &constants);
    ID3D11DeviceContext_PSSetSamplers(renderer.context, 0, 1, &sampler);
    ID3D11DeviceContext_PSSetShaderResources(renderer.context, 0, 1, &image_view);
    ID3D11DeviceContext_Draw(renderer.context, 3, 0);
}

int world_present_draw(const uint32_t pixels[WIDTH * HEIGHT], int vsync)
{
    if (!renderer.device || !pixels) return 0;
    RECT client;
    if (!GetClientRect(renderer.window, &client)) return fail("Window size query", HRESULT_FROM_WIN32(GetLastError()));
    if (client.right <= 0 || client.bottom <= 0 || IsIconic(renderer.window)) return 1;
    if (!resize(client.right, client.bottom)) return 0;
    ID3D11DeviceContext_UpdateSubresource(renderer.context, (ID3D11Resource *)renderer.frame, 0, NULL, pixels, WIDTH*4, 0);
    ID3D11DeviceContext_OMSetRenderTargets(renderer.context, 1, &renderer.target, NULL);
    const float black[4] = {0, 0, 0, 1};
    ID3D11DeviceContext_ClearRenderTargetView(renderer.context, renderer.target, black);
    ID3D11DeviceContext_IASetPrimitiveTopology(renderer.context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_IASetInputLayout(renderer.context, NULL);
    ID3D11DeviceContext_VSSetShader(renderer.context, renderer.vertex, NULL, 0);
    ID3D11DeviceContext_VSSetConstantBuffers(renderer.context, 1, 1, &renderer.uv_constants);
    if (renderer.background_view && renderer.brightness) {
        float uv[4] = {1, 1, 0, 0};
        double picture_aspect = (double)renderer.background_width / renderer.background_height;
        double surface_aspect = (double)renderer.width / renderer.height;
        if (picture_aspect > surface_aspect) uv[0] = (float)(surface_aspect / picture_aspect);
        else uv[1] = (float)(picture_aspect / surface_aspect);
        uv[2] = (1-uv[0])/2; uv[3] = (1-uv[1])/2;
        float gain[4] = {renderer.brightness / 100.0f, 0, 0, 0};
        ID3D11DeviceContext_UpdateSubresource(renderer.context, (ID3D11Resource *)renderer.backdrop_constants, 0, NULL, gain, 0, 0);
        D3D11_VIEWPORT full = {0, 0, (float)renderer.width, (float)renderer.height, 0, 1};
        draw_image(renderer.background_view, renderer.backdrop, renderer.linear, renderer.backdrop_constants, &full, uv);
    }
    int scale = (int)(renderer.width / WIDTH);
    if (scale > (int)(renderer.height / HEIGHT)) scale = renderer.height / HEIGHT;
    if (scale < 1) scale = 1;
    WorldPresentViewport *v = &renderer.viewport;
    v->surface_width = renderer.width; v->surface_height = renderer.height;
    v->width = WIDTH*scale; v->height = HEIGHT*scale; v->scale = scale;
    v->x = ((int)renderer.width-v->width)/2; v->y = ((int)renderer.height-v->height)/2;
    D3D11_VIEWPORT game = {(float)v->x, (float)v->y, (float)v->width, (float)v->height, 0, 1};
    const float uv[4] = {1, 1, 0, 0};
    draw_image(renderer.frame_view, renderer.effect ? renderer.effect : renderer.point,
               renderer.nearest, renderer.effect_constants, &game, uv);
    /* Keep a GPU copy before flip-discard invalidates the displayed surface.
       It is allocated lazily by readback, so normal presentation has no copy. */
    if (renderer.capture) {
        ID3D11DeviceContext_CopyResource(renderer.context, (ID3D11Resource *)renderer.capture, (ID3D11Resource *)renderer.surface);
        renderer.capture_valid = 1;
    }
    HRESULT result = IDXGISwapChain_Present(renderer.swap, vsync ? 1 : 0, 0);
    return SUCCEEDED(result) || fail("Frame presentation", result);
}

void world_present_viewport(WorldPresentViewport *viewport)
{
    if (viewport) *viewport = renderer.viewport;
}

const char *world_present_error(void) { return last_error; }

int world_present_request_readback(void)
{
    if (!renderer.device || !renderer.surface) return 0;
    if (!renderer.capture) {
        D3D11_TEXTURE2D_DESC desc;
        ID3D11Texture2D_GetDesc(renderer.surface, &desc);
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = desc.MiscFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        HRESULT result = ID3D11Device_CreateTexture2D(renderer.device, &desc, NULL, &renderer.capture);
        if (FAILED(result)) return fail("Screenshot texture creation", result);
    }
    renderer.capture_valid = 0;
    return 1;
}

int world_present_readback(uint32_t *pixels, size_t capacity, unsigned *width, unsigned *height)
{
    if (!renderer.device || !pixels || capacity < (size_t)renderer.width*renderer.height) return 0;
    if (!renderer.capture_valid) return 0;
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT result = ID3D11DeviceContext_Map(renderer.context, (ID3D11Resource *)renderer.capture, 0,
                                            D3D11_MAP_READ, 0, &mapped);
    if (FAILED(result)) return fail("Screenshot readback", result);
    for (unsigned row = 0; row < renderer.height; ++row)
        memcpy(pixels + (size_t)row*renderer.width, (const uint8_t *)mapped.pData + (size_t)row*mapped.RowPitch, renderer.width*4);
    ID3D11DeviceContext_Unmap(renderer.context, (ID3D11Resource *)renderer.capture, 0);
    if (width) *width = renderer.width;
    if (height) *height = renderer.height;
    return 1;
}
