#pragma once

#include <ntddk.h>

#include "hardware_ids.h"

typedef struct _EMU1820_PNP_RESOURCES {
    ULONG IoPortStart;
    ULONG IoPortLength;
    ULONG InterruptLevel;
    ULONG InterruptVector;
    KAFFINITY InterruptAffinity;
    USHORT ProcessorGroup;
} EMU1820_PNP_RESOURCES, *PEMU1820_PNP_RESOURCES;

_Must_inspect_result_
BOOLEAN
Emu1820HardwareIdMultiSzMatches(
    _In_reads_bytes_(HardwareIdsBytes) PCWSTR HardwareIds,
    _In_ ULONG HardwareIdsBytes
    );

_Must_inspect_result_
NTSTATUS
Emu1820QueryExactHardwareId(
    _In_ PDEVICE_OBJECT PhysicalDeviceObject,
    _Out_ PBOOLEAN Matches
    );

_Must_inspect_result_
NTSTATUS
Emu1820ParseStartResources(
    _In_ PCM_RESOURCE_LIST RawResources,
    _In_ PCM_RESOURCE_LIST TranslatedResources,
    _Out_ PEMU1820_PNP_RESOURCES Resources
    );
