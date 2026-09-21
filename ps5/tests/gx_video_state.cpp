// SPDX-License-Identifier: GPL-3.0-only
#include "gx_video_state.h"
#include <cstdio>
#include <cmath>
#include <limits>
#include <stdexcept>
using namespace mkw::agc;
int main(){try{
    unsigned checks=0;auto check=[&](bool v){++checks;if(!v)throw std::runtime_error("VI video plan mismatch");};
    auto rejects=[&](auto f){bool failed=false;try{f();}catch(const std::exception&){failed=true;}check(failed);};
    GxVideoSettings s;auto p=plan_gx_video(s);
    check(p.extent.logicalWidth==640&&p.extent.logicalHeight==528&&p.extent.targetWidth==1309&&p.extent.targetHeight==1080);
    s.scale=1;p=plan_gx_video(s);check(p.extent.targetWidth==640&&p.extent.targetHeight==528&&p.presentAspect==0);
    s.scale=2;p=plan_gx_video(s);check(p.extent.targetWidth==1280&&p.extent.targetHeight==1056);
    s.scale=.5f;p=plan_gx_video(s);check(p.extent.targetWidth==320&&p.extent.targetHeight==264);
    s.policy=AURORA_VIEWPORT_STRETCH;s.scale=1;p=plan_gx_video(s);check(p.extent.targetWidth==939&&p.extent.targetHeight==528&&p.presentAspect==16.f/9);
    s.scale=0;p=plan_gx_video(s);check(p.extent.targetWidth==1920&&p.extent.targetHeight==1080);
    s.policy=AURORA_VIEWPORT_FIT;s.scale=1;s.aspectWidth=4;s.aspectHeight=3;
    GXRenderModeObj mode{};mode.viTVmode=VI_TVMODE_PAL_INT;mode.fbWidth=608;mode.efbHeight=480;mode.viWidth=702;mode.viHeight=576;s.mode=mode;
    p=plan_gx_video(s);check(p.extent.logicalWidth==640&&p.extent.logicalHeight==528);check(p.aspectCorrection==1&&p.presentAspect==4.f/3);
    mode.fbWidth=720;mode.efbHeight=576;s.mode=mode;p=plan_gx_video(s);check(p.extent.targetWidth==720&&p.extent.targetHeight==576);
    mode.viWidth=640;mode.viHeight=480;mode.viTVmode=VI_TVMODE_NTSC_INT;s.mode=mode;p=plan_gx_video(s);
    check(std::abs(p.aspectCorrection-(640.f/(858.f*(52.655555f/63.555555f)))/(480.f/486.f))<.000001f);
    mode.viWidth=0;s.mode=mode;p=plan_gx_video(s);check(p.aspectCorrection==1);
    s.scale=64;p=plan_gx_video(s);check(p.extent.targetWidth<=8192&&p.extent.targetHeight<=8192&&uint64_t(p.extent.targetWidth)*p.extent.targetHeight<=7680ull*4320);
    s.scale=std::numeric_limits<float>::quiet_NaN();rejects([&]{plan_gx_video(s);});
    s.scale=-1;rejects([&]{plan_gx_video(s);});s.scale=1;s.surfaceWidth=0;rejects([&]{plan_gx_video(s);});
    std::printf("PASS %u VI render-plan checks: EFB workspace, auto/fixed scale, stretch, aspect correction and limits\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
