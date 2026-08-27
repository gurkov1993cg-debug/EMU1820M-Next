#pragma once

#include "emu/realtime_contract.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace emu1820 {

inline constexpr std::uint16_t kCreativePciVendorId = 0x1102;
inline constexpr std::uint16_t kEmu10k2PciDeviceId = 0x0004;
inline constexpr std::uint16_t kEmuSubsystemVendorId = 0x1102;
inline constexpr std::uint16_t kEmu1010HanaSubsystemDeviceId = 0x4001;

struct PciIdentity final {
    std::uint16_t vendor_id{};
    std::uint16_t device_id{};
    std::uint16_t subsystem_vendor_id{};
    std::uint16_t subsystem_device_id{};
    std::uint8_t revision_id{};
};

enum class HardwareProfile : std::uint8_t {
    unsupported,
    emu1010_hana_maem8810,
};

[[nodiscard]] constexpr HardwareProfile identify_hardware(
    const PciIdentity& identity) noexcept {
    return identity.vendor_id == kCreativePciVendorId &&
                   identity.device_id == kEmu10k2PciDeviceId &&
                   identity.subsystem_vendor_id == kEmuSubsystemVendorId &&
                   identity.subsystem_device_id == kEmu1010HanaSubsystemDeviceId
               ? HardwareProfile::emu1010_hana_maem8810
               : HardwareProfile::unsupported;
}

enum class TransportError : std::uint8_t {
    not_configured,
    none,
    unsupported_sample_rate,
    channel_count_mismatch,
    unsupported_sample_container,
    invalid_period_frames,
    invalid_period_count,
    ring_size_overflow,
    ring_too_large,
};

struct FullDuplexConfig final {
    std::uint32_t sample_rate{48'000};
    std::uint32_t capture_channels{kFullInputChannels};
    std::uint32_t render_channels{kFullOutputChannels};
    std::uint32_t sample_container_bytes{4};
    std::uint32_t frames_per_period{128};
    std::uint32_t period_count{8};
};

struct TransportContract final {
    TransportError error{TransportError::not_configured};
    FullDuplexConfig config{};
    std::uint64_t capture_period_bytes{};
    std::uint64_t render_period_bytes{};
    std::uint64_t capture_ring_bytes{};
    std::uint64_t render_ring_bytes{};

    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return error == TransportError::none;
    }
};

[[nodiscard]] TransportContract make_transport_contract(
    const FullDuplexConfig& config) noexcept;

enum class DmaDirection : std::uint8_t {
    capture,
    render,
};

struct DmaPeriodView final {
    std::uint64_t sequence{};
    std::uint64_t first_sample_position{};
    std::uint64_t byte_offset{};
    std::uint64_t byte_count{};
    std::uint32_t ring_index{};
    std::uint32_t frames{};
    std::uint32_t channels{};
};

class DmaRingLayout final {
public:
    [[nodiscard]] bool configure(
        const TransportContract& contract,
        DmaDirection direction) noexcept;
    [[nodiscard]] DmaPeriodView period_for_sequence(
        std::uint64_t sequence) const noexcept;
    [[nodiscard]] bool configured() const noexcept;

private:
    std::uint64_t period_bytes_{};
    std::uint32_t frames_{};
    std::uint32_t channels_{};
    std::uint32_t period_count_{};
    bool configured_{};
};

enum class ClockSource : std::uint8_t {
    internal,
    spdif,
    adat,
    word_clock,
    sync_card,
};

enum class EngineState : std::uint8_t {
    cold,
    prepared,
    running,
    clock_fault,
};

struct EngineCounters final {
    std::uint64_t periods_serviced{};
    std::uint64_t capture_xruns{};
    std::uint64_t render_xruns{};
    std::uint64_t duplex_phase_errors{};
    std::uint64_t clock_losses{};
};

struct DuplexPeriodResult final {
    bool valid{};
    bool capture_discontinuity{};
    bool render_discontinuity{};
    bool duplex_phase_error{};
    AudioBlockToken capture{};
    AudioBlockToken render{};
    DmaPeriodView capture_dma{};
    DmaPeriodView render_dma{};
};

class FullDuplexEngine final {
public:
    [[nodiscard]] bool prepare(
        const FullDuplexConfig& config,
        ClockSource clock_source) noexcept;
    void report_clock(bool locked, std::uint32_t detected_rate) noexcept;
    [[nodiscard]] bool start() noexcept;
    void stop() noexcept;

    [[nodiscard]] DuplexPeriodResult service_period(
        std::uint64_t capture_sequence,
        std::uint64_t render_sequence) noexcept;

    [[nodiscard]] EngineState state() const noexcept;
    [[nodiscard]] bool clock_locked() const noexcept;
    [[nodiscard]] const TransportContract& contract() const noexcept;
    [[nodiscard]] EngineCounters counters() const noexcept;
    [[nodiscard]] ContinuitySnapshot capture_continuity() const noexcept;
    [[nodiscard]] ContinuitySnapshot render_continuity() const noexcept;

private:
    TransportContract contract_{};
    DmaRingLayout capture_layout_{};
    DmaRingLayout render_layout_{};
    StreamContinuityTracker capture_continuity_{};
    StreamContinuityTracker render_continuity_{};
    EngineCounters counters_{};
    ClockSource clock_source_{ClockSource::internal};
    EngineState state_{EngineState::cold};
    std::uint64_t expected_capture_sequence_{};
    std::uint64_t expected_render_sequence_{};
    bool clock_locked_{};
};

template <std::size_t MonitorQueueCapacity>
class CaptureFanout final {
public:
    void publish_dry_capture(const AudioBlockToken& block) noexcept {
        // The dry ledger is committed before the optional monitor mirror.
        dry_continuity_.observe(block);
        dry_blocks_.fetch_add(1, std::memory_order_relaxed);
        if (!monitor_queue_.try_push(block)) {
            monitor_drops_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    [[nodiscard]] bool try_pop_monitor(AudioBlockToken& block) noexcept {
        return monitor_queue_.try_pop(block);
    }

    [[nodiscard]] ContinuitySnapshot dry_continuity() const noexcept {
        return dry_continuity_.snapshot();
    }

    [[nodiscard]] std::uint64_t dry_blocks() const noexcept {
        return dry_blocks_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::uint64_t monitor_drops() const noexcept {
        return monitor_drops_.load(std::memory_order_acquire);
    }

private:
    StreamContinuityTracker dry_continuity_{};
    SpscQueue<AudioBlockToken, MonitorQueueCapacity> monitor_queue_{};
    std::atomic<std::uint64_t> dry_blocks_{0};
    std::atomic<std::uint64_t> monitor_drops_{0};
};

static_assert(std::numeric_limits<std::uint64_t>::digits >= 64);
static_assert(sizeof(void*) == 8, "EMU1820M-Next supports Windows x64 only");

}  // namespace emu1820
