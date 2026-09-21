// SPDX-License-Identifier: GPL-3.0-only
// Host checks for the diagnostic pad autopilot: script parsing, rejection,
// timing, press merging and the cursor used on the input thread.
#include "pad_autopilot.h"
#include <cstdio>
#include <stdexcept>
#include <string>

using namespace mkw::input;
static unsigned checks;
static void check(bool v, int line = __builtin_LINE()) {
    ++checks;
    if (!v) throw std::runtime_error("autopilot check failed at line " + std::to_string(line));
}
static bool rejected(std::string_view text) { return !parse_autopilot_script(text).error.empty(); }

int main() { try {
    // Button names and combinations use the native DualSense bits.
    check(autopilot_button("cross") == 0x4000 && autopilot_button("circle") == 0x2000);
    check(autopilot_button("up") == 0x10 && autopilot_button("options") == 0x8);
    check(autopilot_button("Cross") == 0 && autopilot_button("") == 0);

    // Single presses, comments, blank lines, default and explicit hold.
    {
        const auto s = parse_autopilot_script("# boot\n\n  30 cross\n31.5 down+cross 200  # combo\r\n");
        check(s.error.empty() && s.presses.size() == 2);
        check(s.presses[0] == AutopilotPress{30000000, 30120000, 0x4000});
        check(s.presses[1] == AutopilotPress{31500000, 31700000, 0x4040});
    }
    // Presses are sorted by start whatever the file order.
    {
        const auto s = parse_autopilot_script("40 circle\n10 cross\n");
        check(s.error.empty() && s.presses[0].buttons == 0x4000 && s.presses[1].buttons == 0x2000);
    }
    // repeat expands a periodic press over [start, end).
    {
        const auto s = parse_autopilot_script("repeat 10 12 500 cross 100\n");
        check(s.error.empty() && s.presses.size() == 4);
        check(s.presses[0] == AutopilotPress{10000000, 10100000, 0x4000});
        check(s.presses[3] == AutopilotPress{11500000, 11600000, 0x4000});
    }
    // Any invalid line rejects the whole script, so a typo never half-drives the game.
    check(rejected("10 jump\n"));
    check(rejected("ten cross\n"));
    check(rejected("10 cross 0\n"));
    check(rejected("10 cross 100 extra\n"));
    check(rejected("-1 cross\n"));
    check(rejected("10 cross+\n"));
    check(rejected("repeat 12 10 500 cross\n"));
    check(rejected("repeat 10 12 100 cross 100\n"));   // hold must be shorter than period
    check(rejected("repeat 10 12 500\n"));
    check(rejected("30 cross\nbogus\n"));
    {
        const auto bad = parse_autopilot_script("30 cross\n40 nope\n");
        check(bad.presses.empty() && bad.error.find("line 2") != std::string::npos);
    }

    // Timing relative to the first apply(), merged with the physical pad.
    {
        PadAutopilot pilot;
        pilot.load(parse_autopilot_script("1 cross 200\n1.1 up 50\n3 circle\n"));
        check(pilot.enabled());
        PadSample sample{}; sample.buttons = 0x800;   // physical R1 held
        const uint64_t origin = 5000000000ull;
        check(pilot.apply(sample, origin) == 0);
        check(sample.connected && sample.buttons == 0x800);
        sample = {}; check(pilot.apply(sample, origin + 1000000) == 0x4000);
        sample = {}; check(pilot.apply(sample, origin + 1120000) == 0x4010);   // overlap
        sample = {}; check(pilot.apply(sample, origin + 1160000) == 0x4000);   // up released
        sample = {}; check(pilot.apply(sample, origin + 1200000) == 0);        // cross released at end
        sample = {}; check(pilot.apply(sample, origin + 3050000) == 0x2000);
        sample = {}; check(pilot.apply(sample, origin + 9000000) == 0 && sample.connected);
        // buttons_at matches apply for the same instants.
        check(pilot.buttons_at(1120000) == 0x4010 && pilot.buttons_at(3050000) == 0x2000);
    }

    // The cursor keeps a long, early hold active while later short presses pass.
    {
        PadAutopilot pilot;
        pilot.load(parse_autopilot_script("0 cross 10000\nrepeat 1 5 1000 up 100\n"));
        PadSample sample{};
        pilot.apply(sample, 0);
        for (uint64_t t = 0; t <= 6000000; t += 50000) {
            sample = {};
            const uint32_t held = pilot.apply(sample, t);
            check(held == pilot.buttons_at(t));
        }
    }

    // A rejected or empty script leaves the pilot off and the sample untouched.
    {
        PadAutopilot pilot;
        pilot.load(parse_autopilot_script("10 nope\n"));
        PadSample sample{};
        check(!pilot.enabled() && pilot.apply(sample, 10000000) == 0 && !sample.connected);
        pilot.load(parse_autopilot_script("# nothing\n"));
        check(!pilot.enabled());
    }

    std::printf("PASS pad autopilot: %u checks, parsing, rejection, timing, merge and cursor\n", checks);
    return 0;
} catch (const std::exception& e) { std::fprintf(stderr, "FAIL after %u: %s\n", checks, e.what()); return 1; } }
