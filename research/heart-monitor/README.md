---
title: Heart Monitor Experiment Handoff
description: Parked firmware, recordings, offline research, verification, conclusions, and next steps.
ms.date: 2026-10-13
---

## Status And Scope

Resume on `experiment/heart-monitor`, branched from `main` at
`a51bf6d` (release `v1.40.0`). The user approved committing and pushing the
recordings and parking the shared Storage changes as well. Nothing in this
experiment has been merged into `main`.

The branch includes the `heart_monitor` device class, `jc4880p433-heart` board,
MAX30101 acquisition, bounded recording and save/retry handling, catalog and
raw/session exports, beat estimation, bindings/actions, Heart Sessions portal,
charts and replay, sample pad, host tests, feature-size accounting, dependencies,
and integration documentation. Generic Storage deletion, sorting, refresh, and
usage updates are parked here too.

> [!WARNING]
> Experimental, nonclinical work. No AF diagnosis or calibrated SpO2 measurement.
> The recordings contain physiological data and may include device identifiers
> and timestamps. Their inclusion was explicitly authorized; do not assume that
> authorization applies to additional subjects or future recordings.

## Archive Map

All paths below are relative to this directory.

<!-- markdownlint-disable MD013 -->

| Path | Contents |
| --- | --- |
| `capture-*.csv`, `capture-d94c9109.json` | Seven earlier raw captures; six have no retained JSON sidecar |
| `s1/capture-*.csv`, `s1/capture-*.json` | Seven later recordings and their metadata |
| `report.html`, `report.js`, `report.css`, `chart.umd.js` | Preserved session report and its local assets |
| `s1/peak-analysis.html`, `s1/holdout-analysis.html` | Earlier causal-detector and holdout reports |
| `s1/*comparison.csv`, `s1/*controls.csv`, `s1/peak-analysis.csv` | Earlier filter, detector, control, and window evidence |
| `s1/replay-sweeps.tar.gz` | All 197 historical s1 CSVs, including detailed replay sweeps and duplicate raw/summary CSVs |
| `s1/analyze_peaks.py`, `s1/*.cpp` | Earlier analysis, candidate implementation, and host probes |
| `experiment/report.html` | Latest expanded detector, SpO2, and pretrained AF report |
| `experiment/*.csv`, `experiment/*summary.json`, `experiment/*pilot.json` | Measured results and coverage summaries |
| `experiment/*rejections.json`, `experiment/exact-cache-*.json` | Exclusions and exact-detector caches |
| `experiment/expanded-af-intervals.mat`, `experiment/published-af-*.mat` | Preserved AF interval inputs and extracted features |
| `experiment/*.py`, `experiment/*.m`, `experiment/*.cpp`, `experiment/*.h` | Reproduction adapters and original algorithm sources |
| `experiment/af-models.mat`, `experiment/af-means.mat`, `experiment/af-stds.mat` | Original pretrained SVMs and their normalization |
| `experiment/af-test_*.txt` | Supplied upstream feature/class examples |
| `*.png`, `*.pdf`, `*.log`, `experiment/*.log`, `s1/*.log` | Visual verification and execution evidence |
| `requirements.lock.txt`, `experiment/octave-packages.json` | Tested Python environment and extracted runtime package versions |
| `SHA256SUMS`, `experiment/manifest.json` | Archive integrity and experiment provenance |

<!-- markdownlint-enable MD013 -->

Reusable capture loading, production replay, and the notebook remain under
`tools/heart-monitor/`; the firmware tests remain in
`tests/test_heart_capture.cpp`. Board and integration documentation remain at
their normal repository paths.

The active notebook defaults to the tracked `s1/` recordings. Its original
saved state, including analysis outputs, is preserved in
`analysis-original.ipynb`.

Installed virtual environments, Octave binaries/packages, C++ executables,
downloaded public datasets, and redundant detector scratch MAT files are not
versioned. Their caches remain under ignored `build/` and `.venv-heart/` paths.
The tracked AF interval inputs allow published-model replay without downloading
the full public dataset again.

## Acquisition And Firmware Decision

The later recordings use paired raw red/IR samples at 100 Hz, averaging 1,
411 microsecond pulse width, 4096 nA ADC range, and LED codes `0x1f`.
Uniform analysis time comes from sample indices, not FIFO polling timestamps.

The parked firmware uses `heart_bpm_v2`: dominant downward-extremum detection
with one-sample confirmation, RMS/slope-based prominence thresholds, and the
event timestamp placed at the extremum. This fixes the recorded rising-shoulder
double-count regression. The report retains `heart_reliable_v1`.

Defaults are unchanged: 10-second window, 2-second startup, 1-second update,
30-180 BPM, 0.5 Hz high-pass, 4 Hz low-pass, minimum correlation 0.35,
minimum RMS 2 counts, 0.3-second refractory period, and contact floor 5000 counts.
Do not reject an estimate because it is slow. BPM confidence does not establish
that each individual pulse interval is valid.

