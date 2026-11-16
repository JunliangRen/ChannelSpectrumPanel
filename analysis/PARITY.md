# Original 0.17.2 versus x64 0.4.1 parity checklist

Date: 2026-11-16. The goal is comprehensive reconstruction, but **it is not complete**. Matching option names and successful synthetic tests do not establish original behavior. For the original component, author credit, and release links, see the [README](../README.md#original-component-and-credit).

## Implemented features and evidence limits

| Original feature | Status in 0.4.1 | Differences / verification limits |
| --- | --- | --- |
| Simple / Horizontal / Vertical | Solid, amplitude-colored, and full-height gradient rendering | Native GDI GradientFill; all combinations still need original pixel comparisons |
| Both antialiasing styles | Bar-top gradient transitions | Native GDI; zero blur means no transition; old approx. labels removed, but not all combinations verified |
| Blur level | Height percentage, 0-100 | 0x10003E3C-0x10003E79: trunc(abs(height)*level/100); zero is handled safely |
| Simple-line | One-pixel channel contours | GDI Polyline and cross-partition connections; boundary point assembly still needs comparison; peaks are an extension |
| Mirror / Right / Left | Implemented | 0.4.1 reproduces the caller swap at 0x100035FC-0x1000361C; 96 Engine-level combinations check low/high-frequency placement, channel order, and single-channel behavior |
| Reverse channel-pos | Implemented | Moves physical partitions while preserving logical direction; not merely an audio swap |
| Rotate | Four orientations | Generic pixel rotation rather than the original four independent drawing paths |
| Separate / Merge all / Specified | Implemented | Merge takes maxima; speaker bits map through the actual channel mask to compact input indices |
| Ordering and active channels | 18-entry order and mask | Used with All input channels; the earlier forced front-L/R default remains |
| Overall and channel padding | Implemented | Pitch-quantized partitions with centered remainder; degenerate sizes clear safely |
| Bar width, spacing, Bar mode | Implemented | Segmented bars align y to even pixels, skip alternate rows, and add 1 to pitch |
| Five base colors | Implemented | UI uses RRGGBB; original COLORREF conversion swaps red/blue bytes |
| Eight-color foreground/background | Implemented | Native GDI seven-segment gradients; foreground palette applies only to Vertical; full combinations lack runtime comparison |
| Frequency ticks | Every 2 kHz; every fifth tick is long | 0x100211A0: max(0,trunc(2000*n*M/(Fs/2))-1); descriptor centers used; bin 0 and intermediate construction state need checking |
| Amplitude ticks | Eight 10 dB levels with alternating lengths | Old/new FFT and log branches restored; original-routine fixture y tolerance is 1 px |
| Tick length 100 | Full-length line | From the coordinate builder's cmp length,0x64 branch |
| Show Peaks / fall / hold | Implemented | Fixed-point updates retained; original Simple-line disables peaks, this version permits them |
| FFT / amplitude normalization | Implemented | Actual host input normalization in old/new stream modes needs audio comparison |
| Edge style | None / Sunken / Static | Native extended window styles; host layout testing incomplete |
| Transparent background | Parent-background implementation | DrawThemeParentBackground, not a direct reproduction of original region restoration/compositing |
| Fast pseudo-transparent | Background cache | Invalidated on size, position, theme, or settings changes; disable caching for continuously changing parent backgrounds |
| Sync with scanline | Experimental | DDraw7 GetScanLine plus a 2 ms multimedia timer posting to the UI; 100 px top margin, timeout/secondary-monitor fallback; scheduling differs |
| Apply / Reset / Cancel | Implemented | Apply is immediate; Cancel retains applied changes and discards later edits; UI testing incomplete |
| New preset import/export | `.csm` implemented | CSMX header and versioned little-endian fields |
| Original preset import/export | Full 300-byte format and DWORD-aligned prefixes | No real original-export fixture; missing tail fields are retained; additional styles cannot export to this format |

## Original `.csp` structure

- 0x1001E71A checks magic 0x07172737; 0x1001E876 writes the same magic.
- 0x1001C3D0-0x1001C856 writes 74 DWORDs in sequence, totaling 4+74*4=300 bytes. See `evidence/csp-field-order.tsv`.
- Special fields include merged transparency flags cfg+0x50 | (cfg+0x54)<<1, the actual FFT size 2^(index+9), 18 order entries, and the final FFT mode.
- Configuration-object offsets are not file offsets. Conversion uses an explicit table rather than memcpy of the old C++ object.
- Hold is stored in milliseconds. The old UI uses 10 ms units; import must not multiply the stored value by 10 again.
- COLORREF/RGB conversion is explicit. Synthetic round trips establish agreement with the field table, not acceptance by the original reader or correctness of every field's semantics.

## Reconstruction configuration

Version 3 contains 77 uint32 values (308 bytes). It preserves the first 12 fields and adds layout, color, palette, scanline, and active-mask fields. Versions 1/2 remain readable. Decoding validates size/version and decodes into temporary Settings before replacing current values; truncated or unknown versions are not partially applied.

## Validation

- Release x64 build and 8,844 assertions passed.
- Coverage includes version 1/2 migration, complete version 3 round trips, every truncation point, original `.csp` magic/length/fields/colors/unknown styles, three-channel max/selection/order, empty masks, padding, eight styles across three directions/reversals/sizes, inverse rotation, preserved backgrounds, disabled peaks, blur percentage, and tick counts.
- DirectDraw, parent-background caching, and dialog interaction lack automated host tests. Compilation does not establish runtime behavior.
- `build/portable-test/foobar2000.exe` reached the New user interface module(s) found dialog. Window activation failed when selecting Columns UI, and recovery failed again. Panel instantiation and playback were not completed.
- No player process was forcibly terminated and the existing user's player configuration was not changed. Any remaining portable test window may be closed manually.

## Remaining work

1. Four-DWORD descriptors are reconstructed; 17,637 valid rows across 13 fixtures match. Untested floating-point profiles, bin 0, other parameters, and remaining caller behavior still need comparison. The forced-direction caller transformation is now covered by Engine-level regression tests.
2. Compare GDI caches, all gradient/background combinations, four rotation paths, and Simple-line connections.
3. Verify amplitude ticks and speaker mapping with real host audio and layouts.
4. Validate real original `.csp` export/reimport, including legacy prefixes.
5. Test loading, save/restart persistence, multiple panels, pause/seek/stop, long runs, DPI, and transparency/scanline behavior across graphics devices and monitors.

These remain outstanding; the release does not claim complete original parity. See the [0.4.0 reconstruction record](RESTORATION-0.4.md) for emulation conditions and evidence.

The direction fix and current host-test status are recorded in [DIRECTION-0.4.1.md](DIRECTION-0.4.1.md).
