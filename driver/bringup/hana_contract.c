#include "hana_contract.h"

static
EMU1820_IO_OPERATION
Emu1820HanaWriteOperation(
    _In_ USHORT Value
    )
{
    EMU1820_IO_OPERATION operation;

    operation.Access = Emu1820IoWrite;
    operation.Offset = EMU1820_AUDIGY_GPIO_IO_OFFSET;
    operation.WidthBytes = (USHORT)sizeof(USHORT);
    operation.Value = Value;
    operation.DelayAfterMicroseconds =
        EMU1820_HANA_PROTOCOL_DELAY_MICROSECONDS;
    return operation;
}

static
EMU1820_IO_OPERATION
Emu1820HanaReadOperation(VOID)
{
    EMU1820_IO_OPERATION operation;

    operation.Access = Emu1820IoRead;
    operation.Offset = EMU1820_AUDIGY_GPIO_IO_OFFSET;
    operation.WidthBytes = (USHORT)sizeof(USHORT);
    operation.Value = 0u;
    operation.DelayAfterMicroseconds = 0u;
    return operation;
}

_Must_inspect_result_
NTSTATUS
Emu1820BuildHanaReadTransaction(
    _In_ UCHAR HanaRegister,
    _Out_ PEMU1820_HANA_TRANSACTION Transaction
    )
{
    USHORT address;

    if (Transaction == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    RtlZeroMemory(Transaction, sizeof(*Transaction));
    if (HanaRegister > EMU1820_HANA_MAXIMUM_REGISTER) {
        return STATUS_INVALID_PARAMETER;
    }

    address = (USHORT)(HanaRegister |
        EMU1820_HANA_REGISTER_ADDRESS_FLAG);
    Transaction->Operations[0] = Emu1820HanaWriteOperation(address);
    Transaction->Operations[1] = Emu1820HanaWriteOperation(
        (USHORT)(address | EMU1820_HANA_PROTOCOL_CLOCK_FLAG));
    Transaction->Operations[2] = Emu1820HanaReadOperation();
    Transaction->Count = 3u;
    return STATUS_SUCCESS;
}

_Must_inspect_result_
NTSTATUS
Emu1820BuildHanaWriteTransaction(
    _In_ UCHAR HanaRegister,
    _In_ UCHAR Value,
    _Out_ PEMU1820_HANA_TRANSACTION Transaction
    )
{
    USHORT address;

    if (Transaction == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    RtlZeroMemory(Transaction, sizeof(*Transaction));
    if (HanaRegister > EMU1820_HANA_MAXIMUM_REGISTER ||
        Value > EMU1820_HANA_MAXIMUM_WRITE_VALUE) {
        return STATUS_INVALID_PARAMETER;
    }

    address = (USHORT)(HanaRegister |
        EMU1820_HANA_REGISTER_ADDRESS_FLAG);
    Transaction->Operations[0] = Emu1820HanaWriteOperation(address);
    Transaction->Operations[1] = Emu1820HanaWriteOperation(
        (USHORT)(address | EMU1820_HANA_PROTOCOL_CLOCK_FLAG));
    Transaction->Operations[2] = Emu1820HanaWriteOperation(Value);
    Transaction->Operations[3] = Emu1820HanaWriteOperation(
        (USHORT)(Value | EMU1820_HANA_PROTOCOL_CLOCK_FLAG));
    Transaction->Count = 4u;
    return STATUS_SUCCESS;
}

static
BOOLEAN
Emu1820ValidGpioOperation(
    _In_ const EMU1820_IO_OPERATION* Operation
    )
{
    if (Operation->Offset != EMU1820_AUDIGY_GPIO_IO_OFFSET ||
        Operation->WidthBytes != sizeof(USHORT) ||
        Operation->Offset >
            (EMU1820_EXPECTED_IO_PORT_BYTES - Operation->WidthBytes)) {
        return FALSE;
    }
    return TRUE;
}

_Must_inspect_result_
BOOLEAN
Emu1820ValidateHanaTransaction(
    _In_ const EMU1820_HANA_TRANSACTION* Transaction
    )
{
    const EMU1820_IO_OPERATION* addressLow;
    const EMU1820_IO_OPERATION* addressClock;
    ULONG index;

    if (Transaction == NULL ||
        (Transaction->Count != 3u && Transaction->Count != 4u)) {
        return FALSE;
    }
    for (index = 0u; index < Transaction->Count; ++index) {
        if (!Emu1820ValidGpioOperation(&Transaction->Operations[index])) {
            return FALSE;
        }
    }

    addressLow = &Transaction->Operations[0];
    addressClock = &Transaction->Operations[1];
    if (addressLow->Access != Emu1820IoWrite ||
        addressClock->Access != Emu1820IoWrite ||
        addressLow->DelayAfterMicroseconds !=
            EMU1820_HANA_PROTOCOL_DELAY_MICROSECONDS ||
        addressClock->DelayAfterMicroseconds !=
            EMU1820_HANA_PROTOCOL_DELAY_MICROSECONDS ||
        addressLow->Value < EMU1820_HANA_REGISTER_ADDRESS_FLAG ||
        addressLow->Value >
            (EMU1820_HANA_REGISTER_ADDRESS_FLAG |
             EMU1820_HANA_MAXIMUM_REGISTER) ||
        addressClock->Value !=
            (addressLow->Value | EMU1820_HANA_PROTOCOL_CLOCK_FLAG)) {
        return FALSE;
    }

    if (Transaction->Count == 3u) {
        const EMU1820_IO_OPERATION* read = &Transaction->Operations[2];
        return read->Access == Emu1820IoRead &&
               read->Value == 0u &&
               read->DelayAfterMicroseconds == 0u;
    }

    {
        const EMU1820_IO_OPERATION* valueLow =
            &Transaction->Operations[2];
        const EMU1820_IO_OPERATION* valueClock =
            &Transaction->Operations[3];
        return valueLow->Access == Emu1820IoWrite &&
               valueClock->Access == Emu1820IoWrite &&
               valueLow->DelayAfterMicroseconds ==
                   EMU1820_HANA_PROTOCOL_DELAY_MICROSECONDS &&
               valueClock->DelayAfterMicroseconds ==
                   EMU1820_HANA_PROTOCOL_DELAY_MICROSECONDS &&
               valueLow->Value <= EMU1820_HANA_MAXIMUM_WRITE_VALUE &&
               valueClock->Value ==
                   (valueLow->Value | EMU1820_HANA_PROTOCOL_CLOCK_FLAG);
    }
}

_Must_inspect_result_
UCHAR
Emu1820DecodeHanaGpioRead(
    _In_ USHORT GpioValue
    )
{
    return (UCHAR)((GpioValue >> 8u) & EMU1820_HANA_READ_VALUE_MASK);
}

_Must_inspect_result_
NTSTATUS
Emu1820ValidateHanaProbeSnapshot(
    _In_ const EMU1820_HANA_PROBE_SNAPSHOT* Snapshot,
    _Out_ PEMU1820_HANA_PROBE_RESULT Result
    )
{
    BOOLEAN dockOnline;
    BOOLEAN dockOffline;

    if (Snapshot == NULL || Result == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    RtlZeroMemory(Result, sizeof(*Result));
    Result->Snapshot = *Snapshot;

    if (Snapshot->Identity != EMU1820_HANA_EXPECTED_ALICE2_IDENTITY) {
        Result->Error = Emu1820HanaProbeInvalidIdentity;
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    if (Snapshot->MajorRevision > 0x07u ||
        Snapshot->MinorRevision > 0x07u) {
        Result->Error = Emu1820HanaProbeInvalidRevision;
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    if ((Snapshot->OptionCards &
         EMU1820_HANA_UNKNOWN_OPTION_MASK) != 0u) {
        Result->Error = Emu1820HanaProbeUnknownOptionBits;
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    dockOnline = (Snapshot->OptionCards &
        EMU1820_HANA_OPTION_DOCK_ONLINE) != 0u;
    dockOffline = (Snapshot->OptionCards &
        EMU1820_HANA_OPTION_DOCK_OFFLINE) != 0u;
    if (dockOnline && dockOffline) {
        Result->Error = Emu1820HanaProbeContradictoryDockState;
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    if (dockOnline) {
        if (Snapshot->DockMajorRevision > 0x07u ||
            Snapshot->DockMinorRevision > 0x07u) {
            Result->Error = Emu1820HanaProbeInvalidDockRevision;
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
        if (Snapshot->DockBoardId > 0x03u) {
            Result->Error = Emu1820HanaProbeInvalidDockBoardId;
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
        Result->DockState = Emu1820AudioDockOnline;
    } else if (dockOffline) {
        Result->DockState = Emu1820AudioDockFirmwareRequired;
    } else {
        Result->DockState = Emu1820AudioDockAbsent;
    }

    Result->Error = Emu1820HanaProbeNone;
    return STATUS_SUCCESS;
}
