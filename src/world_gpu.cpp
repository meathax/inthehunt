// Direct3D 12 presenter for the CRT shaders; see world_gpu.h.
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>

#include "world_gpu.h"
#include "post/post_chain.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

using gdl::post::PostChain;

const char *const world_gpu_preset_name[WORLD_GPU_PRESETS] = {
    "CRT-ROYALE", "ROYALE PVM", "GUEST ADVANCED", "GUEST NTSC", "CRT-GEOM", "CRT-LOTTES",
    "CRT-HYLLIAN", "EASYMODE", "NEWPIXIE", "CONSUMER TV", "CALIGARI", "ZFAST"};
const char *const world_gpu_preset_path[WORLD_GPU_PRESETS] = {
    "crt/crt-royale.slangp", "presets/crt-royale-kurozumi.slangp", "crt/crt-guest-advanced.slangp",
    "crt/crt-guest-advanced-ntsc.slangp", "crt/crt-geom.slangp", "crt/crt-lottes.slangp",
    "crt/crt-hyllian.slangp", "crt/crt-easymode.slangp", "crt/newpixie-crt.slangp",
    "crt/crt-consumer.slangp", "crt/crt-caligari.slangp", "crt/zfast-crt.slangp"};

namespace {

template <class T> struct Com {
    T *p = nullptr;
    Com() = default;
    Com(const Com &) = delete;
    Com &operator=(const Com &) = delete;
    ~Com() { reset(); }
    void reset() { if (p) { p->Release(); p = nullptr; } }
    T **put() { reset(); return &p; }
    T *operator->() const { return p; }
    T *get() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

enum { W = 320, H = 240, FRAMES = 3, BUFFERS = 3 };

void say(char *error, size_t size, const char *text)
{
    if (error && size) snprintf(error, size, "%s", text);
}

std::wstring widen(const char *text)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, text, -1, &w[0], n);
    return w;
}

D3D12_RESOURCE_BARRIER barrier(ID3D12Resource *r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to)
{
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = r;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = from;
    b.Transition.StateAfter = to;
    return b;
}

HRESULT buffer(ID3D12Device *device, D3D12_HEAP_TYPE heap, UINT64 bytes, D3D12_RESOURCE_STATES state, ID3D12Resource **out)
{
    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = heap;
    D3D12_RESOURCE_DESC d = {};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width = bytes;
    d.Height = 1;
    d.DepthOrArraySize = 1;
    d.MipLevels = 1;
    d.SampleDesc.Count = 1;
    d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, state, nullptr, IID_PPV_ARGS(out));
}

HRESULT texture(ID3D12Device *device, UINT w, UINT h, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state,
                ID3D12Resource **out)
{
    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC d = {};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width = w;
    d.Height = h;
    d.DepthOrArraySize = 1;
    d.MipLevels = 1;
    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.Flags = flags;
    return device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, state, nullptr, IID_PPV_ARGS(out));
}

// Copies a tightly packed ARGB image into an upload buffer laid out for
// CopyTextureRegion, then records the copy into `dst` (in COPY_DEST).
void upload(ID3D12Device *device, ID3D12GraphicsCommandList *cmd, ID3D12Resource *staging, ID3D12Resource *dst,
            const uint32_t *pixels, UINT w, UINT h)
{
    D3D12_RESOURCE_DESC desc = dst->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp;
    device->GetCopyableFootprints(&desc, 0, 1, 0, &fp, nullptr, nullptr, nullptr);
    uint8_t *map = nullptr;
    D3D12_RANGE none = {0, 0};
    if (FAILED(staging->Map(0, &none, reinterpret_cast<void **>(&map)))) return;
    for (UINT y = 0; y < h; ++y) memcpy(map + fp.Offset + (size_t)y * fp.Footprint.RowPitch, pixels + (size_t)y * w, (size_t)w * 4);
    staging->Unmap(0, nullptr);
    D3D12_TEXTURE_COPY_LOCATION from = {}, to = {};
    from.pResource = staging;
    from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    from.PlacedFootprint = fp;
    to.pResource = dst;
    to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    cmd->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
}

