#include "emu/transport_contract.hpp"

#include <limits>

namespace emu1820 {
namespace {

constexpr std::uint32_t kMinFramesPerPeriod = 32;
constexpr std::uint32_t kMaxFramesPerPeriod = 2'048;
constexpr std::uint32_t kMinPeriodCount = 2;
constexpr std::uint32_t kMaxPeriodCount = 64;
constexpr std::uint64_t kMaxRingBytes = 64ULL * 1'024ULL * 1'024ULL;

[[nodiscard]] constexpr bool is_power_of_two(const std::uint32_t value) noexcept {
    return value != 0 && (value & (value - 1U)) == 0;
}

[[nodiscard]] bool multiply_checked(
    const std::uint64_t left,
    const std::uint64_t right,
    std::uint64_t& result) noexcept {
    if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left) {
        return false;
    }
    result = left * right;
    return true;
}

}  // namespace

TransportContract make_transport_contract(const FullDuplexConfig& config) noexcept {
    TransportContract contract{};
    contract.config = config;

    if (!is_full_channel_sample_rate(config.sample_rate)) {
        contract.error = TransportError::unsupported_sample_rate;
        return contract;
    }
    if (config.capture_channels != kFullInputChannels ||
        config.render_channels != kFullOutputChannels) {
        contract.error = TransportError::channel_count_mismatch;
        return contract;
    }
    if (config.sample_container_bytes != 4) {
        contract.error = TransportError::unsupported_sample_container;
        return contract;
    }
    if (!is_power_of_two(config.frames_per_period) ||
        config.frames_per_period < kMinFramesPerPeriod ||
        config.frames_per_period > kMaxFramesPerPeriod) {
        contract.error = TransportError::invalid_period_frames;
        return contract;
    }
    if (config.period_count < kMinPeriodCount ||
        config.period_count > kMaxPeriodCount) {
        contract.error = TransportError::invalid_period_count;
        return contract;
    }

    std::uint64_t capture_frame_bytes{};
    std::uint64_t render_frame_bytes{};
    if (!multiply_checked(config.capture_channels, config.sample_container_bytes,
                          capture_frame_bytes) ||
        !multiply_checked(config.render_channels, config.sample_container_bytes,
                          render_frame_bytes) ||
        !multiply_checked(capture_frame_bytes, config.frames_per_period,
                          contract.capture_period_bytes) ||
        !multiply_checked(render_frame_bytes, config.frames_per_period,
                          contract.render_period_bytes) ||
        !multiply_checked(contract.capture_period_bytes, config.period_count,
                          contract.capture_ring_bytes) ||
        !multiply_checked(contract.render_period_bytes, config.period_count,
                          contract.render_ring_bytes)) {
        contract.error = TransportError::ring_size_overflow;
        return contract;
    }

    if (contract.capture_ring_bytes > kMaxRingBytes ||
        contract.render_ring_bytes > kMaxRingBytes) {
        contract.error = TransportError::ring_too_large;
        return contract;
    }

    contract.error = TransportError::none;
    return contract;
}

bool DmaRingLayout::configure(
    const TransportContract& contract,
    const DmaDirection direction) noexcept {
    configured_ = false;
    if (!contract) {
        return false;
    }

    frames_ = contract.config.frames_per_period;
    period_count_ = contract.config.period_count;
    if (direction == DmaDirection::capture) {
        period_bytes_ = contract.capture_period_bytes;
        channels_ = contract.config.capture_channels;
    } else {
        period_bytes_ = contract.render_period_bytes;
        channels_ = contract.config.render_channels;
    }
    configured_ = true;
    return true;
}

DmaPeriodView DmaRingLayout::period_for_sequence(
    const std::uint64_t sequence) const noexcept {
    if (!configured_ ||
        sequence > std::numeric_limits<std::uint64_t>::max() / frames_) {
        return {};
    }

    const auto ring_index = static_cast<std::uint32_t>(sequence % period_count_);
    return {
        sequence,
        sequence * frames_,
        static_cast<std::uint64_t>(ring_index) * period_bytes_,
        period_bytes_,
        ring_index,
        frames_,
        channels_,
    };
}

