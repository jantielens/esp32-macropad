---
title: Instrument REST and persistence interfaces
description: Narrow reference for Coffee Scale and Darkroom Timer APIs, record ownership, and destructive operations.
---

Follow [portal routing/auth/error conventions](web-portal.md).
User workflows live in [Coffee Scale](../user/specialized/coffee-scale/README.md)
and [Darkroom Timer](../user/specialized/darkroom-timer/README.md).
These routes are present only for the selected class/capabilities.

## Coffee Scale

| Method   | Route                               | Contract                                 |
| -------- | ----------------------------------- | ---------------------------------------- |
| GET      | `/api/scale`                        | Weight/flow/calibration/smoothing status |
| POST     | `/api/scale/tare`                   | Tare                                     |
| POST     | `/api/scale/calibrate`              | JSON known_weight_g reference            |
| GET      | `/api/brews`                        | Record list                              |
| GET      | `/api/brews?id=N`                   | Full record including series             |
| POST     | `/api/brews/import`                 | One record object or record array        |
| DELETE   | `/api/brews?id=N`                   | Delete one                               |
| DELETE   | `/api/brews`                        | Clear history                            |
| GET      | `/api/brew-templates`               | Registry list                            |
| GET      | `/api/brew-templates/get?name=NAME` | Template JSON                            |
| POST     | `/api/brew-templates`               | Create/update from template JSON         |
| DELETE   | `/api/brew-templates?name=NAME`     | Remove custom file/override              |

The [class web handlers](../../src/app/device_classes/coffee_scale/web) own
body validation and response shapes.
The [brew engine](../../src/app/device_classes/coffee_scale/brew/brew_manager.cpp)
owns sampled series/captures/markers.
Template reload stops active work before replacing cached template storage.
The [user-authored template language](../user/specialized/coffee-scale/templates.md)
is distinct from record serialization and persisted pad configuration.

## Darkroom Timer

| Method   | Route                      | Contract                             |
| -------- | -------------------------- | ------------------------------------ |
| GET      | `/api/relay`               | Four action slots                    |
| PUT      | `/api/relay`               | Replace/validate relay configuration |
| GET      | `/api/prints`              | Newest-first record metadata         |
| GET      | `/api/prints?id=ID`        | Full record/segments                 |
| PUT      | `/api/prints?id=ID`        | Mutable notes/starred fields         |
| DELETE   | `/api/prints?id=ID`        | Delete one                           |
| DELETE   | `/api/prints?confirm=true` | Clear history                        |
| GET      | `/api/prints/export`       | Full JSON array download             |

Relay routing uses a top-level namespace because `/api/config` has prefix
matching. The [relay handler](../../src/app/device_classes/darkroom_timer/web_portal_relay.cpp)
and [print handler](../../src/app/device_classes/darkroom_timer/web_portal_prints.cpp)
own validation, authentication, and persistence error handling.

[Print log](../../src/app/device_classes/darkroom_timer/print_log.cpp) uses the
Storage facade under `/prints/` and a bounded FIFO.
IDs use daily sequence numbering with a monotonic fallback.
Bodies store exposure/strip parameters, metering context, notes, and star state.
NVS counters/date/sequence are internal bookkeeping, not user configuration.
Delete must update body/index/counters consistently; do not assume a star
prevents eviction or expose internal file writes as a supported editing API.

## Other instrument contracts

[Shutter sessions](../../src/app/device_classes/shutter_tester/shutter_session.cpp)
and [measurement math](shutter-measurement.md) own capture serialization,
waveform/geometry snapshots, asynchronous persistence, and serial CSV.
The [MCP authoring contract](mcp-authoring.md) distinguishes queued operations
from applied or durable results. Do not infer success from a lifecycle popup.
