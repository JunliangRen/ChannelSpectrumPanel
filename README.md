# Channel Spectrum Mirror (x86 / x64 / ARM64EC)

Version: **1.0**. An independent reconstruction of a spectrum visualization panel for **x86, x64 or ARM foobar2000 2.x with matching Columns UI (ARM64EC component)**. Full behavioral and pixel-level parity has not yet been established.

## Original component and credit

The original **Channel Spectrum panel**, `foo_uie_vis_channel_spectrum`, was developed by **ssenna**. It is a 32-bit Columns UI visualization that displays a separate frequency spectrum for each audio channel. Its mirrored layout and falling peak markers are the reference for this project.

- [Original author's component release/download page](https://foo2k.chottu.net/) (Japanese): lists **v0.17.2, released May 18, 2008**, and the original requirement of foobar2000 v0.9.5.2 or later with Columns UI.
- [Original component discussion thread](https://hydrogenaud.io/index.php/topic,59249.0.html), linked from the component's Hydrogenaudio entry.
- [Hydrogenaudio component entry](https://wiki.hydrogenaud.io/index.php?title=Foobar2000%3AComponents%2FChannel_Spectrum_panel_%28foo_uie_vis_channel_spectrum%29): identifies ssenna, the x86 architecture, and the original component.

Credit for the original component belongs to ssenna. This repository is **not an official update, an author-endorsed release, or recovered original source code**. It contains a separately written implementation based on analysis of version 0.17.2. The original DLL is not distributed here. The version numbers of this reconstruction are independent of the original component's releases.

## Installation

Build the component using the instructions below. In foobar2000, select the matching `dist/foo_uie_channel_spectrum_x86-1.0.fb2k-component`, `dist/foo_uie_channel_spectrum_x64-1.0.fb2k-component`, or `dist/foo_uie_channel_spectrum_arm64ec-1.0.fb2k-component` through **Preferences > Components > Install**, apply, and restart. Add **Visualisations > Channel Spectrum Mirror** to the Columns UI layout. Right-click the panel and select **Configure**. The **Color Mode** submenu switches directly between the six original styles and marks the current selection.

Configuration formats 1, 2, 3, 4, and 5 of this reconstruction remain readable. Defaults retain mirrored left/right channels, bass at the center, gray-white gradient bars on black, and bright peak markers. The current configuration format is version 6; older component versions cannot read it. The player, Columns UI, component, and MSVC runtime must use matching architectures. All three targets use the same configuration format and panel GUID. Supporting x86 does not establish compatibility with foobar2000 1.x or older Windows versions.

Version 1.0 replaces the oversized settings sheet with five grouped native tabs: Appearance, Colors, Spectrum, Channels, and Performance. Color swatches open the native Windows color picker for all five colors and all sixteen palette entries, with synchronized RGB hex inputs. Input channels use speaker names. Enabled channels and their display order share a checkable list with Move up / Move down, Reset order, and Enable all / Disable all controls. Reordering keeps each speaker's enabled state. Palette text is available through Edit values editors for copying and pasting, while the main page shows color swatches. Peak hold and tick lengths offer named presets plus exact custom numeric input (tick lengths 1-99 are pixels, 0 hides ticks, and 100 spans the graph); imported nonstandard values are labeled Custom. Peak fall and blur have synchronized sliders and numeric inputs; widths, spacing, padding, and amplitude scale have spin buttons. Rotation choices explain their direction. Related controls are grouped and enabled only when relevant, without discarding their stored values. All settings remain available; hidden-page numeric validation switches to the affected tab. Native dialog tests cover control bounds, label widths, page visibility, dependencies, channel reordering, slider synchronization, and exact custom-value round trips through both original and legacy Mirror presets. Actual player UI testing remains pending.

Version 0.5.1 fixes exception cleanup, allocation-failure recovery, stale snapshots, extreme-level color conversion, and truncated preset imports. Preset export runs in the background; closing the settings dialog does not cancel an export already started. A second save to the same normalized path is rejected until the first job finishes; different paths may save concurrently. See the [stability review](analysis/STABILITY-0.5.1.md).

Version 0.5.0 adds dual-architecture builds; see the [architecture and validation notes](analysis/DUAL-ARCHITECTURE.md).

Version 0.4.1 fixes forced Left/Right combined with Reverse frequency. See the [direction regression record](analysis/DIRECTION-0.4.1.md).

## Target refresh rate

Spectrum > Target FPS accepts integer values from 10 to 200 FPS, with presets for 30, 60, 90, 120, 144, and 200. This is a requested update rate, not measured FPS or display synchronization. Legacy millisecond intervals are converted to FPS with round-half-up rounding: 14 ms becomes 71 FPS, and 16 ms becomes 63 FPS. Earlier fractional format-4 FPS values are rounded in the same way. The manual FPS field retains its existing storage unit for compatibility but only integer manual FPS values are used. Format 5 adds the monitor-following flag. The legacy interval is retained separately as the peak-animation reference cadence.

Enable **Match current monitor** to use the current display mode's refresh rate, including fractional rates such as 60000/1001 Hz. The status line identifies the panel's display and reports its rate to three decimal places. This follows the spectrum panel, not a settings dialog moved onto another screen. Windows chooses the monitor with the largest panel overlap (or the nearest monitor); each panel tracks its own display. A process-scoped Windows location-event hook observes the panel and its current ancestors, including top-level player movement. A hidden top-level listener receives display/device/settings broadcasts. Movement on the same monitor does not query display configuration; monitor changes and display notifications are coalesced over 50 ms before querying. The settings dialog reads the same cached result and receives change notifications, with no independent polling. A 5-second polling fallback is enabled only if listener registration fails. If that fallback timer also fails, drawing continues at manual FPS and the settings status reports that monitoring is unavailable. Failed or integer-only display queries receive at most three delayed precision-recovery attempts (100/250/500 ms); success or window destruction cancels the retry, and exhaustion returns to idle until a new event. Hooks and pending queries are removed when the panel is destroyed; ancestor matching follows reparenting automatically. Only rate changes restart scheduling; peak state is preserved. Manual entry remains integer-only, while auto mode accepts reported rates from 1 to 1000 Hz.

Detection uses [QueryDisplayConfig](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-querydisplayconfig) and matches the active source device to the panel's monitor. Fractional rates are kept as a rational-derived double, not rounded for scheduling. Drivers that only support the legacy mode query are labeled as an integer report. If neither query works, the status line reports the fallback manual FPS. This follows the OS-reported display mode, not instantaneous VRR, and does not provide VSync. Cloned displays share a Windows source and use its first active path; extended displays are matched independently.

Peak hold and falling motion advance by elapsed active time rather than by the number of rendered frames. Changing FPS therefore does not change the falling speed for a given configuration. Legacy fixed-point acceleration and step size remain the reference, evaluated in closed form without a catch-up loop. Paused/hidden intervals do not advance the animation clock.

High-frequency scheduling stops when playback pauses or stops, and when the panel is hidden. Playback callbacks and ancestor visibility events restore it; paused resize/settings redraws use the retained spectrum without advancing peaks. Normal refresh uses QPC absolute deadlines with a 1 ms multimedia-timer wake-up request. Worker callbacks only post a coalesced message; audio processing and drawing remain on the UI thread. Missed frame deadlines are skipped, old timer generations are ignored after restart, and stop waits for callbacks before releasing state. If timer creation fails, the component falls back to the Windows UI timer. Experimental scanline synchronization remains optional and also uses fractional QPC deadlines. System scheduling, UI load, and display composition still affect actual presentation; precise FPS and VSync are not guaranteed.

Use `.csm` to preserve monitor-following mode, manual FPS, and the animation reference. Original `.csp` files can only store integer milliseconds, so export snapshots the current rate (within the original 5-100 ms range), loses monitor-following mode, and rounds the target interval and cannot independently retain the new animation reference when it differs. Original presets and old `.csm` files remain importable. Older component builds cannot read newly saved format-6 `.csm` files.

Existing x86/x64 tests cover preset compatibility, FPS conversion, equal-time peak motion at 30-200 FPS, deadline drift/skip, message coalescing, and timer stop/restart checks. Additional tests cover monitor-mode persistence, format-4 migration, fractional refresh rates, simulated transitions between different display rates, fallback selection, notification coalescing, same-monitor query suppression, native ancestor location events, reparenting, cancellation with a pending query, bounded retry recovery/exhaustion, monitor setup failure isolation, idle refresh gating, and ancestor visibility notifications. These are offline and hidden-window tests, not actual foobar2000 frame-pacing measurements or a physical two-monitor drag test.

## Optional Direct2D backend

Enable **Spectrum > Direct2D (GDI fallback)** to request hardware drawing. It is off by default, including when importing older complete presets. The spectrum calculations, channel layout, animation reference interval, peak timing, manual FPS and monitor-following behavior are unchanged. Rendering uses physical-pixel coordinates and supports all four rotations.

The backend supports Simple, Horizontal, Simple-antialiasing, Horizontal-antialiasing, Separate thin lines, Simple-line, Vertical (including the eight-color foreground palette) and Gradient bars, including peak markers and ticks. Eight-color background palettes are also supported. Segmented bars and parent-background rendering automatically use GDI without changing saved settings. Gradient brushes are cached by color; resizing and animated bar heights update geometry without rebuilding them. Device/target creation, resize or drawing failure falls back to GDI for the rest of the window session; Apply or window recreation allows another attempt. Resources are reused across frames and released with the window. GPU rasterization and gradients may differ slightly from GDI. This is not a new VSync/flip-model presentation implementation and no performance improvement has been measured yet.

Format-6 `.csm` presets preserve the backend preference; `.csp` cannot store it. Automated tests exercise native Direct2D primitives and rotations through an offscreen **software** target. Software HWND targets additionally cover injected Create/Resize/EndDraw failures (target recreation, device removal and out-of-memory errors), same-frame GDI fallback, retry suppression, recovery after reset, COM resource release and 25 native window lifetimes with GDI/USER object-count checks. The lifecycle harness calls the same renderer reset used by panel teardown; it does not instantiate the foobar2000 panel. Gradient tests compare 384 software-rendered scenes against GDI across tiny and regular sizes, all rotations, color edits, palette/style switches and cache reuse (at most two RGB levels allowed). New gradient resources are also checked after device loss and destruction. Actual GPU device loss, hardware rendering, ARM execution and player integration are still untested.

## ARM target

The ARM edition of foobar2000 uses **ARM64EC**, not a plain ARM64 component ABI ([official changelog](https://www.foobar2000.org/changelog)). The pinned SDK includes ARM64EC configurations and `shared-ARM64EC.lib`. The plugin uses the same Columns UI interface and panel GUID. The ARM package is intended for Windows 11 on ARM with a compatible Columns UI installation; host loading has not been tested.

x86/x64 retain their x87 mapping routines. ARM uses the equivalent double-precision C++ mapping formula, so pixel-boundary rounding can differ. Host tests compare that formula against the x87 helper; a separate `CspPortableMapping=true` test build exercises the complete C++ mapping path on x64 (34,107 assertions and zero differences in the current 17,637-row fixture); native ARM tests report original-fixture differences and enforce a one-bar-pitch coordinate bound instead of claiming bit-exact x87 parity. No FFT amplitude or peak animation formula was changed for ARM.

ARM64EC compilation requires the Visual Studio **v143 ARM64/ARM64EC build tools**. The plugin and native test executable have been cross-compiled successfully, and the package has been checked for ARM64EC CHPE metadata. This x64 development machine cannot execute the ARM64EC test binary. Native ARM execution, GPU rendering and player integration remain untested.

## Features

- Original-style modes: Simple, Horizontal, Vertical, Simple-antialiasing, Horizontal-antialiasing, and Simple-line. Gradient bars and Separate thin lines remain available as additional modes.
- Mirror / Right / Left directions, frequency reversal, channel-position reversal, and 0/90/180/270-degree clockwise rotation.
- Separate / Merge all / Specified input; optional display of all input channels, with an **Enabled channels / display order** checkable list for all 18 named speaker positions. The list retains the original speaker-bit encoding internally, so existing `.csp` and `.csm` presets remain compatible; no hexadecimal mask entry is needed. Speaker positions map to input channels through the host channel mask.
- Bar width, spacing, segmented bars, left/right padding, channel gaps, and panel borders.
- Five base RGB colors and eight-color foreground/background palettes.
- Frequency ticks at 2 kHz intervals, amplitude ticks, and separate short/long tick lengths. A length of 100 spans the drawing area.
- Peak visibility, hold time, and fall speed; old/new FFT modes, FFT size, amplitude curves, and frequency-axis scaling.
- Parent-window background rendering and caching, plus experimental DirectDraw scanline synchronization; disabled by default.
- Apply / Cancel / Defaults; versioned `.csm` presets, full 300-byte original `.csp` presets, and older DWORD-aligned preset prefixes.

The original disables peaks in Simple-line mode; this implementation intentionally allows them. Gradient bars and Separate thin lines are additional styles and cannot be exported as original `.csp` presets; use `.csm` instead. The original Separate mode has no equivalent of this project's forced stereo default. Reimporting a `.csp` uses all active input channels.

## Validation and limitations

The previous GDI Release builds passed 10,497 offline assertions per test invocation; current counts are recorded in `build/<architecture>/tests-*.log`. Each architecture runs three preview styles; corresponding previews match byte for byte. The count includes per-bar boundary checks, not that many independent scenarios. Coverage also includes mapping under all four caller rounding modes, floating-point control-state restoration, allocation-failure recovery, GDI cleanup under exceptions, renderer cache isolation, invalid-frame recovery, truncated preset rejection, per-panel cache isolation, allocation-free warmed C++ frame buffers, one-time stopped repaint, and exception-safe logging. Coverage includes configuration migration, field round trips, original preset fields and color byte order, legacy prefixes, channels, rotation, styles, peaks, padding, and buffer boundaries.

Under a controlled x87/SSE emulation profile, 17,637 valid four-DWORD frequency descriptors matched across 13 fixtures. Eight amplitude ticks, speaker mapping, native GDI gradients and polylines, and short preset prefixes have also been implemented.

A separate portable test player reached its initial UI selection dialog, but window automation failed with `failed to activate captured window` after recovery. Columns UI panel creation, settings interaction, actual playback, and stability testing remain incomplete. The existing player configuration was not modified.

Unverified areas include rotation boundary pixels, cross-channel contour endpoints, bin 0 ticks, different floating-point environments, transparency/cache behavior, scanline scheduling, and actual host FFT input. Controlled emulation does not replace playback comparisons with the original. See the [0.4.0 reconstruction record](analysis/RESTORATION-0.4.md) and [parity checklist](analysis/PARITY.md).

## Building

Requirements: Windows, Visual Studio C++ v143 tools including MASM, Windows SDK, PowerShell, Git, and 7-Zip.

```powershell
.\scripts\setup-deps.ps1
.\scripts\build.ps1
```

The default build targets **x86, x64 and ARM64EC** and creates three separate packages only after all selected builds succeed. Use `-Architecture x86`, `-Architecture x64` or `-Architecture arm64ec` for one target. The x64-named project file is retained for existing tooling. All targets require v143 tools; ARM additionally requires the ARM64/ARM64EC compiler and libraries. ARM tests are compiled but explicitly skipped on non-ARM Windows hosts. Run them on Windows on ARM before claiming native test coverage. x86/x64 synthetic previews must match byte for byte; ARM is not held to x87 pixel-identical output. Dependencies remain pinned. Packages are written to `dist/`, DLLs to `dist/<architecture>/`, and tests/logs/previews to `build/<architecture>/`. ARM packages contain an `arm64ec/` DLL, with ARM64EC metadata validated before packaging. Debug PDBs are excluded. Project-authored text uses UTF-8 and CRLF; downloaded SDK files retain their upstream formatting.

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

The settings dialog opens on Appearance. Style-dependent blur, secondary colors, foreground palettes and segmented-bar controls explain or disable inapplicable editing without clearing saved values. Parent-background mode preserves the inactive background palette. Frequency and amplitude scales use explicit choices. Validation selects the appropriate page, including Performance for manual FPS errors. Backend messages describe the draft preference and compatibility, not a claim that the GPU is currently active. No live preview is applied.
