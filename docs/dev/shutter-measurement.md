---
title: Shutter measurement and session statistics
description: Engineering reference for pulse timing, validity, spatial gradients, curtain edges, serial CSV, and browser statistics.
---

User procedures and interpretation live in the
[Shutter Tester topics](../user/specialized/shutter-tester/README.md).
This reference owns implementation math rather than camera-operation advice.

## Source ownership

* [Measurement](../../src/app/device_classes/shutter_tester/shutter_measure.cpp):
  pulse timing, target matching, spatial aggregates, verdict, and serial rows.
* [Curtain statistics](../../src/app/device_classes/shutter_tester/shutter_curtain_stats.cpp):
  optical edge timing and validity.
* [Sessions](../../src/app/device_classes/shutter_tester/shutter_session.cpp):
  captured topology/geometry, waveform slices, persistence, and serialization.
* [Session portal](../../src/app/device_classes/shutter_tester/web/portal_shutter_sessions.js):
  speed grouping, presentation, scatter plot, and simulation.

## Per-sensor timing

Acquisition belongs to [ADC](../../src/app/device_classes/shutter_tester/shutter_adc.cpp)
and [capture coordination](../../src/app/device_classes/shutter_tester/shutter_capture.cpp).
Dark calibration derives a trigger threshold from the lowest sensor baseline
minus SHUTTER_TRIGGER_MARGIN, capped by the fixed threshold.
The pre-trigger ring supplies lookback context. Post-trigger samples define
the silence condition; post-capture samples extend visible trailing context.
SHUTTER_CAPTURE_MAX_SAMPLES caps each channel rather than promising a fixed
wall-clock duration.

Use measured per-sensor sample rate, not requested aggregate DMA frequency.
Startup calibration measures delivered sample sets; idle background calibration
can update that estimate. Topology/channel count and ADC throughput affect it.
Capture capabilities surface the live rate for consumers.

The fixed threshold identifies a coarse pulse. Baseline is the mean of up to
200 preceding samples; noise is their population RMS about that baseline.
The coarse region supplies minimum ADC and depth = baseline minus minimum.
Depth below SHUTTER_MIN_PULSE_DEPTH rejects the sensor.

The primary estimator fits each edge's linear 20-80% depth region, requiring
at least three qualifying samples, then solves its line at the 50% crossing.
Invalid/degenerate/reversed fits fall back to adaptive midpoint crossings with
two-sample interpolation. This avoids fitting clipped bottoms or noisy baselines;
it is not a universal correction for bad optics or amplifier response.

For falling samples x[s-1], x[s] and trailing x[e], x[e+1], midpoint h:

```text
start = (s-1) + (x[s-1]-h)/(x[s-1]-x[s])
end   = e + (h-x[e])/(x[e+1]-x[e])
duration_ms = (end-start)/sample_rate_hz * 1000
```

The slope estimator retains fractional duration while integer start/end fields
truncate crossing positions. Consumers must not reconstruct precise duration
solely from those integers.

Measurement processing additionally rejects durations below
SHUTTER_MIN_PULSE_DURATION_MS and buffer coverage above
SHUTTER_MAX_PULSE_COVERAGE. With multiple valid sensors, a duration ratio above
SHUTTER_MAX_SENSOR_RATIO suppresses the incoherent measurement.
Keep these gates distinct from the later exposure-accuracy verdict.

## Exposure aggregates and target

For valid sensor durations d and arithmetic mean D:

```text
sensor_spread_ms = max(d)-min(d)
sensor_spread_pct = (max(d)-min(d))/D * 100
deviation_pct = (D/nominal_ms-1) * 100
deviation_stops = log2(D/nominal_ms)
```

Uncontrolled matching minimizes absolute log duration ratio against the
standard-speed table. A locked target overrides matching.
Verdict uses absolute deviation only, with warning above approximately one-third
stop and failure above one-half. Secondary diagnostic color thresholds do not
change it.

## Spatial capping and skew

A line array uses abs(log2(max(d)/min(d))) divided by measured diagonal.
Unavailable gradients use a negative sentinel and are omitted from binding/
session output as appropriate.

