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
float4 stageLight(uint group){
    return (scene.Load(4)&131072)?asfloat(scene.Load4(121692+16*group)):float4(1,1,1,1);
}
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
// Each cell packs four native terrain pixels and four sprite pixels.
uint caster(int2 pixel){
    if(any(pixel<0)||any(pixel>=size*2))return 0;
    uint bits=cell(pixel/2)>>(24+((pixel.y&1)*2+(pixel.x&1)));
    return (bits&16)?2:bits&1;
}
uint pointCaster(int2 p){
    if(any(p<0)||any(p>=2*size))return 0;
    uint i=indexOf(p/2);
    uint bits=scene.Load(102492+(i&~3u))>>(8*(i&3)+2*(p.y&1)+(p.x&1));
    return (bits&16)?2:bits&1;
}
// Compare boundary distances by cross multiplication. A corner crossing
// always advances both axes, independently of ray length or camera phase.
int pointBlockerRow(float2 receiver,float2 emitter){
    int2 start=int2(floor(receiver*16+.5)),finish=int2(floor(emitter*16+.5));
    int2 q=start>>4,end=finish>>4;
    uint nearMaterial=pointCaster(q),farMaterial=pointCaster(end);
    int2 delta=finish-start,stepDir=int2(sign(delta)),span=abs(delta);
    int2 border=16*(q+int2(stepDir.x>0,stepDir.y>0));
    int2 distance=abs(border-start)*span.yx;
    int2 stride=16*span.yx;
    int entered=-1;
    [loop]while(any(q!=end)){
        bool2 advance=bool2(q.x!=end.x && (q.y==end.y || distance.x<=distance.y),
                            q.y!=end.y && (q.x==end.x || distance.y<=distance.x));
        q+=int2(advance)*stepDir;
        distance+=int2(advance)*stride;
        if(all(q==end))break;
        uint material=pointCaster(q);
        if(nearMaterial && material==nearMaterial)continue;
        nearMaterial=0;
        if(!material){
            if(entered>=0)return entered;
        }else{
            if(material!=farMaterial)return q.y;
            if(entered<0)entered=q.y;
        }
    }
    return -1;
}
float pointVisibility(float2 receiver,float2 emitter,bool bow){
    int row=pointBlockerRow(receiver,emitter),wl=asint(scene.Load(8));
    if(wl<=0 || min(receiver.y,emitter.y)>=wl || row>=wl){
        if(!bow || abs(receiver.x-emitter.x)<12)return row<0?1:0;
        // The bow lens is vertical in side view. Fixed taps keep tiny nearby
        // silhouette details from switching the entire distant beam on/off.
        float2 rim=float2(0,3.0);
        int top=pointBlockerRow(receiver,emitter-rim);
        int bottom=pointBlockerRow(receiver,emitter+rim);
        return .25*(top<0)+.50*(row<0)+.25*(bottom<0);
    }
    // Sky fill keeps local-light shadows from airborne blockers very faint.
    // Sample a small area of the source for a stable feathered boundary.
    float2 delta=emitter-receiver;
    float2 wing=float2(-delta.y,delta.x)*min(.10,3.5/max(length(delta),1.));
    int a=pointBlockerRow(receiver,emitter-wing),b=pointBlockerRow(receiver,emitter+wing);
    float blocked=.50*(row>=0)+.25*(a>=0 && a<wl)+.25*(b>=0 && b<wl);
    return 1-.16*blocked;
}
float cone(float facing,float2 toLight,uint emitter){
    if(facing>2){
        float2 axis=float2(facing-4,asfloat(scene.Load(44+32*emitter)));
        return .18+.82*smoothstep(-.15,.80,-dot(axis,toLight));
    }
    return facing==0?1:smoothstep(.885,.975,-toLight.x*facing);
}
// One group follows one parallel shaft of sunlight through the side view.
// The first exit from a solid silhouette shadows every subsequent receiver.
groupshared uint firstShadowRow,firstSolidRow,firstIceRow,firstAirRow;
uint terrainFlags(int2 p){
    if(any(p<0)||any(p>=size*2))return false;
    uint i=indexOf(p/2);
    uint bits=scene.Load(82916+(i&~3u))>>(8*(i&3));
    return bits>>((p.y&1)*2+(p.x&1));
}
int sunOffset(int row){return (11*row+25)/50;}
bool skyReaches(int2 p){
    uint column=p.x+sunOffset(p.y);
    uint stop=(scene.Load(102116+(column&~3u))>>(8*(column&3)))&255;
    return stop==0 || p.y+1<int(stop);
}
[numthreads(256,1,1)]
void sunPass(uint3 group:SV_GroupID,uint lane:SV_GroupIndex){
    int y=int(lane),column=int(group.x);
    int2 p=int2(column-sunOffset(y),y);
    if(lane==0){firstShadowRow=240;firstSolidRow=240;firstIceRow=240;firstAirRow=240;}
    GroupMemoryBarrierWithGroupSync();
    if(y>0 && y<240 && p.x>=0 && p.x<320){
        uint previous=caster(int2(column-sunOffset(y-1),y-1));
        bool exitEdge=previous!=0 && previous!=caster(p);
        if(exitEdge && y-1<asint(scene.Load(8)))InterlockedMin(firstAirRow,lane);
        else if(exitEdge){
            InterlockedMin(firstShadowRow,lane);
            if(previous==caster(int2(column-sunOffset(y-2),y-2)) &&
               previous==caster(int2(column-sunOffset(y-3),y-3)))InterlockedMin(firstSolidRow,lane);
        }
        if((scene.Load(4)&262144) && (terrainFlags(int2(column-sunOffset(y-1),y-1))&1) && !(terrainFlags(p)&1))
            InterlockedMin(firstIceRow,lane);
    }
    GroupMemoryBarrierWithGroupSync();
    if(lane==0)directField[96000+column]=float4(firstAirRow,0,0,0);
    if(y<240 && p.x>=0 && p.x<320){
        // Forward scatter fills long shadows. Keep the contact edge sharp,
        // and retain a visible umbra all the way to the bottom.
        float visibility=1;
        if(lane>=firstIceRow && !caster(p))visibility=.72+.13*saturate((y-int(firstIceRow))/150.);
        if(lane>=firstShadowRow)visibility=min(visibility,.64+.17*saturate((y-int(firstShadowRow))/120.));
        if(lane>=firstSolidRow)visibility=min(visibility,.18+.22*saturate((y-int(firstSolidRow))/180.));
        directField[19200+y*320+p.x]=float4(0,0,0,visibility);
    }
}
[numthreads(8,8,1)]
void directPass(uint3 id:SV_DispatchThreadID){
    int2 pixel=id.xy;if(any(pixel>=2*size))return;
    int2 q=pixel/2;
    float2 origin=pixel+.5,p=origin*.5;
    uint flags=scene.Load(4);
    bool sky=!(flags&524288) && skyReaches(pixel);
    float sun=sky && (flags&(2|16|16384))?directField[19200+pixel.y*320+pixel.x].a:1;
    int wl=asint(scene.Load(8));
    if(sky && (flags&(2|16|16384)) && wl>0){
        int column=pixel.x+sunOffset(pixel.y);
        float shade=0;
        // Normalized triangular filter across seventeen adjacent sun rays.
        // It filters airborne silhouettes only, including below the interface.
        [unroll]for(int dx=-8;dx<=8;++dx){
            int c=column+dx;
            if(c>=0 && c<373){
                float gap=pixel.y-directField[96000+c].x;
                if(gap>=0)shade+=(9-abs(dx))*(1-.45*saturate(gap/145.));
            }
        }
        sun*=1-shade*(.16/81.);
    }
    if((terrainFlags(pixel)&16) && caster(pixel)!=2)sun=1;
    float3 energy=0;
    uint bin=(q.y/16)*10+q.x/16;
    [loop]for(uint w=0;w<(scene.Load(0)+31)/32;++w){
        uint bits=scene.Load(81300+20*bin+4*w);
        [loop]while(bits){
            uint i=w*32+firstbitlow(bits);bits&=bits-1;
            float4 e=asfloat(scene.Load4(16+32*i));
            bool headlamp=abs(e.w)==1;
            if(!headlamp && !(flags&(1|4|16)))continue;
            float2 delta=e.xy-p;
            float d=length(delta);
            if(d>=e.z)continue;
            float2 dir=delta/max(.001,d);
            float fall=cone(e.w,dir,i)*pow(saturate(1-d/e.z),2)/(1+d*d*(headlamp?1.73/(e.z*e.z):.0032));
            float3 emission=asfloat(scene.Load3(32+32*i));
            if(fall*max(emission.r,max(emission.g,emission.b))<.0025)continue;
            float vis=pointVisibility(origin,2*e.xy,headlamp);
            float response=1;
            if(flags&1048576){
                uint m=scene.Load(121740+4*(pixel.y*320+pixel.x));
                uint kind=m&255;
                if(kind>0 && kind<10){
                    float2 normal=float2(int(m<<16)>>24,int(m<<8)>>24)/128.;
                    float facing=saturate(.72+dot(normal,dir)*.45);
                    float rough=(kind==3 || kind==8)?.18:(kind==7 || kind==9)?.9:.55;
                    float spec=pow(saturate(.82+dot(normal,dir)*.35),lerp(24,4,rough));
                    response=facing+spec*(1-rough)*.24*stageLight(2).z;
                }
            }
            energy+=emission*fall*vis*response*((headlamp || (flags&1))?1:.10);
        }
    }
    float4 light=float4(energy,sky?sun:0);
    directField[19200+pixel.y*320+pixel.x]=light;
    if(!any(pixel&1))directField[indexOf(q)]=light;
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
    bounceField[indexOf(q)]=float4(sum/paths,direct.a);
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
    float shaft=skyReaches(q)*beam*beam*l.a*exp2(-max(0.,q.y-max(0,wl))*.009);
    uint flags=scene.Load(4);
    float4 water=stageLight(1);
    float3 colour=shaft*float3(3,11,15)*water.rgb*stageLight(2).w*((flags&512)?1.:.32);
    if(flags&32)colour+=l.rgb*float3(9,10,12)*water.rgb*((flags&4)?.65:1);
    // One extinction field is shared by downwelling and local light.
    colour*=exp2(-max(0.,q.y-max(0,wl))*.0015*water.w);
    [loop]for(uint i=0;i<effects.Load(4816);++i){
        float4 e=asfloat(effects.Load4(4824+i*16));float2 delta=q-e.xy;
        float d2=dot(delta,delta),wet=wl<0?1:saturate((max(q.y,e.y)-wl)/max(.01,abs(delta.y)));
        colour+=e.z*900/(900+d2)*(e.w>0?18*rgb(uint(e.w)):float3(18,10,3))*exp(-sqrt(d2)*wet*float3(.012,.005,.0025));
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
// Use the assembled sprite silhouette to approximate a grazing surface
// normal, so adjacent tiles of a multipart vehicle share a single contour.
float rimMask(int2 pixel,int2 extent,bool scenery){
    pixel=clamp(pixel,0,extent-1);
    uint tag=picture.Load(4*(pixel.y*extent.x+pixel.x))>>24;
    // Occluding sprites and HUD cover the scenery; never outline their cutouts.
    return ((tag&240)==160 || tag==255 || (scenery && tag>=96 && tag<128))?1.:0.;
}
float4 exposedSides(int2 p,int2 extent,int width,bool scenery){
    int2 dx=int2(width,0),dy=int2(0,width);
    return 1-float4(rimMask(p+dx,extent,scenery),rimMask(p-dx,extent,scenery),
                    rimMask(p+dy,extent,scenery),rimMask(p-dy,extent,scenery));
}
float facingLight(float4 sides,float2 dir){
    return saturate(dot(sides,max(0,float4(dir.x,-dir.x,dir.y,-dir.y))));
}
float3 rimLight(int2 pixel,int k,int2 extent,uint flags,bool scenery){
    float4 sides=exposedSides(pixel,extent,k,scenery);
    // A softer second texel rolls the highlight back into the hull. Scenery
    // keeps a narrow crest so that large cliffs never compete with the player.
    if(!scenery)sides=max(sides,.42*exposedSides(pixel,extent,2*k,false));
    if(!any(sides))return 0;
    float2 p=clamp((pixel+.5)/(2.*k),.001,float2(size)-.001);
    float surface=max(0,asint(scene.Load(8))*.5);
    float depth=max(0,p.y-surface);
    float2 sunDir=normalize(float2(.22,-1));
    float facing=facingLight(sides,sunDir);

    float3 incident=0;
    if(facing>0){
        int2 native=min(pixel/k,int2(319,239));
        float sun=directField[19200+native.y*320+native.x].a;
        float immersion=asint(scene.Load(8))<0?1:smoothstep(0,6,depth);
        float3 sunlight=lerp(float3(1.36,1.16,.92),float3(.98,1.20,1.38),immersion);
        incident+=sunlight*transmission(depth*.46)*sun*facing;
    }
    uint2 tile=uint2(p)/16;
    uint bin=tile.y*10+tile.x;
    [loop] for(uint w=0;w<(scene.Load(0)+31)/32;++w){
        uint bits=scene.Load(81300+20*bin+4*w);
        [loop] while(bits){
            uint i=w*32+firstbitlow(bits);bits&=bits-1;
            float4 e=asfloat(scene.Load4(16+32*i));
            float2 delta=e.xy-p;float d2=dot(delta,delta);
            if(d2>=e.z*e.z)continue;
            float d=sqrt(max(.0001,d2));float2 dir=delta/d;
            float strength=cone(e.w,dir,i)*facingLight(sides,dir)*pow(saturate(1-d/e.z),2)/(1+(abs(e.w)==1?1.73/(e.z*e.z):.0032)*d2);
            if(strength<.002)continue;
            float wet=saturate((max(p.y,e.y)-surface)/max(.01,abs(delta.y)));
            float3 colour=asfloat(scene.Load3(32+32*i));
            incident+=colour*transmission(wet*d)*strength*pointVisibility(float2(pixel/k)+.5,2*e.xy,abs(e.w)==1)*2.8;
        }
    }
    return incident*(scenery?.62:1.)/(1+.68*max(incident.r,max(incident.g,incident.b)));
}
[numthreads(8,8,1)]
void shadePass(uint3 id:SV_DispatchThreadID){
    uint k=scene.Load(12);uint2 extent=uint2(320,240)*k;
    if(any(id.xy>=extent))return;
    uint offset=(id.y*extent.x+id.x)*4,c=picture.Load(offset);float3 base=rgb(c);
    uint tag=c>>24,flags=scene.Load(4);
    bool terrainRim=tag>=96 && tag<128;
    uint plane=255;
    if(terrainRim){plane=251;tag=224+(tag&31);}
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
        if(flags&131072)base=saturate((lum+(v-lum)*asfloat(scene.Load(82912)))/255.);
        else base=clamp(lum+(v-lum)*17/16,0,255)/255.;
    }
    int2 native=id.xy/k;
    float4 l=float4(0,0,0,1);
    if(flags&(23|32768|16384))l=directField[19200+native.y*320+native.x];
    int wl=asint(scene.Load(8));
    float wet=wl<0?1:saturate((float(native.y)-wl)/8);
    float depth=wet*saturate((native.y-(wl==-1?0:wl))/195.);
    float3 energy=l.rgb/(1+.90*max(l.r,max(l.g,l.b)));
    float3 colour=base;
    if(flags&65536)colour*=lerp(1,(flags&131072)?asfloat(scene.Load3(82900)):float3(.53,.65,.79),depth);
    bool surface=caster(native)!=0;
    if((flags&2) && !(flags&524288) && skyReaches(native)){
        float3 occluded=surface?float3(.49,.55,.72):float3(.87,.91,.95);
        // Open water shows suspended light; the sky never receives a projected silhouette.
        float3 deepTint=(flags&131072)?asfloat(scene.Load3(82900)):float3(.53,.65,.79);
        float deepContrast=dot(deepTint,float3(.25,.50,.25));
        float receive=(surface?1:wet)*lerp(1,deepContrast,depth);
        colour*=lerp(float3(1.02,1.01,1),occluded,(1-l.a)*receive*stageLight(2).y);
    }
    if((flags&131072) && (flags&65536)){
        float4 sunStyle=stageLight(0),finish=stageLight(2);
        float enclosure=surface?1:depth;
        colour*=lerp(float3(1,1,1),sunStyle.rgb*sunStyle.w,wet*.65);
        // Smooth black toe applied to ambient only, before lamps and scattering.
        float peak=max(colour.r,max(colour.g,colour.b));
        colour*=peak/(peak+finish.x*enclosure+.00001);
    }
    if((flags&131072) && (flags&1048576) && (flags&262144)){
        // Thin distant ice receives blue transmitted daylight. Foreground
        // terrain and hulls keep their own depth and occlusion.
        uint ice=terrainFlags(native);
        float edge=(ice&1) && !surface ? .012*l.a : 0;
        colour+=edge*float3(.30,.72,1)*wet;
    }
    float3 ambient=colour;
    if(flags&(1|32768)){
        // The bow beam is seen in profile. Most light goes onto the hull
        // or cliff it strikes; suspended particles reveal its path in water.
        float3 response=pointCaster(native)!=0?.20+.98*base:.105+.49*base;
        colour+=response*energy;
    }
    if(flags&1)colour+=.36*base*directField[indexOf(native/2)].rgb;
    if(flags&4)colour+=wet*energy*float3(.042,.049,.056);
    if((flags&(32|512)) && (tag&240)==224){
        float2 pos=clamp((float2(id.xy/k)-2)*.25,0,float2(79,59));
        uint2 v0=uint2(pos),v1=min(v0+1,uint2(79,59));float2 f=frac(pos);
        colour+=.52*lerp(lerp(volumes[v0.y*80+v0.x].rgb,volumes[v0.y*80+v1.x].rgb,f.x),
                     lerp(volumes[v1.y*80+v0.x].rgb,volumes[v1.y*80+v1.x].rgb,f.x),f.y);
    }
    if((flags&16384) && ((tag&240)==160 || terrainRim)){
        float3 rim=rimLight(id.xy,k,extent,flags,terrainRim);
        colour+=rim*(1-saturate(colour))*(.82+.18*sqrt(base));
    }
    // Compress the combined light against the remaining highlight headroom.
    float3 added=max(0,colour-ambient);
    float3 load=added/max(.001,1-ambient);
    colour=ambient+added/(1+max(load.r,max(load.g,load.b)));
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
            v=max(v,(uint3((old>>16)&255,(old>>8)&255,old&255)*4)>>4);
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
