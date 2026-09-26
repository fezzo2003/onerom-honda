# Honda OBD1 HTS build for OneROM Fire 28 (0.7.1 source integration)

Target: Fire 28 A/B, RP2350, 27C256 (32 KiB), Honda Tuning Suite only.

USB layout:
- CDC 0: HTS/Ostrich-compatible protocol
- CDC 1: Honda OBD1 CN2 UART bridge, 38400 8N1
- existing OneROM vendor interface retained

CN2 pins:
- GPIO26 / SEL_A: UART1 TX
- GPIO27 / SEL_B: UART1 RX
- UART1 FUNCSEL 0x0B on RP2350

ROM update safety:
- HTS ZW data is written to an inactive RAM ROM slot.
- The active slot remains untouched until the transfer checksum validates.
- On success, `ORA_ID_SET_ACTIVE_RAM_SLOT` atomically switches the ECU to the staged slot.
- The changed range is mirrored back to the now-inactive slot.
- On checksum/timeout failure, the touched staging range is rolled back from the live slot.

Known protocol scope in this integration:
- BRR connect probe
- S command swallowing
- Z W streaming: `Z W count addr_hi addr_lo [count*256 data] checksum`, response `O`
- Z R streaming: `Z R count addr_hi addr_lo checksum` then data + checksum

Before vehicle testing, compile with the OneROM pinned ARM GNU 15.3.rel1 toolchain and run HTS traces against a bench ECU.
