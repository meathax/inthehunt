// Post-processing chain: see post_chain.h and docs/post_chain.md.
//
// The beam simulator pass (ps_beam) is a port of the Blur Busters CRT Beam
// Simulator by Mark Rejhon (@BlurBusters) and Timothy Lottes
// (@NOTimothyLottes), MIT licence; the unmodified original and its licence
// are in runtime/post/blurbusters/. The sharpening follows the published
// formula of AMD FidelityFX Contrast Adaptive Sharpening (MIT).
#include <windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>

#define LIBRA_RUNTIME_D3D12
#include "librashader_ld.h"

#include "post_chain.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace gdl::post {
namespace {

namespace fs = std::filesystem;

template <class T> class Com {
public:
    Com() = default;
    Com(const Com& o) : p_(o.p_) { if (p_) p_->AddRef(); }
    Com(Com&& o) noexcept : p_(o.p_) { o.p_ = nullptr; }
    ~Com() { reset(); }
    Com& operator=(Com o) noexcept { std::swap(p_, o.p_); return *this; }
    void reset() { if (p_) { p_->Release(); p_ = nullptr; } }
    T* get() const { return p_; }
    T* operator->() const { return p_; }
    T** put() { reset(); return &p_; }
    explicit operator bool() const { return p_ != nullptr; }
private:
    T* p_ = nullptr;
};

void check(HRESULT hr, const char* what) {
    if (FAILED(hr)) {
        char text[160];
        snprintf(text, sizeof text, "post_chain: %s failed (0x%08lX)", what, static_cast<unsigned long>(hr));
        throw std::runtime_error(text);
    }
}

fs::path path_from_utf8(const std::string& s) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}

const char kShader[] = R"hlsl(
cbuffer C : register(b0) {
    float4 dst_rect;   // x0,y0,x1,y1 of the picture in output pixels
    float4 src_size;   // w,h,1/w,1/h
    float4 adj;        // 1/gamma, saturation, brightness, contrast
    float4 hdr;        // paper white nits, peak nits, decode gamma (0 = sRGB), unused
    uint4 mode;        // filter, picture encode, box taps x, box taps y
    float4 geom;       // curvature x, curvature y, corner radius (fraction of height), sharpen 0..1
    float4 beam;       // raster position 0..1, display refreshes per emulated refresh, gain, gamma
    uint4 mode2;       // background encode, bezel on, keep caller's background, unused
    float4 uvs;        // valid region of t0 / texture size (x, y); 1 = whole texture
};
Texture2D tex : register(t0);
Texture2D tex1 : register(t1);
Texture2D tex2 : register(t2);
SamplerState s_point : register(s0);
SamplerState s_linear : register(s1);

float4 vs(uint id : SV_VertexID) : SV_Position {
    return float4(float((id << 1) & 2) * 2.0 - 1.0, 1.0 - float(id & 2) * 2.0, 0.0, 1.0);
}

float3 fetch(float2 uv) {
    if (mode.x == 0) return tex.SampleLevel(s_point, uv * uvs.xy, 0).rgb;
    if (mode.x == 1) return tex.SampleLevel(s_linear, uv * uvs.xy, 0).rgb;
    if (mode.x == 2) {
        // Sharp bilinear: nearest inside each source pixel, linear only across
        // the one output pixel that straddles a boundary.
        float2 scale = max(floor((dst_rect.zw - dst_rect.xy) * src_size.zw), 1.0);
        float2 t = uv * src_size.xy - 0.5;
        float2 i = floor(t);
        float2 f = saturate((t - i - 0.5) * scale + 0.5);
        return tex.SampleLevel(s_linear, (i + 0.5 + f) * src_size.zw * uvs.xy, 0).rgb;
    }
    // Box filter over the source footprint of this output pixel, in linear light.
    float2 fp = src_size.xy / (dst_rect.zw - dst_rect.xy);
    float2 start = uv * src_size.xy - 0.5 * fp;
    float3 sum = 0.0;
    for (uint y = 0; y < mode.w; ++y)
        for (uint x = 0; x < mode.z; ++x) {
            float2 p = start + (float2(x, y) + 0.5) * fp / float2(mode.z, mode.w);
            sum += pow(tex.SampleLevel(s_point, p * src_size.zw * uvs.xy, 0).rgb, 2.2);
        }
    return pow(sum / float(mode.z * mode.w), 1.0 / 2.2);
}

float3 pq(float3 nits) {
    float3 y = pow(saturate(nits / 10000.0), 0.1593017578125);
    return pow((0.8359375 + 18.8515625 * y) / (1.0 + 18.6875 * y), 78.84375);
}

// SDR colour (sRGB-encoded) to the target encoding.
float3 encode(float3 c, uint enc) {
    c = saturate(c);
    if (enc <= 1) return c;
    float3 lin = hdr.z > 0.0 ? pow(c, hdr.z)
               : lerp(c / 12.92, pow((c + 0.055) / 1.055, 2.4), step(0.04045, c));
    if (enc == 2)   // scRGB: linear BT.709, 1.0 = 80 nits
        return min(lin * hdr.x, hdr.y) / 80.0;
    // HDR10: BT.709 -> BT.2020, PQ
    float3 r2020 = float3(dot(lin, float3(0.627404, 0.329283, 0.043313)),
                          dot(lin, float3(0.069097, 0.919540, 0.011362)),
                          dot(lin, float3(0.016391, 0.088013, 0.895595)));
    return pq(min(r2020 * hdr.x, hdr.y));
}

// Contrast-adaptive sharpening (cross pattern): strong where local contrast
// is low, backing off near edges that are already hard, never overshooting.
float3 sharpen(float2 uv, float3 e) {
    float2 st = max(src_size.zw, 1.0 / (dst_rect.zw - dst_rect.xy));
    float3 b = fetch(uv - float2(0, st.y));
    float3 d = fetch(uv - float2(st.x, 0));
    float3 f = fetch(uv + float2(st.x, 0));
    float3 h = fetch(uv + float2(0, st.y));
    float3 mn = min(e, min(min(b, d), min(f, h)));
    float3 mx = max(e, max(max(b, d), max(f, h)));
    float3 amp = sqrt(saturate(min(mn, 1.0 - mx) / max(mx, 1e-5)));
    float3 w = amp * (-1.0 / lerp(8.0, 5.0, geom.w));
    return saturate((e + (b + d + f + h) * w) / (1.0 + 4.0 * w));
}