UINT64 upload_size(ID3D12Device *device, ID3D12Resource *dst)
{
    D3D12_RESOURCE_DESC desc = dst->GetDesc();
    UINT64 total = 0;
    device->GetCopyableFootprints(&desc, 0, 1, 0, nullptr, nullptr, nullptr, &total);
    return total;
}

void apply(PostChain &chain, int scaling, int beam, UINT out_w, UINT out_h)
{
    gdl::post::Settings &s = chain.settings();
    s.source_mode = gdl::post::SourceMode::AsIs;
    s.base_width = W;
    s.base_height = H;
    s.base_aspect = 4.0f / 3.0f;
    s.integer_scale = scaling == 0;
    s.display_aspect = scaling == 2 && out_h ? (float)out_w / (float)out_h : 4.0f / 3.0f;
    s.passthrough_filter = gdl::post::PassThroughFilter::Nearest;
    s.keep_background = true;
    s.auto_progressive_fix = true;
    s.beam.enabled = beam != 0;
    s.source_fps = 60.0125f;
}

// ---- the window presenter ------------------------------------------------

struct Presenter {
    HWND parent = nullptr, child = nullptr;
    Com<IDXGIFactory4> factory;
    Com<ID3D12Device> device;
    Com<ID3D12CommandQueue> queue;
    Com<IDXGISwapChain3> swap;
    Com<ID3D12DescriptorHeap> rtv_heap;
    UINT rtv_step = 0;
    Com<ID3D12Resource> back[BUFFERS];
    Com<ID3D12CommandAllocator> alloc[FRAMES];
    Com<ID3D12GraphicsCommandList> cmd;
    Com<ID3D12Fence> fence;
    HANDLE event = nullptr;
    UINT64 fence_value = 0, frame_fence[FRAMES] = {};
    UINT frame_index = 0;
    Com<ID3D12Resource> picture, picture_upload[FRAMES];
    Com<ID3D12Resource> bg, bg_upload;
    bool bg_dirty = false, has_bg = false;
    std::unique_ptr<uint32_t[]> bg_pixels;
    UINT bg_w = 0, bg_h = 0;
    UINT width = 0, height = 0;
    bool tearing = false;
    int scaling = 0, beam = 0, vsync = 1;
    std::unique_ptr<PostChain> chain;
    LARGE_INTEGER last_present = {};
    double refresh_hz = 0.0;
    double last_source_time = 0.0;

    void wait_all()
    {
        if (!queue || !fence) return;
        ++fence_value;
        queue->Signal(fence.get(), fence_value);
        if (fence->GetCompletedValue() < fence_value) {
            fence->SetEventOnCompletion(fence_value, event);
            WaitForSingleObject(event, 2000);
        }
    }

    void release_buffers()
    {
        for (auto &b : back) b.reset();
    }

    bool buffers()
    {
        for (UINT i = 0; i < BUFFERS; ++i) {
            if (FAILED(swap->GetBuffer(i, IID_PPV_ARGS(back[i].put())))) return false;
            D3D12_CPU_DESCRIPTOR_HANDLE h = rtv_heap->GetCPUDescriptorHandleForHeapStart();
            h.ptr += (SIZE_T)i * rtv_step;
            device->CreateRenderTargetView(back[i].get(), nullptr, h);
        }
        return true;
    }

    void resize(UINT w, UINT h)
    {
        if (!w || !h || (w == width && h == height)) return;
        wait_all();
        release_buffers();
        swap->ResizeBuffers(BUFFERS, w, h, DXGI_FORMAT_B8G8R8A8_UNORM,
                            tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0);
        width = w;
        height = h;
        buffers();
        bg_dirty = true;
        if (chain) apply(*chain, scaling, beam, width, height);
    }
};

Presenter *gpu;

LRESULT CALLBACK child_proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp)
{
    switch (message) {
    case WM_NCHITTEST: return HTTRANSPARENT;              // clicks belong to the game window
    case WM_SETFOCUS: SetFocus(GetParent(hwnd)); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(hwnd, &ps); EndPaint(hwnd, &ps); return 0; }
    default: return DefWindowProcW(hwnd, message, wp, lp);
    }
}