Keep v2 as the firmware baseline while developing the experiment. NeuroKit is
a Python analysis library; an embedded candidate would be the underlying
MSPTDfast algorithm, not a dependency on NeuroKit itself. Original qppgfast and
MSPTDfast also disagree substantially on several local recordings. More events
or a faster pulse estimate does not establish correctness without beat labels.

## Measured Conclusions

The expanded public benchmark uses MIMIC PERform AF, Zenodo record 6807403:
35 subjects (19 AF, 16 non-AF), 20-minute recordings, and 125 Hz ECG/PPG.
It admits 314/350 two-minute blocks from 34 subjects (19 AF, 15 non-AF).
Each block estimates a constant ECG/PPG timing offset on seconds 2-28 and
scores seconds 32-118, with one-to-one matching within 150 ms. Subjects receive
equal weight; blocks from one subject are not independent observations.

| Detector | Non-AF beat F1 | AF beat F1 |
| --- | ---: | ---: |
| Original MSPTDfast v2 | 94.79% | 93.89% |
| Original qppgfast | 95.07% | 94.42% |
| NeuroKit MSPTDfast v2 | 95.26% | 94.19% |
| Production v2 normalized proxy | 93.25% | 82.84% |

ECG beats are automatic XQRS proxies, not adjudicated truth. The production
proxy receives artificial counts and duplicated red/IR, so this does not test
real optical acquisition or the red-channel veto. Local recordings have no
ECG labels. Exact qppgfast often drops out on the broader-filtered local path;
filtering, artifacts, normalization, and morphology all affect transfer.

The original PhysioNet AF feature code and nine pretrained MIT-AFDB RBF SVMs
were executed without retraining on the evaluation subjects. Official libsvm
probability-mode conversion was independently checked on 100 supplied feature
rows. Feature extraction receives native 125 Hz ECG or 100 Hz PPG interval
counts as floating-point inputs. The fixed majority vote requires five AF votes.

| Interval source | Sensitivity | Specificity | Evaluated windows |
| --- | ---: | ---: | ---: |
| ECG XQRS control | 98.25% | 100% | 100 |
| Original MSPTDfast v2 | 91.23% | 88.89% | 102 |
| Original qppgfast | 98.25% | 91.11% | 102 |
| NeuroKit MSPTDfast v2 | 100% | 91.11% | 102 |
| Production v2 normalized proxy | 91.23% | 100% | 102 |

These use three 30-second windows from each subject's first complete block,
with 12-60 intervals. Each PPG method covers 102/105 possible windows; ECG
covers 100/105 because two additional windows lack sufficient intervals.
All admitted model inputs are finite. Labels are subject-level, not verified
segment labels. No independently labelled motion/ectopy controls or validated
motion signal-quality admission are present. This supports AF screening
research, not an operational alarm. Do not confuse these fixed-model results
with the earlier subject-separated exploratory logistic-regression pilot.

SpO2 remains uncalibrated. Original Maxim accepts 78/128 windows and emits
values from 15% to 100%; original RF accepts 27/128 and emits 95.59%-99.70%.
Those are algorithm outputs, not measured saturation. The independent RMS
ratio/polynomial check agrees with accepted RF outputs within 0.02 percentage
points, which verifies arithmetic only. Neither plausible output nor an
acceptance flag proves accuracy. SpO2 is not ruled out, but trustworthy
percentages need device-specific calibration and paired reference data.

## Verification History

Before parking, the 25 `HeartCapture.*` host tests passed. The firmware build
for `jc4880p433-heart` passed with 3,351,414 bytes of application firmware and
155,760 bytes of globals. No flashing was performed.

The parking verification reran all 25 capture cases plus the production heart
binding profile: 26/26 checks passed.

Production estimator SHA-256 at the completed benchmark:
`743ab257ccf4aad4e5921329cadfebb70df0d4466149a9df13458895396c62f7`.

Relocation checks cover seven later capture inputs, ratio arithmetic, runner
paths, synthetic 60 BPM exact detectors, nine pretrained-model conversions,
and report regeneration. The relocated Playwright portal harness also passed
catalog-only listing, paired capture integrity, charts, pointer selection,
replay, printing, 320/390-pixel layouts, abort/cleanup, notes, checksum errors,
empty beats, import, and delete.

Firmware was not rebuilt solely to park the unchanged source. Existing build
logs and screenshots are historical evidence; they are not hardware validation.

## Resume And Reproduce

Run commands below from the repository root after checking out the branch.
The tested Python version is 3.14.4; dependencies are locked from that environment.
Preserved results can be read without installing the analysis environment.
Rerunning an experiment can replace tracked output files; retain the archived
baseline when comparing later changes.

```bash
git switch experiment/heart-monitor
sha256sum --check research/heart-monitor/SHA256SUMS
python3 -m venv .venv-heart
.venv-heart/bin/python -m pip install -r research/heart-monitor/requirements.lock.txt
python3 -m http.server 8873 --bind 127.0.0.1 --directory research/heart-monitor
```

