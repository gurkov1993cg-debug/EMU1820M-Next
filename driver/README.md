# Kernel area

The only kernel binary currently built is `Emu1820SafetyGate.sys`.

It is a compile/toolchain gate, not an audio driver. `DriverEntry` deliberately returns
`STATUS_NOT_SUPPORTED`, there is no INF file, and the project does not claim an E-MU
PCI hardware ID. This prevents a CI artifact from accidentally replacing the working
legacy driver.

The first hardware-capable milestone must not be enabled until all of the following
exist:

1. Confirmed PCI device/subsystem IDs from physical hardware.
2. A documented register-access boundary and power-state model.
3. BAR mapping with bounds checks and no audio streaming.
4. Kernel-debug access to a disposable Windows test installation.
5. A reviewed rollback procedure to the legacy driver.

