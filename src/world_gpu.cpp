// Direct3D 12 presenter for the CRT shaders; see world_gpu.h.
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>

#include "world_gpu.h"
#include "post/post_chain.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>
#include <string>

using gdl::post::PostChain;
using gdl::post::PreparedPreset;

const char *const world_gpu_preset_name[WORLD_GPU_PRESETS] = {
#define WORLD_CRT_PRESET(name, path) name,
#include "world_crt_presets.inc"
#undef WORLD_CRT_PRESET
};
const char *const world_gpu_preset_path[WORLD_GPU_PRESETS] = {
#define WORLD_CRT_PRESET(name, path) path,
#include "world_crt_presets.inc"
#undef WORLD_CRT_PRESET
};

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
            const uint32_t *pixels, UINT w, UINT h, bool copy = true)
{
    D3D12_RESOURCE_DESC desc = dst->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp;
    device->GetCopyableFootprints(&desc, 0, 1, 0, &fp, nullptr, nullptr, nullptr);
    uint8_t *map = nullptr;
    D3D12_RANGE none = {0, 0};
    if (FAILED(staging->Map(0, &none, reinterpret_cast<void **>(&map)))) return;
    size_t row_bytes = (size_t)w * sizeof *pixels;
    if (row_bytes == fp.Footprint.RowPitch) {
        memcpy(map + fp.Offset, pixels, row_bytes * h);
    } else {
        for (UINT y = 0; y < h; ++y)
            memcpy(map + fp.Offset + (size_t)y * fp.Footprint.RowPitch, pixels + (size_t)y * w, row_bytes);
    }
    staging->Unmap(0, nullptr);
    if (!copy) return;
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

static bool ambient_borders;

void apply(PostChain &chain, int scaling, int beam, UINT out_w, UINT out_h, unsigned source_w)
{
    /* The widescreen session's visible aperture is exactly 16:9. */
    /* The ultrawide raster is 568 columns; its 64:27 aperture is 568 8/9. */
    const float aspect = source_w == 428 ? 16.0f / 9.0f : source_w == 568 ? 64.0f / 27.0f : 4.0f / 3.0f;
    gdl::post::Settings &s = chain.settings();
    s.source_mode = gdl::post::SourceMode::AsIs;
    s.base_width = source_w;
    s.base_height = H;
    s.base_aspect = aspect;
    s.integer_scale = scaling == 0;
    s.display_aspect = scaling == 2 && out_h ? (float)out_w / (float)out_h : aspect;
    s.passthrough_filter = gdl::post::PassThroughFilter::Nearest;
    s.keep_background = true;
    s.ambient_borders = ambient_borders;
    s.auto_progressive_fix = true;
    s.beam.enabled = beam != 0;
    s.preset_curvature = false;           // flat screen, no vignette, in every preset
    s.source_fps = 60.0125f;
}

// ---- the window presenter ------------------------------------------------

#include "world_gpu_transport.h"

struct Presenter {
    GpuTransport transport;
    Com<ID3D12CommandAllocator> light_alloc[FRAMES];
    Com<ID3D12GraphicsCommandList> light_cmd;
    Com<ID3D12Resource> light_upload[FRAMES];
    UINT64 light_fence[FRAMES]={};
    bool early_light_failed=false;
    const WorldLightScene *light_frame=nullptr;
    const float *camera_offsets=nullptr;
    HWND parent = nullptr;
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
    HANDLE frame_wait = nullptr;          // signalled when the swap chain can take a frame
    UINT64 fence_value = 0, frame_fence[FRAMES] = {};
    UINT frame_index = 0;
    Com<ID3D12Resource> picture, picture_upload[FRAMES];
    UINT width = 0, height = 0;
    bool tearing = false;
    bool vrr = false;
    // Tearing support does not mean the display follows our presents. While
    // VRR is requested, compare the measured refresh period with the mode's.
    bool vrr_failed = false, vrr_probing = false;
    int64_t vrr_qpc0 = 0;
    UINT vrr_refresh0 = 0;
    int scaling = 0, beam = 0, vsync = 1;
    std::unique_ptr<PostChain> chain;
    LARGE_INTEGER last_present = {};
    // Preset compilation runs on a worker thread (prepare_preset may run
    // beside frame()); the result is installed on the render thread.
    CRITICAL_SECTION lock;
    HANDLE worker = nullptr;
    std::string wanted;                       // latest request
    std::string building;                     // what the worker is compiling
    std::unique_ptr<PreparedPreset> built;
    std::string built_path, failed, failed_path;
    bool busy = false;
    bool shown = true;
    int picture_k = 1;
    unsigned picture_w = W;
    ~Presenter() { DeleteCriticalSection(&lock); }
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
        HRESULT resized = swap->ResizeBuffers(BUFFERS, w, h, DXGI_FORMAT_B8G8R8A8_UNORM,
                            (tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0) |
                            DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);
        /* On failure the old buffers remain; keep their size and re-acquire them. */
        if (SUCCEEDED(resized)) {
            width = w;
            height = h;
        }
        if (!buffers()) release_buffers();
        if (chain) apply(*chain, scaling, beam, width, height, picture_w);
    }
};

