import json
import io
import subprocess
import sys
from pathlib import Path

import numpy as np
import pandas as pd
from scipy import signal
import plotly.graph_objects as go
from plotly.subplots import make_subplots

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/heart-monitor'))
from heart_analysis import load_capture
from replay import replay_capture

OUTPUT = Path(__file__).resolve().parent


def reliable_mask(replay):
    return ((replay.state == 'usable') & (replay.pulse_valid == 1)
            & ((replay.quality_flags.astype(int) & 6911) == 0)
            & replay.bpm.between(30, 180) & (replay.supporting_samples >= 1000)
            & (replay.correlation >= .65) & (replay.correlation_distinctness >= .15)
            & (replay.red_bpm > 0) & ((replay.red_bpm / replay.bpm - 1).abs() <= .15)
            & ((replay.method == 'AUTOCORRELATION')
               | ((replay.method == 'PEAKS') & (replay.interval_dispersion <= .15))))


def causal_trial(ir, red, detector, lowpass, highpass=.5):
    inputs = '100 10 2 1 30 180 .35 2\n'
    inputs += ''.join(f'{int(first)} {int(second)}\n' for first, second in zip(ir, red))
    result = subprocess.run([str(OUTPUT / f'replay-{detector}'), str(highpass), str(lowpass)],
                            input=inputs, text=True, capture_output=True, check=True)
    return pd.read_csv(io.StringIO(result.stdout))


def causal_metrics(replay):
    usable = replay.state == 'usable'
    reliable = reliable_mask(replay)
    updates = replay.loc[replay.updated_ms.diff().fillna(0) > 0]
    evidence = updates.loc[updates.correlation >= .45]
    stable_agreement = ((evidence.peak_bpm > 0)
                        & ((evidence.peak_bpm / evidence.candidate_bpm - 1).abs() <= .15)
                        & (evidence.interval_dispersion <= .15))
    return dict(usable_s=usable.sum() / 100, reliable_s=reliable.sum() / 100,
                median_bpm=replay.loc[usable, 'bpm'].median(),
                min_bpm=replay.loc[usable, 'bpm'].min(), max_bpm=replay.loc[usable, 'bpm'].max(),
                peak_method_s=((replay.method == 'PEAKS') & usable).sum() / 100,
                stable_agreed_windows=int(stable_agreement.sum()),
                evidence_windows=len(evidence))


