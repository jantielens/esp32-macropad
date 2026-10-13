"""Full-recording blockwise reference experiment with explicit missing-data coverage."""

import argparse
import hashlib
import json

import neurokit2 as nk
import numpy as np
import pandas as pd
from scipy import signal
from scipy.io import savemat
import wfdb
from wfdb.processing import xqrs_detect

from public_pilot import align, match
from references import detect
from run import OUTPUT, WORK
from heart_analysis import Capture
from replay import replay_capture


def main(limit, blocks):
    paths = sorted(WORK.glob("mimic_perform*wfdb/*.hea"))
    if not paths:
        raise FileNotFoundError("Download the MIMIC PERform WFDB archives as described in research/heart-monitor/README.md")
    if limit:
        paths = paths[:limit]
    beats, windows, rejected, interval_series = [], [], [], []
    methods = ["v2-normalized-proxy", "neurokit-msptdfast-v2", "exact-msptdfast-v2", "exact-qppgfast"]
    for path in paths:
        record = wfdb.rdrecord(str(path.with_suffix("")))
        subject = record.comments[0].split("<Original Subject ID>:", 1)[1].split()[0]
        label = int(path.parent.name == "mimic_perform_af_wfdb")
        channels = {name.split(",")[0]: index for index, name in enumerate(record.sig_name)}
        assert record.fs == 125
        accepted = []
        possible = min(blocks, len(record.p_signal) // 15000)
        for block in range(possible):
            raw = record.p_signal[block * 15000:(block + 1) * 15000]
            ppg, ecg = raw[:, channels["PPG"]], raw[:, channels["ECG"]]
            if not np.isfinite(ppg).all() or not np.isfinite(ecg).all() or np.std(ppg) == 0 or np.std(ecg) == 0:
                rejected.append({"subject": subject, "label_af": label, "block": block,
                                 "reason": "missing or flat input; neither interpolation nor gap joining"})
                continue
            ppg100 = signal.resample_poly(ppg, 4, 5)
            filtered = signal.sosfiltfilt(signal.butter(3, [0.67, 8], fs=100,
                                                      btype="bandpass", output="sos"), ppg100)
            accepted.append((block, ppg100, ecg, filtered))
        if not accepted:
            print(subject, "no complete blocks", flush=True)
            continue
        matrix = np.column_stack([row[3] for row in accepted])
        digest = hashlib.sha256(matrix.tobytes()).hexdigest()[:12]
        cache_path = OUTPUT / f"exact-cache-{subject}-{digest}.json"
        if cache_path.exists():
            exact = json.loads(cache_path.read_text())
        else:
            exact = detect(matrix, subject)
            cache_path.write_text(json.dumps([{key: value.tolist() for key, value in row.items()}
                                              for row in exact]) + "\n")
        for (block, ppg100, ecg, filtered), reference in zip(accepted, exact):
            ecg_events = xqrs_detect(ecg, fs=125, verbose=False) / 125
            counts = np.clip(np.rint(100000 - 1000 * (ppg100 - np.mean(ppg100)) / np.std(ppg100)),
                             0, 262143).astype(np.uint32)
            samples = pd.DataFrame({"sample_index": np.arange(len(counts)),
                                    "time_s": np.arange(len(counts)) / 100,
                                    "ir": counts, "red": counts})
            replay = replay_capture(Capture(path, {"capture_status": "complete"}, samples, 100, []))
            events_by_method = {
                methods[0]: replay.loc[replay.beat_detected == 1, "beat_timestamp_ms"].to_numpy() / 1000,
                methods[1]: nk.ppg_findpeaks(filtered, sampling_rate=100, method="charlton2025")["PPG_Peaks"] / 100,
                methods[2]: np.asarray(reference["msptd_peaks"]),
                methods[3]: np.asarray(reference["qppg_peaks"])}
            for method, events in events_by_method.items():
                offset = align(ecg_events, events)
                aligned = events - offset
                ecg_interior = ecg_events[(ecg_events >= 32) & (ecg_events < 118)]
                detected = aligned[(aligned >= 32) & (aligned < 118)]
                differences = match(ecg_interior, detected)
                matched = len(differences)
                beats.append({"subject": subject, "label_af": label, "block": block,
                              "method": method, "reference_beats": len(ecg_interior),
                              "detected_beats": len(detected), "matched_beats": matched,
                              "false_extra_beats": len(detected) - matched,
                              "missed_beats": len(ecg_interior) - matched,
                              "proxy_f1": 2 * matched / max(1, len(ecg_interior) + len(detected)),
                              "offset_s": offset,
                              "timing_mae_ms": float(np.mean(np.abs(differences)) * 1000) if matched else None})
            for method, events in {"ecg-xqrs": ecg_events, **events_by_method}.items():
                for start in [30, 60, 90]:
                    local = events[(events >= start) & (events < start + 30)]
                    intervals = np.diff(local)
                    if len(intervals) < 12:
                        rejected.append({"subject": subject, "label_af": label, "block": block,
                                         "method": method, "start_s": start,
                                         "reason": "fewer than 12 intervals for published model"})
                        continue
                    intervals = intervals[:60]
                    windows.append({"subject": subject, "label_af": label, "block": block,
                                    "method": method, "start_s": block * 120 + start,
                                    "intervals": len(intervals), "interval_source": "ECG" if method == "ecg-xqrs" else "PPG"})
                    interval_series.append(intervals)
        print(subject, "AF" if label else "non-AF", f"{len(accepted)}/{possible} complete blocks", flush=True)
    pd.DataFrame(beats).to_csv(OUTPUT / "expanded-beats.csv", index=False)
    pd.DataFrame(windows).to_csv(OUTPUT / "expanded-af-windows.csv", index=False)
    cells = np.empty((len(interval_series), 1), dtype=object)
    for index, intervals in enumerate(interval_series):
        cells[index, 0] = intervals[:, None] * 1000
    savemat(OUTPUT / "expanded-af-intervals.mat", {"intervals": cells})
    (OUTPUT / "expanded-rejections.json").write_text(json.dumps(rejected, indent=2) + "\n")
    frame = pd.DataFrame(beats)
    grouped = frame.groupby(["subject", "label_af", "method"])[["proxy_f1", "timing_mae_ms"]].mean().reset_index()
    summary = grouped.groupby(["method", "label_af"])[["proxy_f1", "timing_mae_ms"]].mean().reset_index()
    totals = frame.groupby(["method", "label_af"])[["reference_beats", "detected_beats", "matched_beats", "missed_beats", "false_extra_beats"]].sum().reset_index()
    metadata = {"subjects_available": len(paths), "blocks_requested_per_subject": blocks,
                "possible_blocks": len(paths) * blocks, "evaluated_blocks": len(frame) // len(methods),
                "block_seconds": 120, "scored_seconds_per_block": 86,
                "subject_equal_weighted_summary": summary.to_dict("records"),
                "beat_totals": totals.to_dict("records"),
                "interval_input": "milliseconds as sample counts at fs=1000; first up to 60 intervals per 30-second window",
                "limitations": ["Automatic ECG references; no independent adjudication",
                                "Subject AF labels, not segment-by-segment verified labels",
                                "Production input normalized and duplicated red/IR",
                                "No motion/ectopy-labelled external dataset",
                                "Original qppgfast helper is GPL; AFEv has a patent notice"]}
    (OUTPUT / "expanded-summary.json").write_text(json.dumps(metadata, indent=2) + "\n")
    print(summary.to_string(index=False))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--limit", type=int, default=0)
    parser.add_argument("--blocks", type=int, default=10)
    args = parser.parse_args()
    main(args.limit, args.blocks)