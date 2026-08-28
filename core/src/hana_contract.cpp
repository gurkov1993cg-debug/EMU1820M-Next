#include "emu/hana_contract.hpp"

namespace emu1820 {
namespace {

[[nodiscard]] constexpr IoOperation hana_write_operation(
    const std::uint16_t value) noexcept {
    return {
        IoAccess::write,
        kAudigyGpioIoOffset,
        IoWidth::word16,
        value,
        kHanaProtocolDelayMicroseconds,
    };
}

[[nodiscard]] constexpr IoOperation hana_read_operation() noexcept {
    return {
        IoAccess::read,
        kAudigyGpioIoOffset,
        IoWidth::word16,
        0,
        0,
    };
}

[[nodiscard]] constexpr bool valid_gpio_operation(
    const IoOperation& operation) noexcept {
    return operation.offset == kAudigyGpioIoOffset &&
           operation.width == IoWidth::word16 &&
           static_cast<std::uint64_t>(operation.offset) +
                   static_cast<std::uint8_t>(operation.width) <=
               kEmu10k2IoPortBytes;
}

}  // namespace

HanaTransactionResult make_hana_read_transaction(
    const std::uint8_t hana_register) noexcept {
    HanaTransactionResult result{};
    if (hana_register > kHanaMaximumRegister) {
        result.error = HanaProtocolError::invalid_register;
        return result;
    }

    const auto address = static_cast<std::uint16_t>(
        hana_register | kHanaRegisterAddressFlag);
    result.transaction.operations[0] = hana_write_operation(address);
    result.transaction.operations[1] = hana_write_operation(
        static_cast<std::uint16_t>(address | kHanaProtocolClockFlag));
    result.transaction.operations[2] = hana_read_operation();
    result.transaction.count = 3;
    result.error = HanaProtocolError::none;
    return result;
}

HanaTransactionResult make_hana_write_transaction(
    const std::uint8_t hana_register,
    const std::uint8_t value) noexcept {
    HanaTransactionResult result{};
    if (hana_register > kHanaMaximumRegister) {
        result.error = HanaProtocolError::invalid_register;
        return result;
    }
    if (value > kHanaMaximumWriteValue) {
        result.error = HanaProtocolError::invalid_value;
        return result;
    }

    const auto address = static_cast<std::uint16_t>(
        hana_register | kHanaRegisterAddressFlag);
    result.transaction.operations[0] = hana_write_operation(address);
    result.transaction.operations[1] = hana_write_operation(
        static_cast<std::uint16_t>(address | kHanaProtocolClockFlag));
    result.transaction.operations[2] = hana_write_operation(value);
    result.transaction.operations[3] = hana_write_operation(
        static_cast<std::uint16_t>(value | kHanaProtocolClockFlag));
    result.transaction.count = 4;
    result.error = HanaProtocolError::none;
    return result;
}

bool validate_hana_transaction(
    const std::span<const IoOperation> operations) noexcept {
    if (operations.size() != 3 && operations.size() != 4) {
        return false;
    }
    for (const auto& operation : operations) {
        if (!valid_gpio_operation(operation)) {
            return false;
        }
    }

    const auto& address_low = operations[0];
    const auto& address_clock = operations[1];
    if (address_low.access != IoAccess::write ||
        address_clock.access != IoAccess::write ||
        address_low.delay_after_microseconds !=
            kHanaProtocolDelayMicroseconds ||
        address_clock.delay_after_microseconds !=
            kHanaProtocolDelayMicroseconds ||
        address_low.value < kHanaRegisterAddressFlag ||
        address_low.value >
            (kHanaRegisterAddressFlag | kHanaMaximumRegister) ||
        address_clock.value !=
            (address_low.value | kHanaProtocolClockFlag)) {
        return false;
    }

    if (operations.size() == 3) {
        const auto& read = operations[2];
        return read.access == IoAccess::read && read.value == 0 &&
               read.delay_after_microseconds == 0;
    }

    const auto& value_low = operations[2];
    const auto& value_clock = operations[3];
    return value_low.access == IoAccess::write &&
           value_clock.access == IoAccess::write &&
           value_low.delay_after_microseconds ==
               kHanaProtocolDelayMicroseconds &&
           value_clock.delay_after_microseconds ==
               kHanaProtocolDelayMicroseconds &&
           value_low.value <= kHanaMaximumWriteValue &&
           value_clock.value ==
               (value_low.value | kHanaProtocolClockFlag);
}

HanaProbeContract validate_hana_probe_snapshot(
    const HanaProbeSnapshot& snapshot) noexcept {
    HanaProbeContract contract{};
    contract.snapshot = snapshot;

    if (snapshot.identity != kHanaExpectedAlice2Identity) {
        contract.error = HanaProbeError::invalid_hana_identity;
        return contract;
    }
    if (snapshot.major_revision > 0x07 ||
        snapshot.minor_revision > 0x07) {
        contract.error = HanaProbeError::invalid_hana_revision;
        return contract;
    }
    if ((snapshot.option_cards &
         static_cast<std::uint8_t>(~kHanaKnownOptionMask)) != 0) {
        contract.error = HanaProbeError::unknown_option_bits;
        return contract;
    }

    const bool dock_online =
        (snapshot.option_cards & kHanaOptionDockOnline) != 0;
    const bool dock_offline =
        (snapshot.option_cards & kHanaOptionDockOffline) != 0;
    if (dock_online && dock_offline) {
        contract.error = HanaProbeError::contradictory_dock_state;
        return contract;
    }

    if (dock_online) {
        if (snapshot.dock_major_revision > 0x07 ||
            snapshot.dock_minor_revision > 0x07) {
            contract.error = HanaProbeError::invalid_dock_revision;
            return contract;
        }
        if (snapshot.dock_board_id > 0x03) {
            contract.error = HanaProbeError::invalid_dock_board_id;
            return contract;
        }
        contract.dock_state = AudioDockState::online;
    } else if (dock_offline) {
        contract.dock_state = AudioDockState::firmware_required;
    } else {
        contract.dock_state = AudioDockState::absent;
    }

    contract.error = HanaProbeError::none;
    return contract;
}

bool HanaBringup::accept_resources(
    const PnpResourceContract& resources) noexcept {
    if (state_ != HanaBringupState::idle) {
        return reject_transition();
    }
    if (!resources || resources.profile !=
                          HardwareProfile::emu1010_hana_maem8810 ||
        resources.io_port_length != kEmu10k2IoPortBytes) {
        state_ = HanaBringupState::faulted;
        return false;
    }

    resources_ = resources;
    probe_ = {};
    state_ = HanaBringupState::resources_validated;
    return true;
}

bool HanaBringup::begin_read_only_probe() noexcept {
    if (state_ != HanaBringupState::resources_validated) {
        return reject_transition();
    }
    state_ = HanaBringupState::read_only_probe_pending;
    return true;
}

bool HanaBringup::complete_read_only_probe(
    const HanaProbeSnapshot& snapshot) noexcept {
    if (state_ != HanaBringupState::read_only_probe_pending) {
        return reject_transition();
    }

    ++counters_.probes;
    probe_ = validate_hana_probe_snapshot(snapshot);
    if (!probe_) {
        ++counters_.probe_failures;
        state_ = HanaBringupState::faulted;
        return false;
    }

    switch (probe_.dock_state) {
    case AudioDockState::absent:
        state_ = HanaBringupState::dock_absent;
        break;
    case AudioDockState::firmware_required:
        state_ = HanaBringupState::dock_firmware_required;
        break;
    case AudioDockState::online:
        state_ = HanaBringupState::dock_online;
        break;
    }
    return true;
}

void HanaBringup::reset() noexcept {
    resources_ = {};
    probe_ = {};
    state_ = HanaBringupState::idle;
    ++counters_.resets;
}

HanaBringupState HanaBringup::state() const noexcept {
    return state_;
}

const HanaProbeContract& HanaBringup::probe() const noexcept {
    return probe_;
}

HanaBringupCounters HanaBringup::counters() const noexcept {
    return counters_;
}

bool HanaBringup::reject_transition() noexcept {
    ++counters_.rejected_transitions;
    return false;
}

}  // namespace emu1820
