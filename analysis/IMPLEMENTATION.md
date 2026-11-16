# Initial x64 implementation and validation

Date: 2026-11-16. Historical record for version 0.1.0, with a reference to the 0.2.0 follow-up. For the current implementation and remaining work, see [PARITY.md](PARITY.md). The 0.2.0 style and configuration changes are documented in [SIMPLE-LINE.md](SIMPLE-LINE.md).

The target is 64-bit foobar2000 2.x with Columns UI. The reference component is ssenna's Channel Spectrum panel; see the [original release page](https://foo2k.chottu.net/) and the [project attribution](../README.md#original-component-and-credit).

## Structure at version 0.1.0

- `src/panel.cpp`: Columns UI factory, independent GUID, native window, visualization stream, configuration serialization, and dialog.
- `src/spectrum.h`: frequency grouping, amplitude curves, mirrored coordinates, and fixed-point peak state.
- `src/render.h`: gray gradient bars, one-pixel-high bright peak markers, and a black DIB background.
- `tests/core_tests.cpp`: offline core assertions and synthetic BMP output.

## Reconstructed behavior

The default FFT size is 2048, producing 1024 frequency bins. The implementation calls the host's `get_spectrum_absolute`; stream flags are 0 for the old mode and 1 for the new mode. SDK `audio_sample` values are converted to float32 before further calculations to approximate the original input precision. The DC bin is skipped.

Let A be the maximum amplitude among the bins covered by a bar, M the number of bins, R the range, and K=0.2309*log2(M+1):

- Old mode: q=(100/R)*A/K.
- Old mode with Log amplitude: q=(100/R)*ln(1+2A)/ln(1+2K).
- New mode: q=(100/R)*max(0,1+(2/7)*log10(max(A,2^-17))).
- New mode with Log amplitude: replace the slope above with 1/4.

Bar height is truncated from height*q and clipped to the panel height. Nonpositive, infinite, and NaN values are ignored. Each side takes its own maximum; channels are neither averaged nor mixed.

For center x=floor(W/2), bar index j ordered from low to high frequency, and pitch=width+spacing, the initial implementation placed the left bar at x-j*pitch-width and the right bar at x+j*pitch. Odd widths may leave an outer background pixel. Frequency groups used the Q(k) and anchor formulas from the static analysis, followed by fixed-spacing quantization and clipping. This was a major approximation in 0.1.0, not a claim of exact agreement at every resolution. Version 0.4.0 replaced this grouping with reconstructed descriptors.

Peak y coordinates use 1/256-pixel fixed-point units. A bar reaching or exceeding its peak, including equality, resets peak position, velocity, and hold time. After the hold expires, each update performs position+=velocity followed by velocity+=acceleration. The coefficient is trunc(128*fall*interval/(500-interval)); defaults produce 36, or 36/256 pixel on the first falling update. Positions saturate off-screen. Unsigned GetTickCount subtraction handles timer wraparound.

The stream is released while hidden; animation freezes on pause and clears on stop. Seeking or a large time discontinuity resets state. Size and channel-layout changes clear old peaks. In 0.1.0, only OK applied configuration and Cancel discarded it; persistence used version 1, totaling 44 bytes. Applying FFT/amplitude changes reset both stream and animation.

## Validation at version 0.1.0

The Release x64 DLL and offline test executable built successfully. Checks covered reference points for all four amplitude curves, silence/nonfinite input, peak hold boundaries, ten-update cumulative fall, equal-height resets, clock wraparound, channel isolation, centered bass and outer treble, group maxima, geometry across panel widths, equal channels, truncated input, and layout-change peak resets. Assertion totals include repeated geometry checks, not independent scenarios.

`build/synthetic-preview.bmp` uses the same Engine and renderer, with synthetic spectra and the new FFT amplitude mode. It checks geometry and drawing; it does not establish that real music or the default old FFT host output matches the original.

Host loading remained untested. A separate `build/portable-test` player was prepared, but Computer Use startup authorization timed out. No DLL was installed into the existing user's component directory and no existing player settings were changed. Outstanding tests included component recognition, adding/removing CUI panels, playback/pause/stop/seeking, settings persistence across restart, multiple instances, shutdown, actual amplitude, and original animation comparisons.

## Dependencies and provenance

- [foobar2000 SDK 2026-10-01](https://www.foobar2000.org/downloads/SDK-2026-10-01.7z).
- SDK archive SHA256: `D4C55077336FAE81BF8DF0259B5B2748FA45EA84132C656EAD93EB123CBCDC26`.
- [Columns UI SDK](https://github.com/reupen/columns_ui-sdk), pinned commit `e33a58aece664dc2f02342289f25211af5e7576c`.
- See [third-party notices](../THIRD-PARTY-NOTICES.txt).

The original source and symbol names were not recovered. This is an independent C++ implementation derived from static evidence. Version 0.1.0 did not implement all original layouts, DirectDraw scanline synchronization, or original preset import; consult the current parity checklist for later additions.
