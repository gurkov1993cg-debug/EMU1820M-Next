#include "emu/hana_contract.hpp"

#include <array>
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

[[nodiscard]] emu1820::PnpResourceContract valid_resources() {
    emu1820::PnpResourceContract resources{};
    resources.error = emu1820::PnpContractError::none;
    resources.profile = emu1820::HardwareProfile::emu1010_hana_maem8810;
    resources.io_port_start = 0xd000;
    resources.io_port_length = emu1820::kEmu10k2IoPortBytes;
    resources.interrupt_level = 5;
    resources.interrupt_vector = 0x51;
    resources.interrupt_affinity = 1;
    return resources;
}

[[nodiscard]] emu1820::HanaProbeSnapshot online_dock_snapshot() {
    return {
        emu1820::kHanaExpectedAlice2Identity,
        1,
        2,
        static_cast<std::uint8_t>(
            emu1820::kHanaOptionHamoa |
            emu1820::kHanaOptionDockOnline),
        3,
        1,
        2,
    };
}

void test_hana_read_transaction() {
    const auto result = emu1820::make_hana_read_transaction(
        static_cast<std::uint8_t>(emu1820::HanaRegister::identity));
    require(static_cast<bool>(result), "valid HANA read was rejected");
    require(result.transaction.count == 3,
            "HANA read operation count changed");
    require(emu1820::validate_hana_transaction(result.transaction.view()),
            "generated HANA read did not validate");

    using enum emu1820::IoAccess;
    const std::array expected{
        emu1820::IoOperation{
            write,
            emu1820::kAudigyGpioIoOffset,
            emu1820::IoWidth::word16,
            0x62,
            emu1820::kHanaProtocolDelayMicroseconds},
        emu1820::IoOperation{
            write,
            emu1820::kAudigyGpioIoOffset,
            emu1820::IoWidth::word16,
            0xe2,
            emu1820::kHanaProtocolDelayMicroseconds},
        emu1820::IoOperation{
            read,
            emu1820::kAudigyGpioIoOffset,
            emu1820::IoWidth::word16,
            0,
            0},
    };
    require(result.transaction.view().size() == expected.size(),
            "HANA read shape changed");
    for (std::size_t index = 0; index < expected.size(); ++index) {
        require(result.transaction.operations[index] == expected[index],
                "HANA read pulse sequence changed");
    }

    const auto invalid = emu1820::make_hana_read_transaction(0x40);
    require(!invalid &&
                invalid.error == emu1820::HanaProtocolError::invalid_register,
            "out-of-range HANA read was accepted");
}

void test_hana_write_transaction() {
    const auto result = emu1820::make_hana_write_transaction(
        static_cast<std::uint8_t>(emu1820::HanaRegister::word_clock),
        0x01);
    require(static_cast<bool>(result), "valid HANA write was rejected");
    require(result.transaction.count == 4,
            "HANA write operation count changed");
    require(emu1820::validate_hana_transaction(result.transaction.view()),
            "generated HANA write did not validate");

    const std::array<std::uint16_t, 4> expected_values{
        0x45,
        0xc5,
        0x01,
        0x81,
    };
    for (std::size_t index = 0; index < expected_values.size(); ++index) {
        const auto& operation = result.transaction.operations[index];
        require(operation.access == emu1820::IoAccess::write &&
                    operation.offset == emu1820::kAudigyGpioIoOffset &&
                    operation.width == emu1820::IoWidth::word16 &&
                    operation.value == expected_values[index] &&
                    operation.delay_after_microseconds ==
                        emu1820::kHanaProtocolDelayMicroseconds,
                "HANA write pulse sequence changed");
    }

    require(emu1820::make_hana_write_transaction(0x40, 0).error ==
                emu1820::HanaProtocolError::invalid_register,
            "out-of-range HANA write register was accepted");
    require(emu1820::make_hana_write_transaction(0, 0x40).error ==
                emu1820::HanaProtocolError::invalid_value,
            "out-of-range HANA write value was accepted");
}

