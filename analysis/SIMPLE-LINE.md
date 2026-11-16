# Simple-line analysis and version 0.2.0 implementation

This is a historical 0.2.0 record. For the current GDI renderer, frequency descriptors, and partition connections, see the [0.4.0 reconstruction record](RESTORATION-0.4.md).

## Findings

The original Simple-line style is a contour joining adjacent spectrum points, not a set of separate thin vertical bars. An earlier interpretation based only on its name was incorrect. This analysis cross-checked option order, the jump table, POINT array writes, and GDI calls without executing the original DLL. Evidence excerpts, including original disassembly line numbers, are in `evidence/simple-line.txt`.

## Static evidence

- The Simple-line string is at 0x1002BDF0. Six entries are added in order at 0x1001F35D-0x1001F3D3; Simple-line is index 5.
- At 0x1000353E-0x1000355D, mode 5 sets temporary spacing to zero. At 0x10003651-0x10003679, pitch is forced to 2, ignoring configured bar width and spacing.
- At 0x100035C9-0x100035DB, mode 5 clears the effective peak flag even when Show Peaks is enabled.
- The six jump-table entries at 0x100051D4 are 0x10004616, 0x10004633, 0x10004650, 0x10004681, 0x100046E0, and 0x100045D2.
- At 0x100045D2-0x10004611, direction selects an x endpoint; x and amplitude-derived y are written to a per-channel POINT array, eight bytes per point. This path does not fill a rectangle.
- Only mode 5 passes 0x10004C91. At 0x10004C9C-0x10004CA8, color is read from +0xCDC and passed to CreatePen(PS_SOLID,1,color).
- At 0x10004ED3-0x10004EEA, the point-array address and count are passed to Polyline (IAT 0x1002A020). Earlier, 0x10004D51-0x10004ED0 adds boundary points according to direction and neighboring channels.

## Version 0.2.0 implementation and intentional differences

Simple-line + peaks (contour) introduced a one-pixel solid gray-228 outline without fill and with a 2 px pitch. It connected valid frequency groups within each channel, without inserting zero-amplitude points for groups containing no bins. Left/right data remained independent, with low frequencies at the center. At the user's request, peak animation was retained and gray-245 markers drawn last. This intentionally differs from the original, which disables peaks in this mode.

Separate thin lines + peaks was added as a distinct style: one-pixel solid vertical lines with at least one black pixel between them, configurable extra spacing, and retained peaks. It serves the separate-bar appearance without mislabeling it as the original Simple-line. Gradient bars remained the default.

Version 0.2.0 still used the initial simplified frequency groups and an integer DIB line rasterizer instead of the original GDI Polyline. Endpoint inclusion, exact frequency descriptors, and cross-channel boundary connections were not reproduced pixel for pixel. Amplitude and peak-update formulas were unchanged. This did not establish complete parity for all original styles or rotations.

Configuration version 2 used 48 bytes, appending the style enum 0/1/2. Version 1's 44-byte configuration remained readable, defaulting to gradient bars. Unknown styles fell back to gradient bars; unknown versions and truncated data were rejected. Applying a style through OK rebuilt mapping and cleared peaks; Cancel did not apply changes.

## Validation at version 0.2.0

The Release x64 build and 3,940 offline assertions passed. This includes per-bar checks, not 3,940 independent cases. Added checks covered connected contours without fill, no cross-channel connection at that version, peak overlay, solid bar gaps, channel isolation in both line styles, peak retention after the signal ends, and narrow-window buffer boundaries.

`build/simple-line-preview.bmp` and `build/separate-lines-preview.bmp` were generated with the plugin's calculation and rendering core. They are synthetic previews, not player screenshots.

Actual host loading, dialog interaction, save/restart round trips, and playback comparisons with the original were not completed. A previous Computer Use startup authorization had timed out; no retry or modification of the existing player configuration was performed during this phase. Binary/package checks were recorded locally in `build/verification-0.2.0.json`.
