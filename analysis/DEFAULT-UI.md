# Default UI and Columns UI support

## Design and compatibility

Both host adapters use the same `SpectrumWindow`, settings dialog, visualisation stream, FFT/amplitude processing, peak animation, frame timer, monitor tracking, GDI renderer and Direct2D renderer. No audio or animation algorithm was changed for Default UI support.

- DLL: `foo_uie_channel_spectrum.dll`, unchanged on all three architectures.
- Columns UI GUID: `0c45a893-4607-4914-8e64-2189b3a27970`, unchanged.
- Interface-language configuration GUID: unchanged.
- Default UI element GUID: `f169149b-4249-47ee-9f47-8b9aab65b80d`, a new independent registration.
- Payload remains format 6, with the same little-endian words. Versions 1 through 6 and empty/default configurations remain accepted. Preset import/export is unchanged.
- Each panel owns its own settings and rendering window. Language remains the existing shared preference.
- Default UI saves the current settings, including Apply/import and right-click style changes, rather than an outdated construction-time configuration.
- Existing Columns UI layouts remain intact. Switching user interfaces does not automatically translate the host layout; presets can be used in either interface.

## Host boundaries

Default UI supplies the UI element interface, layout configuration, visibility notifications and layout-editing context-menu hooks. Its child window is explicitly shown during initialization. Columns UI retains its existing panel registration, configuration popup and window-transfer sequence.

Both adapters destroy their child window before releasing host references. Native `WM_NCDESTROY` clears the stored handle if the parent destroys the child first. Modal configuration and context-menu calls retain the service; the window procedure keeps that reference alive through exception handling. Timer shutdown, monitor detachment, visualisation stream release and rendering-resource cleanup remain in the shared window destruction path.

## Automated checks and review

`tests/ui_adapter_tests.cpp` runs the production adapters against the actual SDK configuration/service classes, simulated host callbacks and a substitute native child window. It covers payload byte compatibility, historical formats, invalid/truncated payloads, custom values, current-state serialization, independent instances, menu ID offsets, visibility/theme forwarding, both destruction orders, failed initialization, exception cleanup, Columns UI transfer and 25 repeated lifecycle cycles.

The substitute window intentionally does not obtain a real playback stream or render audio. Existing core tests separately cover spectrum/peak behavior, paused style changes, rendering, resource release, monitor events, timers and Direct2D failure fallback. For native execution, the adapter harness builds the SDK `shared.dll` from bundled source only into the test output directory; that DLL is never included in the component package. ARM cross-compilation links the SDK import library without building this test-only runtime, because the executable cannot run on the x64 build machine. Native ARM test execution requires the matching ATL build tools and SDK runtime build.

Review fixes include retaining the service through window-procedure exception reporting, dropping a menu command if its window was destroyed while the menu was active, guarding fallback refresh restarts with host visibility, preserving Columns UI transfer order, and explicitly showing the Default UI child.

## Pending real-host validation

Not performed as part of this change:

- Default UI on a clean foobar2000 profile without Columns UI installed.
- Adding, replacing, copying and removing elements in layout editing mode; closing the player while settings are open.
- Layout save/restart, preset import/export between interfaces and multiple independent panels.
- Hide/show, tab changes, minimize/restore, pause/resume, display changes and renderer fallback in the real player.
- ARM64EC execution on Windows on ARM.

The x86 and x64 automated suites run locally. ARM64EC binaries and test executables are cross-compiled; that is not an ARM runtime test.

## Local validation result

- x86 and x64: 2,036 SDK adapter checks and 441 production-window lifecycle checks per architecture; 20,646 core checks in each of three style runs per architecture.
- Cross-architecture synthetic previews: byte-identical.
- ARM64EC: plugin and all three test executables compiled, not executed.
- Unified package: exactly three architecture DLLs, README and third-party notices; entry hashes and package SHA-256 verified.
- Post-fix review: no further actionable issue identified in the inspected adapter/configuration/window-lifetime changes, including the hidden-stream release fix. This does not establish real-host compatibility or rule out undiscovered defects.

Evidence: `build/real-window-all-build.log`, the architecture-specific `ui-adapter-tests.log`, `spectrum-window-tests.log`, and core test logs.

## Production-window lifecycle harness

`tests/spectrum_window_tests.cpp` runs the production `SpectrumWindow`, host adapters, settings dialog resource/procedure, frame timer, monitor hooks and renderers. Only foobar2000 core services (playback state/callback registration, synthetic spectrum data, language configuration store) and UI host callbacks are simulated. No player process or audio device is started. Native windows are placed offscreen.

Coverage includes host visibility notifications, native ancestor hide/show events, two active panels, both window/service destruction orders, real Columns UI close/reopen, queued playback notifications after HWND destruction, removing a panel while its context menu is tracking, and destroying the child/parent or releasing the host reference while the real settings dialog is open. Apply, Cancel and window-close paths are exercised. Repeated lifetimes check stream/callback/monitor counts and GDI, USER and process-handle counts after warm-up; these are not a general heap-leak detector.

The harness exposed a hidden-window stream retention bug: an ancestor visibility event could stop the timer in `start_timer()` before `tick()` released the visualization stream. The inactive-hidden branch now releases the stream directly and resets the stream timestamp. Paused visible windows retain their previous behavior, and spectrum/amplitude/peak algorithms and preset format remain unchanged.

The build runs native-window tests with a timeout and requires a completion marker in addition to exit code zero, since an SDK fatal path in an incomplete test fixture can exit without a nonzero status. ARM cross-compilation builds this harness but does not execute it.