def causal_session():
    trials = []
    configurations = [('baseline', .5, 4), ('candidate', .5, 4),
                      ('baseline', .5, 3), ('candidate', .5, 3),
                      ('baseline', .5, 2), ('candidate', .5, 2),
                      ('baseline', .7, 3), ('candidate', .7, 3)]
    paths = sorted(OUTPUT.parent.glob('capture-*.csv'))
    paths += sorted(OUTPUT.glob('capture-*.csv'))
    for path in paths:
        capture = load_capture(path)
        if capture.metadata.get('capture_status') != 'complete':
            continue
        for detector, highpass, lowpass in configurations:
            replay = causal_trial(capture.samples.ir, capture.samples.red, detector, lowpass, highpass)
            replay.to_csv(OUTPUT / f'causal-{detector}-{highpass}-{lowpass}-{path.stem}.csv', index=False)
            trials.append(dict(capture=path.stem, detector=detector, highpass=highpass, lowpass=lowpass,
                               **causal_metrics(replay)))
    real = pd.DataFrame(trials)
    real.to_csv(OUTPUT / 'causal-comparison.csv', index=False)
    print('CAUSAL REAL-CAPTURE COMPARISON')
    print(real.loc[real.lowpass == 4].round(2).to_string(index=False))
    time = np.arange(4500) / 100
    rng = np.random.default_rng(20261013)
    cases = []
    for bpm in [30, 36, 46, 60, 90, 150, 180]:
        for shape in ['sine', 'downward_pulse', 'upward_pulse']:
            phase = (time * bpm / 60) % 1
            if shape == 'sine':
                wave = 10 * np.sin(2 * np.pi * phase)
            else:
                wave = -100 * np.exp(-0.5 * ((phase - .45) / .08) ** 2)
                wave += 10 * np.sin(6 * np.pi * phase)
                if shape == 'upward_pulse':
                    wave = -wave
            cases.append((f'{shape}_{bpm}', bpm, 110000 + wave, 90000 + .6 * wave))
    cases.extend([
        ('flat', None, np.full(len(time), 110000), np.full(len(time), 90000)),
        ('independent_noise', None, 110000 + rng.normal(0, 20, len(time)),
         90000 + rng.normal(0, 15, len(time))),
        ('slow_drift', None, 110000 + 300 * np.sin(2 * np.pi * .12 * time) + rng.normal(0, 4, len(time)),
         90000 + 200 * np.sin(2 * np.pi * .12 * time) + rng.normal(0, 4, len(time))),
        ('isolated_steps', None, 110000 + 2000 * (time > 15) - 1000 * (time > 25),
         90000 + 1000 * (time > 15) - 500 * (time > 25)),
        ('no_contact', None, np.full(len(time), 1000), np.full(len(time), 1000)),
        ('clipped', None, np.full(len(time), 262143), np.full(len(time), 262143)),
        ('above_range_240', None, 110000 + 100 * np.sin(2 * np.pi * 4 * time),
         90000 + 60 * np.sin(2 * np.pi * 4 * time)),
    ])
    for seed in [11, 22, 33]:
        noise = np.random.default_rng(seed).normal(0, 30, len(time))
        common = signal.sosfilt(signal.butter(2, 4, fs=100, output='sos'), noise)
        cases.append((f'common_noise_{seed}', None, 110000 + common, 90000 + .8 * common))
    controls = []
    for name, expected, ir, red in cases:
        for detector, highpass, lowpass in configurations:
            replay = causal_trial(ir, red, detector, lowpass, highpass)
            metrics = causal_metrics(replay)
            metrics['error_p95'] = ((replay.loc[replay.state == 'usable', 'bpm'] - expected).abs().quantile(.95)
                                    if expected is not None else np.nan)
            controls.append(dict(signal=name, expected_bpm=expected, detector=detector,
                                 highpass=highpass, lowpass=lowpass, **metrics))
    control = pd.DataFrame(controls)
    control.to_csv(OUTPUT / 'causal-controls.csv', index=False)
    print('CONTROL SUMMARY')
    for (detector, highpass, lowpass), group in control.groupby(['detector', 'highpass', 'lowpass']):
        positive = group.loc[group.expected_bpm.notna()]
        negative = group.loc[group.expected_bpm.isna()]
        print(detector, 'filter', highpass, lowpass, 'positive cases with readings',
              int((positive.usable_s > 0).sum()), '/', len(positive),
              'worst_p95_error', round(positive.error_p95.max(), 2),
              'negative_usable_s', round(negative.usable_s.sum(), 2),
              'negative_reliable_s', round(negative.reliable_s.sum(), 2))
    issues = control.loc[(control.expected_bpm.notna() & ((control.usable_s == 0) | (control.error_p95 > 3)))
                         | (control.expected_bpm.isna() & (control.usable_s > 0))]
    print('CONTROL LIMITATIONS')
    print(issues.round(2).to_string(index=False))


def window_peaks(waveform, mode):
    if mode.startswith('inverted'):
        waveform = -waveform
    amplitude = np.mean(np.abs(waveform))
    slope = np.mean(np.abs(np.diff(waveform)))
    threshold = max(4, amplitude * 0.9, slope * 4)
    options = dict(distance=30, prominence=threshold, height=0)
    if mode.endswith('dominant'):
        options['prominence'] = max(4, 2 * np.std(waveform))
        options['height'] = np.std(waveform)
    if mode.endswith('width'):
        options['width'] = 100 / (2 * 4)
    peaks, properties = signal.find_peaks(waveform, **options)
    intervals = np.diff(peaks) * 10
    intervals = intervals[(intervals >= 330) & (intervals <= 2000)]
    if len(intervals) < 4:
        return peaks, 0, 1
    median = np.median(intervals)
    return peaks, 60000 / median, np.mean(np.abs(intervals - median)) / median


def analyze(capture, replay):
    rows = []
    updates = replay.loc[replay.updated_ms.diff().fillna(0) > 0]
    for _, update in updates.iterrows():
        end = int(round(update.window_end_ms / 10)) + 1
        start = end - int(update.supporting_samples)
        waveform = replay.waveform.iloc[start:end].to_numpy()
        if len(waveform) != 1000 or not np.isfinite(waveform).all():
            continue
        for mode in ['production', 'full_prominence', 'full_prominence_width',
                     'inverted_prominence', 'inverted_prominence_width',
                     'positive_dominant', 'inverted_dominant']:
            if mode == 'production':
                bpm, dispersion = update.peak_bpm, update.interval_dispersion
                count = int(replay.beat_detected.iloc[start:end].sum())
            else:
                peaks, bpm, dispersion = window_peaks(waveform, mode)
                count = len(peaks)
            reference = update.candidate_bpm
            agreed = bpm > 0 and np.isfinite(reference) and reference > 0 and abs(bpm / reference - 1) <= .15
            rows.append(dict(capture=capture.path.stem, time_s=update.time_s,
                             mode=mode, peak_bpm=bpm, dispersion=dispersion,
                             count=count, autocorrelation_bpm=reference,
                             correlation=update.correlation,
                             agrees=agreed, stable=dispersion <= .15))
    return pd.DataFrame(rows)


