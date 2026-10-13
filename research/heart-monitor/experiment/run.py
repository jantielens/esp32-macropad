"""Temporary offline experiment; outputs are not clinical measurements."""

import json
import io
import hashlib
from pathlib import Path
import subprocess
import sys
import shutil

import neurokit2 as nk
import numpy as np
import pandas as pd
from scipy import signal

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools/heart-monitor"))
from heart_analysis import load_capture
from replay import replay_capture

OUTPUT = Path(__file__).resolve().parent
WORK = ROOT / "build/temp/heart/experiment"


def spo2_binary(method):
    binary = WORK / f"{method}-runner"
    source = OUTPUT / ("algorithm_by_RF.cpp" if method == "rf" else "spo2_algorithm.cpp")
    dependencies = [source, OUTPUT / "spo2_runner.cpp", *OUTPUT.glob("*.h")]
    if not binary.exists() or binary.stat().st_mtime < max(path.stat().st_mtime for path in dependencies):
        WORK.mkdir(parents=True, exist_ok=True)
        command = [shutil.which("g++") or "g++", "-std=c++11", "-O2", "-fwrapv", "-I", str(OUTPUT),
                   str(OUTPUT / "spo2_runner.cpp"), str(source), "-o", str(binary)]
        if method == "rf":
            command.append("-DREFERENCE_RF")
        subprocess.run(command, check=True)
    return binary


def ratio_window(red, infrared):
    red_dc, infrared_dc = float(np.mean(red)), float(np.mean(infrared))
    red_ac = signal.detrend(red)
    infrared_ac = signal.detrend(infrared)
    red_rms = float(np.sqrt(np.mean(red_ac ** 2)))
    infrared_rms = float(np.sqrt(np.mean(infrared_ac ** 2)))
    ratio = red_rms / red_dc / (infrared_rms / infrared_dc)
    return {
        "red_dc": red_dc, "ir_dc": infrared_dc,
        "red_ac_rms": red_rms, "ir_ac_rms": infrared_rms,
        "ratio": ratio,
        "red_ir_correlation": float(np.corrcoef(red_ac, infrared_ac)[0, 1]),
        "uncalibrated_polynomial_percent": -45.060 * ratio ** 2 + 30.354 * ratio + 94.845,
    }