bool create_device(Com<IDXGIFactory4> &factory, Com<ID3D12Device> &device, Com<ID3D12CommandQueue> &queue,
                   char *error, size_t size)
{
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(factory.put())))) { say(error, size, "DXGI is unavailable."); return false; }
    if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(device.put())))) {
        say(error, size, "Direct3D 12 is unavailable on this PC.");
        return false;
    }
    D3D12_COMMAND_QUEUE_DESC q = {};
    q.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(device->CreateCommandQueue(&q, IID_PPV_ARGS(queue.put())))) { say(error, size, "No command queue."); return false; }
    return true;
}

}  // namespace

extern "C" int world_gpu_find_shaders(const char *data_dir, char *out, size_t size)
{
    wchar_t exe[MAX_PATH];
    char base[MAX_PATH * 3];
    DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    if (n && n < MAX_PATH) {
        wchar_t *slash = wcsrchr(exe, L'\\');
        if (slash) *slash = 0;
        WideCharToMultiByte(CP_UTF8, 0, exe, -1, base, sizeof base, nullptr, nullptr);
    } else
        base[0] = 0;
    const char *candidates[3][2] = {{base, "/shaders"}, {data_dir ? data_dir : "", "/../shaders"}, {base, "/../shaders"}};
    for (auto &c : candidates) {
        if (!*c[0]) continue;
        char probe[MAX_PATH * 3];
        snprintf(probe, sizeof probe, "%s%s/SOURCES.md", c[0], c[1]);
        if (GetFileAttributesW(widen(probe).c_str()) != INVALID_FILE_ATTRIBUTES) {
            snprintf(out, size, "%s%s", c[0], c[1]);
            return 1;
        }
    }
    return 0;
}

extern "C" int world_gpu_open(void *parent_window, char *error, size_t error_size)
{
    if (gpu) return 1;
    auto p = std::make_unique<Presenter>();
    p->parent = (HWND)parent_window;
    if (!create_device(p->factory, p->device, p->queue, error, error_size)) return 0;
    BOOL allow = FALSE;
    Com<IDXGIFactory5> f5;
    if (SUCCEEDED(p->factory->QueryInterface(IID_PPV_ARGS(f5.put()))) &&
        SUCCEEDED(f5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow, sizeof allow)))
        p->tearing = allow != FALSE;

    static bool registered;
    if (!registered) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = child_proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"InTheHuntGpuView";
        RegisterClassW(&wc);
        registered = true;
    }
    RECT rc;
    GetClientRect(p->parent, &rc);
    p->width = rc.right > 0 ? (UINT)rc.right : 1;
    p->height = rc.bottom > 0 ? (UINT)rc.bottom : 1;
    p->child = CreateWindowExW(0, L"InTheHuntGpuView", L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0,
                               (int)p->width, (int)p->height, p->parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!p->child) { say(error, error_size, "Could not create the shader view."); return 0; }

    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width = p->width;
    sd.Height = p->height;
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = BUFFERS;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.Flags = p->tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
    Com<IDXGISwapChain1> swap1;
    if (FAILED(p->factory->CreateSwapChainForHwnd(p->queue.get(), p->child, &sd, nullptr, nullptr, swap1.put())) ||
        FAILED(swap1->QueryInterface(IID_PPV_ARGS(p->swap.put())))) {
        DestroyWindow(p->child);
        say(error, error_size, "Could not create the swap chain.");
        return 0;
    }
    p->factory->MakeWindowAssociation(p->child, DXGI_MWA_NO_ALT_ENTER);
    D3D12_DESCRIPTOR_HEAP_DESC hd = {};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hd.NumDescriptors = BUFFERS;
    p->device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(p->rtv_heap.put()));
    p->rtv_step = p->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    for (auto &a : p->alloc)
        if (FAILED(p->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(a.put())))) {
            DestroyWindow(p->child);
            say(error, error_size, "Could not create command allocators.");
            return 0;
        }
    p->device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, p->alloc[0].get(), nullptr, IID_PPV_ARGS(p->cmd.put()));
    p->cmd->Close();
    p->device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(p->fence.put()));
    p->event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!p->rtv_heap || !p->cmd || !p->fence || !p->event || !p->buffers() ||
        FAILED(texture(p->device.get(), W, H, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, p->picture.put()))) {
        DestroyWindow(p->child);
        say(error, error_size, "Could not create the presentation resources.");
        return 0;
    }
    UINT64 bytes = upload_size(p->device.get(), p->picture.get());
    for (auto &u : p->picture_upload)
        buffer(p->device.get(), D3D12_HEAP_TYPE_UPLOAD, bytes, D3D12_RESOURCE_STATE_GENERIC_READ, u.put());
    try {
        p->chain = std::make_unique<PostChain>(p->device.get(), p->queue.get());
    } catch (const std::exception &e) {
        DestroyWindow(p->child);
        say(error, error_size, e.what());
        return 0;
    }
    apply(*p->chain, p->scaling, p->beam, p->width, p->height);
    gpu = p.release();
    return 1;
}

