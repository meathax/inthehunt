#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#include <emmintrin.h>  /* SSE2 is baseline on the Windows x64 target. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "world_scene.h"

enum {
    W = 320, H = 240, N = W * H, HUD = 224,
    MARGIN = 32,                       /* layer buffers reach this far past the picture: 17 px of
                                          scroll between frames plus 11 px of displacement */
    LW = W + 2 * MARGIN, LH = H + 2 * MARGIN, LN = LW * LH,
    MAXK = WORLD_SCENE_MAX_K,
    GW = W / 8, GH = H / 8,            /* light grid */
    NONE = 0x7fff,                     /* empty sprite pixel */
    MAX_SPRITES = 256, MAX_RINGS = 6, MAX_PARTICLES = 224
};

typedef struct {
    uint8_t tiles[65536];
    uint8_t sprites[2048];
    uint16_t sprite_owners[256];
    uint32_t colors[1024];
    WorldVideoLayer layers[3];
    uint16_t video_control;
    int waterline;
    int frac[3];
    int lamp[2][3]; /* native bow x, y, facing; zero facing disables */
    unsigned scene_id;
    int valid;
} Snapshot;

typedef struct { int left, top, cols, rows; unsigned code, attr, slot; } Sprite;
typedef struct { float x, y, age, life, strength; } Ring;
typedef struct { float x, y, vx, vy, age, life; int kind; } Particle;

static const uint8_t *gfx1, *gfx2;
static Snapshot cur, prev;
static WeaponEmitter weapon_emitters[MAX_WEAPON_EMITTERS];
static unsigned weapon_count;
void world_scene_weapons(const WeaponEmitter *emitters, unsigned count)
{
    weapon_count=count > MAX_WEAPON_EMITTERS ? MAX_WEAPON_EMITTERS : count;
    if (weapon_count) memcpy(weapon_emitters,emitters,weapon_count*sizeof *emitters);
}

static int new_frame;
static int scene_cache_valid;
static float *camera_output;
static int camera_fractional;
void world_scene_camera_output(float offsets[4]) { camera_output=offsets; }
static int have_history;
void (*world_scene_light_ready)(const WorldLightScene *);
static WorldLightScene *gpu_light;
void world_scene_light_target(WorldLightScene *target)
{
    if (!!target != !!gpu_light) {scene_cache_valid=0;have_history=0;}
    gpu_light=target;
}
static int cached_scale;
static uint32_t layer_clock;
static double sprite_alpha;
static WorldSceneFx cached_fx;

/* Per game frame: palette indices of each layer around the current scroll. */
static uint16_t layer_idx[3][LN];
static uint8_t layer_pri[3][LN];
static int layer_on[3], layer_rows[3];           /* enabled; per-row scroll (no offsets) */
/* Per refresh: colours after the per-layer effects, and the water mask. */
static uint32_t layer_col[3][LN];
static uint8_t receiver_lum[LN], receiver_opacity[LN], layer_wat[3][LN];

static uint16_t sprite_px[MAXK * MAXK * N];      /* palette index | 0x8000 behind tiles */
static uint32_t history[MAXK * MAXK * N];

/* Only opaque HUD pixels are exempt; water continues behind their gaps. */
static uint32_t hud_overlay[W * (H - HUD) * MAXK * MAXK];
typedef struct { unsigned weight; uint32_t x,y,r,g,b; } BlastRadiance;
static BlastRadiance blast_radiance[MAX_SPRITES];
/* Screen-space centres in native pixels, carried with the playfield scroll. */
typedef struct { float x,y,r,g,b,range; } BlastLight;
static BlastLight blast_lights[MAX_SPRITES];
static unsigned blast_light_count;
static uint16_t hot_count[GW * GH], hot_before[GW * GH];
static uint8_t water_map[N];                     /* 1x: open water under this pixel */
static int8_t field_x[N], field_y[N];            /* shockwave displacement, 1/4 px */
static int field_on;
static int16_t sine[8192];
static float light_absorb[3][320];
static uint16_t absorb[3][320];                  /* 256 = nothing absorbed, by depth */
static uint16_t divide[LW * MAXK + 8];           /* subpixel -> buffer pixel for the current K */
static int divide_k;
static int ready;

static Ring rings[MAX_RINGS];
static Particle particles[MAX_PARTICLES];
static float shake;
static unsigned rumble;
static uint32_t last_clock, seed = 0x1234567u;
static int waterline_used;
static void cinema_reset(void);
static void cinema_scroll(int dx,int dy);
static void cinema_blast(float x,float y,int size,const WorldSceneFx *fx);
static void waterfx_reset(void);
static void waterfx_scroll(int dx, int dy);
static int transport_valid;                      /* the light transport below has run this scene */

/* One render's parameters, shared with the worker threads. */
static struct {
    int k, ow, oh;
    uint32_t *out;
    const WorldSceneFx *fx;
    int off_x[3], off_y[3];            /* subpixels, including the margin */
    int shake_x, shake_y;
    int row_dx[H];                     /* subpixels: surface ripple */
    int waterline, T, Q;
    int scroll_x[3], scroll_y[3];
} g;

/* ---- small helpers ------------------------------------------------------- */

static inline unsigned word(const uint8_t *p) { return p[0] | (unsigned)p[1] << 8; }
static inline int S(int a) { return sine[(a * 8) & 8191]; }      /* 1024 steps per turn */
static inline int F(int a) { return sine[a & 8191]; }            /* 8192 steps per turn */
static inline int clamp255(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }
static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
/* Limit an overbright RGB triplet with ONE scale, preserving its hue. */
static uint32_t light_rgb(int r,int gr,int b)
{
    if(r<0)r=0;if(gr<0)gr=0;if(b<0)b=0;
    int peak=r>gr?r:gr;if(b>peak)peak=b;
    if(peak>255){float scale=255.0f/peak;r=(int)(r*scale);gr=(int)(gr*scale);b=(int)(b*scale);}
    return 0xff000000u|(unsigned)r<<16|(unsigned)gr<<8|(unsigned)b;
}
static void light_shoulder(float *r,float *gr,float *b)
{
    float peak=fmaxf(*r,fmaxf(*gr,*b)),scale=.90f/(1+1.05f*peak);
    *r*=scale;*gr*=scale;*b*=scale;
}
/* Length below the surface, including rays crossing the air/water boundary.
 * Native pixel units; coefficients are an artistic water calibration, not metres. */
static int submerged_path(float y0,float y1,float distance)
{
    float fraction=1;
    if(g.waterline>=0){float a=y0-g.waterline,b=y1-g.waterline;
        if(a<0 && b<0)return 0;
        if(a<0)fraction=b/(b-a);else if(b<0)fraction=a/(a-b);
    }
    return clampi((int)(distance*fraction+.5f),0,319);
}
static float frand(void)
{
    seed = seed * 1664525u + 1013904223u;
    return (float)(seed >> 8) / 16777216.0f;
}

static uint64_t plane_pixels[256];

static inline uint64_t decode_plane_row(const uint8_t *row,unsigned stride)
{
    return plane_pixels[row[0]] | plane_pixels[row[stride]]<<1 |
           plane_pixels[row[2*stride]]<<2 | plane_pixels[row[3*stride]]<<3;
}

static void prepare(void)
{
    for (int i = 0; i < 8192; ++i) sine[i] = (int16_t)lrint(256.0 * sin(i * 6.283185307179586 / 8192.0));
    /* Beer-Lambert per channel: red goes first, then green; blue lasts. */
    static const double rate[3] = {0.0036, 0.0018, 0.0010};
    for (int c = 0; c < 3; ++c)
        for (int d = 0; d < 320; ++d) absorb[c][d] = (uint16_t)lrint(256.0 * exp(-rate[c] * d));
    static const float extinction[3]={.012f,.005f,.0025f};
    for(int c=0;c<3;++c)for(int d=0;d<320;++d)light_absorb[c][d]=expf(-extinction[c]*d);
    for (int byte=0; byte<256; ++byte)
        for (int x=0; x<8; ++x)
            plane_pixels[byte] |= (uint64_t)((byte>>(7-x))&1) << (8*x);
    ready = 1;
}

/* Jobs are published as independently claimable row ranges. A late worker
 * does not hold up a frame when another thread can finish its range. */
enum { WORKERS = 7, JOB_PARTS = 24 };
static HANDLE worker_go[WORKERS], job_complete;
static void (*job)(int, int);
static int job_rows;
static volatile LONG job_pending;
static uint32_t job_serial;
static volatile LONG64 claims __attribute__((aligned(8)));
static int workers_ready;

static void run_available_rows(void)
{
    for (;;) {
        LONG64 state=InterlockedCompareExchange64(&claims,0,0);
        unsigned part=(unsigned)state;
        if (part>=JOB_PARTS) return;
        /* The high word distinguishes frames even if this thread was
           preempted between reading the ticket and claiming a range. */
        if (InterlockedCompareExchange64(&claims,state+1,state)!=state) continue;
        int rows=job_rows;
        job((int)part*rows/JOB_PARTS,((int)part+1)*rows/JOB_PARTS);
        if (!InterlockedDecrement(&job_pending)) SetEvent(job_complete);
    }
}

static DWORD WINAPI worker(void *index)
{
    int i=(int)(intptr_t)index;
    for (;;) {
        WaitForSingleObject(worker_go[i],INFINITE);
        run_available_rows();
    }
}

/* Give the eight short, synchronized row jobs distinct fast physical cores
 * on hybrid CPUs. Ideal processors are hints; Windows retains scheduling
 * control. Respect the process affinity and use ordinary scheduling if
 * there are too few available high-performance cores. */
static void renderer_ideal_processors(DWORD ids[WORKERS+1])
{
    DWORD size=0;
    for(int i=0;i<=WORKERS;++i) ids[i]=MAXDWORD;
    if (GetActiveProcessorGroupCount()!=1) return;
    GetLogicalProcessorInformationEx(RelationProcessorCore,NULL,&size);
    if (!size) return;
    BYTE *data=malloc(size);
    DWORD_PTR process_mask,system_mask;
    if (data && GetProcessAffinityMask(GetCurrentProcess(),&process_mask,&system_mask) &&
        GetLogicalProcessorInformationEx(RelationProcessorCore,(void *)data,&size)) {
        BYTE best=0;
        for(DWORD pos=0;pos<size;){
            SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *info=(void *)(data+pos);
            if(info->Processor.EfficiencyClass>best) best=info->Processor.EfficiencyClass;
            pos+=info->Size;
        }
        int count=0;
        for(DWORD pos=0;best && pos<size && count<=WORKERS;){
            SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *info=(void *)(data+pos);
            DWORD_PTR mask=info->Processor.GroupMask[0].Mask & process_mask;
            if(info->Processor.EfficiencyClass==best && mask) ids[count++]=(DWORD)__builtin_ctzll(mask);
            pos+=info->Size;
        }
        if(count<=WORKERS) for(int i=0;i<=WORKERS;++i)ids[i]=MAXDWORD;
    }
    free(data);
}

static void parallel(void (*fn)(int, int), int rows)
{
    if (!workers_ready) {
        DWORD ideal[WORKERS+1];
        renderer_ideal_processors(ideal);
        if (ideal[0]!=MAXDWORD) SetThreadIdealProcessor(GetCurrentThread(),ideal[0]);
        job_complete=CreateEventW(NULL,FALSE,FALSE,NULL);
        workers_ready=job_complete ? 1 : -1;
        for (int i = 0; i < WORKERS; ++i) {
            worker_go[i] = CreateEventW(NULL, FALSE, FALSE, NULL);
            HANDLE thread = worker_go[i] && job_complete
                ? CreateThread(NULL, 0, worker, (void *)(intptr_t)i, 0, NULL) : NULL;
            if (!thread) workers_ready = -1;
            else {
                if(ideal[i+1]!=MAXDWORD) SetThreadIdealProcessor(thread,ideal[i+1]);
                SetThreadPriority(thread, THREAD_PRIORITY_HIGHEST);
                CloseHandle(thread);
            }
        }
    }
    if (workers_ready < 0 || rows < 64) { fn(0, rows); return; }
    job=fn;
    job_rows=rows;
    InterlockedExchange(&job_pending,JOB_PARTS);
    InterlockedExchange64(&claims,(LONG64)((uint64_t)(unsigned)++job_serial<<32));
    for (int i=0;i<WORKERS;++i) SetEvent(worker_go[i]);
    run_available_rows();
    while (InterlockedCompareExchange(&job_pending,0,0))
        WaitForSingleObject(job_complete,INFINITE);
}

/* ---- per game frame ------------------------------------------------------ */

/* The tile layers as world_video_playfields samples them, one layer at a
 * time, over the picture plus a margin. */
/* Optical depth and vivid-colour gain for the six gameplay palettes.
   They scale ambient light; explosions, lamps and the HUD retain their range. */
typedef struct { float r,g,b,saturation; } WaterGrade;
static const WaterGrade water_grades[]={
    {.47f,.62f,.80f,1.035f}, /* blue ice and snow */
    {.45f,.67f,.65f,1.025f}, /* green harbour */
    {.56f,.66f,.74f,1.018f}, /* sunlit industrial waterfront */
    {.47f,.59f,.73f,1.045f}, /* dark caverns, preserve basalt detail */
    {.58f,.58f,.72f,1.035f}, /* warm masonry, cool ice */
    {.40f,.59f,.61f,1.045f}  /* flooded steelworks */
};
/* Sun RGB/ambient, scattering RGB/density, black toe/shadow/specular/shaft.
   Fixed exposure avoids weapon-driven adaptation. Values are artistic. */
static const float stage_lighting[6][12]={
    {1.03f,1.06f,1.12f,.96f, .65f,.88f,1.08f,.86f, .010f,.78f,.72f,.85f},
    {1.08f,1.03f,.88f,.92f, .59f,.90f,.78f,1.15f, .016f,.68f,.60f,.62f},
    {1.13f,1.04f,.88f,.98f, .78f,.90f,1.02f,.72f, .008f,.72f,.68f,.72f},
    {.75f,.88f,1.05f,.80f, .40f,.62f,.86f,1.26f, .026f,.52f,.52f,.32f},
    {1.08f,.97f,.85f,.89f, .62f,.72f,.95f,.95f, .020f,.64f,.62f,.54f},
    {.81f,1.02f,.96f,.84f, .43f,.78f,.74f,1.18f, .023f,.58f,.68f,.42f}
};
static int lighting_stage(void)
{
    unsigned stage=cur.scene_id>>16;
    return (cur.scene_id&65535)==0xb2e && stage<6?(int)stage:-1;
}

static uint8_t terrain_cast[LN], far_ice[LN], floor_material[LN];
static int floor_top[LW];
/* Zero means open sky. Other values encode the first roof hit plus one
   along the same slant as sunPass, including shafts entering at the right. */
static uint8_t sky_stop[376];
static int sky_reaches(int x,int y)
{
    int stop=sky_stop[x+(11*y+25)/50];
    return !stop || y+1<stop;
}

/* Only a solid run connected to the bottom of the playfield is seabed.
   The contour follows opaque pixels inside the arcade collision tiles. */
