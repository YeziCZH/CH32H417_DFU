# WLINK pin-reset extension

The bundled binary is built from the crates.io `wlink` 0.1.2 source with the
following local changes:

- Package version changed to `0.1.2+pinrst.3`.
- `ResetMode::PinRst` was added to the reset subcommand.
- `--hold-ms` controls how long nRST remains low; the default is 100 ms.
- The pin-reset branch opens the WCH-Link probe but does not attach to the
  target MCU.
- It sends `SetRSTPin::Low`, waits for `hold_ms`, sends `SetRSTPin::High`,
  waits 20 ms, then sends `SetRSTPin::Floating`.
- WCH-LinkE v2.22 accepts those PC8 operations as one-byte command `0x0d`
  payloads: `0x13` (low), `0x14` (high), and `0x15` (floating). The previous
  `0x0e, subcommand` payload was a no-op on this firmware and was removed.
- The normal attach-based reset path rejects `PinRst` as unreachable.

Build target:

```text
i686-pc-windows-gnu
```

The 32-bit target is required on this machine because it selects wlink's
CH375 native-driver backend. The x64 build selects WinUSB and is incompatible
with the installed WCH-Link driver.

Hardware validation:

- Probe: WCH-LinkE v2.22 (CH32V305)
- Target: CH32H417MEU, 960 KB Flash
- Wiring: WCH-LinkE controller PC8 reset output connected to target nRST
- Command: `reset pin-rst --hold-ms 500`
- Result: trace showed `81 0d 01 13/14/15`; COM4 emitted a fresh Bootloader
  banner and then the APP boot banner. This directly verifies a target reset.