extern "C" void world_gpu_close(void)
{
    if (!gpu) return;
    gpu->wait_all();
    gpu->chain.reset();
    HWND child = gpu->child;
    if (gpu->event) CloseHandle(gpu->event);
    delete gpu;
    gpu = nullptr;
    if (child) DestroyWindow(child);
}

extern "C" int world_gpu_active(void) { return gpu != nullptr; }

extern "C" int world_gpu_set_preset(const char *path, char *error, size_t error_size)
{
    if (!gpu) { say(error, error_size, "The shader view is not open."); return 0; }
    std::string why;
    gpu->wait_all();
    if (!gpu->chain->set_preset(path ? path : "", &why)) {
        say(error, error_size, why.c_str());
        return 0;
    }
    apply(*gpu->chain, gpu->scaling, gpu->beam, gpu->width, gpu->height);
    return 1;
}

extern "C" void world_gpu_set_options(int scaling, int beam, int vsync)
{
    if (!gpu) return;
    gpu->scaling = scaling;
    gpu->beam = beam;
    gpu->vsync = vsync;
    apply(*gpu->chain, scaling, beam, gpu->width, gpu->height);
}

extern "C" void world_gpu_set_background(const uint32_t *pixels, unsigned width, unsigned height)
{
    if (!gpu) return;
    gpu->has_bg = pixels && width && height;
    if (gpu->has_bg) {
        gpu->bg_pixels.reset(new uint32_t[(size_t)width * height]);
        memcpy(gpu->bg_pixels.get(), pixels, (size_t)width * height * 4);
        gpu->bg_w = width;
        gpu->bg_h = height;
    }
    gpu->bg_dirty = true;
}

extern "C" double world_gpu_refresh_hz(void) { return gpu ? gpu->refresh_hz : 0.0; }

