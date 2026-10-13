"""Offline exploration of heart_capture_v1 files; not clinical measurements."""

import csv
from dataclasses import dataclass
from pathlib import Path

import numpy as np
import pandas as pd
from scipy import signal


@dataclass
class Capture:
    path: Path
    metadata: dict[str, str]
    samples: pd.DataFrame
    sample_rate_hz: float
    warnings: list[str]


def load_capture(path: str | Path) -> Capture:
    path = Path(path).expanduser().resolve()
    metadata = {}
    with path.open(newline="", encoding="utf-8-sig") as source:
        for line in source:
            if line.startswith("#"):
                fields = next(csv.reader([line[1:].strip()]))
                if len(fields) >= 2:
                    metadata[fields[0].strip()] = ",".join(fields[1:]).strip()
    if metadata.get("format") != "heart_capture_v1":
        raise ValueError("Expected a heart_capture_v1 recording")
    samples = pd.read_csv(path, comment="#", encoding="utf-8-sig")
    columns = ["sample_index", "nominal_time_us", "read_elapsed_us", "red", "ir"]
    if list(samples.columns) != columns or samples.empty:
        raise ValueError("Missing samples or unexpected CSV columns")
    samples = samples.apply(pd.to_numeric, errors="raise")
    values = samples.to_numpy(dtype=float)
    if not np.isfinite(values).all() or (values < 0).any() or (values != np.floor(values)).any():
        raise ValueError("Samples must contain finite, nonnegative integers")
    sample_rate = float(metadata["effective_sample_rate_hz"])
    if not np.isfinite(sample_rate) or sample_rate <= 0:
        raise ValueError("Invalid effective sample rate")
    samples["time_s"] = samples.nominal_time_us / 1e6
    warnings = []
    if metadata.get("export_complete") != "true":
        warnings.append("Missing export completion marker; this may be a partial file")
    if metadata.get("capture_status") != "complete":
        warnings.append("Capture was interrupted; nominal time cannot represent lost samples")
    if metadata.get("samples") != str(len(samples)):
        warnings.append("Footer sample count is missing or differs from the stored rows")
    if not np.array_equal(samples.sample_index, np.arange(len(samples))):
        warnings.append("Stored indices are not contiguous; uniform-sample analysis is disabled")
    expected_times = samples.sample_index.to_numpy() * 1e6 / sample_rate
    if not np.allclose(samples.nominal_time_us, expected_times, rtol=0, atol=1):
        warnings.append("Nominal times differ from the configured sample rate")
    if (samples.read_elapsed_us.diff().dropna() < 0).any():
        warnings.append("Host FIFO poll timestamps moved backwards")
    for key in ("fifo_overflow_counter", "ambient_overflow_events"):
        if metadata.get(key, "0") != "0":
            warnings.append(f"{key}: {metadata[key]}")
    if (samples[["red", "ir"]] >= 262143).any().any():
        warnings.append("At least one optical sample reaches the 18-bit maximum")
    if (samples[["red", "ir"]] == 0).any().any():
        warnings.append("At least one optical sample is zero")
    return Capture(path, metadata, samples, sample_rate, warnings)


