#include "emu/realtime_contract.hpp"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <thread>

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

void test_format_contract() {
    require(emu1820::kFullInputChannels == 18, "full input channel contract changed");
    require(emu1820::kFullOutputChannels == 20, "full output channel contract changed");
    require(emu1820::is_full_channel_sample_rate(44'100), "44.1 kHz rejected");
    require(emu1820::is_full_channel_sample_rate(48'000), "48 kHz rejected");
    require(!emu1820::is_full_channel_sample_rate(96'000),
            "96 kHz incorrectly accepted as full-channel mode");
}

void test_queue_boundaries() {
    emu1820::SpscQueue<emu1820::AudioBlockToken, 8> queue;
    emu1820::AudioBlockToken token{};
    require(!queue.try_pop(token), "empty queue returned data");

    for (std::uint64_t index = 0; index < 7; ++index) {
        require(queue.try_push({index, index * 64, 64, 18}), "queue filled too early");
    }
    require(!queue.try_push({7, 448, 64, 18}), "queue overflow was not rejected");

    for (std::uint64_t index = 0; index < 7; ++index) {
        require(queue.try_pop(token), "queued token missing");
        require(token.sequence == index, "queue order changed");
    }
    require(queue.empty(), "queue did not drain");
}

void test_queue_concurrency() {
    constexpr std::uint64_t kTokenCount = 200'000;
    emu1820::SpscQueue<emu1820::AudioBlockToken, 1024> queue;
    std::atomic<bool> start{false};
    std::atomic<bool> failed{false};

    std::thread producer([&] {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        for (std::uint64_t sequence = 0; sequence < kTokenCount; ++sequence) {
            const emu1820::AudioBlockToken token{
                sequence,
                sequence * 64,
                64,
                emu1820::kFullInputChannels,
            };
            while (!queue.try_push(token)) {
                std::this_thread::yield();
            }
        }
    });

    std::thread consumer([&] {
        start.store(true, std::memory_order_release);
        for (std::uint64_t expected = 0; expected < kTokenCount; ++expected) {
            emu1820::AudioBlockToken token{};
            while (!queue.try_pop(token)) {
                std::this_thread::yield();
            }
            if (token.sequence != expected || token.first_sample_position != expected * 64) {
                failed.store(true, std::memory_order_release);
                return;
            }
        }
    });

    producer.join();
    consumer.join();
    require(!failed.load(std::memory_order_acquire), "concurrent queue ordering failed");
    require(queue.empty(), "concurrent queue did not drain");
}

void test_continuity_tracker() {
    emu1820::StreamContinuityTracker tracker;
    tracker.observe({0, 0, 128, 18});
    tracker.observe({1, 128, 128, 18});
    tracker.observe({2, 512, 128, 18});

    const auto state = tracker.snapshot();
    require(state.blocks == 3, "block count incorrect");
    require(state.frames == 384, "frame count incorrect");
    require(state.discontinuities == 1, "sample discontinuity was not detected");
    require(state.expected_next_sample == 640, "next sample position incorrect");
}

void test_monitor_failsafe() {
    emu1820::MonitorFailSafe router{2};
    require(router.select_path(0) == emu1820::MonitorPath::dry,
            "disarmed monitor did not choose dry path");

    router.arm_wet_path();
    router.report_wet_heartbeat(10);
    require(router.select_path(12) == emu1820::MonitorPath::wet,
            "healthy wet path was bypassed");
    require(router.select_path(13) == emu1820::MonitorPath::dry,
            "stale wet heartbeat did not fall back to dry");

    router.report_wet_heartbeat(14);
    router.report_wet_fault();
    require(router.select_path(14) == emu1820::MonitorPath::dry,
            "faulted wet path did not fall back to dry");
    require(router.fallback_count() >= 3, "fallback counter did not advance");
}

}  // namespace

int main() {
    test_format_contract();
    test_queue_boundaries();
    test_queue_concurrency();
    test_continuity_tracker();
    test_monitor_failsafe();
    std::cout << "EMU1820M realtime core tests passed\n";
    return EXIT_SUCCESS;
}