float4 ps(float4 pos : SV_Position) : SV_Target {
    float2 size = dst_rect.zw - dst_rect.xy;
    float2 q = (pos.xy - dst_rect.xy) / size * 2.0 - 1.0;      // -1..1 over the picture
    float2 w = q * (1.0 + geom.xy * q.yx * q.yx);              // tube curvature (barrel)
    // Signed distance in output pixels to the (rounded) picture outline.
    float2 half_px = size * 0.5;
    float r = geom.z * size.y;
    float2 dd = abs(w) * half_px - (half_px - r);
    float sd = length(max(dd, 0.0)) + min(max(dd.x, dd.y), 0.0) - r;
    float alpha = saturate(0.5 - sd);
    if (alpha > 0.999) alpha = 1.0;

    float3 pic = 0.0;
    if (alpha > 0.0) {
        float2 uv = w * 0.5 + 0.5;
        float3 c = fetch(uv);
        if (mode.y == 0) {
            pic = c;                                           // the preset already encoded HDR
        } else {
            if (geom.w > 0.0 && mode.x != 3) c = sharpen(uv, c);
            c = (c - 0.5) * adj.w + 0.5 + adj.z;
            float luma = dot(c, float3(0.2126, 0.7152, 0.0722));
            c = max(lerp(luma.xxx, c, adj.y), 0.0);
            c = pow(c, adj.x);
            pic = encode(c, mode.y);
        }
    }
    if (mode2.z != 0 && alpha <= 0.0) discard;                 // the caller's border art shows through
    float3 bg = 0.0;
    if (mode2.y != 0 && alpha < 1.0) {
        // Plain bezel: a lit inner slope 6% of the picture height wide, then a dark surround.
        float t = saturate(sd / (0.06 * size.y));
        float slope = lerp(0.11, 0.035, t) * (1.0 - 0.35 * q.y / max(abs(q.y), 1.0));
        float shade = t < 1.0 ? slope : 0.012;
        bg = encode(shade * float3(1.0, 0.98, 0.95), mode2.x);
    }
    return float4(lerp(bg, pic, alpha), 1.0);
}

// ---- CRT beam simulator (port of the Blur Busters / Timothy Lottes shader) ----
float3 beam_dec(float3 c) {
    return lerp(c / 12.92, pow((c + 0.055) / 1.055, beam.w), step(0.04045, c));
}
float3 beam_enc(float3 c) {
    return saturate(lerp(c * 12.92, 1.055 * pow(c, 1.0 / beam.w) - 0.055, step(0.0031308, c)));
}
float3 overlap(float start, float3 len, float f_start) {
    return max(0.0, min(start + len, f_start + 1.0) - max(start, f_start));
}
float4 ps_beam(float4 pos : SV_Position) : SV_Target {
    float2 uv = pos.xy * src_size.zw;
    float fph = beam.y;                       // display refreshes per emulated CRT refresh
    float scale = fph * beam.z;               // light of a full-white pixel, in display refreshes
    float3 curr = beam_dec(tex.SampleLevel(s_point, uv, 0).rgb) * scale;    // emulated refresh k
    float3 prev1 = beam_dec(tex1.SampleLevel(s_point, uv, 0).rgb) * scale;  // k-1
    float3 prev2 = beam_dec(tex2.SampleLevel(s_point, uv, 0).rgb) * scale;  // k-2
    float tube = uv.y * fph;                  // when the beam reaches this row, in display refreshes
    float f_start = beam.x * fph;             // this display refresh covers [f_start, f_start + 1]
    float3 result = overlap(tube - fph, prev2, f_start)
                  + overlap(tube, prev1, f_start)
                  + overlap(tube + fph, curr, f_start);
    return float4(beam_enc(result), 1.0);
}
)hlsl";

struct Constants {
    float dst_rect[4];
    float src_size[4];
    float adj[4];
    float hdr[4];
    uint32_t mode[4];
    float geom[4];
    float beam[4];
    uint32_t mode2[4];
    float uvs[4];
};

enum Filter : uint32_t { kNearest = 0, kBilinear = 1, kSharp = 2, kBox = 3 };
enum Encode : uint32_t { kCopy = 0, kSdr = 1, kScRgb = 2, kHdr10 = 3 };

struct Tex {
    Com<ID3D12Resource> res;
    uint32_t w = 0, h = 0;
    DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
    uint32_t rtv_slot = 0;
};

struct Watched {
    fs::path path;
    fs::file_time_type mtime{};
    uintmax_t size = 0;
};

struct Chain {
    libra_d3d12_filter_chain_t handle = nullptr;
    std::string path;
    std::vector<Parameter> params;
    std::vector<Watched> deps;
    LIBRA_COLOR_SPACE color_space = LIBRA_COLOR_SPACE_SDR;
    uint32_t frames_in_flight = 4;
};

struct Rect { float x0, y0, x1, y1; };

// Parameters by which the known preset families decide that a picture is
// interlaced, and the value that means "progressive".
const std::pair<const char*, float> kProgressiveFix[] = {
    {"interm", 0.0f},                   // CRT-Guest-Advanced (all variants): Interlace Mode OFF
    {"interlace_detect_toggle", 0.0f},  // CRT-Royale
    {"HSM_INTERLACE_MODE", 0.0f},       // Mega Bezel: NO LACE
};
// The presets' own curvature switches, used when Settings::preset_curvature is false.
const std::pair<const char*, float> kCurvatureOff[] = {
    {"warpX", 0.0f}, {"warpY", 0.0f},   // CRT-Guest-Advanced, Lottes
    {"geom_mode_runtime", 0.0f},        // CRT-Royale
    {"HSM_CURVATURE_MODE", 0.0f},       // Mega Bezel
    {"CURVATURE", 0.0f},                // zfast curvature variants and others
};

}  // namespace

struct PostChain::Impl {
    ID3D12Device* device = nullptr;
    ID3D12CommandQueue* queue = nullptr;
    libra_instance_t lib{};
    bool lib_ok = false;
    bool force_hlsl = false;
    std::string lib_version;

    Settings settings;
    Stats stats;
    Chain chain, sub;
    std::map<std::string, float> overrides, sub_overrides;
    uint64_t frame_count = 0;      // game images
    uint64_t refresh_count = 0;    // emulated refreshes started through the sub-frame index API
    bool clear_history = true;
    bool sub_clear_history = true;
    bool applied_fix = true, applied_curv = true;   // flags the chain parameters were last set for
    bool params_dirty = true;

    Com<ID3D12RootSignature> root;
    Com<ID3DBlob> vs_blob, ps_blob, ps_beam_blob;
    std::unordered_map<uint32_t, Com<ID3D12PipelineState>> psos;
    Com<ID3D12DescriptorHeap> rtv_heap, srv_heap;
    uint32_t rtv_size = 0, srv_size = 0, srv_next = 0;
    static constexpr uint32_t kSrvTriples = 64;

    Tex prep, latest, crt, subtex, prev1, prev2, beamtex;
    Com<ID3D12Fence> fence;
    uint64_t fence_value = 0;
    HANDLE fence_event = nullptr;
    struct Retired { Com<ID3D12Resource> res; uint64_t call = 0; uint64_t fence = 0; };
    std::vector<Retired> retired;
    // Filter chains replaced by install_preset(): freed once the GPU has passed them.
    struct RetiredChain { libra_d3d12_filter_chain_t handle = nullptr; uint64_t call = 0; uint64_t fence = 0; };
    std::vector<RetiredChain> retired_chains;
    uint64_t calls = 0;

