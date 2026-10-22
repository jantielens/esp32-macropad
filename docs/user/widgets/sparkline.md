---
title: Sparkline history
description: Configure sampled numeric history, multi-line scales, markers, reference lines, and optional Home Assistant backfill.
---

A Sparkline shows how up to three numeric readings change over time. Unlike a
gauge's current value, its sampled history reveals trends and fluctuations;
the chart is runtime history rather than a permanent measurement log.

## Collect a trend

Choose Sparkline in Button Content. Add a numeric live binding, such as
`[health:cpu]`, and optionally two more lines.
Set Time range from 10 seconds to 30 days and One point every at least 100 ms.
The editor derives 2-1024 points and shows the actual interval. At 30 days,
the point limit means an interval of about 42 minutes.
Invalid combinations block saving rather than silently changing your request.
Save Pad and allow time for samples.

Collection continues for configured streams when their pad is not visible.
It is runtime history, not a durable log. Missing new data carries the last
observation forward, so a flat line does not prove fresh sensor messages.

## Choose the scale and annotations

Leave Y Min/Max empty for automatic observed range, or set numeric/bindable limits.
Same scale for all lines defaults on; disable it only to compare shapes with
different magnitudes. Explicit limits override automatic independent scaling.

Line width 1-10 px, smoothing 0-8, and separation 0-6 px affect presentation.
Smoothing also moves extrema markers/current dot to the smoothed curve.
Separation shifts pixels away from exact values; leave it 0 for precise readout.

Min/Max marker size 0 disables it; 1-20 px shows a dot.
An optional marker format permits one floating `%f`/`%e`/`%g` conversion,
such as `hi %.1f`. Empty means dot only.
White label color means inherit the line color.

Current-value labels/dots appear at the right edge.
Label width 0 uses the default area; 1-200 px sets it explicitly.
Up to three fixed numeric reference Y values can have solid/dotted/dashed lines.
Keep reference lines in view expands automatic scale; explicit limits win.
Open a color swatch and use fx inside its dialog to bind the color.
Y reference values are literal numbers.

## Backfill from Home Assistant

On supported PSRAM firmware, configure
[Home Assistant URL/token](../integrations/home-assistant.md), then set a
History entity beside each desired live source. The device requests recorded
entity states first, so changes can appear at finer resolution than long-term
statistics. A history source still needs a live binding. Save Pad and wait for
clock synchronization/backfill.

Set **Raw history limit (hours)** in **Sampling** to control how much recent raw
data is requested for all lines. Use a whole number from 0 to 720:

* **0** uses the full chart time range, subject to Recorder retention and
  response-size limits. This is the default for existing configurations.
* **A positive value** requests only the newest specified hours, capped at the
  chart time range. Older data uses statistics when available.

For example, a 30-day chart with a limit of 24 requests one day of raw history
and fills the older prefix with statistics. If Recorder retains less raw data,
the statistics prefix extends to the actual raw boundary. Entities without mean
statistics leave the older prefix empty. This setting affects backfill only,
not ongoing live collection. Save Pad to apply a changed limit and reload history.

Backfill runs one stream at a time. When raw state history does not cover the
older part of the selected window, the device can fill that older prefix from
Home Assistant's five-minute `mean` statistics; windows over 24 hours use
hourly statistics. Statistics do not overwrite raw-covered slots or fill gaps
inside raw coverage. Only the `mean` statistic is used; there is no per-line
statistic selector. Each statistical mean covers its own five-minute or hourly
period. Missing statistics remain gaps, including at the join to raw history;
the device does not extend an old mean through missing periods.

If one source temporarily fails, usable history can appear first while the
device retries with backoff, up to three attempts per stream. A recovered
response can repair previously backfilled slots without replacing live samples.
After the retry limit, the chart retains usable history and leaves unresolved
parts empty. Successfully empty history does not trigger continuous polling.

The chart preserves unavailable and missing-history gaps instead of drawing a
line across them. Locally collected streams may carry their last observation
forward between samples; this does not mean that Home Assistant has a new
reading. Live samples remain authoritative over history. Raw responses and
individual records have fixed size limits. If a raw response exceeds a limit,
its partial samples are discarded and the device relies on Recorder `mean`
statistics for the full window where available; otherwise that backfill remains
empty. Results also depend on the history retained and made available by
Home Assistant Recorder.

Clear the entity to disable backfill. Clearing a source differs from hiding a
marker, which retains style. If the older part remains empty, check Recorder
retention and whether Home Assistant exposes a `mean` statistic for the entity.
Saved configurations from earlier versions that contain a per-line statistic
selection may need to be re-created; statistic-selection migration is not
provided.

[Widget chooser](README.md) | [User guides](../README.md)
