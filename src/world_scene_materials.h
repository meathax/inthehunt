/* Source-texel material maps. These never change sprite coverage or geometry.
 * Palette roles below are authored from the player and industrial boss sheets.
 * Unknown large sprites use restrained paint/steel; effects and bullets opt out. */
enum { MAT_NONE, MAT_PAINT, MAT_STEEL, MAT_CHROME, MAT_BRASS, MAT_COPPER,
       MAT_RUST, MAT_GLASS, MAT_RUBBER, MAT_ROCK, MAT_COUNT };
typedef struct { float spec, rough, relief; unsigned tint; } Material;
static const Material materials[MAT_COUNT] = {
    {0,1,0,0xffffff}, {.22f,.65f,.7f,0xdbeafa}, {.62f,.35f,1,0xb9d5ec},
    {.90f,.12f,.8f,0xe5f6ff}, {.65f,.30f,1,0xffd07d}, {.55f,.42f,1,0xffab78},
    {.08f,.94f,1.2f,0xb98765}, {.95f,.08f,.4f,0xb5edff}, {.035f,.98f,.5f,0xa0b4bf},
    {.045f,.96f,1.3f,0xa8b9b6}
};
static uint32_t sprite_material[MAXK*MAXK*N];

static int material_kind(unsigned code, unsigned bank, unsigned pen, uint32_t c)
{
    /* Verified hull families, including the turning animation. */
    if ((bank==1 || bank==2) && ((code>=0xc00 && code<0xc90) || (code>=0xf40 && code<0xf80))) {
        static const uint8_t hull[16]={0,MAT_CHROME,MAT_PAINT,MAT_PAINT,MAT_PAINT,MAT_RUBBER,
            MAT_PAINT,MAT_PAINT,MAT_PAINT,MAT_STEEL,MAT_STEEL,MAT_RUBBER,
            MAT_BRASS,MAT_BRASS,MAT_BRASS,MAT_GLASS};
        return hull[pen];
    }
    if (bank==51 && code>=0x2b00 && code<0x3000) {
        static const uint8_t wreck[16]={0,MAT_CHROME,MAT_STEEL,MAT_STEEL,MAT_RUBBER,
            MAT_COPPER,MAT_COPPER,MAT_RUST,MAT_RUST,MAT_PAINT,MAT_PAINT,MAT_RUBBER,
            MAT_BRASS,MAT_BRASS,MAT_ROCK,MAT_GLASS};
        return wreck[pen];
    }
    int r=(c>>16)&255,gr=(c>>8)&255,b=c&255;
    /* Unknown objects are deliberately not labelled glass or precious metal. */
    if (r+gr+b<100) return MAT_RUBBER;
    return abs(r-gr)<22 && abs(gr-b)<22 ? MAT_STEEL : MAT_PAINT;
}
/* Decode each source texel once. The padded height array distinguishes
 * the tile edge (use centre height) from transparent art (centre minus 28). */
static int neighbour_height(int value, int centre)
{
    return value>=0 ? value : value==-1 ? centre-28 : centre;
}

static uint32_t material_texel(unsigned code, unsigned bank, unsigned pen, int x, int y,
                               const int16_t *height)
{
    int h=*height;
    int kind=material_kind(code,bank,pen,cur.colors[(bank*16+pen)&1023]);
    int left=neighbour_height(height[-1],h),right=neighbour_height(height[1],h);
    int top=neighbour_height(height[-18],h),bottom=neighbour_height(height[18],h);
    int nx=clampi((left-right)/3,-54,54), ny=clampi((top-bottom)/3,-54,54);
    unsigned cavity=clampi(((left+right+top+bottom)/4-h)/5,0,15);
    unsigned grain=(code*131u+x*197u+y*31u);grain^=grain>>7;
    return (unsigned)kind | (uint32_t)(uint8_t)(int8_t)nx<<8 | (uint32_t)(uint8_t)(int8_t)ny<<16 | ((grain&0xf0)|cavity)<<24;
}
enum { MATERIAL_CACHE=1024 };
typedef struct {unsigned key,palette;uint32_t pixels[256];} MaterialTile;
static MaterialTile material_tiles[MATERIAL_CACHE];
static unsigned material_palette[64];
static void material_palette_prepare(void)
{
    for(unsigned bank=0;bank<64;++bank){unsigned h=2166136261u;
        for(int p=0;p<16;++p)h=(h^cur.colors[bank*16+p])*16777619u;
        material_palette[bank]=h;}
}
static uint32_t material_surface(unsigned code,unsigned bank,unsigned pen,int x,int y,int flip_x,int flip_y)
{
    (void)pen;bank&=63;code&=32767;
    unsigned key=1+code+(bank<<15),slot=(code*13+bank*107)&(MATERIAL_CACHE-1);
    MaterialTile *tile=&material_tiles[slot];
    if(tile->key!=key || tile->palette!=material_palette[bank]){
        tile->key=key;tile->palette=material_palette[bank];
        int16_t heights[18*18], palette_height[16];
        uint8_t pens[256];
        for (int i=0;i<18*18;++i) heights[i]=-2;
        palette_height[0]=-1;
        for (int i=1;i<16;++i) {
            uint32_t c=cur.colors[(bank*16+i)&1023];
            palette_height[i]=(int16_t)((((c>>16)&255)*3+((c>>8)&255)*5+(c&255)*2)/10);
        }
        for (int yy=0;yy<16;++yy) for (int xx=0;xx<16;++xx) {
            unsigned p=world_video_sprite_pen(gfx2,code,xx,yy);
            pens[yy*16+xx]=(uint8_t)p;
            heights[(yy+1)*18+xx+1]=palette_height[p];
        }
        for (int yy=0;yy<16;++yy) for (int xx=0;xx<16;++xx) {
            int at=yy*16+xx;
            tile->pixels[at]=pens[at] ? material_texel(code,bank,pens[at],xx,yy,&heights[(yy+1)*18+xx+1]) : 0;
        }
    }
    uint32_t v=tile->pixels[y*16+x];
    int nx=(int8_t)(v>>8),ny=(int8_t)(v>>16);
    if(flip_x)nx=-nx;if(flip_y)ny=-ny;
    return (v&0xff0000ffu)|(uint32_t)(uint8_t)(int8_t)nx<<8|(uint32_t)(uint8_t)(int8_t)ny<<16;
}
/* Defined below the transport cache, so shading can reuse its light samples. */
static uint32_t material_shade(uint32_t c,uint32_t surface,int x,int y);
