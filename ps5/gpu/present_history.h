// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <aurora/gfx.h>
#include <array>
namespace mkw::agc {
uint64_t native_present_deadline(uint64_t base,uint64_t interval);
class PresentHistory {
public:
    void record(uint64_t nanos);
    AuroraPresentTiming snapshot(uint64_t now) const;
private:
    struct Sample{uint64_t at=0,interval=0;};
    std::array<Sample,512> samples_{};
    size_t write_=0,count_=0;uint64_t total_=0,last_=0;
};
}
