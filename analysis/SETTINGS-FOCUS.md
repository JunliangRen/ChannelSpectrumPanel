# Settings close focus restoration (issue #2)

Issue: https://github.com/JunliangRen/ChannelSpectrumPanel/issues/2

The 1.3.1 independent settings thread fixed rendering stalls, but its hidden owner is not the player window. Restoring the host with SetActiveWindow only after the settings thread exits does not guarantee foreground ownership. Destruction can already have activated another application.

## Fix

The settings dialog requests a closing handoff through the existing posted host-thread bridge, before EndDialog, for OK, Cancel and WM_CLOSE (including the title-bar Close command). Only the host thread enables the original root and restores focus. The settings thread remains independent; no cross-thread owner relationship or input-queue attachment is introduced.

The root is activated with SetForegroundWindow only when the closing dialog is still the foreground window. The foreground is checked again after EnableWindow, since host callbacks can change it. Keyboard focus is restored only if activation succeeds and the host remains foreground. If the user switched elsewhere before or during the handoff, no later cleanup tries to activate the host.

The original focused child is identified with a lifetime property. If it has been destroyed, hidden, disabled or moved out of the original root, focus falls back to the root. The existing root lifetime marker prevents modifying a destroyed/reused host HWND. Failure and quit cleanup only restore the enabled state and remove matching properties; they do not activate a window.

## Validation

Real native settings dialogs and host windows cover OK, Cancel and the native SC_CLOSE command, both with normal foreground ownership and after switching away, including host destruction. Additional cases cover destroyed focused children, foreground changes during re-enabling or activation, rejected activation, hidden hosts and replaced root lifetime markers. Apply remains on the host thread, custom values are retained, Cancel/Close do not apply drafts, and repeated runs check handle cleanup.

Foreground-query/activation/focus APIs are injected in these automated cases so the tests do not steal focus from the user's desktop. An attempted native test on a separate inactive desktop was blocked at the initial SetForegroundWindow call; it was removed rather than reported as successful native foreground validation. Actual desktop/foobar2000 foreground behavior still needs user validation.

Existing blocked-settings-thread tests continue to check both FFT sampling and WM_PAINT completion, for GDI/Direct2D with scanline synchronization enabled/disabled. ARM64EC can be cross-built here; ARM runtime testing is unavailable.

Final validation (`build/focus-fix-all.log`): x86 and x64 each passed 951 real-window lifecycle checks, 2,036 adapter checks, 696 worker/DLL-unload checks and three runs of 20,782 core checks. Synthetic previews match byte for byte. ARM64EC plugin and test targets compile but were not run. The unified local focus-fix package passed entry and SHA-256 verification. Final review found no further actionable issue; changes remain uncommitted and unpublished for user testing.
