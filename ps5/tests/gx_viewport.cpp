// Tests production logical mapping, state callbacks and hardware snapshots.
#include "gx_viewport.h"
#include "gx/register_backend.hpp"
#include <cstdio>
#include <fstream>
#include <limits>
#include <stdexcept>
namespace gx=aurora::gx;
using namespace mkw::agc;
static unsigned checks;
static void check(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
static void same(const GxMappedViewport& m,aurora::gfx::Viewport v,aurora::gfx::ClipRect r){
    check(m.viewport==v,"mapped viewport differs");check(m.scissor==r,"mapped scissor differs");
}
int main(int argc,char** argv){try{
    if(argc!=2)throw std::runtime_error("Expected SharpProspero reference file");
    std::ifstream in(argv[1],std::ios::binary);uint32_t count;in.read(reinterpret_cast<char*>(&count),4);
    check(in.good()&&count==96,"reference count");
    for(unsigned i=0;i<count;++i){GxMappedViewport m;std::array<GxContextRegister,14> expected;
        in.read(reinterpret_cast<char*>(&m.viewport),24);in.read(reinterpret_cast<char*>(&m.scissor),16);
        in.read(reinterpret_cast<char*>(expected.data()),sizeof(expected));check(in.good(),"truncated reference");
        auto actual=snapshot_gx_viewport(m);
        for(unsigned j=0;j<14;++j){check(actual[j].offset==expected[j].offset,"SharpProspero register offset");
            check(actual[j].pad==0&&actual[j].value==expected[j].value,"SharpProspero register value");}
    }
    gx::GXRegisterState s{};GxRenderExtent full{640,480,1920,1080};
    same(map_gx_viewport(s,full),{0,0,1920,1080,0,1},{0,0,1920,1080});
    // Four-player split-screen quadrants keep independent viewport/scissor.
    for(int row=0;row<2;++row)for(int col=0;col<2;++col){
        s.logicalViewport={float(col*320),float(row*240),320,240,0,1};s.logicalScissor={col*320,row*240,320,240};
        same(map_gx_viewport(s,full),{float(col*960),float(row*540),960,540,0,1},{col*960,row*540,960,540});
    }
    s={};s.logicalViewport={1024,1024,640,480,0,1};s.logicalScissor={1024,1024,640,480};
    same(map_gx_viewport(s,full),{0,0,1920,1080,0,1},{0,0,1920,1080});
    s={};s.scissorOffsetX=20;s.scissorOffsetY=30;s.logicalViewport={20,30,640,480,0,1};s.logicalScissor={20,30,640,480};
    same(map_gx_viewport(s,full),{0,0,1920,1080,0,1},{0,0,1920,1080});
    s={};s.logicalScissor={1,1,2,2};
    same(map_gx_viewport(s,full),{0,0,1920,1080,0,1},{3,2,6,5});
    s.logicalScissor={0,0,0,20};check(map_gx_viewport(s,full).scissor==aurora::gfx::ClipRect{0,0,0,0},"empty scissor");
    s.logicalScissor={0,0,-1,20};check(map_gx_viewport(s,full).scissor.width==0,"negative scissor");
    s={};s.viewportPolicy=AURORA_VIEWPORT_NATIVE;s.logicalViewport={12,19,320,240,.25f,.75f};s.logicalScissor={4,7,100,90};
    same(map_gx_viewport(s,full),s.logicalViewport,s.logicalScissor);
    s={};s.viewportPolicy=AURORA_VIEWPORT_STRETCH;s.logicalViewport={0,0,256,128,0,1};s.logicalScissor={0,0,256,128};
    same(map_gx_viewport(s,{256,128,256,128}),s.logicalViewport,s.logicalScissor);
    // Reversed depth retains a negative scale but ordered clipping bounds.
    auto reversed=snapshot_gx_viewport({{0,0,640,480,1,0},{0,0,640,480}});
    check(reversed[4].value==std::bit_cast<uint32_t>(-1.f),"reversed depth scale");
    check(reversed[6].value==0&&reversed[7].value==std::bit_cast<uint32_t>(1.f),"ordered depth bounds");
    auto invalid=[](GxMappedViewport m){try{snapshot_gx_viewport(m);return false;}catch(const std::invalid_argument&){return true;}};
    for(float bad:{std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}){
        check(invalid({{bad,0,640,480,0,1},{0,0,640,480}}),"reject nonfinite viewport");
        s={};s.logicalViewport.left=bad;check(map_gx_viewport(s,full).scissor.width==0,"nonfinite map has no pixels");
    }
    check(invalid({{0,0,-1,480,0,1},{0,0,640,480}}),"reject unsupported negative viewport width");
    s={};s.logicalScissor={INT32_MAX,INT32_MAX,INT32_MAX,INT32_MAX};s.scissorOffsetX=INT32_MIN;
    check(map_gx_viewport(s,full).scissor.width==0,"overflowing coordinates clipped safely");
    // Exercise the real callbacks used by FIFO decoding; later state changes
    // cannot mutate an already captured AGC register block.
    auto& live=gx::register_state();live={};configure_gx_render_extent(full);
    gx::set_logical_viewport({320,0,320,480,0,1});gx::set_logical_scissor({320,0,320,480});
    same({live.renderViewport,live.renderScissor},{960,0,960,1080,0,1},{960,0,960,1080});
    auto saved=snapshot_gx_viewport({live.renderViewport,live.renderScissor});
    live.stateDirty=false;gx::set_render_viewport(live.renderViewport);gx::set_render_scissor(live.renderScissor);
    check(!live.stateDirty,"unchanged render state remains clean");
    gx::set_render_scissor({0,0,10,10});check(live.stateDirty,"changed scissor marks dirty");
    check(saved[12].value==(0x80000000u|960u),"owned previous draw snapshot");
    bool rejected=false;try{configure_gx_render_extent({0,480,1920,1080});}catch(const std::invalid_argument&){rejected=true;}
    check(rejected,"reject zero render extent");
    configure_gx_render_extent({640,480,1280,960});
    same({live.renderViewport,live.renderScissor},{640,0,640,960,0,1},{640,0,640,960});
    std::printf("PASS GX viewport: %u checks, 96 SharpProspero blocks, split-screen/wrap/resize/state lifetime\n",checks);
    return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
