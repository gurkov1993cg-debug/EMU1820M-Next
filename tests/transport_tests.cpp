#include "emu/transport_contract.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace {

[[noreturn]] void fail(const char* message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void require(const bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

void test_exact_hardware_identity() {
    constexpr emu1820::PciIdentity emu1010{
        0x1102,
        0x0004,
        0x1102,
        0x4001,
        0x04,
    };
    static_assert(
        emu1820::identify_hardware(emu1010) ==
        emu1820::HardwareProfile::emu1010_hana_maem8810);

    auto wrong_device = emu1010;
    wrong_device.device_id = 0x0008;
    require(emu1820::identify_hardware(wrong_device) ==
                emu1820::HardwareProfile::unsupported,
            "a different Creative PCI device was accepted");

    auto wrong_subsystem = emu1010;
    wrong_subsystem.subsystem_device_id = 0x4002;
    require(emu1820::identify_hardware(wrong_subsystem) ==
                emu1820::HardwareProfile::unsupported,
            "a non-Hana subsystem was accepted");
}

void test_transport_sizing() {
    require(!static_cast<bool>(emu1820::TransportContract{}),
            "unconfigured transport reported ready");

    const emu1820::FullDuplexConfig config{
        48'000,
        18,
        20,
        4,
        128,
        8,
    };
    const auto contract = emu1820::make_transport_contract(config);
    require(static_cast<bool>(contract), "valid 48 kHz contract was rejected");
    require(contract.capture_period_bytes == 9'216,
            "capture period byte count is wrong");
    require(contract.render_period_bytes == 10'240,
            "render period byte count is wrong");
    require(contract.capture_ring_bytes == 73'728,
            "capture ring byte count is wrong");
    require(contract.render_ring_bytes == 81'920,
            "render ring byte count is wrong");

    auto invalid = config;
    invalid.sample_rate = 96'000;
    require(emu1820::make_transport_contract(invalid).error ==
                emu1820::TransportError::unsupported_sample_rate,
            "96 kHz was accepted as a full-channel mode");

    invalid = config;
    invalid.capture_channels = 16;
    require(emu1820::make_transport_contract(invalid).error ==
                emu1820::TransportError::channel_count_mismatch,
            "wrong capture channel count was accepted");

    invalid = config;
    invalid.sample_container_bytes = 3;
    require(emu1820::make_transport_contract(invalid).error ==
                emu1820::TransportError::unsupported_sample_container,
            "packed 24-bit transport was accepted");

    invalid = config;
    invalid.frames_per_period = 96;
    require(emu1820::make_transport_contract(invalid).error ==
                emu1820::TransportError::invalid_period_frames,
            "non-power-of-two period was accepted");

    invalid = config;
    invalid.period_count = 1;
    require(emu1820::make_transport_contract(invalid).error ==
                emu1820::TransportError::invalid_period_count,
            "single-period ring was accepted");
}

void test_dma_ring_wrap() {
    const auto contract = emu1820::make_transport_contract({
        44'100,
        18,
        20,
        4,
        64,
        4,
    });
    emu1820::DmaRingLayout capture;
    emu1820::DmaRingLayout render;
    require(capture.configure(contract, emu1820::DmaDirection::capture),
            "capture layout configuration failed");
    require(render.configure(contract, emu1820::DmaDirection::render),
            "render layout configuration failed");

    const auto capture0 = capture.period_for_sequence(0);
    const auto capture3 = capture.period_for_sequence(3);
    const auto capture4 = capture.period_for_sequence(4);
    require(capture0.ring_index == 0 && capture0.byte_offset == 0,
            "first capture period is misplaced");
    require(capture3.ring_index == 3 &&
                capture3.byte_offset == 3 * contract.capture_period_bytes,
            "last capture period is misplaced");
    require(capture4.ring_index == 0 && capture4.byte_offset == 0,
            "capture ring did not wrap");
    require(capture4.first_sample_position == 256,
            "capture sample timeline changed at ring wrap");

    const auto render4 = render.period_for_sequence(4);
    require(render4.ring_index == 0 && render4.byte_count == 5'120,
            "render ring layout is wrong");
}

void test_full_duplex_stability_contract() {
    constexpr std::uint64_t kPeriods = 250'000;
    emu1820::FullDuplexEngine engine;
    require(engine.prepare({48'000, 18, 20, 4, 64, 8},
                           emu1820::ClockSource::internal),
            "full-duplex prepare failed");
    require(!engine.start(), "stream started without a confirmed clock");

    engine.report_clock(true, 48'000);
    require(engine.clock_locked(), "valid internal clock did not lock");
    require(engine.start(), "locked stream did not start");

    for (std::uint64_t sequence = 0; sequence < kPeriods; ++sequence) {
        const auto period = engine.service_period(sequence, sequence);
        require(period.valid, "valid full-duplex period was rejected");
        require(period.capture.channels == 18, "capture channel count changed");
        require(period.render.channels == 20, "render channel count changed");
    }

    const auto counters = engine.counters();
    require(counters.periods_serviced == kPeriods, "period count changed");
    require(counters.capture_xruns == 0, "capture xrun appeared");
    require(counters.render_xruns == 0, "render xrun appeared");
    require(counters.duplex_phase_errors == 0, "duplex phase drift appeared");
    require(engine.capture_continuity().discontinuities == 0,
            "capture sample timeline broke");
    require(engine.render_continuity().discontinuities == 0,
            "render sample timeline broke");
}

void test_xrun_and_clock_loss_reporting() {
    emu1820::FullDuplexEngine cold_engine;
    cold_engine.stop();
    require(cold_engine.state() == emu1820::EngineState::cold,
            "unconfigured engine left the cold state");

    emu1820::FullDuplexEngine engine;
    require(engine.prepare({44'100, 18, 20, 4, 128, 8},
                           emu1820::ClockSource::word_clock),
            "external-clock prepare failed");
    engine.report_clock(true, 44'100);
    require(engine.start(), "external-clock stream did not start");

    require(engine.service_period(0, 0).valid, "first period failed");
    const auto skipped = engine.service_period(2, 2);
    require(skipped.capture_discontinuity && skipped.render_discontinuity,
            "skipped period was not reported");

    const auto phase_error = engine.service_period(3, 4);
    require(phase_error.duplex_phase_error, "duplex phase error was hidden");
    require(engine.counters().capture_xruns == 1, "capture xrun count is wrong");
    require(engine.counters().render_xruns == 2, "render xrun count is wrong");
    require(engine.counters().duplex_phase_errors == 1,
            "duplex phase error count is wrong");

    engine.report_clock(false, 0);
    require(engine.state() == emu1820::EngineState::clock_fault,
            "clock loss did not fault the stream");
    require(engine.counters().clock_losses == 1, "clock loss count is wrong");
    require(!engine.service_period(4, 5).valid,
            "audio continued after clock validity was lost");
}

void test_monitor_backpressure_never_blocks_dry_capture() {
    constexpr std::uint64_t kPeriods = 10'000;
    emu1820::CaptureFanout<8> fanout;

    for (std::uint64_t sequence = 0; sequence < kPeriods; ++sequence) {
        fanout.publish_dry_capture({sequence, sequence * 64, 64, 18});
    }

    require(fanout.dry_blocks() == kPeriods,
            "monitor backpressure stopped dry capture");
    require(fanout.dry_continuity().blocks == kPeriods,
            "dry capture ledger lost blocks");
    require(fanout.dry_continuity().discontinuities == 0,
            "dry capture timeline broke while monitor was stalled");
    require(fanout.monitor_drops() == kPeriods - 7,
            "monitor-only drop accounting is wrong");

    emu1820::AudioBlockToken block{};
    std::uint64_t drained{};
    while (fanout.try_pop_monitor(block)) {
        ++drained;
    }
    require(drained == 7, "monitor queue capacity contract changed");
}

}  // namespace

int main() {
    test_exact_hardware_identity();
    test_transport_sizing();
    test_dma_ring_wrap();
    test_full_duplex_stability_contract();
    test_xrun_and_clock_loss_reporting();
    test_monitor_backpressure_never_blocks_dry_capture();
    std::cout << "EMU1820M transport contract tests passed\n";
    return EXIT_SUCCESS;
}
