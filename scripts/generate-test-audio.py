"""Generate and independently inspect deterministic host-test WAVs (stdlib only).

No player is launched. Files go to the ignored build/ directory by default.
Peak levels are sample-peak dBFS, not RMS levels or predicted panel heights.
"""
from pathlib import Path
import argparse
import hashlib
import json
import math
import wave

RATE = 48000
SCALE = (1 << 23) - 1
FADE = 0.010
ROOT = Path(__file__).resolve().parents[1]


def tone(hz, db=-24):
    return {"kind": "tone", "hz": hz, "peak_dbfs": db}


def sweep(start=30, end=18000, db=-24):
    return {"kind": "log_sweep", "start_hz": start, "end_hz": end, "peak_dbfs": db}


def segment(label, seconds, *signals):
    return {"label": label, "seconds": seconds, "signals": list(signals)}


def suite():
    silent = lambda seconds=1: segment("Silence", seconds, None, None)
    return [
        ("01-channel-direction.wav", 2, [
            silent(2), segment("Left only: 375 Hz", 4, tone(375), None), silent(),
            segment("Right only: 375 Hz", 4, None, tone(375)), silent(),
            segment("Both identical: 375 Hz", 4, tone(375), tone(375)), silent(),
            segment("Left only: 6000 Hz", 4, tone(6000), None), silent(),
            segment("Right only: 6000 Hz", 4, None, tone(6000)), silent(),
            segment("Left 375 Hz; right 6000 Hz", 4, tone(375), tone(6000)), silent(2)]),
        ("02-frequency-sweep.wav", 2, [
            silent(2), segment("Left sweep: 30 to 18000 Hz", 8, sweep(), None), silent(2),
            segment("Right sweep: 30 to 18000 Hz", 8, None, sweep()), silent(2),
            segment("Both identical sweeps: 30 to 18000 Hz", 8, sweep(), sweep()), silent(2)]),
        ("03-amplitude-steps.wav", 2, [silent(2)] + [
            item for db in (-48, -36, -24, -18)
            for item in (segment(f"Both 375 Hz: {db} dBFS sample peak", 4,
                                 tone(375, db), tone(375, db)), silent(2))]),
        ("04-peak-decay.wav", 2, [silent(2)] + [
            item for db in (-36, -24, -18)
            for item in (segment(f"One-second burst: {db} dBFS sample peak", 1,
                                 tone(375, db), tone(375, db)), silent(5))]),
        ("05-mono-layout.wav", 1, [
            segment("Silence", 2, None), segment("Mono 375 Hz", 4, tone(375)),
            segment("Silence", 2, None), segment("Mono 6000 Hz", 4, tone(6000)),
            segment("Silence", 2, None)]),
    ]


def sample(signal, index, frames):
    if signal is None:
        return 0
    t = index / RATE
    if signal["kind"] == "tone":
        cycles = signal["hz"] * t
    else:
        rate = math.log(signal["end_hz"] / signal["start_hz"]) / (frames / RATE)
        cycles = signal["start_hz"] * math.expm1(rate * t) / rate
    fade = round(FADE * RATE)
    envelope = min(1.0, index / fade, (frames - 1 - index) / fade)
    return round(SCALE * 10 ** (signal["peak_dbfs"] / 20) * envelope * math.sin(2 * math.pi * cycles))


def write_audio(path, channels, segments):
    timeline = []
    cursor = 0
    with wave.open(str(path), "wb") as out:
        out.setparams((channels, 3, RATE, 0, "NONE", "not compressed"))
        for spec in segments:
            frames = round(spec["seconds"] * RATE)
            assert len(spec["signals"]) == channels
            payload = bytearray()
            for i in range(frames):
                for signal in spec["signals"]:
                    value = sample(signal, i, frames)
                    payload.extend((value & 0xFFFFFF).to_bytes(3, "little"))
            out.writeframes(payload)
            timeline.append(dict(spec, start_frame=cursor, end_frame=cursor + frames,
                                 start_seconds=cursor / RATE, end_seconds=(cursor + frames) / RATE))
            cursor += frames
    return timeline, cursor


