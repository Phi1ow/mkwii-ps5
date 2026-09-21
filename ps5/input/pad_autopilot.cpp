// SPDX-License-Identifier: GPL-3.0-only
#include "pad_autopilot.h"
#include <algorithm>
#include <cmath>

namespace mkw::input {
namespace {
// Native bit layout shared with virtual_pad.cpp (SharpProspero Pad.cs).
struct Named { std::string_view name; uint32_t bits; };
constexpr Named kButtons[] = {
    {"cross", 0x4000}, {"circle", 0x2000}, {"square", 0x8000}, {"triangle", 0x1000},
    {"options", 0x8}, {"l3", 0x2}, {"r3", 0x4}, {"l1", 0x400}, {"r1", 0x800},
    {"up", 0x10}, {"down", 0x40}, {"left", 0x80}, {"right", 0x20},
};
constexpr uint64_t kDefaultHoldMicros = 120000;
constexpr uint64_t kMaxSeconds = 24 * 3600;
constexpr size_t kMaxPresses = 100000;

std::vector<std::string_view> split(std::string_view line) {
    std::vector<std::string_view> out;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r')) ++i;
        size_t j = i;
        while (j < line.size() && line[j] != ' ' && line[j] != '\t' && line[j] != '\r') ++j;
        if (j > i) out.push_back(line.substr(i, j - i));
        i = j;
    }
    return out;
}
// Plain "123" or "12.5". Hand-written because LLVM 18 libc++ (the PS5 build)
// lacks floating-point std::from_chars, and strtod would depend on locale.
bool number(std::string_view text, double& out) {
    if (text.empty() || text.size() > 16) return false;
    double whole = 0, fraction = 0, scale = 1;
    bool dot = false, digits = false;
    for (const char c : text) {
        if (c == '.') { if (dot) return false; dot = true; continue; }
        if (c < '0' || c > '9') return false;
        digits = true;
        if (dot) { scale /= 10; fraction += (c - '0') * scale; }
        else whole = whole * 10 + (c - '0');
    }
    if (!digits || text.back() == '.' || text.front() == '.') return false;
    out = whole + fraction;
    return out <= double(kMaxSeconds) * 1000;
}
bool buttons(std::string_view text, uint32_t& out) {
    out = 0;
    size_t i = 0;
    while (i <= text.size()) {
        const size_t plus = text.find('+', i);
        const auto token = text.substr(i, plus == std::string_view::npos ? std::string_view::npos : plus - i);
        const uint32_t bits = autopilot_button(token);
        if (!bits) return false;
        out |= bits;
        if (plus == std::string_view::npos) break;
        i = plus + 1;
    }
    return out != 0;
}
uint64_t micros(double seconds) { return uint64_t(std::llround(seconds * 1e6)); }
}

uint32_t autopilot_button(std::string_view name) noexcept {
    for (const auto& b : kButtons) if (b.name == name) return b.bits;
    return 0;
}

AutopilotScript parse_autopilot_script(std::string_view text) {
    AutopilotScript script;
    size_t lineNo = 0, at = 0;
    const auto fail = [&](const char* why) {
        script.presses.clear();
        script.error = "line " + std::to_string(lineNo) + ": " + why;
        return script;
    };
    while (at <= text.size()) {
        const size_t end = text.find('\n', at);
        auto line = text.substr(at, end == std::string_view::npos ? std::string_view::npos : end - at);
        ++lineNo;
        if (const size_t hash = line.find('#'); hash != std::string_view::npos) line = line.substr(0, hash);
        const auto words = split(line);
        if (!words.empty()) {
            if (words[0] == "repeat") {
                // repeat <start s> <end s> <period ms> <buttons> [hold ms]
                double start = 0, stop = 0, period = 0, hold = kDefaultHoldMicros / 1000.0;
                uint32_t bits = 0;
                if (words.size() < 5 || words.size() > 6 || !number(words[1], start) || !number(words[2], stop) ||
                    !number(words[3], period) || !buttons(words[4], bits) ||
                    (words.size() == 6 && !number(words[5], hold)))
                    return fail("expected: repeat <start s> <end s> <period ms> <buttons> [hold ms]");
                if (stop <= start || period <= 0 || hold <= 0 || hold >= period)
                    return fail("repeat needs end > start and 0 < hold < period");
                for (double t = start; t < stop; t += period / 1000.0) {
                    if (script.presses.size() >= kMaxPresses) return fail("too many presses");
                    const uint64_t s = micros(t);
                    script.presses.push_back({s, s + uint64_t(std::llround(hold * 1000)), bits});
                }
            } else {
                double start = 0, hold = kDefaultHoldMicros / 1000.0;
                uint32_t bits = 0;
                if (words.size() < 2 || words.size() > 3 || !number(words[0], start) || !buttons(words[1], bits) ||
                    (words.size() == 3 && !number(words[2], hold)) || hold <= 0)
                    return fail("expected: <start s> <buttons> [hold ms]");
                if (start > double(kMaxSeconds)) return fail("start beyond 24 hours");
                if (script.presses.size() >= kMaxPresses) return fail("too many presses");
                const uint64_t s = micros(start);
                script.presses.push_back({s, s + uint64_t(std::llround(hold * 1000)), bits});
            }
        }
        if (end == std::string_view::npos) break;
        at = end + 1;
    }
    std::sort(script.presses.begin(), script.presses.end(),
              [](const AutopilotPress& a, const AutopilotPress& b) { return a.startMicros < b.startMicros; });
    return script;
}

void PadAutopilot::load(AutopilotScript script) {
    presses_ = script.error.empty() ? std::move(script.presses) : std::vector<AutopilotPress>{};
    started_ = false;
    origin_ = 0;
    cursor_ = 0;
}

uint32_t PadAutopilot::buttons_at(uint64_t elapsed) const noexcept {
    uint32_t held = 0;
    // Presses are sorted by start; stop scanning once they begin in the future.
    for (const auto& p : presses_) {
        if (p.startMicros > elapsed) break;
        if (elapsed < p.endMicros) held |= p.buttons;
    }
    return held;
}

uint32_t PadAutopilot::apply(PadSample& sample, uint64_t now) noexcept {
    if (presses_.empty()) return 0;
    if (!started_) { origin_ = now; started_ = true; cursor_ = 0; }
    const uint64_t elapsed = now >= origin_ ? now - origin_ : 0;
    // Skip presses that are over; a still-held earlier press keeps the cursor
    // in place, so overlapping presses stay correct, just scanned a little more.
    while (cursor_ < presses_.size() && presses_[cursor_].endMicros <= elapsed) ++cursor_;
    uint32_t held = 0;
    for (size_t i = cursor_; i < presses_.size() && presses_[i].startMicros <= elapsed; ++i)
        if (elapsed < presses_[i].endMicros) held |= presses_[i].buttons;
    // The game only reads a connected pad; keep slot 0 present for the whole
    // script even when the DualSense is off.
    sample.connected = true;
    sample.intercepted = false;
    sample.buttons |= held;
    return held;
}
}
