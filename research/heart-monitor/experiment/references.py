"""Thin host adapter for unchanged ppg-beats MATLAB references."""

import json
import argparse
import os
from pathlib import Path
import subprocess

import numpy as np
from scipy.io import loadmat, savemat
from scipy import signal
import pandas as pd
from run import WORK

OUTPUT = Path(__file__).resolve().parent
PREFIX = WORK / "octave-local"


def octave(expression):
    environment = os.environ.copy()
    environment.update(json.loads((PREFIX / "environment.json").read_text()))
    preamble = (f"addpath('{OUTPUT}'); "
                f"addpath(genpath('{PREFIX}/usr/share/octave/packages')); ")
    return subprocess.run([str(PREFIX / "usr/bin/octave-cli"), "--no-gui", "--quiet",
                           "--eval", preamble + expression], env=environment,
                          capture_output=True, text=True, check=True)


def detect(signals, name, sample_rate=100):
    signals = np.asarray(signals, dtype=float)
    if signals.ndim == 1:
        signals = signals[:, None]
    assert signals.ndim == 2 and np.isfinite(signals).all()
    input_path = OUTPUT / f"reference-input-{name}.mat"
    output_path = OUTPUT / f"reference-output-{name}.mat"
    savemat(input_path, {"signals": signals})
    octave(f"reference_runner('{input_path}', '{output_path}', {sample_rate});")
    result = loadmat(output_path, simplify_cells=True)["results"]
    rows = [result] if isinstance(result, dict) else list(result)
    return [{key: (np.asarray(value).reshape(-1) - 1) / sample_rate
             for key, value in row.items()} for row in rows]


def local():
    from run import ROOT
    from heart_analysis import load_capture

    rows, all_events = [], []
    for path in sorted((OUTPUT.parent / "s1").glob("capture-*.csv")):
        capture = load_capture(path)
        replay = pd.read_csv(OUTPUT / f"replay-{path.stem}.csv")
        filtered = signal.sosfiltfilt(signal.butter(3, [0.67, 8], fs=100,
                                                  btype="bandpass", output="sos"),
                                     -capture.samples.ir.to_numpy(dtype=float))
        inputs = np.column_stack([filtered, -replay.waveform.fillna(0).to_numpy()])
        inputs = np.clip(4000 * (inputs - inputs.min(axis=0)) / np.ptp(inputs, axis=0) - 2000, -2000, 2000)
        assert np.isfinite(inputs).all() and inputs.min() >= -2000 and inputs.max() <= 2000
        references = detect(inputs, path.stem)
        events = {"production-v2": replay.loc[replay.beat_detected == 1, "beat_timestamp_ms"].to_numpy() / 1000}
        for waveform, result in zip(["offline-0.67-8Hz", "production-waveform"], references):
            for method in ["msptd", "qppg"]:
                events[f"exact-{method}-{waveform}"] = result[f"{method}_peaks"]
        duration = len(capture.samples) / 100
        for method, times in events.items():
            interior = times[(times >= 10) & (times < duration - 2)]
            rows.append({"capture": path.stem, "method": method,
                         "start_s": 10, "end_s": duration - 2, "detected_beats": len(interior),
                         "median_interval_bpm": 60 / np.median(np.diff(interior)) if len(interior) > 1 else np.nan})
            all_events.extend({"capture": path.stem, "method": method, "time_s": time} for time in interior)
        print(path.stem, "complete", flush=True)
    frame = pd.DataFrame(rows)
    frame.to_csv(OUTPUT / "exact-local-comparison.csv", index=False)
    pd.DataFrame(all_events).to_csv(OUTPUT / "exact-local-events.csv", index=False)
    print(frame.pivot(index="capture", columns="method", values="detected_beats").to_string())


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--local", action="store_true")
    if parser.parse_args().local:
        local()
        raise SystemExit(0)
    times = np.arange(3000) / 100
    result = detect(np.sin(2 * np.pi * times), "synthetic")[0]
    for method in ["msptd_peaks", "qppg_peaks"]:
        interior = result[method][(result[method] >= 10) & (result[method] < 28)]
        assert 16 <= len(interior) <= 20, (method, len(interior))
        assert np.isclose(np.median(np.diff(interior)), 1, atol=0.02)
        print(method, len(interior), "interior beats; median interval", np.median(np.diff(interior)))