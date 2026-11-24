#include <algorithm>
#include "world_gpu_transport_shader.h"
static_assert(sizeof(WorldEffectNode)==32);

// Close single-pixel stipple holes in each material independently. Four
// native pixels and both materials are processed together in each byte.
static void pack_point_coverage(uint8_t *out,const uint32_t *cells,int cols){
    for(int y=0;y<120;++y)for(int x=0;x<cols;++x){
        int i=y*cols+x;
        unsigned c=cells[i]>>24;
        unsigned l=x?cells[i-1]>>24:0,r=x+1<cols?cells[i+1]>>24:0;
        unsigned t=y?cells[i-cols]>>24:0,b=y<119?cells[i+cols]>>24:0;
        unsigned left=((c&0x55)<<1)|((l&0xaa)>>1);
        unsigned right=((c&0xaa)>>1)|((r&0x55)<<1);
        unsigned up=((c&0x33)<<2)|((t&0xcc)>>2);
        unsigned down=((c&0xcc)>>2)|((b&0x33)<<2);
        out[i]=(uint8_t)(c|(left&right&up&down));
    }
}

struct GpuTransport {
    Com<ID3D12RootSignature> root;
    Com<ID3D12PipelineState> sun, direct, indirect, shade, volume, resolve;
    Com<ID3D12Resource> effect_upload[FRAMES], history, volume_field, effect_device, input_device;
    Com<ID3D12Resource> scene_upload[FRAMES], scene, direct_field, bounce_field, result, vis_cache;
    bool ready=false, have_field=false, have_inputs=false;
    uint32_t revision=0, flags=0;
    int scale=0;
    unsigned width=0;

    Com<ID3D12QueryHeap> time_queries;
    Com<ID3D12Resource> time_readback;
    UINT64 frequency=0;
    unsigned sample_counter=0, slow_samples=0, fast_samples=0;
    bool early_sample[FRAMES]={}, split_sample[FRAMES]={};
    bool measured[FRAMES]={}, measured_medium[FRAMES]={}, budget_mode=false, log_times=false;
    // Developer profile (WORLD_GPU_PASSES): one timestamp after every pass.
    enum { PASS_SLOTS=16 };
    Com<ID3D12QueryHeap> pass_queries;
    Com<ID3D12Resource> pass_readback;
    const char *pass_label[FRAMES][PASS_SLOTS]={};
    unsigned pass_count[FRAMES]={};
    bool pass_resolved[FRAMES]={};
    double pass_sum[8]={}; unsigned pass_n[8]={}, pass_frames=0;
    bool force_pass_profile=false, quiet_passes=false;
    const char *source=transport_shader;   /* benchmark variants may replace it */
    void pass_mark(ID3D12GraphicsCommandList *cmd,UINT f,const char *label){
        if(!pass_queries || pass_count[f]>=PASS_SLOTS)return;
        pass_label[f][pass_count[f]]=label;
        cmd->EndQuery(pass_queries.get(),D3D12_QUERY_TYPE_TIMESTAMP,f*PASS_SLOTS+pass_count[f]++);
    }
    void pass_resolve(ID3D12GraphicsCommandList *cmd,UINT f){
        if(!pass_queries || !pass_count[f])return;
        cmd->ResolveQueryData(pass_queries.get(),D3D12_QUERY_TYPE_TIMESTAMP,f*PASS_SLOTS,pass_count[f],
                              pass_readback.get(),f*PASS_SLOTS*8);
        pass_resolved[f]=true;
    }
    void pass_read(UINT f){
        if(!pass_queries || !pass_resolved[f])return;
        static const char *const names[8]={"sun","direct","indirect","resolve","upload","volume","shade","copy"};
        D3D12_RANGE bytes={f*PASS_SLOTS*8u,(f+1)*PASS_SLOTS*8u};
        void *mapped=nullptr;
        if(SUCCEEDED(pass_readback->Map(0,&bytes,&mapped))){
            const UINT64 *t=static_cast<const UINT64*>(mapped)+f*PASS_SLOTS;
            for(unsigned i=1;i<pass_count[f];++i){
                if(!strncmp(pass_label[f][i],"begin",5))continue;
                for(int n=0;n<8;++n)if(!strcmp(pass_label[f][i],names[n])){
                    pass_sum[n]+=(t[i]-t[i-1])*1000.0/frequency;++pass_n[n];}
            }
            D3D12_RANGE none={0,0};pass_readback->Unmap(0,&none);
        }
        pass_resolved[f]=false;pass_count[f]=0;
        if(!quiet_passes && ++pass_frames%240==0){
            fprintf(stderr,"GPU passes (ms per run, runs)");
            for(int n=0;n<8;++n)fprintf(stderr," %s %.4f %u",names[n],pass_n[n]?pass_sum[n]/pass_n[n]:0.,pass_n[n]);
            fputc(10,stderr);
            for(int n=0;n<8;++n){pass_sum[n]=0;pass_n[n]=0;}
        }
    }

