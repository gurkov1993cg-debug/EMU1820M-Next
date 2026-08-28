#include "emu/pnp_contract.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <utility>

namespace {

constexpr emu1820::PciIdentity kEmu1010{
    0x1102,
    0x0004,
    0x1102,
    0x4001,
    0x04,
};

struct ResourcePair final {
    std::array<emu1820::BusResourceDescriptor, 3> raw;
    std::array<emu1820::BusResourceDescriptor, 3> translated;
};

[[noreturn]] void fail(const char* message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void require(const bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

[[nodiscard]] ResourcePair valid_resources() {
    using enum emu1820::BusResourceType;
    return {
        {{
            {io_port, 0xD000, 0x40, 1},
            {interrupt, 0, 0, 0, 11, 11, 1, 0, false},
            {null_resource},
        }},
        {{
            {io_port, 0xD000, 0x40, 1},
            {interrupt, 0, 0, 0, 5, 0x51, 3, 0, false},
            {null_resource},
        }},
    };
}

void test_exact_windows_hardware_id() {
    require(emu1820::matches_emu1010_windows_hardware_id(
                u"PCI\\VEN_1102&DEV_0004&SUBSYS_40011102"),
            "exact Windows Hardware ID was rejected");
    require(emu1820::matches_emu1010_windows_hardware_id(
                u"pci\\ven_1102&dev_0004&subsys_40011102"),
            "case-insensitive Windows Hardware ID was rejected");
    require(!emu1820::matches_emu1010_windows_hardware_id(
                u"PCI\\VEN_1102&DEV_0004&SUBSYS_40021102"),
            "adjacent E-MU subsystem was accepted");
    require(!emu1820::matches_emu1010_windows_hardware_id(
                u"PCI\\VEN_1102&DEV_0004"),
            "generic Creative device ID was accepted");
    require(!emu1820::matches_emu1010_windows_hardware_id(
                u"PCI\\VEN_1102&DEV_0004&SUBSYS_40011102&REV_04"),
            "revision-suffixed ID was accepted as the exact base ID");
}

void test_valid_resource_contract() {
    const auto resources = valid_resources();
    const auto contract = emu1820::make_pnp_resource_contract(
        kEmu1010,
        resources.raw,
        resources.translated);
    require(static_cast<bool>(contract), "valid PCI resources were rejected");
    require(contract.profile ==
                emu1820::HardwareProfile::emu1010_hana_maem8810,
            "wrong hardware profile was selected");
    require(contract.io_port_start == 0xD000 &&
                contract.io_port_length == 0x40,
            "translated I/O port range changed");
    require(contract.interrupt_level == 5 &&
                contract.interrupt_vector == 0x51 &&
                contract.interrupt_affinity == 3,
            "translated interrupt resource changed");
}

void test_identity_and_list_boundaries() {
    const auto resources = valid_resources();

    auto wrong_identity = kEmu1010;
    wrong_identity.subsystem_device_id = 0x4002;
    require(emu1820::make_pnp_resource_contract(
                wrong_identity,
                resources.raw,
                resources.translated).error ==
                emu1820::PnpContractError::unsupported_hardware,
            "unsupported subsystem received resources");

    require(emu1820::make_pnp_resource_contract(
                kEmu1010,
                {},
                {}).error == emu1820::PnpContractError::empty_resource_list,
            "empty resource lists were accepted");
    require(emu1820::make_pnp_resource_contract(
                kEmu1010,
                resources.raw,
                std::span{resources.translated}.first<2>()).error ==
                emu1820::PnpContractError::resource_pair_mismatch,
            "unpaired resource lists were accepted");

    std::array<emu1820::BusResourceDescriptor, 17> oversized{};
    require(emu1820::make_pnp_resource_contract(
                kEmu1010,
                oversized,
                oversized).error == emu1820::PnpContractError::too_many_resources,
            "oversized resource list was accepted");
}

void test_port_fail_closed_rules() {
    auto resources = valid_resources();
    resources.translated[0].length = 0x20;
    require(emu1820::make_pnp_resource_contract(
                kEmu1010,
                resources.raw,
                resources.translated).error ==
                emu1820::PnpContractError::invalid_io_port,
            "short register range was accepted");

    resources = valid_resources();
    resources.raw[0].start = 0;
    require(emu1820::make_pnp_resource_contract(
                kEmu1010,
                resources.raw,
                resources.translated).error ==
                emu1820::PnpContractError::invalid_io_port,
            "zero raw port base was accepted");

    resources = valid_resources();
    resources.translated[0].flags = 0;
    require(emu1820::make_pnp_resource_contract(
                kEmu1010,
                resources.raw,
                resources.translated).error ==
                emu1820::PnpContractError::invalid_io_port,
            "memory-space port descriptor was accepted");

    resources = valid_resources();
    resources.translated[0].start = 0xFFFF'FFF0ULL;
    require(emu1820::make_pnp_resource_contract(
                kEmu1010,
                resources.raw,
                resources.translated).error ==
                emu1820::PnpContractError::invalid_io_port,
            "overflowing x86 I/O port range was accepted");

    resources = valid_resources();
    resources.raw[2] = resources.raw[0];
    resources.translated[2] = resources.translated[0];
    require(emu1820::make_pnp_resource_contract(
                kEmu1010,
                resources.raw,
                resources.translated).error ==
                emu1820::PnpContractError::duplicate_io_port,
            "duplicate I/O port range was accepted");
}

void test_interrupt_fail_closed_rules() {
    auto resources = valid_resources();
    resources.translated[1].message_signaled = true;
    require(emu1820::make_pnp_resource_contract(
                kEmu1010,
                resources.raw,
                resources.translated).error ==
                emu1820::PnpContractError::message_interrupt_unsupported,
            "MSI was accepted before an MSI design exists");

    resources = valid_resources();
    resources.translated[1].interrupt_affinity = 0;
    require(emu1820::make_pnp_resource_contract(
                kEmu1010,
                resources.raw,
                resources.translated).error ==
                emu1820::PnpContractError::invalid_interrupt,
            "zero interrupt affinity was accepted");

    resources = valid_resources();
    resources.raw[2] = resources.raw[1];
    resources.translated[2] = resources.translated[1];
    require(emu1820::make_pnp_resource_contract(
                kEmu1010,
                resources.raw,
                resources.translated).error ==
                emu1820::PnpContractError::duplicate_interrupt,
            "duplicate line interrupt was accepted");
}

void test_missing_mismatched_and_unexpected_resources() {
    using enum emu1820::BusResourceType;
    const std::array port_only{
        emu1820::BusResourceDescriptor{io_port, 0xD000, 0x40, 1},
    };
    require(emu1820::make_pnp_resource_contract(
                kEmu1010,
                port_only,
                port_only).error == emu1820::PnpContractError::missing_interrupt,
            "resource list without interrupt was accepted");

    const std::array interrupt_only{
        emu1820::BusResourceDescriptor{
            interrupt, 0, 0, 0, 11, 11, 1, 0, false},
    };
    require(emu1820::make_pnp_resource_contract(
                kEmu1010,
                interrupt_only,
                interrupt_only).error == emu1820::PnpContractError::missing_io_port,
            "resource list without port range was accepted");

    auto resources = valid_resources();
    resources.translated[0].type = memory;
    require(emu1820::make_pnp_resource_contract(
                kEmu1010,
                resources.raw,
                resources.translated).error ==
                emu1820::PnpContractError::resource_pair_mismatch,
            "raw/translated type mismatch was accepted");

    for (const auto& [type, expected] : std::array{
             std::pair{memory, emu1820::PnpContractError::unexpected_memory_resource},
             std::pair{dma, emu1820::PnpContractError::unexpected_dma_resource},
             std::pair{device_private, emu1820::PnpContractError::unexpected_resource},
             std::pair{unknown, emu1820::PnpContractError::unexpected_resource},
         }) {
        resources = valid_resources();
        resources.raw[2].type = type;
        resources.translated[2].type = type;
        require(emu1820::make_pnp_resource_contract(
                    kEmu1010,
                    resources.raw,
                    resources.translated).error == expected,
                "unexpected PCI resource was accepted");
    }
}

void test_pnp_lifecycle_and_recovery() {
    constexpr std::uint64_t kCycles = 100'000;
    const auto resources = valid_resources();
    emu1820::PnpLifecycle lifecycle;

    require(!lifecycle.start(), "detached device started");
    for (std::uint64_t cycle = 0; cycle < kCycles; ++cycle) {
        require(lifecycle.enumerate(kEmu1010), "enumeration cycle failed");
        require(lifecycle.assign_resources(resources.raw, resources.translated),
                "resource assignment cycle failed");
        require(lifecycle.start(), "start cycle failed");
        require(lifecycle.stop(), "stop cycle failed");
        lifecycle.remove();
        require(lifecycle.state() == emu1820::PnpLifecycleState::removed,
                "remove cycle ended in the wrong state");
    }

    const auto counters = lifecycle.counters();
    require(counters.starts == kCycles, "start counter changed");
    require(counters.stops == kCycles, "stop counter changed");
    require(counters.removals == kCycles, "remove counter changed");
    require(counters.rejected_transitions == 1,
            "invalid transition accounting changed");

    require(lifecycle.enumerate(kEmu1010), "re-enumeration failed");
    require(lifecycle.assign_resources(resources.raw, resources.translated),
            "re-enumeration resource assignment failed");
    require(lifecycle.start(), "re-enumerated device did not start");
    require(lifecycle.stop(), "re-enumerated device did not stop");
    require(lifecycle.assign_resources(resources.raw, resources.translated),
            "stopped device did not revalidate START_DEVICE resources");
    require(lifecycle.start(), "resource-revalidated device did not restart");
    lifecycle.surprise_remove();
    require(lifecycle.state() == emu1820::PnpLifecycleState::surprise_removed,
            "surprise removal was not terminal");
    require(!lifecycle.start(), "surprise-removed device restarted");
    lifecycle.remove();
    require(lifecycle.state() == emu1820::PnpLifecycleState::removed,
            "surprise-removed device did not remove cleanly");
}

void test_invalid_resources_fault_lifecycle() {
    auto resources = valid_resources();
    resources.translated[0].length = 0x80;

    emu1820::PnpLifecycle lifecycle;
    require(lifecycle.enumerate(kEmu1010), "valid identity was rejected");
    require(!lifecycle.assign_resources(resources.raw, resources.translated),
            "invalid resources were assigned");
    require(lifecycle.state() == emu1820::PnpLifecycleState::faulted,
            "invalid resources did not fault the device");
    require(lifecycle.resources().error ==
                emu1820::PnpContractError::invalid_io_port,
            "resource fault reason was lost");
    require(!lifecycle.start(), "faulted device started");
}

}  // namespace

int main() {
    test_exact_windows_hardware_id();
    test_valid_resource_contract();
    test_identity_and_list_boundaries();
    test_port_fail_closed_rules();
    test_interrupt_fail_closed_rules();
    test_missing_mismatched_and_unexpected_resources();
    test_pnp_lifecycle_and_recovery();
    test_invalid_resources_fault_lifecycle();
    std::cout << "EMU1820M PCI/PnP contract tests passed\n";
    return EXIT_SUCCESS;
}
