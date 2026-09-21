// SPDX-License-Identifier: GPL-3.0-only
#include "gx_frame_timing.h"
#include "gx_frame_renderer.h"
#include "gpu_wait_service.h"
#include "present_history.h"
#include "gx/register_backend.hpp"
#include <chrono>
#include <algorithm>
#include <mutex>
#include <cstdio>
#include <stdexcept>
namespace {
std::mutex timingMutex;
mkw::agc::PresentHistory history;
uint64_t presentDeadline=0;
bool producerPaced=false,skipUnready=false;
}
namespace mkw::agc {
void reset_gx_frame_timing(){std::lock_guard lock(timingMutex);history={};presentDeadline=0;producerPaced=false;skipUnready=false;}
uint64_t gx_present_deadline(){std::lock_guard lock(timingMutex);return presentDeadline;}
void record_gx_present(){std::lock_guard lock(timingMutex);history.record(gpu_clock_nanos());}
}
extern "C" {
void aurora_set_frame_worker_wait_callback(AuroraFrameWorkerWaitCallback value){mkw::agc::set_gpu_wait_callback(value);}
void aurora_set_present_schedule(uint64_t base,uint64_t interval){
    mkw::agc::require_outside_gpu_wait_callback();const auto deadline=mkw::agc::native_present_deadline(base,interval);
    std::lock_guard lock(timingMutex);presentDeadline=deadline;
}
void aurora_report_producer_paced(bool value){std::lock_guard lock(timingMutex);producerPaced=value;}
void aurora_get_present_timing(AuroraPresentTiming* timing){if(!timing)return;std::lock_guard lock(timingMutex);*timing=history.snapshot(mkw::agc::gpu_clock_nanos());}
// The PS5 renderer currently submits synchronously. There is no separate
// encoding worker to join, but an ongoing native operation must be excluded
// and unresolved GPU work must never be reported as completed.
bool aurora_wait_for_frame_worker_for(uint32_t micros){
    using namespace mkw::agc;require_outside_gpu_wait_callback();
    const auto start=gpu_clock_nanos(),budget=uint64_t(micros)*1000;
    std::unique_lock lock(aurora::renderer_gpu_mutex(),std::defer_lock);
    while(!lock.try_lock()){
        const auto elapsed=gpu_clock_nanos()-start;if(elapsed>=budget)return false;
        gpu_wait_slice(unsigned(std::clamp<uint64_t>((budget-elapsed)/1000,1,1000)));
    }
    if(auto* renderer=current_gx_frame_renderer())renderer->wait_idle();return true;
}
void aurora_wait_for_frame_worker(){
    while(!aurora_wait_for_frame_worker_for(1000)){}
}
void aurora_set_frame_interpolation_fps(uint32_t fps){if(fps)throw std::invalid_argument("AGC transform interpolation is not implemented");}
uint32_t aurora_get_frame_interpolation_fps(){return 0;}
void aurora_set_skip_unready_pipelines(bool value){std::lock_guard lock(timingMutex);skipUnready=value;}
bool aurora_get_skip_unready_pipelines(){std::lock_guard lock(timingMutex);return skipUnready;}
// Both native shaders are prepared at bootstrap. There is no asynchronous
// shader compiler queue, so this reports its actual empty state.
uint32_t aurora_get_queued_pipeline_count(){return 0;}
}
namespace aurora {
// Upstream Aurora joins only the frame worker's SEALED phase here: the CPU-side
// sealing of the previous frame, never GPU completion. Every FIFO drain calls
// it, i.e. every immediate-mode GXEnd (each text glyph and layout quad). The
// PS5 renderer records synchronously, so that phase is always complete; GPU
// completion is joined by the readers of a target (AgcGxDraw::finish) and by
// the DONE wait of aurora_wait_for_frame_worker. Joining DONE here submitted
// the open draw batch and waited for the GPU on every drain: 62 % of a menu
// frame after the licence screen (artifacts/game-native/99611-*, PORTAGE §75).
std::chrono::nanoseconds wait_for_frame_worker_sealed() noexcept{
    try{mkw::agc::require_outside_gpu_wait_callback();}
    catch(const std::exception& e){std::fprintf(stderr,"[mkw-timing] FIFO drain inside a GPU wait callback: %s\n",e.what());std::terminate();}
    return std::chrono::nanoseconds::zero();
}
}
