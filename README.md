# EMU1820M-Next

[![Safe CI baseline](https://github.com/gurkov1993cg-debug/EMU1820M-Next/actions/workflows/build.yml/badge.svg)](https://github.com/gurkov1993cg-debug/EMU1820M-Next/actions/workflows/build.yml)

An open engineering project for a modern Windows x64 audio stack and CPU-hosted
VST3 monitor mixer for the E-MU 1820M.

## Current status

**Research and CI foundation only. There is no installable E-MU driver yet.**

The repository currently contains:

- A hardware-independent realtime contract library.
- A lock-free single-producer/single-consumer block queue.
- Sample-continuity and wet-monitor fail-safe tests.
- Exact identification of the original MAEM8810 Hana PCI card used by the 1820M.
- A logical 18-input/20-output full-duplex transport and DMA-ring model.
- Clock-loss, xrun, ring-wrap, and duplex phase-drift accounting.
- A non-blocking monitor fan-out proving that a stalled CPU/VST path cannot stop
  the dry-capture ledger.
- A fail-closed PCI/PnP contract for the exact Windows Hardware ID
  `PCI\VEN_1102&DEV_0004&SUBSYS_40011102`.
- Paired raw/translated resource validation for one 64-byte PCI I/O range and
  one line-based interrupt; MSI and unexpected resources are rejected.
- A deterministic PnP lifecycle model exercised through 100,000
  enumerate/start/stop/remove cycles.
- A dormant WDK resource parser inside a kernel compilation safety gate that
  deliberately refuses to load and performs no register access.
- A bounded HANA GPIO transaction grammar restricted to the 16-bit register at I/O
  offset `0x18`, with exhaustive register/value and malformed-sequence tests.
- A read-only HANA/AudioDock snapshot validator and 100,000-cycle bring-up state model;
  the matching dormant WDK implementation contains no physical port access.
- A Windows GitHub Actions build using the official Microsoft WDK NuGet package.

The repository does **not** currently contain:

- Live PCI/PnP attachment, I/O-port execution, or register access.
- HANA or AudioDock firmware loading.
- Physical DMA, ISR/DPC, WaveRT, ASIO, MIDI, HANA clock, or routing implementation.
- A VST3 host or graphical mixer.
- A signed or installable driver package.

## Non-negotiable design rule

The dry recording path must be independent from the CPU VST3 monitoring path:

```text
E-MU inputs -> kernel DMA -> native ASIO -> DAW dry recording
                         \-> CPU VST3 host -> monitor / optional wet recording
```

A plug-in crash, timeout, or CPU overload must never stop dry multichannel capture.
The monitor path must automatically fall back to dry audio.

## Full-channel target

- Windows 10/11 x64 only: 64-bit kernel driver, ASIO component, Console, and
  VST3 worker. A 32-bit/WOW64 stack is deliberately out of scope.
- 18 inputs and 20 outputs at 44.1 or 48 kHz.
- 24-bit capture/playback in a 32-bit transport container where required.
- Native x64 ASIO and WaveRT/WASAPI endpoints.
- One hardware clock and monotonic sample counter for every channel.
- Zero silent channel loss: every discontinuity must be counted and reported.

Higher sample-rate modes have reduced hardware I/O and are not part of the first
full-channel milestone.

## Delivery milestones

1. Realtime and full-duplex transport contracts with deterministic simulation.
2. Read-only x64 PCI/PnP enumeration and bounded resource discovery. The
   hardware-independent contract and dormant WDK parser are now present; live
   enumeration still requires the hardware test milestone.
3. HANA/AudioDock firmware, clock, and routing bring-up with no audio endpoints. The
   bounded transaction and read-only probe contracts are present; physical execution,
   firmware, clock, and routing remain disabled.
4. Dry capture, then full-duplex physical DMA with ISR/DPC accounting.
5. WaveRT endpoints and native 64-bit ASIO.
6. Separate 64-bit Console/VST3 worker with Dry, Wet, and Both modes.

Each milestone must remain buildable and testable. Physical-hardware claims begin
only after a disposable Windows test machine with the E-MU 1010 and AudioDockM is
available.

## Build

GitHub Actions builds and tests the hardware-independent core and compiles a
non-loadable kernel safety gate. The engineering artifact intentionally contains no
INF file and cannot bind to the real E-MU PCI hardware ID.

Local user-mode build:

```powershell
cmake -S . -B out/core -A x64
cmake --build out/core --config Release
ctest --test-dir out/core -C Release --output-on-failure
```

Configuration fails intentionally on a 32-bit toolchain.

Kernel compilation requires Visual Studio, MSBuild, and the WDK packages pinned in
`packages.config`.

## Safety and provenance

- Do not install CI engineering artifacts on an E-MU card.
- Do not upload proprietary Creative/E-MU firmware or binary drivers.
- Hardware behavior will be implemented only from legally usable documentation,
  observable behavior, and compatible open-source references.
- No stability or compatibility claim is valid until it passes testing on physical
  E-MU 1010 PCI + AudioDockM hardware.

E-MU and Creative are trademarks of their respective owners. This project is not
affiliated with or endorsed by Creative Technology or E-MU Systems.
