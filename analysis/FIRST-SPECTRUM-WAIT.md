# Early visualisation registration and diagnostic 6

Current status: early registration remains enabled. Diagnostic-6 logging and availability probes described below were removed after user validation; this document retains their historical interpretation and test evidence.

## Behavior

A visible panel registers its visualisation stream before the playback-active check and before its first animation timer tick. This includes visible stopped/paused panels. Registration does not request samples, run the FFT, or advance animation. Host-hidden panels and panels with hidden ancestors do not prewarm; hiding releases the stream, restoring registers a fresh one, and destruction prevents later events from registering again. A visible stopped panel retains its prewarmed stream even when an old fallback timer message arrives. Changing FFT normalization recreates the stream with the requested flag.

Creation uses a temporary candidate, checks visibility/window identity again before accepting it, and contains creation failures. A later visibility/playback event or active tick may retry. Each instance owns its own stream. The normal rendering request remains get_spectrum_absolute at the current playback time and the configured FFT size. No fallback FFT, time offset or fake spectrum is used for rendering. Amplitude processing, settings format and animation math are unchanged.

## Diagnostic events

The package reports `1.3-startup-diagnostic6`. Existing startup logs remain in `%TEMP%/ChannelSpectrumPanel-startup-<PID>.log`.

When the normal FFT request is unavailable and the playback clock is valid, diagnostic-only probes run at most once per 100 ms, for at most eight rounds per wait cycle:

- `wait.request_time_us`: requested playback timestamp in microseconds, not wall-clock time.
- `wait.pcm_10ms`: availability of 10 ms PCM at the requested timestamp.
- `wait.fft256`: availability of a 256-point spectrum at the same timestamp.
- `wait.fft_minus50ms` / `wait.fft_plus50ms`: availability of the configured FFT at adjacent timestamps.
- `wait.probe_sample_rate`: sample rate reported by a successful probe.
- `wait.probe_exception`: a probe threw; the exception is isolated from rendering.

Probe results use separate scratch chunks and are discarded. No PCM or spectrum sample values are logged. Each probe records its duration, allowing diagnostic overhead to be distinguished from availability waiting. Probes stop after the first successful normal request. They may affect backend caches or add a little work; measured durations should be considered when comparing runs.

`first_spectrum.sample_rate` and `first_spectrum.channels` describe the normal successful frame. Diagnostics reset on new streams, playback start/new track, stop and seek. A bounded 8,192-record UI-thread buffer accommodates repeated hide/reopen/track cycles; no background thread writes to it. Failed normal reads are sampled after the first 20 attempts as before.

A useful comparison uses playback.starting as the common zero point, rather than comparing only create_stream-to-first_spectrum intervals. Early registration moves the interval start and can otherwise make readiness appear slower without delaying the first image.

## Tests and review

The real SpectrumWindow harness covers stopped/paused prewarming without FFT reads, hidden ancestors, host hide/restore, duplicate visibility notifications, normalization changes, creation failure/retry, diagnostic success that never reaches the renderer, probe exceptions and limits, stopping probes after normal success, unavailable clocks, seek reset, stale timers after stop, and destruction during data waiting. Existing multi-instance, Columns UI reopen, settings-open destruction and GDI/USER/process-handle checks remain active.

The first test run exhausted the old 2,048-record diagnostic budget, preventing later event assertions; the buffer remains bounded but was enlarged. Review also found that a stale stopped-state fallback timer could discard a newly prewarmed stream; that path now retains the visible stream and has a regression assertion.

Actual foobar2000 startup benefit and ARM hardware behavior require user testing. This work does not assert a particular first-spectrum latency reduction.

## Final validation

`build/diagnostic6-final-build.log`: x86/x64 each passed 492 real-window lifecycle checks, 696 worker-overlap and actual-DLL-unload checks, 2,036 adapter checks and three runs of 20,776 core checks. Cross-architecture synthetic previews matched byte for byte. ARM64EC plugin and test executables compiled; runtime tests were not run. Unified package contents and DLL hashes passed. Final review found no further actionable issue in stream ownership, diagnostic isolation, reset/visibility behavior or animation preservation.

Package SHA-256: `5b6a379218218e60809f93adc4bc1e55b49144082965abc082ffb6f59ca752ad`.
