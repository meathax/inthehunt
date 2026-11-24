// Radiance cascades for the light transport: a long-range, noise-free
// replacement for the sampled two-bounce indirect pass.
//
// The terrain/sprite occupancy is the scene (one cell per 2x2 arcade pixels).
// A lit surface re-emits its albedo times the light it received. Cascade i has
// one probe per 2^i base cells and 4^(i+1) rays per probe; each ray covers a
// distance band that grows fourfold per cascade, so the first cascade resolves
// contact detail and the last one reaches across the screen at the same total
// ray count per level. Cascades are merged from the top down (each ray adds the
// four matching rays of the next cascade, bilinearly) and the mean of the four
// rays of the base cascade is the radiance arriving at a cell: the same
// quantity the old indirect pass sampled, now with every direction and no
// range limit. Water absorbs each band analytically, and the previous update's
// result is re-emitted for further bounces.
//
// The text is inserted into the transport shader just before shadePass, so it
// shares its buffers and helpers (scene, cell, directField, bounceField...).
static const char *rc_shader=R"HLSL(
RWStructuredBuffer<float4> rcUpper : register(u6);  // merged cascade level+1
RWStructuredBuffer<float4> rcLower : register(u7);  // cascade being built
RWStructuredBuffer<float> rcDist : register(u8);    // cells to the nearest solid cell
RWStructuredBuffer<float4> rcNative : register(u9); // bounce light per arcade pixel
cbuffer rcConstants : register(b0){uint rcLevel;uint rcLevels;uint rcShift;uint rcMode;};
static const uint RC_FLAG=1u<<23;
static const float RC_FEEDBACK=.5;                  // share of last update re-emitted
static const float3 RC_SKY=float3(.30,.39,.42)*.10; // faint daylight entering from above
uint rcSpacing(uint level){return 1u<<(rcShift+level);}
int2 rcProbes(uint level){int d=int(rcSpacing(level));return int2((size.x+d-1)/d,(size.y+d-1)/d);}
uint rcRays(uint level){return 4u<<(2*level);}
float rcStart(uint level){return (float(1u<<(2*level))-1)/3*float(1u<<rcShift);}
float rcEnd(uint level){return (float(1u<<(2*level+2))-1)/3*float(1u<<rcShift);}
uint rcIndex(uint level,int2 probe,uint ray){int2 n=rcProbes(level);return (ray*uint(n.y)+uint(probe.y))*uint(n.x)+uint(probe.x);}
bool solidCell(int2 c){return inside(c)&&(cell(c)>>24)!=0;}
int2 probeCell(int2 probe,uint level){
    return int2(min((float2(probe)+.5)*float(rcSpacing(level)),float2(size)-1.));
}

// Distance to the nearest solid cell by jump flooding, limited to a 31-cell
// window (open water beyond reads as 32): seeds ping-pong through the cascade
// buffers, which are not in use yet. Jumps stay conservative: see rcMarch.
uint rcPack(int2 c){return uint(c.x)|(uint(c.y)<<16);}
[numthreads(8,8,1)]
void rcJfaInit(uint3 id:SV_DispatchThreadID){
    int2 c=int2(id.xy);if(!inside(c))return;
    rcLower[indexOf(c)]=float4(asfloat((cell(c)>>24)?rcPack(c):0xffffffffu),0,0,0);
}
[numthreads(8,8,1)]
void rcJfaStep(uint3 id:SV_DispatchThreadID){
    int2 c=int2(id.xy);if(!inside(c))return;
    int step=int(rcLevel);
    uint best=0xffffffffu;float bestDist=1e9;
    [unroll]for(int j=0;j<9;++j){
        int2 n=c+int2(j%3-1,j/3-1)*step;
        if(!inside(n))continue;
        uint seed=asuint(rcUpper[indexOf(n)].x);
        if(seed==0xffffffffu)continue;
        float2 delta=float2(int2(seed&0xffffu,seed>>16)-c);
        float d=dot(delta,delta);
        if(d<bestDist){bestDist=d;best=seed;}
    }
    rcLower[indexOf(c)]=float4(asfloat(best),0,0,0);
}
[numthreads(8,8,1)]
void rcJfaFinish(uint3 id:SV_DispatchThreadID){
    int2 c=int2(id.xy);if(!inside(c))return;
    uint seed=asuint(rcUpper[indexOf(c)].x);
    float d=32;
    if(seed!=0xffffffffu){float2 delta=float2(int2(seed&0xffffu,seed>>16)-c);d=min(32.,length(delta));}
    rcDist[indexOf(c)]=d;
}

