# Channel Spectrum Panel

A spectrum visualization panel for foobar2000 and Columns UI, with separate audio channels, mirrored frequency layouts and falling peak markers. Available for x86, x64 and ARM64EC, with English, Simplified Chinese and Japanese settings.

## Requirements

- foobar2000 **2.0 or later**, with Columns UI for the same architecture.
- The ARM64EC build requires **Windows 11 on ARM** and the ARM edition of foobar2000.

## Original component

Inspired by **Channel Spectrum panel** (`foo_uie_vis_channel_spectrum`) by **ssenna**, originally a 32-bit Columns UI component.

- [Original author's release and download page](https://foo2k.chottu.net/)
- [Original component discussion](https://hydrogenaud.io/index.php/topic,59249.0.html)

This is an independently written reconstruction, not an official update or recovered original source code. Credit for the original component belongs to ssenna; the original DLL is not distributed here. Exact visual and behavioral parity is not guaranteed.

## Building

Requires Visual Studio C++ v143 tools (including MASM and ARM64/ARM64EC tools), Windows SDK, PowerShell, Git and 7-Zip.

```powershell
.\scripts\setup-deps.ps1
.\scripts\build.ps1
```

The default build produces one package containing all three architectures in `dist/`. Use `-Architecture x86`, `-Architecture x64` or `-Architecture arm64ec` to build one target. ARM runtime tests require Windows on ARM.

## Validation

Automated tests cover rendering, preset compatibility, settings and resource lifetimes. Actual foobar2000 integration and ARM hardware validation remain incomplete.

[Parity notes](analysis/PARITY.md) · [Host test plan](analysis/HOST-TEST-PLAN.md) · [Third-party notices](THIRD-PARTY-NOTICES.txt)
