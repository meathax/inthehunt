// Compiled into the executable so installed games need no external shader files.
static const char *transport_shader=R"HLSL(
ByteAddressBuffer scene : register(t0);
ByteAddressBuffer picture : register(t1);
RWStructuredBuffer<float4> directField : register(u0);
RWStructuredBuffer<float4> bounceField : register(u1);
RWByteAddressBuffer result : register(u2);
static const int2 size=int2(160,120);
uint indexOf(int2 p){ return p.y*160+p.x; }
bool inside(int2 p){return all(p>=0)&&all(p<size);}
uint cell(int2 p){return scene.Load(4496+4*indexOf(p));}
float3 rgb(uint c){return float3((c>>16)&255,(c>>8)&255,c&255)/255.;}
float3 transmission(float distance){return exp2(-distance*float3(.021,.009,.004));}
// Grid traversal visits each crossed cell, including thin silhouettes.
// Starting inside a receiver permits rays to escape through its near face.
bool trace(float2 origin,float2 dir,float limit,out int2 hit,out float dist,out float2 normal){
    hit=int2(origin); int2 step=int2(dir.x>=0?1:-1,dir.y>=0?1:-1);
    float2 stride=1/max(abs(dir),.00001);
    float2 next=(step*(float2(hit)-origin)+float2(step.x>0,step.y>0))*stride;
    bool outside=(cell(hit)>>24)==0;
    dist=0;normal=0;
    [loop] for(int n=0;n<72;++n){
        if(next.x<next.y){dist=next.x;next.x+=stride.x;hit.x+=step.x;normal=float2(-step.x,0);}
        else {dist=next.y;next.y+=stride.y;hit.y+=step.y;normal=float2(0,-step.y);}
        if(dist>limit||!inside(hit))return false;
        if(cell(hit)>>24){if(outside||dist>2)return true;}else outside=true;
    }
    return false;
}
float visible(float2 p,float2 dir,float length){
    int2 hit;float d;float2 normal;
    return trace(p,dir,length,hit,d,normal)?0.:1.;
}
[numthreads(8,8,1)]
void directPass(uint3 id:SV_DispatchThreadID){
    int2 q=id.xy;if(!inside(q))return;
    float2 p=q+.5;int wl=asint(scene.Load(8));float surface=wl<0?0:wl*.5;
    uint flags=scene.Load(4);
    float sun=1;
    if(flags&2){
        sun=0;
        [unroll]for(int j=0;j<4;j++){
            float2 dir=normalize(float2(.25+j*.115,-1));
            sun+=.25*visible(p,dir,clamp((p.y-surface)/-dir.y,0,48));
        }
    }
    float3 irradiance=0;
    [loop]for(uint i=0;i<scene.Load(0);i++){
        float4 a=asfloat(scene.Load4(16+32*i));
        float3 colour=asfloat(scene.Load3(32+32*i));
        float2 delta=a.xy-p;float d2=dot(delta,delta);
        if(d2>=a.z*a.z)continue;
        float d=sqrt(d2);float fall=pow(saturate(1-d/a.z),2)/(1+d2*.012);
        if(fall*max(colour.r,max(colour.g,colour.b))<.009)continue;
        float2 dir=delta/max(d,.001);
        float vis=d<2?1:visible(p,dir,d-1);
        float wet=saturate((max(p.y,a.y)-surface)/max(.01,abs(delta.y)));
        irradiance+=colour*(fall*vis)*transmission(wet*d);
    }
    directField[indexOf(q)]=float4(irradiance,sun);
}
// Deterministic stratified paths prevent independently seeded frames from shimmering.
// Integrate incident radiance at two successive diffuse interactions.
[numthreads(8,8,1)]
void indirectPass(uint3 id:SV_DispatchThreadID){
    int2 q=id.xy;if(!inside(q))return;
    float3 sum=0;int wl=asint(scene.Load(8));
    [unroll]for(int j=0;j<6;j++){
        float angle=(j+.37)*1.04719755;float2 dir=float2(cos(angle),sin(angle));
        float2 p=q+.5;float3 throughput=1;
        [unroll]for(int b=0;b<2;b++){
            int2 hit;float d;float2 normal;
            if(!trace(p,dir,38,hit,d,normal))break;
            float2 hitPoint=p+d*dir;
            float wet=wl<0?1:saturate((max(p.y,hitPoint.y)-wl*.5)/max(.01,abs(p.y-hitPoint.y)));
            throughput*=rgb(cell(hit))*.65*transmission(d*wet);
            float4 light=directField[indexOf(hit)];
            sum+=throughput*(light.rgb+float3(.30,.39,.42)*light.a);
            p=clamp(hitPoint+normal*.05,.001,float2(size)-.001);
            float u=2*frac((j+.5)*.618034)-1;
            dir=normal*sqrt(1-u*u)+float2(-normal.y,normal.x)*u;
        }
    }
    float4 direct=directField[indexOf(q)];
    bounceField[indexOf(q)]=float4(direct.rgb+sum/6,direct.a);
}
[numthreads(8,8,1)]
void shadePass(uint3 id:SV_DispatchThreadID){
    uint k=scene.Load(12);uint2 extent=uint2(320,240)*k;
    if(any(id.xy>=extent))return;
    uint offset=(id.y*extent.x+id.x)*4,c=picture.Load(offset);float3 base=rgb(c);
    if((c>>24)!=254){result.Store(offset,c|0xff000000);return;}
    float2 uv=clamp((id.xy+.5)/(2*k)-.5,0,float2(size)-1);
    int2 a=int2(uv),b=min(a+1,size-1);float2 f=frac(uv);
    float4 l=lerp(lerp(bounceField[indexOf(a)],bounceField[indexOf(int2(b.x,a.y))],f.x),
                  lerp(bounceField[indexOf(int2(a.x,b.y))],bounceField[indexOf(b)],f.x),f.y);
    uint flags=scene.Load(4);int wl=asint(scene.Load(8));
    float wet=wl<0?1:saturate((float(id.y)/k-wl)/8);
    float3 energy=l.rgb/(1+.45*l.rgb);
    float3 colour=base*(1-wet*.29*(1-l.a));
    // Local shadowing also modulates the diffuse skylight scattered into water.
    if((flags&6)==6) colour+=wet*l.a*float3(.002,.009,.012)*exp(-max(0.,float(id.y)/k-max(wl,0))*.012);
    if(flags&1) colour+=base*energy*.78;
    if(flags&4) colour+=wet*energy*float3(.045,.065,.085);
    uint3 v=uint3(saturate(colour)*255+.5);
    result.Store(offset,0xff000000|(v.r<<16)|(v.g<<8)|v.b);
}
)HLSL";