static void find_seabed(void)
{
    int enclosed=cur.waterline<=0;
    memset(sky_stop,enclosed?1:0,sizeof sky_stop);
    for(int x=0;x<LW;++x){
        if(!enclosed && layer_on[1]){
            for(int y=0;y<H && terrain_cast[(y+MARGIN)*LW+x];++y){
                int shaft=x-MARGIN+(11*y+25)/50;
                if((unsigned)shaft<373 && (!sky_stop[shaft] || sky_stop[shaft]>y+1))
                    sky_stop[shaft]=(uint8_t)(y+1);
            }
        }
        int row=MARGIN+H-1, solid_material=0;
        while(layer_on[1] && row>=0 && terrain_cast[row*LW+x]){
            solid_material|=floor_material[row*LW+x];
            --row;
        }
        floor_top[x]=solid_material?row+1:LH;
    }
}
static void render_layer(int layer, int from, int to)
{
    unsigned master = cur.layers[layer].master;
    if (!layer_on[layer]) return;
    int stage=lighting_stage();
    unsigned wide = master & 4, columns = wide ? 128 : 64;
    for (int by = from; by < to; ++by) {
        unsigned sy = (unsigned)(by - MARGIN + 136 + cur.layers[layer].scroll_y) & 511;
        unsigned scroll = cur.layers[layer].scroll_x;
        if (master & 0x40) scroll = word(cur.tiles + 0xf400 + layer * 0x400 + sy * 2);
        uint16_t *idx = layer_idx[layer] + by * LW;
        uint8_t *pri = layer_pri[layer] + by * LW;
        for (int bx = 0; bx < LW;) {
            unsigned sx = (unsigned)(bx - MARGIN + 80) + scroll - 2 * (unsigned)layer + (wide ? 256 : 0);
            unsigned entry = (master & 3) * 0x4000 + ((sy / 8) * columns + ((sx / 8) & (columns - 1))) * 4;
            const uint8_t *tile = cur.tiles + (entry & 0xffff);
            unsigned attr = word(tile + 2), bank = (attr & 0x7f) | ((cur.video_control >> 7) & 0x180);
            const uint8_t *row = gfx1 + word(tile)*8 + ((attr&0x400 ? ~sy : sy)&7);
            uint64_t pens = decode_plane_row(row,0x80000);
            if (attr & 0x200) pens = __builtin_bswap64(pens);
            pens >>= 8*(sx&7);
            int end = bx + 8 - (int)(sx&7);
            if (end>LW) end=LW;
            unsigned code=word(tile);
            int terrain=layer==1;
            int depth=by-MARGIN-cur.waterline;
            /* The wave strip shares the playfield with structures in stages
               one and two; the other stages have a separate water layer. */
            if((stage==0 && (bank&63)==24) || (stage==1 && (bank&63)==16))
                terrain=0;
            /* The cavern has composite rock/foam tiles along the interface.
               Only the repeating foam artwork is optically transparent. */
            if(stage==3 && (bank&63)==15 && ((code>=0x6488 && code<=0x648d) ||
                code==0x6323 || code==0x6333))terrain=0;
            uint64_t sea=0;
            int ice=layer==2 && stage==0 && (bank&63)==14 && code>=0x2256 &&
                code<=0x2330 && depth>=0 && depth<48 && cur.waterline!=-1;
            if(ice){
                /* Far-plane ice embeds the sea gradient in opaque tiles.
                   Subtract the matching row of open water, pen for pen. */
                sea=decode_plane_row(gfx1+(0x1fb3+depth/8)*8+(sy&7),0x80000)>>(8*(sx&7));
            }
            for (; bx<end; ++bx, pens>>=8) {
                unsigned pen = pens&15;
                idx[bx] = pen || layer==2 ? (uint16_t)((bank*16+pen)&1023) : 0xffff;
                if(layer==1){
                    terrain_cast[by*LW+bx]=terrain && pen;
                    floor_material[by*LW+bx]=(0xff02u >> ((attr>>11)&15))&1;
                }
                if(layer==2 && stage==0){
                    far_ice[by*LW+bx]=ice && pen!=(sea&15);
                    sea>>=8;
                }
                pri[bx] = pen && ((attr&0x100) || ((attr&0x80) && pen>=8));
            }
        }
    }
}

static void render_tile_rows(int from,int to)
{
    for (int layer=0; layer<3; ++layer) render_layer(layer,from,to);
}

/* Sprite-list codes for spray, foam and impact animation parts. Decode the
   same animation lists and banked descriptors that the board submits to OAM. */
static uint8_t impact_art[65536 / 8], flash_art[65536 / 8];
static void mark_impact_frames(const uint8_t *rom, unsigned animation, uint8_t *art)
{
    for (unsigned frame=0; frame<128; ++frame) {
        unsigned descriptor=word(rom+animation+2*frame);
        if (descriptor==0xffff) break;
        const uint8_t *bank=rom+((descriptor&1)?0x90000:0x80000);
        unsigned count=word(bank+descriptor);
        if (count>210) break;
        for (unsigned part=0; part<count; ++part) {
            unsigned code=word(bank+(uint16_t)(descriptor+6+8*part));
            art[code>>3]|=(uint8_t)(1u<<(code&7));
        }
    }
}

void world_scene_init_impact_art(const uint8_t *program_rom)
{
    unsigned animations[]={word(program_rom+0x6cec),word(program_rom+0x6d2a),
        word(program_rom+0x6d67),word(program_rom+0x6ad9),0x7507,0x7511,0x7555,0x75a7};
    memset(impact_art,0,sizeof impact_art);
    for (unsigned i=0;i<sizeof animations/sizeof animations[0];++i)
        mark_impact_frames(program_rom,animations[i],impact_art);
    /* World impact, kill, warship and boss blast sequences. Water-entry
       animations have their own artwork and do not emit explosion light. */
    const unsigned blasts[]={word(program_rom+0x6ad9),0x747f,0x74a1,0x74c3,
        0x74e5,0x7533,0x75df};
    memset(flash_art,0,sizeof flash_art);
    for(unsigned i=0;i<sizeof blasts/sizeof blasts[0];++i)
        mark_impact_frames(program_rom,blasts[i],flash_art);
}

void world_scene_init(const uint8_t *tiles_rom, const uint8_t *sprites_rom)
{
    gfx1 = tiles_rom;
    gfx2 = sprites_rom;
}

void world_scene_reset(void)
{
    weapon_count=0;
    cur.valid = prev.valid = 0;
    transport_valid = 0;
    have_history = 0;
    memset(rings, 0, sizeof rings);
    memset(particles, 0, sizeof particles);
    shake = 0;
    blast_light_count=0;waterfx_reset();
    scene_cache_valid=0;last_clock=0;rumble=0;
    memset(hot_before,0,sizeof hot_before);
}

void world_scene_submit(const WorldSceneFrame *frame)
{
    if (!ready) prepare();
    if (!gfx1 || !gfx2) return;
    if (!cur.valid || cur.scene_id != frame->scene_id) {
        blast_light_count=0;waterfx_reset();memset(rings,0,sizeof rings);memset(particles,0,sizeof particles);
        memset(hot_before,0,sizeof hot_before);have_history=0;shake=0;rumble=0;
        scene_cache_valid=0;transport_valid=0;last_clock=0;
    }
    if (cur.valid) memcpy(&prev, &cur, sizeof prev);
    memcpy(cur.tiles, frame->tiles, sizeof cur.tiles);
    memcpy(cur.sprites, frame->sprites, sizeof cur.sprites);
    if(frame->sprite_owners)memcpy(cur.sprite_owners,frame->sprite_owners,sizeof cur.sprite_owners);
    else memset(cur.sprite_owners,0,sizeof cur.sprite_owners);
    memcpy(cur.colors, frame->colors, sizeof cur.colors);
    memcpy(cur.layers, frame->layers, sizeof cur.layers);
    memcpy(cur.frac, frame->frac, sizeof cur.frac);
    cur.video_control = frame->video_control;
    cur.waterline = frame->waterline;
    memcpy(cur.lamp, frame->lamp, sizeof cur.lamp);
    cur.scene_id = frame->scene_id;
    cur.valid = 1;
    if (!prev.valid || prev.scene_id != cur.scene_id) memcpy(&prev, &cur, sizeof prev);
    for (int layer=0; layer<3; ++layer) {
        layer_on[layer]=!(cur.layers[layer].master & 0x10);
        layer_rows[layer]=(cur.layers[layer].master & 0x40)!=0;
    }
    parallel(render_tile_rows,LH);
    find_seabed();
    /* Rings and particles live in the playfield: follow its scroll. */
    int dx = (int16_t)(cur.layers[1].scroll_x - prev.layers[1].scroll_x);
    int dy = (int16_t)(cur.layers[1].scroll_y - prev.layers[1].scroll_y);
    if (abs(dx) > 32 || abs(dy) > 32) {
        blast_light_count=0;waterfx_reset();memset(rings,0,sizeof rings);memset(particles,0,sizeof particles);
        memset(hot_before,0,sizeof hot_before);have_history=0;scene_cache_valid=0;dx=dy=0;
    }
    for(unsigned i=0;i<blast_light_count;++i){
        blast_lights[i].x-=dx;
        blast_lights[i].y-=dy;
    }
    waterfx_scroll(dx, dy);
    for (int i = 0; i < MAX_RINGS; ++i) { rings[i].x -= (float)dx; rings[i].y -= (float)dy; }
    for (int i = 0; i < MAX_PARTICLES; ++i) { particles[i].x -= (float)dx; particles[i].y -= (float)dy; }
    new_frame = 1;
}

/* ---- sprites -------------------------------------------------------------- */

static int parse_sprites(const uint8_t *buffer, Sprite *list)
{
    int count = 0;
    for (unsigned offset = 0; offset < 2048 && count < MAX_SPRITES; ) {
        const uint8_t *entry = buffer + offset;
        unsigned geometry = word(entry);
        Sprite *s = &list[count++];
        s->cols = 1 << ((geometry >> 11) & 3);
        s->rows = 1 << ((geometry >> 9) & 3);
        s->slot=offset/8;
        offset += 8u * (unsigned)s->cols;
        s->top = 360 - (int)(geometry & 511) - 16 * (s->rows - 1);
        s->left = (int)((word(entry + 6) - 16u) & 511) - 80;
        s->code = word(entry + 2);
        s->attr = word(entry + 4);
    }
    return count;
}

#include "world_scene_materials.h"

/* The texel whose material sprite_block stores. Looked up only when the
   texel actually owns a pixel: nearer sprites already cover many of them. */
static struct { int wanted, x, y, flip_x, flip_y; unsigned code, bank, pen; } drawing;
static inline uint32_t drawing_material(void)
{
    return drawing.wanted ? material_surface(drawing.code, drawing.bank, drawing.pen, drawing.x, drawing.y,
                                             drawing.flip_x, drawing.flip_y) : 0;
}
static inline void sprite_block(int px, int py, uint16_t value)
{
    int k = g.k;
    if (px <= -k || py <= -k || px >= g.ow || py >= g.oh) return;
    int x0 = px < 0 ? 0 : px, x1 = px + k > g.ow ? g.ow : px + k;
    int y0 = py < 0 ? 0 : py, y1 = py + k > g.oh ? g.oh : py + k;
    int store = g.fx->materials || g.fx->contact_shade, looked_up = 0;
    uint32_t material = 0;
    for (int y = y0; y < y1; ++y) {
        uint16_t *row = sprite_px + (size_t)y * g.ow;
        for (int x = x0; x < x1; ++x)
            if (row[x] == NONE) {                     /* the first sprite owns the pixel */
                row[x] = value;
                if (store) {
                    if (!looked_up) { material = drawing_material(); looked_up = 1; }
                    sprite_material[(size_t)y*g.ow+x] = material;
                }
            }
    }
}

/* Sprite RAM has drawing order but no actor IDs. Connected pieces in the
 * same palette must use one translation, including on animation changes.
 * Only an unambiguous previous graphic can establish that translation. */
typedef struct { int root, dx, dy, votes; } SpriteMotion;

static int motion_root(SpriteMotion *motion, int i)
{
    while (motion[i].root != i) {
        motion[i].root = motion[motion[i].root].root;
        i = motion[i].root;
    }
    return i;
}

static void sprite_motions(const Sprite *now, int count, const Sprite *before, int old,
                           SpriteMotion *motion)
{
    for (int i=0; i<count; ++i) {
        const Sprite *s=&now[i];
        motion[i]=(SpriteMotion){.root=i};
        if (s->top>=H+8 || s->top+s->rows*16<=-8) continue;
        for (int j=0; j<i; ++j) {
            const Sprite *p=&now[j];
            if ((p->attr & 255)!=(s->attr & 255)) continue;
            if (p->top>=H+8 || p->top+p->rows*16<=-8) continue;
            if (s->left>p->left+p->cols*16 || p->left>s->left+s->cols*16 ||
                s->top>p->top+p->rows*16 || p->top>s->top+s->rows*16) continue;
            motion[motion_root(motion,j)].root=i;
        }
    }
    for (int i=0; i<count; ++i) {
        const Sprite *s=&now[i];
        if (s->top>=H+8 || s->top+s->rows*16<=-8) continue;
        int match=-1;
        for (int j=0; j<old; ++j) {
            const Sprite *p=&before[j];
            if (s->code!=p->code || s->attr!=p->attr || s->rows!=p->rows || s->cols!=p->cols ||
                abs(s->left-p->left)>20 || abs(s->top-p->top)>20) continue;
            if (match>=0) { match=-1; break; }
            match=j;
        }
        if (match<0) continue;
        SpriteMotion *m=&motion[motion_root(motion,i)];
        int dx=s->left-before[match].left, dy=s->top-before[match].top;
        if (!m->votes) { m->dx=dx; m->dy=dy; m->votes=1; }
        else if (m->dx!=dx || m->dy!=dy) m->votes=-1;
    }
}

