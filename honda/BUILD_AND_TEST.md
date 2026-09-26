# Honda HTS/CN2 Fire 28 — first bench build

This branch targets OneROM v0.7.1 and builds two complete RP2350 images:

- `OneROM-Honda-HTS-CN2-fire-28-a-BENCH.uf2`
- `OneROM-Honda-HTS-CN2-fire-28-b-BENCH.uf2`

The system plugin exposes two USB CDC ports:

- CDC0 — Honda Tuning Suite / Ostrich-compatible emulator protocol
- CDC1 — Honda OBD1 CN2 bridge, UART1, 38400 8N1

CN2 uses SEL_A/GPIO26 as UART1 TX and SEL_B/GPIO27 as UART1 RX in this test build.
The RP2350 input must be level shifted as appropriate for the Honda CN2 signal.

## Critical limitation of the BENCH build

`bench_base_27c256.bin` is intentionally 32 KiB of `0xFF`.

**Do not attempt to start an engine with the BENCH UF2.**

This build is for validating:

1. Fire 28 boots and enumerates on USB.
2. HTS sees the emulator CDC interface.
3. BRR/S/Ostrich connection sequence is stable.
4. Full 32 KiB Z-mode upload and readback work repeatedly.
5. Interrupted/bad-checksum transfers recover without crashing.
6. CN2 CDC bridge remains stable while HTS traffic is active.

A real known-good Honda 32 KiB ROM should replace `honda/bench_base_27c256.bin` before an ECU/engine-running test.

## Persistence status

HTS writes are protected with a live/staging RAM-slot swap so the ECU never sees a partially written ROM. This first test build does **not** yet persist live HTS edits into the composed flash ROM across a complete power loss. Power cycling returns to the ROM image embedded in the UF2.

That behaviour is deliberate for the first bench test: it lets us validate USB/protocol/CN2 stability before adding flash erase/program operations to a running ECU environment.

## Build

The included GitHub Actions workflow `.github/workflows/build-honda-hts.yml` uses OneROM's pinned Arm GNU toolchain installer, builds the v0.7.1 base firmware and Honda system plugin, composes Fire 28 A and B 27C256 images with the OneROM CLI, builds Raspberry Pi `picotool`, and converts the complete images to RP2350 UF2.
