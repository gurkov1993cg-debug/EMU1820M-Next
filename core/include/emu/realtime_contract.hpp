#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace emu1820 {

inline constexpr std::uint32_t kFullInputChannels = 18;
inline constexpr std::uint32_t kFullOutputChannels = 20;
inline constexpr std::array<std::uint32_t, 2> kFullChannelSampleRates{44'100, 48'000};

[[nodiscard]] constexpr bool is_full_channel_sample_rate(
    const std::uint32_t sample_rate) noexcept {
    return sample_rate == kFullChannelSampleRates[0] ||
           sample_rate == kFullChannelSampleRates[1];
}

struct AudioBlockToken final {
    std::uint64_t sequence{};
    std::uint64_t first_sample_position{};
    std::uint32_t frames{};
    std::uint32_t channels{};
};

static_assert(std::is_trivially_copyable_v<AudioBlockToken>);

template <typename T, std::size_t Capacity>
class SpscQueue final {
    static_assert(Capacity >= 2, "SPSC capacity must be at least two");
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "SPSC capacity must be a power of two");
    static_assert(std::is_trivially_copyable_v<T>,
                  "Realtime queue entries must be trivially copyable");

public:
    [[nodiscard]] bool try_push(const T& value) noexcept {
        const auto write = write_index_.load(std::memory_order_relaxed);
        const auto next = increment(write);
        if (next == read_index_.load(std::memory_order_acquire)) {
            return false;
        }

        entries_[write] = value;
        write_index_.store(next, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool try_pop(T& value) noexcept {
        const auto read = read_index_.load(std::memory_order_relaxed);
        if (read == write_index_.load(std::memory_order_acquire)) {
            return false;
        }

        value = entries_[read];
        read_index_.store(increment(read), std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool empty() const noexcept {
        return read_index_.load(std::memory_order_acquire) ==
               write_index_.load(std::memory_order_acquire);
    }

private:
    [[nodiscard]] static constexpr std::size_t increment(
        const std::size_t value) noexcept {
        return (value + 1U) & (Capacity - 1U);
    }

    alignas(64) std::array<T, Capacity> entries_{};
    alignas(64) std::atomic<std::size_t> write_index_{0};
    alignas(64) std::atomic<std::size_t> read_index_{0};
};

struct ContinuitySnapshot final {
    std::uint64_t blocks{};
    std::uint64_t frames{};
    std::uint64_t discontinuities{};
    std::uint64_t expected_next_sample{};
};

class StreamContinuityTracker final {
public:
    void reset() noexcept;
    void observe(const AudioBlockToken& block) noexcept;
    [[nodiscard]] ContinuitySnapshot snapshot() const noexcept;

private:
    bool initialized_{false};
    ContinuitySnapshot snapshot_{};
};

enum class MonitorPath : std::uint8_t {
    dry,
    wet,
};

class MonitorFailSafe final {
public:
    explicit MonitorFailSafe(std::uint64_t heartbeat_timeout_blocks) noexcept;

    void arm_wet_path() noexcept;
    void disarm_wet_path() noexcept;
    void report_wet_heartbeat(std::uint64_t capture_block_sequence) noexcept;
    void report_wet_fault() noexcept;

    [[nodiscard]] MonitorPath select_path(
        std::uint64_t capture_block_sequence) const noexcept;
    [[nodiscard]] std::uint64_t fallback_count() const noexcept;

private:
    std::uint64_t timeout_blocks_;
    std::atomic<bool> armed_{false};
    std::atomic<bool> faulted_{false};
    std::atomic<std::uint64_t> last_heartbeat_{0};
    mutable std::atomic<std::uint64_t> fallback_count_{0};
};

}  // namespace emu1820