bool DmaRingLayout::configured() const noexcept {
    return configured_;
}

bool FullDuplexEngine::prepare(
    const FullDuplexConfig& config,
    const ClockSource clock_source) noexcept {
    if (state_ == EngineState::running) {
        return false;
    }

    const auto candidate = make_transport_contract(config);
    if (!candidate) {
        return false;
    }

    DmaRingLayout capture;
    DmaRingLayout render;
    if (!capture.configure(candidate, DmaDirection::capture) ||
        !render.configure(candidate, DmaDirection::render)) {
        return false;
    }

    contract_ = candidate;
    capture_layout_ = capture;
    render_layout_ = render;
    capture_continuity_.reset();
    render_continuity_.reset();
    counters_ = {};
    clock_source_ = clock_source;
    state_ = EngineState::prepared;
    expected_capture_sequence_ = 0;
    expected_render_sequence_ = 0;
    clock_locked_ = false;
    return true;
}

void FullDuplexEngine::report_clock(
    const bool locked,
    const std::uint32_t detected_rate) noexcept {
    const bool valid_lock = locked && contract_ &&
                            detected_rate == contract_.config.sample_rate;
    if (state_ == EngineState::running && !valid_lock) {
        ++counters_.clock_losses;
        state_ = EngineState::clock_fault;
    }
    clock_locked_ = valid_lock;
}

bool FullDuplexEngine::start() noexcept {
    if (state_ != EngineState::prepared || !clock_locked_) {
        return false;
    }
    state_ = EngineState::running;
    return true;
}

void FullDuplexEngine::stop() noexcept {
    if (contract_) {
        state_ = EngineState::prepared;
        expected_capture_sequence_ = 0;
        expected_render_sequence_ = 0;
        capture_continuity_.reset();
        render_continuity_.reset();
    } else {
        state_ = EngineState::cold;
    }
}

DuplexPeriodResult FullDuplexEngine::service_period(
    const std::uint64_t capture_sequence,
    const std::uint64_t render_sequence) noexcept {
    DuplexPeriodResult result{};
    if (state_ != EngineState::running || !clock_locked_) {
        return result;
    }

    result.capture_dma = capture_layout_.period_for_sequence(capture_sequence);
    result.render_dma = render_layout_.period_for_sequence(render_sequence);
    if (result.capture_dma.frames == 0 || result.render_dma.frames == 0) {
        return result;
    }

    result.capture_discontinuity = capture_sequence != expected_capture_sequence_;
    result.render_discontinuity = render_sequence != expected_render_sequence_;
    result.duplex_phase_error = capture_sequence != render_sequence;
    if (result.capture_discontinuity) {
        ++counters_.capture_xruns;
    }
    if (result.render_discontinuity) {
        ++counters_.render_xruns;
    }
    if (result.duplex_phase_error) {
        ++counters_.duplex_phase_errors;
    }

    expected_capture_sequence_ = capture_sequence + 1;
    expected_render_sequence_ = render_sequence + 1;

    result.capture = {
        capture_sequence,
        result.capture_dma.first_sample_position,
        result.capture_dma.frames,
        result.capture_dma.channels,
    };
    result.render = {
        render_sequence,
        result.render_dma.first_sample_position,
        result.render_dma.frames,
        result.render_dma.channels,
    };
    capture_continuity_.observe(result.capture);
    render_continuity_.observe(result.render);
    ++counters_.periods_serviced;
    result.valid = true;
    return result;
}

EngineState FullDuplexEngine::state() const noexcept {
    return state_;
}

bool FullDuplexEngine::clock_locked() const noexcept {
    return clock_locked_;
}

const TransportContract& FullDuplexEngine::contract() const noexcept {
    return contract_;
}

EngineCounters FullDuplexEngine::counters() const noexcept {
    return counters_;
}

ContinuitySnapshot FullDuplexEngine::capture_continuity() const noexcept {
    return capture_continuity_.snapshot();
}

ContinuitySnapshot FullDuplexEngine::render_continuity() const noexcept {
    return render_continuity_.snapshot();
}

}  // namespace emu1820
