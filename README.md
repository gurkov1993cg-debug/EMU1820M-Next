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
- A kernel compilation safety gate that deliberately refuses to load.
- A Windows GitHub Actions build using the official Microsoft WDK NuGet package.

The repository does **not** currently contain:

- PCI enumeration or BAR/register access.
- HANA or AudioDock firmware loading.
- DMA, ISR/DPC, WaveRT, ASIO, MIDI, clock, or routing implementation.
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

- 18 inputs and 20 outputs at 44.1 or 48 kHz.
- 24-bit capture/playback in a 32-bit transport container where required.
- Native x64 ASIO and WaveRT/WASAPI endpoints.
- One hardware clock and monotonic sample counter for every channel.
- Zero silent channel loss: every discontinuity must be counted and reported.

Higher sample-rate modes have reduced hardware I/O and are not part of the first
full-channel milestone.

## Build

GitHub Actions builds and tests the hardware-independent core and compiles a
non-loadable kernel safety gate. The engineering artifact intentionally contains no
INF file and cannot claim the real E-MU PCI hardware ID.

Local user-mode build:

```powershell
cmake -S . -B out/core -A x64
cmake --build out/core --config Release
ctest --test-dir out/core -C Release --output-on-failure
```

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