def main():
    captures, ratios, intervals, rhythm, references, comparisons = [], [], [], [], [], []
    for path in sorted((OUTPUT.parent / "s1").glob("capture-*.csv")):
        capture = load_capture(path)
        if capture.warnings:
            raise ValueError(f"{path.name}: {capture.warnings}")
        if capture.sample_rate_hz != 100:
            raise ValueError("Initial experiment expects the verified 100 Hz acquisition")
        replay = replay_capture(capture)
        replay.to_csv(OUTPUT / f"replay-{path.stem}.csv", index=False)
        events = replay.loc[replay.beat_detected == 1, "beat_timestamp_ms"].to_numpy() / 1000
        events = np.unique(events[events >= 2])
        for previous, current in zip(events[:-1], events[1:]):
            intervals.append({"capture": path.stem, "start_s": previous,
                              "end_s": current, "interval_ms": (current - previous) * 1000})
        duration = len(capture.samples) / capture.sample_rate_hz
        filtered = signal.sosfiltfilt(signal.butter(3, [0.67, 8], btype="bandpass",
                                                  fs=100, output="sos"),
                                     -capture.samples.ir.to_numpy(dtype=float))
        reference_events = nk.ppg_findpeaks(filtered, sampling_rate=100,
                                            method="charlton2025")["PPG_Peaks"] / 100
        reference_events = reference_events[(reference_events >= 2) & (reference_events < duration - 2)]
        comparable_events = events[events < duration - 2]
        causal_reference_events = nk.ppg_findpeaks(-replay.waveform.fillna(0).to_numpy(),
                                                  sampling_rate=100,
                                                  method="charlton2025")["PPG_Peaks"] / 100
        causal_reference_events = causal_reference_events[
            (causal_reference_events >= 2) & (causal_reference_events < duration - 2)]
        pd.DataFrame({"time_s": reference_events}).to_csv(
            OUTPUT / f"msptd-{path.stem}.csv", index=False)
        nearest = np.array([np.min(np.abs(reference_events - event)) for event in comparable_events])
        comparisons.append({"capture": path.stem, "v2_events": len(comparable_events),
                            "neurokit_msptdfast_v2_events": len(reference_events),
                            "msptd_on_production_waveform_events": len(causal_reference_events),
                            "v2_near_reference_within_150ms": int(np.sum(nearest <= 0.15)),
                            "median_nearest_reference_ms": float(np.median(nearest) * 1000),
                            "reference_median_interval_bpm": 60 / np.median(np.diff(reference_events)),
                            "interpretation": "detector agreement only; no labelled beats"})
        captures.append({"capture": path.stem, "samples": len(capture.samples),
                         "duration_s": duration, "detected_events_after_startup": len(events),
                         "usable_fraction": float((replay.state.str.lower() == "usable").mean()),
                         "states": json.dumps(replay.state.value_counts().to_dict())})
        red = np.rint(signal.resample_poly(capture.samples.red.to_numpy(dtype=float), 1, 4))
        infrared = np.rint(signal.resample_poly(capture.samples.ir.to_numpy(dtype=float), 1, 4))
        starts = list(range(50, len(red) - 99, 100))
        batch = "".join(f"{int(infrared[index])} {int(red[index])}\n"
                        for start in starts for index in range(start, start + 100))
        for method in ["maxim", "rf"]:
            process = subprocess.run([str(spo2_binary(method))], input=batch,
                                     text=True, capture_output=True, check=True)
            reference = pd.read_csv(io.StringIO(process.stdout))
            assert len(reference) == len(starts)
            reference["capture"] = path.stem
            reference["method"] = method
            reference["start_s"] = np.array(starts) / 25
            references.extend(reference.to_dict("records"))
            if method == "rf":
                for start, row in zip(starts, reference.to_dict("records")):
                    if row["spo2_valid"]:
                        expected = ratio_window(red[start:start + 100], infrared[start:start + 100])
                        assert abs(row["spo2"] - expected["uncalibrated_polynomial_percent"]) < 0.02
        for start in range(50, len(red) - 99, 100):
            ratios.append({"capture": path.stem, "start_s": start / 25,
                           **ratio_window(red[start:start + 100], infrared[start:start + 100])})
        for start in np.arange(2, duration - 30 + 1e-8, 30):
            window_events = events[(events >= start) & (events < start + 30)]
            pulse_intervals = np.diff(window_events) * 1000
            changes = np.diff(pulse_intervals)
            if len(pulse_intervals) < 3:
                continue
            rhythm.append({"capture": path.stem, "start_s": start, "beats": len(window_events),
                           "median_pulse_rate_bpm": 60000 / np.median(pulse_intervals),
                           "pulse_interval_sd_ms": np.std(pulse_intervals, ddof=1),
                           "successive_difference_rms_ms": np.sqrt(np.mean(changes ** 2)),
                           "interval_cv": np.std(pulse_intervals, ddof=1) / np.mean(pulse_intervals),
                           "af_label": "unknown; no ECG reference"})
    if not captures:
        raise ValueError("No raw captures found")
    for name, rows in [("captures", captures), ("ratios", ratios),
                       ("intervals", intervals), ("rhythm", rhythm),
                       ("spo2-references", references), ("detector-comparison", comparisons)]:
        frame = pd.DataFrame(rows)
        frame.to_csv(OUTPUT / f"{name}.csv", index=False)
        print(f"{name}: {len(frame)} rows")
        if name in {"captures", "detector-comparison"}:
            print(frame.to_string(index=False))
    assert np.isclose(ratio_window(1000 + np.sin(np.arange(100)),
                                  2000 + 2 * np.sin(np.arange(100)))["ratio"], 1)
    sources = [ROOT / "src/app/device_classes/heart_monitor/heart_bpm.cpp",
               *[OUTPUT / name for name in ["spo2_algorithm.cpp", "spo2_algorithm.h",
                                            "algorithm_by_RF.cpp", "algorithm_by_RF.h"]]]
    manifest = {"purpose": "experimental, nonclinical; no firmware changes",
                "neurokit_version": nk.__version__, "python": sys.version,
                "source_sha256": {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
                                  for path in sources},
                "capture_sha256": {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
                                   for path in sorted((OUTPUT.parent / "s1").glob("capture-*.csv"))},
                "settings": {"startup_excluded_s": 2, "ratio_windows_s": 4,
                             "spo2_resampling": "100 to 25 Hz scipy resample_poly, rounded counts",
                             "msptd": "NeuroKit charlton2025, IR negated, zero-phase 0.67-8 Hz",
                             "msptd_filter_control": "same detector on negated production replay waveform",
                             "rhythm_windows_s": 30, "rhythm_regularization_gate": False},
                "limitations": ["Unlabelled captures cannot establish sensitivity or beat accuracy",
                                "SpO2 polynomial is uncalibrated for these optics",
                                "Rhythm features are not AF predictions",
                                "NeuroKit differs from original MATLAB refinement timing",
                                "qppgfast and pretrained AF inference not yet run"]}
    (OUTPUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")


if __name__ == "__main__":
    main()