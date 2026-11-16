# Version 0.4.1: forced direction and Reverse frequency

## Behavior fixed

Version 0.4.0 treated forced Right/Left as overriding Reverse frequency. The original coordinate builder behaves that way internally, but its caller first swaps direction values 1 and 2 when Reverse is enabled (VA 0x100035FC-0x1000361C). Omitting this preprocessing reversed the intended result for those combinations.

The corrected Engine direction is:

| Setting | Reverse off | Reverse on |
| --- | --- | --- |
| Right | Low frequency left, high frequency right | Low frequency right, high frequency left |
| Left | Low frequency right, high frequency left | Low frequency left, high frequency right |
| Mirror, two channels | Bass at center with normal channel order | Treble at center with normal channel order |
| Mirror, one displayed channel | Low frequency left | Low frequency right |

The single-display Mirror flip remains separate from forced directions. Reverse channel positions moves physical partitions without changing each logical channel's frequency direction. No configuration format change is required.

## Regression evidence

The new regression passes spectra through Engine::update, exercising caller direction selection, descriptor construction, grouping, and emitted bar positions. It does not merely call the direction helper.

Ninety-six combinations cover three directions, Reverse on/off, reversed/normal channel positions, linear/logarithmic frequency axes, Simple/Simple-line, and one/two displayed channels. Separate probes at bins 8 and 900 must emit exactly one full-height group per channel, retain the correct physical partition, and place bass/treble in the expected order. Explicit truth tables encode the expected original behavior independently of the helper's implementation.

The new regression failed before the fix with `Engine frequency direction matches original caller and builder`. After the fix, all 8,844 assertions passed, and the existing 13 coordinate fixtures still matched all 17,637 valid descriptors with zero differences. The Release x64 plugin and all three synthetic-preview runs built/passed. These are offline checks, not a complete host playback comparison.

## Host verification status

The existing portable player was still at `New user interface module(s) found`. Its dialog was readable, but selecting Columns UI failed with `failed to activate captured window`. Refreshing the returned window and retrying activation failed with the same error. No successful panel or playback interaction was observed.

The user then asked that Computer Use stop because they were away from the computer. No further UI actions were performed. This test attempt did not confirm loading the 0.4.1 DLL, creating the panel, changing settings, or playing audio in foobar2000. Those tests remain deferred until the user permits resuming computer interaction.

Follow-up host checks: install/load 0.4.1 in the portable copy, test Left/Right with Reverse using separate-channel tones, compare Mirror, test pause/stop/seek and resize, then verify settings across restart and multiple panel instances. Existing player settings were not changed by this test attempt.
