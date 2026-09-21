// SPDX-License-Identifier: GPL-3.0-only
#include "gx_frame_renderer.h"
#include "gx_frame_timing.h"
#include "gpu_wait_service.h"
#include "gx_perf_stats.h"
#include "../runtime/async_log.h"
#include "gx_renderer.h"
#include "gx_memory_sources.h"
#include "gx_display_copy.h"
#include "gx_copy_readback.h"
#include "gx/fifo.hpp"
#include "gx/register_backend.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <memory>
#include <thread>
#include <vector>
#include <xmmintrin.h>
// Guest scheduler idle accounting from WiiCompiled os_scheduler.cpp; weak so
// diagnostics that link the renderer without the runtime still build.
extern "C" uint64_t mkw_guest_idle_nanos() __attribute__((weak));
extern "C" uint64_t mkw_guest_idle_sleep_nanos() __attribute__((weak));
// DIAGNOSTIC exact function profiler, linked only into the profiling variant
// of the game (ps5/runtime/function_profiler_ps5.cpp); reports on frame ends.
extern "C" void mkw_fprof_frame_end() __attribute__((weak));
extern "C" uint64_t sceKernelGetProcessTime(); // microseconds since process start
extern "C" int sceKernelGetCurrentCpu(); // processor running the calling (guest) thread
namespace mkw::agc {
namespace {GxFrameRenderer* active=nullptr;
// DIAGNOSTIC screen capture, enabled by /app0/UserData/screenshot.txt: every
// kShotInterval the displayed image is read back, halved and written as
// Logs/shot-<process seconds>.bgra (u32 width, u32 height, BGRA rows) by a
// detached thread. Lets a run be checked visually without the TV.
constexpr uint64_t kShotInterval=30'000'000'000u;
bool screenshots_requested(){
#if defined(MKW_PS5_RELEASE)
    return false;  // player package: never probes the flag file
#endif
    static const bool requested=[]{
        if(std::FILE* flag=std::fopen("/app0/UserData/screenshot.txt","rb")){std::fclose(flag);return true;}
        return false;
    }();
    return requested;
}
void capture_screen(const GpuColorTarget& target,uint64_t processMicros){
    const auto& layout=target.layout();
    const uint32_t width=layout.width()/2,height=layout.height()/2;
    auto pixels=std::make_shared<std::vector<uint8_t>>(8+size_t(width)*height*4);
    std::memcpy(pixels->data(),&width,4);std::memcpy(pixels->data()+4,&height,4);
    const auto* base=static_cast<const uint8_t*>(target.data());
    uint8_t* out=pixels->data()+8;
    for(uint32_t y=0;y<height;++y)for(uint32_t x=0;x<width;++x,out+=4)
        std::memcpy(out,base+layout.pixel_offset(x*2,y*2,0),4);
    std::thread([pixels,processMicros]{
        char path[96];std::snprintf(path,sizeof path,"/app0/UserData/Logs/shot-%05llu.bgra",(unsigned long long)(processMicros/1000000));
        if(std::FILE* file=std::fopen(path,"wb")){std::fwrite(pixels->data(),1,pixels->size(),file);std::fclose(file);}
        mkw_log("[mkw-shot] wrote %s\n",path);
    }).detach();
}
uint32_t clear_color(const aurora::Vec4<float>& c){
    auto byte=[](float v){if(!std::isfinite(v))throw std::invalid_argument("Nonfinite frame clear color");return uint32_t(std::lround(std::clamp(v,0.f,1.f)*255));};
    return byte(c.z())|(byte(c.y())<<8)|(byte(c.x())<<16)|(byte(c.w())<<24);
}
bool same_extent(GxRenderExtent a,GxRenderExtent b){return a.logicalWidth==b.logicalWidth&&a.logicalHeight==b.logicalHeight&&a.targetWidth==b.targetWidth&&a.targetHeight==b.targetHeight;}
}
struct GxFrameRenderer::Impl {
    struct Core {
        std::shared_ptr<GpuColorTarget> color;
        std::shared_ptr<GpuDepthTarget> depth;
        GxRenderer renderer;
        GxTextureCopyBackend textures;
        GxDisplayCopyBackend display;
        static void finish(void* p){static_cast<Core*>(p)->renderer.finish();}
        // EFB texture copies only need the preceding draws submitted: their blits
        // follow them on the GPU queue, and a CPU readback of a copy waits for the
        // blits (gx_copy_readback). The display copy keeps the full wait.
        static void submit(void* p){static_cast<Core*>(p)->renderer.submit_draws();}
        Core(GxPreparedShaders s,AgcBlit& blit,GxRenderExtent e,uint16_t anisotropy)
            :color(std::make_shared<GpuColorTarget>(e.targetWidth,e.targetHeight)),depth(std::make_shared<GpuDepthTarget>(e.targetWidth,e.targetHeight)),
             renderer(s.vertex,s.pixel,color,depth,GxMemorySources::array,GxMemorySources::texture,nullptr,anisotropy),
             textures(blit,color,e,submit,this,depth),display(blit,color,depth,e,finish,this){}
    };
    GxPreparedShaders shaders;uint16_t anisotropy;
    AgcBlit blit;VideoPresenter presenter;
    std::unique_ptr<Core> core;
    GxVideoPlan plan{};GxRenderExtent previousExtent;
    PresentedFrame presented{};
    bool inFrame=false,failed=false,closed=false,disableFilter=false;
    // Periodic backend timing report: wall time inside begin..end, presentation,
    // draw submission, GPU waits, blits and texture uploads per frame.
    uint64_t frameStart=0,reportStart=0,lastEnd=0,insideNanos=0,betweenNanos=0,uploadsAtReport=0,digestsAtReport=0,skipsAtReport=0;unsigned reportFrames=0;
    // Stutter: begin-to-begin intervals of the frames in this report window.
    uint64_t nextShot=0;  // process nanoseconds of the next diagnostic capture
    // Frames whose guest float work raised the MXCSR denormal-input (DE) or
    // underflow (UE) sticky flags, and frames ending with flush-to-zero set.
    // The runtime only drives the FTZ/DAZ control bits, never these flags.
    unsigned denormalFrames=0,flushToZeroFrames=0;
    uint64_t lastBegin=0,idleAtReport=0,idleSleepAtReport=0;std::array<uint64_t,128> intervals{};unsigned intervalCount=0;
    void report(uint64_t now){
        HostPhaseScope phase(HostPhase::FrameReport);
        auto& s=gx_perf_stats();const double frames=double(reportFrames);
        const auto perFrameMs=[&](uint64_t nanos){return double(nanos)*1e-6/frames;};
        const auto perFrame=[&](uint64_t count){return double(count)/frames;};
        const uint64_t uploads=core?core->renderer.texture_uploads():0;
        const uint64_t digests=core?core->renderer.texture_digests():0;
        const uint64_t skips=core?core->renderer.texture_digest_skips():0;
        const double seconds=double(now-reportStart)*1e-9;
        char line[2000];int n=0;
        const auto put=[&](const char* label,double value,const char* unit){
            if(n>=0&&n<int(sizeof line))n+=std::snprintf(line+n,sizeof line-size_t(n),"%s %.2f%s; ",label,value,unit);};
        n=std::snprintf(line,sizeof line,"[mkw-frame-stats] %u frames %.1f fps; process-time %.1f s; ",reportFrames,frames/seconds,double(sceKernelGetProcessTime())*1e-6);
        put("frame",perFrameMs(insideNanos),"ms");
        put("outside",perFrameMs(betweenNanos),"ms");
        put("present",perFrameMs(s.presentNanos.load()),"ms");
        put("draws",perFrame(s.drawCount.load()),"/f");
        // Sampled phases scale by exact counts over timed samples.
        const auto scaled=[&](double nanos,uint64_t timed,uint64_t total){return timed?double(nanos)*double(total)/double(timed):0.0;};
        put("frontend",scaled(double(s.frontendNanos.load()),s.timedFrontends.load(),s.frontendCount.load())*1e-6/frames,"ms");
        put("build",scaled(double(s.buildNanos.load()),s.timedDraws.load(),s.drawCount.load())*1e-6/frames,"ms");
        put("submit",scaled(double(s.submitNanos.load()),s.timedDraws.load(),s.drawCount.load())*1e-6/frames,"ms");
        put("snapshot",scaled(double(s.snapshotNanos.load()),s.timedFrontends.load(),s.frontendCount.load())*1e-6/frames,"ms");
        put("build-state",scaled(double(s.stateNanos.load()),s.timedDraws.load(),s.drawCount.load())*1e-6/frames,"ms");
        put("build-serialize",scaled(double(s.serializeNanos.load()),s.timedDraws.load(),s.drawCount.load())*1e-6/frames,"ms");
        put("build-material",scaled(double(s.materialNanos.load()),s.timedDraws.load(),s.drawCount.load())*1e-6/frames,"ms");
        put("gpu-wait",perFrameMs(s.drawWaitNanos.load()),"ms");
        put("blits",perFrame(s.blitCount.load()),"/f");
        put("blit",perFrameMs(s.blitNanos.load()),"ms");put("blit-wait",perFrameMs(s.blitWaitNanos.load()),"ms");put("blit-waits",perFrame(s.blitWaits.load()),"/f");
        put("fifo-drain",perFrameMs(s.fifoNanos.load()),"ms");
        put("layout",scaled(double(s.layoutNanos.load()),s.timedFrontends.load(),s.frontendCount.load())*1e-6/frames,"ms");
        put("segments",perFrame(s.segmentCount.load()),"/f");put("snap-verts",perFrame(s.snapshotVertices.load()),"/f");
        put("snap-record-kb",perFrame(s.snapshotRecordBytes.load())/1024.0,"/f");put("snap-array-kb",perFrame(s.snapshotArrayBytes.load())/1024.0,"/f");
        put("copytex",perFrameMs(s.copyTexNanos.load()),"ms");put("copytexs",perFrame(s.copyTexCount.load()),"/f");
        put("copy-drain",perFrameMs(s.copyDrainNanos.load()),"ms");put("copy-finish",perFrameMs(s.copyFinishNanos.load()),"ms");
        put("copy-publish",perFrameMs(s.copyPublishNanos.load()),"ms");
        put("copy-overlap",perFrameMs(s.copyOverlapNanos.load()),"ms");put("copy-defer",perFrameMs(s.copyDeferNanos.load()),"ms");
        put("copy-retire",perFrameMs(s.copyRetireNanos.load()),"ms");
        put("materialize",perFrameMs(s.materializeNanos.load()),"ms");put("materializations",perFrame(s.materializeCount.load()),"/f");
        put("target-creates",perFrame(s.targetCreates.load()),"/f");put("target-create",perFrameMs(s.targetCreateNanos.load()),"ms");
        put("target-destroys",perFrame(s.targetDestroys.load()),"/f");put("target-destroy",perFrameMs(s.targetDestroyNanos.load()),"ms");
        put("blit-submit",perFrameMs(s.blitSubmitNanos.load()),"ms");put("blit-suspend",perFrameMs(s.blitSuspendNanos.load()),"ms");
        put("blit-clears",perFrame(s.blitClears.load()),"/f");put("blit-depth",perFrame(s.blitDepthCopies.load()),"/f");
        put("skip-empty",perFrame(s.skipEmptyCount.load()),"/f");
        put("skip-cull",perFrame(s.skipCullCount.load()),"/f");
        put("skip-noindex",perFrame(s.skipNoIndexCount.load()),"/f");
        put("draws-merged",perFrame(s.mergedDrawCount.load()),"/f");
        put("tex-uploads",perFrame(uploads-uploadsAtReport),"/f");
        put("tex-digests",perFrame(digests-digestsAtReport),"/f");
        put("tex-digest-skips",perFrame(skips-skipsAtReport),"/f");
        const uint64_t idle=mkw_guest_idle_nanos?mkw_guest_idle_nanos():0;
        const uint64_t idleSleep=mkw_guest_idle_sleep_nanos?mkw_guest_idle_sleep_nanos():0;
        put("guest-idle",perFrameMs(idle-idleAtReport),"ms");
        put("guest-idle-sleep",perFrameMs(idleSleep-idleSleepAtReport),"ms");
        put("guest-cpu",double(sceKernelGetCurrentCpu()),"");
        put("fp-denormal-frames",double(denormalFrames),"");put("fp-flush-frames",double(flushToZeroFrames),"");denormalFrames=flushToZeroFrames=0;
        put("log-write-max",double(mkw_log_max_write_nanos())*1e-6,"ms");put("log-dropped",double(mkw_log_dropped()),"");
        if(intervalCount){
            std::sort(intervals.begin(),intervals.begin()+intervalCount);
            const auto at=[&](double q){return double(intervals[std::min<unsigned>(intervalCount-1,unsigned(q*double(intervalCount)))])*1e-6;};
            unsigned over20=0,over34=0,over51=0;
            for(unsigned i=0;i<intervalCount;++i){const double v=double(intervals[i])*1e-6;over20+=v>20;over34+=v>34;over51+=v>51;}
            put("interval-p50",at(.5),"ms");put("interval-p95",at(.95),"ms");
            put("interval-max",double(intervals[intervalCount-1])*1e-6,"ms");
            put("frames>20ms",double(over20),"");put("frames>34ms",double(over34),"");put("frames>51ms",double(over51),"");
        }
        mkw_log("%s\n",line);
        s.reset();reportStart=now;insideNanos=betweenNanos=0;reportFrames=0;
        uploadsAtReport=uploads;digestsAtReport=digests;skipsAtReport=skips;
        idleAtReport=idle;idleSleepAtReport=idleSleep;intervalCount=0;
    }
    GxDrawSink oldDraw{};GxTextureCopyBackend* oldTexture=nullptr;GxDisplayCopyBackend* oldDisplay=nullptr;
    Impl(GxPreparedShaders s,uint16_t a):shaders(s),anisotropy(a),blit(s.copyVertex,s.copyPixel,AgcBlit::ShaderAbi::GxCopy),presenter(blit),previousExtent(current_gx_render_extent()){
        set_gx_copy_completion_wait(+[](void* p){static_cast<AgcBlit*>(p)->finish();},&blit);
    }
    ~Impl(){set_gx_copy_completion_wait(nullptr,nullptr);}
    void restore(){if(inFrame){
        exchange_gx_draw_sink(oldDraw);exchange_gx_texture_copy_backend(oldTexture);exchange_gx_display_copy_backend(oldDisplay);inFrame=false;
    }}
    bool begin(){
        require_outside_gpu_wait_callback();
        std::lock_guard lock(aurora::renderer_gpu_mutex());
        if(closed||failed||inFrame)throw std::logic_error("Invalid Aurora begin_frame lifecycle");
        HostPhaseScope phase(HostPhase::FrameBegin);
        frameStart=gpu_clock_nanos();if(!reportStart)reportStart=frameStart;if(lastEnd)betweenNanos+=frameStart-lastEnd;
        // Log long gaps between frames individually; averages hide a single stall.
        if(lastEnd&&frameStart-lastEnd>500'000'000u)
            // Process uptime places the stall against the kernel log:
            // system uptime = the EXEC timestamp klog prints + this value.
            mkw_log("[mkw-stall] %.0f ms between end_frame and begin_frame, ending at process time %.3f s\n",
                double(frameStart-lastEnd)*1e-6,double(sceKernelGetProcessTime())*1e-6);
        if(lastBegin&&intervalCount<intervals.size())intervals[intervalCount++]=frameStart-lastBegin;lastBegin=frameStart;
        // GXInit can enqueue state before VI opens the first frame. Preserve
        // that FIFO prefix: as in upstream Aurora, end() drains it in order
        // after this frame's receivers have been installed.
        auto next=snapshot_gx_video();
        try{
            if(!core||!same_extent(next.extent,plan.extent)){
                if(core)core->renderer.finish();
                auto replacement=std::make_unique<Core>(shaders,blit,next.extent,anisotropy);
                core=std::move(replacement);
            }
            plan=next;commit_gx_video_size(plan.extent);
            auto& state=aurora::gx::register_state();
            state.viewportPolicy=plan.policy;
            configure_gx_render_extent(plan.extent);
            const float z=std::min(float(state.clearDepth)/16777216.f,16777215.f/16777216.f);
            blit.fill_depth_tested(core->color,core->depth,{0,0,plan.extent.targetWidth,plan.extent.targetHeight},clear_color(state.clearColor),
                {aurora::gx::UseReversedZ?1.f-z:z,DepthCompare::Always,true},15);
            core->display.begin_frame();core->display.set_disable_filter(disableFilter);
            oldDraw=exchange_gx_draw_sink(core->renderer.sink());oldTexture=exchange_gx_texture_copy_backend(&core->textures);oldDisplay=exchange_gx_display_copy_backend(&core->display);inFrame=true;
            return true;
        }catch(...){failed=true;throw;}
    }
    void end(){
        require_outside_gpu_wait_callback();
        std::lock_guard lock(aurora::renderer_gpu_mutex());
        if(closed||failed||!inFrame)throw std::logic_error("Invalid Aurora end_frame lifecycle");
        HostPhaseScope phase(HostPhase::FrameEnd);
        try{
            {const auto drainStart=gpu_clock_nanos();if(aurora::gx::fifo::get_buffer_size())aurora::gx::fifo::drain();core->renderer.finish();gx_perf_add(gx_perf_stats().fifoNanos,gpu_clock_nanos()-drainStart);}
            std::shared_ptr<const GpuColorTarget> source=core->display.latest();if(!source)source=core->color;
            const float aspect=plan.presentAspect>0?plan.presentAspect:float(source->layout().width())/source->layout().height();
            const auto presentStart=gpu_clock_nanos();
            HostPhaseScope presentPhase(HostPhase::Present);
            // Submit without waiting for the vertical blank; the flip is retired at
            // the next frame's presentation, by which time it is normally on screen.
            presented=presenter.submit(source,aspect,true,gx_present_deadline());record_gx_present();restore();
            if(screenshots_requested()){
                const uint64_t micros=gpu_clock_nanos()/1000;  // not the ~22 us kernel clock on every frame
                if(micros*1000>=nextShot){
                    nextShot=micros*1000+kShotInterval;
                    (void)presenter.retire_pending();
                    capture_screen(presenter.current_target(),micros);
                }
            }
            const auto now=gpu_clock_nanos();gx_perf_add(gx_perf_stats().presentNanos,now-presentStart);insideNanos+=now-frameStart;lastEnd=now;
            const unsigned csr=_mm_getcsr();
            denormalFrames+=(csr&0x12u)!=0;flushToZeroFrames+=(csr&0x8040u)==0x8040u;
            if(csr&0x3fu)_mm_setcsr(csr&~0x3fu);
#if defined(MKW_PS5_RELEASE)
            // Player package: no periodic timing line, only restart the window.
            if(++reportFrames>=120){gx_perf_stats().reset();reportStart=now;insideNanos=betweenNanos=0;reportFrames=0;intervalCount=0;denormalFrames=flushToZeroFrames=0;}
#else
            if(++reportFrames>=120)report(now);
#endif
            if(mkw_fprof_frame_end)mkw_fprof_frame_end();
        }catch(...){failed=true;restore();throw;}
    }
    int close(){
        require_outside_gpu_wait_callback();
        std::lock_guard lock(aurora::renderer_gpu_mutex());if(closed)return 0;
        if(inFrame){aurora::gx::fifo::clear_buffer();restore();}
        int rc=presenter.close();if(rc<0)return rc;
        // A draw/blit exception can leave GPU work unresolved even when
        // VideoOut closes successfully. Keep all owners, including the
        // bootstrap's shaders, until process cleanup in that state.
        try{if(!failed)blit.finish();}catch(...){failed=true;return -1;}
        if(failed||blit.pending())return -1;
        try{if(core)core->renderer.finish();}catch(...){failed=true;return -1;}
        core.reset();configure_gx_render_extent(previousExtent);commit_gx_video_size(std::nullopt);closed=true;return 0;
    }
};
GxFrameRenderer::GxFrameRenderer(GxPreparedShaders s,uint16_t a){
    if(!s.vertex||!s.pixel||!s.copyVertex||!s.copyPixel||(a!=1&&a!=2&&a!=4&&a!=8&&a!=16))throw std::invalid_argument("Invalid frame renderer shaders/options");
    impl_=std::make_unique<Impl>(s,a);
}
GxFrameRenderer::~GxFrameRenderer(){if(active==this)exchange_gx_frame_renderer(nullptr);
    if(impl_&&impl_->close()<0){std::fputs("[mkw-frame] close failed; frame resources retained\n",stderr);(void)impl_.release();}}
bool GxFrameRenderer::begin_frame(){return impl_->begin();}
void GxFrameRenderer::end_frame(){impl_->end();}
void GxFrameRenderer::wait_idle() const{std::lock_guard lock(aurora::renderer_gpu_mutex());if(impl_->failed)throw std::logic_error("Frame GPU completion unresolved");impl_->blit.finish();if(impl_->core)impl_->core->renderer.finish();}
void GxFrameRenderer::set_disable_copy_filter(bool v){impl_->disableFilter=v;if(impl_->core)impl_->core->display.set_disable_filter(v);}
PresentedFrame GxFrameRenderer::last_presented() const{return impl_->presented;}
const GpuColorTarget& GxFrameRenderer::current_target() const{(void)impl_->presenter.retire_pending();return impl_->presenter.current_target();}
GxVideoPlan GxFrameRenderer::current_video_plan() const{return impl_->plan;}
int GxFrameRenderer::close(){return impl_->close();}
GxFrameRenderer* exchange_gx_frame_renderer(GxFrameRenderer* value){std::lock_guard lock(aurora::renderer_gpu_mutex());auto* old=active;active=value;return old;}
GxFrameRenderer* current_gx_frame_renderer(){std::lock_guard lock(aurora::renderer_gpu_mutex());return active;}
bool begin_active_gx_frame(){require_outside_gpu_wait_callback();std::lock_guard lock(aurora::renderer_gpu_mutex());if(!active)throw std::logic_error("Aurora requires the PS5 frame renderer at bootstrap");return active->begin_frame();}
void end_active_gx_frame(){require_outside_gpu_wait_callback();std::lock_guard lock(aurora::renderer_gpu_mutex());if(!active)throw std::logic_error("Aurora requires the PS5 frame renderer at bootstrap");active->end_frame();}
}
extern "C" bool aurora_begin_frame(){return mkw::agc::begin_active_gx_frame();}
extern "C" void aurora_end_frame(){mkw::agc::end_active_gx_frame();}
