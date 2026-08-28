#pragma once

#include <ntddk.h>

#include "hardware_ids.h"

#define EMU1820_AUDIGY_GPIO_IO_OFFSET 0x18u
#define EMU1820_HANA_REGISTER_ADDRESS_FLAG 0x40u
#define EMU1820_HANA_PROTOCOL_CLOCK_FLAG 0x80u
#define EMU1820_HANA_MAXIMUM_REGISTER 0x3fu
#define EMU1820_HANA_MAXIMUM_WRITE_VALUE 0x3fu
#define EMU1820_HANA_READ_VALUE_MASK 0x7fu
#define EMU1820_HANA_PROTOCOL_DELAY_MICROSECONDS 10u
#define EMU1820_HANA_MAXIMUM_OPERATIONS 4u

#define EMU1820_HANA_IDENTITY_REGISTER 0x22u
#define EMU1820_HANA_MAJOR_REVISION_REGISTER 0x23u
#define EMU1820_HANA_MINOR_REVISION_REGISTER 0x24u
#define EMU1820_HANA_OPTION_CARDS_REGISTER 0x21u
#define EMU1820_HANA_EXPECTED_ALICE2_IDENTITY 0x55u
#define EMU1820_HANA_OPTION_HAMOA 0x01u
#define EMU1820_HANA_OPTION_SYNC 0x02u
#define EMU1820_HANA_OPTION_DOCK_ONLINE 0x04u
#define EMU1820_HANA_OPTION_DOCK_OFFLINE 0x08u
#define EMU1820_HANA_KNOWN_OPTION_MASK 0x0fu
#define EMU1820_HANA_UNKNOWN_OPTION_MASK 0xf0u

typedef enum _EMU1820_IO_ACCESS {
    Emu1820IoRead = 0,
    Emu1820IoWrite = 1
} EMU1820_IO_ACCESS;

typedef struct _EMU1820_IO_OPERATION {
    EMU1820_IO_ACCESS Access;
    USHORT Offset;
    USHORT WidthBytes;
    USHORT Value;
    USHORT DelayAfterMicroseconds;
} EMU1820_IO_OPERATION, *PEMU1820_IO_OPERATION;

typedef struct _EMU1820_HANA_TRANSACTION {
    EMU1820_IO_OPERATION Operations[EMU1820_HANA_MAXIMUM_OPERATIONS];
    ULONG Count;
} EMU1820_HANA_TRANSACTION, *PEMU1820_HANA_TRANSACTION;

_Must_inspect_result_
NTSTATUS
Emu1820BuildHanaReadTransaction(
    _In_ UCHAR HanaRegister,
    _Out_ PEMU1820_HANA_TRANSACTION Transaction
    );

_Must_inspect_result_
NTSTATUS
Emu1820BuildHanaWriteTransaction(
    _In_ UCHAR HanaRegister,
    _In_ UCHAR Value,
    _Out_ PEMU1820_HANA_TRANSACTION Transaction
    );

_Must_inspect_result_
BOOLEAN
Emu1820ValidateHanaTransaction(
    _In_ const EMU1820_HANA_TRANSACTION* Transaction
    );

_Must_inspect_result_
UCHAR
Emu1820DecodeHanaGpioRead(
    _In_ USHORT GpioValue
    );

typedef struct _EMU1820_HANA_PROBE_SNAPSHOT {
    UCHAR Identity;
    UCHAR MajorRevision;
    UCHAR MinorRevision;
    UCHAR OptionCards;
    UCHAR DockMajorRevision;
    UCHAR DockMinorRevision;
    UCHAR DockBoardId;
} EMU1820_HANA_PROBE_SNAPSHOT, *PEMU1820_HANA_PROBE_SNAPSHOT;

typedef enum _EMU1820_AUDIODOCK_STATE {
    Emu1820AudioDockAbsent = 0,
    Emu1820AudioDockFirmwareRequired = 1,
    Emu1820AudioDockOnline = 2
} EMU1820_AUDIODOCK_STATE;

typedef enum _EMU1820_HANA_PROBE_ERROR {
    Emu1820HanaProbeNone = 0,
    Emu1820HanaProbeInvalidIdentity,
    Emu1820HanaProbeInvalidRevision,
    Emu1820HanaProbeUnknownOptionBits,
    Emu1820HanaProbeContradictoryDockState,
    Emu1820HanaProbeInvalidDockRevision,
    Emu1820HanaProbeInvalidDockBoardId
} EMU1820_HANA_PROBE_ERROR;

typedef struct _EMU1820_HANA_PROBE_RESULT {
    EMU1820_HANA_PROBE_ERROR Error;
    EMU1820_AUDIODOCK_STATE DockState;
    EMU1820_HANA_PROBE_SNAPSHOT Snapshot;
} EMU1820_HANA_PROBE_RESULT, *PEMU1820_HANA_PROBE_RESULT;

_Must_inspect_result_
NTSTATUS
Emu1820ValidateHanaProbeSnapshot(
    _In_ const EMU1820_HANA_PROBE_SNAPSHOT* Snapshot,
    _Out_ PEMU1820_HANA_PROBE_RESULT Result
    );
