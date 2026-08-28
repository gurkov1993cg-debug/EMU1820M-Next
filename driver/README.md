# Kernel area

The only kernel binary currently built is `Emu1820SafetyGate.sys`.

It is a compile/toolchain gate, not an audio driver. `DriverEntry` deliberately returns
`STATUS_NOT_SUPPORTED`, there is no INF file, and the project does not claim an E-MU
PCI hardware ID. This prevents a CI artifact from accidentally replacing the working
legacy driver.

The gate now also compiles the first x64 PCI/PnP boundary:

- exact base Hardware ID matching for
  `PCI\VEN_1102&DEV_0004&SUBSYS_40011102` inside a bounded `REG_MULTI_SZ`;
- strict pairing of raw and translated `CM_RESOURCE_LIST` entries;
- acceptance of exactly one `0x40`-byte I/O-port range and one line interrupt;
- rejection of MSI, memory, DMA, private, duplicate, missing, mismatched, and
  oversized resource layouts.

It also compiles the next hardware-safe boundary:

- bounded HANA GPIO read/write transaction descriptions restricted to 16-bit I/O at
  offset `0x18`;
- strict six-bit register/write-value limits and fixed-delay pulse sequencing; and
- simulated HANA identity/revision and AudioDock presence validation.

These routines are intentionally dormant. There is no `AddDevice`, PortCls adapter,
dispatch table, INF, port-I/O primitive, interrupt connection, or DMA allocation.

The first hardware-capable milestone must not be enabled until all of the following
exist:

1. Confirmed PCI device/subsystem IDs from physical hardware.
2. Physical confirmation of the documented register-access boundary and power-state
   model.
3. I/O-resource execution with bounds checks and no audio streaming.
4. Kernel-debug access to a disposable Windows test installation.
5. A reviewed rollback procedure to the legacy driver.