void test_exhaustive_protocol_domain() {
    for (std::uint16_t hana_register = 0;
         hana_register <= emu1820::kHanaMaximumRegister;
         ++hana_register) {
        const auto read = emu1820::make_hana_read_transaction(
            static_cast<std::uint8_t>(hana_register));
        require(read &&
                    emu1820::validate_hana_transaction(read.transaction.view()),
                "valid exhaustive HANA read failed");

        for (std::uint16_t value = 0;
             value <= emu1820::kHanaMaximumWriteValue;
             ++value) {
            const auto write = emu1820::make_hana_write_transaction(
                static_cast<std::uint8_t>(hana_register),
                static_cast<std::uint8_t>(value));
            require(write && emu1820::validate_hana_transaction(
                                 write.transaction.view()),
                    "valid exhaustive HANA write failed");
        }
    }

    for (std::uint32_t raw = 0; raw <= 0xffff; ++raw) {
        const auto decoded = emu1820::decode_hana_gpio_read(
            static_cast<std::uint16_t>(raw));
        require(decoded == ((raw >> 8U) & 0x7fU),
                "HANA GPIO decoder escaped its seven-bit domain");
    }
}

void test_protocol_mutations_fail_closed() {
    auto read = emu1820::make_hana_read_transaction(0x22).transaction;
    read.operations[0].offset = 0x3f;
    require(!emu1820::validate_hana_transaction(read.view()),
            "misaligned/out-of-range GPIO operation was accepted");

    read = emu1820::make_hana_read_transaction(0x22).transaction;
    read.operations[1].value ^= 0x01;
    require(!emu1820::validate_hana_transaction(read.view()),
            "incorrect HANA address clock edge was accepted");

    read = emu1820::make_hana_read_transaction(0x22).transaction;
    read.operations[2].access = emu1820::IoAccess::write;
    require(!emu1820::validate_hana_transaction(read.view()),
            "write substituted for HANA read was accepted");

    auto write = emu1820::make_hana_write_transaction(0x05, 0x01).transaction;
    write.operations[3].delay_after_microseconds = 0;
    require(!emu1820::validate_hana_transaction(write.view()),
            "missing HANA write delay was accepted");

    write = emu1820::make_hana_write_transaction(0x05, 0x01).transaction;
    write.count = 2;
    require(!emu1820::validate_hana_transaction(write.view()),
            "truncated HANA transaction was accepted");

    write = emu1820::make_hana_write_transaction(0x05, 0x01).transaction;
    write.count = write.operations.size() + 1;
    require(write.view().empty() &&
                !emu1820::validate_hana_transaction(write.view()),
            "oversized HANA transaction escaped its fixed storage");
}

void test_probe_register_plan() {
    const std::array expected_initial{
        emu1820::HanaRegister::identity,
        emu1820::HanaRegister::major_revision,
        emu1820::HanaRegister::minor_revision,
        emu1820::HanaRegister::option_cards,
    };
    require(emu1820::kHanaInitialProbeRegisters == expected_initial,
            "initial HANA probe register set changed");

    const std::array expected_dock{
        emu1820::HanaRegister::dock_major_revision,
        emu1820::HanaRegister::dock_minor_revision,
        emu1820::HanaRegister::dock_board_id,
    };
    require(emu1820::kHanaOnlineDockProbeRegisters == expected_dock,
            "online AudioDock probe register set changed");
}

void test_probe_snapshot_validation() {
    auto snapshot = online_dock_snapshot();
    auto result = emu1820::validate_hana_probe_snapshot(snapshot);
    require(result && result.dock_state == emu1820::AudioDockState::online,
            "valid online AudioDock snapshot was rejected");

    snapshot = online_dock_snapshot();
    snapshot.option_cards = emu1820::kHanaOptionDockOffline;
    result = emu1820::validate_hana_probe_snapshot(snapshot);
    require(result && result.dock_state ==
                          emu1820::AudioDockState::firmware_required,
            "offline AudioDock was not classified for firmware");

    snapshot = online_dock_snapshot();
    snapshot.option_cards = 0;
    result = emu1820::validate_hana_probe_snapshot(snapshot);
    require(result && result.dock_state == emu1820::AudioDockState::absent,
            "absent AudioDock was not classified");

    snapshot = online_dock_snapshot();
    snapshot.identity = 0x15;
    require(emu1820::validate_hana_probe_snapshot(snapshot).error ==
                emu1820::HanaProbeError::invalid_hana_identity,
            "non-Alice2 HANA identity was accepted");

    snapshot = online_dock_snapshot();
    snapshot.major_revision = 8;
    require(emu1820::validate_hana_probe_snapshot(snapshot).error ==
                emu1820::HanaProbeError::invalid_hana_revision,
            "out-of-range HANA revision was accepted");

    snapshot = online_dock_snapshot();
    snapshot.option_cards = 0x10;
    require(emu1820::validate_hana_probe_snapshot(snapshot).error ==
                emu1820::HanaProbeError::unknown_option_bits,
            "unknown HANA option bits were accepted");

    snapshot = online_dock_snapshot();
    snapshot.option_cards = static_cast<std::uint8_t>(
        emu1820::kHanaOptionDockOnline |
        emu1820::kHanaOptionDockOffline);
    require(emu1820::validate_hana_probe_snapshot(snapshot).error ==
                emu1820::HanaProbeError::contradictory_dock_state,
            "contradictory AudioDock state was accepted");

    snapshot = online_dock_snapshot();
    snapshot.dock_minor_revision = 8;
    require(emu1820::validate_hana_probe_snapshot(snapshot).error ==
                emu1820::HanaProbeError::invalid_dock_revision,
            "out-of-range AudioDock revision was accepted");

    snapshot = online_dock_snapshot();
    snapshot.dock_board_id = 4;
    require(emu1820::validate_hana_probe_snapshot(snapshot).error ==
                emu1820::HanaProbeError::invalid_dock_board_id,
            "out-of-range AudioDock board ID was accepted");
}

