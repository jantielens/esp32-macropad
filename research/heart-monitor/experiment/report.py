"""Render the measured pilot outputs as a standalone local report."""

import hashlib
import importlib
import json

import numpy as np
import pandas as pd
import plotly.graph_objects as go
from plotly.subplots import make_subplots

from run import OUTPUT, ROOT, WORK
from heart_analysis import load_capture


def table(frame):
    return '<div class="table-scroll">' + frame.to_html(index=False, float_format=lambda value: f"{value:.3f}") + '</div>'


def main():
    comparisons = pd.read_csv(OUTPUT / "detector-comparison.csv")
    captures = pd.read_csv(OUTPUT / "captures.csv")
    ratios = pd.read_csv(OUTPUT / "ratios.csv")
    oxygen = pd.read_csv(OUTPUT / "spo2-references.csv")
    public = json.loads((OUTPUT / "public-pilot.json").read_text())
    expanded = json.loads((OUTPUT / "expanded-summary.json").read_text())
    expanded_beats = pd.read_csv(OUTPUT / "expanded-beats.csv")
    exact_local = pd.read_csv(OUTPUT / "exact-local-comparison.csv")
    published = json.loads((OUTPUT / "published-af-summary.json").read_text())
    exclusions = json.loads((OUTPUT / "public-rejections.json").read_text())
    manifest = json.loads((OUTPUT / "manifest.json").read_text())
    for field in ["source_sha256", "capture_sha256"]:
        manifest[field] = {name.replace("build/temp/heart/", "research/heart-monitor/"): digest
                           for name, digest in manifest[field].items()}
    manifest["limitations"] = [item for item in manifest["limitations"]
                               if item != "qppgfast and pretrained AF inference not yet run"]
    manifest["completed_references"] = ["Original MSPTDfast v2", "Original qppgfast",
                                         "Original PhysioNet pretrained nine-model AF vote"]
    manifest["source_urls"] = {
        "maxim": "https://github.com/sparkfun/SparkFun_MAX3010x_Sensor_Library/tree/master/src",
        "rf": "https://github.com/aromring/MAX30102_by_RF",
        "msptd_python": "https://github.com/neuropsychology/NeuroKit/blob/master/neurokit2/ppg/ppg_findpeaks.py",
        "msptd_matlab": "https://github.com/peterhcharlton/ppg-beats/blob/main/source/msptdfastv2_beat_detector.m",
        "dataset": "https://zenodo.org/records/6807403"}
    source_module = importlib.import_module("neurokit2.ppg.ppg_findpeaks")
    manifest["installed_msptd_source_sha256"] = hashlib.sha256(
        open(source_module.__file__, "rb").read()).hexdigest()
    manifest.setdefault("dataset_md5", {}).update({path.name: hashlib.md5(path.read_bytes()).hexdigest()
                                                  for path in WORK.glob("*wfdb.zip")})
    manifest["script_sha256"] = {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
                                for path in [OUTPUT / name for name in ["run.py", "public_pilot.py", "report.py", "spo2_runner.cpp", "Arduino.h"]]}
    (OUTPUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    manifest["continuation_sha256"] = {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
        for path in [*OUTPUT.glob("*.m"), *[OUTPUT / name for name in ["references.py", "expanded.py", "published_af.py", "prepare_octave.py", "af-models.mat", "af-means.mat", "af-stds.mat", "af-test_feature.txt", "af-test_class.txt"]]]}
    manifest["reference_runtime"] = {"octave": "11.1.0, private extracted packages; no system install", "libsvm_official": "3.37.0"}
    manifest["source_urls"]["exact_ppg_references"] = "https://github.com/peterhcharlton/ppg-beats/tree/main/source"
    manifest["source_urls"]["published_af_features_and_models"] = "https://github.com/cliffordlab/PhysioNet-Cardiovascular-Signal-Toolbox/tree/master/Tools/ECG_Analysis_Tools"
    (OUTPUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    colors = ["#167345", "#c94f37", "#5f6871"]
    beat_chart = go.Figure()
    for column, label, color in zip(
        ["v2_events", "neurokit_msptdfast_v2_events", "msptd_on_production_waveform_events"],
        ["Production v2", "NeuroKit MSPTDfast v2, 0.67-8 Hz", "NeuroKit MSPTDfast v2, production waveform"], colors):
        beat_chart.add_bar(x=comparisons.capture.str.replace("capture-", ""),
                           y=comparisons[column], name=label, marker_color=color)
    beat_chart.update_layout(barmode="group", yaxis_title="Detected events, common interior span")
    ratio_chart = make_subplots(rows=2, cols=1, shared_xaxes=True,
                               subplot_titles=["Detrended RMS ratio of ratios", "Red/IR correlation"])
    for capture, frame in ratios.groupby("capture"):
        ratio_chart.add_trace(go.Scatter(x=frame.start_s, y=frame.ratio, mode="lines+markers",
                                         name=capture.replace("capture-", "")), row=1, col=1)
        ratio_chart.add_trace(go.Scatter(x=frame.start_s, y=frame.red_ir_correlation,
                                         mode="lines", name=capture, showlegend=False), row=2, col=1)
    ratio_chart.update_xaxes(title_text="Seconds from capture start", row=2, col=1)
    waveform_chart = go.Figure()
    selected = "capture-657132c2"
    capture = load_capture(OUTPUT.parent / f"s1/{selected}.csv")
    interior = capture.samples[(capture.samples.time_s >= 25) & (capture.samples.time_s < 45)]
    waveform_chart.add_scatter(x=interior.time_s, y=interior.ir, mode="lines", name="Raw IR counts", line_color="#333333")
    replay = pd.read_csv(OUTPUT / f"replay-{selected}.csv")
    event_lists = [replay.loc[replay.beat_detected == 1, "beat_timestamp_ms"].to_numpy() / 1000,
                   pd.read_csv(OUTPUT / f"msptd-{selected}.csv").time_s.to_numpy()]
    for events, label, color in zip(event_lists, ["v2 events", "NeuroKit events"], colors):
        events = events[(events >= 25) & (events < 45)]
        waveform_chart.add_scatter(x=events, y=capture.samples.ir.to_numpy()[np.rint(events * 100).astype(int)],
                                   mode="markers", name=label, marker={"color": color, "size": 9})
    waveform_chart.update_layout(xaxis_title="Seconds", yaxis_title="Raw IR ADC counts")
    charts = []
    for index, chart in enumerate([beat_chart, waveform_chart, ratio_chart]):
        chart.update_layout(template="plotly_white", font={"family": "Georgia", "size": 13},
                            height=480 if index != 2 else 640, margin={"l": 55, "r": 15, "t": 35, "b": 120},
                            legend={"orientation": "h", "y": -0.22})
        charts.append(chart.to_html(full_html=False, include_plotlyjs=True if index == 0 else False,
                                   config={"responsive": True, "displayModeBar": False}))
    acceptance = oxygen.groupby("method").agg(windows=("spo2_valid", "size"),
                                               algorithm_accepted=("spo2_valid", "sum"))
    acceptance["accepted_fraction"] = acceptance.algorithm_accepted / acceptance.windows
    bounds = oxygen[oxygen.spo2_valid == 1].groupby("method").spo2.agg(["min", "median", "max"])
    oxygen_summary = acceptance.join(bounds).reset_index()
    rhythm_metrics = pd.DataFrame([{"method": name, **public[name]} for name in
                                  ["neurokit-msptdfast-v2", "v2-normalized-proxy"]])
    beat_metrics = pd.DataFrame(public["beat_summary"])
    beat_metrics["label_af"] = beat_metrics.label_af.map({0: "non-AF", 1: "AF"})
    full_metrics = pd.DataFrame(expanded["subject_equal_weighted_summary"])
    full_metrics["label_af"] = full_metrics.label_af.map({0: "non-AF", 1: "AF"})
    local_counts = exact_local.pivot(index="capture", columns="method", values="detected_beats").reset_index()
    af_metrics = pd.DataFrame(published["results"])
    af_metrics["possible_windows"] = expanded["subjects_available"] * 3
    af_metrics["coverage"] = af_metrics.evaluated_windows / af_metrics.possible_windows
    af_metrics["insufficient_interval_windows"] = af_metrics.subjects * 3 - af_metrics.evaluated_windows - af_metrics.nonfinite_feature_windows
    sections = [
        '<h1>Heart Monitor: Offline Benchmark</h1><p class="warning">Experimental, nonclinical. No AF diagnosis or calibrated SpO2 measurement. Firmware unchanged.</p>',
        '<h2>Expanded Results</h2><p>Original MATLAB MSPTDfast v2 and qppgfast sources now run unchanged under private Octave 11.1.0. Synthetic 60 BPM checks pass. NeuroKit is a Python comparison implementation, not a firmware replacement.</p>',
        f'<p>{expanded["evaluated_blocks"]}/{expanded["possible_blocks"]} two-minute blocks admitted from {expanded_beats.subject.nunique()}/35 subjects (19 AF, 15 non-AF). Scoring uses seconds 32-118 of each block, after a constant delay calibrated on seconds 2-28; automatic ECG XQRS proxies and one-to-one matching within 150 ms. Averages give subjects equal weight; repeated blocks are not independent participants.</p>',
        table(full_metrics),
        '<p>The exact references retain roughly 94% AF beat-agreement F1 versus 83% for the normalized production-v2 proxy. Non-AF differences are smaller. Matched timing error excludes missed beats. Public replay uses artificial counts and duplicated red/IR, not a real optical acquisition or red-channel-veto validation.</p>',
        '<h3>Exact References On Device Captures</h3><p>Seven unlabelled recordings. Counts use seconds 10 through two seconds before the end, after qppgfast learning. Sensor troughs map to positive peaks. Linear input normalization to +/-2000 prevents valid optical values triggering qppgfast missing-count handling; whole-record normalization remains sensitive to large acquisition/artifact excursions. Compare zero-phase 0.67-8 Hz filtering with the saved production waveform.</p>',
        table(local_counts),
        '<p>Some captures agree, others differ substantially. qppgfast often drops out on the broader-filtered raw path and counts more events on the production waveform. MSPTDfast also disagrees on several captures. Neither more detections nor a faster estimate establishes correctness; genuinely slow pulses must not be rejected simply for being slow. Paired ECG or adjudicated beats are needed to settle these cases.</p>',
        '<h3>Published AF Reference, Not Retrained</h3><p>Original PhysioNet AF_features with native 125 Hz ECG or 100 Hz PPG interval counts; original nine MIT-AFDB-trained RBF SVMs, per-model normalization, eight selected features, probability classification, and majority vote (at least five AF votes). Official libsvm conversion matches independent kernel/calibrated-probability calculations on 100 supplied feature rows. No training on evaluation subjects.</p>',
        '<p>Evaluation uses three 30-second windows from the first complete block per admitted subject, with at least 12 and at most 60 intervals. Each PPG method supplies 102/105 possible windows; ECG supplies 100/105, with two additional insufficient-interval windows. One unavailable subject accounts for three missing windows per method. All admitted windows have finite model inputs; metrics apply only to accepted data.</p>',
        table(af_metrics),
        '<p>The ECG-derived control performs strongly, while PPG transfer changes sensitivity and specificity. Better public beat F1 does not automatically yield the best AF classifier. This is an ECG-trained model applied to PPG without recalibration, not a validated device alarm. Window counts are not independent subjects; labels are subject-level rather than segment-adjudicated; motion SQI and separately labelled ectopy controls are absent.</p>',
        '<p>Upstream qppgfast helpers include GPL provenance. Original AF feature code is GPL and AFEv carries a patent notice; deployment requires review. Pulsewatch weights require sign-in; no verified SiamAF weights URL was found.</p>',
        '<h2>Initial Pilot, Retained For Comparison</h2>',
        f'<p>Seven raw captures, {captures.duration_s.sum():.2f} seconds total; 128 four-second red/IR windows; 13 thirty-second local rhythm windows. Public pilot: first 120 seconds of 35 subjects, 29 admitted (16 AF, 13 non-AF).</p>',
        '<h2>1. Beat Detection</h2><p>Local recordings have no ECG labels. Counts and proximity are detector agreement, not accuracy. MSPTD uses NeuroKit 0.2.13, method charlton2025; its refinement timing differs from the original MATLAB source. Raw IR is negated to map sensor troughs to physiological peaks. Reference processing is noncausal; the production detector is causal. Two seconds at each edge are excluded.</p>',
        charts[0], table(comparisons.drop(columns=["interpretation"])),
        '<h3>Disagreement Example: 657132c2</h3><p>Raw signal and reported event times, not manually labelled beats. Different processing paths can place extrema at different phases.</p>', charts[1],
        '<h3>Public ECG-Proxy Pilot</h3><p>WFDB XQRS supplies automatic ECG beat proxies, not adjudicated labels. The first 30 seconds set one constant timing offset per detector and subject. Scoring uses seconds 32-118 with one-to-one matching within 150 ms; no drift correction. Table shows mean per-subject F1 and timing error of matched beats only. A low timing error does not excuse missed beats.</p>',
        '<p>Production replay receives 125-to-100 Hz resampled bedside PPG inverted, normalized to standard deviation 1000 and offset by 100000 counts, with duplicated red/IR. This is a detector proxy, not a valid MAX30101 acquisition or red-channel-veto test.</p>', table(beat_metrics),
        '<h2>2. SpO2 References</h2><p>Original Maxim/SparkFun and RF C++ sources are unchanged, with licences retained. Inputs use anti-aliased 100-to-25 Hz resampling and rounded counts. Each reference processes consecutive nonoverlapping four-second windows; RF state resets between captures. Maxim uses -fwrapv for host integer semantics. Its header produces two upstream unused-static warnings.</p>',
        '<p class="warning">The table contains uncalibrated algorithm percentages. Maxim accepts outputs as low as 15%, which must not be interpreted as measured oxygen saturation. RF accepts fewer windows, but shares the same unvalidated calibration polynomial. Its periodicity/correlation gates must not be repurposed as AF admission rules.</p>',
        table(oxygen_summary), charts[2],
        '<p>Independent Python linear-detrend/RMS calculations agree with accepted RF polynomial outputs within 0.02 percentage points. This checks arithmetic, not calibration or medical accuracy. Synthetic 60 BPM input returned 60 BPM from both original C++ references.</p>',
        '<h2>3. Rhythm Feasibility</h2><p>Local pulse interval variability is exported without the BPM regularity gate, but missed/extra detections can dominate these features. No AF labels exist for the local captures.</p>',
        '<p>The public classifier is a new exploratory logistic-regression baseline, NOT a replay of PhysioNet, Pulsewatch, or SiamAF. Features: median pulse rate, interval CV, relative RMSSD, pNN50. Standardization and training occur within five subject-separated stratified folds; all windows of a subject stay together. Fixed probability threshold 0.5. Admission requires finite/nonflat signals and at least 12 detected beats, with no regularity gate; no validated motion SQI is present.</p>',
        table(rhythm_metrics),
        '<p>Each admitted subject contributes three 30-second windows. The six excluded subjects account for 18/105 unavailable windows per detector. Metrics apply only to accepted data. Non-AF positive windows/hour is a window count extrapolation, not an operational alarm rate. This small bedside dataset lacks separate motion/ectopy labels, independent external evaluation, uncertainty estimates, and device-specific AF recordings.</p>',
        table(pd.DataFrame(exclusions)),
        '<h2>Conclusions And Remaining Work</h2><ul><li>Keep production v2 for now. MSPTDfast merits a bounded-memory causal/streaming feasibility study, not a blind switch to NeuroKit. Resolve device-capture disagreements with paired ECG or beat adjudication, including slow pulses.</li><li>AF screening remains research. Separate artifact admission from regularity and add independent ectopy/motion controls. Published-model replay is not clinical validation.</li><li>SpO2 is not ruled out, but trustworthy percentages require device-specific optical calibration and paired reference data. Algorithm acceptance is not accuracy. No deliberate hypoxia collection.</li><li>Verify ECG references and segment labels, report subject-level uncertainty, and evaluate on an independent dataset before considering an alarm.</li></ul>',
        '<h2>Artifacts And Reproduction</h2><p><a href="manifest.json">Source, capture and dataset hashes</a> | <a href="detector-comparison.csv">Detector comparison</a> | <a href="ratios.csv">Ratios</a> | <a href="spo2-references.csv">SpO2 reference outputs</a> | <a href="rhythm.csv">Local rhythm features</a> | <a href="public-beat-pilot.csv">Public beat results</a> | <a href="public-rhythm-pilot.csv">Held-out rhythm predictions</a> | <a href="public-pilot.json">Public metrics</a></p>',
        '<pre>.venv-heart/bin/python build/temp/heart/experiment/run.py\n.venv-heart/bin/python build/temp/heart/experiment/public_pilot.py\n.venv-heart/bin/python build/temp/heart/experiment/report.py</pre>',
        '<p><a href="expanded-summary.json">Expanded summary</a> | <a href="expanded-beats.csv">Expanded beat results</a> | <a href="expanded-rejections.json">Expanded rejections</a> | <a href="exact-local-comparison.csv">Exact local counts and median intervals</a> | <a href="exact-local-events.csv">Exact local events</a> | <a href="published-af-summary.json">Published AF metrics</a> | <a href="published-af-results.csv">Published AF predictions</a></p>',
        '<pre>.venv-heart/bin/python build/temp/heart/experiment/references.py\n.venv-heart/bin/python build/temp/heart/experiment/references.py --local\n.venv-heart/bin/python build/temp/heart/experiment/expanded.py\n.venv-heart/bin/python build/temp/heart/experiment/published_af.py --validate\n.venv-heart/bin/python build/temp/heart/experiment/published_af.py\n.venv-heart/bin/python build/temp/heart/experiment/report.py</pre>',
        '<p>Reference runtime packages installed only in .venv-heart: NeuroKit 0.2.13 and WFDB 4.3.1. WFDB installation selected pandas 2.3.3, replacing 3.0.6. No repository requirements or production files changed.</p>']
    html = '<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>Heart Monitor Offline Pilot</title><style>body{margin:0;color:#25282a;background:#fafcfb;font:17px/1.55 Georgia,serif}main{max-width:1160px;margin:auto;padding:24px}h1{font-size:32px}h2{border-top:1px solid #becac3;padding-top:22px;margin-top:38px;font-size:25px}h3{font-size:20px}.warning{border-left:4px solid #bb493c;padding:12px 18px;background:#fff4f1}.table-scroll{overflow:auto;margin:20px 0}table{border-collapse:collapse;font:13px/1.4 monospace;width:100%}th,td{padding:10px;border:1px solid #d4ddd7;text-align:left}th{background:#edf2ef}pre{overflow:auto;background:#edf2ef;padding:18px;font-size:13px}a{color:#146a42}@media(max-width:600px){main{padding:12px}h1{font-size:26px}body{font-size:16px}}</style></head><body><main>' + ''.join(sections) + '</main></body></html>'
    html = html.replace("build/temp/heart/experiment/", "research/heart-monitor/experiment/")
    html = html.replace("<title>Heart Monitor Offline Pilot</title>", "<title>Heart Monitor Offline Benchmark</title>")
    (OUTPUT / "report.html").write_text(html)
    assert "NaN" not in table(rhythm_metrics)
    print(oxygen_summary.to_string(index=False))
    print(f"Report: {OUTPUT / 'report.html'}")


if __name__ == "__main__":
    main()