    // The caller already waited on this frame slot's fence.
    void read_time(UINT frame){
        if(!measured[frame])return;
        measured[frame]=false;
        D3D12_RANGE bytes={frame*32u,(frame+1)*32u};
        void *mapped=nullptr;
        if(FAILED(time_readback->Map(0,&bytes,&mapped)))return;
        const UINT64 *times=static_cast<const UINT64*>(mapped)+4*frame;
        UINT64 ticks=times[3]-times[2];
        if(split_sample[frame])ticks+=times[1]-times[0];
        double ms=ticks*1000.0/frequency;
        D3D12_RANGE no_writes={0,0};
        time_readback->Unmap(0,&no_writes);
        if(log_times)fprintf(stderr,"GPU effects sample %.3f ms\n",ms);
        if(!measured_medium[frame])return;
        slow_samples=ms>4.0 ? slow_samples+1 : 0;
        fast_samples=ms<1.5 ? fast_samples+1 : 0;
        if(slow_samples>=3)budget_mode=true;
        if(fast_samples>=24)budget_mode=false;
    }

    bool init(ID3D12Device *dev, ID3D12CommandQueue *queue=nullptr)
    {
        if(queue && SUCCEEDED(queue->GetTimestampFrequency(&frequency)) && frequency){
            D3D12_QUERY_HEAP_DESC q={D3D12_QUERY_HEAP_TYPE_TIMESTAMP,4*FRAMES,0};
            if(SUCCEEDED(dev->CreateQueryHeap(&q,IID_PPV_ARGS(time_queries.put()))))
                buffer(dev,D3D12_HEAP_TYPE_READBACK,FRAMES*32,D3D12_RESOURCE_STATE_COPY_DEST,time_readback.put());
            log_times=getenv("WORLD_PACING_LOG")!=nullptr;
            if(getenv("WORLD_GPU_PASSES") || force_pass_profile){
                D3D12_QUERY_HEAP_DESC passes={D3D12_QUERY_HEAP_TYPE_TIMESTAMP,(UINT)PASS_SLOTS*(UINT)FRAMES,0};
                if(FAILED(dev->CreateQueryHeap(&passes,IID_PPV_ARGS(pass_queries.put()))) ||
                   FAILED(buffer(dev,D3D12_HEAP_TYPE_READBACK,(UINT)FRAMES*(UINT)PASS_SLOTS*8,D3D12_RESOURCE_STATE_COPY_DEST,pass_readback.put())))
                    pass_queries.reset();
            }
        }
        D3D12_ROOT_PARAMETER params[9]={};
        for(UINT i=0;i<5;i++){
            params[i].ParameterType=i<2?D3D12_ROOT_PARAMETER_TYPE_SRV:D3D12_ROOT_PARAMETER_TYPE_UAV;
            params[i].Descriptor.ShaderRegister=i<2?i:i-2;
        }
        D3D12_ROOT_SIGNATURE_DESC desc={};
        params[5].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;params[5].Descriptor.ShaderRegister=2;
        params[6].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;params[6].Descriptor.ShaderRegister=3;
        params[7].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;params[7].Descriptor.ShaderRegister=4;
        params[8].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;params[8].Descriptor.ShaderRegister=5;
        desc.NumParameters=9; desc.pParameters=params;
        Com<ID3DBlob> signature, errors;
        if(FAILED(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,signature.put(),errors.put())) ||
           FAILED(dev->CreateRootSignature(0,signature->GetBufferPointer(),signature->GetBufferSize(),IID_PPV_ARGS(root.put()))))return false;
        const char *names[]={"sunPass","directPass","indirectPass","shadePass","volumePass","resolvePass"};
        Com<ID3D12PipelineState> *states[]={&sun,&direct,&indirect,&shade,&volume,&resolve};
        // Bind raw-buffer shader addresses to the native upload layout.
        struct ShaderOffset { const char *name; size_t value; };
#define SCENE_OFFSET(field) {"SCENE_" #field, offsetof(WorldLightScene,field)}
#define EFFECT_OFFSET(field) {"EFFECT_" #field, offsetof(WorldEffectBatch,field)}
        const ShaderOffset offsets[] = {
            {"MAX_RASTER_WIDTH",WORLD_VIEW_MAX_WIDTH},
            SCENE_OFFSET(width), SCENE_OFFSET(light_bins), SCENE_OFFSET(water_grade),
            SCENE_OFFSET(ice_coverage), SCENE_OFFSET(sky_stop), SCENE_OFFSET(point_coverage),
            SCENE_OFFSET(stage_light), SCENE_OFFSET(surface), SCENE_OFFSET(strengths),
            EFFECT_OFFSET(nodes), EFFECT_OFFSET(ocean_rows), EFFECT_OFFSET(mirror),
            EFFECT_OFFSET(ripple), EFFECT_OFFSET(reflect_rows), EFFECT_OFFSET(reflect_row),
            EFFECT_OFFSET(volumes), EFFECT_OFFSET(playfield_scroll), EFFECT_OFFSET(volume_count),
            EFFECT_OFFSET(volume_time), EFFECT_OFFSET(volume_scroll), EFFECT_OFFSET(history_reset),
        };
#undef SCENE_OFFSET
#undef EFFECT_OFFSET
        constexpr size_t count=sizeof offsets/sizeof *offsets;
        char values[count][24];
        D3D_SHADER_MACRO defines[count+1]={};
        for(size_t i=0;i<count;++i){
            snprintf(values[i],sizeof values[i],"%zu",offsets[i].value);
            defines[i]={offsets[i].name,values[i]};
        }
        for(int i=0;i<6;i++){
            Com<ID3DBlob> code;
            if(FAILED(D3DCompile(source,strlen(source),"water transport",defines,nullptr,names[i],"cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,code.put(),errors.put()))){
                if(errors) fprintf(stderr,"%s\n",(const char*)errors->GetBufferPointer());
                return false;
            }
            D3D12_COMPUTE_PIPELINE_STATE_DESC state={};
            state.pRootSignature=root.get();
            state.CS={code->GetBufferPointer(),code->GetBufferSize()};
            if(FAILED(dev->CreateComputePipelineState(&state,IID_PPV_ARGS(states[i]->put()))))return false;
        }
        for(auto &u:scene_upload)
            if(FAILED(buffer(dev,D3D12_HEAP_TYPE_UPLOAD,offsetof(WorldLightScene,effects),D3D12_RESOURCE_STATE_GENERIC_READ,u.put())))return false;
        if(FAILED(buffer(dev,D3D12_HEAP_TYPE_DEFAULT,offsetof(WorldLightScene,effects),D3D12_RESOURCE_STATE_COPY_DEST,scene.put())))return false;
        constexpr UINT maxPitch=((WORLD_VIEW_MAX_WIDTH*4*4+255)/256)*256;
        constexpr UINT maxFrameBytes=maxPitch*H*4;
        for(auto &u:effect_upload)
            if(FAILED(buffer(dev,D3D12_HEAP_TYPE_UPLOAD,sizeof(WorldEffectBatch),D3D12_RESOURCE_STATE_GENERIC_READ,u.put())))return false;
        if(FAILED(buffer(dev,D3D12_HEAP_TYPE_DEFAULT,sizeof(WorldEffectBatch),D3D12_RESOURCE_STATE_COPY_DEST,effect_device.put())) ||
           FAILED(buffer(dev,D3D12_HEAP_TYPE_DEFAULT,maxFrameBytes,D3D12_RESOURCE_STATE_COPY_DEST,input_device.put())))return false;
        auto storage=[&](Com<ID3D12Resource>& resource,UINT bytes){
            D3D12_RESOURCE_DESC d=scene->GetDesc();
            d.Width=bytes;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            D3D12_HEAP_PROPERTIES heap={};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
            return SUCCEEDED(dev->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(resource.put())));
        };
        ready=storage(volume_field,WORLD_VIEW_MAX_WIDTH/4*60*16) && storage(history,maxFrameBytes) && storage(direct_field,(WORLD_PROBES+WORLD_VIEW_MAX_WIDTH*240+WORLD_SKY_COLUMNS)*16) && storage(bounce_field,WORLD_PROBES*16) && storage(result,maxFrameBytes)
            && storage(vis_cache,WORLD_VIEW_MAX_WIDTH*H*16);
        return ready;
    }