static void draw_sprites(double alpha)
{
    if((g.fx->materials || g.fx->contact_shade))material_palette_prepare();
    static Sprite now[MAX_SPRITES], before[MAX_SPRITES];
    int k = g.k;
    for (size_t i = 0, n = (size_t)g.ow * g.oh; i < n; ++i) sprite_px[i] = NONE;
    memset(blast_radiance,0,sizeof blast_radiance);
    memset(hot_count, 0, sizeof hot_count);
    if (cur.video_control & 0x80) return;
    int count = parse_sprites(cur.sprites, now);
    int old = parse_sprites(prev.sprites, before);
    SpriteMotion motion[MAX_SPRITES];
    int changed=memcmp(cur.sprites,prev.sprites,sizeof cur.sprites)!=0;
    int moving=g.fx->interp && alpha<1.0 && changed;
    int track=moving || (changed && g.fx->smooth && cur.frac[1]!=0);
    if (track) sprite_motions(now,count,before,old,motion);
    int camera = (int16_t)(cur.layers[1].scroll_x - prev.layers[1].scroll_x);
    int independent = (cur.video_control & 0x2000) != 0;
    double fraction_now = g.fx->smooth ? cur.frac[1] / 256.0 : 0.0;
    double fraction_before = g.fx->smooth ? prev.frac[1] / 256.0 : 0.0;

    /* Light is gathered per sprite and kept only for the game's explosion
       graphics (fire-coloured paint alone also matches submarines, coins and
       the CONTINUE lettering). Palette banks 1-2 (the players' submarines)
       and 11 (the HUD's GO arrow) never glow. */
    for (int i = 0; i < count; ++i) {
        const Sprite *s = &now[i];
        int height = s->rows * 16;
        unsigned submission=cur.sprite_owners[s->slot], owner=submission&255;
        BlastRadiance *flash=&blast_radiance[owner?owner-1:s->slot];
        unsigned own_bank = s->attr & 0x7f;
        int may_glow = own_bank != 1 && own_bank != 2 && own_bank != 11;
        /* The game's explosion graphics, in whatever palette. */
        unsigned family = s->code >> 4;
        int blast = (family >= 0x0a9 && family <= 0x0ab) || family == 0x0cb || family == 0x0cc ||
                    family == 0x0fc || family == 0x0fd || (family >= 0x103 && family <= 0x109);
        /* The vertical weapon frames (ROM tables 99AD/99B5) and its
           palette-3 wake (9AB3) share tile families with explosions. */
        int rising_round=(s->code>=0xaac && s->code<=0xaaf) || s->code==0xacf || s->code==0x1038;
        int bubble_trail=own_bank==3 && ((s->code>=0xa94 && s->code<=0xaaa) ||
                          s->code==0x1fc8 || s->code==0x106a);
        int authored_flash=(submission>>8)==2 || ((submission>>8)==1 &&
            (flash_art[s->code>>3] & (1u<<(s->code&7))));
        blast=authored_flash || (blast && !rising_round && !bubble_trail);
        if(authored_flash)may_glow=1;
        if (s->top >= H + 8 || s->top + height <= -8) continue;
        double x=s->left, y=s->top;
        SpriteMotion *m=track ? &motion[motion_root(motion,i)] : NULL;
        int hud=own_bank==11 && s->top>=HUD;
        if (!hud) {
            if (m && m->votes==1) {
                if (moving) { x-=m->dx*(1-alpha); y-=m->dy*(1-alpha); }
                if (m->dx==-camera)
                    x-=moving ? fraction_before+(fraction_now-fraction_before)*alpha : fraction_now;
            } else if (!changed && camera==0) x-=fraction_now;
        }
        int left = (int)floor(x * k + 0.5), top = (int)floor(y * k + 0.5);

        int flip_x = (s->attr & 0x100) != 0, flip_y = (s->attr & 0x200) != 0;
        unsigned bank = (s->attr & (independent ? 0x7f : 0xff)) | ((cur.video_control & 0x8000) ? 0x100 : 0);
        uint16_t flags = (!independent || !(s->attr & 0x80)) ? 0x8000 : 0;
        /* Mark receiver pixels before sprite parts are assembled. The GPU
           finds the outer edge of their union, including transparent pens. */
        int transmissive=blast || bubble_trail || (impact_art[s->code>>3] & (1u<<(s->code&7)));
        if(transmissive)flags|=0x1000; /* Luminous gases transmit the lights inside them. */
        if (g.fx->rim && !transmissive && own_bank!=3 && own_bank!=10 && own_bank!=11 &&
            own_bank!=39 && own_bank!=43 && own_bank!=55) flags |= 0x2000;
        for (int row = 0; row < height; ++row) {
            int screen_y = s->top + row;
            if (screen_y < -8 || screen_y >= H + 8) continue;
            int local = flip_y ? height - 1 - row : row;
            for (int c = 0; c < s->cols; ++c) {
                int shown = flip_x ? s->cols - 1 - c : c;
                unsigned code = s->code + (unsigned)local / 16 + 8u * (unsigned)c;
                const uint8_t *src=gfx2+(code&0x7fff)*32+(local&15);
                uint64_t halves[2]={decode_plane_row(src,0x100000),decode_plane_row(src+16,0x100000)};
                for (int col = 0; col < 16; ++col) {
                    unsigned tx=flip_x ? 15-col : col;
                    unsigned pen=(unsigned)(halves[tx/8]>>(8*(tx&7)))&15;
                    if (!pen) continue;
                    uint16_t index = (uint16_t)((bank * 16 + pen) & 1023);
                    /* A column starting at 497 or beyond wraps to the left edge. */
                    int wrap = s->left + shown * 16 >= 497 ? 512 : 0;
                    int sx = s->left + shown * 16 + col - wrap;
                    int px = left + (shown * 16 + col - wrap) * k, py = top + row * k;
                    drawing.wanted = (g.fx->materials || g.fx->contact_shade) && height>=16 && own_bank!=3 && own_bank!=10 && own_bank!=11 && own_bank!=39 && own_bank!=43 && own_bank!=55 && !transmissive;
                    drawing.code = code; drawing.bank = bank; drawing.pen = pen;
                    drawing.x = flip_x ? 15-col : col; drawing.y = local&15;
                    drawing.flip_x = flip_x; drawing.flip_y = flip_y;
                    sprite_block(px, py, index | flags | (hud ? 0x4000 : 0));
                    /* Fire, flashes and tracer light their surroundings. */
                    if (may_glow && blast && sx >= 0 && sx < W && screen_y >= 0 && screen_y < H) {
                        uint32_t colour = cur.colors[index];
                        unsigned r = (colour >> 16) & 255, gr = (colour >> 8) & 255, b = colour & 255;
                        unsigned peak=r>gr?r:gr;
                        if(b>peak)peak=b;
                        /* Track the animated luminous core per descriptor, even
                           for blue and violet fire sharing a cell with another blast. */
                        unsigned weight=peak>48?(peak-48)>>3:0;
                        if(weight){
                            flash->weight+=weight;
                            flash->x+=weight*sx;flash->y+=weight*screen_y;
                            flash->r+=weight*r;flash->g+=weight*gr;flash->b+=weight*b;
                            if((r>=176 && r>=b+40) || (r>=236 && gr>=220))++hot_count[(screen_y/8)*GW+sx/8];
                        }
                    }
                }
            }
        }

    }
}

/* ---- light transport (per game frame, half resolution) ------------------- */

/* The playfield's rock is traced as a 2D scene at half resolution: a
 * distance field of the solid pixels lets rays step quickly through open
 * water. Lights are the burning sprites (fire, flashes, exhaust), gathered
 * per 8x8 cell. Each probe gets
 *   - direct light from every light in range, with a soft shadow from a
 *     sphere-traced ray through the rock;
 *   - bounced light: a few rays per frame in rotating directions pick up the
 *     light leaving the first rock they hit (its colour times the light it
 *     received last frame), so repeated frames add further bounces;
 *   - sunlight visibility along the light-shaft angle, up to the surface.
 * Everything is presentation only. */
enum { PW = W / 2, PH = H / 2, PN = PW * PH, MAX_LIGHTS = 140, BOUNCE_RAYS = 3 };

typedef struct { float x, y, r, g, b, range, radius2, inv_range, peak, axis_x, axis_y; } Light;

static uint8_t solid[PN];                 /* rock in the playfield, below the surface */
static float albedo[3][PN];               /* rock colour, 0..1 */
static float sdf[PN];                     /* probes to the nearest rock (0 inside) */
static float direct[3][PN], bounce[3][PN], total[3][PN];
static float light_dx[PN], light_dy[PN];  /* where the light comes from, weighted */
static float sunlight[N];                        /* incident sunlight at native pixel centres */
static float sun_vis[PN];                 /* 0..1 */
static Light lights[MAX_LIGHTS];
static unsigned tile_glow_sum[3][GW * GH];
static uint16_t tile_glow_count[GW * GH];
static int light_count, transport_frame;
static unsigned transport_scene = 0xffffffffu;
static int transport_waterline;
static const WorldSceneFx *transport_fx;
/* Conservative light bins; skip only contributions the old 1% test rejects. */
enum { LT = 8, LTX = (PW + LT - 1) / LT, LTY = (PH + LT - 1) / LT };
static uint16_t light_bins[LTX * LTY][MAX_LIGHTS], light_bin_count[LTX * LTY];
static uint8_t traced[PN];
typedef struct { float r, g, b, dx, dy, sun; } PixelLight;
static PixelLight pixel_light[N];
static void cache_transport(void);
static void bin_lights(void)
{
    memset(light_bin_count, 0, sizeof light_bin_count);
    for (int i = 0; i < light_count; ++i) {
        Light *l = &lights[i];
        float peak = fmaxf(l->r, fmaxf(l->g, l->b)), lo = 0, hi = l->range;
        for (int j = 0; j < 16; ++j) {
            float d = (lo + hi) * 0.5f, cut = 1 - d / l->range;
            if (peak * cut * cut / (1 + d * d * 0.012f) >= 0.01f) lo = d; else hi = d;
        }
        /* Upper bound plus epsilon: never remove a contributing old ray. */
        float radius2 = (hi + 0.01f) * (hi + 0.01f);
        l->radius2=radius2;
        l->peak=peak;
        l->inv_range=1.0f/l->range;
        for (int ty = 0; ty < LTY; ++ty) for (int tx = 0; tx < LTX; ++tx) {
            float x = l->x, y = l->y;
            float x0 = tx * LT, x1 = x0 + LT, y0 = ty * LT, y1 = y0 + LT;
            float dx = x < x0 ? x0-x : x > x1 ? x-x1 : 0;
            float dy = y < y0 ? y0-y : y > y1 ? y-y1 : 0;
            if (dx*dx+dy*dy <= radius2) {
                int b = ty*LTX+tx; light_bins[b][light_bin_count[b]++] = (uint16_t)i;
            }
        }
    }
}
/* Sample low-frequency indirect and local light on a regular lattice.
 * Native sunlight and contact shadows retain their full-resolution masks.
 * The last probe on either axis is an anchor even for a partial cell. */
enum { LIGHT_STEP=4 };
static void trace_mask(void)
{
    for (int y=0;y<PH;++y) for (int x=0;x<PW;++x)
        traced[y*PW+x]=(x%LIGHT_STEP==0 || x==PW-1) && (y%LIGHT_STEP==0 || y==PH-1);
}
static void reconstruct_transport(void)
{
    float *maps[]={direct[0],direct[1],direct[2],bounce[0],bounce[1],bounce[2],light_dx,light_dy,sun_vis};
    for (unsigned m=0;m<sizeof maps/sizeof maps[0];++m) {
        float *v=maps[m];
        for (int y=0;y<PH;++y) {
            if(y%LIGHT_STEP && y!=PH-1) continue;
            for (int x=0;x<PW-1;x+=LIGHT_STEP) {
                int end=clampi(x+LIGHT_STEP,0,PW-1), i=y*PW+x;
                float step=(v[y*PW+end]-v[i])/(end-x);
                for(int j=1;j<end-x;++j)v[i+j]=v[i]+step*j;
            }
        }
        for(int y=0;y<PH-1;y+=LIGHT_STEP) {
            int end=clampi(y+LIGHT_STEP,0,PH-1);
            for(int j=1;j<end-y;++j){
                float f=(float)j/(end-y);
                for(int x=0;x<PW;++x){
                    int i=y*PW+x;
                    v[i+j*PW]=v[i]+f*(v[end*PW+x]-v[i]);
                }
            }
        }
    }
}

/* Open water: blue or green and not bright (0..255). */
static inline int water_amount(uint32_t p)
{
    int r = (p >> 16) & 255, gr = (p >> 8) & 255, b = p & 255;
    int lum = (r * 3 + gr * 5 + b * 2) / 10;
    int cool = (gr > b ? gr : b) - r;
    return clamp255(clamp255(cool * 3) * (255 - lum) / 200);
}

/* Draw priority selects the artwork. Lighting sees the resulting surface. */
static inline uint32_t receiver_colour(int i, int y)
{
    for (int layer = y >= HUD ? 1 : 0; layer < 3; ++layer)
        if (layer_on[layer] && (layer_col[layer][i] >> 24)) return layer_col[layer][i];
    return 0;
}

/* Propagate sunlight through the native silhouette. The kernel is centred half
 * a pixel to the right on the row above, along the sun direction. Its spread
 * produces a penumbra that widens with distance from the casting edge. */
static void build_sunlight(void)
{
    float rows[2][LW],air[2][LW];
    for (int x=0; x<LW; ++x) rows[0][x]=rows[1][x]=air[0][x]=air[1][x]=1.0f;
    /* Continue the shadow through the tile margin above the viewport. */
    for (int y=-MARGIN; y<H; ++y) {
        const float *above=rows[(y+MARGIN)&1];
        float *below=rows[(y+MARGIN+1)&1];

        const float *sky=air[(y+MARGIN)&1];
        float *skyBelow=air[(y+MARGIN+1)&1];
        for (int x=1; x<LW-2; ++x) {
            float diffuse=g.waterline>0?(sky[x-1]+sky[x]+sky[x+1]+sky[x+2])*.25f:1.f;
            float incident=((above[x]+above[x+1])*3.0f+above[x-1]+above[x+2])*0.125f;
            if (y>=0 && x>=MARGIN && x<MARGIN+W) sunlight[y*W+x-MARGIN]=y+MARGIN>floor_top[x]?1.f:incident*(.84f+.16f*diffuse);
            float keep=1.f-.34f*receiver_opacity[(y+MARGIN)*LW+x]/255.f;
            int outside=g.waterline>0 && y<g.waterline;
            below[x]=outside?incident:incident*keep;
            skyBelow[x]=outside?diffuse*keep:diffuse;
        }
    }
}

/* Build the distance field from the same visible material as the sun pass. */
static void build_solid(int trace_sun)
{
    if (gpu_light) {
        for (int py=0; py<PH; ++py) {
            int sy=2*py+1;

            uint8_t *row=solid+py*PW;
            for (int px=0; px<PW; ++px) row[px]=layer_on[1] && terrain_cast[(sy+MARGIN)*LW+MARGIN+2*px+1];
        }
        return;
    }
    if (trace_sun) build_sunlight();
    for (int py = 0; py < PH; ++py)
        for (int px = 0; px < PW; ++px) {
            int p = py * PW + px, sx = px * 2 + 1, sy = py * 2 + 1;
            solid[p] = 0;
            albedo[0][p] = albedo[1][p] = albedo[2][p] = 0;
            /* The surface's own wave tiles are not rock. */
            if (sy >= H) continue;
            int i = (sy + MARGIN) * LW + sx + MARGIN;
            if (receiver_opacity[i]<128) continue;
            uint32_t c = receiver_colour(i, sy);
            solid[p] = 1;
            albedo[0][p] = ((c >> 16) & 255) / 255.0f;
            albedo[1][p] = ((c >> 8) & 255) / 255.0f;
            albedo[2][p] = (c & 255) / 255.0f;
        }
    /* Only the occupancy mask determines the distance transform. Animated
       colours and moving lights can change while that mask stays identical. */
    static uint8_t previous_solid[PN];
    static int have_distance;
    if (have_distance && !memcmp(previous_solid,solid,sizeof solid)) return;
    memcpy(previous_solid,solid,sizeof solid);
    have_distance=1;
    /* Chamfer distance transform (1, 1.414), two passes. */
    const float big = 1e6f, diag = 1.41421356f;
    for (int p = 0; p < PN; ++p) sdf[p] = solid[p] ? 0.0f : big;
    for (int y = 0; y < PH; ++y)
        for (int x = 0; x < PW; ++x) {
            float *d = &sdf[y * PW + x];
            if (x > 0 && d[-1] + 1 < *d) *d = d[-1] + 1;
            if (y > 0) {
                if (d[-PW] + 1 < *d) *d = d[-PW] + 1;
                if (x > 0 && d[-PW - 1] + diag < *d) *d = d[-PW - 1] + diag;
                if (x < PW - 1 && d[-PW + 1] + diag < *d) *d = d[-PW + 1] + diag;
            }
        }
    for (int y = PH - 1; y >= 0; --y)
        for (int x = PW - 1; x >= 0; --x) {
            float *d = &sdf[y * PW + x];
            if (x < PW - 1 && d[1] + 1 < *d) *d = d[1] + 1;
            if (y < PH - 1) {
                if (d[PW] + 1 < *d) *d = d[PW] + 1;
                if (x < PW - 1 && d[PW + 1] + diag < *d) *d = d[PW + 1] + diag;
                if (x > 0 && d[PW - 1] + diag < *d) *d = d[PW - 1] + diag;
            }
        }
}