float rcWet(float y0,float y1){
    int wl=asint(scene.Load(8));
    return wl<0?1.:saturate((max(y0,y1)-wl*.5)/max(.01,abs(y0-y1)));
}
// Light leaving a lit surface cell: what it received, times its colour. The
// last update's bounce light is included, so repeated updates add more bounces.
float3 rcEmission(int2 c){
    float4 l=directField[indexOf(c)];
    float3 prev=(rcMode&1)?bounceField[indexOf(c)].rgb:float3(0,0,0);
    return rgb(cell(c))*.65*(l.rgb+float3(.30,.39,.42)*l.a+prev*RC_FEEDBACK);
}
// 0 reached the end of the band, 1 hit a surface, 2 left through the top, 3 left elsewhere.
// Sphere tracing crosses open water; the last cells before a surface are visited
// one by one so thin silhouettes are never skipped. A ray starting inside rock
// may pass through at most two units of it.
int rcMarch(float2 o,float2 d,float t0,float t1,out int2 hit,out float tHit){
    hit=int2(0,0);tHit=t1;
    float2 ad=max(abs(d),1e-5);
    float t=t0+.001;
    bool outside=false;
    [loop]for(int n=0;n<192;++n){
        if(t>=t1)return 0;
        float2 p=o+d*t;
        int2 c=int2(floor(p));
        if(!inside(c)){tHit=t;hit=c;return p.y<0?2:3;}
        if(cell(c)>>24){
            if(outside||t>2){hit=c;tHit=t;return 1;}
        }else{
            outside=true;
            float jump=rcDist[indexOf(c)]-1.5;
            if(jump>=1){t+=jump;continue;}
        }
        float2 f=p-float2(c);
        float2 gap=float2(d.x>0?1-f.x:f.x,d.y>0?1-f.y:f.y)/ad;
        t+=min(gap.x,gap.y)+.001;
    }
    return 0;
}
// Mean of the four child rays of the next cascade, interpolated between the four
// nearest probes. Probes on the other side of a surface are left out.
float3 rcMerge(int2 probe,uint ray){
    if(rcLevel+1>=rcLevels)return float3(0,0,0);
    uint up=rcLevel+1;
    int2 nu=rcProbes(up);
    int2 base=(probe-1)>>1;
    float2 f=float2((probe.x&1)?.25:.75,(probe.y&1)?.25:.75);
    bool mine=solidCell(probeCell(probe,rcLevel));
    float3 sum=float3(0,0,0),plain=float3(0,0,0);float weights=0;
    [unroll]for(int j=0;j<4;++j){
        int2 u=clamp(base+int2(j&1,j>>1),int2(0,0),nu-1);
        float w=((j&1)?f.x:1-f.x)*((j>>1)?f.y:1-f.y);
        float3 s=float3(0,0,0);
        [unroll]for(uint c=0;c<4;++c)s+=rcUpper[rcIndex(up,u,ray*4+c)].rgb;
        s*=.25;
        plain+=s*w;
        float same=(solidCell(probeCell(u,up))==mine)?1.:0.;   // masked by arithmetic: a branch here
        sum+=s*(w*same);weights+=w*same;                       // compiled to NaN output
    }
    // Branch, don't select: the unused quotient is 0/0 when no probe qualifies.
    float3 merged=plain;
    if(weights>.001)merged=sum/weights;
    return merged;
}
[numthreads(8,8,1)]
void rcCascadePass(uint3 id:SV_DispatchThreadID){
    int2 n=rcProbes(rcLevel);
    if(any(int2(id.xy)>=n))return;
    uint ray=id.z,rays=rcRays(rcLevel);
    int2 probe=int2(id.xy);
    float2 o=min((float2(probe)+.5)*float(rcSpacing(rcLevel)),float2(size)-.01);
    float angle=(ray+.5)*(6.2831853/rays);
    float2 dir=float2(cos(angle),sin(angle));
    float t0=rcStart(rcLevel),t1=rcEnd(rcLevel);
    int2 hit;float th;
    int code=rcMarch(o,dir,t0,t1,hit,th);
    float3 radiance=float3(0,0,0);
    float2 begin=o+dir*t0;
    if(code==1){
        float wet=rcWet(begin.y,o.y+dir.y*th);
        radiance=rcEmission(hit)*transmission((th-t0)*wet);
    }else if(code==2){
        float te=max(t0,-o.y/dir.y);
        float sky=skyReaches(int2(int((o.x+dir.x*te)*2),0))?1.:.15;
        radiance=RC_SKY*sky*transmission((te-t0)*rcWet(begin.y,0));
    }else if(code==0){
        float wet=rcWet(begin.y,o.y+dir.y*t1);
        radiance=transmission((t1-t0)*wet)*rcMerge(probe,ray);
    }
    rcLower[rcIndex(rcLevel,probe,ray)]=float4(radiance,1);
}
// Radiance arriving at each cell: the mean over the base cascade's rays.
[numthreads(8,8,1)]
void rcIntegratePass(uint3 id:SV_DispatchThreadID){
    int2 c=int2(id.xy);if(!inside(c))return;
    int2 n=rcProbes(0);
    float d=float(rcSpacing(0));
    float2 g=(float2(c)+.5)/d-.5;
    int2 base=int2(floor(g));
    float2 f=g-float2(base);
    bool mine=solidCell(c);
    float3 sum=float3(0,0,0),plain=float3(0,0,0);float weights=0;
    [unroll]for(int j=0;j<4;++j){
        int2 u=clamp(base+int2(j&1,j>>1),int2(0,0),n-1);
        float w=((j&1)?f.x:1-f.x)*((j>>1)?f.y:1-f.y);
        if(w<=0)continue;
        float3 s=float3(0,0,0);
        [unroll]for(uint r=0;r<4;++r)s+=rcUpper[rcIndex(0,u,r)].rgb;
        s*=.25;
        plain+=s*w;
        float same=(solidCell(probeCell(u,0))==mine)?1.:0.;
        sum+=s*(w*same);weights+=w*same;
    }
    float3 irradiance=plain;
    if(weights>.001)irradiance=sum/weights;
    bounceField[indexOf(c)]=float4(irradiance,directField[indexOf(c)].a);
}
// Bounce light needs no reconstruction; only the sun visibility is smoothed.
[numthreads(8,8,1)]
void rcResolvePass(uint3 id:SV_DispatchThreadID){
    int2 p=int2(id.xy);if(!inside(p))return;
    uint solid=cell(p)>>24;
    float alpha=0,weights=0;
    [unroll]for(int y=-1;y<=1;++y)[unroll]for(int x=-1;x<=1;++x){
        int2 q=clamp(p+int2(x,y),int2(0,0),size-1);
        float w=(x==0?2:1)*(y==0?2:1);
        if((cell(q)>>24)!=solid)w=0;
        alpha+=bounceField[indexOf(q)].a*w;
        weights+=w;
    }
    directField[indexOf(p)]=float4(bounceField[indexOf(p)].rgb,alpha/weights);
}
// Interpolate the cell field to arcade pixels once per update, so the shading
// pass reads one value per pixel and edges stay on their own side of a surface.
[numthreads(8,8,1)]
void rcUpsamplePass(uint3 id:SV_DispatchThreadID){
    int2 px=int2(id.xy);
    if(px.x>=rasterWidth||px.y>=240)return;
    float2 g=(float2(px)+.5)*.5-.5;
    int2 base=int2(floor(g));
    float2 f=g-float2(base);
    bool mine=solidCell(px/2);
    float4 sum=float4(0,0,0,0),plain=float4(0,0,0,0);float weights=0;
    [unroll]for(int j=0;j<4;++j){
        int2 u=clamp(base+int2(j&1,j>>1),int2(0,0),size-1);
        float w=((j&1)?f.x:1-f.x)*((j>>1)?f.y:1-f.y);
        float4 s=directField[indexOf(u)];
        plain+=s*w;
        float same=(solidCell(u)==mine)?1.:0.;
        sum+=s*(w*same);weights+=w*same;
    }
    float4 value=plain;
    if(weights>.001)value=sum/weights;
    rcNative[px.y*rasterWidth+px.x]=value;
}
float3 bounceAt(int2 native){
    if(scene.Load(4)&RC_FLAG)return rcNative[native.y*rasterWidth+native.x].rgb;
    return directField[indexOf(native/2)].rgb;
}
)HLSL";