Four-corner order is TL, TR, BL, BR. With all channels valid, derive left/right
and top/bottom mean exposures and positive physical column/row spans:

```text
gx = abs(log2(right_mean/left_mean))/column_span_mm
gy = abs(log2(bottom_mean/top_mean))/row_span_mm
gradient_magnitude = sqrt(gx*gx + gy*gy)
```

The portal averages available X/Y gradients across shots, then extrapolates
sqrt((avg_gx*36)^2 + (avg_gy*24)^2) stops over a 36x24 mm gate.
Without 2D values it uses average line gradient times 43.27 mm.
This is extrapolation, not a full-frame measurement.

Arrival spreads determine vertical/horizontal/leaf travel classification.
Vertical cross-axis pairs are BR-BL and TR-TL; horizontal pairs are BL-TL and
BR-TR. Opening and closing edges each have per-pair skew in microseconds.
The differential summary is opening bottom-row minus top-row skew, normalized
by column span in microseconds/mm. It is distinct from each raw skew field.
Sampling/channel offsets can imitate mechanical tilt.

## Curtain edge statistics

Using caller-supplied pre-pulse baseline and minimum, locate 10/50/90% optical
crossings. Convert interpolated positions through sample rate:

```text
curtain1_ms = (open90-open10)/rate * 1000
dwell_ms = max(0, close90-open90)/rate * 1000
curtain2_ms = (close10-close90)/rate * 1000
curtain_ratio = curtain2_ms/curtain1_ms
```

edges_detected means crossings exist. valid additionally rejects full-open
regimes and recovery tails. Gate constants belong to
[curtain statistics](../../src/app/device_classes/shutter_tester/shutter_curtain_stats.h).
Durations can be serialized diagnostically when a ratio is physically invalid;
browser aggregates must exclude invalid ratios.
Fractional chart positions divide by stored slice length minus one, not by the
full capture-buffer length.

## Browser aggregates and plots

At each nominal speed, Shot spread is peak-to-peak shot-average duration divided
by its mean, requiring two shots.
Repeatability is population standard deviation divided by mean duration times
100, shown at three or more shots.
The row verdict uses mean signed stop deviation, then its absolute magnitude;
it is not worst-shot classification.

Curtain columns average physically valid sensor edges. Balance divides the
mean second edge by mean first edge; the optional delta summarizes within-shot
ratio ranges, not the same statistic.
Speed trend regresses mean stop deviation against log2 nominal milliseconds;
its displayed sign is inverted.

Scatter X is log2(1 + deviation_pct/100), with nominal at zero.
Average diamonds apply that transform to mean percentage deviation.
The ideal-window width scales actual chart span by nominal/measured duration.
Its center uses a valid dwell midpoint when available, otherwise waveform
minimum. The illustrative sweep is not part of measurement or persistence.

## Serial analysis

The measurement exporter is
[monitor_meas.sh](../../tools/shutter-tester/monitor_meas.sh).
Its positional inputs are optional PORT and BAUD (default 115200), with optional
`--raw`. It filters `[MEAS]` rows into timestamped CSV and optionally retains raw
serial output beside the script. Stop with Ctrl+C.

The current script sources a sibling config.sh, but the shared helper is at
the repository root. Its bootstrap path needs correction before that relocated
script can run standalone. There is no root-level measurement exporter.
See [script usage](scripts.md) for the regular serial monitor.
Header definitions in the exporter and row generation in the measurement source
must change together.

The leading fields identify sequence, preset/capture/time, target/lock,
average/deviation, sensor spread/verdict, active/valid counts, X/Y capping,
differential skew, travel classification, and per-curtain pair skews.
Each sensor repeats duration, minimum, baseline, noise RMS, depth, SNR,
threshold, start, end, and sample count.
SNR is 20*log10(depth/noise); undefined cases and invalid sensor slots are empty.
Check the emitted header rather than assuming a fixed sensor count.
An SNR number or pass verdict alone does not certify the measurement setup.