Presenter *gpu;

DWORD WINAPI compile_preset(void *context)
{
    Presenter *p = static_cast<Presenter *>(context);
    for (;;) {
        EnterCriticalSection(&p->lock);
        std::string path = p->building;
        LeaveCriticalSection(&p->lock);
        std::string why;
        std::unique_ptr<PreparedPreset> result;
        try {
            result = p->chain->prepare_preset(path, &why);
        } catch (const std::exception &e) {
            why = e.what();
        }
        EnterCriticalSection(&p->lock);
        if (p->wanted != path) {               // superseded while compiling: build the newest
            p->building = p->wanted;
            LeaveCriticalSection(&p->lock);
            continue;
        }
        if (result) {
            p->built = std::move(result);
            p->built_path = path;
        } else {
            p->failed = why.empty() ? "the preset could not be compiled" : why;
            p->failed_path = path;
        }
        p->busy = false;
        LeaveCriticalSection(&p->lock);
        return 0;
    }
}

bool create_device(Com<IDXGIFactory4> &factory, Com<ID3D12Device> &device, Com<ID3D12CommandQueue> &queue,
                   char *error, size_t size)
{
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(factory.put())))) { say(error, size, "DXGI is unavailable."); return false; }
    Com<IDXGIFactory6> preferred;
    if (SUCCEEDED(factory->QueryInterface(IID_PPV_ARGS(preferred.put())))) {
        for (UINT index=0; ; ++index) {
            Com<IDXGIAdapter1> adapter;
            if (FAILED(preferred->EnumAdapterByGpuPreference(index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                            IID_PPV_ARGS(adapter.put())))) break;
            DXGI_ADAPTER_DESC1 desc={};
            if (FAILED(adapter->GetDesc1(&desc)) || (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) continue;
            if (SUCCEEDED(D3D12CreateDevice(adapter.get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(device.put())))) {
                fprintf(stderr,"D3D12 effects adapter: %ls\n",desc.Description);
                break;
            }
        }
    }
    if (!device && FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(device.put())))) {
        say(error, size, "Direct3D 12 is unavailable on this PC.");
        return false;
    }
    D3D12_COMMAND_QUEUE_DESC q = {};
    q.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    q.Priority = D3D12_COMMAND_QUEUE_PRIORITY_HIGH;
    if (FAILED(device->CreateCommandQueue(&q, IID_PPV_ARGS(queue.put())))) {
        q.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        if (FAILED(device->CreateCommandQueue(&q, IID_PPV_ARGS(queue.put())))) {
            say(error, size, "No command queue."); return false;
        }
    }
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
    InitializeCriticalSection(&p->lock);
    p->parent = (HWND)parent_window;
    if (!create_device(p->factory, p->device, p->queue, error, error_size)) return 0;
    BOOL allow = FALSE;
    Com<IDXGIFactory5> f5;
    if (SUCCEEDED(p->factory->QueryInterface(IID_PPV_ARGS(f5.put()))) &&
        SUCCEEDED(f5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow, sizeof allow)))
        p->tearing = allow != FALSE;

    RECT rc;
    GetClientRect(p->parent, &rc);
    p->width = rc.right > 0 ? (UINT)rc.right : 1;
    p->height = rc.bottom > 0 ? (UINT)rc.bottom : 1;
    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width = p->width;
    sd.Height = p->height;
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = BUFFERS;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.Flags = (p->tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0) | DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    Com<IDXGISwapChain1> swap1;
    if (FAILED(p->factory->CreateSwapChainForHwnd(p->queue.get(), p->parent, &sd, nullptr, nullptr, swap1.put())) ||
        FAILED(swap1->QueryInterface(IID_PPV_ARGS(p->swap.put())))) {
        say(error, error_size, "Could not create the swap chain.");
        return 0;
    }
    p->factory->MakeWindowAssociation(p->parent, DXGI_MWA_NO_ALT_ENTER);
    if (getenv("WORLD_PACING_LOG")) {
        Com<IDXGIOutput> output;
        Com<IDXGIOutput6> output6;
        UINT flags=0;
        if (SUCCEEDED(p->swap->GetContainingOutput(output.put())) &&
            SUCCEEDED(output->QueryInterface(IID_PPV_ARGS(output6.put()))) &&
            SUCCEEDED(output6->CheckHardwareCompositionSupport(&flags)))
            fprintf(stderr,"DXGI fullscreen hardware composition flags %u\n",flags);
    }
    p->swap->SetMaximumFrameLatency(1);
    p->frame_wait = p->swap->GetFrameLatencyWaitableObject();
    D3D12_DESCRIPTOR_HEAP_DESC hd = {};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hd.NumDescriptors = BUFFERS;
    p->device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(p->rtv_heap.put()));
    p->rtv_step = p->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    for (auto &a : p->alloc)
        if (FAILED(p->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(a.put())))) {
            say(error, error_size, "Could not create command allocators.");
            return 0;
        }
    p->device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, p->alloc[0].get(), nullptr, IID_PPV_ARGS(p->cmd.put()));
    p->cmd->Close();
    p->device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(p->fence.put()));
    p->event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!p->rtv_heap || !p->cmd || !p->fence || !p->event || !p->buffers() ||
        FAILED(texture(p->device.get(), p->picture_w, H, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, p->picture.put()))) {
        say(error, error_size, "Could not create the presentation resources.");
        return 0;
    }
    UINT64 bytes = upload_size(p->device.get(), p->picture.get());
    for (auto &u : p->picture_upload)
        buffer(p->device.get(), D3D12_HEAP_TYPE_UPLOAD, bytes, D3D12_RESOURCE_STATE_GENERIC_READ, u.put());
    try {
        p->chain = std::make_unique<PostChain>(p->device.get(), p->queue.get());
    } catch (const std::exception &e) {
        say(error, error_size, e.what());
        return 0;
    }
    apply(*p->chain, p->scaling, p->beam, p->width, p->height, p->picture_w);
    if (!p->transport.init(p->device.get(),p->queue.get())) fprintf(stderr,"GPU transport unavailable; using CPU lighting.\n");
    else if(getenv("WORLD_PACING_LOG")) fprintf(stderr,"GPU path transport ready.\n");
    gpu = p.release();
    return 1;
}

