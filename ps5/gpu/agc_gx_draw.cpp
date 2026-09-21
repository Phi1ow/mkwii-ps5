// SPDX-License-Identifier: GPL-3.0-only
// Driver ABI from the supplied ps5link-sdk GPU example; GX shader ABI from
// this port's raw vertex/lighting/texgen and direct-TEV shader implementations.
#include "agc_gx_draw.h"
#include "gpu_wait_service.h"
#include "gx_perf_stats.h"
#include "gpu_buffer.h"
#include "gpu_fence.h"
#include <bit>
#include <cstdio>
#include <cstring>
#include <optional>
#include <vector>
#include <stdexcept>
extern "C" {
int sceKernelUsleep(unsigned);
void* sceAgcGetRegisterDefaults();
int sceAgcLinkShaders(void*,void*,void*,void*,void*,unsigned);
void sceAgcDcbSetCxRegistersIndirect(void*,void*,unsigned);
void sceAgcDcbSetShRegistersIndirect(void*,void*,unsigned);
void sceAgcDcbSetUcRegistersIndirect(void*,void*,unsigned);
void sceAgcCbSetShRegisterRangeDirect(void*,unsigned,unsigned*,unsigned);
void* sceAgcDcbSetIndexSize(void*,unsigned char,unsigned char);
void* sceAgcDcbSetIndexBuffer(void*,void*);
void* sceAgcDcbSetIndexCount(void*,unsigned);
void* sceAgcDcbDrawIndex(void*,unsigned,void*,uint64_t);
int sceAgcDriverSubmitDcb(void*);
int sceAgcSuspendPoint();
}
namespace mkw::agc {
namespace {
struct Register {uint16_t offset,pad;uint32_t value;};
struct Dcb {uint32_t *bottom,*top,*up,*down;intptr_t callback;void* user;uint32_t reserved,pad;};
static_assert(sizeof(Register)==8&&sizeof(Dcb)==56);
unsigned char exhausted(Dcb* d,unsigned,void*){*static_cast<bool*>(d->user)=true;return 0;}
template<class T>T field(void* p,size_t offset){T v;std::memcpy(&v,static_cast<char*>(p)+offset,sizeof(v));return v;}
unsigned resource(void* shader,unsigned kind){
    if(!shader)throw std::invalid_argument("Missing GX prepared shader");
    auto* ud=field<void*>(shader,8);if(!ud)throw std::invalid_argument("Missing GX shader resource table");
    auto* slots=field<uint16_t*>(ud,8+sizeof(void*)*kind);
    if(!slots||field<uint16_t>(ud,46+kind*2)!=1)throw std::invalid_argument("GX shader requires one resource in each buffer class");
    unsigned offset=slots[0]&0x7fff;
    if(offset>12)throw std::invalid_argument("GX shader buffer exceeds user registers");
    return offset;
}
std::array<uint32_t,4> buffer(void* pointer,size_t size){
    if(!size||size>UINT32_MAX)throw std::invalid_argument("GX buffer exceeds descriptor range");
    const auto a=reinterpret_cast<uintptr_t>(pointer);
    if(a>=(uint64_t{1}<<48))throw std::invalid_argument("GX buffer address exceeds GPU range");
    return {uint32_t(a),uint32_t((a>>32)&65535),uint32_t(size),0x30005204};
}
size_t aligned(size_t value){if(value>SIZE_MAX-255)throw std::length_error("GX upload size overflow");return (value+255)&~size_t(255);}
}
struct AgcGxDraw::Impl {
    // Draws are recorded into a batch and the batch is submitted as one DCB,
    // the pattern the SDK documents (record a frame, submit once, one suspend
    // point). Each batch owns GPU-visible command/register storage and an
    // upload arena that stay untouched until its completion serial is seen.
    static constexpr size_t CommandBytes=1u<<20;
    static constexpr size_t WordBytes=256u<<10;          // DCB words live in [0, WordBytes)
    static constexpr size_t RegisterBase=WordBytes;       // register arrays follow
    static constexpr size_t MaxRegistersPerDraw=1024;     // 34 linkage + shader + targets + raster, with margin
    static constexpr size_t MaxShaderRegisters=512;
    static constexpr size_t WordsPerDrawMargin=256;       // generous bound for one draw's packets + fence
    struct Batch {
        GpuBuffer commands{CommandBytes};
        std::unique_ptr<GpuBuffer> upload;
        std::vector<GxDirectMaterial> materials;          // texture/copy owners for the GPU's reads
        std::vector<std::shared_ptr<GpuColorTarget>> colors;
        std::vector<std::shared_ptr<GpuDepthTarget>> depths;
        size_t uploadUsed=0,registerUsed=0;
        size_t vertexOffset=SIZE_MAX,materialOffset=SIZE_MAX;  // last uploaded blocks, for deduplication
        uint32_t* shared=nullptr;unsigned sharedCount=0;       // shader SH registers, once per batch
        Register* uc=nullptr;
        Dcb dcb{};bool overflow=false;
        unsigned draws=0;
        uint64_t serial=0;                                     // last serial recorded; fence value when submitted
        bool submitted=false;
        template<class T>T* at(size_t offset){return reinterpret_cast<T*>(static_cast<char*>(commands.data())+offset);}
        void release(){materials.clear();colors.clear();depths.clear();}
    };
    GpuBuffer label{65536}; // Shared end-of-pipe completion label, written in submission order.
    std::unique_ptr<Batch> batches[InFlightSlots];
    Batch* open=nullptr;
    size_t cursor=0;                                    // next ring position for a new batch
    void *vs,*ps;unsigned constantSlot,geometrySlot,materialSlot;
    // Driver defaults and shader linkage are constant for this pair; resolve
    // them once instead of scanning the default context on every draw.
    std::array<uint32_t,16> colorBase{},depthBase{};
    uint32_t blendBase=0,cullBase=0;
    Register linkageRegs[34]{},ucRegs[8]{};
    std::vector<Register> shaderCx,shaderSh;             // per-shader registers, identical for every draw
    uint64_t serial=0,submittedSerial=0;bool failed=false;
    Impl(void* v,void* p):vs(v),ps(p),constantSlot(resource(v,3)),geometrySlot(resource(v,0)),materialSlot(resource(p,0)){
        if(constantSlot<geometrySlot+4&&geometrySlot<constantSlot+4)throw std::invalid_argument("Overlapping GX vertex buffers");
        auto* ud=field<void*>(ps,8);auto* pixelSlots=field<uint16_t*>(ud,8);
        if(materialSlot!=0||!(pixelSlots[0]&0x8000)||field<uint16_t>(ud,50))throw std::invalid_argument("Wrong direct-TEV pixel buffer ABI");
        *static_cast<uint64_t*>(label.data())=0;label.flush(64);
        auto* defaults=sceAgcGetRegisterDefaults();if(!defaults)throw std::runtime_error("AGC defaults unavailable");
        auto blocks=field<Register**>(defaults,0);auto count=field<uint32_t>(defaults,0x20);
        if(!blocks||!blocks[0]||!count||count>3000)throw std::runtime_error("Invalid AGC default context");
        auto value=[&](uint16_t offset){for(unsigned i=0;i<count;++i)if(blocks[0][i].offset==offset)return blocks[0][i].value;throw std::runtime_error("Missing AGC context default");};
        for(unsigned i=0;i<16;++i){colorBase[i]=value(ColorTargetOffsets[i]);depthBase[i]=value(DepthTargetOffsets[i]);}
        blendBase=value(0x1e0);cullBase=value(0x205);
        if(sceAgcLinkShaders(linkageRegs,ucRegs,nullptr,vs,ps,4)<0)throw std::runtime_error("GX shader linkage failed");
        for(unsigned i=0;i<11;++i)if(linkageRegs[i].offset!=0x191+i||linkageRegs[i].value!=i)throw std::runtime_error("GX shaders require eleven matching parameters");
        for(void* shader:{vs,ps}){
            auto cxCount=field<uint8_t>(shader,91),shCount=field<uint8_t>(shader,92);
            auto* cr=field<Register*>(shader,24);auto* sr=field<Register*>(shader,32);
            if((cxCount&&!cr)||(shCount&&!sr)||shaderSh.size()+shCount>MaxShaderRegisters)throw std::runtime_error("Invalid GX shader registers");
            shaderCx.insert(shaderCx.end(),cr,cr+cxCount);shaderSh.insert(shaderSh.end(),sr,sr+shCount);
        }
        if(34+shaderCx.size()+32+14+10>MaxRegistersPerDraw)throw std::runtime_error("GX context exceeds batch register bound");
    }
    uint64_t completed() const noexcept{
        __builtin_ia32_clflush(label.data());__atomic_thread_fence(__ATOMIC_SEQ_CST);
        return *static_cast<volatile uint64_t*>(label.data());
    }
    // Only submitted work can be unresolved; an open batch has no GPU references.
    bool pending() const noexcept{return failed||completed()<submittedSerial;}
    // Non-blocking: drop the owners of every batch the GPU has reported.
    void retire(){
        const auto done=completed();
        for(auto& b:batches)if(b&&b->submitted&&b->serial<=done){b->release();b->submitted=false;}
    }
    void wait_until(uint64_t target){
        const auto started=gpu_clock_nanos();
        // Spin before sleeping: most slot waits retire in microseconds, well
        // under the ~1ms granularity of a kernel sleep. Guest timing callbacks
        // still run inside gpu_wait_slice during both phases.
        for(unsigned i=0;i<8192;++i){
            if(completed()>=target){retire();gx_perf_add(gx_perf_stats().drawWaitNanos,gpu_clock_nanos()-started);return;}
            if((i&1023)==1023)gpu_wait_slice(0);
        }
        for(unsigned i=0;i<5000;++i){
            if(completed()>=target){retire();gx_perf_add(gx_perf_stats().drawWaitNanos,gpu_clock_nanos()-started);return;}
            gpu_wait_slice(1000);
        }
        failed=true;throw std::runtime_error("GX draw completion timeout; resources retained");
    }
    void begin_batch(size_t uploadNeeded){
        auto& owner=batches[cursor%InFlightSlots];
        if(!owner)owner=std::make_unique<Batch>();
        Batch& b=*owner;
        retire();
        // The batch's previous submission must have retired before its storage is rewritten.
        if(b.submitted)wait_until(b.serial);
        b.release();
        if(!b.upload||b.upload->size()<uploadNeeded){
            size_t capacity=4u<<20;while(capacity<uploadNeeded)capacity*=2;
            auto grown=std::make_unique<GpuBuffer>(capacity);b.upload=std::move(grown);
        }
        b.uploadUsed=0;b.registerUsed=0;b.draws=0;b.overflow=false;b.submitted=false;
        b.vertexOffset=b.materialOffset=SIZE_MAX;
        auto* words=b.at<uint32_t>(0);
        b.dcb=Dcb{words,words+WordBytes/4,words,words+WordBytes/4,reinterpret_cast<intptr_t>(&exhausted),&b.overflow,0,0};
        // Constant per-batch register arrays: shader SH registers and the primitive (UC) records.
        auto* sh=b.at<Register>(RegisterBase);
        std::memcpy(sh,shaderSh.data(),shaderSh.size()*sizeof(Register));
        b.shared=reinterpret_cast<uint32_t*>(sh);b.sharedCount=unsigned(shaderSh.size());
        b.uc=b.at<Register>(RegisterBase+MaxShaderRegisters*sizeof(Register));
        std::memcpy(b.uc,ucRegs,3*sizeof(Register));
        b.registerUsed=MaxShaderRegisters*sizeof(Register)+3*sizeof(Register);
        open=&b;++cursor;
    }
    void submit_open(){
        if(!open||!open->draws){open=nullptr;return;}
        Batch& b=*open;open=nullptr;
        auto& d=b.dcb;
        if(b.overflow||d.down-d.up<8)throw std::length_error("GX draw command buffer overflow");
        auto fence=encode_gpu_completion(reinterpret_cast<uintptr_t>(label.data()),b.serial);std::memcpy(d.up,fence.data(),sizeof(fence));d.up+=8;
        auto* words=b.at<uint32_t>(0);
        b.upload->flush(0,b.uploadUsed);
        b.commands.flush(0,size_t(d.up-words)*4);
        b.commands.flush(RegisterBase,b.registerUsed);
        struct {void* words;uint32_t count;uint8_t flag;} submission{};submission.words=words;submission.count=uint32_t(d.up-words);
        b.submitted=true;submittedSerial=b.serial;
        if(sceAgcDriverSubmitDcb(&submission)<0){failed=true;throw std::runtime_error("GX submit failed; resources retained");}
        sceAgcSuspendPoint();
        retire();
    }
    void finish(){
        if(failed)throw std::logic_error("GX draw unresolved; resources retained");
        submit_open();
        if(completed()<submittedSerial)wait_until(submittedSerial);else retire();
    }
    void submit(){
        if(failed)throw std::logic_error("GX draw unresolved; resources retained");
        submit_open();
    }
    uint64_t draw(GxDrawPacket packet,std::shared_ptr<GpuColorTarget> c,std::shared_ptr<GpuDepthTarget> z){
        if(failed)throw std::logic_error("GX draw unresolved; resources retained");
        if(!c||!c->data()||!z||!z->data()||c->layout().width()!=z->layout().width()||c->layout().height()!=z->layout().height())
            throw std::invalid_argument("GX draw requires matching live color/depth targets");
        if(packet.geometry.empty()||packet.geometry.size()>UINT32_MAX||packet.indices.empty()||packet.indices.size()%3||packet.indices.size()>UINT32_MAX/4)
            throw std::invalid_argument("Invalid GX draw packet sizes");
        if(packet.cullMode>3||(packet.depthControl&~0x76u)||!(packet.depthControl&2)||packet.blend.targetMask>15)
            throw std::invalid_argument("Invalid GX draw raster packet");
        for(const auto& copy:packet.material.color_copies())if(copy&&copy->target->data()==c->data())
            throw std::invalid_argument("GX draw cannot sample its active color target");
        if(serial==UINT64_MAX)throw std::overflow_error("GX draw completion serial exhausted");
        const auto viewport=snapshot_gx_viewport(packet.viewport);
        // Empty scissor and cull-all still consume no GPU work or serial.
        if(packet.cullMode==3||packet.viewport.scissor.width<=0||packet.viewport.scissor.height<=0)return serial;
        constexpr size_t VertexBytes=MKW_GX_TEXGEN_OFFSET+MKW_GX_TEXGEN_BYTES;
        static_assert(sizeof(GxTransformState)+sizeof(MkwLighting)==MKW_GX_TEXGEN_OFFSET);
        const size_t geometryBytes=aligned(packet.geometry.size()),indexBytes=aligned(packet.indices.size()*4);
        const size_t worstUpload=geometryBytes+indexBytes+aligned(VertexBytes)+aligned(sizeof(GxDirectUniforms));
        if(worstUpload>UINT32_MAX)throw std::length_error("GX upload exceeds descriptor address range");
        // Start a new batch when the open one is full or cannot hold this draw.
        if(open&&(open->draws>=BatchDraws||!open->upload||open->upload->size()-open->uploadUsed<worstUpload||
                  open->registerUsed+MaxRegistersPerDraw*sizeof(Register)>CommandBytes-RegisterBase||
                  size_t(open->dcb.down-open->dcb.up)<WordsPerDrawMargin))
            submit_open();
        if(!open)begin_batch(worstUpload);
        Batch& b=*open;
        auto* data=static_cast<char*>(b.upload->data());
        const auto place=[&](const void* bytes,size_t size){
            const size_t at=b.uploadUsed;std::memcpy(data+at,bytes,size);b.uploadUsed+=aligned(size);return at;};
        const size_t geometryOffset=place(packet.geometry.data(),packet.geometry.size());
        const size_t indexOffset=place(packet.indices.data(),packet.indices.size()*4);
        // Consecutive draws of one model usually share transforms/lighting/texgen
        // and material; reuse the previous upload instead of copying it again.
        if(b.vertexOffset==SIZE_MAX||
           std::memcmp(data+b.vertexOffset,&packet.transforms,sizeof(packet.transforms))||
           std::memcmp(data+b.vertexOffset+sizeof(packet.transforms),&packet.lighting,sizeof(packet.lighting))||
           std::memcmp(data+b.vertexOffset+MKW_GX_TEXGEN_OFFSET,&packet.texgen,sizeof(packet.texgen))){
            const size_t at=b.uploadUsed;
            std::memcpy(data+at,&packet.transforms,sizeof(packet.transforms));
            std::memcpy(data+at+sizeof(packet.transforms),&packet.lighting,sizeof(packet.lighting));
            std::memcpy(data+at+MKW_GX_TEXGEN_OFFSET,&packet.texgen,sizeof(packet.texgen));
            b.uploadUsed+=aligned(VertexBytes);b.vertexOffset=at;
        }
        if(b.materialOffset==SIZE_MAX||std::memcmp(data+b.materialOffset,&packet.material.uniforms(),sizeof(GxDirectUniforms)))
            b.materialOffset=place(&packet.material.uniforms(),sizeof(GxDirectUniforms));
        auto* cx=b.at<Register>(RegisterBase+b.registerUsed);unsigned nc=0;
        auto add=[&](Register r){cx[nc++]=r;};
        for(auto r:linkageRegs)add(r);
        for(auto r:shaderCx)add(r);
        auto colorValues=c->context_values(colorBase);
        for(unsigned i=0;i<16;++i)add({ColorTargetOffsets[i],0,colorValues[i]});
        auto depthValues=z->context_values(depthBase);
        for(unsigned i=0;i<16;++i)add({DepthTargetOffsets[i],0,depthValues[i]});
        for(auto r:viewport)add({r.offset,0,r.value});
        add({0,0,0});add({0x200,0,packet.depthControl});
        add({0x1e0,0,(blendBase&~0x7fff1fffu)|packet.blend.control});add({0x8e,0,packet.blend.targetMask});
        for(unsigned i=0;i<4;++i)add({uint16_t(0x105+i),0,std::bit_cast<uint32_t>(packet.blend.constant[i])});
        // GX's front face is clockwise (WiiCompiled gx.cpp). PA_SU_SC_MODE:
        // front/back cull bits 0/1, clockwise face bit 2. Validate on hardware.
        add({0x205,0,(cullBase&~7u)|4u|packet.cullMode});
        if(nc>MaxRegistersPerDraw)throw std::length_error("GX context overflow");
        b.registerUsed+=size_t(nc)*sizeof(Register);
        auto vb=buffer(data+geometryOffset,packet.geometry.size()),cb=buffer(data+b.vertexOffset,VertexBytes),pb=buffer(data+b.materialOffset,sizeof(GxDirectUniforms));
        auto& d=b.dcb;
        sceAgcDcbSetCxRegistersIndirect(&d,cx,nc);sceAgcDcbSetShRegistersIndirect(&d,b.shared,b.sharedCount);sceAgcDcbSetUcRegistersIndirect(&d,b.uc,3);
        sceAgcCbSetShRegisterRangeDirect(&d,0x8c+constantSlot,cb.data(),4);sceAgcCbSetShRegisterRangeDirect(&d,0x8c+geometrySlot,vb.data(),4);
        sceAgcCbSetShRegisterRangeDirect(&d,0xc+materialSlot,pb.data(),4);
        sceAgcDcbSetIndexSize(&d,1,0);sceAgcDcbSetIndexBuffer(&d,data+indexOffset);sceAgcDcbSetIndexCount(&d,uint32_t(packet.indices.size()));
        sceAgcDcbDrawIndex(&d,uint32_t(packet.indices.size()),data+indexOffset,0);
        if(b.overflow||d.down-d.up<8)throw std::length_error("GX draw command buffer overflow");
        // Owners the GPU will read through: material textures/copies and targets.
        b.materials.push_back(std::move(packet.material));
        if(b.colors.empty()||b.colors.back()!=c)b.colors.push_back(std::move(c));
        if(b.depths.empty()||b.depths.back()!=z)b.depths.push_back(std::move(z));
        ++b.draws;b.serial=++serial;
        return serial;
    }
};
AgcGxDraw::AgcGxDraw(void* vs,void* ps):impl_(std::make_unique<Impl>(vs,ps)){}
AgcGxDraw::~AgcGxDraw(){if(impl_&&impl_->pending()){std::fputs("[mkw-gx-draw] unresolved GPU work retained until process cleanup\n",stderr);(void)impl_.release();}}
bool AgcGxDraw::pending() const noexcept{return impl_->pending();}
void AgcGxDraw::finish() const{impl_->finish();}
void AgcGxDraw::submit() const{impl_->submit();}
uint64_t AgcGxDraw::draw(GxDrawPacket p,std::shared_ptr<GpuColorTarget> c,std::shared_ptr<GpuDepthTarget> z){return impl_->draw(std::move(p),std::move(c),std::move(z));}
}
