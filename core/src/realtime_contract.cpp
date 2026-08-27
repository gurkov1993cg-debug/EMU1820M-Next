#include "emu/realtime_contract.hpp"

namespace emu1820 {

void StreamContinuityTracker::reset() noexcept {
    initialized_ = false;
    snapshot_ = {};
}

void StreamContinuityTracker::observe(const AudioBlockToken& block) noexcept {
    if (initialized_ && block.first_sample_position != snapshot_.expected_next_sample) {
        ++snapshot_.discontinuities;
    }

    initialized_ = true;
    ++snapshot_.blocks;
    snapshot_.frames += block.frames;
    snapshot_.expected_next_sample = block.first_sample_position + block.frames;
}

ContinuitySnapshot StreamContinuityTracker::snapshot() const noexcept {
    return snapshot_;
}

MonitorFailSafe::MonitorFailSafe(
    const std::uint64_t heartbeat_timeout_blocks) noexcept
    : timeout_blocks_(heartbeat_timeout_blocks == 0 ? 1 : heartbeat_timeout_blocks) {}

void MonitorFailSafe::arm_wet_path() noexcept {
    faulted_.store(false, std::memory_order_release);
    armed_.store(true, std::memory_order_release);
}

void MonitorFailSafe::disarm_wet_path() noexcept {
    armed_.store(false, std::memory_order_release);
}

void MonitorFailSafe::report_wet_heartbeat(
    const std::uint64_t capture_block_sequence) noexcept {
    last_heartbeat_.store(capture_block_sequence, std::memory_order_release);
    faulted_.store(false, std::memory_order_release);
}

void MonitorFailSafe::report_wet_fault() noexcept {
    faulted_.store(true, std::memory_order_release);
}

MonitorPath MonitorFailSafe::select_path(
    const std::uint64_t capture_block_sequence) const noexcept {
    if (!armed_.load(std::memory_order_acquire) ||
        faulted_.load(std::memory_order_acquire)) {
        fallback_count_.fetch_add(1, std::memory_order_relaxed);
        return MonitorPath::dry;
    }

    const auto heartbeat = last_heartbeat_.load(std::memory_order_acquire);
    const bool heartbeat_is_future = heartbeat > capture_block_sequence;
    const bool timed_out = !heartbeat_is_future &&
                           (capture_block_sequence - heartbeat) > timeout_blocks_;
    if (timed_out) {
        fallback_count_.fetch_add(1, std::memory_order_relaxed);
        return MonitorPath::dry;
    }

    return MonitorPath::wet;
}

std::uint64_t MonitorFailSafe::fallback_count() const noexcept {
    return fallback_count_.load(std::memory_order_acquire);
}

}  // namespace emu1820

