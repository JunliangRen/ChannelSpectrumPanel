# Stability review and fixes (0.5.1)

This review covers engine state, drawing resource ownership, stream validation, and preset parsing. It is not a claim of exhaustive stability or actual player validation.

## Confirmed issues fixed

- Paint exceptions could bypass EndPaint. A scoped PaintSession now pairs BeginPaint/EndPaint on normal and exceptional exits. Bitmap selection failures also discard unusable panel surfaces.
- Simple-line allocated a GDI pen before allocating point vectors. An allocation exception could leave the pen selected and leaked. SelectedPen now restores the previous pen and deletes its own pen on every exit. Memory-surface bitmap selection failures are checked as well.
- Engine rebuild published dimensions before allocating all descriptors and peak arrays. A partial allocation failure could cause the next update to skip rebuilding and access incomplete vectors. Failed builds now reset cached engine state before rethrowing.
- Invalid input frames cleared visible output but retained held peaks. They now reset the engine. Panel input validation uses division to avoid overflowing bins * channels on x86, rejects fewer than two bins, clears stale output, and resets the stream timestamp. Hide, destroy, and applied settings also discard obsolete timeline state.
- Horizontal color selection multiplied a level by 24 and converted to int before clamping. Huge or infinite levels could overflow the conversion. The level is now bounded before conversion; NaN and negative levels select the lowest color.
- A CSMX-only file was accepted as an empty host configuration and reset settings to defaults. File parsing now rejects this truncated preset and preserves settings on failure. Empty host configuration and original CSP short-prefix semantics remain supported.

## Offline evidence

Both x86 and x64 Release builds passed 10,497 checks per invocation, with three preview styles on each architecture. Added tests cover:

- Invalid frame -> silence recovery without stale held peaks; explicit reset and speaker-layout changes.
- Failure injection at 32 successive allocation positions during resize, followed by retry at the same dimensions and comparison against a fresh engine. The allocator override exists only in the test executable.
- Real GDI memory-DC pen restoration after injected exceptions, stable GDI object counts over repeated failures, and repeated DIB resize/destruction.
- Alternating renderer settings to check that shared caches do not leak colors between panels.
- Huge, infinite, NaN, negative, and normal color levels.
- Every truncated CSMX prefix, unchanged settings after failed import, valid complete import, empty host configuration, and original header-only prefixes.

The original coordinate fixtures still match: zero differences across 17,637 descriptors in 13 cases. Corresponding x86/x64 synthetic BMP previews match byte for byte. Builds retain existing vendor SDK warnings.

## Remaining validation

No player was launched and no visible test windows were opened. Actual WM_PAINT exception delivery, host window lifecycle, pause/seek/track changes, dialog interaction, transparency, DirectDraw timer/monitor behavior, multi-panel host integration, and long playback sessions still require player tests. PaintSession is covered by source inspection/builds, not an actual window-message test. GDI tests use memory DCs and do not establish graphics-driver or playback stability.

## Follow-up performance review

- WndProc error reporting now marks the error before calling a sink, catches sink failures, and avoids the allocating formatter/destructor path. A throwing sink regression verifies that reporting does not propagate exceptions and runs only once.
- Each panel owns its gradient and contour caches. Different panel dimensions/settings no longer evict each other's caches. Only Vertical generates the foreground gradient; Simple-antialiasing generates one AA ramp and Horizontal-antialiasing generates 24. Other styles skip AA ramp construction. GDI surfaces are released on window destruction.
- Engine channel lists and spectral accumulation buffers reuse their capacity. Contour channel point lists and physical ordering lists are also retained per panel. Changing dimensions, bins, styles, or channel layouts may still allocate.
- Stopped playback requests a repaint once per transition while retaining polling to detect resumed playback. A 1,000-poll regression and play/stop transition check cover this decision logic. Size/theme/settings invalidation is unchanged.
- Zero rotation draws directly into the destination DIB and skips the final full-image rotation copy. Nonzero rotations retain scratch storage; transparent backgrounds still require restoring the cached background. Tests verify destination aliasing without scratch allocation and retain existing rotation checks.
- All eight styles alternate two independently sized/configured panels under a test allocator that rejects every C++ allocation after warm-up. The ten subsequent frames per style pass and preserve expected pixels. This excludes OS/GDI/internal host allocations and is not a real-time CPU benchmark.
- All six synthetic preview files (three per architecture) remain byte-identical to the pre-optimization output. Existing 17,637 descriptor comparisons remain unchanged.

Actual player testing remains pending. These tests do not measure end-to-end playback latency, real UI responsiveness, CPU percentage, or DirectDraw behavior.

## Timer fallback, paused redraw, and preset saves

