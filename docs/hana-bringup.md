# HANA and AudioDock read-only bring-up contract

This phase defines the bounded GPIO transaction grammar and validates simulated
HANA/AudioDock observations. It deliberately performs no physical port access, does
not load firmware, and cannot bind to the PCI card.

## I/O boundary

The original MAEM8810 profile has a validated 64-byte PCI I/O window. HANA is reached
through the 16-bit Audigy GPIO register at offset `0x18`. A protocol transaction may
touch only that offset with 16-bit operations.

The protocol uses two bounded transaction shapes:

- Read: latch one register address with two writes, then perform one 16-bit read.
- Write: latch one register address with two writes, then latch one six-bit value with
  two writes.

Every write has a fixed 10-microsecond post-operation delay. Register addresses and
write values are restricted to six bits. A GPIO read is reduced to the seven input bits
reported by the HANA interface. No arbitrary I/O offset, unbounded wait, implicit retry,
or read-modify-write operation is represented by the contract.

## First read-only probe

The initial observation set is intentionally small:

- HANA identity, major revision, and minor revision.
- Option-card presence/status.
- AudioDock revision and board ID only after the option status reports the dock online.

For the exact original 1010/1820M profile, the Alice2 HANA identity is `0x55`. HANA and
online AudioDock revision fields are constrained to their documented widths. Unknown
option bits and simultaneous online/offline dock flags fail closed.

The resulting AudioDock classification is one of:

- absent;
- present but requiring firmware; or
- online.

Classification does not authorize power, firmware, clock, mute, routing, or interrupt
writes. Those operations need separate reviewed state transitions and physical-card
evidence.

## Kernel safety gate

The WDK project contains a C implementation of the same transaction builders and
snapshot validator so x64 compiler and SAL warnings are caught early. It contains no
`READ_PORT_*` or `WRITE_PORT_*` call. `DriverEntry` still returns
`STATUS_NOT_SUPPORTED`, and no INF or CAT file is produced.

The portable tests exhaust every legal register/write-value pair, all 65,536 possible
16-bit GPIO read values, every option byte, malformed transaction mutations, and
100,000 reset/probe state-machine cycles.
