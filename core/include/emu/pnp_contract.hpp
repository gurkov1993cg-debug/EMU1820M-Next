#pragma once

#include "emu/transport_contract.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace emu1820 {

inline constexpr std::u16string_view kEmu1010WindowsHardwareId =
    u"PCI\\VEN_1102&DEV_0004&SUBSYS_40011102";
inline constexpr std::uint64_t kEmu10k2IoPortBytes = 0x40;
inline constexpr std::size_t kMaxPnpResourceDescriptors = 16;
inline constexpr std::uint32_t kBusResourcePortIoFlag = 0x0001;

[[nodiscard]] bool matches_emu1010_windows_hardware_id(
    std::u16string_view hardware_id) noexcept;

enum class BusResourceType : std::uint8_t {
    null_resource,
    io_port,
    interrupt,
    memory,
    dma,
    device_private,
    unknown,
};

struct BusResourceDescriptor final {
    BusResourceType type{BusResourceType::unknown};
    std::uint64_t start{};
    std::uint64_t length{};
    std::uint32_t flags{};
    std::uint32_t interrupt_level{};
    std::uint32_t interrupt_vector{};
    std::uint64_t interrupt_affinity{};
    std::uint16_t processor_group{};
    bool message_signaled{};
};

enum class PnpContractError : std::uint8_t {
    not_configured,
    none,
    unsupported_hardware,
    empty_resource_list,
    too_many_resources,
    resource_pair_mismatch,
    missing_io_port,
    duplicate_io_port,
    invalid_io_port,
    missing_interrupt,
    duplicate_interrupt,
    invalid_interrupt,
    message_interrupt_unsupported,
    unexpected_memory_resource,
    unexpected_dma_resource,
    unexpected_resource,
};

struct PnpResourceContract final {
    PnpContractError error{PnpContractError::not_configured};
    HardwareProfile profile{HardwareProfile::unsupported};
    std::uint64_t io_port_start{};
    std::uint64_t io_port_length{};
    std::uint32_t interrupt_level{};
    std::uint32_t interrupt_vector{};
    std::uint64_t interrupt_affinity{};
    std::uint16_t processor_group{};

    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return error == PnpContractError::none;
    }
};

[[nodiscard]] PnpResourceContract make_pnp_resource_contract(
    const PciIdentity& identity,
    std::span<const BusResourceDescriptor> raw_resources,
    std::span<const BusResourceDescriptor> translated_resources) noexcept;

enum class PnpLifecycleState : std::uint8_t {
    detached,
    enumerated,
    resources_validated,
    started,
    stopped,
    surprise_removed,
    removed,
    faulted,
};

struct PnpLifecycleCounters final {
    std::uint64_t starts{};
    std::uint64_t stops{};
    std::uint64_t surprise_removals{};
    std::uint64_t removals{};
    std::uint64_t rejected_transitions{};
};

class PnpLifecycle final {
public:
    [[nodiscard]] bool enumerate(const PciIdentity& identity) noexcept;
    [[nodiscard]] bool assign_resources(
        std::span<const BusResourceDescriptor> raw_resources,
        std::span<const BusResourceDescriptor> translated_resources) noexcept;
    [[nodiscard]] bool start() noexcept;
    [[nodiscard]] bool stop() noexcept;
    void surprise_remove() noexcept;
    void remove() noexcept;
    void fault() noexcept;

    [[nodiscard]] PnpLifecycleState state() const noexcept;
    [[nodiscard]] const PnpResourceContract& resources() const noexcept;
    [[nodiscard]] PnpLifecycleCounters counters() const noexcept;

private:
    [[nodiscard]] bool reject_transition() noexcept;

    PciIdentity identity_{};
    PnpResourceContract resources_{};
    PnpLifecycleCounters counters_{};
    PnpLifecycleState state_{PnpLifecycleState::detached};
};

}  // namespace emu1820
