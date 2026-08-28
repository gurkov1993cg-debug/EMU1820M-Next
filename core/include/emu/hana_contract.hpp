#pragma once

#include "emu/pnp_contract.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace emu1820 {

inline constexpr std::uint16_t kAudigyGpioIoOffset = 0x18;
inline constexpr std::uint8_t kHanaRegisterAddressFlag = 0x40;
inline constexpr std::uint8_t kHanaProtocolClockFlag = 0x80;
inline constexpr std::uint8_t kHanaMaximumRegister = 0x3f;
inline constexpr std::uint8_t kHanaMaximumWriteValue = 0x3f;
inline constexpr std::uint8_t kHanaReadValueMask = 0x7f;
inline constexpr std::uint16_t kHanaProtocolDelayMicroseconds = 10;

enum class IoAccess : std::uint8_t {
    read,
    write,
};

enum class IoWidth : std::uint8_t {
    word16 = 2,
};

struct IoOperation final {
    IoAccess access{IoAccess::read};
    std::uint16_t offset{};
    IoWidth width{IoWidth::word16};
    std::uint16_t value{};
    std::uint16_t delay_after_microseconds{};

    [[nodiscard]] constexpr bool operator==(
        const IoOperation&) const noexcept = default;
};

struct HanaTransaction final {
    std::array<IoOperation, 4> operations{};
    std::size_t count{};

    [[nodiscard]] constexpr std::span<const IoOperation> view() const noexcept {
        return count <= operations.size()
                   ? std::span<const IoOperation>{operations.data(), count}
                   : std::span<const IoOperation>{};
    }
};

enum class HanaProtocolError : std::uint8_t {
    none,
    invalid_register,
    invalid_value,
    malformed_transaction,
};

struct HanaTransactionResult final {
    HanaProtocolError error{HanaProtocolError::malformed_transaction};
    HanaTransaction transaction{};

    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return error == HanaProtocolError::none;
    }
};

[[nodiscard]] HanaTransactionResult make_hana_read_transaction(
    std::uint8_t hana_register) noexcept;

[[nodiscard]] HanaTransactionResult make_hana_write_transaction(
    std::uint8_t hana_register,
    std::uint8_t value) noexcept;

[[nodiscard]] bool validate_hana_transaction(
    std::span<const IoOperation> operations) noexcept;

[[nodiscard]] constexpr std::uint8_t decode_hana_gpio_read(
    const std::uint16_t gpio_value) noexcept {
    return static_cast<std::uint8_t>(
        (gpio_value >> 8U) & kHanaReadValueMask);
}

enum class HanaRegister : std::uint8_t {
    dock_power = 0x04,
    word_clock = 0x05,
    default_clock = 0x06,
    unmute = 0x07,
    fpga_config = 0x08,
    irq_enable = 0x09,
    optical_type = 0x0b,
    irq_status = 0x20,
    option_cards = 0x21,
    identity = 0x22,
    major_revision = 0x23,
    minor_revision = 0x24,
    dock_major_revision = 0x25,
    dock_minor_revision = 0x26,
    dock_board_id = 0x27,
};

inline constexpr std::uint8_t kHanaExpectedAlice2Identity = 0x55;
inline constexpr std::uint8_t kHanaOptionHamoa = 0x01;
inline constexpr std::uint8_t kHanaOptionSync = 0x02;
inline constexpr std::uint8_t kHanaOptionDockOnline = 0x04;
inline constexpr std::uint8_t kHanaOptionDockOffline = 0x08;
inline constexpr std::uint8_t kHanaKnownOptionMask = 0x0f;

inline constexpr std::array<HanaRegister, 4> kHanaInitialProbeRegisters{
    HanaRegister::identity,
    HanaRegister::major_revision,
    HanaRegister::minor_revision,
    HanaRegister::option_cards,
};

inline constexpr std::array<HanaRegister, 3> kHanaOnlineDockProbeRegisters{
    HanaRegister::dock_major_revision,
    HanaRegister::dock_minor_revision,
    HanaRegister::dock_board_id,
};

struct HanaProbeSnapshot final {
    std::uint8_t identity{};
    std::uint8_t major_revision{};
    std::uint8_t minor_revision{};
    std::uint8_t option_cards{};
    std::uint8_t dock_major_revision{};
    std::uint8_t dock_minor_revision{};
    std::uint8_t dock_board_id{};
};

enum class AudioDockState : std::uint8_t {
    absent,
    firmware_required,
    online,
};

enum class HanaProbeError : std::uint8_t {
    not_probed,
    none,
    invalid_hana_identity,
    invalid_hana_revision,
    unknown_option_bits,
    contradictory_dock_state,
    invalid_dock_revision,
    invalid_dock_board_id,
};

struct HanaProbeContract final {
    HanaProbeError error{HanaProbeError::not_probed};
    AudioDockState dock_state{AudioDockState::absent};
    HanaProbeSnapshot snapshot{};

    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return error == HanaProbeError::none;
    }
};

[[nodiscard]] HanaProbeContract validate_hana_probe_snapshot(
    const HanaProbeSnapshot& snapshot) noexcept;

enum class HanaBringupState : std::uint8_t {
    idle,
    resources_validated,
    read_only_probe_pending,
    dock_absent,
    dock_firmware_required,
    dock_online,
    faulted,
};

struct HanaBringupCounters final {
    std::uint64_t probes{};
    std::uint64_t probe_failures{};
    std::uint64_t resets{};
    std::uint64_t rejected_transitions{};
};

class HanaBringup final {
public:
    [[nodiscard]] bool accept_resources(
        const PnpResourceContract& resources) noexcept;
    [[nodiscard]] bool begin_read_only_probe() noexcept;
    [[nodiscard]] bool complete_read_only_probe(
        const HanaProbeSnapshot& snapshot) noexcept;
    void reset() noexcept;

    [[nodiscard]] HanaBringupState state() const noexcept;
    [[nodiscard]] const HanaProbeContract& probe() const noexcept;
    [[nodiscard]] HanaBringupCounters counters() const noexcept;

private:
    [[nodiscard]] bool reject_transition() noexcept;

    PnpResourceContract resources_{};
    HanaProbeContract probe_{};
    HanaBringupCounters counters_{};
    HanaBringupState state_{HanaBringupState::idle};
};

}  // namespace emu1820
