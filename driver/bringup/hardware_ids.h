#pragma once

// Exact PCI identity for the original MAEM8810 "Hana" E-MU 1010 card used by
// the 1820/1820M. This intentionally does not match newer 1010b/PCIe or 0404
// variants until each profile has its own reviewed hardware implementation.
#define EMU1820_PCI_VENDOR_ID             0x1102u
#define EMU1820_PCI_DEVICE_ID             0x0004u
#define EMU1820_SUBSYSTEM_VENDOR_ID       0x1102u
#define EMU1820_SUBSYSTEM_DEVICE_ID       0x4001u

#define EMU1820_WINDOWS_HARDWARE_ID \
    L"PCI\\VEN_1102&DEV_0004&SUBSYS_40011102"

// EMU10K2 exposes its indexed registers and E-MU FPGA GPIO interface through
// one PCI I/O range. No register access is permitted until this exact span has
// been validated from the paired raw/translated START_DEVICE resources.
#define EMU1820_EXPECTED_IO_PORT_BYTES     0x40u
#define EMU1820_MAX_RESOURCE_DESCRIPTORS   16u
