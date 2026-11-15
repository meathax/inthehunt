// Compiled into the executable so installed games need no external shader files.
static const char *transport_shader=R"HLSL(
ByteAddressBuffer scene : register(t0);
ByteAddressBuffer picture : register(t1);
RWStructuredBuffer<float4> directField : register(u0);
RWStructuredBuffer<float4> bounceField : register(u1);
RWByteAddressBuffer result : register(u2);
ByteAddressBuffer effects : register(t2);
RWByteAddressBuffer history : register(u3);
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
    // Four interleaved phases distribute samples across neighboring probes.
    // Fixed phases keep identical frames stable without temporal accumulation.
    float phase=((q.x&1)+2*(q.y&1)+.5)*.25;
    float sun=1;
    if(flags&(2|16)){
        sun=0;
        [unroll]for(int j=0;j<2;j++){
            float2 dir=normalize(float2(.23+(j+phase)*.20,-1));
            sun+=.5*visible(p,dir,clamp((p.y-surface)/-dir.y,0,48));
        }
    }
    float3 irradiance=0;
    uint bin=(q.y/16)*10+q.x/16;
    [loop]for(uint w=0;w<(scene.Load(0)+31)/32;++w){
      uint candidates=scene.Load(81300+20*bin+4*w);
      [loop]while(candidates){
        uint i=32*w+firstbitlow(candidates);candidates&=candidates-1;
        float4 a=asfloat(scene.Load4(16+32*i));
        float3 colour=asfloat(scene.Load3(32+32*i));
        float2 delta=a.xy-p;float d2=dot(delta,delta);
        if(d2>=a.z*a.z)continue;
        float d=sqrt(d2);float fall=pow(saturate(1-d/a.z),2)/(1+d2*.012);
        float strength=fall*max(colour.r,max(colour.g,colour.b));
        if(strength<.002)continue;
        fall*=smoothstep(.002,.011,strength);
        float2 dir=delta/max(d,.001);
        // A finite emitter produces a penumbra behind blocking scenery.
        float2 side=float2(-dir.y,dir.x);
        float offset=(phase-.5)*min(3.,a.z*.075);
        float2 target=delta+side*offset;
        float path=length(target);
        float vis=d<2?1:visible(p,target/max(path,.001),max(0.,path-1));
        float wet=saturate((max(p.y,a.y)-surface)/max(.01,abs(delta.y)));
        irradiance+=colour*(fall*vis)*transmission(wet*d);
    }
    }
    directField[indexOf(q)]=float4(irradiance,sun);
}
// Deterministic stratified paths prevent independently seeded frames from shimmering.
// Integrate incident radiance at two successive diffuse interactions.
[numthreads(8,8,1)]
void indirectPass(uint3 id:SV_DispatchThreadID){
    int2 q=id.xy;if(!inside(q))return;
    float3 sum=0;int wl=asint(scene.Load(8));
    uint quality=scene.Load(4);
    int paths=(quality&2048)?1:(quality&1024)?2:4;
    [loop]for(int j=0;j<paths;j++){
        float phase=((q.x&1)+2*(q.y&1)+.5)*.25;
        float angle=(j+phase)*(6.2831853/paths);float2 dir=float2(cos(angle),sin(angle));
        float2 p=q+.5;float3 throughput=1;
        [loop]for(int b=0;b<(paths<4?1:2);b++){
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
    bounceField[indexOf(q)]=float4(direct.rgb+sum/paths,direct.a);
}
// Reconstruct the interleaved paths once per lighting update, in probe space.
[numthreads(8,8,1)]
void resolvePass(uint3 id:SV_DispatchThreadID){
    int2 p=id.xy;if(!inside(p))return;
    uint solid=cell(p)>>24;
    float4 sum=0;float weights=0;
    [unroll]for(int y=-1;y<=1;++y)[unroll]for(int x=-1;x<=1;++x){
        int2 n=clamp(p+int2(x,y),0,size-1);
        float w=(x==0?2:1)*(y==0?2:1);
        if((cell(n)>>24)!=solid)w=0;
        sum+=bounceField[indexOf(n)]*w;
        weights+=w;
    }
    directField[indexOf(p)]=sum/weights;
}
RWStructuredBuffer<float4> volumes : register(u4);
[numthreads(8,8,1)]
void volumePass(uint3 id:SV_DispatchThreadID){
    if(any(id.xy>=uint2(80,60)))return;
    int2 q=id.xy*4+2;
    int wl=asint(scene.Load(8)),t=asint(effects.Load(4812)),scroll=asint(effects.Load(4808));
    uint at=id.y*80+id.x;
    if(wl>=0 && q.y<wl){volumes[at]=0;return;}
    float4 l=float4(0,0,0,1);
    if(scene.Load(4)&23)l=directField[indexOf(q/2)];
    else [unroll]for(int j=1;j<=4;j++){
        int2 p=int2(min(q.x+j*5,319),q.y-j*10);
        if(p.y>=0 && (cell(p/2)>>24))l.a*=.3;
    }
    // Wide, slow variations in downwelling light. Continuous phase avoids
    // the stepped diagonals caused by integer division in the shaft texture.
    float x=q.x+scroll-.32*q.y;
    float beam=.5+.28*sin(x*.026+t*.003)+.19*sin(x*.051+q.y*.006-t*.0021);
    float shaft=beam*beam*l.a*exp2(-max(0.,q.y-max(0,wl))*.009);
    uint flags=scene.Load(4);
    float3 colour=shaft*float3(3,11,15)*((flags&512)?1.:.32);
    if(flags&32)colour+=l.rgb*float3(9,10,12)*((flags&4)?.65:1);
    [loop]for(uint i=0;i<effects.Load(4816);++i){
        float3 e=asfloat(effects.Load3(4824+i*16));float2 delta=q-e.xy;
        float d2=dot(delta,delta),wet=wl<0?1:saturate((max(q.y,e.y)-wl)/max(.01,abs(delta.y)));
        colour+=e.z*900/(900+d2)*float3(18,10,3)*exp(-sqrt(d2)*wet*float3(.012,.005,.0025));
    }
    volumes[at]=float4(min(255.,colour)/255.,0);
}
// One output thread owns a subpixel, including every overlapping stamp.
int3 compositeParticles(uint2 pixel,uint scale,uint mask,float4 light,int3 colour){
    int2 native=int2(pixel/scale);
    uint node=effects.Load(4*((native.y/8)*40+native.x/8));
    [loop]while(node){
        uint at=43768+node*32;
        int4 shape=asint(effects.Load4(at));
        uint4 data=effects.Load4(at+16);
        node=data.w;
        uint kind=data.y;
        if(!(mask&(kind==0?1:kind==3?4:2)))continue;
        int2 delta=native-shape.xy;
        int3 ink=int3((data.x>>16)&255,(data.x>>8)&255,data.x&255);
        int alpha=data.x>>24;
        if(kind==3){
            int2 sub=int2(pixel)-shape.xy;
            if(all(sub>=0) && all(sub<shape.z))colour=min(255,colour+ink);
            continue;
        }
        if(kind==6){
            int r=shape.z,d=delta.x*delta.x+4*delta.y*delta.y;
            if(d>=r*r || abs(delta.y)>r/2 ||
               uint((delta.y+r/2)*(2*r+1)+delta.x+r)>=data.z)continue;
            int phase=(shape.w+27*delta.x+49*delta.y)&1023;
            alpha=alpha*(r*r-d)/(r*r)*(210+int(round(256*sin(phase*.00613592315)))/6)/256;
            ink=min(255,ink+int3(light.rgb*float3(45,35,25)));
        }else if(kind<=2){
            if(any(delta!=0))continue;
        }else{
            int rad=shape.z,r2=rad*rad+1;
            if(abs(delta.x)>rad || abs(delta.y)>rad)continue;
            // The silt stamp compresses dy/2 using integer truncation.
            int start=kind==4?delta.y*2-(delta.y<=0?1:0):delta.y;
            int end=kind==4?delta.y*2+(delta.y>=0?1:0):delta.y;
            [loop]for(int dy=max(start,-rad);dy<=min(end,rad);++dy){
                int d=delta.x*delta.x+dy*dy;
                if(d>=r2 || uint((dy+rad)*(2*rad+1)+delta.x+rad)>=data.z)continue;
                int a=alpha*(r2-d)/r2;
                int3 c=int3(100,190,210);
                if(kind==4){
                    int phase=(shape.w+delta.x*61+dy*39)&1023;
                    a=a*(192+int(round(256*sin(phase*.00613592315)))/4)/256;
                    c=int3(100,112,104);
                }else if((ink.b&1) && delta.x-dy>=rad-1){
                    c=min(255,int3(float3(80,126,150)+light.a*float3(95,88,78)+light.rgb*float3(120,62,40)));
                }else if(!(ink.b&2))continue;
                colour+=(c-colour)*a/256;
            }
            continue;
        }
        if(kind==2)ink=min(255,int3(float3(80,126,150)+light.a*float3(95,88,78)+light.rgb*float3(120,62,40)));
        colour+=(ink-colour)*alpha/256;
    }
    return colour;
}
[numthreads(8,8,1)]
void shadePass(uint3 id:SV_DispatchThreadID){
    uint k=scene.Load(12);uint2 extent=uint2(320,240)*k;
    if(any(id.xy>=extent))return;
    uint offset=(id.y*extent.x+id.x)*4,c=picture.Load(offset);float3 base=rgb(c);
    uint tag=c>>24,flags=scene.Load(4);
    uint plane=255;
    if(tag>=32 && tag<96){
        plane=250+(tag>>5);
        tag=224+(tag&31);
    }
    if(tag==255){result.Store(offset,c);if(flags&8)history.Store(offset,0);return;}
    if((flags&4096) && (tag&240)==224){
        uint row=id.y/k;
        if(effects.Load(43736+(row/32)*4)&(1u<<(row%32))){
            int3 v=int3(round(base*255));
            int lum=(v.r*3+v.g*5+v.b*2)/10;
            int cool=clamp((max(v.g,v.b)-v.r)*3,0,255);
            int water=min(255,cool*(255-lum)/200);
            int f=3+water*((flags&8192)?5:21)/255;
            base=(v+(int3(5,28,52)-v)*f/256)/255.;
        }
    }
    if(flags&64){
        int row=int(id.y/k)-asint(effects.Load(5208));
        if(row>=0 && row<asint(effects.Load(5212))){
            uint sampleMask=effects.Load(5336+(row*320+id.x/k)*4);
            if(sampleMask&(1u<<((id.y%k)*k+id.x%k))){
                int x=clamp(int(id.x)+asint(effects.Load(5216+4*row)),0,int(extent.x)-1);
                int y=2*asint(effects.Load(5208))*k-1-int(id.y);
                uint mirror=picture.Load((uint(y)*extent.x+x)*4);
                if((mirror>>24)!=255)
                    base=lerp(base,rgb(mirror),(sampleMask>>16)/256.);
            }
        }
    }
    if(flags&128){
        int3 v=int3(round(base*255));
        int lum=(v.r*3+v.g*5+v.b*2)/10;
        base=clamp(lum+(v-lum)*17/16,0,255)/255.;
    }
    float2 uv=clamp((id.xy+.5)/(2*k)-.5,0,float2(size)-1);
    int2 a=int2(uv),b=min(a+1,size-1);float2 f=frac(uv);
    float4 l=float4(0,0,0,1);
    if(flags&23)l=lerp(lerp(directField[indexOf(a)],directField[indexOf(int2(b.x,a.y))],f.x),
                  lerp(directField[indexOf(int2(a.x,b.y))],directField[indexOf(b)],f.x),f.y);
    int wl=asint(scene.Load(8));
    float wet=wl<0?1:saturate((float(id.y)/k-wl)/8);
    float3 energy=l.rgb/(1+.45*max(l.r,max(l.g,l.b)));
    float3 colour=base*(1-((flags&2)?wet*.29*(1-l.a):0));
    // Local shadowing also modulates the diffuse skylight scattered into water.
    if((flags&6)==6) colour+=wet*l.a*float3(.002,.009,.012)*exp(-max(0.,float(id.y)/k-max(wl,0))*.012);
    if(flags&1) colour+=base*energy*.78;
    if(flags&4) colour+=wet*energy*float3(.045,.065,.085);
    if((flags&(32|512)) && (tag&240)==224){
        float2 pos=clamp((float2(id.xy/k)-2)*.25,0,float2(79,59));
        uint2 v0=uint2(pos),v1=min(v0+1,uint2(79,59));float2 f=frac(pos);
        colour+=lerp(lerp(volumes[v0.y*80+v0.x].rgb,volumes[v0.y*80+v1.x].rgb,f.x),
                     lerp(volumes[v1.y*80+v0.x].rgb,volumes[v1.y*80+v1.x].rgb,f.x),f.y);
    }
    // Stochastic rounding of fractional light removes 8-bit halo contours.
    // Anchor it to arcade pixels so static light stays still at every scale.
    float rounding=.5;
    if(flags&(1|2|4|32|512)){
        uint2 pos=id.xy/k;
        uint h=pos.x*0x9e3779b9u+pos.y*0x85ebca6bu;
        h=(h^(h>>16))*0x7feb352du;
        rounding=((h^(h>>15))&1023u)/1024.+.00048828125;
    }
    uint3 v=uint3(saturate(colour)*255+rounding);
    if((tag&15)<8){
        uint3 particles=uint3(compositeParticles(id.xy,k,tag&7,l,int3(v)));
        if(any(v!=particles))plane=255;
        v=particles;
    }
    if(flags&8){
        if(!effects.Load(4804)){
            uint old=history.Load(offset);
            v=max(v,(uint3((old>>16)&255,(old>>8)&255,old&255)*7)>>4);
        }
        history.Store(offset,(v.r<<16)|(v.g<<8)|v.b);
    }
    if((flags&256) && id.y/k>=120){
        uint dim=id.y/k-120;
        v=(v*(256-dim)+uint3(2,10,22)*dim)>>8;
    }
    result.Store(offset,(plane<<24)|(v.r<<16)|(v.g<<8)|v.b);
}
)HLSL";
