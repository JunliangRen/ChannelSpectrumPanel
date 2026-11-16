# Channel Spectrum Mirror (x86 / x64)

Experimental version: **0.5.1**. An independent reconstruction of a spectrum visualization panel for **32-bit or 64-bit foobar2000 2.x with matching Columns UI**. Full behavioral and pixel-level parity has not yet been established.

## Original component and credit

The original **Channel Spectrum panel**, `foo_uie_vis_channel_spectrum`, was developed by **ssenna**. It is a 32-bit Columns UI visualization that displays a separate frequency spectrum for each audio channel. Its mirrored layout and falling peak markers are the reference for this project.

- [Original author's component release/download page](https://foo2k.chottu.net/) (Japanese): lists **v0.17.2, released May 18, 2008**, and the original requirement of foobar2000 v0.9.5.2 or later with Columns UI.
- [Original component discussion thread](https://hydrogenaud.io/index.php/topic,59249.0.html), linked from the component's Hydrogenaudio entry.
- [Hydrogenaudio component entry](https://wiki.hydrogenaud.io/index.php?title=Foobar2000%3AComponents%2FChannel_Spectrum_panel_%28foo_uie_vis_channel_spectrum%29): identifies ssenna, the x86 architecture, and the original component.

Credit for the original component belongs to ssenna. This repository is **not an official update, an author-endorsed release, or recovered original source code**. It contains a separately written implementation based on analysis of version 0.17.2. The original DLL is not distributed here. The version numbers of this reconstruction are independent of the original component's releases.

## Installation

Build the component using the instructions below. In foobar2000, select the matching `dist/foo_uie_channel_spectrum_x86-0.5.1.fb2k-component` or `dist/foo_uie_channel_spectrum_x64-0.5.1.fb2k-component` through **Preferences > Components > Install**, apply, and restart. Add **Visualisations > Channel Spectrum Mirror** to the Columns UI layout. Right-click the panel and select **Configure**.

Configurations from versions 0.1/0.2 of this reconstruction remain readable. Defaults retain mirrored left/right channels, bass at the center, gray-white gradient bars on black, and bright peak markers. The current configuration format is version 3; older component versions cannot read it. The player, Columns UI, component, and MSVC runtime must use matching architectures. Both builds use the same configuration format and panel GUID. Supporting x86 does not establish compatibility with foobar2000 1.x or older Windows versions.

Version 0.5.1 fixes exception cleanup, allocation-failure recovery, stale snapshots, extreme-level color conversion, and truncated preset imports. Preset export runs in the background; closing the settings dialog does not cancel an export already started. A second save to the same normalized path is rejected until the first job finishes; different paths may save concurrently. See the [stability review](analysis/STABILITY-0.5.1.md).

Version 0.5.0 adds dual-architecture builds; see the [architecture and validation notes](analysis/DUAL-ARCHITECTURE.md).

Version 0.4.1 fixes forced Left/Right combined with Reverse frequency. See the [direction regression record](analysis/DIRECTION-0.4.1.md).

## Features

- Original-style modes: Simple, Horizontal, Vertical, Simple-antialiasing, Horizontal-antialiasing, and Simple-line. Gradient bars and Separate thin lines remain available as additional modes.
- Mirror / Right / Left directions, frequency reversal, channel-position reversal, and 0/90/180/270-degree clockwise rotation.
- Separate / Merge all / Specified input; optional display of all input channels, with 18 speaker positions and an active mask. Speaker positions map to input channels through the host channel mask.
- Bar width, spacing, segmented bars, left/right padding, channel gaps, and panel borders.
- Five base RGB colors and eight-color foreground/background palettes.
- Frequency ticks at 2 kHz intervals, amplitude ticks, and separate short/long tick lengths. A length of 100 spans the drawing area.
- Peak visibility, hold time, and fall speed; old/new FFT modes, FFT size, amplitude curves, and frequency-axis scaling.
- Parent-window background rendering and caching, plus experimental DirectDraw scanline synchronization; disabled by default.
- Apply / Cancel / Defaults; versioned `.csm` presets, full 300-byte original `.csp` presets, and older DWORD-aligned preset prefixes.

The original disables peaks in Simple-line mode; this implementation intentionally allows them. Gradient bars and Separate thin lines are additional styles and cannot be exported as original `.csp` presets; use `.csm` instead. The original Separate mode has no equivalent of this project's forced stereo default. Reimporting a `.csp` uses all active input channels.

## Validation and limitations

Both Release builds pass 10,497 offline assertions per test invocation. Each architecture runs three preview styles; corresponding previews match byte for byte. The count includes per-bar boundary checks, not 10,497 independent scenarios. Coverage also includes mapping under all four caller rounding modes, floating-point control-state restoration, allocation-failure recovery, GDI cleanup under exceptions, renderer cache isolation, invalid-frame recovery, truncated preset rejection, per-panel cache isolation, allocation-free warmed C++ frame buffers, one-time stopped repaint, and exception-safe logging. Coverage includes configuration migration, field round trips, original preset fields and color byte order, legacy prefixes, channels, rotation, styles, peaks, padding, and buffer boundaries.

Under a controlled x87/SSE emulation profile, 17,637 valid four-DWORD frequency descriptors matched across 13 fixtures. Eight amplitude ticks, speaker mapping, native GDI gradients and polylines, and short preset prefixes have also been implemented.

A separate portable test player reached its initial UI selection dialog, but window automation failed with `failed to activate captured window` after recovery. Columns UI panel creation, settings interaction, actual playback, and stability testing remain incomplete. The existing player configuration was not modified.

Unverified areas include rotation boundary pixels, cross-channel contour endpoints, bin 0 ticks, different floating-point environments, transparency/cache behavior, scanline scheduling, and actual host FFT input. Controlled emulation does not replace playback comparisons with the original. See the [0.4.0 reconstruction record](analysis/RESTORATION-0.4.md) and [parity checklist](analysis/PARITY.md).

## Building

Requirements: Windows, Visual Studio C++ v143 tools including MASM, Windows SDK, PowerShell, Git, and 7-Zip.

```powershell
.\scripts\setup-deps.ps1
.\scripts\build.ps1
```

The default build compiles and tests both architectures, then creates two separate packages. Use `-Architecture x86` or `-Architecture x64` for a single target, and `-MSBuild` to specify an MSBuild.exe path. The Visual Studio installation must include x86 and x64 C++ tools. The x64-named project file is retained for existing tooling; it supports both targets. Dependency versions and hashes are pinned in the setup script. Packages are written to `dist/`; DLLs are written to `dist/x86/` and `dist/x64/`; tests, logs, and synthetic previews are separated under `build/x86/` and `build/x64/`. Debug PDB files are excluded from the package. Project-authored text uses UTF-8 and CRLF; downloaded SDK files retain their upstream formatting.

[Static analysis](analysis/STATIC-ANALYSIS.md) | [Initial implementation](analysis/IMPLEMENTATION.md) | [Simple-line analysis](analysis/SIMPLE-LINE.md) | [Third-party notices](THIRD-PARTY-NOTICES.txt)

## Repository contents and optional instruction emulation

The repository includes source, build/package scripts, analysis notes, selected evidence, and regression fixtures. SDKs, the original DLL, full disassembly, Python dependencies, portable players, build output, and redundant raw emulation output are excluded from Git. Existing local copies are retained.

Normal builds and regression tests use the committed fixtures and do not require Python or the original DLL. To regenerate the instruction-emulation fixtures, provide the original DLL with the expected SHA256 and run:

```powershell
python -m pip install --target tools/oracle-deps unicorn==2.1.4 pefile==2024.8.26
$env:CSP_ORIGINAL_DLL = 'C:\path\to\foo_uie_vis_channel_spectrum.dll'
python scripts/original_oracle.py
```

The emulator executes only the controlled coordinate routine and does not initialize the original DLL. Generated raw JSON is ignored; compact coordinate fixtures are retained for regression comparisons.

## Preparing host playback tests

Generate deterministic 48 kHz / 24-bit test WAVs without starting foobar2000:

```powershell
python scripts/generate-test-audio.py
```

The files, playlist, segment timeline, checksums, and measured PCM manifest are written to the ignored `build/host-test-audio/` directory. The generator uses only the Python standard library and verifies encoded channel isolation, tone frequencies, levels, sweep progression, and digital silence. Follow the [host test plan](analysis/HOST-TEST-PLAN.md) when ready. Audio-file verification does not establish plugin playback correctness.
