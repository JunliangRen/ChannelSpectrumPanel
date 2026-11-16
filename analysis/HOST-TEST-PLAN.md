# Host test plan for version 0.5.0

Status: **prepared, not run in foobar2000**. Audio generation and encoded-PCM inspection do not establish plugin loading, FFT normalization, rendering parity, or animation timing. Do not resume desktop automation until the user is available and permits it.

## Generate the test material

Python 3.9 or later, standard library only:

```powershell
python scripts/generate-test-audio.py
```

The default output is `build/host-test-audio/`, excluded from Git. An optional `--output` argument selects another directory. The generator replaces its named output files on rerun; use a dedicated directory. It does not launch a player, change settings, or play audio.

Load `playlist.m3u8` into the portable player when ready. It references five WAV files by relative name; keep the folder together. `TIMELINE.md` lists each segment's exact start/end time. `manifest.json` records sample ranges, signal definitions, measured levels/frequencies, and hashes. `SHA256SUMS.txt` provides file checksums.

| File | Duration | Purpose |
| --- | --- | --- |
| `01-channel-direction.wav` | 33 s | Left/right isolation; 375 Hz and 6000 Hz; identical stereo and unequal-frequency channels |
| `02-frequency-sweep.wav` | 32 s | Separate left/right, then identical stereo logarithmic sweeps from 30 Hz to 18 kHz |
| `03-amplitude-steps.wav` | 26 s | Identical stereo 375 Hz at -48, -36, -24, and -18 dBFS sample peak, separated by silence |
| `04-peak-decay.wav` | 20 s | One-second identical stereo 375 Hz bursts at -36, -24, and -18 dBFS, each followed by five seconds of silence |
| `05-mono-layout.wav` | 14 s | True one-channel PCM: 375 Hz, silence, and 6000 Hz; not a stereo file with one silent channel |

All files are 48 kHz, 24-bit signed little-endian integer PCM. Active signals have 10 ms linear entrance/exit fades, exact-zero endpoints, and exact digital silence in inactive channels. Ordinary tones/sweeps are -24 dBFS sample peak; -18 dBFS is the maximum in this set. Start with low listening volume; the files include sustained high-frequency tones. Listening is optional for visual checks.

Levels describe encoded sample peaks, not RMS or predicted panel heights. A steady sine's RMS is approximately 3.0103 dB below its peak. At FFT=2048 and 48 kHz, 375 Hz is bin 16 and 6000 Hz is bin 256. Windowing may spread energy into neighboring bins; do not require exactly one visible bar in the host. Sweep speed is logarithmic in time, not a prediction of constant visual speed.

## Establish the baseline

Run this plan separately in isolated x86 and x64 portable test players with matching Columns UI and component packages, not the normal player profile. Record the exact player, Columns UI, and component versions. Install the local 0.5.0 package through Preferences > Components and restart when requested. Verify the component version in Preferences before testing; an old loaded DLL invalidates the comparison.

Add Visualisations > Channel Spectrum Mirror. Use this baseline:

| Control | Value |
| --- | --- |
| FFT size | 2048 |
| New FFT normalization | Enabled; repeat amplitude comparisons with it disabled later |
| Logarithmic amplitude | Disabled |
| Refresh ms / Peak fall / Peak hold ms | 14 / 10 / 0 |
| Range | 100 |
| Drawing style | Simple initially; repeat direction checks with Simple-line |
| Direction / Rotation | Mirror / 0 |
| Channel mode | Separate |
| Separate: use all input channels | Enabled, particularly for the mono test |
| Reverse frequency / Reverse channel positions | Both disabled initially |
| Linear frequency | Disabled initially; repeat with enabled |
| Bar width / Spacing | 1 / 0 |
| Left / Right / Channel padding | 0 / 0 / 0 |
| Show peaks | Enabled |
| Segmented bars / Palettes / Ticks | Disabled initially |
| Parent background / Cache / Scanline sync | Disabled |
| Foreground 1 / Foreground 2 / Background / Peak | E4E4E4 / 202020 / 000000 / F5F5F5 |
| Channel order / Active speaker mask | Default 1-18 / 3FFFF |

Keep ReplayGain, DSP, crossfading, and channel remapping disabled in the test profile for repeatability. Record output device/sample-rate conversion settings and playback volume; use the same settings for comparisons. Do not assume player volume affects the visualization stream in the same way as the audible output.

Measure client-area pixel width/height, not merely the outer window. Use the same dimensions, Windows scaling, and rotation when comparing renderers. If a setting is clipped or inaccessible, record it as a UI issue instead of silently substituting a different baseline.

## A. Loading and normal operation

1. Confirm the component is recognized as 0.5.0 and a panel can be added without an error.
2. Play `01-channel-direction.wav`; confirm spectrum activity during tones and no sustained bars in silent channels. Peaks may remain briefly after a tone.
3. Pause during a tone, resume, then stop. The current implementation is intended to freeze on pause and clear on stop; compare original behavior separately.
4. Seek forward and backward between known segments. Confirm updating resumes and old peak history does not remain indefinitely.
5. Resize the panel repeatedly, including a narrow window. Check for clipping, stale content, errors, and crashes.
6. Add a second panel with a different direction. Check independent settings and correct updates; remove the test instance afterward.
7. Apply settings, make another unapplied edit, then Cancel. Confirm the applied state remains. Export a `.csm`, restart, verify persistence, and verify reimport into the test panel.