    // The current game image, in a texture the module owns.
    Tex* owned = nullptr;
    uint32_t src_w = 0, src_h = 0;
    bool chain_dirty = true;       // the CRT chain has not run for the current image
    bool chain_ok = false;         // crt holds a valid picture
    bool chain_encodes_hdr = false;
    // beam simulator
    bool beam_valid = false;
    int64_t beam_k = 0;            // emulated refresh the history is at
    // legacy sub-frame indices
    uint32_t legacy_sub = 0, legacy_total = 1;
    std::string last_error;

    // ------------------------------------------------------------------ setup
    void load_runtime(std::wstring dir) {
        if (dir.empty()) {
            wchar_t exe[MAX_PATH * 4];
            const DWORD n = GetModuleFileNameW(nullptr, exe, static_cast<DWORD>(std::size(exe)));
            dir = fs::path(std::wstring(exe, n)).parent_path().wstring();
        }
        // Pre-load by full path so that the loader header's LoadLibraryW by
        // name, and librashader's own lookup of dxcompiler.dll, find these.
        LoadLibraryExW((dir + L"\\dxcompiler.dll").c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!LoadLibraryExW((dir + L"\\librashader.dll").c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH)) {
            lib_version = "librashader.dll not found or not loadable (Windows error " + std::to_string(GetLastError()) + ")";
            return;
        }
        lib = librashader_load_instance();
        if (!lib.instance_loaded) {
            lib_version = "librashader.dll has no usable C API";
            return;
        }
        const size_t abi = lib.instance_abi_version();
        const size_t api = lib.instance_api_version();
        lib_version = "librashader ABI " + std::to_string(abi) + " API " + std::to_string(api);
        lib_ok = abi == LIBRASHADER_CURRENT_ABI && api >= 4;
    }