/* Glowing scenery (lava, fires, flares painted into the tiles) shines too:
   per 8x8 cell, the fire-coloured share of the nearest world layer. */
static void gather_tile_glow(void)
{
    for (int cy = 0; cy < H / 8; ++cy)
        for (int cx = 0; cx < GW; ++cx) {
            unsigned sum[3] = {0, 0, 0}, n = 0;
            for (int j = 1; j < 8; j += 2)
                for (int i = 1; i < 8; i += 2) {
                    int sx = cx * 8 + i, sy = cy * 8 + j;
                    for (int layer = 1; layer < 3; ++layer) {
                        if (!layer_on[layer]) continue;
                        uint32_t c = layer_col[layer][(sy + MARGIN) * LW + sx + MARGIN];
                        if (!(c >> 24)) continue;
                        unsigned r = (c >> 16) & 255, gr = (c >> 8) & 255, b = c & 255;
                        if (r >= 200 && gr + 40 <= r && b + 110 <= r) { sum[0] += r; sum[1] += gr; sum[2] += b; ++n; }
                        break;                                /* the nearer layer hides the far one */
                    }
                }
            tile_glow_count[cy * GW + cx] = (uint16_t)n;
            for (int c = 0; c < 3; ++c) tile_glow_sum[c][cy * GW + cx] = sum[c];
        }
}

/* Linear RGB and reach in half-resolution light probes. Tube and rack
   discharges scatter a little blue-green light in water. Powder is warm;
   rocket exhaust has a pale hot core and a shorter amber flight plume. */
typedef struct { float r,g,b,range,offset,aim; } WeaponSpectrum;
static const WeaponSpectrum spectra[]={
    [TUBE_PULSE]={.48f,.93f,1.08f,29,0,0}, [HEAVY_TUBE]={.95f,1.44f,1.55f,38,0,0},
    [RACK_PULSE]={.12f,.28f,.30f,15,0,0}, [GUN_FLASH]={2.75f,1.52f,.58f,48,0,0},
    [MORTAR_FLASH]={3.65f,1.58f,.43f,64,0,0}, [ROCKET_IGNITION]={2.9f,1.63f,.76f,52,0,0},
    [ARC_FLASH]={.65f,1.68f,2.7f,44,0,0}, [ROCKET_PLUME]={.82f,.36f,.085f,15,-4,-1},
    [VERTICAL_PULSE]={.36f,.21f,.08f,13,0,0},
    [VERTICAL_EXHAUST]={.20f,.10f,.035f,7,-2,-1},
    [BURNING_FUEL]={1.65f,.52f,.08f,36,0,0}, [ARC_GLOW]={.24f,.70f,1.2f,27,0,0},
    /* Range is in probes; offset is native pixels along the flight axis.
       A zero aim is a round lamp; signed aim points forward or astern. */
    [TORPEDO_LENS]={.56f,.035f,.019f,10,5,0},
    [HEAVY_LENS]={.68f,.085f,.025f,14,6,0},
    [CHARGE_FUSE]={.56f,.19f,.035f,13,2,0},
    [TRACER]={.53f,.25f,.075f,9,-1,-1},
    [SHELL_FUSE]={.52f,.12f,.035f,11,2,0},
    [HOT_FRAGMENT]={.35f,.105f,.025f,7,0,0},
    [BEAM_CORE]={.13f,.44f,.78f,14,1,0},
    [ICE_SHARD]={.085f,.21f,.32f,7,0,0},
    [BOSS_AMBER]={1.12f,.52f,.13f,24,0,0}, [BOSS_RED]={1.28f,.12f,.045f,25,0,0},
    [BOSS_CYAN]={.12f,.82f,1.24f,28,0,0}, [BOSS_VIOLET]={.64f,.21f,1.04f,23,0,0},
    [BOSS_TEAL]={.08f,1.08f,.72f,29,0,0}, [BOSS_ICE]={.38f,.88f,1.40f,31,0,0}
};
static void gather_weapon_lights(void)
{
    for(unsigned i=0;i<weapon_count && light_count<MAX_LIGHTS;++i){
        WeaponEmitter e=weapon_emitters[i];
        WeaponSpectrum s=spectra[e.kind];
        float power=e.kind<ROCKET_PLUME ? e.level/255.f : .80f;
        float dx=e.dx/64.f,dy=e.dy/64.f;
        if(s.offset || s.aim){
            float length=sqrtf(dx*dx+dy*dy);
            if(length>0){dx/=length;dy/=length;}
        }
        lights[light_count++]=(Light){.x=(e.x+dx*s.offset)*.5f,.y=(e.y+dy*s.offset)*.5f,.r=s.r*power,
            .g=s.g*power,.b=s.b*power,.range=s.range,.axis_x=dx*s.aim,.axis_y=dy*s.aim};
    }
}

/* Taper the gas jet along the bore. Its short white core lights the actual
   receiver through transport; only the visible gas itself is composited. */
static void draw_weapon_flashes(uint32_t *out)
{
    for(unsigned i=0;i<weapon_count;++i){
        WeaponEmitter e=weapon_emitters[i];
        if(e.kind>=ROCKET_PLUME) continue;
        WeaponSpectrum s=spectra[e.kind];
        int wet=g.waterline<0 || e.y>=g.waterline;
        float length=(e.kind==VERTICAL_PULSE?3:e.kind==RACK_PULSE?4:e.kind==MORTAR_FLASH?14:10)*(wet?.72f:1);
        float inv=1/sqrtf((float)(e.dx*e.dx+e.dy*e.dy));
        for(int y=-15;y<=15;++y)for(int x=-15;x<=15;++x){
            int sx=e.x+x-g.shake_x/g.k,sy=e.y+y-g.shake_y/g.k;
            if(sx<0 || sx>=W || sy<0 || sy>=H)continue;
            float along=(x*e.dx+y*e.dy)*inv,side=fabsf((y*e.dx-x*e.dy)*inv);
            if(along < -1 || along>=length)continue;
            float taper=1-fmaxf(0,along)/length;
            float alpha=e.level/255.f*taper*(1-side/(.65f+2.2f*taper));
            if(alpha<=0)continue;
            int cell=(sy+MARGIN)*LW+sx+MARGIN;
            if(layer_on[0] && layer_pri[0][cell])continue;
            for(int row=0;row<g.k;++row)for(int col=0;col<g.k;++col){
                uint32_t *p=out+(size_t)(sy*g.k+row)*g.ow+sx*g.k+col;
                unsigned c=*p;
                *p=(c&0xff000000u)|(0xffffff&light_rgb(((c>>16)&255)+(int)(145*s.r*alpha),
                    ((c>>8)&255)+(int)(145*s.g*alpha),(c&255)+(int)(145*s.b*alpha)));
            }
        }
    }
}

/* Combine nearby impact cels before filtering. Sprite slots may change as
   the board animates an explosion, so match in scroll-corrected space. */
static void update_blast_lights(void)
{
    BlastLight target[MAX_SPRITES]={0};
    for(unsigned i=0;i<MAX_SPRITES;++i){
        const BlastRadiance *f=&blast_radiance[i];
        if(!f->weight)continue;
        float inv=1.f/f->weight,x=f->x*inv,y=f->y*inv;
        unsigned slot=blast_light_count;
        float nearest=16*16;
        for(unsigned j=0;j<blast_light_count;++j){
            float dx=x-blast_lights[j].x,dy=y-blast_lights[j].y;
            float d=dx*dx+dy*dy;
            if(d<nearest){nearest=d;slot=j;}
        }
        if(slot==blast_light_count){
            if(slot==MAX_SPRITES)continue;
            blast_lights[blast_light_count++]=(BlastLight){.x=x,.y=y};
        }
        float power=1.75f*f->weight/(f->weight+850.f);
        float range=9+21*power;
        /* The strongest overlapping cel supplies the colour and centre. */
        if(target[slot].range>=range)continue;
        float r=f->r*inv/255.f,gr=f->g*inv/255.f,b=f->b*inv/255.f;
        target[slot]=(BlastLight){x,y,r*r*power,gr*gr*power,b*b*power,range};
    }
    unsigned keep=0;
    for(unsigned i=0;i<blast_light_count;++i){
        BlastLight l=blast_lights[i],t=target[i];
        float rate=t.range?.28f:.15f;
        l.r+=rate*(t.r-l.r);l.g+=rate*(t.g-l.g);l.b+=rate*(t.b-l.b);
        if(t.range){
            l.x+=.5f*(t.x-l.x);l.y+=.5f*(t.y-l.y);
            l.range+=l.range?.28f*(t.range-l.range):t.range;
        }
        if(t.range || fmaxf(l.r,fmaxf(l.g,l.b))>.01f)blast_lights[keep++]=l;
    }
    blast_light_count=keep;
}

static void gather_lights(void)
{
    light_count=0;
    for(unsigned i=0;i<blast_light_count && light_count<MAX_LIGHTS;++i){
        const BlastLight *l=blast_lights+i;
        lights[light_count++]=(Light){.x=.5f*l->x,.y=.5f*l->y,
            .r=l->r,.g=l->g,.b=l->b,.range=l->range};
    }
    gather_weapon_lights();
    gather_tile_glow();
    for(int y=0;y<GH && light_count<MAX_LIGHTS;y+=2)
    for(int x=0;x<GW && light_count<MAX_LIGHTS;x+=2){
        unsigned n=0,sx=0,sy=0,sum[3]={0};
        for(int dy=0;dy<2 && y+dy<GH;++dy)
        for(int dx=0;dx<2 && x+dx<GW;++dx){
            int at=(y+dy)*GW+x+dx;
            unsigned count=tile_glow_count[at];
            /* Tile emission is independent of the blast crossing this cell. */
            if(!count)continue;
            n+=count;sx+=count*(4*(x+dx)+2);sy+=count*(4*(y+dy)+2);
            for(int c=0;c<3;++c)sum[c]+=tile_glow_sum[c][at];
        }
        if(!n)continue;
        float power=.56f*fminf(n/40.f,1);
        float r=sum[0]/(255.f*n),g0=sum[1]/(255.f*n),b=sum[2]/(255.f*n);
        lights[light_count++]=(Light){.x=(float)sx/n,.y=(float)sy/n,
            .r=r*r*power,.g=g0*g0*power,.b=b*b*power,.range=25};
    }
}

static inline float sdf_at(float x, float y)
{
    int ix = (int)x, iy = (int)y;
    if (ix < 0 || iy < 0 || ix >= PW || iy >= PH) return 4.0f;
    return sdf[iy * PW + ix];
}

/* Soft visibility from (x, y) toward a point dist away along (ux, uy). A
 * probe inside rock may reach up to two probes out of it (lit faces). */
static float trace_soft(float x, float y, float ux, float uy, float dist, float k)
{
    float t = 0.0f, res = 1.0f;
    int inside = 0;
    while (t < dist && sdf_at(x + ux * t, y + uy * t) <= 0.0f) {
        if (++inside > 2) return 0.0f;
        t += 1.0f;
    }
    while (t < dist - 1.0f) {
        float h = sdf_at(x + ux * t, y + uy * t);
        if (h < 0.5f) return 0.0f;
        float v = k * (h - 0.5f) / (t + 1.0f);
        if (v < res) res = v;
        t += h > 0.8f ? h : 0.8f;
    }
    return res < 0 ? 0 : res;
}

static void transport_rows(int from, int to)
{
    const WorldSceneFx *fx = transport_fx;
    float surface = transport_waterline < 0 ? -1.0f : transport_waterline / 2.0f;
    const float sun_y = -0.8944272f;   /* along the light shafts, up and right */
    for (int y = from; y < to; ++y)
        for (int x = 0; x < PW; ++x) {
            int p = y * PW + x;
            if (!traced[p]) continue;
            int bin = (y / LT) * LTX + x / LT;
            float fx0 = x + 0.5f, fy0 = y + 0.5f;
            float dr = 0, dg = 0, db = 0, vx = 0, vy = 0;
            if (y < PH && fx->lights)
                for (int j = 0; j < light_bin_count[bin]; ++j) {
                    const Light *l = &lights[light_bins[bin][j]];
                    float dx = l->x - fx0, dy = l->y - fy0, d2 = dx * dx + dy * dy;
                    if (d2 > l->radius2) continue;
                    float d = sqrtf(d2), cut = 1.0f - d * l->inv_range;
                    float fall = cut * cut / (1.0f + d2 * 0.012f);
                    if((l->axis_x || l->axis_y) && d>.001f){
                        float facing=-(dx*l->axis_x+dy*l->axis_y)/d;
                        float t=fminf(1,fmaxf(0,(facing+.15f)/.95f));
                        fall*=.18f+.82f*t*t*(3-2*t);
                    }
                    /* Bound the unoccluded contribution before tracing: distant
                       explosion cells otherwise spend most of a busy frame on
                       shadow rays for less than 1% light per colour channel. */
                    if (fall * l->peak < 0.01f) continue;
                    float vis = d < 1.5f ? 1.0f : trace_soft(fx0, fy0, dx / d, dy / d, d, 9.0f);
                    if (vis <= 0.0f) continue;
                    float e = fall * vis;
                    int wet=submerged_path(fy0*2,l->y*2,d*2);
                    dr += l->r * e * light_absorb[0][wet]; dg += l->g * e * light_absorb[1][wet]; db += l->b * e * light_absorb[2][wet];
                    float lum = (l->r + l->g + l->b) * e;
                    if (d > 0.01f) { vx += dx / d * lum; vy += dy / d * lum; }
                }
            direct[0][p] = dr; direct[1][p] = dg; direct[2][p] = db;
            light_dx[p] = vx; light_dy[p] = vy;

            /* Bounce: the rock each ray meets reflects what it received. */
            float br = 0, bg = 0, bb = 0;
            if (y < PH && fx->lights) {
                static const float direction[BOUNCE_RAYS][2] = {
                    {0.966f, 0.259f}, {-0.707f, 0.707f}, {-0.259f, -0.966f}
                };
                for (int r = 0; r < BOUNCE_RAYS; ++r) {
                    float ux = direction[r][0], uy = direction[r][1], t = 0.0f;
                    int inside = 0;
                    while (sdf_at(fx0 + ux * t, fy0 + uy * t) <= 0.0f && inside < 3) { ++inside; t += 1.0f; }
                    if (inside >= 3) continue;
                    for (int step = 0; step < 24 && t < 48.0f; ++step) {
                        float hx = fx0 + ux * t, hy = fy0 + uy * t;
                        int ix = (int)hx, iy = (int)hy;
                        if (ix < 0 || iy < 0 || ix >= PW || iy >= PH) break;
                        float s = sdf[iy * PW + ix];
                        if (s <= 0.0f) {
                            int q = iy * PW + ix;
                            float fade = 1.0f / (1.0f + t * 0.05f);
                            int wet=submerged_path(fy0*2,hy*2,t*2);
                            br += albedo[0][q] * total[0][q] * fade * light_absorb[0][wet];
                            bg += albedo[1][q] * total[1][q] * fade * light_absorb[1][wet];
                            bb += albedo[2][q] * total[2][q] * fade * light_absorb[2][wet];
                            break;
                        }
                        t += s > 0.8f ? s : 0.8f;
                    }
                }
                float gain = 0.9f / BOUNCE_RAYS;
                br *= gain; bg *= gain; bb *= gain;
            }
            /* Ease changes in reflected radiance over successive game frames. */
            float keep = transport_valid ? 0.7f : 0.0f;
            bounce[0][p] = bounce[0][p] * keep + br * (1 - keep);
            bounce[1][p] = bounce[1][p] * keep + bg * (1 - keep);
            bounce[2][p] = bounce[2][p] * keep + bb * (1 - keep);

            float sv = fx->sunshadow ? sunlight[(2*y+1)*W+2*x+1] : 1.0f;
            /* Sunlit rock contributes to the same radiance field that bounce
               rays sample on the next frame. Water filters the incoming ray. */
            if (fx->sunshadow && fx->lights && solid[p] && sv > 0.0f && sky_reaches(2*x+1,2*y+1)) {
                float depth = fmaxf(0.0f, fy0 - fmaxf(surface, 0.0f));
                int wet = clampi((int)(2.0f * depth / -sun_y), 0, 319);
                float sun = 0.48f * sv;
                direct[0][p] += sun * light_absorb[0][wet];
                direct[1][p] += sun * 0.96f * light_absorb[1][wet];
                direct[2][p] += sun * 0.86f * light_absorb[2][wet];
            }
            sun_vis[p] = sv;
        }
}

