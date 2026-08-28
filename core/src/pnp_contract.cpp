#include "emu/pnp_contract.hpp"

#include <limits>

namespace emu1820 {
namespace {

[[nodiscard]] constexpr char16_t ascii_upper(const char16_t value) noexcept {
    return value >= u'a' && value <= u'z'
               ? static_cast<char16_t>(value - (u'a' - u'A'))
               : value;
}

[[nodiscard]] constexpr bool valid_port_range(
    const BusResourceDescriptor& resource) noexcept {
    constexpr auto kMaxPort =
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max());
    return resource.start != 0 &&
           resource.length == kEmu10k2IoPortBytes &&
           (resource.flags & kBusResourcePortIoFlag) != 0 &&
           resource.start <= kMaxPort &&
           resource.start <= kMaxPort - (resource.length - 1U);
}

}  // namespace

bool matches_emu1010_windows_hardware_id(
    const std::u16string_view hardware_id) noexcept {
    if (hardware_id.size() != kEmu1010WindowsHardwareId.size()) {
        return false;
    }

    for (std::size_t index = 0; index < hardware_id.size(); ++index) {
        if (ascii_upper(hardware_id[index]) !=
            ascii_upper(kEmu1010WindowsHardwareId[index])) {
            return false;
        }
    }
    return true;
}

PnpResourceContract make_pnp_resource_contract(
    const PciIdentity& identity,
    const std::span<const BusResourceDescriptor> raw_resources,
    const std::span<const BusResourceDescriptor> translated_resources) noexcept {
    PnpResourceContract contract{};
    contract.profile = identify_hardware(identity);
    if (contract.profile == HardwareProfile::unsupported) {
        contract.error = PnpContractError::unsupported_hardware;
        return contract;
    }
    if (raw_resources.empty() || translated_resources.empty()) {
        contract.error = PnpContractError::empty_resource_list;
        return contract;
    }
    if (raw_resources.size() > kMaxPnpResourceDescriptors ||
        translated_resources.size() > kMaxPnpResourceDescriptors) {
        contract.error = PnpContractError::too_many_resources;
        return contract;
    }
    if (raw_resources.size() != translated_resources.size()) {
        contract.error = PnpContractError::resource_pair_mismatch;
        return contract;
    }

    bool found_port = false;
    bool found_interrupt = false;
    for (std::size_t index = 0; index < raw_resources.size(); ++index) {
        const auto& raw = raw_resources[index];
        const auto& translated = translated_resources[index];
        if (raw.type != translated.type) {
            contract.error = PnpContractError::resource_pair_mismatch;
            return contract;
        }

        switch (translated.type) {
        case BusResourceType::null_resource:
            break;
        case BusResourceType::io_port:
            if (found_port) {
                contract.error = PnpContractError::duplicate_io_port;
                return contract;
            }
            if (!valid_port_range(raw) || !valid_port_range(translated)) {
                contract.error = PnpContractError::invalid_io_port;
                return contract;
            }
            found_port = true;
            contract.io_port_start = translated.start;
            contract.io_port_length = translated.length;
            break;
        case BusResourceType::interrupt:
            if (found_interrupt) {
                contract.error = PnpContractError::duplicate_interrupt;
                return contract;
            }
            if (raw.message_signaled || translated.message_signaled) {
                contract.error = PnpContractError::message_interrupt_unsupported;
                return contract;
            }
            if (raw.interrupt_vector == 0 ||
                translated.interrupt_level == 0 ||
                translated.interrupt_vector == 0 ||
                translated.interrupt_affinity == 0) {
                contract.error = PnpContractError::invalid_interrupt;
                return contract;
            }
            found_interrupt = true;
            contract.interrupt_level = translated.interrupt_level;
            contract.interrupt_vector = translated.interrupt_vector;
            contract.interrupt_affinity = translated.interrupt_affinity;
            contract.processor_group = translated.processor_group;
            break;
        case BusResourceType::memory:
            contract.error = PnpContractError::unexpected_memory_resource;
            return contract;
        case BusResourceType::dma:
            contract.error = PnpContractError::unexpected_dma_resource;
            return contract;
        case BusResourceType::device_private:
        case BusResourceType::unknown:
            contract.error = PnpContractError::unexpected_resource;
            return contract;
        }
    }

    if (!found_port) {
        contract.error = PnpContractError::missing_io_port;
        return contract;
    }
    if (!found_interrupt) {
        contract.error = PnpContractError::missing_interrupt;
        return contract;
    }

    contract.error = PnpContractError::none;
    return contract;
}

bool PnpLifecycle::enumerate(const PciIdentity& identity) noexcept {
    if (state_ != PnpLifecycleState::detached &&
        state_ != PnpLifecycleState::removed) {
        return reject_transition();
    }

    identity_ = identity;
    resources_ = {};
    if (identify_hardware(identity_) == HardwareProfile::unsupported) {
        resources_.error = PnpContractError::unsupported_hardware;
        state_ = PnpLifecycleState::faulted;
        return false;
    }

    state_ = PnpLifecycleState::enumerated;
    return true;
}

bool PnpLifecycle::assign_resources(
    const std::span<const BusResourceDescriptor> raw_resources,
    const std::span<const BusResourceDescriptor> translated_resources) noexcept {
    if (state_ != PnpLifecycleState::enumerated &&
        state_ != PnpLifecycleState::stopped) {
        return reject_transition();
    }

    resources_ =
        make_pnp_resource_contract(identity_, raw_resources, translated_resources);
    if (!resources_) {
        state_ = PnpLifecycleState::faulted;
        return false;
    }

    state_ = PnpLifecycleState::resources_validated;
    return true;
}

bool PnpLifecycle::start() noexcept {
    if (state_ != PnpLifecycleState::resources_validated || !resources_) {
        return reject_transition();
    }
    state_ = PnpLifecycleState::started;
    ++counters_.starts;
    return true;
}

bool PnpLifecycle::stop() noexcept {
    if (state_ != PnpLifecycleState::started) {
        return reject_transition();
    }
    state_ = PnpLifecycleState::stopped;
    ++counters_.stops;
    return true;
}

void PnpLifecycle::surprise_remove() noexcept {
    if (state_ != PnpLifecycleState::enumerated &&
        state_ != PnpLifecycleState::resources_validated &&
        state_ != PnpLifecycleState::started &&
        state_ != PnpLifecycleState::stopped) {
        static_cast<void>(reject_transition());
        return;
    }
    state_ = PnpLifecycleState::surprise_removed;
    ++counters_.surprise_removals;
}

void PnpLifecycle::remove() noexcept {
    if (state_ == PnpLifecycleState::detached ||
        state_ == PnpLifecycleState::removed) {
        static_cast<void>(reject_transition());
        return;
    }
    identity_ = {};
    resources_ = {};
    state_ = PnpLifecycleState::removed;
    ++counters_.removals;
}

void PnpLifecycle::fault() noexcept {
    if (state_ == PnpLifecycleState::removed) {
        static_cast<void>(reject_transition());
        return;
    }
    state_ = PnpLifecycleState::faulted;
}

PnpLifecycleState PnpLifecycle::state() const noexcept {
    return state_;
}

const PnpResourceContract& PnpLifecycle::resources() const noexcept {
    return resources_;
}

PnpLifecycleCounters PnpLifecycle::counters() const noexcept {
    return counters_;
}

bool PnpLifecycle::reject_transition() noexcept {
    ++counters_.rejected_transitions;
    return false;
}

}  // namespace emu1820
