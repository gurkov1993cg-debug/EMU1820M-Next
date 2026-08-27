#pragma once

// Exact PCI identity for the original MAEM8810 "Hana" E-MU 1010 card used by
// the 1820/1820M. This intentionally does not match newer 1010b/PCIe or 0404
// variants until each profile has its own reviewed hardware implementation.
#define EMU1820_PCI_VENDOR_ID             0x1102u
#define EMU1820_PCI_DEVICE_ID             0x0004u
#define EMU1820_SUBSYSTEM_VENDOR_ID       0x1102u
#define EMU1820_SUBSYSTEM_DEVICE_ID       0x4001u

