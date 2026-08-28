# Architecture contract

## Priority order

1. Bit-correct, synchronized dry capture of every available hardware input.
2. Deterministic recovery and explicit error reporting.
3. Native ASIO latency and multiclient behavior.
4. CPU-hosted VST3 monitoring.
5. Mixer convenience and appearance.

No later layer may weaken an earlier one.

## Planned process boundaries

Every component is x64. Cubase 15 and modern VST3 plug-ins are 64-bit, so the project
does not add a second 32-bit ASIO or plug-in bridge that could weaken testing and crash
isolation.

### Kernel audio driver

Owns PCI resources, firmware state, DMA, interrupt/DPC handling, hardware clocks,
sample position, and safe routing primitives. It performs no VST processing, file I/O,
dynamic memory allocation in the streaming path, or graphical work.

### Native ASIO x64 component

Maps the driver's synchronized buffers into the DAW contract, reports exact latency and
sample position, and rejects incompatible sample-rate changes while streams are active.

### Console service and UI

Controls routing and owns a separate CPU VST3 monitoring stream. A watchdog selects the
dry monitor path when the VST worker misses its deadline or reports a fault. Closing the
Console must not close the DAW's capture stream.

### VST3 worker

Runs outside the kernel and outside the dry-record dispatch path. Plug-in scanning must
also be isolated so a malformed plug-in cannot stop the Console. Plug-ins that introduce
look-ahead or excessive latency must be identified before live monitoring is armed.

## Realtime rules

- Preallocate and pin streaming buffers before starting DMA.
- No locks, waits, heap allocation, logging, or plug-in calls from ISR context.
- ISR acknowledges hardware and records minimal state; bounded DPC work advances
  completed periods.
- All channels share one monotonic sample timeline.
- A dropped, repeated, or reordered period increments a visible xrun counter.
- Routing changes are atomic at an audio-block boundary.
- The driver never performs hidden sample-rate conversion.

## First supported operating mode

The first hardware milestone targets 18 inputs and 20 outputs at 44.1/48 kHz. Higher
sample-rate modes are separate milestones because the hardware exposes fewer channels.

## Hardware identity boundary

The first physical profile is deliberately restricted to the original MAEM8810 Hana
E-MU 1010 card used with the AudioDock[M]:

- PCI vendor `0x1102`
- PCI device `0x0004`
- subsystem vendor `0x1102`
- subsystem device `0x4001`

Newer 1010b, PCIe, CardBus, and 0404 variants must not bind to the first driver. Their
register, firmware, and transport differences require separate reviewed profiles.

The corresponding first-profile Windows Hardware ID is exactly
`PCI\VEN_1102&DEV_0004&SUBSYS_40011102`. A revision-qualified ID may appear in the
device's `REG_MULTI_SZ`, but the driver must find the exact base entry rather than
accepting a prefix or a generic Creative device ID.

## PCI/PnP resource boundary

`IRP_MN_START_DEVICE` supplies paired raw and translated resource lists. The first
profile accepts only:

- one PCI I/O-port descriptor with an exact `0x40`-byte span in both lists;
- one conventional line interrupt with a nonzero translated level, vector, and
  affinity; and
- optional paired null descriptors.

The parser rejects mismatched list shapes, duplicate descriptors, MSI, memory BARs,
DMA resources, private descriptors, zero or overflowing port ranges, and more than 16
partial descriptors. Only translated values may be retained for future register and
interrupt operations. Passing this boundary still does not authorize register access:
the current safety gate returns `STATUS_NOT_SUPPORTED` from `DriverEntry`.

## Logical versus physical DMA

The current transport engine models the user-visible 18-input/20-output timeline, ring
sizes, period wrap, and xrun rules. It does not yet claim that the hardware presents one
interleaved 18-channel DMA engine. The physical EMU10K2/FX8010 capture topology will be
mapped into the logical contract only after bounded BAR/I/O discovery and hardware
validation. This prevents a convenient simulator layout from becoming an unverified
hardware assumption.

## Clock-loss behavior

No hidden sample-rate conversion or invented samples are allowed. A mismatched or lost
clock faults the physical stream, increments a visible counter, and rejects further
period completion until the stream is stopped and the clock is valid again. VST3
failure is different: it affects only the monitor mirror and falls back to dry audio.
