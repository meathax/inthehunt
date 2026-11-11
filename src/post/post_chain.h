// Post-processing chain for the Gauntlet Dark Legacy PC port.
//
// Standalone module: it knows nothing about the game, the Voodoo renderer or
// the window. The caller (the presentation layer) owns the Direct3D 12
// device, the command queue, the input texture and the output target. The
// module owns no device, performs no read-back and executes nothing: it
// records draws into the caller's open command list.
//
//   source prepare   -> optional box downsample of a high-resolution render
//                       to the base picture (511x384, or wider for widescreen)
//                       or to "full width x 384 lines"                [native]
//   beam simulator   -> optional Blur Busters CRT beam simulation, one
//                       sub-frame per display refresh, with the two previous
//                       emulated refreshes as history                 [native]
//   CRT chain        -> any RetroArch .slangp preset through librashader
//                       (D3D12 runtime, C API, loaded from librashader.dll)
//   final pass       -> placement (aspect, letterbox/pillarbox, integer
//                       scale), curvature, bezel, gamma, saturation,
//                       contrast-adaptive sharpening, and the output
//                       encoding: SDR, scRGB or HDR10                 [native]
//
// Everything marked [native] works without librashader.dll.
//
// See docs/post_chain.md for the integration contract.
#pragma once

#include <windows.h>
#include <d3d12.h>
#include <dxgiformat.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace gdl::post {

enum class OutputColorSpace : uint32_t {
    SDR = 0,    // R8G8B8A8_UNORM / B8G8R8A8_UNORM / R10G10B10A2_UNORM, sRGB-encoded values
    HDR10 = 1,  // R10G10B10A2_UNORM, BT.2020 primaries, ST.2084 (PQ) encoded
    scRGB = 2,  // R16G16B16A16_FLOAT, BT.709 primaries, linear, 1.0 = 80 nits
};

enum class SourceMode : uint32_t {
    AsIs = 0,            // feed the input texture to the chain unchanged
    DownsampleToBase,    // box-filter (linear light) to the base picture size
    ScanStructure,       // keep the input width, box-filter vertically to base_height lines
};

enum class ViewportMode : uint32_t {
    Aspect = 0,  // the chain renders into an aspect-correct rectangle; bars are black
    Full,        // the chain gets the whole output (bezel presets place the picture themselves)
};

enum class PassThroughFilter : uint32_t { Nearest = 0, Bilinear, SharpBilinear };

struct BeamSettings {
    bool enabled = false;      // native CRT beam simulator; needs a display faster than the source
    float gain = 0.7f;         // "brightness vs clarity": time-averaged light = gain x picture.
                               // 1.0 = full brightness (bright pixels stay lit for the whole refresh),
                               // 0.5 = half brightness, every pixel lit at most half the time
    float gamma = 2.4f;        // gamma of the picture and the display, as in the original shader
    bool lcd_anti_retention = true;  // when the display rate is an even multiple of the source rate,
                               // run the simulated tube 0.001 display refresh per refresh slow so that
                               // the bright band drifts and an LCD's polarity inversion cannot burn in
};

struct Settings {
    // --- source preparation
    SourceMode source_mode = SourceMode::AsIs;
    uint32_t base_width = 511;            // base picture of the 4:3 game
    uint32_t base_height = 384;
    float base_aspect = 4.0f / 3.0f;      // aspect the base picture is shown at

    // --- placement
    ViewportMode viewport_mode = ViewportMode::Aspect;
    float display_aspect = 4.0f / 3.0f;   // aspect of the picture as shown: 4:3, or 16:9 / 21:9 for a
                                          // widescreen source. Pillarbox / letterbox bars follow from it.
    bool integer_scale = false;           // largest integer multiple of base_height that fits
    PassThroughFilter passthrough_filter = PassThroughFilter::SharpBilinear;  // no-preset path

    // --- simple adjustments (final pass; independent of librashader)
    float gamma = 1.0f;        // output = input^(1/gamma); 1 = unchanged
    float saturation = 1.0f;   // 0 = grey, 1 = unchanged
    float brightness = 0.0f;   // added, -1..1
    float contrast = 1.0f;     // around mid grey
    float sharpen = 0.0f;      // contrast-adaptive sharpening, 0 = off .. 1 = strongest
    float curvature = 0.0f;    // native tube curvature, 0 = flat .. 1 = strong (0.15 is a gentle 27" face)
    float corner_radius = 0.0f;  // rounded picture corners, fraction of the picture height
    bool bezel = false;        // native plain bezel around the picture instead of black bars
    bool keep_background = false;  // In the Hunt port: leave the output outside the picture as the
                                   // caller drew it (border art) instead of painting black bars
    bool preset_curvature = true;   // false: switch the loaded preset's own curvature parameters off

