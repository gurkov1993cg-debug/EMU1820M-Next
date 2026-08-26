# Architecture contract

## Priority order

1. Bit-correct, synchronized dry capture of every available hardware input.
2. Deterministic recovery and explicit error reporting.
3. Native ASIO latency and multiclient behavior.
4. CPU-hosted VST3 monitoring.
5. Mixer convenience and appearance.

No later layer may weaken an earlier one.

## Planned process boundaries

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

