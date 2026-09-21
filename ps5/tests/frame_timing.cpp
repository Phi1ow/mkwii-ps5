// SPDX-License-Identifier: GPL-3.0-only
#include "present_history.h"
#include "gpu_wait_service.h"
#include <cmath>
#include <cstdio>
#include <stdexcept>
namespace {unsigned callbacks=0,sleeps=0,lastSleep=0;int sleepError=0;
void service(){++callbacks;}
void recursive(){mkw::agc::gpu_wait_slice(0);}
void failing(){throw std::runtime_error("callback test");}}
extern "C" int sceKernelUsleep(unsigned us){++sleeps;lastSleep=us;return sleepError;}
int main(){try{
    using namespace mkw::agc;unsigned checks=0;
    auto check=[&](bool value){if(!value)throw std::runtime_error("Timing check failed");++checks;};
    auto near=[&](double a,double b){check(std::abs(a-b)<1e-8);};
    auto reject=[&](auto f){bool threw=false;try{f();}catch(const std::exception&){threw=true;}check(threw);};
    check(native_present_deadline(1000000000,20000000)==986500000);
    check(native_present_deadline(0,1)==0&&native_present_deadline(1,0)==0);
    reject([]{native_present_deadline(1,2);});reject([]{native_present_deadline(uint64_t(INT64_MAX),1);});
    PresentHistory history;auto t=history.snapshot(0);check(t.totalPresentCount==0&&t.sampleCount==0);
    history.record(0);history.record(10000000);history.record(30000000);history.record(60000000);
    t=history.snapshot(60000000);check(t.totalPresentCount==4&&t.sampleCount==3);near(t.averageFrameTimeMs,20);near(t.framesPerSecond,50);near(t.effectiveFramesPerSecond,50);near(t.p95FrameTimeMs,20);near(t.jitterMs,std::sqrt(200./3));
    t=history.snapshot(1060000001);check(t.sampleCount==0&&t.totalPresentCount==4&&t.framesPerSecond==0);
    reject([&]{history.record(1);});check(history.snapshot(60000000).totalPresentCount==4);
    history={};for(unsigned i=0;i<601;++i)history.record(uint64_t(i)*1000000);
    t=history.snapshot(600000000);check(t.totalPresentCount==601&&t.sampleCount==512);near(t.averageFrameTimeMs,1);near(t.jitterMs,0);
    set_gpu_wait_callback(service);gpu_wait_slice(9999);check(callbacks==1&&sleeps==1&&lastSleep==1000);
    gpu_wait_slice(0);check(callbacks==2&&sleeps==1);
    set_gpu_wait_callback(recursive);reject([]{gpu_wait_slice(0);});
    set_gpu_wait_callback(failing);reject([]{gpu_wait_slice(0);});
    set_gpu_wait_callback(service);gpu_wait_slice(0);check(callbacks==3);
    set_gpu_wait_callback(nullptr);gpu_wait_slice(10);check(callbacks==3&&lastSleep==10);
    sleepError=-1;reject([]{gpu_wait_slice(10);});sleepError=0;
    const auto before=sleeps;gpu_wait_until(gpu_clock_nanos());check(sleeps==before);
    std::printf("PASS %u presentation history/deadline and bounded wait-service checks\n",checks);
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