def analyze_capture(
    capture: Capture,
    startup_s: float = 1.0,
    low_hz: float = 0.4,
    high_hz: float = 4.0,
    edge_s: float = 2.0,
    min_distance_s: float = 0.35,
    prominence_sigma: float = 1.0,
    window_s: float = 10.0,
    invert: bool = False,
) -> dict[str, pd.DataFrame]:
    """Return exploratory signals, candidate beats, spectra and window summaries.

    Filtering is zero-phase and noncausal. Startup is excluded explicitly;
    filter edges remain plotted but are flagged and excluded from summaries.
    Peak thresholds are heuristic, not a contact or clinical quality classifier.
    """
    sample_rate = capture.sample_rate_hz
    if not np.array_equal(capture.samples.sample_index, np.arange(len(capture.samples))):
        raise ValueError("Cannot filter a recording with noncontiguous stored indices")
    if not np.allclose(np.diff(capture.samples.time_s), 1 / sample_rate, rtol=0, atol=1e-6):
        raise ValueError("Cannot filter samples with nonuniform nominal times")
    parameters = [startup_s, low_hz, high_hz, edge_s, min_distance_s, prominence_sigma, window_s]
    if not np.isfinite(parameters).all():
        raise ValueError("Analysis parameters must be finite")
    if not 0 < low_hz < high_hz < sample_rate / 2:
        raise ValueError("Require 0 < low_hz < high_hz < Nyquist frequency")
    if startup_s < 0 or edge_s < 0 or min_distance_s <= 0 or prominence_sigma <= 0 or window_s <= 0:
        raise ValueError("Invalid startup, edge, peak or window settings")
    samples = capture.samples.loc[capture.samples.time_s >= startup_s].copy().reset_index(drop=True)
    if len(samples) < max(32, int((2 * edge_s + 1) * sample_rate)):
        raise ValueError("Recording is too short for these startup and edge settings")
    samples["eligible"] = (
        (samples.time_s >= samples.time_s.iloc[0] + edge_s)
        & (samples.time_s <= samples.time_s.iloc[-1] - edge_s)
    )
    sos = signal.butter(3, [low_hz, high_hz], btype="bandpass", fs=sample_rate, output="sos")
    beat_rows, interval_rows, spectrum_rows, window_rows, summary_rows = [], [], [], [], []
    for channel in ("red", "ir"):
        filtered = signal.sosfiltfilt(sos, samples[channel].to_numpy(dtype=float))
        samples[f"{channel}_filtered"] = filtered
        usable = filtered[samples.eligible.to_numpy()]
        polarity = -1 if invert else 1
        peaks, properties = signal.find_peaks(
            polarity * filtered,
            distance=max(1, int(min_distance_s * sample_rate)),
            prominence=prominence_sigma * np.std(usable),
        )
        for peak, prominence in zip(peaks, properties["prominences"]):
            beat_rows.append({"channel": channel, "time_s": samples.time_s.iloc[peak],
                              "value": filtered[peak], "prominence": prominence,
                              "eligible": bool(samples.eligible.iloc[peak])})
        for previous, current in zip(peaks[:-1], peaks[1:]):
            interval = samples.time_s.iloc[current] - samples.time_s.iloc[previous]
            interval_rows.append({"channel": channel, "time_s": samples.time_s.iloc[current],
                                  "interval_s": interval, "candidate_bpm": 60 / interval,
                                  "eligible": bool(samples.eligible.iloc[previous] and samples.eligible.iloc[current])})
        frequencies, power = signal.periodogram(usable, fs=sample_rate, window="hann")
        band = (frequencies >= low_hz) & (frequencies <= high_hz)
        dominant_hz = frequencies[band][np.argmax(power[band])]
        spectrum_rows.extend({"channel": channel, "frequency_hz": frequency, "power": density}
                             for frequency, density in zip(frequencies[band], power[band]))
        eligible_intervals = [row["interval_s"] for row in interval_rows
                              if row["channel"] == channel and row["eligible"]]
        baseline = samples.loc[samples.eligible, channel].mean()
        summary_rows.append({"channel": channel, "baseline_counts": baseline,
                             "filtered_rms_counts": np.std(usable),
                             "filtered_rms_percent": 100 * np.std(usable) / baseline if baseline else np.nan,
                             "spectral_candidate_bpm": 60 * dominant_hz,
                             "peak_interval_candidate_bpm": 60 / np.median(eligible_intervals) if eligible_intervals else np.nan,
                             "eligible_peaks": int(samples.eligible.iloc[peaks].sum())})
        eligible_samples = samples.loc[samples.eligible]
        start = eligible_samples.time_s.iloc[0]
        end = eligible_samples.time_s.iloc[-1] + 1 / sample_rate
        while start + window_s <= end + 1e-8:
            window = samples.loc[(samples.time_s >= start) & (samples.time_s < start + window_s)]
            frequencies, power = signal.periodogram(window[f"{channel}_filtered"], fs=sample_rate, window="hann")
            band = (frequencies >= low_hz) & (frequencies <= high_hz)
            dominant_hz = frequencies[band][np.argmax(power[band])]
            intervals = [row["interval_s"] for row in interval_rows
                         if row["channel"] == channel and row["eligible"]
                         and row["time_s"] - row["interval_s"] >= start
                         and row["time_s"] < start + window_s]
            window_rows.append({"channel": channel, "start_s": start, "end_s": start + window_s,
                                "spectral_candidate_bpm": 60 * dominant_hz,
                                "peak_interval_candidate_bpm": 60 / np.median(intervals) if intervals else np.nan,
                                "intervals": len(intervals),
                                "red_ir_correlation": window.red_filtered.corr(window.ir_filtered)
                                if "ir_filtered" in window else np.nan})
            start += window_s
    windows = pd.DataFrame(window_rows, columns=[
        "channel", "start_s", "end_s", "spectral_candidate_bpm",
        "peak_interval_candidate_bpm", "intervals", "red_ir_correlation",
    ])
    correlations = windows.loc[windows.channel == "ir", "red_ir_correlation"].to_numpy()
    windows.loc[windows.channel == "red", "red_ir_correlation"] = correlations
    return {
        "signals": samples,
        "beats": pd.DataFrame(beat_rows, columns=["channel", "time_s", "value", "prominence", "eligible"]),
        "intervals": pd.DataFrame(interval_rows, columns=["channel", "time_s", "interval_s", "candidate_bpm", "eligible"]),
        "spectrum": pd.DataFrame(spectrum_rows),
        "windows": windows,
        "summary": pd.DataFrame(summary_rows),
    }