def main():
    results = []
    captures = sorted(OUTPUT.parent.glob('capture-*.csv'))
    captures += sorted(OUTPUT.glob('capture-*.csv'))
    for path in captures:
        capture = load_capture(path)
        if capture.metadata.get('capture_status') != 'complete':
            continue
        replay = replay_capture(capture)
        replay.to_csv(OUTPUT / f'baseline-{path.stem}.csv', index=False)
        result = analyze(capture, replay)
        results.append(result)
        if path.stem != 'capture-657132c2':
            continue
        figure = make_subplots(rows=3, cols=1, shared_xaxes=True,
                               subplot_titles=['Unchanged causal IR waveform and detected peaks',
                               'Causal peak rates versus autocorrelation',
                               'Unchanged reliability threshold'])
        segment = replay.loc[replay.time_s.between(45, 85)]
        figure.add_trace(go.Scatter(x=segment.time_s, y=segment.waveform,
                                   name='Causal IR', line=dict(color='#333333')), row=1, col=1)
        beats = segment.loc[segment.beat_detected == 1]
        positions = np.rint(beats.beat_timestamp_ms / 10).astype(int)
        figure.add_trace(go.Scatter(x=positions / 100, y=replay.waveform.iloc[positions],
                                   mode='markers', name='Production peaks',
                                   marker=dict(color='#c34735', symbol='x', size=9)), row=1, col=1)
        candidate = pd.read_csv(OUTPUT / 'causal-candidate-0.5-4-capture-657132c2.csv')
        candidate_segment = candidate.loc[candidate.time_s.between(45, 85)]
        candidate_beats = candidate_segment.loc[candidate_segment.beat_detected == 1]
        candidate_positions = np.rint(candidate_beats.beat_timestamp_ms / 10).astype(int)
        figure.add_trace(go.Scatter(x=candidate_positions / 100,
                       y=replay.waveform.iloc[candidate_positions], mode='markers',
                       name='Causal candidate peaks',
                       marker=dict(color='#137c70', symbol='circle-open', size=11)), row=1, col=1)
        for selected, label, color in [(segment, 'Production peak BPM', '#c34735'),
                           (candidate_segment, 'Causal candidate peak BPM', '#137c70')]:
            figure.add_trace(go.Scatter(x=selected.time_s, y=selected.peak_bpm.where(selected.peak_bpm > 0),
                           name=label, line=dict(color=color)), row=2, col=1)
        figure.add_trace(go.Scatter(x=segment.time_s, y=segment.candidate_bpm,
                                   name='Autocorrelation BPM', line=dict(color='#111111')), row=2, col=1)
        for selected, label, color in [(segment, 'Production correlation', '#666666'),
                           (candidate_segment, 'Candidate correlation', '#137c70')]:
            figure.add_trace(go.Scatter(x=selected.time_s, y=selected.correlation,
                           name=label, line=dict(color=color)), row=3, col=1)
        figure.add_hline(y=.65, line_dash='dash', line_color='#c34735',
                 annotation_text='Report reliability minimum 0.65', row=3, col=1)
        figure.update_layout(height=1050, title='657132c2: causal detector study, firmware unchanged',
                             template='plotly_white', legend=dict(orientation='h'), hovermode='x unified')
        figure.update_xaxes(title_text='Nominal seconds', row=3, col=1)
        figure.update_yaxes(title_text='Filtered ADC counts', row=1, col=1)
        figure.update_yaxes(title_text='BPM', row=2, col=1)
        figure.update_yaxes(title_text='Autocorrelation', range=[0, 1], row=3, col=1)
        figure.write_html(OUTPUT / 'peak-analysis.html', include_plotlyjs=True)
    combined = pd.concat(results, ignore_index=True)
    combined.to_csv(OUTPUT / 'peak-analysis.csv', index=False)
    print('WINDOW EVIDENCE (agreement uses autocorrelation, not verified heartbeat truth)')
    for (capture, mode), group in combined.groupby(['capture', 'mode']):
        eligible = group.loc[group.correlation >= .45]
        good = eligible.loc[eligible.agrees & eligible.stable]
        print(capture, mode, 'windows', len(eligible), 'stable+agree', len(good),
              'median peak BPM', round(eligible.peak_bpm.replace(0, np.nan).median(), 2))
    recent = combined.loc[(combined.capture == 'capture-657132c2') & combined.time_s.between(51, 104)]
    print('TARGET AFTER 51s')
    for mode, group in recent.groupby('mode'):
        print(mode, 'windows', len(group), 'stable+agree', int((group.agrees & group.stable).sum()),
              'median', round(group.peak_bpm.replace(0, np.nan).median(), 2))


if __name__ == '__main__':
    if '--causal' in sys.argv:
        causal_session()
    else:
        main()