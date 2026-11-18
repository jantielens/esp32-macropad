---
title: Action operation migration ledger
description: Inventory of legacy action owners and operation migration status.
---

## Phase 2: built-in operations

This is a source inventory, not a release or hardware certification. Operations
are capability-gated with their owners. Each listed argument accepts a literal
or a dynamic template; enum values are exact strings and booleans normalize
only `true` and `false`. Required arguments remain required even where their
literal value can be empty. A rejected operation stops its typed suffix.

Verification labels below describe **available test source**, not a claim that
the test was run during this reconciliation. `Direct` calls the production
handler with subsystem doubles; it does not by itself prove argument
normalization. `Normalized` exercises production parsing/resolution and
dispatch. `Pending` identifies missing evidence, not necessarily a known
failure. All physical hardware verification remains pending.

| Legacy owner | Operation or command family | Typed arguments | Actual completion | Verification status |
|---|---|---|---|---|
| `screen` | `screen.open` | `target`: screen ID | Screen manager accepts the navigation intent; rendering follows later. | Normalized: `test_runtime_variable_actions.cpp`; hardware pending. |
| `back` | `screen.back` | None | Back intent attempted; empty history is a logged successful no-op. | Direct: `test_runtime_variable_actions.cpp`; hardware pending. |
| `cycle_pad` | `pad.cycle` | `direction`: `next`/`previous`; `wrap`: boolean; `excluded_pads`: empty or comma-separated pad numbers | Eligible destination selected and navigation queued; no destination fails. | Parsing/catalog and exclusions: `test_action_parse`; normalized dispatch, dynamic rejection and suffix stopping with a display double: `test_runtime_variable_actions_display`. Hardware pending. |
| `button_nav` | `button.navigate` | `command`: `previous`/`next`/`tap`/`tap_hold` | Display-task navigation returns success; stale dialog generation or rejected navigation fails. | Direct: `test_list_navigation_action`; normalized dispatch, dynamic rejection and suffix stopping with a display double: `test_runtime_variable_actions_display`. Hardware pending. |
| `list_nav` | `list.navigate` | `target`: List ID; `command`: `previous_page`/`next_page` | Active pad and List accept the display-task request; stale input, pad, or screen generation fails. | Direct: `test_list_navigation_action`; normalized dispatch, dynamic rejection and suffix stopping with a display double: `test_runtime_variable_actions_display`. Hardware pending. |
| `timer` | `timer.adjust` | `timer`: integer 1–3; `seconds`: signed integer | Timer command succeeds. | Normalized: `test_runtime_variable_actions.cpp`; hardware pending. |
| `timer` | `timer.set` | `timer`: integer 1–3; `seconds`: integer 0–4294967 | Timer command succeeds. | Normalized: `test_runtime_variable_actions.cpp`; hardware pending. |
| `timer` | `timer.start_up`, `timer.toggle_up`, `timer.stop`, `timer.pause`, `timer.resume`, `timer.reset` | `timer`: integer 1–3 | Timer command succeeds. | Normalized: `test_runtime_variable_actions.cpp`; hardware pending. |
| `timer` | `timer.start_down`, `timer.toggle_down` | `timer`: integer 1–3; `seconds`: integer 1–4294967 | Timer command succeeds. | Normalized: `test_runtime_variable_actions.cpp`; hardware pending. |
| `alarm` | `alarm.cancel`, `alarm.snooze` | `alarm`: enum `0` (active) or `1` | Alarm command accepted into the manager queue, not yet applied. | Normalized runner to queue double: `test_runtime_variable_actions.cpp`; manager queue: `test_alarm_manager.cpp`; hardware pending. |
| `alarm` | `alarm.set_time`, `alarm.adjust_minutes` | `minutes`: integer 0–1439 for set; signed integer for adjust | Command queued, not yet applied. | Normalized runner to queue double: `test_runtime_variable_actions.cpp`; manager queue: `test_alarm_manager.cpp`; hardware pending. |
| `alarm` | `alarm.enable`, `alarm.disable`, `alarm.toggle` | None | Command queued, not yet applied. | Normalized runner to queue double: `test_runtime_variable_actions.cpp`; manager queue: `test_alarm_manager.cpp`; hardware pending. |
| `alarm` | `alarm.weekday_enable`, `alarm.weekday_disable`, `alarm.weekday_toggle` | `day`: enum `0`–`6`, Sunday–Saturday | Command queued, not yet applied. | Normalized runner to queue double: `test_runtime_variable_actions.cpp`; manager queue: `test_alarm_manager.cpp`; hardware pending. |
| `system` | `wifi.station.set_enabled`, `wifi.ap.set_enabled` | `enabled`: boolean | Network request admitted; application and connection occur later. | Normalized: `test_runtime_variable_actions.cpp`; hardware pending. |
| `system` | `wifi.radio.turn_off`, `device.restart_setup_ap` | None | Radio-off or main-loop setup-AP restart request admitted; transition happens later. | Direct/normalized request and rejection: `test_runtime_variable_actions.cpp`; hardware pending. |
| `system` | `device.restart`, `display.sleep`, `display.wake` | None | Reboot or screensaver manager called; not proof of completed restart or panel transition. | Direct/normalized: `test_runtime_variable_actions.cpp`; hardware pending. |
| `delay` | `delay.wait` | `duration_ms`: integer 1–55000 | Pauses until timer completes, then resumes the captured suffix. | Normalized and continuation: `test_runtime_variable_actions.cpp`; hardware pending. |
| `set_var` | `variable.set` | `name`: variable-name string; `value`: string, may be empty | Variable is stored or unchanged; resolution and assignment are transactional. | Normalized and concurrent update: `test_runtime_variable_actions.cpp`; hardware pending. |
| `brightness` | `display.brightness.set`, `display.brightness.adjust` | `percent`: integer 0–100 for set, signed integer for adjust | Brightness setter called with clamped result. | Normalized: `test_runtime_variable_actions.cpp`; hardware pending. |
| `save_brightness` | `display.brightness.save` | None | Settings save succeeds. | Direct success/failure: `test_runtime_variable_actions.cpp`; hardware pending. |
| `volume` | `audio.volume.set`, `audio.volume.adjust` | `percent`: integer 0–100 for set, signed integer for adjust | Volume setter called with clamped result. | Normalized: `test_runtime_variable_actions.cpp`; hardware pending. |
| `save_volume` | `audio.volume.save` | None | Settings save succeeds. | Direct success/failure: `test_runtime_variable_actions.cpp`; hardware pending. |
| `music` | `music.play_pause`, `music.next`, `music.previous`, `music.stop` | None | Audio worker accepts transport request, not playback completion. | Normalized: `test_runtime_variable_actions.cpp`; hardware pending. |
| `sound_alert` | `audio.alert.tone`, `audio.alert.mp3` | `pattern`: string, may be empty, or `file`: nonempty string; `volume`: integer 0–100 | Audio worker accepts one-shot request, not playback completion. MP3 requires the sound player. | Normalized runner to audio double: `test_runtime_variable_actions.cpp`; worker: `test_audio_worker.cpp`; hardware pending. |
| `alarm_tone`, `alarm_mp3` | `alarm.tone`, `alarm.mp3` | `pattern` or `file` and `volume`, as above | Audio worker accepts loop request; not completion of the loop. | Normalized runner to audio double: `test_runtime_variable_actions.cpp`; worker: `test_audio_worker.cpp`; hardware pending. |
| `sound_alert` | `audio.alert.stop`, `audio.stop_all` | None | Stop function called; guarded alert stop is scoped to the current work owner. | Normalized runner to audio double: `test_runtime_variable_actions.cpp`; worker: `test_audio_worker.cpp`; hardware pending. |
| `key` | `keyboard.send` | `sequence`: key-sequence string | Pauses after admission until the HID sequence completes or fails. | Normalized runner: `test_runtime_variable_actions_display`; transport: `test_key_action_transport.cpp`. Hardware pending. |
| `mouse_button` | `mouse.click` | `button`: `left`/`right`/`middle` | HID click request accepted. | Normalized runner: `test_runtime_variable_actions_display`; transport: `test_key_action_transport.cpp`. Hardware pending. |
| `gamepad` | `gamepad.button.tap`, `gamepad.button.down`, `gamepad.button.up` | `button`: integer 1–16 | Tap pauses until press and release complete; down/up complete when latch request succeeds. | Normalized runner: `test_runtime_variable_actions_display`; transport: `test_key_action_transport.cpp`. Hardware pending. |
| `gamepad` | `gamepad.hat.tap`, `gamepad.hat.down`, `gamepad.hat.up` | `direction`: `up`/`down`/`left`/`right` | Same tap versus latch completion as button. | Normalized runner: `test_runtime_variable_actions_display`; transport: `test_key_action_transport.cpp`. Hardware pending. |
| `gamepad` | `gamepad.trigger.tap`, `gamepad.trigger.down`, `gamepad.trigger.up` | `trigger`: `left`/`right` | Same tap versus latch completion as button. | Normalized runner: `test_runtime_variable_actions_display`; transport: `test_key_action_transport.cpp`. Hardware pending. |
| `ble_pair` | `ble.pair` | None | Pairing requested on an initialized, idle BLE transport. | Normalized runner: `test_runtime_variable_actions_display`; transport: `test_key_action_transport.cpp`. Hardware pending. |
| `mqtt` | `mqtt.publish` | `topic`: nonempty string; `payload`: string, may be empty | MQTT publish call accepts the message; no broker delivery guarantee. | Dynamic normalized dispatch, publish rejection and suffix stopping: `test_runtime_variable_actions_display`; broker and hardware pending. |
| `bthome` | `bthome.send` | `button`: integer 1–10; `event`: `press`/`double_press`/`long_press` | Event accepted by the BTHome sender, not proof of radio delivery. | Dynamic normalized dispatch, admission rejection and suffix stopping: `test_runtime_variable_actions_display`; sender: `test_ble_hid.cpp`; hardware pending. |
| `ha_service` | `ha.service.call` | `entity_id`: domain-qualified string; `service`: bare name; `data_json`: JSON-object string or empty | Normally queued; synchronous hooks execute immediately. Tracked MCP presses report delivery separately. | Dynamic normalized tracked dispatch, queue-full suffix cancellation and captured context: `test_runtime_variable_actions_display`; delivery: `test_ha_service_delivery`; hardware pending. |
| `notify` | `dialog.notify` | `message`: string; `duration_ms`: integer 0 or 1000–300000 | Dialog creation succeeds; it need not have been dismissed. | Dynamic normalized dispatch, invalid timing and dialog rejection stop suffix: `test_runtime_variable_actions_display`; dialog lifecycle: `test_stopwatch_round_ui`; hardware pending. |
| `confirm` | `dialog.confirm` | `message`, `approve_text`, `cancel_text`: strings; `timeout_ms`: integer 0 or 1000–300000 | Pauses until approval; cancellation, timeout, or admission failure stops the suffix. | Dynamic normalized dispatch, captured continuation, cancellation, full pool, invalid timing and admission rejection: `test_runtime_variable_actions_display`; real dialog lifecycle and timeout: `test_stopwatch_round_ui`; hardware pending. |
| `visual_alert` | `visual_alert.start` | `color`: hex string or empty; `pattern`: `breathe`/`blink`/`solid`; `period_ms`: integer 0–10000; `intensity`: integer 0–100; `duration_ms`: integer 0–2147483647 | Display alert show requested; zero values use documented defaults or persistence. | Dynamic normalized dispatch, defaults, invalid color/pattern and suffix stopping: `test_runtime_variable_actions_display`; hardware pending. |
| `visual_alert` | `visual_alert.stop` | None | Display alert stop requested. | Normalized dispatch to alert double: `test_runtime_variable_actions_display`; hardware pending. |
| `display_refresh` | `display.refresh_full` | None | Full-refresh request queued; panel refresh completes later. | Normalized dispatch, unavailable/failed queue results and suffix stopping: `test_runtime_variable_actions_display`; hardware pending. |
| `image_next`, `image_previous` | `image.next`, `image.previous` | None | Image-library cursor advances and generation updates; no available image fails. | Normalized dispatch and suffix stopping with image-library doubles: `test_runtime_variable_actions_display`. Real cursor and hardware integration pending. |
| `camera_capture` | `camera.capture` | `save_to`: `latest`/`roll`/`both` | Pauses until capture and requested storage writes complete, or fails. | Normalized: `test_runtime_variable_actions.cpp`; hardware pending. |
| `voice_note_record` | `voice_note.record.toggle`, `voice_note.record.start`, `voice_note.record.pause`, `voice_note.record.resume`, `voice_note.record.save`, `voice_note.record.discard` | None | Pauses after recording command admission until its continuation completes or fails. | Typed dispatch, rejection, and continuation: `test_runtime_variable_actions_display`; hardware pending. |
| `voice_note_process` | `voice_note.process` | None | Processing request accepted; remote processing completes later. | Typed dispatch and rejection: `test_runtime_variable_actions_display`; hardware pending. |
| `voice_note_delete` | `voice_note.delete` | `id`: 16 lowercase hexadecimal characters | Pauses until main-loop deletion succeeds or fails. | Normalized item-context dispatch and continuation: `test_runtime_variable_actions_display`; hardware pending. |
| `voice_note_load` | `voice_note.load` | `id`: 16 lowercase hexadecimal characters | Load request accepted; loaded content arrives later. | Normalized item-context dispatch, rejection, and invalid dynamic ID: `test_runtime_variable_actions_display`; hardware pending. |