/* Interleave probe rows across the eight workers so that a blast near the
 * seabed does not leave seven workers waiting for one dense strip. */
static void distributed_transport_rows(int from,int to)
{
    for (int row=from; row<to; ++row) {
        int y=(row/8)+(row%8)*(PH/8);
        transport_rows(y,y+1);
    }
}

/* Each worker writes disjoint light-grid rows from the completed sprite
   and tile planes. Use the compositor's exact subpixel-to-texel mapping. */
static void pack_gpu_light_rows(int from,int to)
{
    int stage=lighting_stage();
    for(int y=from;y<to;++y)for(int x=0;x<PW;++x){
        int at=y*PW+x, sx=(2*x+1)*g.k, sy=(2*y+1)*g.k;
        uint16_t sprite=sprite_px[(size_t)sy*g.ow+sx];
        uint32_t c=0;
        if(solid[at]) c=receiver_colour((2*y+1+MARGIN)*LW+2*x+1+MARGIN,2*y+1)|0xff000000u;
        if(sprite!=NONE && !(sprite&0x4000) && (!(sprite&0x8000)||!solid[at]))
            c=cur.colors[sprite&1023]|0xff000000u;
        unsigned coverage=0,ice=0;
        for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx){
            int nx=2*x+dx, ny=2*y+dy;
            int px=nx*g.k+g.shake_x, py=ny*g.k+g.shake_y;
            unsigned sp=(unsigned)px<(unsigned)g.ow && (unsigned)py<(unsigned)g.oh
                ?sprite_px[(size_t)py*g.ow+px]:NONE;
            int terrain=divide[clampi(py,0,g.oh-1)+g.off_y[1]]*LW+
                        divide[clampi(px,0,g.ow-1)+g.off_x[1]];
            unsigned bit=1u<<(dy*2+dx);
            if(sp!=NONE && !(sp&0x5000) && (!(sp&0x8000) || !layer_on[1] || !layer_pri[1][terrain]))coverage|=bit<<4;
            if(layer_on[1] && terrain_cast[terrain])coverage|=bit;
            if(layer_on[1] && terrain/LW>floor_top[terrain%LW])ice|=bit<<4;
            gpu_light->surface[ny*W+nx]=sp!=NONE && g.fx->materials
                ?sprite_material[(size_t)clampi(py,0,g.oh-1)*g.ow+clampi(px,0,g.ow-1)]:0;
            if(stage==0 && layer_on[2]){
                int far_pixel=divide[clampi(py,0,g.oh-1)+g.off_y[2]]*LW+divide[clampi(px,0,g.ow-1)+g.off_x[2]];
                if(far_ice[far_pixel])ice|=bit;
            }
        }
        gpu_light->ice_coverage[at]=ice;
        gpu_light->cells[at]=(c&0xffffff)|(coverage<<24);
    }
}

static void transport(const WorldSceneFx *fx)
{
    /* Glow has its own switch: request radiance without enabling surface lighting. */
    WorldSceneFx requested=*fx;
    requested.lights=fx->lights || fx->glow || fx->bubble_light || (gpu_light && (fx->rim || fx->headlights));
    requested.sunshadow=fx->sunshadow || fx->bubble_light;
    fx=&requested;
    if (!fx->lights && !fx->sunshadow && !(gpu_light && (fx->water_volume || fx->rays))) {
        if (fx->debris || fx->silt || fx->sparks || fx->impact_motes || fx->pressure || fx->sediment_light || fx->water_volume || fx->aftermath) build_solid(0);
        transport_valid = 0; return;
    }
    if (transport_scene != cur.scene_id) { transport_valid = 0; transport_scene = cur.scene_id; }
    transport_fx = fx;
    transport_waterline = g.waterline;
    build_solid(fx->sunshadow);
    if (fx->lights) gather_lights();
    else light_count = 0;
    if (gpu_light) {
        memcpy(gpu_light->sky_stop,sky_stop,sizeof sky_stop);
        ++gpu_light->revision;
        unsigned lamps=0;
        for(int player=0;fx->headlights && player<2;++player){
            const int *bow=cur.lamp[player];
            if(!bow[2])continue;
            /* Board actor origin supplies a stable mount through hull animation.
               Coverage validates a live hull without moving the lens per pose. */
            int nose=bow[0],visible=0;
            for(int dx=-12;dx<=12;++dx){
                int x=nose-17*bow[2]+dx,y=bow[1];
                if((unsigned)x>=W || (unsigned)y>=H)continue;
                unsigned ink=sprite_px[(size_t)(y*g.k)*g.ow+x*g.k];
                if(ink!=NONE && ((ink&1023)>>4)==(unsigned)(1+player))visible=1;
            }
            if(!visible)continue;
            float *e=gpu_light->emitters[lamps++];
            static const float ranges[]={124,148,174,208};
            static const float gains[]={1.30f,1.48f,1.68f,1.90f};
            unsigned level=fx->headlight_level & 3;
            float gain=gains[level];
            e[0]=(nose-g.shake_x/g.k)*.5f;e[1]=(bow[1]-g.shake_y/g.k)*.5f;e[2]=ranges[level];e[3]=(float)bow[2];
            e[4]=2.8f*gain;e[5]=1.94f*gain;e[6]=.94f*gain;e[7]=0;
        }
        gpu_light->count=lamps+light_count;
        if(gpu_light->count>WORLD_EMITTERS)gpu_light->count=WORLD_EMITTERS;
        gpu_light->waterline=g.waterline;
        for(int i=0;i<(int)(gpu_light->count-lamps);++i){
            float *e=gpu_light->emitters[lamps+i];const Light *l=lights+i;
            e[3]=(l->axis_x || l->axis_y)?4+l->axis_x:0;e[7]=l->axis_y;
            e[0]=l->x-.5f*g.shake_x/g.k;e[1]=l->y-.5f*g.shake_y/g.k;e[2]=l->range;
            e[4]=l->r;e[5]=l->g;e[6]=l->b;
        }
        parallel(pack_gpu_light_rows,PH);
        transport_valid=0;
        return;
    }
    if (!transport_valid) { memset(total, 0, sizeof total); memset(bounce, 0, sizeof bounce); }
    bin_lights();
    trace_mask();
    parallel(distributed_transport_rows, PH);
    reconstruct_transport();
    for (int c = 0; c < 3; ++c)
        for (int p = 0; p < PN; ++p) total[c][p] = direct[c][p] + bounce[c][p];
    /* A soft shoulder so a big blast tints its surroundings instead of
       burning them to white. The bounce pass reads total, so it stays in the
       same range. */
    for (int p = 0; p < PN; ++p) light_shoulder(&total[0][p],&total[1][p],&total[2][p]);
    cache_transport();
    ++transport_frame;
    transport_valid = 1;
}

/* Bilinear look-ups at a screen pixel. */
static inline float probe_sample(const float *map, int x, int y)
{
    float fx = (x - 1) * 0.5f, fy = (y - 1) * 0.5f;
    if (fx < 0) fx = 0;
    if (fy < 0) fy = 0;
    int ix = (int)fx, iy = (int)fy;
    if (ix >= PW - 1) ix = PW - 2;
    if (iy >= PH - 1) iy = PH - 2;
    float ax = fx - ix, ay = fy - iy;
    if (ax > 1) ax = 1;
    if (ay > 1) ay = 1;
    const float *m = map + iy * PW + ix;
    return (m[0] * (1 - ax) + m[1] * ax) * (1 - ay) + (m[PW] * (1 - ax) + m[PW + 1] * ax) * ay;
}

/* Shared by both layers and both display refreshes; no repeated bilinear
 * lookups or light-direction normalization in the per-layer pixel loop. */
static PixelLight average_light(PixelLight a, PixelLight b)
{
    return (PixelLight){(a.r+b.r)*0.5f,(a.g+b.g)*0.5f,(a.b+b.b)*0.5f,
        (a.dx+b.dx)*0.5f,(a.dy+b.dy)*0.5f,(a.sun+b.sun)*0.5f};
}
static PixelLight horizontal_light[PH][W];
static void horizontal_light_rows(int from, int to)
{
    /* Exact separable form of probe_sample at integer screen coordinates.
     * Copy anchors once, then average the gaps instead of six bilinear calls
     * per pixel. Keep raw direction until after interpolation. */
    for (int y=from;y<to;++y) {
        PixelLight *row=horizontal_light[y];
        for (int x=0;x<PW;++x) {
            int p=y*PW+x;
            row[2*x+1]=(PixelLight){total[0][p],total[1][p],total[2][p],light_dx[p],light_dy[p],sun_vis[p]};
        }
        row[0]=row[1];
        for (int x=1;x<PW;++x) row[2*x]=average_light(row[2*x-1],row[2*x+1]);
    }
}

static void finish_light_rows(int from, int to)
{
    for (int y=from; y<to; ++y) {
        const PixelLight *row=horizontal_light[y/2];
        PixelLight *out=pixel_light+y*W;
        if (y && !(y&1)) {
            for (int x=0; x<W; ++x) out[x]=average_light(horizontal_light[y/2-1][x],row[x]);
        } else memcpy(out,row,W*sizeof *out);
        for (int x=0; x<W; ++x) {
            PixelLight *l=out+x;
            float inv=1.0f/sqrtf(l->dx*l->dx+l->dy*l->dy+0.0001f);
            l->dx*=inv;
            l->dy*=inv;
            if (transport_fx->sunshadow) l->sun=sunlight[y*W+x];
        }
    }
}

static void cache_transport(void)
{
    parallel(horizontal_light_rows,PH);
    parallel(finish_light_rows,H);
}

/* ---- per refresh: layer colours and per-layer effects ------------------- */

/* Each tile walks only the particles intersecting it, in arcade draw order. */
static unsigned effect_tails[WORLD_EFFECT_TILES];
static uint64_t effect_coverage[WORLD_EFFECT_TILES];
static int gpu_stamp(int x,int y,int radius,int phase,unsigned rgba,unsigned kind,unsigned limit,
                     int left,int top,int right,int bottom)
{
    if(!gpu_light)return 0;
    if(right<0 || bottom<0 || left>=W || top>=H)return 1;
    WorldEffectBatch *batch=&gpu_light->effects;
    int x0=clampi(left,0,W-1)/8,x1=clampi(right,0,W-1)/8;
    int y0=clampi(top,0,H-1)/8,y1=clampi(bottom,0,H-1)/8;
    if(batch->count+(x1-x0+1)*(y1-y0+1)>=WORLD_EFFECT_NODES)return 0;
    for(int row=y0;row<=y1;++row)for(int col=x0;col<=x1;++col){
        unsigned tile=row*40+col, n=++batch->count;
        batch->nodes[n]=(WorldEffectNode){x,y,radius,phase,rgba,kind,limit,0};
        int xmin=clampi(left-col*8,0,7),xmax=clampi(right-col*8,0,7);
        int ymin=clampi(top-row*8,0,7),ymax=clampi(bottom-row*8,0,7);
        uint64_t cols=((2u<<xmax)-(1u<<xmin))*UINT64_C(0x0101010101010101);
        uint64_t rows=(UINT64_MAX<<(8*ymin)) & (UINT64_MAX>>(8*(7-ymax)));
        effect_coverage[tile]|=cols&rows;
        if(batch->heads[tile])batch->nodes[effect_tails[tile]].next=n;
        else batch->heads[tile]=n;
        effect_tails[tile]=n;
    }
    return 1;
}
static unsigned stamp_colour(int r,int gr,int b,int a)
{
    return (unsigned)clamp255(a)<<24 | (unsigned)clamp255(r)<<16 |
           (unsigned)clamp255(gr)<<8 | (unsigned)clamp255(b);
}
static int wf_rock(float x,float y);
static void wf_pixel(uint32_t *out,int x,int y,int r,int gr,int b,int a,int underwater);
#include "world_scene_cinema.h"

static void colour_rows(int from, int to)
{
    int stage=lighting_stage();
    for (int layer = 0; layer < 3; ++layer) {
        if (!layer_on[layer]) continue;
        for (int i = from * LW; i < to * LW; ++i) {
            unsigned index = layer_idx[layer][i];
            uint32_t c = index == 0xffff ? 0 : cur.colors[index];
            layer_col[layer][i] = c;
        }
    }
    for (int by = from; by < to; ++by) {
        for (int i = by * LW; i < (by + 1) * LW; ++i) {
            uint32_t c = receiver_colour(i, by - MARGIN);
            int lum = (((c >> 16) & 255) * 3 + ((c >> 8) & 255) * 5 + (c & 255) * 2) / 10;
            receiver_lum[i] = (uint8_t)lum;
            if(!gpu_light){
                if(stage<0)receiver_opacity[i]=(uint8_t)(clampi(lum*12,0,255)*(255-water_amount(c))/255);
                else receiver_opacity[i]=layer_on[1] && terrain_cast[i]?255:
                    stage==0 && layer_on[2] && far_ice[i]?80:0;
            }
        }
    }
}

/* One water classification per palette entry is enough for all three
   arcade layers. The palette can animate on every board tick. */
