#include "emu/hana_contract.hpp"
#include "emu/realtime_contract.hpp"
#include "emu/transport_contract.hpp"

#include <cstdlib>
#include <iostream>

int main() {
    constexpr std::uint64_t kProbePeriods = 100'000;
    const auto hana_read = emu1820::make_hana_read_transaction(
        static_cast<std::uint8_t>(emu1820::HanaRegister::identity));
    const auto hana_probe = emu1820::validate_hana_probe_snapshot({
        emu1820::kHanaExpectedAlice2Identity,
        1,
        0,
        emu1820::kHanaOptionDockOnline,
        1,
        0,
        0,
    });
    if (!hana_read ||
        !emu1820::validate_hana_transaction(hana_read.transaction.view()) ||
        !hana_probe) {
        return EXIT_FAILURE;
    }

    emu1820::FullDuplexEngine engine;
    if (!engine.prepare({48'000, 18, 20, 4, 64, 8},
                        emu1820::ClockSource::internal)) {
        return EXIT_FAILURE;
    }
    engine.report_clock(true, 48'000);
    if (!engine.start()) {
        return EXIT_FAILURE;
    }

    emu1820::CaptureFanout<64> fanout;
    emu1820::MonitorFailSafe monitor{2};
    monitor.arm_wet_path();

    for (std::uint64_t sequence = 0; sequence < kProbePeriods; ++sequence) {
        const auto period = engine.service_period(sequence, sequence);
        if (!period.valid) {
            return EXIT_FAILURE;
        }
        fanout.publish_dry_capture(period.capture);

        // Simulate a VST monitor worker that stops responding halfway through.
        if (sequence < kProbePeriods / 2) {
            monitor.report_wet_heartbeat(sequence);
            emu1820::AudioBlockToken mirrored{};
            (void)fanout.try_pop_monitor(mirrored);
        }
        (void)monitor.select_path(sequence);
    }

    const auto state = engine.capture_continuity();
    const auto counters = engine.counters();
    std::cout << "EMU1820M-Next CI engine probe (no hardware access)\n"
              << "blocks=" << state.blocks << " frames=" << state.frames
              << " discontinuities=" << state.discontinuities
              << " capture_xruns=" << counters.capture_xruns
              << " render_xruns=" << counters.render_xruns
              << " monitor_drops=" << fanout.monitor_drops()
              << " dry_fallbacks=" << monitor.fallback_count()
              << " hana_probe_ops=" << hana_read.transaction.count << '\n';

    return state.discontinuities == 0 && counters.capture_xruns == 0 &&
                   counters.render_xruns == 0 &&
                   fanout.dry_blocks() == kProbePeriods
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
}