def unpack24(payload, channels):
    values = [[] for _ in range(channels)]
    for i in range(0, len(payload), 3):
        value = int.from_bytes(payload[i:i + 3], "little")
        if value & 0x800000:
            value -= 0x1000000
        values[(i // 3) % channels].append(value)
    return values


def level(value):
    return None if value == 0 else 20 * math.log10(value / SCALE)


def frequency(values):
    # Independent positive-going zero-crossing measurement, with interpolation.
    crossings = []
    for i in range(1, len(values)):
        if values[i - 1] <= 0 < values[i]:
            crossings.append(i - 1 - values[i - 1] / (values[i] - values[i - 1]))
    assert len(crossings) > 2
    return (len(crossings) - 1) * RATE / (crossings[-1] - crossings[0])


def inspect_audio(path, channels, timeline, frames):
    """Read encoded PCM back; do not reuse the synthesis function for validation."""
    report = []
    with wave.open(str(path), "rb") as source:
        assert source.getparams()[:4] == (channels, 3, RATE, frames)
        assert source.getcomptype() == "NONE"
        for spec in timeline:
            count = spec["end_frame"] - spec["start_frame"]
            decoded = unpack24(source.readframes(count), channels)
            assert all(len(values) == count for values in decoded)
            if channels == 2 and spec["signals"][0] == spec["signals"][1]:
                assert decoded[0] == decoded[1], "identical channels differ"
            measurements = []
            for values, signal in zip(decoded, spec["signals"]):
                peak = max(abs(v) for v in values)
                assert peak < SCALE, "clipping"
                assert values[0] == values[-1] == 0, "nonzero segment boundary"
                if signal is None:
                    assert peak == 0, "inactive channel is not digital silence"
                    measurements.append({"peak_dbfs": None, "digital_silence": True})
                    continue
                plateau = values[RATE // 10:-RATE // 10]
                rms = math.sqrt(sum(v * v for v in plateau) / len(plateau))
                assert abs(level(peak) - signal["peak_dbfs"]) < 0.02
                measurement = {"peak_dbfs": level(peak), "plateau_rms_dbfs": level(rms)}
                if signal["kind"] == "tone":
                    hz = frequency(plateau)
                    assert abs(hz - signal["hz"]) < 0.1
                    assert abs(level(rms) - (signal["peak_dbfs"] - 10 * math.log10(2))) < 0.02
                    measurement["measured_hz"] = hz
                else:
                    # Measure successive 0.1 s windows independently of the chirp formula.
                    window = RATE // 10
                    estimates = [frequency(values[i:i + window])
                                 for i in range(window, count - window, window)]
                    assert all(a < b for a, b in zip(estimates, estimates[1:])), "sweep is not increasing"
                    assert signal["start_hz"] < estimates[0] < signal["start_hz"] * 1.25
                    assert signal["end_hz"] * 0.8 < estimates[-1] < signal["end_hz"]
                    measurement["measured_window_hz"] = estimates
                measurements.append(measurement)
            report.append({"label": spec["label"], "channels": measurements})
        assert source.readframes(1) == b""
    return report


def text_file(path, text):
    path.write_bytes(text.replace("\n", "\r\n").encode("utf-8"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "build/host-test-audio")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    records = []
    playlist = ["#EXTM3U"]
    times = ["# Host-test audio timeline", "", "48 kHz / 24-bit integer PCM. Levels are sample-peak dBFS.",
             "10 ms fades at tone/sweep boundaries. Silence intervals are exact digital zero.",
             "Intervals below are [start, end) in seconds within each file.", ""]
    for filename, channels, segments in suite():
        path = args.output / filename
        timeline, frames = write_audio(path, channels, segments)
        measurements = inspect_audio(path, channels, timeline, frames)
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        records.append({"file": filename, "channels": channels, "sample_rate": RATE,
                        "bits_per_sample": 24, "frames": frames, "seconds": frames / RATE,
                        "sha256": digest, "segments": timeline, "inspection": measurements})
        playlist.extend([f"#EXTINF:{frames // RATE},{path.stem}", filename])
        times.extend([f"## {filename}", "", "| Start | End | Signal |", "| --- | --- | --- |"])
        times.extend(f"| {seg['start_seconds']:g} | {seg['end_seconds']:g} | {seg['label']} |" for seg in timeline)
        times.append("")
        print(f"PASS {filename}: {frames / RATE:g} s, {channels} channel(s), SHA256 {digest}", flush=True)
    text_file(args.output / "playlist.m3u8", "\n".join(playlist) + "\n")
    text_file(args.output / "TIMELINE.md", "\n".join(times) + "\n")
    text_file(args.output / "manifest.json", json.dumps({
        "generator": "scripts/generate-test-audio.py", "fade_seconds": FADE,
        "validation": "Encoded PCM inspected; not a foobar2000 playback or original-component comparison.",
        "files": records}, indent=2) + "\n")
    text_file(args.output / "SHA256SUMS.txt", "".join(f"{r['sha256']}  {r['file']}\n" for r in records))
    print(f"Ready: {args.output / 'playlist.m3u8'}")


if __name__ == "__main__":
    main()