    // Medium, or High with automatic quality, lightens the lighting while
    // measured GPU time stays over budget. Early and frame recording agree.
    static bool adaptive(const WorldLightScene &data){
        return (data.flags&(1024|2097152))!=0 && (data.flags&WORLD_LIGHT_FIELD_MASK)!=0;
    }
    uint32_t dispatch_flags(const WorldLightScene &data) const {
        return data.flags | (adaptive(data) && budget_mode ? 2048u : 0u);
    }
    bool needs_light(const WorldLightScene &data) const {
        return !have_field || width!=data.width || revision!=data.revision || flags!=dispatch_flags(data) || scale!=data.scale;
    }
    bool prepare(ID3D12GraphicsCommandList *cmd,ID3D12Resource *upload,int k,
                 const WorldLightScene &data,uint32_t dispatch_flags,UINT frame=0){
        bool changed=!have_field || width!=data.width || revision!=data.revision || flags!=dispatch_flags;
        auto transition=[&](ID3D12Resource *r,D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to){
            auto b=barrier(r,from,to);cmd->ResourceBarrier(1,&b);
        };
        int cols=data.width>W && data.width<=WORLD_VIEW_MAX_WIDTH ? (int)data.width : W;
        if (changed || scale!=k) {
            void *map=nullptr;
            D3D12_RANGE empty={0,0};
            if(FAILED(upload->Map(0,&empty,&map)))return false;
            memcpy(map,&data,offsetof(WorldLightScene,effects));
            auto *packed=static_cast<WorldLightScene*>(map);
            packed->flags=dispatch_flags;
            packed->width=cols;
            pack_point_coverage(packed->point_coverage,data.cells,cols/2);
            uint32_t bins[((WORLD_VIEW_MAX_WIDTH+31)/32)*8][5]={};
            for(unsigned i=0;i<data.count && i<WORLD_EMITTERS;++i){
                const float *e=data.emitters[i];
                // radius is conservative; the shader retains the per-probe falloff test.
                int xmin=std::max(0,(int)floorf((e[0]-e[2])/16));
                int xmax=std::min((cols-1)/32,(int)floorf((e[0]+e[2])/16));
                int ymin=std::max(0,(int)floorf((e[1]-e[2])/16));
                int ymax=std::min(7,(int)floorf((e[1]+e[2])/16));
                for(int y=ymin;y<=ymax;++y)for(int x=xmin;x<=xmax;++x)
                    bins[y*((cols+31)/32)+x][i/32]|=1u<<(i%32);
            }
            memcpy(packed->light_bins,bins,sizeof bins);
            upload->Unmap(0,nullptr);
            if(have_field)
                transition(scene.get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
            cmd->CopyBufferRegion(scene.get(),0,upload,0,offsetof(WorldLightScene,effects));
            transition(scene.get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            scale=k;
        }
        if (changed && (data.flags&WORLD_LIGHT_FIELD_MASK)) {
            pass_mark(cmd,frame,"begin-light");
            cmd->SetComputeRootSignature(root.get());
            cmd->SetComputeRootShaderResourceView(0,scene->GetGPUVirtualAddress());
            cmd->SetComputeRootUnorderedAccessView(2,direct_field->GetGPUVirtualAddress());
            cmd->SetComputeRootUnorderedAccessView(3,bounce_field->GetGPUVirtualAddress());
            cmd->SetComputeRootUnorderedAccessView(8,vis_cache->GetGPUVirtualAddress());
            D3D12_RESOURCE_BARRIER sync={};sync.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;
            cmd->ResourceBarrier(1,&sync);
            if(!(data.flags&524288) && (data.flags&WORLD_LIGHT_SUN_MASK)){
                cmd->SetPipelineState(sun.get());
                cmd->Dispatch(cols+53,1,1); // Include the 53-pixel slant at row 239.
                cmd->ResourceBarrier(1,&sync);
                pass_mark(cmd,frame,"sun");
            }
            cmd->SetPipelineState(direct.get());cmd->Dispatch((cols+7)/8,30,1);
            cmd->ResourceBarrier(1,&sync);
            pass_mark(cmd,frame,"direct");
            cmd->SetPipelineState(indirect.get());cmd->Dispatch((cols/2+7)/8,15,1);
            cmd->ResourceBarrier(1,&sync);
            pass_mark(cmd,frame,"indirect");
            cmd->SetPipelineState(resolve.get());cmd->Dispatch((cols/2+7)/8,15,1);
            cmd->ResourceBarrier(1,&sync);
            pass_mark(cmd,frame,"resolve");
        }
        width=data.width;revision=data.revision;flags=dispatch_flags;have_field=true;
        return true;
    }

    bool record(ID3D12GraphicsCommandList *cmd,UINT frame,int k,const WorldLightScene &data,
                ID3D12Resource *source,ID3D12Resource *picture)
    {
        auto transition=[&](ID3D12Resource *r,D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to){
            auto b=barrier(r,from,to);cmd->ResourceBarrier(1,&b);
        };
        const int cols=data.width>W && data.width<=WORLD_VIEW_MAX_WIDTH ? (int)data.width : W;
        const UINT pitch=(cols*k*4+255)&~255u;
        read_time(frame);
        bool medium=adaptive(data);
        uint32_t dispatch=dispatch_flags(data);
        bool changed=!have_field || width!=data.width || revision!=data.revision || flags!=dispatch;
        bool measure=early_sample[frame] || (time_readback && (medium || log_times) && (changed || !(data.flags&WORLD_LIGHT_FIELD_MASK)) && (++sample_counter%16==0));
        if(measure)cmd->EndQuery(time_queries.get(),D3D12_QUERY_TYPE_TIMESTAMP,frame*4+2);
        pass_read(frame);
        if(!prepare(cmd,scene_upload[frame].get(),k,data,dispatch,frame))return false;
        pass_mark(cmd,frame,"begin-record");
        void *effect_map=nullptr;
        D3D12_RANGE empty={0,0};
        if(FAILED(effect_upload[frame]->Map(0,&empty,&effect_map)))return false;
        size_t bytes=offsetof(WorldEffectBatch,nodes)+(data.effects.count+1)*sizeof(WorldEffectNode);
        memcpy(effect_map,&data.effects,bytes);
        effect_upload[frame]->Unmap(0,nullptr);
        // A scaled frame is sampled repeatedly by neighboring shader lanes.
        // Stage it and the particle lists in local GPU memory before dispatch.
        if(have_inputs){
            transition(input_device.get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
            transition(effect_device.get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
        }
        have_inputs=true;
        cmd->CopyBufferRegion(input_device.get(),0,source,0,(H*k-1)*pitch+cols*k*4);
        cmd->CopyBufferRegion(effect_device.get(),0,effect_upload[frame].get(),0,bytes);
        transition(input_device.get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        transition(effect_device.get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        cmd->SetComputeRootSignature(root.get());
        cmd->SetComputeRootShaderResourceView(0,scene->GetGPUVirtualAddress());
        cmd->SetComputeRootShaderResourceView(1,input_device->GetGPUVirtualAddress());
        cmd->SetComputeRootUnorderedAccessView(2,direct_field->GetGPUVirtualAddress());
        cmd->SetComputeRootUnorderedAccessView(3,bounce_field->GetGPUVirtualAddress());
        cmd->SetComputeRootUnorderedAccessView(4,result->GetGPUVirtualAddress());
        cmd->SetComputeRootShaderResourceView(5,effect_device->GetGPUVirtualAddress());
        cmd->SetComputeRootUnorderedAccessView(6,history->GetGPUVirtualAddress());
        cmd->SetComputeRootUnorderedAccessView(7,volume_field->GetGPUVirtualAddress());
        cmd->SetComputeRootUnorderedAccessView(8,vis_cache->GetGPUVirtualAddress());
        D3D12_RESOURCE_BARRIER sync={};sync.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;
        cmd->ResourceBarrier(1,&sync);
        pass_mark(cmd,frame,"upload");
        if(data.flags&(32|512)){
            cmd->SetPipelineState(volume.get());cmd->Dispatch((cols/4+7)/8,8,1);
            cmd->ResourceBarrier(1,&sync);
            pass_mark(cmd,frame,"volume");
        }
        cmd->SetPipelineState(shade.get());cmd->Dispatch((cols*k+7)/8,H*k/8,1);
        cmd->ResourceBarrier(1,&sync);
        pass_mark(cmd,frame,"shade");
        transition(result.get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION src={},dst={};
        src.pResource=result.get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint.Footprint={DXGI_FORMAT_B8G8R8A8_UNORM,UINT(cols*k),UINT(H*k),1,pitch};
        dst.pResource=picture;dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        cmd->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        transition(result.get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        pass_mark(cmd,frame,"copy");
        pass_resolve(cmd,frame);
        if(measure){
            cmd->EndQuery(time_queries.get(),D3D12_QUERY_TYPE_TIMESTAMP,frame*4+3);
            cmd->ResolveQueryData(time_queries.get(),D3D12_QUERY_TYPE_TIMESTAMP,frame*4+2,2,time_readback.get(),frame*32+16);
            split_sample[frame]=early_sample[frame];
            early_sample[frame]=false;
            measured[frame]=true;
            measured_medium[frame]=medium;
        }
        return true;
    }
};
