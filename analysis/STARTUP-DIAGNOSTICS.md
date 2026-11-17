# Startup diagnostic investigation (historical)

Diagnostic builds 1-6 were local test artifacts and were not published releases. The diagnostic code, logging timer, extra FFT/PCM probes and `-Diagnostics` build/package switch have now been removed. The notes below describe historical measurements and intermediate implementations; normal builds retain the startup optimizations and save fix. Existing local logs remain as investigation evidence.

## Evidence

The first supplied player log had two UI-thread DirectDraw initializations of 123.368 ms and 29.280 ms. Diagnostic 2 removed the redundant initialization. The subsequent real-player log (31892) still showed a 119.134 ms first initialization, 41.442 ms from stream creation to first spectrum, and 173.331 ms from playback-start notification to the first spectrum paint. The user reported a large improvement with scanline synchronization disabled. Different playback-start timing prevents attributing the earlier 918 ms spectrum-data wait to this optimization.

## Thread ownership and fallback

DirectDraw creation, GetScanLine and release now run on one dedicated worker per scanline device lifetime. DDSCL_NORMAL permits a null HWND; the worker receives neither a panel HWND nor a SpectrumWindow pointer. The UI owns the timer and its generation tokens, and consumes an atomic scanline sample. A steady-clock timestamp rejects samples older than 4 ms. This remains an experimental, approximate scanline heuristic rather than a presentation/vblank guarantee.

The existing deadline-based multimedia timer continues producing frames while initialization is pending or fails. It requests samples without waiting for the driver; no graphics driver call executes in the UI-thread scanline path. Sample requests coalesce through an auto-reset event. Paused devices sleep rather than poll. Resuming reuses the device; disabling synchronization or destroying the panel signals cancellation without joining a potentially slow driver call. Initialization failures are retained until a full reset, avoiding retries on ordinary host events.

The worker owns shared cancellation state and uses `_beginthreadex`. The v143 UCRT retains the entry-point module and performs CRT cleanup and module release when the worker returns normally. The worker never calls ExitThread or FreeLibraryAndExitThread directly. After cancellation it finishes any in-flight driver call, releases resources on its owning thread and returns. No forced thread termination is used.

A module-wide atomic admission limit allows at most four outstanding driver tasks across all panels. Cancelled tasks retain their slots through driver cleanup, so toggling or recreating panels cannot accumulate an unbounded number of blocked driver calls. Admission or thread-creation failure uses the regular timer fallback. Allocation and launch failure release their slots. A driver that never returns may retain one of these bounded slots and its module reference until process exit, but the panel/UI never joins it. Retrying after saturation requires toggling synchronization or reopening a panel after capacity is available.

No settings format, FFT calculation, peak animation math, UI host GUID or DLL name changed.

## Diagnostic events

Logs remain at `%TEMP%/ChannelSpectrumPanel-startup-<PID>.log`. All capture-buffer writes remain on the UI thread. `scanline.initialize` now measures dispatching the worker, not driver initialization. `scanline.worker_ready_us` / `scanline.worker_failed_us` report worker status when first observed on the UI thread; their detail field is initialization duration in microseconds (zero if an exception prevents measurement). They do not imply that the UI was blocked for that duration. Existing playback, spectrum availability, paint, reuse and unchanged-timer events remain available.

## Regression coverage

Tests gate a fake driver initialization while verifying that start returns, timer frames remain available, cancellation returns and the panel can be destroyed before initialization completes. Success, failure and exception paths all release on the worker. Additional cases exercise failure caching, pause/resume, fresh sample publication, repeated reopen, stale timer generations and device reuse. Existing real SpectrumWindow tests cover hidden/restored panels, removal, and destruction with settings open, using synthetic host/audio services.

Actual foobar2000 playback and ARM hardware execution are deferred to the user.

Reference: https://learn.microsoft.com/en-us/windows/win32/api/ddraw/nf-ddraw-idirectdraw7-setcooperativelevel

## Diagnostic 3 validation and review (historical)

The final diagnostic-3 build passed 2,036 adapter checks, 469 production-window lifecycle checks, and three runs of 20,744 core checks on each of x86 and x64. Synthetic preview files matched byte for byte across those architectures. ARM64EC plugin and test executables compiled; runtime execution was skipped. Unified package entries and DLL hashes passed verification. Evidence: `build/diagnostic3-final-build.log`.

A first run exposed a test fixture using default Sleep resolution while checking a 4 ms sample lifetime. The fixture now acquires the same 1 ms timer-resolution lease as production ScanlineSync, and the complete matrix was rerun successfully. Review also replaced coarse GetTickCount sample aging with steady_clock timestamps. Final review covered ownership, cancellation during initialization, pending timer messages, exception cleanup, module unloading, cross-thread capture and resource lifetimes; no further actionable issue was identified. This is not proof of actual driver/player behavior.

