#include "emu/realtime_contract.hpp"

#include <cstdlib>
#include <iostream>

int main() {
    emu1820::StreamContinuityTracker tracker;
    emu1820::MonitorFailSafe monitor{2};
    monitor.arm_wet_path();

    for (std::uint64_t block = 0; block < 1'000; ++block) {
        tracker.observe({block, block * 128, 128, emu1820::kFullInputChannels});
        if (block < 500) {
            monitor.report_wet_heartbeat(block);
        }
        (void)monitor.select_path(block);
    }

    const auto state = tracker.snapshot();
    std::cout << "EMU1820M-Next CI engine probe (no hardware access)\n"
              << "blocks=" << state.blocks << " frames=" << state.frames
              << " discontinuities=" << state.discontinuities
              << " dry_fallbacks=" << monitor.fallback_count() << '\n';

    return state.discontinuities == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