extern "C" void world_gpu_close(void)
{
    if (!gpu) return;
    if (gpu->worker) {                        // let a running compile finish first
        WaitForSingleObject(gpu->worker, INFINITE);
        CloseHandle(gpu->worker);
        gpu->worker = nullptr;
    }
    gpu->built.reset();
    gpu->wait_all();
    gpu->chain.reset();
    if (gpu->event) CloseHandle(gpu->event);
    if (gpu->frame_wait) CloseHandle(gpu->frame_wait);
    delete gpu;
    gpu = nullptr;
}

extern "C" int world_gpu_active(void) { return gpu != nullptr; }

extern "C" void world_gpu_show(int show)
{
    if (!gpu || gpu->shown == (show != 0)) return;
    gpu->shown = show != 0;
}

// Queue geometry-dependent lighting while CPU sprite/layer composition runs.
extern "C" void world_gpu_prepare_lights(const WorldLightScene *data)
{
    Presenter *p=gpu;
    if(!p || !data || !p->transport.ready || p->early_light_failed ||
       !(data->flags&WORLD_LIGHT_FIELD_MASK) || !p->transport.needs_light(*data))return;
    UINT f=p->frame_index;
    if(p->fence->GetCompletedValue()<std::max(p->light_fence[f],p->frame_fence[f]))return;
    if(!p->light_alloc[f] &&
       (FAILED(p->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(p->light_alloc[f].put()))) ||
        FAILED(buffer(p->device.get(),D3D12_HEAP_TYPE_UPLOAD,offsetof(WorldLightScene,effects),
                      D3D12_RESOURCE_STATE_GENERIC_READ,p->light_upload[f].put())))){
        p->early_light_failed=true;return;
    }
    if(!p->light_cmd){
        if(FAILED(p->device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,p->light_alloc[f].get(),
                                              nullptr,IID_PPV_ARGS(p->light_cmd.put())))){p->early_light_failed=true;return;}
    }else if(FAILED(p->light_alloc[f]->Reset()) || FAILED(p->light_cmd->Reset(p->light_alloc[f].get(),nullptr))){
        p->early_light_failed=true;return;
    }
    auto &transport=p->transport;
    transport.read_time(f);
    transport.pass_read(f);
    bool sample=transport.time_readback && transport.log_times && ++transport.sample_counter%16==0;
    auto *cmd=p->light_cmd.get();
    if(sample)cmd->EndQuery(transport.time_queries.get(),D3D12_QUERY_TYPE_TIMESTAMP,f*4);
    bool had_field=transport.have_field;
    auto old_revision=transport.revision,old_flags=transport.flags,old_width=transport.width;
    int old_scale=transport.scale;
    bool ok=transport.prepare(cmd,p->light_upload[f].get(),data->scale,*data,transport.dispatch_flags(*data),f);
    if(sample){
        cmd->EndQuery(transport.time_queries.get(),D3D12_QUERY_TYPE_TIMESTAMP,f*4+1);
        cmd->ResolveQueryData(transport.time_queries.get(),D3D12_QUERY_TYPE_TIMESTAMP,f*4,2,
                              transport.time_readback.get(),f*32);
    }
    if(FAILED(cmd->Close()) || !ok){
        transport.have_field=had_field;transport.width=old_width;
        transport.revision=old_revision;transport.flags=old_flags;transport.scale=old_scale;
        p->early_light_failed=true;
        return;
    }
    transport.early_sample[f]=sample;
    ID3D12CommandList *lists[]={p->light_cmd.get()};
    p->queue->ExecuteCommandLists(1,lists);
    p->light_fence[f]=++p->fence_value;
    if(FAILED(p->queue->Signal(p->fence.get(),p->light_fence[f])))p->early_light_failed=true;
}
extern "C" int world_gpu_transport_available(void) { return gpu && gpu->shown && gpu->transport.ready; }
extern "C" void world_gpu_camera_offsets(const float *offsets) { if(gpu) gpu->camera_offsets=offsets; }
extern "C" void world_gpu_light_frame(const WorldLightScene *frame) { if(gpu) gpu->light_frame=frame; }