    // --- preset handling
    bool auto_progressive_fix = true;  // set the known interlace-detection parameters of the loaded
                                       // preset to "progressive" (this game is 384 lines progressive,
                                       // which most presets mistake for interlaced)

    // --- sub-frame beam simulation
    BeamSettings beam;

    // --- output encoding
    OutputColorSpace color_space = OutputColorSpace::SDR;
    float paper_white_nits = 200.0f;   // luminance of SDR white (1.0) in HDR modes
    float peak_nits = 1000.0f;         // display peak; values are limited to this
    float sdr_decode_gamma = 2.2f;     // gamma used to linearise the SDR picture for HDR (0 = sRGB curve)
    bool expand_gamut = false;         // passed to HDR-aware presets (ExpandGamut uniform)

    // --- timing uniforms
    float source_fps = 60.386f;        // OriginalFPS for shaders that ask
};

struct Parameter {
    std::string name;
    std::string description;
    float initial = 0, minimum = 0, maximum = 0, step = 0;
    float preset_value = 0;   // value from the preset file (or the shader default)
    float value = 0;          // value in use (override, automatic fix, or preset_value)
};

// The render target the final pass writes to (normally the swap-chain back
// buffer). It must be in D3D12_RESOURCE_STATE_RENDER_TARGET; it is left in
// that state. The caller owns it.
struct Output {
    ID3D12Resource* resource = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
    DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
    uint32_t width = 0;
    uint32_t height = 0;
};

// Where one display refresh sits on the emulated CRT's time axis. Only the
// beam simulator uses it.
struct RefreshTiming {
    // Start of this display refresh, in emulated CRT refreshes (one unit =
    // one 16.56 ms scanout of the game). The integer part counts emulated
    // refreshes, the fraction is the position of the beam down the tube.
    // Must not go backwards.
    double source_time = 0.0;
    // Length of this display refresh in the same unit: source rate / display
    // rate. 0.25 on a display running at exactly four times the source rate;
    // 60.386 / 240 = 0.2516 on a 240.000 Hz display. Need not be 1/integer.
    double span = 1.0;
};

struct Stats {
    uint64_t source_frames = 0;       // new game images (process calls)
    uint64_t presents = 0;            // display refreshes (present calls)
    uint64_t reloads = 0;             // successful hot reloads
    uint64_t reload_failures = 0;
    uint32_t chain_width = 0, chain_height = 0;   // size the CRT chain renders at
    DXGI_FORMAT chain_format = DXGI_FORMAT_UNKNOWN;
    bool preset_is_hdr_aware = false;
    bool chain_encodes_hdr = false;   // the preset itself writes HDR10/scRGB this frame
    bool beam_active = false;         // the beam simulator ran in the last present
    uint64_t beam_refreshes = 0;      // emulated refreshes the simulator has started
    uint32_t auto_fixes = 0;          // parameters set by auto_progressive_fix / preset_curvature
};

// A preset whose filter chain was built off the render thread
// (PostChain::prepare_preset); hand it to PostChain::install_preset. Dropping
// it without installing frees the chain (no GPU work used it yet).
class PreparedPreset {
public:
    ~PreparedPreset();
    PreparedPreset(const PreparedPreset&) = delete;
    PreparedPreset& operator=(const PreparedPreset&) = delete;
    const std::string& path() const;                 // empty = pass-through
    const std::vector<Parameter>& parameters() const;
private:
    friend class PostChain;
    PreparedPreset();
    struct Data;
    std::unique_ptr<Data> d_;
};

class PostChain {
public:
    // `device` and `queue` belong to the caller and must outlive the object.
    // `runtime_dir` is the folder holding librashader.dll and dxcompiler.dll
    // (empty = the folder of the running executable).
    PostChain(ID3D12Device* device, ID3D12CommandQueue* queue, const std::wstring& runtime_dir = L"");
    ~PostChain();
    PostChain(const PostChain&) = delete;
    PostChain& operator=(const PostChain&) = delete;

    // True when librashader.dll was loaded. Without it everything native
    // still works and set_preset() with a non-empty path fails cleanly.
    bool shader_runtime_available() const;
    std::string shader_runtime_version() const;

    // Load a .slangp preset (UTF-8 path). An empty path selects pass-through.
    // On failure the previous preset stays active and `error` describes why.
    bool set_preset(const std::string& slangp_path, std::string* error = nullptr);
    const std::string& preset_path() const;

