#include "pnp_contract.h"

#define EMU1820_HARDWARE_ID_BUFFER_CHARS 512u

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, Emu1820QueryExactHardwareId)
#endif

static
WCHAR
Emu1820AsciiUpper(
    _In_ WCHAR Value
    )
{
    if (Value >= L'a' && Value <= L'z') {
        return (WCHAR)(Value - (L'a' - L'A'));
    }
    return Value;
}

static
BOOLEAN
Emu1820HardwareIdEntryMatches(
    _In_reads_(CandidateLength) PCWSTR Candidate,
    _In_ SIZE_T CandidateLength
    )
{
    static const WCHAR Expected[] = EMU1820_WINDOWS_HARDWARE_ID;
    const SIZE_T expectedLength = RTL_NUMBER_OF(Expected) - 1u;
    SIZE_T index;

    if (CandidateLength != expectedLength) {
        return FALSE;
    }

    for (index = 0; index < expectedLength; ++index) {
        if (Emu1820AsciiUpper(Candidate[index]) !=
            Emu1820AsciiUpper(Expected[index])) {
            return FALSE;
        }
    }
    return TRUE;
}

_Must_inspect_result_
BOOLEAN
Emu1820HardwareIdMultiSzMatches(
    _In_reads_bytes_(HardwareIdsBytes) PCWSTR HardwareIds,
    _In_ ULONG HardwareIdsBytes
    )
{
    SIZE_T characterCount;
    SIZE_T offset;
    BOOLEAN foundMatch = FALSE;

    if (HardwareIds == NULL ||
        HardwareIdsBytes < (2u * sizeof(WCHAR)) ||
        (HardwareIdsBytes % sizeof(WCHAR)) != 0u) {
        return FALSE;
    }

    characterCount = HardwareIdsBytes / sizeof(WCHAR);
    offset = 0;
    while (offset < characterCount && HardwareIds[offset] != L'\0') {
        SIZE_T entryLength = 0;
        while ((offset + entryLength) < characterCount &&
               HardwareIds[offset + entryLength] != L'\0') {
            ++entryLength;
        }
        if ((offset + entryLength) >= characterCount) {
            return FALSE;
        }
        if (Emu1820HardwareIdEntryMatches(
                HardwareIds + offset,
                entryLength)) {
            foundMatch = TRUE;
        }
        offset += entryLength + 1u;
    }
    // The terminator consumed above ends the last entry. A valid MULTI_SZ has
    // one additional NUL at the current offset.
    if (offset >= characterCount || HardwareIds[offset] != L'\0') {
        return FALSE;
    }
    return foundMatch;
}

