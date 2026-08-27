#include <ntddk.h>

#include "hardware_ids.h"

C_ASSERT(sizeof(PVOID) == 8);
C_ASSERT(EMU1820_PCI_VENDOR_ID == 0x1102u);
C_ASSERT(EMU1820_PCI_DEVICE_ID == 0x0004u);
C_ASSERT(EMU1820_SUBSYSTEM_VENDOR_ID == 0x1102u);
C_ASSERT(EMU1820_SUBSYSTEM_DEVICE_ID == 0x4001u);

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
