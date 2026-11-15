#include <algorithm>
#include "world_gpu_transport_shader.h"
static_assert(offsetof(WorldLightScene,water_grade)==82900 && offsetof(WorldLightScene,ice_coverage)==82916);
static_assert(offsetof(WorldLightScene,sky_stop)==102116);
static_assert(offsetof(WorldLightScene,light_bins)==81300);
static_assert(offsetof(WorldEffectBatch,mirror)==5336 && offsetof(WorldEffectBatch,ocean_rows)==43736 && offsetof(WorldEffectBatch,nodes)==43768 && sizeof(WorldEffectNode)==32);

static_assert(offsetof(WorldLightScene,point_coverage)==102492);

// Close single-pixel stipple holes in each material independently. Four
// native pixels and both materials are processed together in each byte.
static void pack_point_coverage(uint8_t *out,const uint32_t *cells){
    for(int y=0;y<120;++y)for(int x=0;x<160;++x){
        int i=y*160+x;
        unsigned c=cells[i]>>24;
        unsigned l=x?cells[i-1]>>24:0,r=x<159?cells[i+1]>>24:0;
        unsigned t=y?cells[i-160]>>24:0,b=y<119?cells[i+160]>>24:0;
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
    Com<ID3D12Resource> scene_upload[FRAMES], scene, direct_field, bounce_field, result;
    bool ready=false, have_field=false, have_inputs=false;
    uint32_t revision=0, flags=0;
    int scale=0;

    Com<ID3D12QueryHeap> time_queries;
    Com<ID3D12Resource> time_readback;
    UINT64 frequency=0;
    unsigned sample_counter=0, slow_samples=0, fast_samples=0;
    bool early_sample[FRAMES]={}, split_sample[FRAMES]={};
    bool measured[FRAMES]={}, measured_medium[FRAMES]={}, budget_mode=false, log_times=false;

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
        }
        D3D12_ROOT_PARAMETER params[8]={};
        for(UINT i=0;i<5;i++){
            params[i].ParameterType=i<2?D3D12_ROOT_PARAMETER_TYPE_SRV:D3D12_ROOT_PARAMETER_TYPE_UAV;
            params[i].Descriptor.ShaderRegister=i<2?i:i-2;
        }
        D3D12_ROOT_SIGNATURE_DESC desc={};
        params[5].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;params[5].Descriptor.ShaderRegister=2;
        params[6].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;params[6].Descriptor.ShaderRegister=3;
        params[7].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;params[7].Descriptor.ShaderRegister=4;
        desc.NumParameters=8; desc.pParameters=params;
        Com<ID3DBlob> signature, errors;
        if(FAILED(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,signature.put(),errors.put())) ||
           FAILED(dev->CreateRootSignature(0,signature->GetBufferPointer(),signature->GetBufferSize(),IID_PPV_ARGS(root.put()))))return false;
        const char *names[]={"sunPass","directPass","indirectPass","shadePass","volumePass","resolvePass"};
        Com<ID3D12PipelineState> *states[]={&sun,&direct,&indirect,&shade,&volume,&resolve};
        for(int i=0;i<6;i++){
            Com<ID3DBlob> code;
            if(FAILED(D3DCompile(transport_shader,strlen(transport_shader),"water transport",nullptr,nullptr,names[i],"cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,code.put(),errors.put()))){
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
        for(auto &u:effect_upload)
            if(FAILED(buffer(dev,D3D12_HEAP_TYPE_UPLOAD,sizeof(WorldEffectBatch),D3D12_RESOURCE_STATE_GENERIC_READ,u.put())))return false;
        if(FAILED(buffer(dev,D3D12_HEAP_TYPE_DEFAULT,sizeof(WorldEffectBatch),D3D12_RESOURCE_STATE_COPY_DEST,effect_device.put())) ||
           FAILED(buffer(dev,D3D12_HEAP_TYPE_DEFAULT,W*H*4*4*4,D3D12_RESOURCE_STATE_COPY_DEST,input_device.put())))return false;
        auto storage=[&](Com<ID3D12Resource>& resource,UINT bytes){
            D3D12_RESOURCE_DESC d=scene->GetDesc();
            d.Width=bytes;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            D3D12_HEAP_PROPERTIES heap={};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
            return SUCCEEDED(dev->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(resource.put())));
        };
        ready=storage(volume_field,80*60*16) && storage(history,W*H*4*4*4) && storage(direct_field,(WORLD_PROBES+320*240+373)*16) && storage(bounce_field,WORLD_PROBES*16) && storage(result,W*H*4*4*4);
        return ready;
    }

    bool needs_light(const WorldLightScene &data) const {
        return !have_field || revision!=data.revision || flags!=data.flags || scale!=data.scale;
    }
    bool prepare(ID3D12GraphicsCommandList *cmd,ID3D12Resource *upload,int k,
                 const WorldLightScene &data,uint32_t dispatch_flags){
        bool changed=!have_field || revision!=data.revision || flags!=dispatch_flags;
        auto transition=[&](ID3D12Resource *r,D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to){
            auto b=barrier(r,from,to);cmd->ResourceBarrier(1,&b);
        };
        if (changed || scale!=k) {
            void *map=nullptr;
            D3D12_RANGE empty={0,0};
            if(FAILED(upload->Map(0,&empty,&map)))return false;
            memcpy(map,&data,offsetof(WorldLightScene,effects));
            auto *packed=static_cast<WorldLightScene*>(map);
            packed->flags=dispatch_flags;
            pack_point_coverage(packed->point_coverage,data.cells);
            uint32_t bins[10*8][5]={};
            for(unsigned i=0;i<data.count && i<WORLD_EMITTERS;++i){
                const float *e=data.emitters[i];
                // radius is conservative; the shader retains the per-probe falloff test.
                int xmin=std::max(0,(int)floorf((e[0]-e[2])/16));
                int xmax=std::min(9,(int)floorf((e[0]+e[2])/16));
                int ymin=std::max(0,(int)floorf((e[1]-e[2])/16));
                int ymax=std::min(7,(int)floorf((e[1]+e[2])/16));
                for(int y=ymin;y<=ymax;++y)for(int x=xmin;x<=xmax;++x)
                    bins[y*10+x][i/32]|=1u<<(i%32);
            }
            memcpy(packed->light_bins,bins,sizeof bins);
            upload->Unmap(0,nullptr);
            if(have_field)
                transition(scene.get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
            cmd->CopyBufferRegion(scene.get(),0,upload,0,offsetof(WorldLightScene,effects));
            transition(scene.get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            scale=k;
        }
        if (changed && (data.flags&(23|32768|16384))) {
            cmd->SetComputeRootSignature(root.get());
            cmd->SetComputeRootShaderResourceView(0,scene->GetGPUVirtualAddress());
            cmd->SetComputeRootUnorderedAccessView(2,direct_field->GetGPUVirtualAddress());
            cmd->SetComputeRootUnorderedAccessView(3,bounce_field->GetGPUVirtualAddress());
            D3D12_RESOURCE_BARRIER sync={};sync.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;
            cmd->ResourceBarrier(1,&sync);
            if(!(data.flags&524288) && (data.flags&(2|16|16384))){
                cmd->SetPipelineState(sun.get());
                cmd->Dispatch(373,1,1); // 320 columns plus the 53-pixel slant at row 239
                cmd->ResourceBarrier(1,&sync);
            }
            cmd->SetPipelineState(direct.get());cmd->Dispatch(40,30,1);
            cmd->ResourceBarrier(1,&sync);
            cmd->SetPipelineState(indirect.get());cmd->Dispatch(20,15,1);
            cmd->ResourceBarrier(1,&sync);
            cmd->SetPipelineState(resolve.get());cmd->Dispatch(20,15,1);
            cmd->ResourceBarrier(1,&sync);
        }
        revision=data.revision;flags=dispatch_flags;have_field=true;
        return true;
    }

    bool record(ID3D12GraphicsCommandList *cmd,UINT frame,int k,const WorldLightScene &data,
                ID3D12Resource *source,ID3D12Resource *picture)
    {
        auto transition=[&](ID3D12Resource *r,D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to){
            auto b=barrier(r,from,to);cmd->ResourceBarrier(1,&b);
        };
        read_time(frame);
        bool medium=(data.flags&1024)!=0 && (data.flags&(23|32768|16384))!=0;
        uint32_t dispatch_flags=data.flags | (medium && budget_mode ? 2048u : 0u);
        bool changed=!have_field || revision!=data.revision || flags!=dispatch_flags;
        bool measure=early_sample[frame] || (time_readback && (medium || log_times) && (changed || !(data.flags&(23|32768|16384))) && (++sample_counter%16==0));
        if(measure)cmd->EndQuery(time_queries.get(),D3D12_QUERY_TYPE_TIMESTAMP,frame*4+2);
        if(!prepare(cmd,scene_upload[frame].get(),k,data,dispatch_flags))return false;
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
        cmd->CopyBufferRegion(input_device.get(),0,source,0,W*H*k*k*4);
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
        D3D12_RESOURCE_BARRIER sync={};sync.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;
        cmd->ResourceBarrier(1,&sync);
        if(data.flags&(32|512)){
            cmd->SetPipelineState(volume.get());cmd->Dispatch(10,8,1);
            cmd->ResourceBarrier(1,&sync);
        }
        cmd->SetPipelineState(shade.get());cmd->Dispatch(W*k/8,H*k/8,1);
        transition(result.get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION src={},dst={};
        src.pResource=result.get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint.Footprint={DXGI_FORMAT_B8G8R8A8_UNORM,UINT(W*k),UINT(H*k),1,UINT(W*k*4)};
        dst.pResource=picture;dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        cmd->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        transition(result.get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
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
