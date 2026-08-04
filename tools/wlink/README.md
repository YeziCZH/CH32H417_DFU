# Bundled WLINK

This directory contains the Windows x86 native-driver build of
`ch32-rs/wlink` 0.1.2 used by this project.

- Upstream: https://github.com/ch32-rs/wlink
- License: MIT OR Apache-2.0
- Binary: `wlink.exe`
- SHA-256: `55A20C7D4B70E6A5729A901CBC0EF3BEA4D6222D6BE824F9635AE28C674745F4`

Run it from the repository root through the path-independent wrapper:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/wlink.ps1 --version
powershell -ExecutionPolicy Bypass -File scripts/wlink.ps1 list
```

`UPSTREAM_README.md` is the README distributed with this binary. The license
texts are provided in `LICENSE-MIT` and `LICENSE-APACHE`.