Package SHA-256: `3add35b07958580da92d61bec5c8b0f4a8fc881b24ff4dc82a5ab26834a16508`.

## Diagnostic 4 review corrections

Diagnostic 3 review identified unbounded accumulation of cancelled driver tasks and the use of CreateThread for a CRT-using worker. Both are addressed above. The actual-unload harness compiles the production AsyncScanlineDevice into a separate DLL, blocks initialization (success/failure/exception), query and release, destroys the client, releases the host DLL reference, verifies retention while blocked, then verifies actual unload after the last worker finishes. Repeated overlap tests fill all four slots with cancelled tasks, reject 24 additional attempts, free one slot and reuse it, then verify complete unload. Thread-launch failure is injected in the core harness.

The fixture uses fake driver gates rather than a running player or hardware. The x86/x64 unload results validate the installed UCRT's module-lifetime behavior; ARM64EC runtime validation remains pending. Tests have a process timeout; a failed assertion terminates the isolated process without unwinding gate objects while workers still use them.

## Diagnostic 4 final validation

Final evidence: `build/diagnostic4-final-build-2.log`. Each of x86/x64 passed 696 worker-overlap/actual-DLL-unload checks, 2,036 SDK adapter checks, 469 production-window checks and three runs of 20,756 core checks. ARM64EC plugin, test DLL and test executables compiled; no ARM runtime tests were run. x86/x64 synthetic previews matched byte for byte and unified package contents/hashes passed verification.

The release-gate fixture was corrected to wait for successful initialization before cancelling, so it deterministically tests release rather than cancellation before initialization. An earlier full run also reported one failure at the pre-existing save-conflict completion assertion; its error was not captured, so the cause is undetermined. Failure diagnostics were added to that test. The complete rerun passed on both architectures; only expected injected write-failure messages were observed. Preset-saving production code was not changed.

Final review covered admission races, allocation/launch failure ownership, cancellation during initialization/query/release, callback generation checks, worker exception cleanup, module retention and last-worker unload. No additional actionable issue was identified in the asynchronous scanline changes. Actual player behavior and ARM hardware execution remain unverified.

Unified diagnostic-4 package SHA-256: `4e6e916216c7350fc34fdeee8e4c09ab3e89b092dfa3a3e8479635b45cb6a879`.

## Diagnostic 5

The intermittent save failure was reproduced at MoveFileExW with Win32 error 5. Bounded replacement retries and exact failure diagnostics are included in diagnostic 5; see [SAVE-REPLACEMENT.md](SAVE-REPLACEMENT.md) for reproduction, ownership review and final three-architecture validation. The earlier unexplained save-test failure note above is historical; the specific original run had no error code, but the same test now has a reproduced and fixed transient replacement failure.

## Diagnostic 6

Visible panels now register the visualisation stream before playback starts. First-spectrum availability probes are isolated from rendering and bounded per wait cycle. See [FIRST-SPECTRUM-WAIT.md](FIRST-SPECTRUM-WAIT.md) for events, lifecycle semantics, tests and final build evidence. The current capture limit is 8,192 records.

## Diagnostic removal and production review

Startup capture buffers, log writes and timer 4, timing probes in rendering/monitor/scanline paths, diagnostic-only worker status/timing fields, and extra PCM/small-FFT/offset-spectrum probes were removed. The diagnostic headers and build/package switches were removed as well. Standard save errors and test-only stress counters remain; the installed plugin has no startup log output.

The final review checked that removing instrumentation did not alter worker admission/cancellation, CRT/module cleanup, timer generation checks, early stream registration, hidden/destroyed stream release, FFT selection, animation math, or bounded atomic-save retries. No further actionable finding was identified. Tests explicitly reject extra PCM/FFT/offset requests while the normal spectrum is unavailable and after clock recovery.

`build/production-startup-final.log`: x86/x64 each passed 475 real-window checks, 696 actual-DLL-unload/overlap checks, 2,036 adapter checks, 500 save-conflict stress rounds (9,500 checks), and three runs of 20,776 core checks. The stress runs recovered 5 and 1 natural access-denied retries respectively. ARM64EC plugin and tests compiled; runtime tests were skipped. Cross-architecture previews and package content/hash verification passed. All three packaged DLLs were additionally checked for absence of startup-log/probe/version markers. Actual player and ARM hardware tests were not performed.

The local normal unified package retains version 1.3 and is not a newly published release. SHA-256: `a24dea662995dca15d073f411a187e07a623d8fe1fad1ea46b1833d6569df817`.