void test_all_option_bytes_are_classified_or_rejected() {
    auto snapshot = online_dock_snapshot();
    std::uint32_t accepted = 0;
    for (std::uint32_t options = 0; options <= 0xff; ++options) {
        snapshot.option_cards = static_cast<std::uint8_t>(options);
        const auto result = emu1820::validate_hana_probe_snapshot(snapshot);
        const bool known =
            (options & ~static_cast<std::uint32_t>(
                           emu1820::kHanaKnownOptionMask)) == 0;
        const bool contradictory =
            (options & emu1820::kHanaOptionDockOnline) != 0 &&
            (options & emu1820::kHanaOptionDockOffline) != 0;
        require(static_cast<bool>(result) == (known && !contradictory),
                "option-byte classification was not fail-closed");
        if (result) {
            ++accepted;
        }
    }
    require(accepted == 12,
            "unexpected number of valid HANA option combinations");
}

void test_bringup_state_machine_and_stress() {
    constexpr std::uint64_t kCycles = 100'000;
    const auto resources = valid_resources();
    const auto snapshot = online_dock_snapshot();
    emu1820::HanaBringup bringup;

    require(!bringup.begin_read_only_probe(),
            "HANA probe began without resources");
    for (std::uint64_t cycle = 0; cycle < kCycles; ++cycle) {
        require(bringup.accept_resources(resources),
                "HANA resources were rejected during stress");
        require(bringup.begin_read_only_probe(),
                "HANA read-only probe did not begin during stress");
        require(bringup.complete_read_only_probe(snapshot),
                "HANA read-only probe failed during stress");
        require(bringup.state() == emu1820::HanaBringupState::dock_online,
                "HANA stress cycle ended in the wrong state");
        bringup.reset();
    }

    const auto counters = bringup.counters();
    require(counters.probes == kCycles,
            "HANA probe counter changed during stress");
    require(counters.probe_failures == 0,
            "HANA stress produced a probe failure");
    require(counters.resets == kCycles,
            "HANA reset counter changed during stress");
    require(counters.rejected_transitions == 1,
            "HANA invalid transition accounting changed");
}

void test_invalid_resources_and_probe_fault() {
    auto resources = valid_resources();
    resources.io_port_length = 0x20;
    emu1820::HanaBringup bringup;
    require(!bringup.accept_resources(resources),
            "short I/O window reached HANA bring-up");
    require(bringup.state() == emu1820::HanaBringupState::faulted,
            "invalid HANA resources did not fault bring-up");

    bringup.reset();
    require(bringup.accept_resources(valid_resources()),
            "valid HANA resources were rejected after reset");
    require(bringup.begin_read_only_probe(),
            "valid HANA probe did not begin after reset");
    auto snapshot = online_dock_snapshot();
    snapshot.identity = 0;
    require(!bringup.complete_read_only_probe(snapshot),
            "invalid HANA identity completed bring-up");
    require(bringup.state() == emu1820::HanaBringupState::faulted &&
                bringup.counters().probe_failures == 1,
            "invalid HANA identity did not produce a counted fault");
}

}  // namespace

int main() {
    test_hana_read_transaction();
    test_hana_write_transaction();
    test_exhaustive_protocol_domain();
    test_protocol_mutations_fail_closed();
    test_probe_register_plan();
    test_probe_snapshot_validation();
    test_all_option_bytes_are_classified_or_rejected();
    test_bringup_state_machine_and_stress();
    test_invalid_resources_and_probe_fault();
    std::cout << "EMU1820M HANA/AudioDock contract tests passed\n";
    return EXIT_SUCCESS;
}