    void create_native() {
        D3D12_DESCRIPTOR_HEAP_DESC hd{};
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        hd.NumDescriptors = 8;
        check(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(rtv_heap.put())), "CreateDescriptorHeap(rtv)");
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        hd.NumDescriptors = kSrvTriples * 3;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        check(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(srv_heap.put())), "CreateDescriptorHeap(srv)");
        rtv_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        srv_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        prep.rtv_slot = 0; crt.rtv_slot = 1; subtex.rtv_slot = 2; latest.rtv_slot = 3;
        prev1.rtv_slot = 4; prev2.rtv_slot = 5; beamtex.rtv_slot = 6;

        Com<ID3DBlob> err;
        auto compile = [&](const char* entry, const char* target, Com<ID3DBlob>& out) {
            if (FAILED(D3DCompile(kShader, sizeof kShader - 1, "post_native", nullptr, nullptr, entry, target, 0, 0, out.put(), err.put()))) {
                std::string text = "post_chain: native shader: ";
                if (err) text.append(static_cast<const char*>(err->GetBufferPointer()), err->GetBufferSize());
                throw std::runtime_error(text);
            }
        };
        compile("vs", "vs_5_0", vs_blob);
        compile("ps", "ps_5_0", ps_blob);
        compile("ps_beam", "ps_5_0", ps_beam_blob);

        D3D12_DESCRIPTOR_RANGE range{};
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors = 3;
        D3D12_ROOT_PARAMETER params[2]{};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[0].Constants.Num32BitValues = sizeof(Constants) / 4;
        params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable.NumDescriptorRanges = 1;
        params[1].DescriptorTable.pDescriptorRanges = &range;
        params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_STATIC_SAMPLER_DESC samp[2]{};
        for (int i = 0; i < 2; ++i) {
            samp[i].Filter = i ? D3D12_FILTER_MIN_MAG_MIP_LINEAR : D3D12_FILTER_MIN_MAG_MIP_POINT;
            samp[i].AddressU = samp[i].AddressV = samp[i].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            samp[i].MaxLOD = D3D12_FLOAT32_MAX;
            samp[i].ShaderRegister = static_cast<UINT>(i);
            samp[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        }
        D3D12_ROOT_SIGNATURE_DESC rd{};
        rd.NumParameters = 2;
        rd.pParameters = params;
        rd.NumStaticSamplers = 2;
        rd.pStaticSamplers = samp;
        Com<ID3DBlob> blob;
        check(D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, blob.put(), err.put()), "D3D12SerializeRootSignature");
        check(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(root.put())), "CreateRootSignature");

        check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.put())), "CreateFence");
        fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    }

    ID3D12PipelineState* pso_for(DXGI_FORMAT fmt, bool beam) {
        const uint32_t key = static_cast<uint32_t>(fmt) | (beam ? 0x10000u : 0u);
        auto it = psos.find(key);
        if (it != psos.end()) return it->second.get();
        ID3DBlob* pixel = beam ? ps_beam_blob.get() : ps_blob.get();
        D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
        d.pRootSignature = root.get();
        d.VS = {vs_blob->GetBufferPointer(), vs_blob->GetBufferSize()};
        d.PS = {pixel->GetBufferPointer(), pixel->GetBufferSize()};
        d.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        d.SampleMask = ~0u;
        d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        d.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        d.NumRenderTargets = 1;
        d.RTVFormats[0] = fmt;
        d.SampleDesc.Count = 1;
        Com<ID3D12PipelineState> pso;
        check(device->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(pso.put())), "CreateGraphicsPipelineState");
        ID3D12PipelineState* raw = pso.get();
        psos.emplace(key, std::move(pso));
        return raw;
    }

    // Only used before a filter chain is destroyed (preset change, reload,
    // destruction): command lists already submitted may still use it.
    void wait_idle() {
        const uint64_t v = ++fence_value;
        check(queue->Signal(fence.get(), v), "Signal");
        if (fence->GetCompletedValue() < v) {
            fence->SetEventOnCompletion(v, fence_event);
            WaitForSingleObject(fence_event, INFINITE);
        }
        retired.clear();
        for (auto& r : retired_chains)
            if (r.handle)
                if (libra_error_t err = lib.d3d12_filter_chain_free(&r.handle)) lib.error_free(&err);
        retired_chains.clear();
    }

    // Hand a filter chain to the deferred release (never waits).
    void retire(Chain& c) {
        if (c.handle) retired_chains.push_back({c.handle, calls, 0});
        c = Chain{};
    }

    // Old internal textures may still be referenced by command lists that are
    // recorded or queued. Keep them for 16 more calls, then until the queue
    // has passed a fence signalled at that point. Never waits.
    void collect_retired() {
        ++calls;
        const uint64_t done = fence->GetCompletedValue();
        for (auto& r : retired)
            if (!r.fence && calls - r.call >= 16) {
                r.fence = ++fence_value;
                queue->Signal(fence.get(), r.fence);
            }
        std::erase_if(retired, [&](const Retired& r) { return r.fence && r.fence <= done; });
        for (auto& r : retired_chains) {
            if (!r.fence && calls - r.call >= 16) {
                r.fence = ++fence_value;
                queue->Signal(fence.get(), r.fence);
            }
            if (r.fence && r.fence <= done && r.handle)
                if (libra_error_t err = lib.d3d12_filter_chain_free(&r.handle)) lib.error_free(&err);
        }
        std::erase_if(retired_chains, [](const RetiredChain& r) { return !r.handle; });
    }

    D3D12_CPU_DESCRIPTOR_HANDLE rtv(const Tex& t) const {
        D3D12_CPU_DESCRIPTOR_HANDLE h = rtv_heap->GetCPUDescriptorHandleForHeapStart();
        h.ptr += static_cast<SIZE_T>(t.rtv_slot) * rtv_size;
        return h;
    }

    // Returns true when the texture was (re)created.
    bool ensure(Tex& t, uint32_t w, uint32_t h, DXGI_FORMAT fmt) {
        if (t.res && t.w == w && t.h == h && t.fmt == fmt) return false;
        if (t.res) retired.push_back({t.res, calls, 0});
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width = w;
        rd.Height = h;
        rd.DepthOrArraySize = 1;
        rd.MipLevels = 1;
        rd.Format = fmt;
        rd.SampleDesc.Count = 1;
        rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_CLEAR_VALUE cv{};
        cv.Format = fmt;
        cv.Color[3] = 1.0f;
        check(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                                              &cv, IID_PPV_ARGS(t.res.put())), "CreateCommittedResource(post texture)");
        t.w = w; t.h = h; t.fmt = fmt;
        t.state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        device->CreateRenderTargetView(t.res.get(), nullptr, rtv(t));
        return true;
    }

    static void transition(ID3D12GraphicsCommandList* cmd, Tex& t, D3D12_RESOURCE_STATES to) {
        if (t.state == to) return;
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = t.res.get();
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b.Transition.StateBefore = t.state;
        b.Transition.StateAfter = to;
        cmd->ResourceBarrier(1, &b);
        t.state = to;
    }

    struct DrawArgs {
        ID3D12Resource* src = nullptr;      // t0
        ID3D12Resource* src1 = nullptr;     // t1 (beam: previous refresh)
        ID3D12Resource* src2 = nullptr;     // t2 (beam: the one before)
        uint32_t sw = 0, sh = 0;
        Rect rect{};
        Filter filter = kNearest;
        Encode encode = kCopy;
        Encode background = kSdr;
        bool final_pass = false;            // adjustments, curvature and bezel apply
        bool beam = false;
        float beam_pos = 0, beam_fph = 1;
        float uvs_x = 1, uvs_y = 1;        // valid region of src / its texture size
    };

    // One native full-target draw into the given render target.
    void draw(ID3D12GraphicsCommandList* cmd, const DrawArgs& a, D3D12_CPU_DESCRIPTOR_HANDLE target,
              DXGI_FORMAT fmt, uint32_t tw, uint32_t th) {
        const uint32_t slot = (srv_next++ % kSrvTriples) * 3;
        D3D12_CPU_DESCRIPTOR_HANDLE cpu = srv_heap->GetCPUDescriptorHandleForHeapStart();
        cpu.ptr += static_cast<SIZE_T>(slot) * srv_size;
        D3D12_GPU_DESCRIPTOR_HANDLE gpu = srv_heap->GetGPUDescriptorHandleForHeapStart();
        gpu.ptr += static_cast<UINT64>(slot) * srv_size;
        ID3D12Resource* srcs[3] = {a.src, a.src1 ? a.src1 : a.src, a.src2 ? a.src2 : a.src};
        for (ID3D12Resource* r : srcs) {
            device->CreateShaderResourceView(r, nullptr, cpu);
            cpu.ptr += srv_size;
        }

        const Settings& s = settings;
        Constants c{};
        c.dst_rect[0] = a.rect.x0; c.dst_rect[1] = a.rect.y0; c.dst_rect[2] = a.rect.x1; c.dst_rect[3] = a.rect.y1;
        c.src_size[0] = static_cast<float>(a.sw); c.src_size[1] = static_cast<float>(a.sh);
        c.src_size[2] = 1.0f / static_cast<float>(a.sw); c.src_size[3] = 1.0f / static_cast<float>(a.sh);
        c.adj[0] = 1.0f / std::max(s.gamma, 0.05f);
        c.adj[1] = s.saturation;
        c.adj[2] = s.brightness;
        c.adj[3] = s.contrast;
        c.hdr[0] = s.paper_white_nits;
        c.hdr[1] = std::max(s.peak_nits, 1.0f);
        c.hdr[2] = s.sdr_decode_gamma;
        c.mode[0] = a.filter;
        c.mode[1] = a.encode;
        const float rw = a.rect.x1 - a.rect.x0, rh = a.rect.y1 - a.rect.y0;
        c.mode[2] = static_cast<uint32_t>(std::clamp(std::ceil(static_cast<float>(a.sw) / rw - 1e-4f), 1.0f, 16.0f));
        c.mode[3] = static_cast<uint32_t>(std::clamp(std::ceil(static_cast<float>(a.sh) / rh - 1e-4f), 1.0f, 16.0f));
        if (a.final_pass) {
            const float k = std::clamp(s.curvature, 0.0f, 1.0f);
            c.geom[0] = 0.12f * k;      // horizontal bow from the vertical position
            c.geom[1] = 0.16f * k;
            c.geom[2] = std::clamp(s.corner_radius, 0.0f, 0.25f);
            c.geom[3] = a.encode == kCopy ? 0.0f : std::clamp(s.sharpen, 0.0f, 1.0f);
            c.mode2[1] = s.bezel ? 1u : 0u;
            c.mode2[2] = s.keep_background && !s.bezel ? 1u : 0u;
        }
        c.mode2[0] = a.background;
        c.beam[0] = a.beam_pos;
        c.beam[1] = a.beam_fph;
        c.beam[2] = std::clamp(s.beam.gain, 0.05f, 1.0f);
        c.beam[3] = std::clamp(s.beam.gamma, 1.0f, 4.0f);
        c.uvs[0] = a.uvs_x; c.uvs[1] = a.uvs_y; c.uvs[2] = c.uvs[3] = 1.0f;

        ID3D12DescriptorHeap* heaps[] = {srv_heap.get()};
        cmd->SetDescriptorHeaps(1, heaps);
        cmd->SetGraphicsRootSignature(root.get());
        cmd->SetPipelineState(pso_for(fmt, a.beam));
        cmd->SetGraphicsRoot32BitConstants(0, sizeof(Constants) / 4, &c, 0);
        cmd->SetGraphicsRootDescriptorTable(1, gpu);
        const D3D12_VIEWPORT vp{0, 0, static_cast<float>(tw), static_cast<float>(th), 0, 1};
        const D3D12_RECT sc{0, 0, static_cast<LONG>(tw), static_cast<LONG>(th)};
        cmd->RSSetViewports(1, &vp);
        cmd->RSSetScissorRects(1, &sc);
        cmd->OMSetRenderTargets(1, &target, FALSE, nullptr);
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmd->DrawInstanced(3, 1, 0, 0);
    }

    // 1:1 copy of a module texture into another (same size).
    void copy(ID3D12GraphicsCommandList* cmd, Tex& from, Tex& to) {
        ensure(to, from.w, from.h, from.fmt);
        transition(cmd, to, D3D12_RESOURCE_STATE_RENDER_TARGET);
        DrawArgs a;
        a.src = from.res.get(); a.sw = from.w; a.sh = from.h;
        a.rect = {0, 0, static_cast<float>(from.w), static_cast<float>(from.h)};
        draw(cmd, a, rtv(to), to.fmt, to.w, to.h);
        transition(cmd, to, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }

    // --------------------------------------------------------------- presets
    std::string error_text(libra_error_t err) {
        std::string text = "librashader error";
        char* raw = nullptr;
        if (lib.error_write(err, &raw) == 0 && raw) {
            text = raw;
            lib.error_free_string(&raw);
        }
        lib.error_free(&err);
        return text;
    }

    // Every file the preset pulls in, found by following what the text names:
    // #reference lines, any value that resolves to an existing file, and
    // #include lines inside shader sources.
    static void scan_deps(const fs::path& file, std::set<fs::path>& seen, int depth = 0) {
        std::error_code ec;
        const fs::path canon = fs::weakly_canonical(file, ec);
        if (ec || !fs::is_regular_file(canon, ec) || !seen.insert(canon).second || depth > 24) return;
        const std::string ext = canon.extension().string();
        const bool is_preset = ext == ".slangp";
        const bool is_source = ext == ".slang" || ext == ".inc" || ext == ".h" || ext == ".glsl";
        if (!is_preset && !is_source) return;
        std::ifstream in(canon, std::ios::binary);
        std::string line;
        const fs::path dir = canon.parent_path();
        auto unquote = [](std::string s) {
            const auto a = s.find_first_not_of(" \t\r\"");
            const auto b = s.find_last_not_of(" \t\r\"");
            return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
        };
        while (std::getline(in, line)) {
            const auto first = line.find_first_not_of(" \t");
            if (first == std::string::npos) continue;
            if (line.compare(first, 10, "#reference") == 0) {
                scan_deps(dir / path_from_utf8(unquote(line.substr(first + 10))), seen, depth + 1);
            } else if (line.compare(first, 8, "#include") == 0) {
                scan_deps(dir / path_from_utf8(unquote(line.substr(first + 8))), seen, depth + 1);
            } else if (is_preset && line[first] != '#') {
                const auto eq = line.find('=');
                if (eq == std::string::npos) continue;
                const std::string value = unquote(line.substr(eq + 1));
                if (value.empty() || value.find_first_of("./\\") == std::string::npos) continue;
                scan_deps(dir / path_from_utf8(value), seen, depth + 1);
            }
        }
    }

    static std::vector<Watched> watch_list(const std::string& preset) {
        std::set<fs::path> seen;
        scan_deps(path_from_utf8(preset), seen);
        std::vector<Watched> out;
        for (const auto& p : seen) {
            std::error_code ec;
            out.push_back({p, fs::last_write_time(p, ec), fs::file_size(p, ec)});
        }
        return out;
    }

    static bool changed(const std::vector<Watched>& deps) {
        for (const auto& w : deps) {
            std::error_code e1, e2;
            const auto t = fs::last_write_time(w.path, e1);
            const auto s = fs::file_size(w.path, e2);
            if (e1 || e2 || t != w.mtime || s != w.size) return true;
        }
        return false;
    }

    bool build(const std::string& path, uint32_t frames_in_flight, Chain& out, std::string* error) {
        if (!lib_ok) {
            if (error) *error = "shader presets are unavailable: " + lib_version;
            return false;
        }
        Chain c;
        c.path = path;
        c.frames_in_flight = frames_in_flight;
        c.deps = watch_list(path);
        libra_shader_preset_t preset = nullptr;
        libra_preset_opt_t popt{};
        popt.version = LIBRASHADER_CURRENT_VERSION;
        popt.original_aspect_uniforms = true;
        popt.frametime_uniforms = true;
        if (libra_error_t err = lib.preset_create_with_options(path.c_str(), nullptr, &popt, &preset)) {
            if (error) *error = error_text(err);
            return false;
        }
        libra_preset_param_list_t list{};
        if (libra_error_t err = lib.preset_get_runtime_params(&preset, &list)) {
            lib.error_free(&err);
        } else {
            for (uint64_t i = 0; i < list.length; ++i) {
                const libra_preset_param_t& p = list.parameters[i];
                Parameter q;
                q.name = p.name ? p.name : "";
                q.description = p.description ? p.description : "";
                q.initial = p.initial; q.minimum = p.minimum; q.maximum = p.maximum; q.step = p.step;
                q.value = q.preset_value = p.initial;
                c.params.push_back(std::move(q));
            }
            lib.preset_free_runtime_params(list);
        }
        if (libra_error_t err = lib.preset_color_space(&preset, &c.color_space)) {
            lib.error_free(&err);
            c.color_space = LIBRA_COLOR_SPACE_SDR;
        }
        filter_chain_d3d12_opt_t opt{};
        opt.version = LIBRASHADER_CURRENT_VERSION;
        opt.frames_in_flight = frames_in_flight;
        opt.force_hlsl_pipeline = force_hlsl;
        if (libra_error_t err = lib.d3d12_filter_chain_create(&preset, device, &opt, &c.handle)) {
            if (error) *error = error_text(err);
            if (preset) lib.preset_free(&preset);
            return false;
        }
        if (preset) lib.preset_free(&preset);
        // The values the chain will really use (shader defaults overlaid by the preset).
        for (auto& p : c.params) {
            float v = 0;
            if (libra_error_t e2 = lib.d3d12_filter_chain_get_param(&c.handle, p.name.c_str(), &v)) lib.error_free(&e2);
            else p.value = p.preset_value = v;
        }
        out = std::move(c);
        return true;
    }

    void destroy(Chain& c) {
        if (c.handle) {
            wait_idle();
            if (libra_error_t err = lib.d3d12_filter_chain_free(&c.handle)) lib.error_free(&err);
        }
        c = Chain{};
    }

    void set_value(Chain& c, Parameter& p, float value) {
        if (p.value == value) return;
        if (libra_error_t err = lib.d3d12_filter_chain_set_param(&c.handle, p.name.c_str(), value)) lib.error_free(&err);
        else p.value = value;
    }

    // Bring every parameter of the CRT chain to: user override, else
    // automatic fix (if enabled), else the preset's own value.
    void apply_main() {
        params_dirty = false;
        applied_fix = settings.auto_progressive_fix;
        applied_curv = settings.preset_curvature;
        stats.auto_fixes = 0;
        if (!chain.handle) return;
        for (auto& p : chain.params) {
            float want = p.preset_value;
            bool fixed = false;
            if (settings.auto_progressive_fix)
                for (const auto& [name, v] : kProgressiveFix)
                    if (p.name == name) { want = v; fixed = true; }
            if (!settings.preset_curvature)
                for (const auto& [name, v] : kCurvatureOff)
                    if (p.name == name) { want = v; fixed = true; }
            auto it = overrides.find(p.name);
            if (it != overrides.end()) { want = it->second; fixed = false; }
            if (fixed && want != p.preset_value) ++stats.auto_fixes;
            // HDR drive parameters are set per frame in run_chain().
            if (it == overrides.end() && (p.name == "hcrt_max_nits" || p.name == "hcrt_paper_white_nits" || p.name == "hcrt_hdr")) continue;
            set_value(chain, p, want);
        }
    }

    void apply_sub() {
        if (!sub.handle) return;
        for (auto& p : sub.params) {
            auto it = sub_overrides.find(p.name);
            set_value(sub, p, it != sub_overrides.end() ? it->second : p.preset_value);
        }
    }

    void set_if_present(Chain& c, const char* name, float value) {
        if (overrides.count(name)) return;
        for (auto& p : c.params)
            if (p.name == name) set_value(c, p, value);
    }

    bool replace(Chain& slot, const std::string& path, uint32_t fif, std::string* error) {
        if (path.empty()) {
            destroy(slot);
            return true;
        }
        Chain next;
        if (!build(path, fif, next, error)) return false;
        destroy(slot);
        slot = std::move(next);
        return true;
    }

    Rect placement(uint32_t ow, uint32_t oh) const {
        if (settings.viewport_mode == ViewportMode::Full) return {0, 0, static_cast<float>(ow), static_cast<float>(oh)};
        const float aspect = std::max(settings.display_aspect, 0.1f);
        float h = static_cast<float>(oh);
        if (settings.integer_scale && settings.base_height && oh >= settings.base_height)
            h = static_cast<float>((oh / settings.base_height) * settings.base_height);
        float w = std::round(h * aspect);
        if (w > static_cast<float>(ow)) {          // wider than the display: letterbox
            w = static_cast<float>(ow);
            h = std::round(w / aspect);
            if (settings.integer_scale && settings.base_height && h >= static_cast<float>(settings.base_height)) {
                h = std::floor(h / static_cast<float>(settings.base_height)) * static_cast<float>(settings.base_height);
                w = std::min(std::round(h * aspect), static_cast<float>(ow));
            }
        }
        const float x = std::floor((static_cast<float>(ow) - w) * 0.5f);
        const float y = std::floor((static_cast<float>(oh) - h) * 0.5f);
        return {x, y, x + w, y + h};
    }

    bool libra_frame(Chain& c, ID3D12GraphicsCommandList* cmd, ID3D12Resource* src, Tex& dst, uint64_t count,
                     bool clear, uint32_t sub_index, uint32_t total, LIBRA_COLOR_SPACE cs) {
        transition(cmd, dst, D3D12_RESOURCE_STATE_RENDER_TARGET);
        libra_image_d3d12_t in{};
        in.image_type = LIBRA_D3D12_IMAGE_TYPE_RESOURCE;
        in.handle.resource = src;
        libra_image_d3d12_t out{};
        out.image_type = LIBRA_D3D12_IMAGE_TYPE_OUTPUT_IMAGE;
        out.handle.output.descriptor = rtv(dst);
        out.handle.output.format = dst.fmt;
        out.handle.output.width = dst.w;
        out.handle.output.height = dst.h;
        frame_d3d12_opt_t fo{};
        fo.version = LIBRASHADER_CURRENT_VERSION;
        fo.clear_history = clear;
        fo.frame_direction = 1;
        fo.rotation = 0;
        fo.total_subframes = std::max(total, 1u);
        fo.current_subframe = sub_index + 1;
        fo.aspect_ratio = settings.display_aspect;
        fo.frames_per_second = settings.source_fps;
        fo.frametime_delta = static_cast<uint32_t>(std::lround(1000.0f / std::max(settings.source_fps, 1.0f)));
        fo.color_space = cs;
        fo.brightness_nits = settings.paper_white_nits;
        fo.expand_gamut = settings.expand_gamut ? 1u : 0u;
        libra_error_t err = lib.d3d12_filter_chain_frame(&c.handle, cmd, static_cast<size_t>(count), in, out, nullptr, nullptr, &fo);
        transition(cmd, dst, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        if (err) {
            last_error = error_text(err);
            return false;
        }
        return true;
    }

    // Runs the CRT preset on `src` into `crt`, sized for the given output.
    bool run_chain(ID3D12GraphicsCommandList* cmd, ID3D12Resource* src, uint32_t ow, uint32_t oh, uint64_t count) {
        const Settings& s = settings;
        if (params_dirty || applied_fix != s.auto_progressive_fix || applied_curv != s.preset_curvature) apply_main();
        const Rect r = placement(ow, oh);
        const uint32_t cw = static_cast<uint32_t>(r.x1 - r.x0), ch = static_cast<uint32_t>(r.y1 - r.y0);
        const bool hdr_aware = chain.color_space != LIBRA_COLOR_SPACE_SDR;
        LIBRA_COLOR_SPACE cs = LIBRA_COLOR_SPACE_SDR;
        DXGI_FORMAT fmt = DXGI_FORMAT_R8G8B8A8_UNORM;
        if (hdr_aware && s.color_space == OutputColorSpace::HDR10) { cs = LIBRA_COLOR_SPACE_HDR10; fmt = DXGI_FORMAT_R10G10B10A2_UNORM; }
        if (hdr_aware && s.color_space == OutputColorSpace::scRGB) { cs = LIBRA_COLOR_SPACE_SC_RGB; fmt = DXGI_FORMAT_R16G16B16A16_FLOAT; }
        chain_encodes_hdr = cs != LIBRA_COLOR_SPACE_SDR;
        if (hdr_aware) {
            // Parameters by which HDR-aware presets (Sony Megatron) take the
            // display description; explicit overrides win.
            set_if_present(chain, "hcrt_max_nits", s.peak_nits);
            set_if_present(chain, "hcrt_paper_white_nits", s.paper_white_nits);
            set_if_present(chain, "hcrt_hdr", chain_encodes_hdr ? 1.0f : 0.0f);
        }
        ensure(crt, cw, ch, fmt);
        const bool ok = libra_frame(chain, cmd, src, crt, count, clear_history, 0, 1, cs);
        clear_history = false;
        if (!ok) chain_encodes_hdr = false;
        stats.chain_width = cw; stats.chain_height = ch; stats.chain_format = fmt;
        stats.preset_is_hdr_aware = hdr_aware;
        return ok;
    }
};

PostChain::PostChain(ID3D12Device* device, ID3D12CommandQueue* queue, const std::wstring& runtime_dir)
    : impl_(std::make_unique<Impl>()) {
    impl_->device = device;
    impl_->queue = queue;
    impl_->create_native();
    impl_->load_runtime(runtime_dir);
}

PostChain::~PostChain() {
    impl_->destroy(impl_->chain);
    impl_->destroy(impl_->sub);
    impl_->wait_idle();
    if (impl_->fence_event) CloseHandle(impl_->fence_event);
}

bool PostChain::shader_runtime_available() const { return impl_->lib_ok; }
std::string PostChain::shader_runtime_version() const { return impl_->lib_version; }
const std::string& PostChain::preset_path() const { return impl_->chain.path; }
Settings& PostChain::settings() { return impl_->settings; }
const Settings& PostChain::settings() const { return impl_->settings; }
const Stats& PostChain::stats() const { return impl_->stats; }
uint64_t PostChain::frame_count() const { return impl_->frame_count; }
void PostChain::set_force_hlsl_pipeline(bool force) { impl_->force_hlsl = force; }
void PostChain::reset_history() {
    impl_->clear_history = impl_->sub_clear_history = true;
    impl_->beam_valid = false;
}

DXGI_FORMAT PostChain::output_format_for(OutputColorSpace cs) {
    switch (cs) {
        case OutputColorSpace::HDR10: return DXGI_FORMAT_R10G10B10A2_UNORM;
        case OutputColorSpace::scRGB: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        default: return DXGI_FORMAT_R8G8B8A8_UNORM;
    }
}

bool PostChain::set_preset(const std::string& path, std::string* error) {
    if (!impl_->replace(impl_->chain, path, 8, error)) return false;
    impl_->apply_main();
    impl_->clear_history = true;
    impl_->chain_dirty = true;
    impl_->chain_ok = false;
    return true;
}

struct PreparedPreset::Data {
    Chain chain;
    libra_instance_t lib{};
    ~Data() {
        if (chain.handle)
            if (libra_error_t err = lib.d3d12_filter_chain_free(&chain.handle)) lib.error_free(&err);
    }
};
PreparedPreset::PreparedPreset() : d_(std::make_unique<Data>()) {}
PreparedPreset::~PreparedPreset() = default;
const std::string& PreparedPreset::path() const { return d_->chain.path; }
const std::vector<Parameter>& PreparedPreset::parameters() const { return d_->chain.params; }

std::unique_ptr<PreparedPreset> PostChain::prepare_preset(const std::string& path, std::string* error) const {
    std::unique_ptr<PreparedPreset> p(new PreparedPreset());
    p->d_->lib = impl_->lib;
    if (path.empty()) return p;   // pass-through
    if (!impl_->build(path, 8, p->d_->chain, error)) return nullptr;
    return p;
}

void PostChain::install_preset(std::unique_ptr<PreparedPreset> preset) {
    if (!preset) return;
    Impl& m = *impl_;
    m.retire(m.chain);
    m.chain = std::move(preset->d_->chain);
    preset->d_->chain = Chain{};
    m.apply_main();
    m.clear_history = true;
    m.chain_dirty = true;
    m.chain_ok = false;
}

uint64_t PostChain::preset_fingerprint(const std::string& path) {
    if (path.empty()) return 0;
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint64_t v) { h ^= v; h *= 1099511628211ull; };
    for (const Watched& w : Impl::watch_list(path)) {
        mix(std::hash<std::string>{}(w.path.string()));
        mix(static_cast<uint64_t>(w.mtime.time_since_epoch().count()));
        mix(static_cast<uint64_t>(w.size));
    }
    return h;
}

