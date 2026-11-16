# Channel Spectrum panel 0.17.2: detailed static analysis

Date: 2026-11-16. Scope: the original x86 DLL's horizontal-frequency, upward-growing bar path, emphasizing stereo mirroring, amplitude, and peaks. This is a historical reverse-engineering record, not the author's source or an implementation report. Later reconstruction and emulation results are in [RESTORATION-0.4.md](RESTORATION-0.4.md).

The reference is ssenna's original Channel Spectrum panel. See the [author's release page](https://foo2k.chottu.net/) and [project attribution](../README.md#original-component-and-credit).

## 1. Sample, method, and evidence boundaries

Sample: `foo_uie_vis_channel_spectrum.dll`, provided locally and not distributed in this repository.

- Version string: 0.17.2; size: 241664 bytes; PE Machine: 0x14c; ImageBase: 0x10000000.
- SHA256: `e5bc0405fd9da2a59d9889fffc6fc21f8e9da71dc972d68c5e5218794a038c4b`.
- Methods: PE metadata/dialog parsing, x86 disassembly, argument tracing, x87 stack algebra, and configuration read/write cross-references.
- During this analysis phase, the DLL was not loaded or executed, foobar2000 settings were not opened or modified, and no porting code was written.
- Addresses are VAs relative to this sample's preferred base; RVA=VA-0x10000000. They are not live process addresses.
- Instruction facts, algebraic reconstruction, and runtime verification are separate evidence levels. These formulas were statically recovered without frame-by-frame or pixel-by-pixel comparison.

The analysis connects Mirror/Reverse controls, channel partitions, linear and adaptive logarithmic frequency mapping, four amplitude transforms, and peak parameters. It provides a behavioral specification for ordinary dimensions, two channels, and Rotate=0. Narrow windows, bin 0 special values, all rotations, and antialiasing paths require separate checks; this is not a complete reconstruction of the component.

## 2. Main functions and data flow

| Address | Statically identified purpose | Evidence |
| --- | --- | --- |
| 0x100034C0 | A horizontal-frequency, upward-drawing frame path | Reads settings/spectrum, builds mapping, draws bars/peaks |
| 0x100037E5-0x1000381B | Playback time and spectrum retrieval | Vtable +0x0C/+0x14; double time, FFT size, and chunk arguments |
| 0x100038F8-0x1000396C | Pass settings to coordinate builder | 17 stack arguments plus a rectangle pointer in ECX |
| 0x1000B370-0x1000C13A | Partitions, frequency mapping, grid quantization, descriptors | Section 3 |
| 0x100041DD-0x10004557 | Amplitude normalization and pixel height | Section 4 |
| 0x100047E5-0x10004899 | Peak update and short-line drawing | Section 5 |
| 0x10020970 onward | Apply settings, invalidate caches, refresh/peak coefficients | settings-transfer.txt |
| 0x10021520-0x10021972 | Constructor defaults | defaults.txt |
| 0x10022880 | Floating-point truncation toward zero | SSE CVTTSD2SI; fallback adjusts FISTP results |

The audio entry points closely match the signatures and method order of `visualisation_stream::get_absolute_time/get_spectrum_absolute`. The SDK describes channel-preserving spectra carried in an audio_chunk, not ordinary time-domain PCM. The interface identification is a high-confidence inference; original symbol names were not recovered. Reference: [SDK documentation mirror](https://chocobo1.github.io/fb2k_doc/SDK-2015-01-14/doxygen/d8/d4e/classvisualisation__stream.html).

At 0x10020BEE-0x10020CC4, FFT mode affects stream creation as well as rendering formulas: the code checks a host 0.9.5.2 version condition and passes the mode into visualization-stream creation. Old/new input normalization may differ. Equal numeric A values across modes do not establish equivalent real-audio output.

## 3. Mirroring and frequency coordinates

### 3.1 Settings and object fields

`cfg` denotes the configuration object and `obj` the runtime panel object. These are not absolute file-format offsets.

| UI setting | Control ID / dialog | cfg offset | obj offset | Meaning |
| --- | --- | --- | --- | --- |
| Channel Mode | 0x402 / 108 | +0x5C | +0xD04 | 0 Separate; 1 Merge all; 2 Specified below |
| Reverse freq-axis | 0x43B / 108 | +0x60 | +0xD00 | Boolean |
| Specified channel | 0x403 / 108 | +0x64 | +0xD08 | Selected channel |
| Rotate | 0x404 / 108 | +0x68 | +0xD0C | This analysis emphasizes 0 |
| Reverse channel-pos | 0x43C / 108 | +0x6C | +0xD10 | Reverse physical partitions |
| Range scale | 0x43D / 108 | +0x70 | +0xD14 | 1-1000; amplitude, not frequency range |
| Spacing | 0x43F / 108 | +0x74 | +0xD18 | Bar spacing, with mode-dependent additions |
| Linear scale (frequency) | 0x444 / 108 | +0x8C | +0xD34 | 1 linear; 0 adaptive logarithmic |
| Direction | 0x44B / 116 | +0x114 | +0xDDC | 0 Mirror; 1 Right; 2 Left |
| Left / Right padding | 0x44C/0x44E / 115 | +0x118/+0x11C | +0xDE0/+0xDE4 | Overall left/right padding |
| Channel Padding | 0x450 / 116 | +0x120 | +0xDE8 | Gap between partitions |

The Direction strings are inserted as Mirror, Right, Left at 0x1001E4B3-0x1001E506. This enum order is instruction-derived, not inferred from a screenshot.

### 3.2 Channel partitions

Let W be width after left/right padding, C the displayed channel count, G the channel gap, and P the drawing pitch. For ordinary nondegenerate dimensions, 0x1000B52B-0x1000B6B2 assigns each channel:

`S = P * floor(floor((W - (C-1)*G) / C) / P)`

The remainder is `E = W - (C-1)*G - C*S`. If its absolute value is at least 2, all partitions shift by trunc(E/2), approximately centering the grid. For ordinary W/P/C, E is nonnegative.

Physical partition j starts at `L + j*(S+G) + shift` and ends S pixels later. Reverse channel-pos stores its records under logical channel C-1-j without changing audio content.

P is not simply Bar width. At 0x10003524-0x10003679, it is normally Bar width + Spacing + a mode-dependent extra 1. Simple-line is handled separately. The conversion must remain mode-dependent.

### 3.3 Conditions for centered bass

For zero-based logical channel c, Reverse freq-axis flag r, and Direction=Mirror:

- `(c+r) & 1 == 0`: 0x1000B7DD increases frequency from right to left within the partition.
- `(c+r) & 1 == 1`: 0x1000BC99 increases frequency from left to right.

Inside the coordinate builder, Direction=Right forces the second path and Direction=Left forces the first. However, the main drawing caller swaps forced values 1/2 when Reverse freq-axis is enabled, at 0x100035FC-0x1000361C. Updating Mirror parity alone misses this caller transformation. The 0.4.0 builder-only comparison does not cover it; see the current parity checklist.

For ordinary stereo with normal channel order, centered bass uses Separate, Mirror, Rotate=0, Reverse freq-axis off, and Reverse channel-pos off:

`treble <- left channel <- bass | bass -> right channel -> treble`

Each side uses its own channel spectrum; one side is not a copied image. Reversing channel positions changes partition ownership and, in Mirror mode, can change whether the center contains bass or treble. It is not just an L/R label swap.

### 3.4 Continuous coordinates and adaptive logarithmic mapping

Define FFT size N, M=N/2, D=M-1, and bin index k. The builder loops over k=0..M-1; main drawing starts at k=1, so bin 0 is not drawn as a normal bar.

Under the SDK spectrum convention, physical frequency is normally `f_k = k*sampleRate/N`. The highest bin k=M-1 is below Nyquist. Coordinate endpoint normalization uses D rather than M.

For linear frequency, distance from the low-frequency endpoint is:

`u(k) = S*k/D`.

The nonlinear mode is not a simple log(k) spread over the panel. It first constructs:

`Q(k) = 1024 * ln(1 + 1024*k/D) / ln(1025)`.

Evidence: 0x1000B6E6-0x1000B755; constants 1024 and 1025 at 0x1002C540 and 0x1002C538.

Each channel has an anchor index m, initially 0:

`T = max(m, S*m/D)`

`F_m(k) = T + (S-T) * (Q(k)-Q(m)) / (1024-Q(m))`.

For finite ordinary inputs, the builder computes both F_m(k-1) and F_m(k):

- If their separation exceeds one pixel, 0x1000B8E8 or 0x1000BD98 updates m to k and uses max(k,S*k/D), together with the corresponding previous-bin value, for the boundaries.
- Otherwise it uses anchored logarithmic interpolation.
- Linear frequency takes a separate S*k/D path and bypasses this hybrid result.

This preserves roughly at least one pixel per bin at low indices while compressing higher frequencies, where multiple bins may share a bar. The anchor is mapping-construction state, not an animation variable.

This is algebra recovered from the x87 stack. For k=0, Q(-1) is evaluated early and can produce an invalid logarithm for small FFT sizes. The original relies on comparison state and later skipping bin 0; the general real-valued formula must not be applied blindly to initialization. That boundary was not instruction-executed during this static phase. Degenerate S, C=0, and related cases are outside these simplified formulas' guarantees.

### 3.5 Mirroring, truncation, grid alignment, and aggregation

For an ordinary positive-width partition [a,b]:

- The forward path adds u to origin a; the reverse path subtracts u from origin b.
- 0x10022880 truncates toward zero; IDIV/MUL then aligns a boundary to multiples of P. Preserve the order of negation and truncation for reverse coordinates; floor is not an interchangeable operation.
- Reverse path 0x1000BA2F-0x1000BA6A quantizes the previous bin's negative coordinate by P before adding b; the current endpoint extends at least P to the left.
- Forward path 0x1000BECC-0x1000BF04 quantizes the current bin's positive coordinate by P before adding a; the previous endpoint extends at least P to the right.
- Four-DWORD descriptors are read at 0x10004449-0x10004471: x1, draw flag, x2, baseline y. The second field is not a normal RECT.top coordinate.
- Bins in one drawing group accumulate maximum amplitude until a nonzero draw flag emits the bar and clears the accumulator: 0x10004349-0x10004477 and 0x10004918-0x10004922.

Drawing a separate bar for every bin would not reproduce this behavior.

## 4. Amplitude calculation

### 4.1 Channels and bin aggregation

Spectrum values are read as float32. Separate maps input channels into custom display order and takes maxima within each drawing group. Merge all takes maxima across channels. Specified below skips unselected channels. No analyzed path replaces these maxima with an average or RMS.

A denotes the accumulated nonnegative maximum for the current drawing group. Its physical normalization depends on the host FFT mode; the DLL alone does not establish which recording level corresponds to A=1.

### 4.2 Four transforms

Let R be Range scale (1-1000), K=`0.2309 * log2(M+1)`, and q the dimensionless bar-height amplitude.

| FFT mode | Logarithm scale | Reconstructed q |
| --- | --- | --- |
| Old, 0 | Off | `(100/R) * A/K` |
| Old, 0 | On | `(100/R) * ln(1+2*A)/ln(1+2*K)` |
| New, 1 | Off | `(100/R) * max(0, 1 + (2/7)*log10(max(A,2^-17)))` |
| New, 1 | On | `(100/R) * max(0, 1 + (1/4)*log10(max(A,2^-17)))` |

Normalization is at 0x100041DD-0x10004260 and mode branching at 0x10004477-0x10004546. Constants 0.2309, 2, 2/7, 1/4, and 2^-17 were decoded from PE data as little-endian float64.

New mode already uses log10 with Logarithm scale off; the switch changes its slope. Old mode instead selects linear versus ln(1+2A). A single generic logarithmic on/off rewrite would be incorrect. Increasing R reduces height: R is a divisor, not a gain percentage.

Algebra checks at R=100: new mode A=1 gives q=1; A=0.01 gives q=3/7 (about 0.428571) with the switch off and q=0.5 with it on. Old mode A=K gives q=1 in both variants. These are formula checks, not audio tests.

### 4.3 Bar height, color, and screen coordinates

For absolute drawing-rectangle height H:

- `height = trunc(H*q)`, at 0x10004546-0x10004551.
- `colorIndex = clamp(trunc(24*q), 0, 23)`, at 0x10004551-0x10004574.
- `y = baseline - height`, at 0x10004578-0x100045A0.
- Certain Bar mode paths align y to an even pixel and clip to the drawing area's top, at 0x100045A4-0x100045B8.

q is not universally clamped to 1 here; excessive heights are clipped as coordinates. The 24-level index participates in rendering even when the resulting appearance is gray-white on black.

## 5. Peak animation and parameters

### 5.1 Parameter sources

| Parameter | UI input | cfg -> obj | Range / units |
| --- | --- | --- | --- |
| Refresh interval | 108:0x434 | +0x3C -> +0xCF4 | 5-100 ms |
| Fall speed | 117:0x445 | +0x4C -> +0xCB4 | 1-99, dimensionless input |
| Hold time[10ms] | 117:0x447 | +0xBC -> +0xCB8 | Input 0-1000, multiplied by 10 and stored as 0-10000 ms |
| Show Peaks | 108:0x42D | +0x34 -> +0xCF0 | Boolean, also suppressed by certain styles |
| Derived acceleration a | No separate control | +0xCB0 | Discrete coefficient in 1/256 pixel per update squared |

Hold multiplication is at 0x1001CEE1/0x1001CEF9; Fall speed limits at 0x1001CF45-0x1001CF63; Refresh limits at 0x1001F216-0x1001F232.

For V=Fall speed and I=Refresh interval:

`a = trunc(128 * V * I / (500-I))`.

Evidence: 0x10020FBA-0x10020FF0; constant 128.0 at 0x1002C4E0. The UI limit I<=100 avoids the singularity near 500. Validation of manually imported invalid settings was not fully audited in this phase.

Constructor defaults: I=14 ms, V=10, Hold=0 ms, FFT size=2048, Range scale=100, Direction=Mirror, Reverse freq-axis=0. These do not identify the user's saved settings. The constructor initially sets a to 1; applying settings recomputes it as 36. The placeholder 1 is not the steady-state coefficient.

### 5.2 State per channel and drawing index

Three array banks have channel stride 0x10:

- Pointer `obj+0x8C+0x10*c`: peak coordinate P.
- Pointer `obj+0x1AC+0x10*c`: falling step v.
- Pointer `obj+0x2CC+0x10*c`: last contact time t0.

Elements are 32-bit values; position retains eight fractional bits. In the analyzed upward-bar path, screen y increases downward, so smaller P means a higher peak.

Let Y be the bar top converted to that internal coordinate system and now the current GetTickCount:

1. If P>=Y: P=Y, v=a, t0=now. Equality also refreshes hold time.
2. Otherwise, if unsigned 32-bit now-t0 is less than Hold, retain position and step.
3. Otherwise, P=P+v, then v=v+a.
4. For output, arithmetic-shift P right by eight, apply the Bar mode alignment mask, and call Polyline with two endpoints sharing y.

Addresses: 0x10004814, 0x10004817, 0x1000481B-0x10004836, 0x1000483B-0x1000486A, and 0x1000487C-0x10004899.

After n actual falling updates since reset, without a new peak:

`P_n = P_0 + a*n*(n+1)/2`; pixel displacement is `a*n*(n+1)/512`.

With default a=36, ten falling updates move about 7.734 pixels; twenty move about 29.531 pixels. Update counts cannot be replaced directly with elapsed time. Hold uses a clock, while falling uses update count; rendering stalls affect real-time motion.

### 5.3 Details and reproduction limits

- The main path obtains one timestamp per frame, not one per bar.
- Hold begins at the most recent contact between bar and peak; flat consecutive tops repeatedly reset it.
- A peak is a short solid line, not a particle emitter. It appears as a bright dot when bars are narrow.
- Original instructions add the drawing area's top before storing state and apply further offsets afterward. The formulas directly apply to client top=0; nonzero local origins require checking the full coordinate chain.
- These updates cover the specified drawing path. Signs and endpoint order in other rotations were not checked instruction by instruction during this phase.

## 6. Rendering, refresh, and x64 implications

GDI pens, Polyline, GradientFill, memory bitmaps, and BitBlt calls are confirmed. The DLL also dynamically loads ddraw.dll / DirectDrawCreateEx, and resources contain Sync with scanline. Its 2 ms multimedia timer supports an additional synchronization path; this does not mean the spectrum runs at a fixed 500 FPS. Ordinary refresh uses SetTimer.

An x64 reconstruction need not reproduce every CRT or old SDK implementation. It needs current host audio and Columns UI interfaces while preserving identified behavior:

1. Independent channels, Mirror parity, and partition grids.
2. Adaptive frequency anchors, max aggregation, and exact truncation order.
3. Amplitude formulas matched to actual host input normalization, rather than assuming identically named old/new values are equivalent.
4. 1/256-pixel peak state, equality-based timer resets, and acceleration per update.
5. A separate decision and implementation for scanline synchronization, outside amplitude/peak formulas.

This static phase neither audited the entire current SDK ABI nor built an x64 project. Feasibility was an algorithm/architecture conclusion, not compilation or target-program validation.

## 7. Unknowns and subsequent validation specification

The normal path's core logic is recovered, but these remain unproven by this phase:

- The screenshot's exact preset, FFT mode, refresh interval, and peak settings; one image cannot establish them, and the running user configuration was not read.
- Adaptive mapping at k=0, narrow windows, no channels, integer overflow, and other boundary cases.
- Pixel endpoint rules for all rotations, both antialiasing modes, and Simple-line.
- FFT window, normalization, and floating-point representation differences between old and new host SDK streams.
- Clearing versus retaining peaks after stop or invalid chunks, and every cache rebuild path.

Suggested comparisons: left-only/right-only tones for mirroring; sweeps for bin grouping and coordinates; fixed levels for amplitude slopes; short tones followed by silence for hold/acceleration; varying widths for pitch quantization; varying refresh intervals for update-based rather than time-based motion. This phase recorded that specification without generating test audio, running the original plugin, or implementing the port.

## 8. File index

- `../channel-spectrum-disassembly.txt`: local full disassembly, excluded from Git.
- `evidence/sample.json`: sample identity.
- `evidence/constants.tsv`: decoded floating constants and original bytes.
- `evidence/resource-labels.tsv`: original UI text.
- `evidence/coordinate-builder.txt`: full horizontal coordinate-builder body with original disassembly line numbers.
- `evidence/amplitude-and-peaks.txt`: amplitude, aggregation, and peak evidence.
- `evidence/peak-dialog.txt`, `evidence/main-settings-ui.txt`, `evidence/checkbox-bindings.txt`, `evidence/direction-ui.txt`: controls and configuration bindings.
- `evidence/settings-transfer.txt`, `evidence/defaults.txt`: applying settings and constructor defaults.
- `evidence/audio-input.txt`, `evidence/float-truncation.txt`: input interfaces and truncation.

Project-authored text uses UTF-8/CRLF. The original DLL was neither copied into the project nor modified.

## 9. Algebra review checkpoints

These x87 stack checkpoints preserve details that are easy to misread. Stack notation lists ST(0), ST(1), and so on from left to right.

### Amplitude

- Before 0x10004225, old mode produces [K,1,2]; new mode produces [1,1,2].
- At 0x10004231-0x10004239, old+log replaces the first item with ln(1+2K). Multiplication by R followed by FDIVR 100 yields normalization coefficient B.
- Before 0x10004477, the effective stack is [0,1/4,2^-17,B,1,2]; the leading zero is a loop temporary.
- After new mode's `fstp st(5)` at 0x1000448B, the stack is [1/4,2^-17,B,1,0].
- New+log retains 1/4; new+nonlog loads 2/7 at 0x100044D4.
- 0x100044DE adds 1 and compares with zero. Nonpositive results take a zero-producing path; other results recompute log10(max(A,2^-17)) and multiply by B.
- Old branches form B*A or B*ln(1+2A). The four formulas are therefore instruction-derived rather than guesses from control labels.

### Logarithmic frequency

- Near 0x1000B785, the stack is approximately [Q(k-1),ln(1025),D,1024,1], with Q(k) stored temporarily.
- 0x1000B7F4-0x1000B821 selects max(m,S*m/D), not min. Misreading this comparison changes low-frequency density.
- 0x1000B854-0x1000B87F forms F_m(k-1) and F_m(k).
- After fabs and comparison with 1, `test ah,0x41; jp` takes the anchor-reset branch for finite values greater than 1. Equality does not reset it.
- This threshold uses continuous pixel distance before P quantization; bar pitch P cannot replace the threshold 1.

### Constructor colors

0x1002169C-0x100216D7 gives ForeGround1=#E4E4E4, ForeGround2=#202020, BackGround=#000000, and Peak=#F5F5F5. These match the reference's gray-white-on-black appearance, but constructor defaults do not prove that the screenshot used unchanged colors or a particular gradient mode.

Additional evidence:

- `evidence/resource-controls.tsv`: control IDs, labels, and coordinates parsed directly from dialog templates.
- `evidence/rotation-dispatch.txt`: the Rotate=0 jump-table entry reaches 0x100034C0.
- `evidence/formula-examples.tsv`: numerical examples calculated from these formulas without executing the DLL; not runtime validation.