static uint8_t palette_water[1024];
static void layer_rows_fx(int from, int to)
{
    const WorldSceneFx *fx = g.fx;
    int stage=lighting_stage();
    const WaterGrade *grade=stage>=0?&water_grades[stage]:NULL;
    const int lit = (fx->lights || fx->glow) && transport_valid, shadowed = fx->sunshadow && transport_valid;
    for (int layer = 0; layer < 3; ++layer) {
        if (!layer_on[layer]) continue;
        for (int by = from; by < to; ++by) {
            int y = by - MARGIN;
            if(y>=H || (layer==0 && y>=HUD)){memset(layer_wat[layer]+by*LW,0,LW);continue;}
            int wy = y + g.scroll_y[1];
            int depth = y - g.waterline;
            int under = g.waterline < 0 ? 256 : depth < 0 ? 0 : depth >= 8 ? 256 : (depth + 1) * 32;
            if (depth < 0) depth = 0;
            float profile=(!gpu_light && grade && fx->depth)?clampi(depth,0,195)/195.f:0;
            int depth_r=profile?(int)(256*(1+profile*(grade->r-.53f))):256;
            int depth_g=profile?(int)(256*(1+profile*(grade->g-.65f))):256;
            int depth_b=profile?(int)(256*(1+profile*(grade->b-.79f))):256;
            int ray_fade = depth < 190 ? 190 - depth : 0;
            int sy = clampi(y, 0, H - 1), in_play = y >= 0 && y < H;
            for (int bx = 0; bx < LW; ++bx) {
                int i = by * LW + bx;
                uint32_t p = layer_col[layer][i];
                if (!(p >> 24)) { layer_wat[layer][i] = 0; continue; }
                int r = (p >> 16) & 255, gr = (p >> 8) & 255, b = p & 255;
                const int r0 = r, g0 = gr, b0 = b;          /* the surface's own colour */
                int x = bx - MARGIN, wx = x + g.scroll_x[1], sx = clampi(x, 0, W - 1);
                int lum = receiver_lum[i];
                int w = palette_water[layer_idx[layer][i]] * under >> 8;
                layer_wat[layer][i] = (uint8_t)w;
                int surface = w < 128;                       /* rock, ice, wrecks: not open water */
                int sky=in_play && sky_reaches(sx,sy);
                float sv = shadowed && sky ? pixel_light[sy*W+sx].sun : 1.0f;

                if (fx->depth && under) {
                    /* Water absorbs red first, then green: the light that
                       reaches this depth from the surface. Rock and wrecks keep most of their colour. */
                    r=r*depth_r>>8; gr=gr*depth_g>>8; b=b*depth_b>>8;
                    int d = depth > 319 ? 319 : depth;
                    int amount = w + ((255 - w) >> 2);
                    r = r * (256 - (amount * (256 - absorb[0][d]) >> 8)) >> 8;
                    gr = gr * (256 - (amount * (256 - absorb[1][d]) >> 8)) >> 8;
                    b = b * (256 - (amount * (256 - absorb[2][d]) >> 8)) >> 8;
                }
                if (fx->haze && under) {
                    /* Distance in water: the far layer drifts toward blue. */
                    int t = 23 * w >> 8;
                    r += (6 - r) * t >> 8; gr += (34 - gr) * t >> 8; b += (64 - b) * t >> 8;
                }
                /* Expose ambient scenery before adding direct and scattered light.
                   Submerged distance and traced sunlight set the local exposure. */
                if (fx->lights && under) {
                    float distance = clampi(depth, 0, 220) / 220.0f;
                    float exposure = 1.0f - (under / 256.0f) *
                        (0.09f + 0.24f * distance + 0.24f * (1.0f - sv));
                    r = (int)(r * exposure);
                    gr = (int)(gr * exposure);
                    b = (int)(b * exposure);
                }
                if (surface && under && bx>0 && bx<LW-1 && by>0 && by<LH-1 &&
                    fx->contact_shade) {
                    const uint8_t *L=receiver_lum+i;
                    int relief=(L[-1]+L[1]+L[-LW]+L[LW])/4-lum;
                    int keep=256-(clampi(relief,0,96)*under>>9);
                    r=r*keep>>8; gr=gr*keep>>8; b=b*keep>>8;
                }
                if (shadowed && surface && under && in_play) {
                    /* Rock out of the sun's reach is a little darker. */
                    int keep = 222 + (int)(34 * sv);
                    r = r * keep >> 8; gr = gr * keep >> 8; b = b * keep >> 8;
                }
                if (lit && in_play) {
                    const PixelLight *light = &pixel_light[sy*W+sx];
                    float lr = light->r, lg = light->g, lb = light->b;
                    if (lr + lg + lb > 0.004f) {
                        if (surface && fx->lights) {
                            /* Lit surfaces show their own colour (not the
                               water's absorption), facing the light more. */
                            float k = 1.18f * (128 - w) / 128.0f;
                            r += (int)((r0 + 70) * lr * k); gr += (int)((g0 + 70) * lg * k); b += (int)((b0 + 70) * lb * k);
                        }
                        if (fx->glow && under) {
                            /* Light scattered by the water itself: halos,
                               strongest in open water. */
                            float k = (w * 0.5f + 40.0f) * under / 256.0f * (fx->water_volume ? .65f : 1.0f);
                            r += (int)(lr * k); gr += (int)(lg * k); b += (int)(lb * k);
                        }
                    }
                }
                if (!gpu_light && sky && fx->rays && w && ray_fade) {
                    int s = (wx * 2 + wy) * 8;
                    int n = F(s * 2 + g.Q) + F(s * 5 - g.Q * 2 + 2400) + F(s + g.Q / 2 + 5600);
                    if (n > 60) {
                        int add = (int)((n - 60) * ray_fade * w / (190 * 255) * sv * (fx->water_volume ? .65f : 1.0f));
                        r += add * 20 >> 8; gr += add * 36 >> 8; b += add * 42 >> 8;
                    }
                }
                layer_col[layer][i] = y < H ? light_rgb(r,gr,b) :
                    0xff000000u | (uint32_t)clamp255(r)<<16 | (uint32_t)clamp255(gr)<<8 | (uint32_t)clamp255(b);
            }
        }
    }
}

/* ---- composite ------------------------------------------------------------ */

static inline __attribute__((always_inline)) void compose_scaled_rows(int from, int to, int k)
{
    const WorldSceneFx *fx = g.fx;
    const int ow = g.ow, oh = g.oh;
    const uint32_t backdrop = cur.colors[0];
    struct Memo {int mat_cell,post_cell,bg_cell,bg_layer; uint32_t bg_base,bg_result;uint32_t mat,base,shaded,input,result;} memo[W];
    memset(memo,0,sizeof memo);
    int last_field_row=-1, displaced=0;
    for (int Y = from; Y < to; ++Y) {
        int y1 = Y / k;
        uint32_t *o = g.out + (size_t)Y * ow;

        /* Keep displaced samples on the displayed playfield. The tilemap
           outside it can contain unrelated palette rows and unused art. */
        int shaken_y = clampi(Y + g.shake_y, 0, oh - 1);
        int row_dx = g.row_dx[y1]+g.shake_x, row_dy = shaken_y-Y;
        int glint = fx->surface && g.waterline >= 0 && y1 >= g.waterline - 3 && y1 <= g.waterline + 1;
        int block_row = Y % k == 0;
        /* Adjacent output rows within one native scanline often sample
         * identical art. Reuse only when every layer and sprite input agrees.
         * Each worker copies exclusively from rows it has already produced. */
        if (field_on && y1!=last_field_row) {
            static const int8_t quiet[W];
            displaced=memcmp(field_y+y1*W,quiet,W)!=0;
            last_field_row=y1;
        }
        if (Y>from && !block_row && !displaced) {
            int same=1;
            for (int layer=0;layer<3;++layer) {
                int v=(layer ? shaken_y : Y)+g.off_y[layer];
                int prev_y=layer ? clampi(Y-1+g.shake_y,0,oh-1) : Y-1;
                if (layer_on[layer] && divide[v]!=divide[prev_y+g.off_y[layer]]) same=0;
            }
            int sy=Y+g.shake_y;
            if (same && (sy==0 || sy==oh)) same=0;
            if (same && sy>0 && sy<oh) {
                size_t pos=(size_t)sy*ow;
                same=!memcmp(sprite_px+pos,sprite_px+pos-ow,ow*sizeof *sprite_px);
                if (same && (fx->materials || fx->contact_shade))
                    same=!memcmp(sprite_material+pos,sprite_material+pos-ow,ow*sizeof *sprite_material);
            }
            if (same && y1>=HUD && g.shake_y)
                same=!memcmp(sprite_px+(size_t)Y*ow,sprite_px+(size_t)(Y-1)*ow,ow*sizeof *sprite_px);
            if (same) {
                memcpy(o,o-ow,ow*sizeof *o);
                if (y1>=HUD) {
                    uint32_t *hud=hud_overlay+(size_t)(Y-HUD*k)*ow;
                    memcpy(hud,hud-ow,ow*sizeof *hud);
                }
                continue;
            }
        }
        for (int X = 0; X < ow; ++X) {
            int x1 = k == 2 ? X / 2 : divide[X];
            int dx = row_dx, dy = row_dy;
            if (field_on) {
                dx += field_x[y1 * W + x1] * k / 4;
                dy += field_y[y1 * W + x1] * k / 4;
            }
            /* Nearest layer first: the first opaque one is the pixel. Offsets
               stay inside the margin, so no clamping is needed. */
            int U = clampi(X + dx, 0, ow-1), V = clampi(Y + dy, 0, oh-1);
            int protect=fx->pressure || fx->shock || fx->surface;
            int base_u=protect?clampi(X+g.shake_x,0,ow-1):U,base_v=protect?shaken_y:V;
            uint32_t c = backdrop;
            int rim_receiver = 0;
            int id = -1, wet = 0, at[3] = {-1, -1, -1};
            for (int layer = 0; layer < 3; ++layer) {
                if (!layer_on[layer]) continue;
                int i = layer ? divide[base_v + g.off_y[layer]] * LW + divide[base_u + g.off_x[layer]]
                              : divide[Y + g.off_y[0]] * LW + divide[X + g.off_x[0]];
                at[layer] = i;
                uint32_t p = layer_col[layer][i];
                if(layer==2 && !layer_pri[2][i] && protect){
                    int far_i=divide[V+g.off_y[2]]*LW+divide[U+g.off_x[2]];
                    p=layer_col[2][far_i];
                }
                if (!(p >> 24)) continue;
                c = p;
                id = layer;
                wet = layer_wat[layer][i];
                break;
            }
            if (!gpu_light || (fx->ocean_depth &&
                !(gpu_light->effects.ocean_rows[y1/32] & (1u<<(y1%32))))) {
                struct Memo *background=&memo[x1];
                int key=y1*W+x1+1;
                if (background->bg_cell!=key || background->bg_layer!=id || background->bg_base!=c) {
                    background->bg_cell=key;
                    background->bg_layer=id;
                    background->bg_base=c;
                    background->bg_result=cinema_background(c,id,x1,y1);
                }
                c=background->bg_result;
            }
            int qx=X+g.shake_x, qy=Y+g.shake_y;
            /* HUD sprites are screen-fixed. Move a world sprite as a whole
               even when it crosses the score band or a water distortion. */
            uint16_t fixed=sprite_px[(size_t)Y*ow+X];
            int hud_sprite=y1>=HUD && fixed!=NONE && (fixed&0x4000);
            if(hud_sprite){qx=X;qy=Y;}
            if (qx >= 0 && qy >= 0 && qx < ow && qy < oh) {
                uint16_t sprite = sprite_px[(size_t)qy * ow + qx];
                if (sprite != NONE && (!(sprite & 0x4000) || hud_sprite || (qx==X && qy==Y))) {
                    int blocked = 0;
                    if (sprite & 0x8000) {
                        /* A sprite behind the tiles is hidden by any layer's
                           front pens here, including layers behind the pixel. */
                        for (int layer = 0; layer < 3 && !blocked; ++layer) {
                            if (!layer_on[layer]) continue;
                            int i = at[layer];
                            if (i < 0)
                                i = divide[base_v + g.off_y[layer]] * LW + divide[base_u + g.off_x[layer]];
                            blocked = layer_pri[layer][i] && (layer_col[layer][i] >> 24);
                        }
                    }
                    if (!blocked) {
                        rim_receiver = (sprite & 0x2000) != 0;
                        c = cur.colors[sprite & 1023];
                        id = 3;
                        uint32_t mat=(fx->materials || fx->contact_shade)?sprite_material[(size_t)qy*ow+qx]:0;
                        if(mat && !hud_sprite){
                            int cell=y1*W+x1+1;struct Memo *m=&memo[x1];
                            if(cell!=m->mat_cell || mat!=m->mat || c!=m->base){
                                m->mat_cell=cell;m->mat=mat;m->base=c;m->shaded=material_shade(c,mat,x1,y1);
                            }
                            c=m->shaded;
                        }
                        if ((!mat || !fx->materials) && fx->lights && transport_valid && !hud_sprite) {
                            float lr = pixel_light[y1*W+x1].r, lg = pixel_light[y1*W+x1].g,
                                  lb = pixel_light[y1*W+x1].b;
                            int r = (c >> 16) & 255, gr = (c >> 8) & 255, b = c & 255;
                            /* Fire does not light itself; darker paint takes the most. */
                            if (lr + lg + lb > 0.004f && !(r >= 176 && r >= b + 40)) {
                                float k = 0.45f * (300 - (r * 3 + gr * 5 + b * 2) / 10) / 300.0f;
                                r += (int)((r + 20) * lr * k); gr += (int)((gr + 20) * lg * k);
                                b += (int)((b + 20) * lb * k);
                                c = light_rgb(r,gr,b);
                            }
                        }
                    }
                }
            }
            if (block_row && X % k == 0) {
                water_map[y1 * W + x1] = (uint8_t)(id >= 0 && id < 3 ? wet : 0);
            }

            /* Most pixels need none of the screen-space colour work. */
            int cell = y1 * W + x1;
            int hud_pixel = y1 >= HUD && (id == 0 || (id == 3 && hud_sprite));
            if (y1 >= HUD) hud_overlay[(size_t)(Y - HUD*k)*ow + X] = hud_pixel ? c : 0;
            int needs = !hud_pixel && (glint || (fx->vivid && !gpu_light));
            struct Memo *post=&memo[x1];uint32_t post_input=c;
            int post_tag=(cell+1)*2+(glint && id>0 && id<3 && wet>60);
            if(needs && post->post_cell==post_tag && post->input==c)c=post->result;
            else if (needs) {
                int r = (c >> 16) & 255, gr = (c >> 8) & 255, b = c & 255;
                if (glint && id>0 && id<3 && wet>60) {
                    int wx = x1 + g.scroll_x[1];
                    int n = S(wx * 41 + g.T * 6) + S(wx * 17 - g.T * 4) + S(wx * 7 + y1 * 90 + g.T * 2);
                    if (n > 430) { int add = (n - 430) / 2; r += add; gr += add; b += add; }
                }
                if (fx->vivid && !gpu_light) {
                    int lum = (r * 3 + gr * 5 + b * 2) / 10;
                    r = lum + (r - lum) * 17 / 16; gr = lum + (gr - lum) * 17 / 16; b = lum + (b - lum) * 17 / 16;
                }
                c = light_rgb(r,gr,b);
                post->post_cell=post_tag;post->input=post_input;post->result=c;
            }
            if(gpu_light && !hud_pixel){
                int volume=(fx->water_volume || fx->rays || fx->ocean_depth) && id>=0 && id<3 && (g.waterline<0 || y1>=g.waterline);
                // Foreground destructible tiles and middle scenery share
                // the facade silhouette while retaining their drawing planes.
                int terrain_rim=fx->rim && (id==0 || id==1);
                unsigned scenery_tag=(id==0?0x80:0x60)|(volume?0x0e:0x1e);
                unsigned tag=terrain_rim?scenery_tag:rim_receiver?0xae:volume?0xee:0xfe;
                c=(c&0xffffffu)|(tag<<24);
            }
            /* In the GPU input tag, retain the five volume/particle bits
               alongside the plane. Sprites and UI keep their original tag. */
            if(camera_fractional && !hud_pixel && (id==1 || id==2) && !(gpu_light && fx->rim && id==1)){
                unsigned tag=gpu_light ? ((unsigned)id<<5)|((c>>24)&31) : 250u+id;
                c=(c&0xffffffu)|(tag<<24);
            }
            o[X] = c;
        }
    }
}

/* Specialize the common 120 Hz scale so native pixel divisions and
 * subpixel phase tests compile to shifts and masks in the inner loop. */