Open `http://127.0.0.1:8873/experiment/report.html` for the latest report,
`/report.html` for the earlier session report, and `/s1/peak-analysis.html` or
`/s1/holdout-analysis.html` for earlier detector studies.

For local references, install a compiler with C++11 support. The original
SpO2 host runners rebuild automatically under ignored build space.

```bash
.venv-heart/bin/python research/heart-monitor/experiment/run.py
.venv-heart/bin/python research/heart-monitor/experiment/published_af.py --validate
```

The private Octave setup uses system Python with `python3-apt`, an existing apt
index, and `dpkg-deb`. It downloads and extracts packages without committing apt
changes, installing system packages, or using sudo. Package availability is
distribution-dependent; the saved package inventory describes the tested
Octave 11.1.0 environment, not a guarantee of identical future apt resolution.
Re-run setup after moving a checkout because its generated loader paths are
absolute. A supported system Octave may instead be integrated into the adapter.

```bash
/usr/bin/python3 research/heart-monitor/experiment/prepare_octave.py
.venv-heart/bin/python research/heart-monitor/experiment/references.py
.venv-heart/bin/python research/heart-monitor/experiment/references.py --local
.venv-heart/bin/python research/heart-monitor/experiment/published_af.py
.venv-heart/bin/python research/heart-monitor/experiment/report.py
```

For full public replay, download the two WFDB ZIPs from
[Zenodo record 6807403](https://zenodo.org/records/6807403), review its dataset
terms, and extract them under `build/temp/heart/experiment/`. The resulting
folders must be `mimic_perform_af_wfdb/` and `mimic_perform_non_af_wfdb/`.

| Archive | Published MD5 |
| --- | --- |
| `mimic_perform_af_wfdb.zip` | `4bafa5bbb74848e9a9f32028df7e26b1` |
| `mimic_perform_non_af_wfdb.zip` | `a80b8869dbd921a40ad445c1bc090e27` |

```bash
.venv-heart/bin/python research/heart-monitor/experiment/public_pilot.py
.venv-heart/bin/python research/heart-monitor/experiment/expanded.py
```

Restore the earlier detailed sweep CSVs with:

```bash
tar -xzf research/heart-monitor/s1/replay-sweeps.tar.gz -C research/heart-monitor/s1
```

Earlier analysis scripts and host probes are preserved research snapshots.
Their v1 baseline outputs predate the v2 firmware integration; running the
current production estimator does not recreate that old baseline. Review
`s1/analyze_peaks.py`, compile its candidate/baseline runners, and distinguish
historical evidence from new v2 results before rerunning those studies.

For host verification, configure/build the normal CMake suite, then select
`HeartCapture.*`. For a new firmware change, build only the preferred
`jc4880p433-heart` board unless another board is explicitly requested.

```bash
cmake -S . -B build/host-tests
cmake --build build/host-tests --target test_heart_capture --parallel
ctest --test-dir build/host-tests -R '^HeartCapture\.' --output-on-failure
PATH="$PWD/.venv-heart/bin:$PATH" ./build.sh jc4880p433-heart
```

The browser harness requires Playwright/Chromium, `pdftotext`, and generated
board-specific `src/app/web_assets.h`. Make Playwright resolvable through the
normal Node module lookup or `NODE_PATH`; it is not a firmware dependency.

```bash
node research/heart-monitor/verify-portal.cjs
```

## Next Steps

1. Pair disputed local recordings with ECG or adjudicated beat annotations,
   including genuinely slow pulses. Do not choose a detector from unlabelled
   counts alone.
2. Evaluate an MSPTDfast streaming/causal implementation offline, then profile
   bounded memory, latency, CPU cost, startup, and missed/extra beats before
   proposing a production replacement.
3. Define artifact admission independently of rhythm regularity. Existing BPM
   periodicity and reliability gates must not silently exclude AF recordings.
4. Add independently labelled motion, ectopy, and non-AF irregular controls;
   verify ECG references and segment labels; report subject-level uncertainty
   and evaluate an independent dataset before considering an AF alarm.
5. Obtain device-specific paired optical/reference data before claiming SpO2
   accuracy. Do not collect deliberately induced hypoxia.
6. Review licensing and patent constraints before deployment. The qppgfast
   wrapper/helpers include BSD/GPL provenance; AF feature code includes GPL
   notices and AFEv/BPcount carry explicit patent notices. Upstream notices are
   preserved in the archived source.

Pulsewatch's available model link redirects to sign-in; no verified SiamAF
weights URL was found. Their pretrained inference remains blocked, not
completed. Original MATLAB MSPTDfast/qppgfast and the accessible PhysioNet
pretrained SVM route were completed. Source URLs and hashes are in
`experiment/manifest.json`; source files remain unchanged except for our
separate host adapters and relocation paths.
