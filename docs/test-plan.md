# Stability acceptance plan

Passing CI is necessary but not evidence that the hardware driver works. Hardware claims
require all tests below on a disposable Windows installation with kernel debugging.

## CI transport contract

Before physical hardware access is enabled, every change must prove:

- x64-only configuration; a 32-bit build is rejected.
- Exact MAEM8810 PCI/subsystem matching and rejection of adjacent Creative models.
- 18 capture and 20 render channels at 44.1/48 kHz in a 32-bit sample container.
- Deterministic capture/render period sizing and ring wrap.
- A shared monotonic timeline across at least 250,000 simulated full-duplex periods.
- Explicit xrun, duplex phase-error, and clock-loss accounting.
- A full monitor queue drops only monitor copies while the dry ledger remains complete.
- MSVC warnings are fatal and Address/Undefined Behavior sanitizers pass locally.

## Stage 0: safe enumeration

- Confirm the expected `1102:0004`, subsystem `1102:4001`, and physical revision ID.
- Map PCI resources read-only where possible.
- Verify clean start/stop and 100 cold boots.
- Verify rollback to the legacy driver.

## Stage 1: clocks and firmware

- Load only legally redistributable or user-extracted firmware.
- Detect HANA and AudioDockM versions.
- Switch 44.1/48 kHz only while streams are stopped.
- Exercise internal clock and external-clock loss/recovery without a crash.

## Stage 2: dry capture first

- Capture all 18 inputs at 24-bit/44.1 kHz and 24-bit/48 kHz.
- Verify a known per-channel test pattern and common sample alignment.
- Run 72 hours at a 128-frame period with zero lost/repeated blocks.
- Repeat at 64 frames under CPU, storage, graphics, and network load.
- Record every FIFO overrun, DMA error, and sample-position discontinuity.

## Stage 3: full duplex

- Exercise all available inputs and outputs simultaneously.
- Run Windows HLK low-period capture and full-duplex glitch tests.
- Verify no channel reassignment after restart or sample-rate change.

## Stage 4: ASIO

- Cubase multichannel record/playback at 64/128/256/512 frames.
- Exact latency and monotonic sample-position reporting.
- Multiple clients at one sample rate; explicit rejection of incompatible rates.
- 100 open/close cycles and 100 project sample-rate transitions.

## Stage 5: VST3 monitoring

- Dry recording continues while the VST worker is killed or deliberately stalled.
- Automatic monitor fallback occurs within the configured deadline.
- Plug-in crash, scan failure, and CPU overload never stop the dry ASIO stream.
- `Dry`, `Wet`, and `Both` recording modes are distinguishable and sample-aligned.
