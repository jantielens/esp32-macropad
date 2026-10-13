"""Small subject-separated feasibility pilot, not a validated AF classifier."""

import argparse
import itertools
import json

import neurokit2 as nk
import numpy as np
import pandas as pd
from scipy import signal
from sklearn.linear_model import LogisticRegression
from sklearn.metrics import confusion_matrix, roc_auc_score
from sklearn.model_selection import StratifiedGroupKFold
from sklearn.pipeline import make_pipeline
from sklearn.preprocessing import StandardScaler
import wfdb
from wfdb.processing import xqrs_detect

from run import OUTPUT, ROOT, WORK
from heart_analysis import Capture
from replay import replay_capture


def match(reference, detected, tolerance=0.15):
    reference_index = detected_index = 0
    differences = []
    while reference_index < len(reference) and detected_index < len(detected):
        difference = detected[detected_index] - reference[reference_index]
        if abs(difference) <= tolerance:
            differences.append(difference)
            reference_index += 1
            detected_index += 1
        elif difference < 0:
            detected_index += 1
        else:
            reference_index += 1
    return np.array(differences)


def align(reference, detected):
    calibration_reference = reference[(reference >= 2) & (reference < 28)]
    calibration_detected = detected[(detected >= 2) & (detected < 28)]
    offsets = np.arange(-1, 1.001, 0.02)
    scores = [len(match(calibration_reference, calibration_detected - offset)) for offset in offsets]
    offset = offsets[int(np.argmax(scores))]
    residual = match(calibration_reference, calibration_detected - offset)
    return float(offset + np.median(residual)) if len(residual) else 0.0


def features(events):
    intervals = np.diff(events)
    if len(intervals) < 11 or not np.all(intervals > 0):
        return None
    mean = np.mean(intervals)
    changes = np.diff(intervals)
    return {"median_bpm": 60 / np.median(intervals),
            "cv": np.std(intervals, ddof=1) / mean,
            "relative_rmssd": np.sqrt(np.mean(changes ** 2)) / mean,
            "pnn50": np.mean(np.abs(changes) > 0.05)}