static void compose_rows(int from, int to)
{
    if (g.k == 2) compose_scaled_rows(from, to, 2);
    else compose_scaled_rows(from, to, g.k);
}

/* ---- the sea surface as a mirror ----------------------------------------- */

/* Just below the waterline the water reflects what is above it: the rows
 * above, mirrored, rippling and fading with depth, on open water only. */
static void reflect_surface(uint32_t *out)
{
    int wl = g.waterline, k = g.k;
    if (wl < 12 || wl > H - 12) return;
    int reach = wl < 30 ? wl : 30;
    if (wl + reach > H) reach = H - wl;
    if(gpu_light){gpu_light->effects.reflect_row=wl;gpu_light->effects.reflect_rows=reach;}
    for (int d = 0; d < reach; ++d) {
        int a = (int)(80.0f * (1.0f - (float)d / reach));
        int ripple = (S(d * 53 + g.T * 6) + S(d * 19 - g.T * 4)) * (d + 3) * k / 2400;
        if(gpu_light){
            gpu_light->effects.ripple[d]=ripple;
            const uint8_t *wet=water_map+(wl+d)*W;
            for(int x=0;x<W;++x){
                unsigned bits=0;
                if(wet[x]>=60){
                    for(int j=0;j<k;++j)for(int i=0;i<k;++i){
                        int sx=x*k+i+g.shake_x,sy=(wl+d)*k+j+g.shake_y;
                        if((unsigned)sx>=(unsigned)g.ow || (unsigned)sy>=(unsigned)g.oh ||
                           sprite_px[sy*g.ow+sx]==NONE)bits|=1u<<(j*k+i);
                    }
                }
                gpu_light->effects.mirror[d][x]=bits|((unsigned)(a*wet[x]>>8)<<16);
            }
            continue;
        }
        for (int sub = 0; sub < k; ++sub) {
            int Y = (wl + d) * k + sub, SY = (wl - 1 - d) * k + (k - 1 - sub);
            if (SY < 0 || Y >= g.oh) continue;
            uint32_t *o = out + (size_t)Y * g.ow;
            const uint32_t *src = out + (size_t)SY * g.ow;
            const uint8_t *wet = water_map + (wl + d) * W;
            for (int X = 0; X < g.ow; ++X) {
                int w = wet[divide[X]];
                if (w < 60) continue;
                int sx=X+g.shake_x,sy=Y+g.shake_y;
                if(sx>=0 && sx<g.ow && sy>=0 && sy<g.oh && sprite_px[(size_t)sy*g.ow+sx]!=NONE)continue;
                int t = a * w >> 8;
                int SX = clampi(X + ripple, 0, g.ow - 1);
                uint32_t q = o[X], m = src[SX];
                int r = (int)((q >> 16) & 255), gr = (int)((q >> 8) & 255), b = (int)(q & 255);
                r += ((int)((m >> 16) & 255) - r) * t >> 8;
                gr += ((int)((m >> 8) & 255) - gr) * t >> 8;
                b += ((int)(m & 255) - b) * t >> 8;
                o[X] = (q&0xff000000u) | (uint32_t)r << 16 | (uint32_t)gr << 8 | (uint32_t)b;
            }
        }
    }
}

#include "world_scene_waterfx.h"

/* ---- explosions: rings, shake, particles -------------------------------- */
extern unsigned world_scene_explosions;

static void spawn_particle(float x, float y, int kind)
{
    for (int i = 0; i < MAX_PARTICLES; ++i) {
        Particle *p = &particles[i];
        if (p->life > 0 && p->age < p->life) continue;
        p->x = x; p->y = y; p->age = 0; p->kind = kind;
        if (kind == 0) {            /* ember */
            p->vx = (frand() - 0.5f) * 70.0f; p->vy = -20.0f - frand() * 50.0f; p->life = 0.35f + frand() * 0.45f;
        } else {                    /* bubble */
            p->vx = (frand() - 0.5f) * 26.0f; p->vy = -24.0f - frand() * 40.0f; p->life = 0.8f + frand() * 1.0f;
        }
        return;
    }
}

/* A cell full of fire where there was none a frame ago is a new explosion. */
static void detect_explosions(const WorldSceneFx *fx)
{
    for (int cy = 0; cy < H / 8; ++cy)
        for (int cx = 0; cx < GW; ++cx) {
            int cell = cy * GW + cx;
            if (fx->silt && hot_count[cell] >= 8 && hot_before[cell] < 3)
                waterfx_impact(cx * 8 + 4.0f, cy * 8 + 4.0f);
            if ((fx->debris || fx->sparks || fx->impact_motes || fx->pressure || fx->sediment_light || fx->water_volume || fx->aftermath || fx->blast_reflect) && hot_count[cell] >= 10 && hot_before[cell] < 3) {
                int area = 0, peak = 1;
                for (int j = -1; j <= 1; ++j)
                    for (int i = -1; i <= 1; ++i) {
                        int x = cx + i, y = cy + j;
                        if (x < 0 || x >= GW || y < 0 || y >= H / 8) continue;
                        int q = y * GW + x;
                        area += hot_count[q];
                        if (hot_count[q] > hot_count[cell] || (hot_count[q] == hot_count[cell] && q < cell)) peak = 0;
                    }
                if (peak && area >= 24) waterfx_blast(cx * 8 + 4.0f, cy * 8 + 4.0f, area, fx);
            }
            if (hot_count[cell] < 26) continue;
            /* A real fireball is big, is the brightest cell of its patch, and
               sits where nothing burned a frame ago (moving flames, shots and
               the submarine itself always have a hot neighbour behind them). */
            int around = 0, size = 0, peak = 1;
            for (int j = -2; j <= 2; ++j)
                for (int i = -2; i <= 2; ++i) {
                    int x = cx + i, y = cy + j;
                    if (x < 0 || y < 0 || x >= GW || y >= GH) continue;
                    if (hot_before[y * GW + x] > around) around = hot_before[y * GW + x];
                    if (abs(i) <= 1 && abs(j) <= 1) size += hot_count[y * GW + x];
                    if (hot_count[y * GW + x] > hot_count[cell]) peak = 0;
                }
            if (around > 3 || size < 70 || !peak) continue;
            float x = cx * 8 + 4.0f, y = cy * 8 + 4.0f;
            int near_ring = 0;
            for (int i = 0; i < MAX_RINGS; ++i)
                if (rings[i].life > 0 && rings[i].age < 0.2f && fabsf(rings[i].x - x) < 28 && fabsf(rings[i].y - y) < 28)
                    near_ring = 1;
            if (near_ring) continue;
            int slot = 0;
            for (int i = 0; i < MAX_RINGS; ++i) {
                if (rings[i].life <= 0 || rings[i].age >= rings[i].life) { slot = i; break; }
                if (rings[i].age > rings[slot].age) slot = i;
            }
            rings[slot] = (Ring){x, y, 0, 0.5f, 1.0f};
            ++world_scene_explosions;
            waterfx_blast(x, y, size, fx);
            if (fx->shake && size >= 110) {       /* only the big ones shake the screen */
                shake += 1.0f;
                if (shake > 2.5f) shake = 2.5f;
                if (rumble < 40000) rumble += 22000;
            }
            if (fx->particles) {
                for (int i = 0; i < 7; ++i) spawn_particle(x + (frand() - 0.5f) * 14, y + (frand() - 0.5f) * 14, 0);
                for (int i = 0; i < 5; ++i) spawn_particle(x + (frand() - 0.5f) * 20, y + (frand() - 0.5f) * 12, 1);
            }
        }
    /* Burning things keep shedding the odd ember. */
    if (fx->particles)
        for (int cell = 0; cell < GW * (H / 8); ++cell)
            if (hot_count[cell] >= 24 && frand() < 0.10f)
                spawn_particle((cell % GW) * 8 + frand() * 8, (cell / GW) * 8 + frand() * 8, 0);
    memcpy(hot_before, hot_count, sizeof hot_before);
}

static void build_field(const WorldSceneFx *fx, float dt)
{
    field_on = 0;
    int any = 0;
    for (int i = 0; i < MAX_RINGS; ++i) {
        Ring *ring = &rings[i];
        if (ring->life <= 0) continue;
        ring->age += dt;
        if (ring->age >= ring->life) { ring->life = 0; continue; }
        any = 1;
    }
    if (!any || !fx->shock || fx->pressure) return;
    memset(field_x, 0, sizeof field_x);
    memset(field_y, 0, sizeof field_y);
    field_on = 1;
    for (int i = 0; i < MAX_RINGS; ++i) {
        const Ring *ring = &rings[i];
        if (ring->life <= 0) continue;
        float t = ring->age / ring->life, radius = 6.0f + 110.0f * t, band = 9.0f;
        float strength = 10.0f * (1.0f - t) * ring->strength;        /* quarter pixels */
        float inner = radius > band ? radius - band : 0.0f, outer = radius + band;
        int x0 = clampi((int)(ring->x - radius - band), 0, W - 1), x1 = clampi((int)(ring->x + radius + band), 0, W - 1);
        int y0 = clampi((int)(ring->y - radius - band), g.waterline<0?0:clampi(g.waterline,0,H-1), H - 1), y1 = clampi((int)(ring->y + radius + band), 0, H - 1);
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) {
                float dx = x - ring->x, dy = y - ring->y, d2 = dx * dx + dy * dy;
                /* Only the thin ring needs a square root, not its entire box. */
                if (d2 >= outer * outer || d2 < inner * inner) continue;
                float d = sqrtf(d2);
                float off = fabsf(d - radius);
                if (off >= band || d < 1.0f) continue;
                float push = strength * (1.0f - off / band);
                int j = y * W + x;
                field_x[j] = (int8_t)clampi(field_x[j] - (int)(dx / d * push), -16, 16);
                field_y[j] = (int8_t)clampi(field_y[j] - (int)(dy / d * push), -16, 16);
            }
    }
}

static inline void add_block(uint32_t *out, int x, int y, int size, int r, int gr, int b)
{
    if(gpu_stamp(x,y,size,0,stamp_colour(r,gr,b,255),3,0,x/g.k,y/g.k,
                 (x+size-1)/g.k,(y+size-1)/g.k))return;
    for (int j = 0; j < size; ++j) {
        int py = y + j;
        if (py < 0 || py >= H*g.k) continue;
        for (int i = 0; i < size; ++i) {
            int px = x + i;
            if (px < 0 || px >= g.ow) continue;
            int sx=px+g.shake_x,sy=py+g.shake_y;
            if(sx>=0 && sx<g.ow && sy>=0 && sy<g.oh && sprite_px[(size_t)sy*g.ow+sx]!=NONE)continue;
            if(water_map[(py/g.k)*W+px/g.k]<60)continue;
            uint32_t p = out[(size_t)py * g.ow + px];
            out[(size_t)py * g.ow + px] = 0xff000000u | (uint32_t)clamp255((int)((p >> 16) & 255) + r) << 16 |
                                          (uint32_t)clamp255((int)((p >> 8) & 255) + gr) << 8 |
                                          (uint32_t)clamp255((int)(p & 255) + b);
        }
    }
}

static void draw_particles(uint32_t *out, const WorldSceneFx *fx, float dt, uint32_t time_ms)
{
    int k = g.k;
    for (int i = 0; i < MAX_PARTICLES; ++i) {
        Particle *p = &particles[i];
        if (!fx->particles) {p->life=0;continue;}
        if (p->life <= 0) continue;
        p->age += dt;
        if (p->age >= p->life) { p->life = 0; continue; }
        p->x += p->vx * dt;
        p->y += p->vy * dt;
        if (p->kind == 0) {
            int wet=g.waterline<0 || p->y>=g.waterline;
            p->vx/=1+dt*(wet?7:2);p->vy-=dt*(wet?10:30);
            if(wet && p->life>.28f)p->life=.28f;
        }
        else p->x += sinf(p->age * 9.0f + (float)i) * 10.0f * dt;
        if (!fx->particles) continue;
        int x = (int)p->x, y = (int)p->y;
        if (x < 0 || y < 0 || x >= W || y >= H) { if (y < -4) p->life = 0; continue; }
        if (p->kind == 1 && g.waterline >= 0 && y < g.waterline + 2) { p->life = 0; continue; }
        float fade = fmaxf(0,1.0f - p->age / p->life);
        int px = (int)(p->x * k), py = (int)(p->y * k);
        if (p->kind == 0) add_block(out, px, py, k, (int)(230 * fade), (int)(120 * fade), (int)(30 * fade));
        else add_block(out, px, py, k < 2 ? 1 : k - 1, (int)(70 * fade), (int)(110 * fade), (int)(120 * fade));
    }
    /* Marine snow on two parallax layers, over open water only. */
    if (fx->snow)
        for (unsigned n = 0; n < 80; ++n) {
            unsigned h = n * 2654435761u;
            int layer = (int)(n & 1);
            unsigned speed = layer ? 9 : 5;
            int drift_x = layer ? g.scroll_x[1] : g.scroll_x[2], drift_y = layer ? g.scroll_y[1] : g.scroll_y[2];
            int x = (int)((h >> 8) % W) - (int)(time_ms * speed / 1000 % W) + (S((int)(time_ms / 9 + (h >> 4))) >> 6) - drift_x;
            int y = (int)((h >> 18) % H) + (int)(time_ms * (speed + 2) / 1400 % H) - drift_y;
            x = ((x % W) + W) % W;
            y = ((y % H) + H) % H;
            if (water_map[y * W + x] < 70) continue;
            int add=(layer?42:24)+(S((int)(h>>12)+g.T/6)>>6);
            add_block(out,x*k,y*k,k,add*2/3,add,add*9/8);
        }
}

/* ---- render ---------------------------------------------------------------- */

/* Stage timing for tuning (WORLD_SCENE_RUN prints it). */
double world_scene_stage_ms[6];
double world_scene_waterfx_draw_ms;
unsigned world_scene_explosions;      /* explosions detected so far */
static double stamp(void)
{
    LARGE_INTEGER t, f;
    QueryPerformanceCounter(&t);
    QueryPerformanceFrequency(&f);
    return (double)t.QuadPart * 1000.0 / (double)f.QuadPart;
}

