// SPDX-License-Identifier: GPL-3.0-only
#include "video_presenter.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace mkw::agc {
BlitRect fit_present_rect(uint32_t width,uint32_t height,float aspect){
    if(!width||!height||width>16384||height>16384||!std::isfinite(aspect)||aspect<=0)
        throw std::invalid_argument("Invalid presentation size/aspect");
    // Same fitting/centering as WiiCompiled calculate_present_viewport_for_aspect.
    // Clamp in floating point before narrowing to avoid overflow for tiny ratios.
    auto extent=[](double value,uint32_t maximum){return uint32_t(std::clamp(std::round(value),1.0,double(maximum)));};
    uint32_t w=width,h=extent(double(w)*double(1.f/aspect),height);
    if(h==height)w=extent(double(h)*double(aspect),width);
    return {(width-w)/2,(height-h)/2,w,h};
}
}