extern "C" int world_gpu_visible(void) { return gpu && gpu->shown; }

extern "C" void world_gpu_set_ambient_borders(int on)
{
    ambient_borders = on != 0;
    if (gpu && gpu->chain) gpu->chain->settings().ambient_borders = ambient_borders;
}

extern "C" int world_gpu_set_preset(const char *path, char *error, size_t error_size)
{
    if (!gpu) { say(error, error_size, "The shader view is not open."); return 0; }
    std::string wanted = path ? path : "";
    EnterCriticalSection(&gpu->lock);
    gpu->wanted = wanted;
    gpu->failed.clear();
    bool start = !gpu->busy;
    if (start) {
        gpu->busy = true;
        gpu->building = wanted;
    }
    LeaveCriticalSection(&gpu->lock);
    if (start) {
        if (gpu->worker) {
            WaitForSingleObject(gpu->worker, INFINITE);
            CloseHandle(gpu->worker);
        }
        gpu->worker = CreateThread(nullptr, 0, compile_preset, gpu, 0, nullptr);
        if (!gpu->worker) {
            EnterCriticalSection(&gpu->lock);
            gpu->busy = false;
            LeaveCriticalSection(&gpu->lock);
            say(error, error_size, "Could not start the shader compiler thread.");
            return 0;
        }
    }
    return 1;
}