int world_scene_render(uint32_t *out, unsigned scale, double alpha, const WorldSceneFx *fx, uint32_t clock)
{
    static int audit_selection=-2;
    if(audit_selection==-2){const char *v=getenv("WORLD_SCENE_OPTION");audit_selection=v?atoi(v):-1;}
    WorldSceneFx audit_fx={0};
    if(audit_selection>=0){
        switch(audit_selection){
        case 2: audit_fx.rays=1;break;
        case 3: audit_fx.depth=1;break;
        case 4: audit_fx.snow=1;break;
        case 5: audit_fx.vivid=1;break;
        case 7: audit_fx.trails=1;break;
        case 8: audit_fx.lights=1;break;
        case 11: audit_fx.haze=1;break;
        case 13: audit_fx.sunshadow=1;break;
        case 14: audit_fx.glow=1;break;
        case 15: audit_fx.reflect=1;break;
        case 17: audit_fx.shock=1;break;
        case 18: audit_fx.particles=1;break;
        case 19: audit_fx.shake=1;break;
        case 20: audit_fx.surface=1;break;
        case 21: audit_fx.smooth=1;break;
        case 22: audit_fx.interp=1;break;
        case 23: audit_fx.wakes=1;break;
        case 24: audit_fx.debris=1;break;
        case 25: audit_fx.silt=1;break;
        case 26: audit_fx.splashes=1;break;
        case 27: audit_fx.sparks=1;break;
        case 28: audit_fx.impact_motes=1;break;
        case 29: audit_fx.pressure=1;break;
        case 30: audit_fx.sediment_light=1;break;
        case 31: audit_fx.water_volume=1;break;
        case 32: audit_fx.ocean_depth=1;break;
        case 33: audit_fx.aftermath=1;break;
        case 34: audit_fx.materials=1;break;
        case 36: audit_fx.bubble_light=1;break;
        case 37: audit_fx.contact_shade=1;break;
        case 38: audit_fx.blast_reflect=1;break;
        default:break;
        }
        fx=&audit_fx;
    }
    if (!cur.valid || !out) return 0;
    /* The title's logo and ice are flat artwork over the sea. Casting their
       silhouettes into that water produces long dark bands below the logo. */
    WorldSceneFx title_fx;
    switch (cur.scene_id & 65535) {
    case 0x1de6: case 0x1e6c: case 0x1ea5:
    case 0x1f19: case 0x1f52: case 0x1f85:
        title_fx = *fx;
        title_fx.sunshadow = 0;
        title_fx.contact_shade = 0;
        fx = &title_fx;
        break;
    }
    /* Story, ending and other non-gameplay screens without a sea surface are
       authored artwork and captions, often over sky. Underwater depth grading
       would darken the sky and dim the caption text. 0C04 and 0C3E hold the
       last gameplay picture, so they keep its grade. */
    unsigned master = cur.scene_id & 65535;
    if (master != 0xb2e && master != 0xc04 && master != 0xc3e && cur.waterline < 0 && fx != &title_fx) {
        title_fx = *fx;
        title_fx.depth = title_fx.ocean_depth = title_fx.water_volume = 0;
        title_fx.haze = title_fx.rays = title_fx.sunshadow = title_fx.contact_shade = 0;
        fx = &title_fx;
    }
    if (!ready) prepare();
    if(gpu_light){
        memset(effect_coverage,0,sizeof effect_coverage);
        memset(gpu_light->effects.heads,0,sizeof gpu_light->effects.heads);
        gpu_light->effects.count=0;
        gpu_light->effects.reflect_rows=0;
        gpu_light->effects.history_reset=!scene_cache_valid || !have_history || cached_scale!=(int)scale || !cached_fx.trails;
    }
    int k = scale < 1 ? 1 : scale > MAXK ? MAXK : (int)scale;
    g.k = k; g.ow = W * k; g.oh = H * k; g.fx = fx;
    if (divide_k != k) {
        for (int i = 0; i < LW * MAXK + 8; ++i) divide[i] = (uint16_t)(i / k);
        divide_k = k;
        have_history = 0;
    }
    uint32_t time_ms = clock / 8;
    float dt = last_clock ? (float)(clock - last_clock) / 8000.0f : 0.0f;
    if (dt < 0 || dt > 0.1f) dt = 0.016f;
    last_clock = clock;
    g.T = (int)(time_ms / 12);
    g.Q = (int)(clock / 12);
    g.waterline = cur.waterline;
    waterline_used = cur.waterline < 0 ? 0 : cur.waterline > H ? H : cur.waterline;
    double ahead = fx->smooth && alpha>1 ? fmin(alpha-1,1.0) : 0;
    if (alpha < 0) alpha = 0;
    if (alpha > 1 || !fx->interp) alpha = 1;

    /* Where each layer is: between the previous and the current true scroll
       (whole pixels plus the camera's fraction), in subpixels. */
    for (int layer = 0; layer < 3; ++layer) {
        g.scroll_x[layer] = cur.layers[layer].scroll_x;
        g.scroll_y[layer] = cur.layers[layer].scroll_y;
        double now_x = fx->smooth ? cur.frac[layer] / 256.0 : 0.0, now_y = 0.0;
        double x = now_x, y = now_y;
        int dx = (int16_t)(prev.layers[layer].scroll_x - cur.layers[layer].scroll_x);
        int dy = (int16_t)(prev.layers[layer].scroll_y - cur.layers[layer].scroll_y);
        if (alpha < 1 && abs(dx) <= 16 && abs(dy) <= 16) {
            double before_x = dx + (fx->smooth ? prev.frac[layer] / 256.0 : 0.0);
            x = before_x + (now_x - before_x) * alpha;
            y = dy + (now_y - dy) * alpha;
        }
        if (ahead && layer && abs(dx)<=8 && abs(dy)<=8 &&
            cur.layers[layer].master==prev.layers[layer].master) {
            x += ahead * (now_x - prev.frac[layer]/256.0 - dx);
            y -= ahead * dy;
        }
        if (layer_rows[layer]) x = 0;                 /* per-row scroll: left as the game set it */
        g.off_x[layer] = (int)floor(x * k + 0.5) + MARGIN * k;
        g.off_y[layer] = (int)floor(y * k + 0.5) + MARGIN * k;
        if(camera_output && layer){
            camera_output[2*(layer-1)]=(float)(x*k-(g.off_x[layer]-MARGIN*k));
            camera_output[2*(layer-1)+1]=(float)(y*k-(g.off_y[layer]-MARGIN*k));
        }
    }

    camera_fractional=camera_output && (camera_output[0] || camera_output[1] || camera_output[2] || camera_output[3]);

    /* Ripples stay local to the surface; full-screen shimmer was removed. */
    for (int y = 0; y < H; ++y) {
        int shift = 0;
        int below = g.waterline < 0 ? 400 : y - g.waterline;
        if (y < H && below >= 0) {
            if (fx->surface && below < 28) shift += S(below * 46 - g.T * 7) * (28 - below) * 2 * k / (28 * 256);
        }
        g.row_dx[y] = shift;
    }
    if (fx->shake && shake > 0.05f) {
        float amplitude = 0.5f * shake * k;
        g.shake_x = (int)lroundf(amplitude * sinf(time_ms * 0.09f));
        g.shake_y = (int)lroundf(amplitude * cosf(time_ms * 0.07f));
        shake *= expf(-dt * 7.0f);
    } else {
        g.shake_x = g.shake_y = 0;
        if (!fx->shake) shake = 0;
    }

    int changed_frame=new_frame;
    int fx_changed=!scene_cache_valid || memcmp(&cached_fx,fx,sizeof *fx)!=0;
    int layers_dirty=changed_frame || fx_changed || (uint32_t)(clock-layer_clock)>=133;
    double t0 = stamp();
    if(layers_dirty){
        parallel(colour_rows,LH);
    }
    double t1 = stamp();
    if(changed_frame || fx_changed || cached_scale!=k || ((fx->smooth || fx->interp) && sprite_alpha!=alpha))draw_sprites(alpha);
    if(changed_frame)update_blast_lights();
    if(changed_frame || fx_changed)transport(fx);
    if(gpu_light){
        gpu_light->scale=k;
        gpu_light->flags=(fx->depth?65536u:0u)|(fx->headlights?32768u:0u)|(fx->rim?16384u:0u)|(fx->lights?1u:0u)|(fx->sunshadow?2u:0u)|(fx->glow?4u:0u)|(fx->trails?8u:0u)|
            (fx->materials?1048576u:0u)|(fx->bubble_light?16u:0u)|(fx->water_volume?32u:0u)|(fx->reflect?64u:0u)|(fx->vivid?128u:0u)|(fx->rays?512u:0u)|(fx->ocean_depth?4096u:0u)|(fx->haze?8192u:0u)|
            (fx->caustics?4194304u:0u)|(fx->auto_quality?2097152u:0u);
        const unsigned strengths[6]={fx->light_strength,fx->shadow_strength,fx->depth_strength,
                                     fx->shaft_strength,fx->rim_strength,fx->caustic_strength};
        for(int i=0;i<6;++i)gpu_light->strengths[i]=strengths[i]?strengths[i]*.01f:1.f;
        int stage=lighting_stage();
        WaterGrade grade=stage>=0?water_grades[stage]:(WaterGrade){.53f,.65f,.79f,1.0625f};
        memcpy(gpu_light->water_grade,&grade,sizeof grade);
        static const float neutral_light[12]={1,1,1,1,1,1,1,1,0,1,1,1};
        memcpy(gpu_light->stage_light,stage>=0?stage_lighting[stage]:neutral_light,sizeof neutral_light);
        if(cur.waterline<=0)gpu_light->flags|=1u<<19;
        if(stage>=0)gpu_light->flags|=1u<<17;
        if(stage==0)gpu_light->flags|=1u<<18;
        gpu_light->waterline=g.waterline;
        if (world_scene_light_ready) world_scene_light_ready(gpu_light);
    }
    if (new_frame) {
        waterfx_begin_frame();
        waterfx_motion(fx);
        detect_explosions(fx);
        new_frame = 0;
    }
    double t2 = stamp();
    if(layers_dirty){
        for(unsigned i=0;i<1024;++i)palette_water[i]=(uint8_t)water_amount(cur.colors[i]);
        parallel(layer_rows_fx,LH);
        layer_clock=clock;
    }
    double t3 = stamp();
    build_field(fx, dt);
    cinema_volume_dirty=changed_frame || fx_changed || layers_dirty;
    cinema_update(dt);
    if(changed_frame || fx_changed)material_prepare();

    if(gpu_light && fx->ocean_depth){
        // CPU color is still needed before surface glints and muzzle overlays.
        // All other terrain can take the same integer tint in the final shader.
        uint32_t *rows=gpu_light->effects.ocean_rows;
        memset(rows,0xff,sizeof gpu_light->effects.ocean_rows);
        if(fx->surface && g.waterline>=0){
            for(int y=clampi(g.waterline-3,0,H-1);y<=clampi(g.waterline+1,0,H-1);++y)
                rows[y/32]&=~(1u<<(y%32));
        }
        if(fx->lights)for(unsigned i=0;i<weapon_count;++i){
            const WeaponEmitter *e=weapon_emitters+i;
            if(e->kind>=ROCKET_PLUME)continue;
            int center=e->y-g.shake_y/k;
            for(int y=clampi(center-15,0,H-1);y<=clampi(center+15,0,H-1);++y)
                rows[y/32]&=~(1u<<(y%32));
        }
    }
    g.out = out;
    parallel(compose_rows, g.oh);
    double t4 = stamp();

    if (fx->lights) draw_weapon_flashes(out);
    if (fx->reflect) reflect_surface(out);
    double waterfx_start = stamp();
    if (fx->blast_reflect) draw_blast_reflections(out);
    waterfx_draw(out, fx, dt);
    cinema_draw(out);
    world_scene_waterfx_draw_ms = stamp() - waterfx_start;
    draw_particles(out, fx, dt, time_ms);

    if (fx->trails && !gpu_light) {
        size_t n = (size_t)g.ow * (size_t)(H * k);
        if (have_history) {
            /* Four BGRA pixels at once. 7/16 is the exact phosphor decay. */
            const __m128i zero = _mm_setzero_si128(), seven = _mm_set1_epi16(7);
            for (size_t i = 0; i < n; i += 4) {
                __m128i old = _mm_loadu_si128((const __m128i *)(history + i));
                __m128i lo = _mm_unpacklo_epi8(old, zero), hi = _mm_unpackhi_epi8(old, zero);
                lo = _mm_srli_epi16(_mm_mullo_epi16(lo, seven), 4);
                hi = _mm_srli_epi16(_mm_mullo_epi16(hi, seven), 4);
                __m128i color = _mm_loadu_si128((const __m128i *)(out + i));
                color = _mm_max_epu8(color, _mm_packus_epi16(lo, hi));
                _mm_storeu_si128((__m128i *)(out + i), color);
                _mm_storeu_si128((__m128i *)(history + i), color);
            }
        } else {
            memcpy(history, out, n * sizeof *out);
        }
        have_history = 1;
    } else
        have_history = gpu_light && fx->trails;
    for (size_t i=0, n=(size_t)g.ow*(H-HUD)*k; i<n; ++i)
        if (hud_overlay[i]) out[(size_t)g.ow*HUD*k+i]=hud_overlay[i];
    if(gpu_light){
        /* Tag visibility only in occupied tiles. Opaque UI written later stays
           protected. Each bit follows the sampling rule of its CPU effect. */
        for(int tile=0;gpu_light->effects.count && tile<WORLD_EFFECT_TILES;++tile){
            if(!gpu_light->effects.heads[tile])continue;
            int left=(tile%40)*8,top=(tile/40)*8;
            uint64_t covered=effect_coverage[tile];
            while(covered){
                unsigned bit=(unsigned)__builtin_ctzll(covered);
                covered&=covered-1;
                int x=left+(bit&7),y=top+(bit>>3);
                int dx=g.shake_x,dy=g.shake_y;
                if(!(fx->pressure || fx->shock || fx->surface)){
                    dx+=g.row_dx[y];
                    if(field_on){dx+=field_x[y*W+x]*k/4;dy+=field_y[y*W+x]*k/4;}
                }
                int wet=water_map[y*W+x]>=60;
                for(int j=0;j<k;++j)for(int i=0;i<k;++i){
                    int xx=x*k+i,yy=y*k+j,at=yy*g.ow+xx;
                    if((out[at]>>24)==255)continue;
                    int u=xx+dx,v=yy+dy;
                    unsigned mask=0;
                    if((unsigned)u>=(unsigned)g.ow || (unsigned)v>=(unsigned)g.oh || sprite_px[v*g.ow+u]==NONE){
                        mask=1;
                        if(wet && (g.waterline<0 || y>=g.waterline) && (!layer_on[0] ||
                            !(layer_col[0][divide[yy+g.off_y[0]]*LW+divide[xx+g.off_x[0]]]>>24)))mask|=2;
                    }
                    u=xx+g.shake_x;v=yy+g.shake_y;
                    if(wet && ((unsigned)u>=(unsigned)g.ow || (unsigned)v>=(unsigned)g.oh || sprite_px[v*g.ow+u]==NONE))mask|=4;
                    out[at]=(out[at]&0xffffff)|(out[at]&0xf0000000u)|(mask<<24);
                }
            }
        }
    }
    cached_fx=*fx;cached_scale=k;sprite_alpha=alpha;scene_cache_valid=1;
    double t5 = stamp();
    world_scene_stage_ms[0] += t1 - t0; world_scene_stage_ms[1] += t2 - t1; world_scene_stage_ms[2] += t3 - t2;
    world_scene_stage_ms[3] += t4 - t3; world_scene_stage_ms[4] += t5 - t4; world_scene_stage_ms[5] += 1;
    return 1;
}

unsigned world_scene_take_rumble(void)
{
    unsigned value = rumble > 65535 ? 65535 : rumble;
    rumble = 0;
    return value;
}

int world_scene_waterline(void) { return waterline_used; }

void world_scene_debug(uint32_t *out)
{
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            int p = (y / 2) * PW + x / 2;
            int r, gr, b;
            if (solid[p]) { r = (int)(albedo[0][p] * 255); gr = (int)(albedo[1][p] * 255); b = (int)(albedo[2][p] * 255); }
            else r = gr = b = (int)(sun_vis[p] * 90);
            r += (int)(total[0][p] * 200); gr += (int)(total[1][p] * 200); b += (int)(total[2][p] * 200);
            out[y * W + x] = light_rgb(r,gr,b);
        }
    for (int i = 0; i < light_count; ++i) {
        int x = (int)(lights[i].x * 2), y = (int)(lights[i].y * 2);
        if (x >= 0 && y >= 0 && x < W && y < H) out[y * W + x] = 0xffff00ffu;
    }
}