bool PostChain::set_subframe_preset(const std::string& path, std::string* error) {
    if (!impl_->replace(impl_->sub, path, 8, error)) return false;
    impl_->apply_sub();
    impl_->sub_clear_history = true;
    return true;
}

bool PostChain::poll_reload(std::string* error) {
    Impl& m = *impl_;
    bool reloaded = false;
    for (int i = 0; i < 2; ++i) {
        Chain& c = i ? m.sub : m.chain;
        if (!c.handle || !Impl::changed(c.deps)) continue;
        const std::string path = c.path;
        const uint32_t fif = c.frames_in_flight;
        if (m.replace(c, path, fif, error)) {
            if (i) { m.apply_sub(); m.sub_clear_history = true; }
            else { m.apply_main(); m.clear_history = true; m.chain_dirty = true; m.chain_ok = false; }
            ++m.stats.reloads;
            reloaded = true;
        } else {
            // Keep running the old chain; do not retry until the files change again.
            c.deps = Impl::watch_list(path);
            ++m.stats.reload_failures;
        }
    }
    return reloaded;
}

void PostChain::set_parameter(const std::string& name, float value) {
    impl_->overrides[name] = value;
    impl_->apply_main();
    impl_->chain_dirty = true;
}
void PostChain::clear_parameter(const std::string& name) {
    impl_->overrides.erase(name);
    impl_->apply_main();
    impl_->chain_dirty = true;
}
void PostChain::clear_parameters() {
    impl_->overrides.clear();
    impl_->apply_main();
    impl_->chain_dirty = true;
}
std::vector<Parameter> PostChain::parameters() const { return impl_->chain.params; }
void PostChain::set_subframe_parameter(const std::string& name, float value) {
    impl_->sub_overrides[name] = value;
    impl_->apply_sub();
}