extern "C" void world_gpu_present(const uint32_t *frame, double source_time)
{
    Presenter *p = gpu;
    if (!p) return;
    RECT rc;
    GetClientRect(p->parent, &rc);
    if (rc.right <= 0 || rc.bottom <= 0) return;
    if ((UINT)rc.right != p->width || (UINT)rc.bottom != p->height) {
        MoveWindow(p->child, 0, 0, rc.right, rc.bottom, FALSE);
        p->resize((UINT)rc.right, (UINT)rc.bottom);
    }
    UINT f = p->frame_index;
    if (p->fence->GetCompletedValue() < p->frame_fence[f]) {
        p->fence->SetEventOnCompletion(p->frame_fence[f], p->event);
        WaitForSingleObject(p->event, 1000);
    }
    p->alloc[f]->Reset();
    ID3D12GraphicsCommandList *cmd = p->cmd.get();
    cmd->Reset(p->alloc[f].get(), nullptr);

    UINT b = p->swap->GetCurrentBackBufferIndex();
    ID3D12Resource *target = p->back[b].get();
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = p->rtv_heap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += (SIZE_T)b * p->rtv_step;

    // Background: the border art at client size, or black.
    if (p->bg_dirty) {
        p->bg.reset();
        p->bg_upload.reset();
        if (p->has_bg && p->bg_w == p->width && p->bg_h == p->height &&
            SUCCEEDED(texture(p->device.get(), p->width, p->height, D3D12_RESOURCE_FLAG_NONE,
                              D3D12_RESOURCE_STATE_COPY_DEST, p->bg.put())) &&
            SUCCEEDED(buffer(p->device.get(), D3D12_HEAP_TYPE_UPLOAD, upload_size(p->device.get(), p->bg.get()),
                             D3D12_RESOURCE_STATE_GENERIC_READ, p->bg_upload.put()))) {
            upload(p->device.get(), cmd, p->bg_upload.get(), p->bg.get(), p->bg_pixels.get(), p->width, p->height);
            D3D12_RESOURCE_BARRIER t = barrier(p->bg.get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
            cmd->ResourceBarrier(1, &t);
        } else
            p->bg.reset();
        p->bg_dirty = false;
    }
    if (p->bg) {
        D3D12_RESOURCE_BARRIER t = barrier(target, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST);
        cmd->ResourceBarrier(1, &t);
        cmd->CopyResource(target, p->bg.get());
        t = barrier(target, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET);
        cmd->ResourceBarrier(1, &t);
    } else {
        D3D12_RESOURCE_BARRIER t = barrier(target, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
        cmd->ResourceBarrier(1, &t);
        const float black[4] = {0, 0, 0, 1};
        cmd->ClearRenderTargetView(rtv, black, 0, nullptr);
    }

    ID3D12Resource *fresh = nullptr;
    if (frame) {
        upload(p->device.get(), cmd, p->picture_upload[f].get(), p->picture.get(), frame, W, H);
        D3D12_RESOURCE_BARRIER t = barrier(p->picture.get(), D3D12_RESOURCE_STATE_COPY_DEST,
                                           D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        cmd->ResourceBarrier(1, &t);
        fresh = p->picture.get();
    }
    gdl::post::Output out;
    out.resource = target;
    out.rtv = rtv;
    out.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    out.width = p->width;
    out.height = p->height;
    gdl::post::RefreshTiming timing;
    double span = p->refresh_hz > 1.0 ? 60.0125 / p->refresh_hz : 1.0;
    if (source_time < p->last_source_time) source_time = p->last_source_time;
    p->last_source_time = source_time;
    timing.source_time = source_time;
    timing.span = span;
    p->chain->frame(cmd, fresh, out, timing);
    D3D12_RESOURCE_BARRIER t = barrier(target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    cmd->ResourceBarrier(1, &t);
    if (fresh) {
        D3D12_RESOURCE_BARRIER back_to_copy = barrier(p->picture.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                                                      D3D12_RESOURCE_STATE_COPY_DEST);
        cmd->ResourceBarrier(1, &back_to_copy);
    }
    cmd->Close();
    ID3D12CommandList *lists[] = {cmd};
    p->queue->ExecuteCommandLists(1, lists);
    // The beam simulator needs every display refresh, so it always syncs.
    bool sync = p->vsync || p->beam;
    p->swap->Present(sync ? 1 : 0, !sync && p->tearing ? DXGI_PRESENT_ALLOW_TEARING : 0);
    ++p->fence_value;
    p->queue->Signal(p->fence.get(), p->fence_value);
    p->frame_fence[f] = p->fence_value;
    p->frame_index = (f + 1) % FRAMES;

    // Refresh rate, from the spacing of synchronised presents.
    LARGE_INTEGER now, rate;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&rate);
    if (sync && p->last_present.QuadPart) {
        double hz = (double)rate.QuadPart / (double)(now.QuadPart - p->last_present.QuadPart);
        if (hz > 23.0 && hz < 1000.0) p->refresh_hz = p->refresh_hz > 0.0 ? p->refresh_hz * 0.95 + hz * 0.05 : hz;
    }
    p->last_present = now;
}

extern "C" int world_gpu_render_offscreen(const uint32_t *frame, const char *preset, int scaling, unsigned width,
                                          unsigned height, uint32_t *out_pixels, char *error, size_t error_size)
{
    Com<IDXGIFactory4> factory;
    Com<ID3D12Device> device;
    Com<ID3D12CommandQueue> queue;
    if (!create_device(factory, device, queue, error, error_size)) return 0;
    Com<ID3D12CommandAllocator> alloc;
    Com<ID3D12GraphicsCommandList> cmd;
    Com<ID3D12Fence> fence;
    Com<ID3D12Resource> picture, picture_upload, target, readback;
    Com<ID3D12DescriptorHeap> heap;
    device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(alloc.put()));
    device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.get(), nullptr, IID_PPV_ARGS(cmd.put()));
    device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.put()));
    texture(device.get(), W, H, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, picture.put());
    texture(device.get(), width, height, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET, target.put());
    if (!alloc || !cmd || !fence || !picture || !target) { say(error, error_size, "Could not create resources."); return 0; }
    buffer(device.get(), D3D12_HEAP_TYPE_UPLOAD, upload_size(device.get(), picture.get()), D3D12_RESOURCE_STATE_GENERIC_READ, picture_upload.put());
    D3D12_RESOURCE_DESC td = target->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp;
    UINT64 total = 0;
    device->GetCopyableFootprints(&td, 0, 1, 0, &fp, nullptr, nullptr, &total);
    buffer(device.get(), D3D12_HEAP_TYPE_READBACK, total, D3D12_RESOURCE_STATE_COPY_DEST, readback.put());
    D3D12_DESCRIPTOR_HEAP_DESC hd = {};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hd.NumDescriptors = 1;
    device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(heap.put()));
    if (!picture_upload || !readback || !heap) { say(error, error_size, "Could not create buffers."); return 0; }
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = heap->GetCPUDescriptorHandleForHeapStart();
    device->CreateRenderTargetView(target.get(), nullptr, rtv);
    std::unique_ptr<PostChain> chain;
    try {
        chain = std::make_unique<PostChain>(device.get(), queue.get());
    } catch (const std::exception &e) {
        say(error, error_size, e.what());
        return 0;
    }
    std::string why;
    if (preset && *preset && !chain->set_preset(preset, &why)) { say(error, error_size, why.c_str()); return 0; }
    apply(*chain, scaling, 0, width, height);
    chain->settings().keep_background = false;
    upload(device.get(), cmd.get(), picture_upload.get(), picture.get(), frame, W, H);
    D3D12_RESOURCE_BARRIER t = barrier(picture.get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmd->ResourceBarrier(1, &t);
    gdl::post::Output out;
    out.resource = target.get();
    out.rtv = rtv;
    out.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    out.width = width;
    out.height = height;
    // A few frames, so presets with feedback or history settle.
    for (int i = 0; i < 4; ++i) {
        if (i) {
            cmd->Close();
            ID3D12CommandList *lists[] = {cmd.get()};
            queue->ExecuteCommandLists(1, lists);
            queue->Signal(fence.get(), (UINT64)i);
            while (fence->GetCompletedValue() < (UINT64)i) Sleep(1);
            alloc->Reset();
            cmd->Reset(alloc.get(), nullptr);
        }
        chain->frame(cmd.get(), i == 0 ? picture.get() : picture.get(), out);
    }
    t = barrier(target.get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
    cmd->ResourceBarrier(1, &t);
    D3D12_TEXTURE_COPY_LOCATION from = {}, to = {};
    from.pResource = target.get();
    from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    to.pResource = readback.get();
    to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    to.PlacedFootprint = fp;
    cmd->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
    cmd->Close();
    ID3D12CommandList *lists[] = {cmd.get()};
    queue->ExecuteCommandLists(1, lists);
    queue->Signal(fence.get(), 100);
    while (fence->GetCompletedValue() < 100) Sleep(1);
    uint8_t *map = nullptr;
    D3D12_RANGE all = {0, (SIZE_T)total};
    if (FAILED(readback->Map(0, &all, reinterpret_cast<void **>(&map)))) { say(error, error_size, "Readback failed."); return 0; }
    for (unsigned y = 0; y < height; ++y) memcpy(out_pixels + (size_t)y * width, map + fp.Offset + (size_t)y * fp.Footprint.RowPitch, (size_t)width * 4);
    D3D12_RANGE none = {0, 0};
    readback->Unmap(0, &none);
    return 1;
}
