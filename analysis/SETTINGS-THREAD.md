# Settings interaction stalls

## Evidence

The user interaction log `ChannelSpectrumPanel-interaction-52008.log` showed:

- Opening settings: 469 ms without a completed paint; 156 ms before dialog initialization and 297 ms inside initialization.
- Holding the caption: 49 completed paints in 922 ms, with reduced responsiveness.
- Holding Close: 2,156 ms without a dispatched refresh, FFT request, or paint, despite 1,080 timer callbacks.

Posting WM_TIMER instead of WM_APP did not resolve these stalls. The earlier restricted-pump test was not a reproduction of the native Close tracking loop.

## Implementation

Settings now run on a dedicated UI thread. The spectrum, playback control, configuration Apply and persisted language remain on the host thread. Playback control is explicitly main-thread-only in the foobar2000 SDK.

The dialog has a hidden owner on its own thread. It deliberately has no cross-thread owner relationship with the player: that would implicitly attach input queues. The original root is disabled on the host thread and restored only if its lifetime marker still matches. The host pumps messages while waiting for the settings thread to finish, including posted Apply requests sent to a private message-only bridge.

The dialog thread owns its controls, fonts, monitor watcher, COM apartment and file/color dialogs. No panel object is accessed there. It inherits the caller's DPI awareness context when the OS supports that API. Dialog positioning uses a captured owner rectangle, bounded by its monitor work area. The existing foreground instance retention remains in the host adapters and context-menu handler.

Worker completion is joined before destroying the bridge or releasing stack state. WM_QUIT is forwarded to the worker and restored to the host queue after cleanup. Reopening the same panel's settings reentrantly is suppressed. Config encoding, GUIDs, rendering algorithms and animation clocks are unchanged.

## Validation boundaries

The real SpectrumWindow harness blocks the settings thread completely for 400 ms during initialization and again during later dialog processing. It checks both successful FFT reads and completed paints continue, for GDI/Direct2D and both scanline-sync settings. It verifies Apply and language saving execute on the host thread, custom values survive, and the host is re-enabled. Existing removal, parent destruction, host-reference release, cancel, close and handle-growth tests now cover the threaded dialog. Additional tests cover WM_QUIT propagation and dialog-creation failure cleanup.

This isolates settings-window interactions; it does not make rendering independent of arbitrary stalls elsewhere on foobar2000's main thread. The automated harness is separate from actual player testing. User testing of the fix recorded about 120 completed paints per second while holding Close, and continued painting through six dialog opens. Interaction diagnostics have been removed for release. The test harness now observes WM_PAINT completion without production counters. ARM hardware testing remains outstanding.

## Release review

The shutdown loop retries WM_QUIT delivery with a bounded wait while the worker queue is not yet available. A per-panel lifetime marker prevents the settings monitor watcher from querying or notifying a reused HWND. Monitor tracking uses the actual panel window independently of the dialog owner passed by Columns UI. Regression tests cover delayed worker startup, thread launch failure, dialog creation failure, and target lifetime marker replacement.

Identity-guarded settings observers never post private refresh or visibility messages to the host panel. The host already has its own main-thread observer; this avoids a cross-thread HWND destruction/reuse race between a validity check and PostMessage.

Apply and language updates use posted bridge messages with per-request completion events. They are not dispatched as synchronous sent messages inside rendering calls. The settings thread services incoming synchronous window messages while awaiting completion; the request stays alive until the host signals its completion. Tests assert that Apply is not running in a sent-message context.

Final release validation: `build/release-1.3.1-final-reviewed.log` passed on x86 and x64 (555 real-window checks, 2,036 adapter checks, 696 worker/DLL-unload checks and three runs of 20,782 core checks per architecture). ARM64EC plugin and test targets compiled, but were not executed. The separate save stress run completed 500 iterations / 9,500 checks on each executable architecture. Unified package hashes matched all three DLLs, and the final DLLs contain no temporary startup or interaction log markers. Final review found no further actionable issue in the reviewed changes.