void PostChain::process(ID3D12GraphicsCommandList* cmd, ID3D12Resource* input, uint32_t out_width, uint32_t out_height) {
    process(cmd, input, out_width, out_height, 0, 0);
}

void PostChain::process(ID3D12GraphicsCommandList* cmd, ID3D12Resource* input, uint32_t, uint32_t,
                        uint32_t valid_width, uint32_t valid_height) {
    Impl& m = *impl_;
    m.collect_retired();
    const Settings& s = m.settings;
    const D3D12_RESOURCE_DESC in_desc = input->GetDesc();
    const uint32_t texw = static_cast<uint32_t>(in_desc.Width), texh = in_desc.Height;
    // The valid region (top-left of the texture); 0 = the whole texture.
    const uint32_t iw = valid_width && valid_width <= texw ? valid_width : texw;
    const uint32_t ih = valid_height && valid_height <= texh ? valid_height : texh;

    // Source preparation. Either way the result lives in a texture the module
    // owns, so the caller's image is not needed after this command list.
    uint32_t tw = iw, th = ih;
    Impl::DrawArgs a;
    a.src = input; a.sw = iw; a.sh = ih;
    a.uvs_x = static_cast<float>(iw) / static_cast<float>(texw);
    a.uvs_y = static_cast<float>(ih) / static_cast<float>(texh);
    Tex* target = &m.latest;
    if (s.source_mode != SourceMode::AsIs && s.base_height && s.base_width) {
        const float widen = std::max(s.display_aspect, 0.1f) / std::max(s.base_aspect, 0.1f);
        tw = s.source_mode == SourceMode::DownsampleToBase
                 ? static_cast<uint32_t>(std::lround(static_cast<float>(s.base_width) * widen)) : iw;
        th = s.base_height;
        if (tw != iw || th != ih) { a.filter = kBox; target = &m.prep; }
        else { tw = iw; th = ih; }
    }
    m.ensure(*target, tw, th, DXGI_FORMAT_R8G8B8A8_UNORM);
    Impl::transition(cmd, *target, D3D12_RESOURCE_STATE_RENDER_TARGET);
    a.rect = {0, 0, static_cast<float>(tw), static_cast<float>(th)};
    m.draw(cmd, a, m.rtv(*target), target->fmt, tw, th);
    Impl::transition(cmd, *target, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    m.owned = target;

    if (tw != m.src_w || th != m.src_h) {
        m.clear_history = m.sub_clear_history = true;   // history textures change size
        m.beam_valid = false;
        m.src_w = tw; m.src_h = th;
    }
    m.chain_dirty = true;
    ++m.frame_count;
    ++m.stats.source_frames;
}

void PostChain::present(ID3D12GraphicsCommandList* cmd, const Output& out, uint32_t subframe, uint32_t total) {
    Impl& m = *impl_;
    total = std::max(total, 1u);
    if (subframe == 0) ++m.refresh_count;
    m.legacy_sub = subframe;
    m.legacy_total = total;
    RefreshTiming t;
    t.span = 1.0 / static_cast<double>(total);
    t.source_time = static_cast<double>(m.refresh_count ? m.refresh_count - 1 : 0) + static_cast<double>(subframe) * t.span;
    present(cmd, out, t);
    m.legacy_sub = 0;
    m.legacy_total = 1;
}

void PostChain::present(ID3D12GraphicsCommandList* cmd, const Output& out, const RefreshTiming& timing) {
    Impl& m = *impl_;
    m.collect_retired();
    ++m.stats.presents;
    const Settings& s = m.settings;
    if (!m.owned) {   // nothing processed yet: black
        const float black[4] = {0, 0, 0, 1};
        cmd->ClearRenderTargetView(out.rtv, black, 0, nullptr);
        return;
    }
    Tex* pic = m.owned;

    // Beam simulator: first in the chain, on the unprocessed game picture, as
    // its authors specify. One pass per display refresh.
    const bool beam = s.beam.enabled && timing.span > 0.0 && timing.span < 0.999;
    m.stats.beam_active = beam;
    int64_t k = 0;
    if (beam) {
        double fph = 1.0 / timing.span;                 // display refreshes per emulated refresh
        double t = timing.source_time;
        const double nearest = std::round(fph);
        if (s.beam.lcd_anti_retention && std::fabs(fph - nearest) < 1e-3 && std::fmod(nearest, 2.0) == 0.0) {
            t = t * fph / (fph + 0.001);                // the simulated tube runs very slightly slow
            fph += 0.001;
        }
        k = static_cast<int64_t>(std::floor(t));
        const double pos = t - static_cast<double>(k);
        // History: prev1 is the picture the beam is drawing now (emulated
        // refresh k-1 in the original's numbering), prev2 the one before.
        if (!m.beam_valid || m.prev1.w != pic->w || m.prev1.h != pic->h) {
            m.copy(cmd, *pic, m.prev1);
            m.copy(cmd, *pic, m.prev2);
            m.beam_valid = true;
            m.beam_k = k;
        } else if (k > m.beam_k) {
            const int64_t steps = std::min<int64_t>(k - m.beam_k, 2);
            for (int64_t i = 0; i < steps; ++i) {
                std::swap(m.prev1.res, m.prev2.res);     // same size, format and state
                std::swap(m.prev1.rtv_slot, m.prev2.rtv_slot);
                m.copy(cmd, *pic, m.prev1);
            }
            m.stats.beam_refreshes += static_cast<uint64_t>(k - m.beam_k);
            m.beam_k = k;
        }
        m.ensure(m.beamtex, pic->w, pic->h, DXGI_FORMAT_R8G8B8A8_UNORM);
        Impl::transition(cmd, m.beamtex, D3D12_RESOURCE_STATE_RENDER_TARGET);
        Impl::DrawArgs a;
        a.src = pic->res.get(); a.src1 = m.prev1.res.get(); a.src2 = m.prev2.res.get();
        a.sw = pic->w; a.sh = pic->h;
        a.rect = {0, 0, static_cast<float>(pic->w), static_cast<float>(pic->h)};
        a.beam = true;
        a.beam_pos = static_cast<float>(pos);
        a.beam_fph = static_cast<float>(fph);
        m.draw(cmd, a, m.rtv(m.beamtex), m.beamtex.fmt, pic->w, pic->h);
        Impl::transition(cmd, m.beamtex, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        pic = &m.beamtex;
    }

    // CRT chain: once per game image, or once per display refresh behind the beam simulator.
    bool chain_ran = false;
    if (m.chain.handle) {
        if (beam || m.chain_dirty || !m.chain_ok || m.crt.w == 0) {
            const uint64_t count = beam ? static_cast<uint64_t>(std::max<int64_t>(k, 0)) : (m.frame_count ? m.frame_count - 1 : 0);
            m.chain_ok = m.run_chain(cmd, pic->res.get(), out.width, out.height, count);
            m.chain_dirty = beam;   // after beam frames the plain picture must be redone
        }
        if (m.chain_ok) { pic = &m.crt; chain_ran = true; }
        else m.chain_encodes_hdr = false;
    } else {
        m.chain_encodes_hdr = false;
        m.stats.chain_width = m.stats.chain_height = 0;
        m.stats.chain_format = DXGI_FORMAT_UNKNOWN;
        m.stats.preset_is_hdr_aware = false;
    }
    m.stats.chain_encodes_hdr = m.chain_encodes_hdr;

    // Optional librashader sub-frame preset (BFI), only without the native simulator.
    if (m.sub.handle && !beam && m.legacy_total > 1 && !m.chain_encodes_hdr) {
        m.ensure(m.subtex, pic->w, pic->h, DXGI_FORMAT_R8G8B8A8_UNORM);
        if (m.libra_frame(m.sub, cmd, pic->res.get(), m.subtex, m.refresh_count ? m.refresh_count - 1 : 0,
                          m.sub_clear_history, m.legacy_sub, m.legacy_total, LIBRA_COLOR_SPACE_SDR))
            pic = &m.subtex;
        m.sub_clear_history = false;
    }

    // Final pass.
    const float ow = static_cast<float>(out.width), oh = static_cast<float>(out.height);
    Rect r = m.placement(out.width, out.height);
    if (chain_ran && pic->w == out.width && pic->h == out.height) r = {0, 0, ow, oh};
    const bool same_size = static_cast<uint32_t>(r.x1 - r.x0) == pic->w && static_cast<uint32_t>(r.y1 - r.y0) == pic->h;
    Impl::DrawArgs a;
    a.src = pic->res.get(); a.sw = pic->w; a.sh = pic->h;
    a.rect = r;
    a.final_pass = true;
    a.filter = kNearest;
    if (!same_size) {
        a.filter = (chain_ran || m.sub.handle) ? kBilinear
                 : s.passthrough_filter == PassThroughFilter::Nearest ? kNearest
                 : s.passthrough_filter == PassThroughFilter::Bilinear ? kBilinear : kSharp;
        // Minifying (a high-resolution source shown small): average instead of aliasing.
        if (static_cast<float>(pic->w) > (r.x1 - r.x0) * 1.5f || static_cast<float>(pic->h) > (r.y1 - r.y0) * 1.5f) a.filter = kBox;
    }
    if (s.curvature > 0.0f && a.filter == kNearest) a.filter = kBilinear;
    a.background = s.color_space == OutputColorSpace::HDR10 ? kHdr10 : s.color_space == OutputColorSpace::scRGB ? kScRgb : kSdr;
    a.encode = m.chain_encodes_hdr ? kCopy : a.background;
    m.draw(cmd, a, out.rtv, out.format, out.width, out.height);
}

}  // namespace gdl::post
