# x64 PCI/PnP bring-up contract

This milestone establishes what the future Windows x64 driver may trust before it
touches E-MU hardware. It does not install or attach a driver.

## Exact device boundary

The first supported physical profile is the original MAEM8810 Hana E-MU 1010 PCI
card used by the 1820/1820M:

| Field | Required value |
|---|---:|
| PCI vendor | `0x1102` |
| PCI device | `0x0004` |
| Subsystem vendor | `0x1102` |
| Subsystem device | `0x4001` |
| Windows base Hardware ID | `PCI\VEN_1102&DEV_0004&SUBSYS_40011102` |

The matcher is ASCII case-insensitive but exact in length. It does not accept a
generic `VEN/DEV` prefix, a different E-MU subsystem, or a revision-qualified string
as a substitute for the required base entry.

## START_DEVICE resource policy

Windows supplies bus-relative raw resources and system-translated resources as paired
lists. The parser preserves their index relationship and fails closed unless both
lists contain the same bounded layout.

| Resource | Policy |
|---|---|
| PCI I/O ports | Exactly one; `0x40` bytes; nonzero 32-bit range in both lists |
| Interrupt | Exactly one line interrupt; nonzero translated level/vector/affinity |
| MSI | Rejected until separately designed and tested |
| Memory BAR | Rejected for this first profile |
| DMA/private/unknown | Rejected |
| Null descriptor | Allowed only as a paired placeholder |
| Total partial descriptors | `1..16` |

The `0x40` span covers the documented EMU10K2 indexed-register, interrupt, HCFG,
GPIO/Hana-interface, timer, AC97, and P16V port-offset boundary. It does not prove that
the card is safe to access on an arbitrary PCI bridge.

## Lifecycle rule

The portable state machine permits only:

1. detach/remove to exact enumeration;
2. enumeration or stop to validated resources;
3. validated resources to start;
4. start to stop; and
5. active states to surprise removal, followed by removal.

Invalid resources fault the lifecycle and cannot start. Every rejected transition is
counted. CI exercises 100,000 normal cycles plus surprise-removal and fault paths.

## Deliberate safety stop

`Emu1820SafetyGate.sys` compiles the parser, but `DriverEntry` always returns
`STATUS_NOT_SUPPORTED`. The repository contains no INF or CAT file. Therefore this
milestone cannot bind to the card, read an I/O port, connect an interrupt, load Hana
firmware, or start DMA.

The next hardware-capable change requires a disposable Windows x64 machine, kernel
debugging, a captured resource log from the real card, and a rollback path to the
legacy driver before any register access is enabled.
