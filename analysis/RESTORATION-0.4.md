# Version 0.4.0 reconstruction record

2026-11-16. An independent x64 implementation, not recovered original source. See the [original component attribution and release links](../README.md#original-component-and-credit).

## Instruction-level frequency coordinate comparisons

`scripts/original_oracle.py` executes only the original coordinate routine at 0x1000B370 in Unicorn 2.1.4, stopping at 0x1000C3A5. It does not initialize the DLL or load it for native execution. Controlled stubs implement containers, zeroing, CopyRect, and the frequency-threshold helper. Original DLL SHA256: `e5bc0405fd9da2a59d9889fffc6fc21f8e9da71dc972d68c5e5218794a038c4b`.

Each bin has four DWORDs: left / draw / right / baseline. When a new boundary appears, the original marks and rewrites the previous descriptor instead of drawing uniform frequency buckets. The reconstruction accumulates maxima across overlapping bins, then updates the peak for the emitted drawing position.

Logarithmic mapping preserves x87 intermediates and the rounding order of selected double caches. `src/mapping_x87.asm` is independently written Win64 MASM arithmetic; it saves and restores the x87 control word. The controlled emulation profile uses CW=0x027F, the original CRT's SSE truncation branch, and x87 pow. The original live player's control word has not been measured; this profile is not claimed to represent every host.

Thirteen fixtures cover linear/logarithmic axes, wide/narrow windows, 256/1024 bins, two/three channels, pitch, spacing, direction, and reversed channel positions. All 17,637 valid descriptors with bin>=1 match field for field. The original anomalous bin 0 boundary is not reproduced, and drawing starts at bin 1. This is not exhaustive parameter coverage. Compact fixtures are in `tests/fixtures/original-coordinates.txt`; regenerable raw output is `analysis/evidence/original-coordinate-oracle.json`, excluded from Git.

Version 0.4.0 also made forced Right/Left override Reverse frequency and added the single-display Mirror flip from 0x1000384B-0x10003859. **Documentation review correction:** the coordinate builder ignores Reverse for forced directions, but its caller swaps direction 1/2 at 0x100035FC-0x1000361C when Reverse is enabled. The current implementation does not reproduce that caller step. The descriptor comparison remains valid for its supplied inputs; it does not prove end-to-end direction parity. No implementation change was made during this documentation revision.

## Ticks and channels

Amplitude ticks follow 0x1000C13C-0x1000C39F: 0 through -70 dB in 10 dB steps, alternating long and short. Old FFT mode first computes 0.2309*log2(1+bins*10^(-index/2)), then applies amplitude mapping. New FFT mode uses (100/range)*(1-index/7), replacing the denominator with 8 in log mode. Ticks are drawn on both edges of the entire padded graph, not repeated per channel; length 100 spans the width. Fixture comparisons allow a one-pixel y tolerance, not exact pixel parity.

Frequency ticks use descriptor centers at 2 kHz intervals. Safe bin 0 handling and the original descriptor state during construction have not been compared individually.

Speaker mapping follows 0x10001580: check the requested speaker bit, then count lower set bits to obtain the compact host input index. Selection and ordering use speaker positions rather than consecutive input numbers. UI positions 1-18 are FL, FR, FC, LFE, BL, BR, FLC, FRC, BC, SL, SR, TC, TFL, TFC, TFR, TBL, TBC, and TBR.

## GDI rendering

Native memory DCs, 32-bit DIBs, GradientFill, and Polyline replace approximate color interpolation and a custom line rasterizer. A 24-color cache interpolates foreground2 to foreground using index/23. TRIVERTEX color components are shifted left by eight bits. Background and Vertical eight-color gradients use seven integer-bounded segments. The foreground palette applies only to Vertical, not Simple, Horizontal, or either AA style.

Both AA modes fade from background to foreground over a bar-top region whose height is the configured blur percentage. With blur=0, no black pixel is inserted. Simple-line uses a one-pixel GDI pen and connects neighboring physical partitions. The original inserts boundary points into each channel's array, while this implementation uses separate connecting polylines. Direction-dependent endpoint inclusion, all rotations, and all palette combinations still need pixel comparisons with the original.

The previously recovered amplitude formulas and fixed-point peak updates remain in use. Peaks in Simple-line are an explicitly requested extension; the original disables them.

## Preset compatibility

The original reader at 0x1001D7B0 retains later settings when it reaches EOF on a DWORD boundary. Import now accepts magic plus zero through 74 DWORDs, filling missing tail fields from the caller's current Settings. Full 300-byte presets remain readable/writable. Missing magic, non-DWORD alignment, oversized input, and unknown enums are rejected. Runtime export/reimport by the original program is untested. Dialog import uses the last applied settings as the baseline for absent fields.

## Validation limits

Release x64 plugin and test builds passed, with 7,696 assertions. This includes per-bar checks, not 7,696 independent scenarios. The tests include 13 descriptor fixtures and generate three offline style previews. The Simple-line synthetic image was inspected; it is not a screenshot comparison with original playback.

Computer Use selection in the portable player's initial UI dialog returned `coordinate input geometry is unavailable`; refreshing the window and retrying activation returned `failed to activate captured window`. Further input stopped after recovery failed. Columns UI instantiation, settings persistence, real playback, transparency, and scanline synchronization were not validated in the host.

Further validation must compare x86/x64 output for the same audio and FFT, boundary pixels in four orientations, actual original presets, pause/seek/stop, multiple panels, DPI/multiple monitors, transparency, and scanline behavior. Transparency and synchronization remain compatibility implementations rather than verified copies of the original paths.