extern "C" int world_gpu_preset_status(char *message, size_t size)
{
    if (!gpu) return 0;
    EnterCriticalSection(&gpu->lock);
    int state = gpu->busy || gpu->built ? 1 : !gpu->failed.empty() ? -1 : 0;
    if (state < 0) say(message, size, gpu->failed.c_str());
    LeaveCriticalSection(&gpu->lock);
    return state;
}

extern "C" void world_gpu_set_options(int scaling, int beam, int vsync)
{
    if (!gpu) return;
    gpu->scaling = scaling;
    gpu->beam = beam;
    gpu->vsync = vsync;
    apply(*gpu->chain, scaling, beam, gpu->width, gpu->height, gpu->picture_w);
}


extern "C" int world_gpu_request_vrr(int enable)
{
    Presenter *p = gpu;
    if (!p) return 0;
    bool want = enable && p->shown && p->tearing && !p->beam && !p->vrr_failed;
    if (!want) { p->vrr_probing = false; p->vrr = false; return 0; }
    // A display that really follows 60 Hz presents refreshes at about our rate;
    // a fixed-rate one keeps refreshing at its mode rate (e.g. 120 Hz) and
    // shows torn, unevenly held frames. Fall back to vsync pacing then.
    DXGI_FRAME_STATISTICS st = {};
    if (SUCCEEDED(p->swap->GetFrameStatistics(&st)) && st.SyncQPCTime.QuadPart) {
        if (!p->vrr_probing) {
            p->vrr_probing = true;
            p->vrr_qpc0 = st.SyncQPCTime.QuadPart;
            p->vrr_refresh0 = st.SyncRefreshCount;
        } else if (st.SyncRefreshCount - p->vrr_refresh0 >= 90) {
            LARGE_INTEGER rate;
            QueryPerformanceFrequency(&rate);
            double measured = (double)(st.SyncRefreshCount - p->vrr_refresh0) * rate.QuadPart /
                              (double)(st.SyncQPCTime.QuadPart - p->vrr_qpc0);
            DEVMODEW mode = {};
            mode.dmSize = sizeof mode;
            double nominal = EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode) ? mode.dmDisplayFrequency : 0;
            if (nominal > 75 && measured > 0.85 * nominal) {
                p->vrr_failed = true;
                fprintf(stderr, "VRR not active (display at %.1f Hz of %.0f Hz): using vsync pacing.%c", measured, nominal, 10);
                p->vrr = false;
                return 0;
            }
            p->vrr_qpc0 = st.SyncQPCTime.QuadPart;
            p->vrr_refresh0 = st.SyncRefreshCount;
        }
    }
    p->vrr = true;
    return 1;
}

extern "C" int world_gpu_vrr_failed(void) { return gpu && gpu->vrr_failed; }
extern "C" void world_gpu_retry_vrr(void) { if (gpu) { gpu->vrr_failed = false; gpu->vrr_probing = false; } }

extern "C" double world_gpu_refresh_hz(void) { return gpu ? gpu->refresh_hz : 0.0; }

extern "C" int world_gpu_frame_statistics(unsigned *present_count, unsigned *present_refresh,
                                          unsigned *last_present)
{
    if (!gpu || !gpu->swap) return 0;
    DXGI_FRAME_STATISTICS st = {};
    UINT last = 0;
    if (FAILED(gpu->swap->GetFrameStatistics(&st)) || FAILED(gpu->swap->GetLastPresentCount(&last))) return 0;
    *present_count = st.PresentCount;
    *present_refresh = st.PresentRefreshCount;
    *last_present = last;
    return 1;
}

extern "C" int world_gpu_sync_sample(int64_t *qpc, unsigned *refresh)
{
    DXGI_FRAME_STATISTICS st={};
    if (!gpu || FAILED(gpu->swap->GetFrameStatistics(&st)) || !st.SyncQPCTime.QuadPart) return 0;
    *qpc=st.SyncQPCTime.QuadPart;
    *refresh=st.SyncRefreshCount;
    return 1;
}

extern "C" void world_gpu_wait_frame(void)
{
    if (gpu && gpu->frame_wait) {
        DWORD r = WaitForSingleObject(gpu->frame_wait, 100);
        if (r != WAIT_OBJECT_0 && getenv("WORLD_PACING_LOG")) fprintf(stderr, "frame wait %lu%c", (unsigned long)r, 10);
    }
}