- The regular timer is established before the scanline fallback message is logged. Timer creation failure is checked; logger exceptions cannot prevent the fallback from running. Unit tests inject timer and logger failures.
- The last successful spectrum and its channel/bin/rate/mask metadata are retained for paused redraws. Resize/show/settings changes mark the layout dirty; paused polling rebuilds once from that snapshot without fetching new audio or advancing existing peak state. Rebuilding geometry initializes peaks for the new layout. Stop, invalid input, and destruction invalidate the snapshot. Actual window-message behavior still needs player validation.
- Preset export writes to a unique temporary file in the destination directory. Checked write, FlushFileBuffers, and CloseHandle precede replacement with MoveFileExW. A failed transaction removes its temporary file and reports failure. This is not a guarantee against all power-loss or remote-filesystem failure modes.
- Tests inject failure at each save stage, verify close ownership, save and replace real test files, lock a destination to force replacement failure, verify its original bytes remain unchanged, and check temporary-file cleanup. Tests use only an isolated build subdirectory.

Final x86/x64 builds pass 10,497 checks per invocation across three styles each, with identical cross-architecture previews and unchanged original coordinate fixtures. No player was launched.

## Snapshot provenance and background save lifetime

- A snapshot records its FFT normalization mode. Paused redraw uses that mode even if the requested mode changes; the next successful live frame adopts the new mode.
- Panel instances register main-thread playback callbacks for starting, new track, stop, and seek. These invalidate cached frames even while the window is hidden and refresh timers are stopped. The SDK callback base unregisters on instance destruction. No worker accesses playback services or panel state.
- Preset export now queues a Windows thread-pool callback after the save destination is selected. The job owns copies of the path and bytes, and a shared completion result. It never captures HWND, dialog state, or Panel pointers. Only the live dialog polls the atomic result and displays errors; destruction cancels polling and releases its reference without joining a blocked storage task. Closing the dialog does not cancel an already-started save. Reopening creates independent dialog state.
- The task acquires a module reference before submission. FreeLibraryWhenCallbackReturns releases it only after callback-local destructors have completed. Submission failure deletes the task and releases the reference. Error messages use fixed-size task-owned storage, published by release/acquire synchronization. The callback catches standard and unknown exceptions.
- Tests cover both FFT-mode transitions, snapshot invalidation, thread-pool submission failure, worker exceptions, blocked work running off the caller thread, dropping the caller's result reference while work is pending, eventual state release, a new independent task, and a real background file save with copied input bytes.
- Tests exercise the shared snapshot state and real background worker, not actual host playback notifications or dialog messages. Component DLL unload during an in-flight save is protected by module ownership and documented Windows callback semantics; it has not been exercised inside foobar2000. A process exit can terminate outstanding work; no power-loss durability guarantee is made.

Windows API references: [thread-pool submission](https://learn.microsoft.com/en-us/windows/win32/api/threadpoolapiset/nf-threadpoolapiset-trysubmitthreadpoolcallback), [deferred module release](https://learn.microsoft.com/en-us/windows/win32/api/threadpoolapiset/nf-threadpoolapiset-freelibrarywhencallbackreturns).

## Same-path background-save coordination

A process-local registry reserves each normalized absolute target path before queuing a save. Path comparison is case-insensitive and lexical normalization folds dot/dot-dot components. The worker owns the reservation independently of dialog/result lifetime. It releases the reservation before publishing success or failure; allocation/setup/submission failures release it through ownership cleanup. Completed result objects do not keep paths occupied.

The registry lock protects only reservation insertion/removal, never filesystem I/O or waiting for worker completion. Different targets can run concurrently. No disk canonicalization is performed on the UI thread: distinct junction, symlink, short-name, or drive-alias spellings are not unified. This coordination applies to this component's tasks in the same process, not external writers.

Added tests hold a real background transaction, reject same-path saves from a simulated reopened dialog after the original owner releases its reference, check relative/absolute/dot/case aliases, complete a different-path transaction while the first stays blocked, verify final file bytes, and retry after success, worker failure, and thread-pool submission failure. No actual settings windows are opened.

Both architectures pass 10,497 checks per invocation across three styles, with unchanged original coordinate fixtures and matching cross-architecture previews. Actual player tests remain pending.

## Concurrent registration and allocation-failure sweep

The reservation stress test runs eight rounds of eight actual submitting threads released from a common start gate. The winning background operation remains gated until every submitter has returned. Each round verifies exactly one owner, seven explicit path-conflict rejections, successful completion, and immediate reacquisition.

A thread-local allocation counter measures the real setup path before submission. Both x86 and x64 currently have 15 measured C++ allocation positions for this test. Each position is failed in turn; every attempt raises bad_alloc and a subsequent save to the same path succeeds. This covers path/string setup, lease creation, registry insertion, function capture, result, and work allocation without exhausting system memory. OS allocation and thread-pool failures remain separately modeled by existing tests.

Final validation: both architectures pass 10,497 checks per invocation, three styles each, with zero differences across 17,637 reference descriptors and matching cross-architecture previews.

The follow-up source review covered registry ownership and locking, task submission/completion, module lifetime, UI polling/destruction, snapshot callbacks, engine recovery, GDI caches/resources, presets, and scanline teardown. No new actionable defect was identified. This is source review plus bounded offline tests, not proof of absence of races/leaks or real host stability. Actual dialog destruction, playback callback integration, module unloading in foobar2000, graphics-driver behavior, and filesystem aliases beyond lexical normalization remain outside the completed validation. No production code changes were needed for this test/review pass.