Wi-Fi station feedback `[health:wifi_station_enabled]` is the **applied**
station mode, not the desired state or connection status. An admitted request
can still be in flight, so feedback can temporarily retain the previous value;
rejection fails the action. `[health:wifi_connected]` instead reports network
connection. Radio-off is a separate command. The catalog example maps `aan`
and `uit` to the required boolean strings and maps unknown values to `invalid`,
which is rejected rather than silently disabling Wi-Fi.

The host tests below exercise software behavior at subsystem boundaries. They
do not establish physical hardware behavior.

## Phase 2: shared hosts

| Host | Current typed behavior | Verification status |
|---|---|---|
| Pad buttons, long press, pad-level and widget event actions | Versioned pad actions use operation envelopes; stored arguments and originating event, item, and pad context are captured for continuation. Legacy pad actions are not silently translated. | Context and continuation: `test_runtime_variable_actions.cpp` and `test_action_continuation.cpp`. Hardware pending. |
| Gamepad Button widget | One literal `gamepad.button.down`, `gamepad.hat.down`, or `gamepad.trigger.down` target is copied into the widget at creation; touch owns and releases that target independently of the action latch. Dynamic targets are rejected. | `test_gamepad_widgets` exercises typed control snapshots, release and cancellation; `test_portal_pad_dialog_transaction.js` checks authoring round trips and rejection. Hardware pending. |
| Boot | Validates and owns typed arguments, atomically replaces saved configuration, and snapshots before dispatch. | Ownership and atomic saves: `test_timer_config.cpp`. Hardware pending. |
| Hardware buttons | Tap/hold snapshots own argument storage; modal dialogs dispatch the first navigation action only. Literal typed navigation can establish confirmation input availability; dynamic commands alone cannot guarantee it. | Typed event snapshots: `test_mqtt_triggers.cpp`; dialog fixture: `test_stopwatch_round_ui.cpp`. Hardware pending. |
| Swipes | Per-direction action is validated and copied before interactive LVGL-owner dispatch. | Ownership and atomic saves: `test_timer_config.cpp`. Hardware pending. |
| MQTT triggers | Validates and owns actions; copies a matching action list before noninteractive loop-owner dispatch. | Snapshots and replacement: `test_mqtt_triggers.cpp`. Broker and hardware pending. |
| Timer expiry | Configuration and running timers own action storage; expiry copies actions before noninteractive loop-owner dispatch. | Ownership and rejection: `test_timer_config.cpp`. Real timer and hardware pending. |
| Alarm ring/stop hooks | Typed and legacy actions are accepted only when their operation/type is synchronous; configuration, session, stop, and dispatch snapshots own their arguments. | Hook dispatch and rejection: `test_alarm_manager.cpp`. Hardware pending. |
| Camera motion | Validates, stores, and snapshots action lists for background dispatch. | Ownership and atomic saves: `test_timer_config.cpp`. Camera hardware pending. |
| MCP pad button press | Typed HA calls use observed list dispatch and tracked delivery; pausable actions retain continuation semantics. | Typed tracking and pauses: `test_mcp_press_button.cpp`. Live HA and hardware pending. |
| MCP direct controls and portal e-paper refresh | Fixed controls use typed operations or direct subsystem APIs on the existing main-loop or portal path. Notification, alert color, and volume retain checked binding resolution; invalid inputs and unavailable commands report failure. | MCP validation and typed dispatch: `test_mcp_direct_controls` (deferred bridge stubbed); portal refresh admission and failure: `test_sticky_hardware`. Scheduling and hardware pending. |

## Action template discovery and validation

The shared action-template visitor scans dynamic operation arguments and legacy
bindable fields, but not fixed operation strings. Pad source scanning retains
the originating pad's binding context. `test_action_bindings` covers
action-only MQTT and Home Assistant aliases, legacy fields, fixed strings, and
music analysis demand.

Explicit dynamic arguments reject unclosed binding tokens during JSON validation
and again before dispatch. `test_runtime_variable_actions` covers malformed
input, suffix cancellation, literal strings, nested expressions, and fallbacks
across display, headless, and no-PSRAM profiles. Hardware remains pending.

## Pending inventory

Phase 3 device-class owners (nine): `meter`, `strip`, `shelly`, `expose`,
`print`, `scale`, `brew`, `voice`, and `shutter`.

Board-specific types (four): `charging_mode`, `status_led`, `vibrate`, and
`shutdown`. The `shutdown` type has separate implementations on the M5Stack
StopWatch and reTerminal Sticky Interactive.

These are not claimed as migrated by the built-in inventory above.
