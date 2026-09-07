---
title: Flight Radar Extension
description: Configure the Flight Radar native Extension for nearby ADS-B aircraft
ms.date: 2026-09-07
ms.topic: how-to
---

## Overview

Flight Radar is a native Extension for supported ESP32-P4 and ESP32-S3 boards.
It displays aircraft detected near a configured latitude and longitude, using
the public ADS-B data service at ADSB.lol. Aircraft markers point in their
reported direction of travel; labels show callsign, barometric altitude, and
aircraft type. The footer shows the data source status, displayed aircraft
count, and duration of the most recent request.

The Extension requires an active internet connection. It requests data from
`https://api.adsb.lol` and does not require an API key.

## Install and add the widget

1. Build and install the `flight-radar` package from the device portal's
   **Extensions** page.
2. Add or edit a pad button.
3. Choose the **Extension** widget and select **Flight Radar**.
4. Enter the Extension configuration JSON described below.

## Configuration

The default location is Brussels Airport. Use your location and the desired
search range for useful results.

```json
{
  "lat": 50.901389,
  "lon": 4.484444,
  "range_km": 25,
  "max_planes": 20,
  "interval": 5
}
```

| Field | Default | Range | Description |
| --- | ---: | ---: | --- |
| `lat` | `50.901389` | -90 to 90 | Centre latitude in decimal degrees |
| `lon` | `4.484444` | -180 to 180 | Centre longitude in decimal degrees |
| `range_km` | `50` | 1 to 463 | Radar radius in kilometres. `range` is accepted as an alias. |
| `max_planes` | `20` | 1 to 100 | Maximum nearest aircraft to display and retain in the snapshot |
| `interval` | `10` | 1 to 3600 | Seconds between ADS-B requests for this configuration |

Invalid values prevent the widget from loading. An empty configuration uses
all defaults.

## Operation and limits

The Extension queries ADSB.lol with a radius rounded up to nautical miles,
then filters aircraft using their exact calculated distance in kilometres.
Only aircraft with reported latitude and longitude can appear on the radar.

One Extension package supports up to four displayed Flight Radar widgets and
up to four unique scan configurations. Widgets with identical configuration
share one request stream and snapshot. A fifth widget, or a fifth unique
configuration, cannot be created. Requests for distinct configurations run
sequentially to keep TLS and network memory use bounded.

The radar needs at least a 32 by 32 pixel button. A square or landscape button
provides more room for the radar rings and aircraft labels. At most eight
aircraft labels are shown, although every retained aircraft is represented by a
marker.

When no aircraft are within range, the footer reports `No aircraft in range`.
Request and parse failures are also shown there. Lower `max_planes`, shorten
the range, or increase `interval` when using several distinct radar views.

## Build from source

Build and sign every native Extension package, including Flight Radar:

```bash
./tools/build-p4-extensions.sh
```

The command requires an extension signing key at
`.secrets/extension-signing-private.pem`, or the `EXTENSION_SIGNING_KEY`
environment variable. It produces ESP32-P4 and ESP32-S3 packages in
`build/extensions`.