## B. Direction and channel identity

Use `01-channel-direction.wav`. In left-only/right-only segments, the inactive channel must remain silent apart from peak decay left over from the preceding segment. Compare the 375 Hz and 6000 Hz positions within each active channel partition.

| Direction | Reverse off: frequency increases toward | Reverse on: frequency increases toward |
| --- | --- | --- |
| Right | Right, in both partitions | Left, in both partitions |
| Left | Left, in both partitions | Right, in both partitions |
| Mirror, normal stereo channel order | Left in L; right in R (bass toward center) | Right in L; left in R (treble toward center) |

Repeat all six combinations with reversed channel positions, then with linear frequency and Simple-line. Reversing channel positions must move each logical channel to the other partition without changing that channel's direction. Therefore Mirror with reversed channel positions also swaps the center's bass/treble relationship; it is not merely a relabeling operation.

For `05-mono-layout.wav`, with all input channels enabled, expect one partition. Mirror defaults to bass on the left; Reverse moves bass to the right. Forced Right/Left follow the same table as above. The separate forced-stereo default extension is not the baseline for comparison with the original mono mode.

## C. Frequency placement

Play `02-frequency-sweep.wav` with peaks temporarily disabled to distinguish current bars from trailing markers. The active group should progress from bass to treble in the direction selected above. Record unexpected discontinuities or channel leakage. Quantized groups and nonuniform motion are expected; the adaptive frequency axis is not a simple logarithmic ruler.

Compare linear and nonlinear axes, then enable frequency ticks. Tick intervals are 2 kHz with a long tick every fifth interval. Check the lowest-frequency edge separately; bin 0 remains an acknowledged comparison gap.

## D. Amplitude comparison against the original

Use a separate x86 foobar2000/Columns UI portable setup with the original 0.17.2 component, obtained from the [original author's page](https://foo2k.chottu.net/). Do not install the x86 DLL into the x64 player. Do not replace the normal player profile. Use the same audio, FFT size, mode, dimensions, range, and relevant settings in both players.

Play `03-amplitude-steps.wav` and capture the middle of each four-second plateau, avoiding its fades and FFT startup. For each mode combination (old/new FFT, logarithmic amplitude off/on), record the maximum bar height and client height, plus their ratio. Higher input levels should not produce lower steady-state maxima with unchanged settings.

Do not equate WAV dBFS directly with internal spectrum A. Host windowing and normalization may differ. If heights disagree, first establish equivalent host FFT input before changing the recovered amplitude formula. The generator verifies PCM levels, not the host spectrum scaling.

## E. Peak hold and falling motion

Use `04-peak-decay.wav`, initially Simple with peaks enabled. Record the end of each burst and its following five seconds of silence. Repeat with hold=500 ms (original Hold time[10ms]=50), then restore 0. Keep fall=10 and interval=14 ms.

The first 10 ms of the tone's fade and the host's FFT window/history affect when the visible bar disappears. Treat the last observed bar/peak contact as the timing reference, not the nominal PCM boundary alone. Hold begins at the last contact, including equal-height contact.

The offline reference coefficient is a=trunc(128*10*14/(500-14))=36 in 1/256-pixel units. After n actual falling updates, predicted displacement is 36*n*(n+1)/512 pixels. Ten updates give about 7.734 px; twenty give about 29.531 px. These are update counts, not a wall-clock frame-rate promise. Record capture frame rate and actual elapsed time when comparing real animation.

Test hold behavior and visual fall trajectory against the original without compensating for scheduling differences by changing amplitude. The original disables peaks for Simple-line, so use Simple to compare peak animation; separately check that this reconstruction's optional Simple-line peaks work.

## Record results

Use one row per test/configuration:

| Test / file time | Player/component versions | Client size / DPI | Settings changed from baseline | Observed result / capture | Pass / fail / not run |
| --- | --- | --- | --- | --- | --- |
| Loading and playback | Pending | Pending | Baseline | Not run | Not run |
| Six direction combinations | Pending | Pending | Direction + Reverse | Not run | Not run |
| Reversed channels / linear / Simple-line | Pending | Pending | As specified | Not run | Not run |
| Mono | Pending | Pending | All input channels enabled | Not run | Not run |
| Amplitude plateaus | Pending | Pending | Four FFT/amplitude combinations | Not run | Not run |
| Peak decay | Pending | Pending | Hold 0 and 500 ms | Not run | Not run |
| Pause / stop / seek / resize | Pending | Pending | Baseline | Not run | Not run |
| Persistence / multiple panels | Pending | Pending | Record actual values | Not run | Not run |

Keep captures, populated results, and original presets under the ignored `build/host-test-audio/` directory. Summarize confirmed observations in the analysis documentation afterward. A successful generated-file check must never be reported as a passed host test.
