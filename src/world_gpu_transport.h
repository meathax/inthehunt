#include "world_gpu_transport_shader.h"

struct GpuTransport {
    Com<ID3D12RootSignature> root;
    Com<ID3D12PipelineState> direct, indirect, shade;
    Com<ID3D12Resource> scene_upload[FRAMES], scene, direct_field, bounce_field, result;
    bool ready=false, have_field=false;
    uint32_t revision=0, flags=0;
    int scale=0;

    bool init(ID3D12Device *dev)
    {
        D3D12_ROOT_PARAMETER params[5]={};
        for(UINT i=0;i<5;i++){
            params[i].ParameterType=i<2?D3D12_ROOT_PARAMETER_TYPE_SRV:D3D12_ROOT_PARAMETER_TYPE_UAV;
            params[i].Descriptor.ShaderRegister=i<2?i:i-2;
        }
        D3D12_ROOT_SIGNATURE_DESC desc={};
        desc.NumParameters=5; desc.pParameters=params;
        Com<ID3DBlob> signature, errors;
        if(FAILED(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,signature.put(),errors.put())) ||
           FAILED(dev->CreateRootSignature(0,signature->GetBufferPointer(),signature->GetBufferSize(),IID_PPV_ARGS(root.put()))))return false;
        const char *names[]={"directPass","indirectPass","shadePass"};
        Com<ID3D12PipelineState> *states[]={&direct,&indirect,&shade};
        for(int i=0;i<3;i++){
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
            if(FAILED(buffer(dev,D3D12_HEAP_TYPE_UPLOAD,sizeof(WorldLightScene),D3D12_RESOURCE_STATE_GENERIC_READ,u.put())))return false;
        if(FAILED(buffer(dev,D3D12_HEAP_TYPE_DEFAULT,sizeof(WorldLightScene),D3D12_RESOURCE_STATE_COPY_DEST,scene.put())))return false;
        auto storage=[&](Com<ID3D12Resource>& resource,UINT bytes){
            D3D12_RESOURCE_DESC d=scene->GetDesc();
            d.Width=bytes;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            D3D12_HEAP_PROPERTIES heap={};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
            return SUCCEEDED(dev->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(resource.put())));
        };
        ready=storage(direct_field,WORLD_PROBES*16) && storage(bounce_field,WORLD_PROBES*16) && storage(result,W*H*4*4*4);
        return ready;
    }

    bool record(ID3D12GraphicsCommandList *cmd,UINT frame,int k,const WorldLightScene &data,
                ID3D12Resource *source,ID3D12Resource *picture)
    {
        auto transition=[&](ID3D12Resource *r,D3D12_RESOURCE_STATES from,D3D12_RESOURCE_STATES to){
            auto b=barrier(r,from,to);cmd->ResourceBarrier(1,&b);
        };
        bool changed=!have_field || revision!=data.revision || flags!=data.flags;
        if (changed || scale!=k) {
            void *map=nullptr;
            D3D12_RANGE empty={0,0};
            if(FAILED(scene_upload[frame]->Map(0,&empty,&map)))return false;
            memcpy(map,&data,sizeof data);
            scene_upload[frame]->Unmap(0,nullptr);
            if(have_field)
                transition(scene.get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
            cmd->CopyBufferRegion(scene.get(),0,scene_upload[frame].get(),0,sizeof data);
            transition(scene.get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            scale=k;
        }
        cmd->SetComputeRootSignature(root.get());
        cmd->SetComputeRootShaderResourceView(0,scene->GetGPUVirtualAddress());
        cmd->SetComputeRootShaderResourceView(1,source->GetGPUVirtualAddress());
        cmd->SetComputeRootUnorderedAccessView(2,direct_field->GetGPUVirtualAddress());
        cmd->SetComputeRootUnorderedAccessView(3,bounce_field->GetGPUVirtualAddress());
        cmd->SetComputeRootUnorderedAccessView(4,result->GetGPUVirtualAddress());
        D3D12_RESOURCE_BARRIER sync={};sync.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;
        cmd->ResourceBarrier(1,&sync);
        if (changed) {
            cmd->SetPipelineState(direct.get());cmd->Dispatch(20,15,1);
            cmd->ResourceBarrier(1,&sync);
            cmd->SetPipelineState(indirect.get());cmd->Dispatch(20,15,1);
            cmd->ResourceBarrier(1,&sync);
            revision=data.revision;flags=data.flags;have_field=true;
        }
        cmd->SetPipelineState(shade.get());cmd->Dispatch(W*k/8,H*k/8,1);
        transition(result.get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION src={},dst={};
        src.pResource=result.get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint.Footprint={DXGI_FORMAT_B8G8R8A8_UNORM,UINT(W*k),UINT(H*k),1,UINT(W*k*4)};
        dst.pResource=picture;dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        cmd->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
        transition(result.get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        return true;
    }
};
