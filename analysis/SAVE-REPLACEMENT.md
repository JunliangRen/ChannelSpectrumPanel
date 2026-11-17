# Intermittent preset-save replacement failure

## Reproduction

The earlier failure at the completed-result reservation assertion lacked the underlying I/O error. Precise stage and Win32 error diagnostics were added, then the existing real-file conflict/reopen scenario was repeated with `core_tests.exe --save-stress`.

The baseline reproduced a failure at a later same-path retry: `Could not replace preset; original file is unchanged. (Win32 error 5)`. Evidence: `build/save-investigation-baseline.log`. This identifies MoveFileExW's final replacement as the failing stage, not the in-process reservation check, write, flush, or close. The OS rejected the rename/replacement with access denied. The specific external process/filesystem component responsible is not established, and the original undiagnosed run cannot be proven to have had the identical error.

After the fix, the first 500-round real-file run passed 9,500 checks and observed one natural access-denied retry that recovered; no sharing/lock retries were observed in that run. Evidence: `build/save-fix-stress-x86.log`. These counters are test-only and that stress mode does not run injected replacement errors.

## Change and ownership review

Replacement now retries only ERROR_ACCESS_DENIED (5), ERROR_SHARING_VIOLATION (32), and ERROR_LOCK_VIOLATION (33), for at most 16 attempts and fifteen 20 ms sleeps. This requests at most 300 ms of sleep; scheduler delays and the duration of OS filesystem calls are not bounded by that value. Other failures stop immediately. A persistent read-only destination or lock still fails, reports the exact stage/error, and follows existing temporary-file cleanup. The destination is never deleted to force replacement, and successful completion is never published for a failed attempt.

The production export path runs on its existing worker. The path lease remains held for the whole operation, including retries; its registry mutex is not held during filesystem calls or sleeps. Other target paths remain independent. File close precedes replacement. The path lease is reset before the release-store publishing completion, and readers acquire-load the status before reading error data. No race or missing release in that sequence was identified. The production background-save ownership code was not changed.

Short writes now produce ERROR_WRITE_FAULT rather than reporting an unrelated stale last-error value. Diagnostics preserve the OS error before catch cleanup can overwrite it.

## Regression tests

- Injected transient access/sharing/lock failures recover with exact attempt/delay counts.
- Persistent failures stop at the retry limit with their final error intact.
- A non-transient disk-full failure is not retried.
- A real open-file sharing blocker is released on another thread, allowing replacement to recover.
- A real read-only destination retains its original contents, reports error 5 and leaves no temporary file.
- Existing write/flush/close failure, same-path conflict, immediate retry, result retention, orphaned job, different-path parallelism, submission failure and allocation failure tests remain active.
- `scripts/build.ps1 -SaveStress` additionally runs 500 real-file save-conflict iterations per executable architecture.

ARM hardware and actual foobar2000 playback are not tested by this work.

## Final verification

`build/diagnostic5-final-build.log`: x86 and x64 each passed 500 real-file conflict iterations (9,500 checks), 696 worker-overlap/actual-DLL-unload checks, 2,036 adapter checks, 469 real-window checks, and three runs of 20,776 core checks. The extended stress counters recorded 3 natural access-denied retries on x86 and 7 on x64, all recovered. No sharing/lock retries occurred in these stress runs. ARM64EC plugin and test binaries compiled, runtime tests skipped. Synthetic previews matched across x86/x64 and unified package entries/hashes passed.

Final review found no further actionable issue in the replacement retry, error capture, path-lease lifetime or completion publication changes. Production save operations remain on the background worker; no retry sleeps were added to the settings UI thread. Existing unrelated driver-lifetime changes remain covered by the unload tests.

Unified diagnostic-5 package SHA-256: `c207005d2d5934dbc13e9d08e237fd38370d455cbf3a7472d892c1ef42d24313`.
