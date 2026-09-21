// SPDX-License-Identifier: GPL-3.0-only
#include "gpu_wait_service.h"
#include <atomic>
#include <chrono>
#include <algorithm>
#include <stdexcept>
extern "C" int sceKernelUsleep(unsigned);
namespace mkw::agc {
namespace {std::atomic<GpuWaitCallback> callback{nullptr};thread_local bool servicing=false;}
void set_gpu_wait_callback(GpuWaitCallback value){require_outside_gpu_wait_callback();callback.store(value,std::memory_order_release);}
void require_outside_gpu_wait_callback(){if(servicing)throw std::logic_error("Guest timing callback cannot reenter Aurora");}
uint64_t gpu_clock_nanos(){return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
void gpu_wait_slice(unsigned micros){
    require_outside_gpu_wait_callback();
    if(const auto f=callback.load(std::memory_order_acquire)){
        struct Guard{Guard(){servicing=true;}~Guard(){servicing=false;}} guard;
        f();
    }
    if(micros&&sceKernelUsleep(std::min(micros,1000u))<0)throw std::runtime_error("GPU wait sleep failed");
}
void gpu_wait_until(uint64_t deadline){
    require_outside_gpu_wait_callback();
    for(;;){const auto now=gpu_clock_nanos();if(now>=deadline)return;
        gpu_wait_slice(unsigned(std::clamp<uint64_t>((deadline-now)/1000,1,1000)));
    }
}
}