def main(limit):
    assert len(match(np.array([1.0]), np.array([1.0, 1.01]))) == 1
    assert np.isclose(align(np.arange(1, 29), np.arange(1, 29) + 0.2), 0.2)
    paths = [path for pair in itertools.zip_longest(
        sorted((WORK / "mimic_perform_af_wfdb").glob("*.hea")),
        sorted((WORK / "mimic_perform_non_af_wfdb").glob("*.hea"))) for path in pair if path]
    if not paths:
        raise FileNotFoundError("Download the MIMIC PERform WFDB archives as described in research/heart-monitor/README.md")
    if limit:
        paths = paths[:limit]
    beat_rows, feature_rows, rejected, identities = [], [], [], set()
    for path in paths:
        record = wfdb.rdrecord(str(path.with_suffix("")), sampto=15000)
        subject = record.comments[0].split("<Original Subject ID>:", 1)[1].split()[0]
        assert subject not in identities, f"Repeated subject {subject}"
        identities.add(subject)
        label = int(path.parent.name == "mimic_perform_af_wfdb")
        assert record.fs == 125
        ppg = record.p_signal[:, next(index for index, name in enumerate(record.sig_name) if name.startswith("PPG"))]
        ecg = record.p_signal[:, next(index for index, name in enumerate(record.sig_name) if name.startswith("ECG"))]
        if not np.isfinite(ppg).all() or not np.isfinite(ecg).all() or np.std(ppg) == 0:
            rejected.append({"subject": subject, "reason": "nonfinite or flat waveform"})
            continue
        ecg_events = xqrs_detect(ecg, fs=125, verbose=False) / 125
        ppg100 = signal.resample_poly(ppg, 4, 5)
        scale = np.std(ppg100)
        counts = np.rint(100000 - 1000 * (ppg100 - np.mean(ppg100)) / scale)
        counts = np.clip(counts, 0, 262143).astype(np.uint32)
        samples = pd.DataFrame({"sample_index": np.arange(len(counts)),
                                "time_s": np.arange(len(counts)) / 100,
                                "ir": counts, "red": counts})
        synthetic = Capture(path, {"capture_status": "complete"}, samples, 100, [])
        replay = replay_capture(synthetic)
        production_events = replay.loc[replay.beat_detected == 1, "beat_timestamp_ms"].to_numpy() / 1000
        filtered = signal.sosfiltfilt(signal.butter(3, [0.67, 8], fs=100,
                                                  btype="bandpass", output="sos"), ppg100)
        msptd_events = nk.ppg_findpeaks(filtered, sampling_rate=100,
                                      method="charlton2025")["PPG_Peaks"] / 100
        for method, events in [("v2-normalized-proxy", production_events),
                               ("neurokit-msptdfast-v2", msptd_events)]:
            offset = align(ecg_events, events)
            aligned_events = events - offset
            reference = ecg_events[(ecg_events >= 32) & (ecg_events < 118)]
            detected = aligned_events[(aligned_events >= 32) & (aligned_events < 118)]
            differences = match(reference, detected)
            matched = len(differences)
            beat_rows.append({"subject": subject, "label_af": label, "method": method,
                              "reference_beats": len(reference), "detected_beats": len(detected),
                              "matched_beats": matched, "alignment_offset_s": offset,
                              "proxy_f1": 2 * matched / max(1, len(reference) + len(detected)),
                              "timing_mae_ms": np.mean(np.abs(differences)) * 1000 if matched else np.nan})
            for start in [30, 60, 90]:
                window = events[(events >= start) & (events < start + 30)]
                values = features(window)
                if values is None:
                    rejected.append({"subject": subject, "method": method, "start_s": start,
                                     "reason": "fewer than 12 detected beats; no regularity gate"})
                else:
                    feature_rows.append({"subject": subject, "label_af": label,
                                         "method": method, "start_s": start, **values})
        print(f"{subject}: {'AF' if label else 'non-AF'}", flush=True)
    beats = pd.DataFrame(beat_rows)
    feature_frame = pd.DataFrame(feature_rows)
    metrics = {"subjects": len(identities), "seconds_per_subject": 120,
               "ecg_reference": "WFDB XQRS automatically detected; not manually verified",
               "production_input": "125->100 Hz, inverted PPG scaled to std=1000 plus DC=100000; duplicated red/IR",
               "classification": "exploratory logistic regression, NOT PhysioNet/Pulsewatch/SiamAF reference",
               "admission": "finite/nonflat and >=12 detected beats only; not a validated motion SQI",
               "missing_controls": "no segment-level ectopy/motion labels; no MAX30101 AF validation",
               "beat_summary": beats.groupby(["method", "label_af"])[["proxy_f1", "timing_mae_ms"]].mean().reset_index().to_dict("records")}
    if not limit:
        for method, frame in feature_frame.groupby("method"):
            values = frame[["median_bpm", "cv", "relative_rmssd", "pnn50"]].to_numpy()
            labels = frame.label_af.to_numpy()
            probabilities = np.zeros(len(frame))
            folds = StratifiedGroupKFold(n_splits=5, shuffle=True, random_state=42)
            for train, test in folds.split(values, labels, groups=frame.subject):
                assert set(frame.iloc[train].subject).isdisjoint(frame.iloc[test].subject)
                model = make_pipeline(StandardScaler(), LogisticRegression(class_weight="balanced", random_state=42))
                model.fit(values[train], labels[train])
                probabilities[test] = model.predict_proba(values[test])[:, 1]
            feature_frame.loc[frame.index, "held_out_probability_af"] = probabilities
            true_negative, false_positive, false_negative, true_positive = confusion_matrix(
                labels, probabilities >= 0.5, labels=[0, 1]).ravel()
            metrics[method] = {"accepted_windows": len(frame), "possible_windows": len(paths) * 3,
                               "coverage": len(frame) / (len(paths) * 3),
                               "exploratory_auc": roc_auc_score(labels, probabilities),
                               "sensitivity": true_positive / (true_positive + false_negative),
                               "specificity": true_negative / (true_negative + false_positive),
                               "false_positive_windows": int(false_positive),
                               "non_af_positive_windows_per_hour": false_positive / ((true_negative + false_positive) / 120)}
    beats.to_csv(OUTPUT / "public-beat-pilot.csv", index=False)
    feature_frame.to_csv(OUTPUT / "public-rhythm-pilot.csv", index=False)
    (OUTPUT / "public-pilot.json").write_text(json.dumps(metrics, indent=2) + "\n")
    (OUTPUT / "public-rejections.json").write_text(json.dumps(rejected, indent=2) + "\n")
    print(json.dumps(metrics, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--limit", type=int, default=0)
    main(parser.parse_args().limit)