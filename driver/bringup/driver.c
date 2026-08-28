#include <ntddk.h>

#include "hana_contract.h"
#include "hardware_ids.h"
#include "pnp_contract.h"

C_ASSERT(sizeof(PVOID) == 8);
C_ASSERT(EMU1820_PCI_VENDOR_ID == 0x1102u);
C_ASSERT(EMU1820_PCI_DEVICE_ID == 0x0004u);
C_ASSERT(EMU1820_SUBSYSTEM_VENDOR_ID == 0x1102u);
C_ASSERT(EMU1820_SUBSYSTEM_DEVICE_ID == 0x4001u);
C_ASSERT(EMU1820_EXPECTED_IO_PORT_BYTES == 0x40u);
C_ASSERT(sizeof(EMU1820_PNP_RESOURCES) >= 32u);
C_ASSERT(EMU1820_AUDIGY_GPIO_IO_OFFSET + sizeof(USHORT) <=
    EMU1820_EXPECTED_IO_PORT_BYTES);
C_ASSERT(EMU1820_HANA_MAXIMUM_REGISTER == 0x3fu);
C_ASSERT(EMU1820_HANA_MAXIMUM_WRITE_VALUE == 0x3fu);
C_ASSERT(EMU1820_HANA_EXPECTED_ALICE2_IDENTITY == 0x55u);

DRIVER_INITIALIZE DriverEntry;

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#endif

_Use_decl_annotations_
NTSTATUS DriverEntry(
    PDRIVER_OBJECT DriverObject,
    PUNICODE_STRING RegistryPath
    )
{
    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(RegistryPath);

    // Safety gate: this CI binary must never attach to physical hardware.
    return STATUS_NOT_SUPPORTED;
}
