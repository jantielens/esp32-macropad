"""Replay raw recordings through the same causal C++ estimator as firmware."""

import argparse
import csv
import io
from pathlib import Path
import shutil
import subprocess
import time

import numpy as np
import pandas as pd

from heart_analysis import Capture, load_capture


def build_runner() -> Path:
    root = Path(__file__).resolve().parents[2]
    sources = [Path(__file__).with_suffix(".cpp"),
               root / "src/app/device_classes/heart_monitor/heart_bpm.cpp"]
    header = sources[1].with_suffix(".h")
    binary = root / "build/temp/heart-replay"
    if not binary.exists() or any(path.stat().st_mtime > binary.stat().st_mtime
                                  for path in [*sources, header]):
        compiler = shutil.which("g++")
        if not compiler:
            raise RuntimeError("PC replay requires g++ with C++11 support")
        binary.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run([compiler, "-std=c++11", "-O2", "-Wall", "-Wextra", "-Werror",
                        *(str(path) for path in sources), "-o", str(binary)], check=True)
    return binary


def replay_capture(capture: Capture, speed: float = 0, window_s: float = 10,
                   startup_s: float = 2, min_correlation: float = 0.35,
                   callback=None) -> pd.DataFrame:
    if not np.isfinite([speed, window_s, startup_s, min_correlation]).all() or speed < 0:
        raise ValueError("Replay settings must be finite; speed must be nonnegative")
    if capture.metadata.get("capture_status") != "complete":
        raise ValueError("Interrupted acquisition has unknown gaps; cannot replay as continuous samples")
    if not np.array_equal(capture.samples.sample_index, np.arange(len(capture.samples))):
        raise ValueError("Replay requires contiguous stored sample indices")
    if not np.allclose(np.diff(capture.samples.time_s), 1 / capture.sample_rate_hz, atol=1e-6, rtol=0):
        raise ValueError("Replay requires uniform nominal sample times")
    if (capture.samples[["ir", "red"]] > 262143).any().any():
        raise ValueError("Optical samples exceed the 18-bit range")
    config = f"{capture.sample_rate_hz} {window_s} {startup_s} 1 30 180 {min_correlation} 2\n"
    binary = build_runner()
    if not speed and callback is None:
        inputs = config + "\n".join(f"{int(ir)} {int(red)}" for ir, red in
                      zip(capture.samples.ir, capture.samples.red)) + "\n"
        output = subprocess.run([str(binary)], input=inputs, text=True, capture_output=True, check=True)
        return pd.read_csv(io.StringIO(output.stdout))
    rows = []
    with subprocess.Popen([str(binary)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, text=True, bufsize=1) as process:
        try:
            process.stdin.write(config)
            process.stdin.flush()
            columns = next(csv.reader([process.stdout.readline()]))
            started = time.monotonic()
            for index, (ir, red) in enumerate(zip(capture.samples.ir, capture.samples.red)):
                if speed:
                    remaining = started + index / capture.sample_rate_hz / speed - time.monotonic()
                    if remaining > 0:
                        time.sleep(remaining)
                process.stdin.write(f"{int(ir)} {int(red)}\n")
                process.stdin.flush()
                line = process.stdout.readline()
                if not line:
                    raise RuntimeError(process.stderr.read().strip() or "Replay runner stopped")
                row = dict(zip(columns, next(csv.reader([line]))))
                rows.append(row)
                if callback:
                    callback(row)
            process.stdin.close()
            if process.wait() != 0:
                raise RuntimeError(process.stderr.read().strip() or "Replay failed")
        finally:
            if process.poll() is None:
                process.terminate()
    result = pd.DataFrame(rows)
    for column in result.columns.difference(["state", "method"]):
        result[column] = pd.to_numeric(result[column], errors="raise")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("--speed", type=float, default=0, help="0 = accelerated; 1 = real time")
    parser.add_argument("--output", type=Path, help="Optional replay results CSV (not the raw input)")
    arguments = parser.parse_args()
    capture = load_capture(arguments.capture)
    if arguments.output and arguments.output.resolve() == capture.path:
        parser.error("Output must not overwrite the raw recording")
    for warning in capture.warnings:
        print(f"Warning: {warning}")
    previous = {"second": -1, "pulse": "0"}

    def report(row):
        second = int(float(row["time_s"]))
        if second != previous["second"] or row["pulse"] != previous["pulse"]:
            print(f'{float(row["time_s"]):6.2f}s  {row["state"]:11} '
                  f'BPM {row["bpm"] or "--":>9}  pulse {row["pulse"]}  '
                f'{row["method"]:15} correlation {float(row["correlation"]):.2f} '
                f'flags {int(row["quality_flags"]):#x} '
                f'IBI {float(row["ibi_ms"]):.0f} ms', flush=True)
        previous.update(second=second, pulse=row["pulse"])

    results = replay_capture(capture, speed=arguments.speed, callback=report)
    if arguments.output:
        results.to_csv(arguments.output, index=False)


if __name__ == "__main__":
    main()