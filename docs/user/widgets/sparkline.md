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
Set Time range from 10 seconds to 7 days and One point every at least 100 ms.
The editor derives 2-1024 points and shows the actual interval.
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
History entity beside each desired live source.
Choose Average reading, Latest state, or Accumulated total for the statistic
appropriate to that entity. A history source still needs a live binding.
Save Pad and wait for clock synchronization/backfill.

Backfill runs once per stream, one stream at a time.
HA records five-minute statistics; finer historical grids interpolate coarse
values. Windows over 24 hours use hourly statistics.
Live samples take precedence. Gaps can carry earlier values forward.
This is not a claim of original sub-minute historical precision.

Clear the entity to disable backfill; after save/reload its statistic resets
to default. Clearing a source differs from hiding a marker, which retains style.
Check HA statistics availability if the historical part remains empty.

[Widget chooser](README.md) | [User guides](../README.md)
