// Developer tool: GPU lighting cost and output for dumped scenes.
// bench_gpu_light RUNS OUT_DIR [--shader A.hlsl --shader B.hlsl ...] light-*.bin...
// Scenes come from WORLD_LIGHT_DUMP. Each --shader file holds the transport
// shader source; without any, the built-in shader is measured. Variants are
// interleaved in one process so GPU clock drift affects them equally.
// Prints mean ms per pass per variant and writes OUT_DIR/v<N>/<scene>.rgba.
#include "../src/world_gpu.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static std::string slurp(const char *path)
{
    std::string text;
    if (FILE *f = std::fopen(path, "rb")) {
        char buffer[65536];
        size_t n;
        while ((n = std::fread(buffer, 1, sizeof buffer, f)) > 0) text.append(buffer, n);
        std::fclose(f);
    }
    return text;
}

// Older diagnostic captures were raw 320-wide structs, without a width field.
// Read each contiguous region into its current member so the original files
// remain usable with the current built-in transport shader.
static bool read_capture(FILE *f, WorldLightScene &scene, uint32_t *frame)
{
    if (!f || std::fseek(f, 0, SEEK_END)) return false;
    const long length = std::ftell(f);
    std::rewind(f);
    constexpr size_t oldPixels = 320 * 240, oldProbes = oldPixels / 4;
    constexpr size_t oldSceneBytes = 428972 + 43768 + WORLD_EFFECT_NODES * sizeof(WorldEffectNode);
    auto read = [&](void *dst, size_t bytes) { return std::fread(dst, 1, bytes, f) == bytes; };
    if (length == long(oldSceneBytes + oldPixels * 4)) {
        auto &effects = scene.effects;
        scene.width = 320;
        if (!read(&scene, offsetof(WorldLightScene, cells)) || !read(scene.cells, oldProbes * 4) ||
            !read(&scene.revision, 4) || !read(scene.light_bins, 10 * 8 * 5 * 4) ||
            !read(scene.water_grade, sizeof scene.water_grade) || !read(scene.ice_coverage, oldProbes) ||
            !read(scene.sky_stop, 376) || !read(scene.point_coverage, oldProbes) ||
            !read(scene.stage_light, sizeof scene.stage_light) || !read(scene.surface, oldPixels * 4) ||
            !read(scene.strengths, sizeof scene.strengths) || !read(effects.heads, 40 * 30 * 4) ||
            !read(&effects.count, offsetof(WorldEffectBatch, mirror) - offsetof(WorldEffectBatch, count)))
            return false;
        for (auto &row : effects.mirror) if (!read(row, 320 * 4)) return false;
        if (!read(effects.ocean_rows, sizeof effects.ocean_rows) || !read(effects.nodes, sizeof effects.nodes))
            return false;
    } else if (!read(&scene, sizeof scene)) return false;
    return (scene.width == WORLD_VIEW_ARCADE_WIDTH || scene.width == WORLD_VIEW_WIDE_WIDTH) &&
           scene.effects.count < WORLD_EFFECT_NODES && read(frame, scene.width * 240 * 4);
}

int main(int argc, char **argv)
{
    if (argc < 4) { std::puts("usage: bench_gpu_light RUNS OUT_DIR [--shader FILE]... SCENE..."); return 2; }
    unsigned runs = (unsigned)std::atoi(argv[1]);
    static const char *names[8] = {"sun", "direct", "indirect", "resolve", "upload", "volume", "shade", "copy"};
    std::vector<std::string> shader_text, shader_name;
    std::vector<std::string> paths;
    for (int a = 3; a < argc; ++a) {
        if (!std::strcmp(argv[a], "--shader") && a + 1 < argc) {
            shader_text.push_back(slurp(argv[++a]));
            if (shader_text.back().empty()) { std::printf("cannot read %s\n", argv[a]); return 1; }
            shader_name.push_back(argv[a]);
        } else paths.push_back(argv[a]);
    }
    unsigned count = (unsigned)paths.size(), variants = shader_text.empty() ? 1 : (unsigned)shader_text.size();
    std::vector<const char *> sources(variants, nullptr);
    for (unsigned v = 0; v < shader_text.size(); ++v) sources[v] = shader_text[v].c_str();
    if (shader_name.empty()) shader_name.push_back("built-in");
    std::vector<const WorldLightScene *> scenes(count);
    std::vector<const uint32_t *> frames(count);
    std::vector<uint32_t *> shaded(count * variants);
    std::vector<std::string> labels(count);
    for (unsigned s = 0; s < count; ++s) {
        WorldLightScene *scene = new WorldLightScene();
        uint32_t *frame = new uint32_t[WORLD_VIEW_MAX_WIDTH * 240];
        const std::string &path = paths[s];
        size_t slash = path.find_last_of("/\\");
        size_t prior = slash == std::string::npos || !slash ? std::string::npos : path.find_last_of("/\\", slash - 1);
        labels[s] = path.substr(prior == std::string::npos ? 0 : prior + 1);
        labels[s].resize(labels[s].size() - 4);
        for (char &c : labels[s]) if (c == '/' || c == '\\') c = '_';
        FILE *f = std::fopen(path.c_str(), "rb");
        if (!read_capture(f, *scene, frame)) {
            std::printf("cannot read %s\n", path.c_str());
            return 1;
        }
        std::fclose(f);
        scenes[s] = scene;
        frames[s] = frame;
    }
    for (auto &p : shaded) p = new uint32_t[WORLD_VIEW_MAX_WIDTH * 240];
    std::vector<double> ms(count * variants * 8);
    char error[512] = {};
    if (!world_gpu_light_benchmark(scenes.data(), frames.data(), count, sources.data(), variants, runs,
                                   reinterpret_cast<double (*)[8]>(ms.data()), shaded.data(), error, sizeof error)) {
        std::puts(error);
        return 1;
    }
    for (unsigned v = 0; v < variants; ++v) {
        std::string dir = std::string(argv[2]) + "/v" + std::to_string(v);
        std::string make = "mkdir \"" + dir + "\" 2>nul";
        std::system(make.c_str());
        double total[8] = {};
        for (unsigned s = 0; s < count; ++s) {
            const double *m = &ms[(v * count + s) * 8];
            if (FILE *o = std::fopen((dir + "/" + labels[s] + ".rgba").c_str(), "wb")) {
                std::fwrite(shaded[v * count + s], 4, scenes[s]->width * 240, o);
                std::fclose(o);
            }
            if (std::getenv("BENCH_SCENES")) {
                double sum = 0;
                std::printf("v%u %-24s", v, labels[s].c_str());
                for (int n = 0; n < 8; ++n) { std::printf(" %s %.4f", names[n], m[n]); sum += m[n]; }
                std::printf("  total %.4f\n", sum);
            }
            for (int n = 0; n < 8; ++n) total[n] += m[n];
        }
        double sum = 0;
        std::printf("v%u MEAN", v);
        for (int n = 0; n < 8; ++n) { std::printf(" %s %.4f", names[n], total[n] / count); sum += total[n] / count; }
        std::printf("  total %.4f  (%s)\n", sum, shader_name[v].c_str());
    }
    return 0;
}
