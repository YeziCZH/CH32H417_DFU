# Bundled WLINK

This directory contains a patched Windows x86 native-driver build of
`ch32-rs/wlink` 0.1.2 used by this project.

- Upstream: https://github.com/ch32-rs/wlink
- License: MIT OR Apache-2.0
- Local version: `0.1.2+pinrst.3`
- Binary: `wlink.exe`
- SHA-256: `A3572CEF4D958D4507ACB94764EAD4B73D7061D5C243470C4EE20849885D35DB`
- Source change notes: `PINRST_PATCH_NOTES.md`

Run it from the repository root through the path-independent wrapper:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/wlink.ps1 --version
powershell -ExecutionPolicy Bypass -File scripts/wlink.ps1 list
```

The local `reset pin-rst` extension drives the WCH-LinkE physical nRST line
without attaching to the target first:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/wlink.ps1 reset pin-rst --hold-ms 500
```

The WCH-LinkE exposes this reset output on its CH32V305 controller's PC8 pin.
PC8 must be physically connected to the target nRST. The sequence is nRST low,
actively high for 20 ms, then floating, and it does not attach to the target's
SWD interface.

Hardware validation passed with WCH-LinkE v2.22 and CH32H417MEU. A 500 ms pin
reset produced a fresh `[BOOT] CH32H417 DFU` line on COM4, followed by the APP
boot log. Trace output confirmed the actual probe packets were
`81 0d 01 13`, `81 0d 01 14`, and `81 0d 01 15`.

`UPSTREAM_README.md` is the README distributed with this binary. The license
texts are provided in `LICENSE-MIT` and `LICENSE-APACHE`.