extern "C" void world_gpu_present(const uint32_t *frame, int k, double source_time, unsigned source_w)
{
    Presenter *p = gpu;
    if (!p) return;
    if (k < 1) k = 1;
    if (source_w != 428 && source_w != 568) source_w = W;
    if (k != p->picture_k || source_w != p->picture_w) {
        /* The source picture changed size: new texture and upload buffers. */
        p->wait_all();
        p->picture.reset();
        for (auto &u : p->picture_upload) u.reset();
        if (FAILED(texture(p->device.get(), (UINT)(source_w * k), (UINT)(H * k), D3D12_RESOURCE_FLAG_NONE,
                           D3D12_RESOURCE_STATE_COPY_DEST, p->picture.put())))
            return;
        UINT64 bytes = upload_size(p->device.get(), p->picture.get());
        for (auto &u : p->picture_upload)
            buffer(p->device.get(), D3D12_HEAP_TYPE_UPLOAD, bytes, D3D12_RESOURCE_STATE_GENERIC_READ, u.put());
        p->picture_k = k;
        p->picture_w = source_w;
        apply(*p->chain, p->scaling, p->beam, p->width, p->height, source_w);
    }
    if (!p->picture) return;
    {
        std::unique_ptr<PreparedPreset> ready;
        EnterCriticalSection(&p->lock);
        if (p->built && p->built_path == p->wanted) ready = std::move(p->built);
        else p->built.reset();
        LeaveCriticalSection(&p->lock);
        if (ready) {
            p->chain->install_preset(std::move(ready));
            apply(*p->chain, p->scaling, p->beam, p->width, p->height, p->picture_w);
        }
    }
    RECT rc;
    GetClientRect(p->parent, &rc);
    if (rc.right <= 0 || rc.bottom <= 0) return;
    if ((UINT)rc.right != p->width || (UINT)rc.bottom != p->height) {
        p->resize((UINT)rc.right, (UINT)rc.bottom);
    }
    UINT f = p->frame_index;
    if (p->fence->GetCompletedValue() < p->frame_fence[f]) {
        p->fence->SetEventOnCompletion(p->frame_fence[f], p->event);
        WaitForSingleObject(p->event, 2000);
        /* Never reset an allocator the GPU may still be using: skip this refresh. */
        if (p->fence->GetCompletedValue() < p->frame_fence[f]) return;
    }
    /* No back buffer after a failed resize: skip rather than record into null. */
    if (!p->back[p->swap->GetCurrentBackBufferIndex()]) return;
    p->alloc[f]->Reset();
    ID3D12GraphicsCommandList *cmd = p->cmd.get();
    cmd->Reset(p->alloc[f].get(), nullptr);

    UINT b = p->swap->GetCurrentBackBufferIndex();
    ID3D12Resource *target = p->back[b].get();
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = p->rtv_heap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += (SIZE_T)b * p->rtv_step;

    D3D12_RESOURCE_BARRIER begin_render = barrier(target, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    cmd->ResourceBarrier(1, &begin_render);
    const float black[4] = {0, 0, 0, 1};
    cmd->ClearRenderTargetView(rtv, black, 0, nullptr);

    ID3D12Resource *fresh = nullptr;
    if (frame) {
        upload(p->device.get(), cmd, p->picture_upload[f].get(), p->picture.get(), frame,
               (UINT)(p->picture_w * p->picture_k), (UINT)(H * p->picture_k), !p->light_frame);
        if(p->light_frame && p->transport.ready)
            if (!p->transport.record(cmd,f,k,*p->light_frame,p->picture_upload[f].get(),p->picture.get())) {
                p->transport.ready=false;
                upload(p->device.get(),cmd,p->picture_upload[f].get(),p->picture.get(),frame,
                       (UINT)(p->picture_w*k),(UINT)(H*k));
            }
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
    if (fresh && !p->beam && p->chain->preset_path().empty())
        p->chain->present_pixels(cmd,fresh,(UINT)(p->picture_w*k),(UINT)(H*k),out,p->camera_offsets);
    else
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
    bool sync = !p->vrr && (p->vsync || p->beam);
    HRESULT presented = p->swap->Present(sync ? 1 : 0, !sync && p->tearing ? DXGI_PRESENT_ALLOW_TEARING : 0);
    if (FAILED(presented) && getenv("WORLD_PACING_LOG")) fprintf(stderr, "Present failed %08lx reason %08lx%c", (unsigned long)presented, (unsigned long)p->device->GetDeviceRemovedReason(), 10);
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

static int render_offscreen(const float *offsets,const WorldLightScene *lighting, const uint32_t *frame, unsigned source_w,
                            const char *preset, int scaling, unsigned width,
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
    texture(device.get(), source_w, H, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, picture.put());
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
    apply(*chain, scaling, 0, width, height, source_w);
    chain->settings().keep_background = false;
    upload(device.get(), cmd.get(), picture_upload.get(), picture.get(), frame, source_w, H);
    GpuTransport transport;
    if (lighting && (!transport.init(device.get()) ||
        !transport.record(cmd.get(),0,1,*lighting,picture_upload.get(),picture.get()))) {
        say(error,error_size,"GPU lighting test dispatch failed");
        return 0;
    }
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
        if(offsets)chain->present_pixels(cmd.get(),picture.get(),source_w,H,out,offsets);
        else chain->frame(cmd.get(),picture.get(),out);
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

extern "C" int world_gpu_light_benchmark(const WorldLightScene *const *scenes,const uint32_t *const *pixels,
                                         unsigned count,const char *const *sources,unsigned variants,unsigned runs,
                                         double (*pass_ms)[8],uint32_t *const *shaded,char *error,size_t error_size)
{
    Com<IDXGIFactory4> factory;
    Com<ID3D12Device> device;
    Com<ID3D12CommandQueue> queue;
    if (!create_device(factory, device, queue, error, error_size)) return 0;
    Com<ID3D12CommandAllocator> alloc;
    Com<ID3D12GraphicsCommandList> cmd;
    Com<ID3D12Fence> fence;
    Com<ID3D12Resource> picture, source, readback, one;
    device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(alloc.put()));
    device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.get(), nullptr, IID_PPV_ARGS(cmd.put()));
    device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.put()));
    // Frames share a staging slot size, but each scene keeps its own row pitch.
    const size_t slotBytes=((WORLD_VIEW_MAX_WIDTH*4+255)&~255u)*H;
    texture(device.get(), WORLD_VIEW_MAX_WIDTH, H, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, picture.put());
    UINT64 outputs = (UINT64)slotBytes * count * variants;
    buffer(device.get(), D3D12_HEAP_TYPE_UPLOAD, (UINT64)slotBytes * count, D3D12_RESOURCE_STATE_GENERIC_READ, source.put());
    buffer(device.get(), D3D12_HEAP_TYPE_READBACK, outputs, D3D12_RESOURCE_STATE_COPY_DEST, readback.put());
    buffer(device.get(), D3D12_HEAP_TYPE_DEFAULT, slotBytes, D3D12_RESOURCE_STATE_COPY_DEST, one.put());
    if (!alloc || !cmd || !fence || !picture || !source || !readback || !one) { say(error, error_size, "Could not create resources."); return 0; }
    uint8_t *map = nullptr;
    if (FAILED(source->Map(0, nullptr, reinterpret_cast<void **>(&map)))) { say(error, error_size, "Upload map failed."); return 0; }
    for (unsigned s=0;s<count;++s) {
        unsigned bytes=(scenes[s]->width>WORLD_VIEW_ARCADE_WIDTH ? scenes[s]->width : WORLD_VIEW_ARCADE_WIDTH)*4;
        unsigned pitch=(bytes+255)&~255u;
        for(int y=0;y<H;++y)
            memcpy(map+s*slotBytes+y*pitch,pixels[s]+y*(bytes/4),bytes);
    }
    source->Unmap(0, nullptr);
    std::unique_ptr<WorldLightScene> copy(new WorldLightScene());
    std::vector<std::unique_ptr<GpuTransport>> transports;
    for (unsigned v = 0; v < variants; ++v) {
        transports.emplace_back(new GpuTransport());
        GpuTransport &g = *transports.back();
        g.force_pass_profile = true;
        g.quiet_passes = true;
        if (sources && sources[v]) g.source = sources[v];
        if (!g.init(device.get(), queue.get()) || !g.pass_queries) { say(error, error_size, "GPU transport variant failed to build."); return 0; }
    }
    for (unsigned i = 0; i < count * variants; ++i) for (int n = 0; n < 8; ++n) pass_ms[i][n] = 0;
    UINT64 value = 0;
    uint32_t revision = 1;
    bool copied = false;
    LARGE_INTEGER rate, began, now;
    QueryPerformanceFrequency(&rate);
    QueryPerformanceCounter(&began);
    // Two seconds of continuous work hold the GPU at its working clock; then
    // scenes and variants interleave (in alternating order) to share drift.
    for (unsigned run = 0; run <= runs;) {
        QueryPerformanceCounter(&now);
        bool warm = (now.QuadPart - began.QuadPart) < rate.QuadPart * 2;
        for (unsigned s = 0; s < count; ++s) for (unsigned j = 0; j < variants; ++j) {
            unsigned v = (run + s) & 1 ? variants - 1 - j : j;
            GpuTransport &g = *transports[v];
            *copy = *scenes[s];
            copy->revision = ++revision;   /* force the light field to be rebuilt */
            alloc->Reset(); cmd->Reset(alloc.get(), nullptr);
            if (copied) { auto b = barrier(one.get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST); cmd->ResourceBarrier(1, &b); }
            cmd->CopyBufferRegion(one.get(), 0, source.get(), (UINT64)s * slotBytes, slotBytes);
            { auto b = barrier(one.get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE); cmd->ResourceBarrier(1, &b); }
            copied = true;
            for (int n = 0; n < 8; ++n) { g.pass_sum[n] = 0; g.pass_n[n] = 0; }
            if (!g.record(cmd.get(), 0, 1, *copy, one.get(), picture.get())) { say(error, error_size, "Lighting dispatch failed."); return 0; }
            if (!warm && run == runs) {
                auto r = barrier(g.result.get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
                cmd->ResourceBarrier(1, &r);
                cmd->CopyBufferRegion(readback.get(), (UINT64)(v * count + s) * slotBytes, g.result.get(), 0, slotBytes);
                r = barrier(g.result.get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                cmd->ResourceBarrier(1, &r);
            }
            cmd->Close();
            ID3D12CommandList *lists[] = {cmd.get()};
            queue->ExecuteCommandLists(1, lists);
            queue->Signal(fence.get(), ++value);
            while (fence->GetCompletedValue() < value) YieldProcessor();
            g.pass_read(0);
            if (!warm && run >= 1)
                for (int n = 0; n < 8; ++n)
                    pass_ms[v * count + s][n] += g.pass_n[n] ? g.pass_sum[n] / g.pass_n[n] / runs : 0;
        }
        if (!warm) ++run;
    }
    if (FAILED(readback->Map(0, nullptr, reinterpret_cast<void **>(&map)))) { say(error, error_size, "Readback failed."); return 0; }
    for(unsigned i=0;i<count*variants;++i) if(shaded && shaded[i]) {
        unsigned bytes=(scenes[i%count]->width>WORLD_VIEW_ARCADE_WIDTH ? scenes[i%count]->width : WORLD_VIEW_ARCADE_WIDTH)*4;
        unsigned pitch=(bytes+255)&~255u;
        for(int y=0;y<H;++y)
            memcpy(shaded[i]+y*(bytes/4),map+i*slotBytes+y*pitch,bytes);
    }
    D3D12_RANGE none = {0, 0};
    readback->Unmap(0, &none);
    return 1;
}

extern "C" int world_gpu_render_offscreen(const uint32_t *frame,unsigned source_w,const char *preset,int scaling,
                                          unsigned width,unsigned height,uint32_t *out,char *error,size_t size)
{
    return render_offscreen(nullptr,nullptr,frame,source_w,preset,scaling,width,height,out,error,size);
}

extern "C" int world_gpu_light_offscreen(const WorldLightScene *scene,const uint32_t *pixels,uint32_t *out,
                                         unsigned source_w,char *error,size_t size)
{
    return render_offscreen(nullptr,scene,pixels,source_w,"",2,source_w,H,out,error,size);
}

extern "C" int world_gpu_camera_offscreen(const float offsets[4],const uint32_t *pixels,uint32_t *out,
                                          unsigned source_w,unsigned scale,char *error,size_t size)
{
    return render_offscreen(offsets,nullptr,pixels,source_w,"",2,source_w*scale,H*scale,out,error,size);
}