    // The same as set_preset() in two halves, so that the slow part does not
    // stall the render thread:
    // prepare_preset() compiles the preset and builds its filter chain. It may
    // run on ANY thread, also while frame() runs on the render thread (it only
    // reads the loaded runtime and uses the device's free-threaded creation
    // calls). Null on failure, with `error` set. An empty path prepares
    // pass-through.
    std::unique_ptr<PreparedPreset> prepare_preset(const std::string& slangp_path, std::string* error = nullptr) const;
    // Render thread, between frame() calls: switch to the prepared preset.
    // Never waits: the previous filter chain is released once the queue has
    // passed every command list that used it. Overrides and automatic fixes
    // are applied; shader history starts again.
    void install_preset(std::unique_ptr<PreparedPreset> preset);
    // Any thread: fingerprint of the preset file and every file it pulls in
    // (path, modification time, size). A change means a reload is due.
    static uint64_t preset_fingerprint(const std::string& slangp_path);

    // Optional librashader sub-frame preset (BFI presets from slang-shaders).
    // It runs after the CRT chain when present() is given sub-frame indices
    // and the native beam simulator is off. Empty path removes it.
    bool set_subframe_preset(const std::string& slangp_path, std::string* error = nullptr);

    // Re-check the preset file and every file it references. Reloads when
    // something changed. Returns true if a reload happened and succeeded.
    bool poll_reload(std::string* error = nullptr);

    // Parameter overrides survive preset reloads and win over the automatic
    // fixes; an override for a name the preset does not have has no effect.
    void set_parameter(const std::string& name, float value);
    void clear_parameter(const std::string& name);
    void clear_parameters();
    std::vector<Parameter> parameters() const;            // CRT chain
    void set_subframe_parameter(const std::string& name, float value);

    Settings& settings();
    const Settings& settings() const;

    // librashader compiles shaders SPIR-V -> DXIL by default and falls back to
    // SPIR-V -> HLSL -> DXC. `true` forces the HLSL route. Next preset load.
    void set_force_hlsl_pipeline(bool force);

    // Drop temporal state (shader history, beam history) on the next frame:
    // call after a discontinuity (state load, scene cut).
    void reset_history();

    // --- per display refresh: the one call a presenter needs ---------------
    //
    // `new_image` is the game picture if the renderer produced a new one
    // since the previous call, otherwise nullptr (the last one is shown
    // again). It must be a 2D R8G8B8A8_UNORM or B8G8R8A8_UNORM texture in
    // D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE; it is left in that state
    // and must stay alive and unchanged until `cmd` has executed. `cmd` is an
    // open direct command list of the same device; the module changes its
    // descriptor heaps, root signature, pipeline, viewport, scissor, render
    // target and topology and does not restore them.
    void frame(ID3D12GraphicsCommandList* cmd, ID3D12Resource* new_image, const Output& out,
               const RefreshTiming& timing = RefreshTiming{}) {
        if (new_image) process(cmd, new_image, out.width, out.height);
        present(cmd, out, timing);
    }

    // The two halves of frame(), for callers that want them apart.
    // process(): once per NEW game image.
    void process(ID3D12GraphicsCommandList* cmd, ID3D12Resource* input,
                 uint32_t out_width, uint32_t out_height);
    // The same for a picture in the top-left valid_width x valid_height of a
    // larger texture (the exchange textures of the presenter after a
    // resolution change). 0 = the whole texture.
    void process(ID3D12GraphicsCommandList* cmd, ID3D12Resource* input,
                 uint32_t out_width, uint32_t out_height, uint32_t valid_width, uint32_t valid_height);
    // present(): once per DISPLAY REFRESH.
    void present(ID3D12GraphicsCommandList* cmd, const Output& out, const RefreshTiming& timing = RefreshTiming{});
    // present() for an exact multiple: sub-frame `subframe` of `total` per
    // emulated refresh; a call with subframe == 0 starts the next refresh.
    void present(ID3D12GraphicsCommandList* cmd, const Output& out, uint32_t subframe, uint32_t total_subframes);

    // Convenience for single-shot use: process() followed by one present().
    void run(ID3D12GraphicsCommandList* cmd, ID3D12Resource* input, const Output& out) {
        process(cmd, input, out.width, out.height);
        present(cmd, out, RefreshTiming{});
    }

    // Formats an output target must have for a colour space.
    static DXGI_FORMAT output_format_for(OutputColorSpace cs);

    const Stats& stats() const;
    uint64_t frame_count() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace gdl::post