_Must_inspect_result_
NTSTATUS
Emu1820QueryExactHardwareId(
    _In_ PDEVICE_OBJECT PhysicalDeviceObject,
    _Out_ PBOOLEAN Matches
    )
{
    NTSTATUS status;
    ULONG requiredBytes = 0;
    WCHAR hardwareIds[EMU1820_HARDWARE_ID_BUFFER_CHARS];

    PAGED_CODE();

    if (Matches == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    *Matches = FALSE;
    if (PhysicalDeviceObject == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    RtlZeroMemory(hardwareIds, sizeof(hardwareIds));

    status = IoGetDeviceProperty(
        PhysicalDeviceObject,
        DevicePropertyHardwareID,
        (ULONG)sizeof(hardwareIds),
        hardwareIds,
        &requiredBytes);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    if (requiredBytes < (2u * sizeof(WCHAR)) ||
        requiredBytes > sizeof(hardwareIds) ||
        (requiredBytes % sizeof(WCHAR)) != 0u) {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    *Matches = Emu1820HardwareIdMultiSzMatches(
        hardwareIds,
        requiredBytes);
    if (!*Matches) {
        status = STATUS_NOT_SUPPORTED;
    }
    return status;
}

static
NTSTATUS
Emu1820ValidatePortPair(
    _In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Raw,
    _In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Translated,
    _Out_ PEMU1820_PNP_RESOURCES Resources
    )
{
    if ((Raw->Flags & CM_RESOURCE_PORT_IO) == 0u ||
        (Translated->Flags & CM_RESOURCE_PORT_IO) == 0u ||
        Raw->u.Port.Length != EMU1820_EXPECTED_IO_PORT_BYTES ||
        Translated->u.Port.Length != EMU1820_EXPECTED_IO_PORT_BYTES ||
        Raw->u.Port.Start.HighPart != 0 ||
        Translated->u.Port.Start.HighPart != 0 ||
        Raw->u.Port.Start.LowPart == 0u ||
        Translated->u.Port.Start.LowPart == 0u ||
        Raw->u.Port.Start.LowPart >
            (MAXULONG - (EMU1820_EXPECTED_IO_PORT_BYTES - 1u)) ||
        Translated->u.Port.Start.LowPart >
            (MAXULONG - (EMU1820_EXPECTED_IO_PORT_BYTES - 1u))) {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    Resources->IoPortStart = Translated->u.Port.Start.LowPart;
    Resources->IoPortLength = Translated->u.Port.Length;
    return STATUS_SUCCESS;
}

static
NTSTATUS
Emu1820ValidateInterruptPair(
    _In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Raw,
    _In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Translated,
    _Out_ PEMU1820_PNP_RESOURCES Resources
    )
{
    if ((Raw->Flags & CM_RESOURCE_INTERRUPT_MESSAGE) != 0u ||
        (Translated->Flags & CM_RESOURCE_INTERRUPT_MESSAGE) != 0u) {
        return STATUS_NOT_SUPPORTED;
    }
    if (Raw->u.Interrupt.Vector == 0u ||
        Translated->u.Interrupt.Level == 0u ||
        Translated->u.Interrupt.Vector == 0u ||
        Translated->u.Interrupt.Affinity == 0u) {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    Resources->InterruptLevel = Translated->u.Interrupt.Level;
    Resources->InterruptVector = Translated->u.Interrupt.Vector;
    Resources->InterruptAffinity = Translated->u.Interrupt.Affinity;
#if defined(NT_PROCESSOR_GROUPS)
    Resources->ProcessorGroup = Translated->u.Interrupt.Group;
#else
    Resources->ProcessorGroup = 0u;
#endif
    return STATUS_SUCCESS;
}

_Must_inspect_result_
NTSTATUS
Emu1820ParseStartResources(
    _In_ PCM_RESOURCE_LIST RawResources,
    _In_ PCM_RESOURCE_LIST TranslatedResources,
    _Out_ PEMU1820_PNP_RESOURCES Resources
    )
{
    PCM_FULL_RESOURCE_DESCRIPTOR rawFull;
    PCM_FULL_RESOURCE_DESCRIPTOR translatedFull;
    ULONG descriptorIndex;
    BOOLEAN foundPort = FALSE;
    BOOLEAN foundInterrupt = FALSE;
    EMU1820_PNP_RESOURCES candidate;

    if (Resources == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    RtlZeroMemory(Resources, sizeof(*Resources));
    if (RawResources == NULL || TranslatedResources == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    RtlZeroMemory(&candidate, sizeof(candidate));

    // A PCI PDO supplies one full descriptor containing its partial resource
    // list. Reject multiple variable-length full descriptors rather than
    // walking an unbounded layout in this first bring-up parser.
    if (RawResources->Count != 1u || TranslatedResources->Count != 1u) {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    rawFull = &RawResources->List[0];
    translatedFull = &TranslatedResources->List[0];
    if (rawFull->InterfaceType != translatedFull->InterfaceType ||
        rawFull->BusNumber != translatedFull->BusNumber ||
        rawFull->PartialResourceList.Count == 0u ||
        rawFull->PartialResourceList.Count !=
            translatedFull->PartialResourceList.Count ||
        rawFull->PartialResourceList.Count >
            EMU1820_MAX_RESOURCE_DESCRIPTORS) {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    for (descriptorIndex = 0;
         descriptorIndex < rawFull->PartialResourceList.Count;
         ++descriptorIndex) {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR raw =
            &rawFull->PartialResourceList.PartialDescriptors[descriptorIndex];
        PCM_PARTIAL_RESOURCE_DESCRIPTOR translated =
            &translatedFull->PartialResourceList.PartialDescriptors[descriptorIndex];
        NTSTATUS status;

        if (raw->Type != translated->Type) {
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }

        switch (translated->Type) {
        case CmResourceTypeNull:
            break;
        case CmResourceTypePort:
            if (foundPort) {
                return STATUS_DEVICE_CONFIGURATION_ERROR;
            }
            status = Emu1820ValidatePortPair(raw, translated, &candidate);
            if (!NT_SUCCESS(status)) {
                return status;
            }
            foundPort = TRUE;
            break;
        case CmResourceTypeInterrupt:
            if (foundInterrupt) {
                return STATUS_DEVICE_CONFIGURATION_ERROR;
            }
            status = Emu1820ValidateInterruptPair(
                raw,
                translated,
                &candidate);
            if (!NT_SUCCESS(status)) {
                return status;
            }
            foundInterrupt = TRUE;
            break;
        case CmResourceTypeMemory:
        case CmResourceTypeDma:
        case CmResourceTypeDevicePrivate:
        default:
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
    }

    if (!foundPort || !foundInterrupt) {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    *Resources = candidate;
    return STATUS_SUCCESS;
}
