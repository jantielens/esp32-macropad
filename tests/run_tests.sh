#!/bin/bash
# ============================================================================
# Run all host-native unit and integration tests
# ============================================================================
# No ESP32 needed — compiles and runs on the development machine.
# Usage: ./tests/run_tests.sh

set -e
cd "$(dirname "$0")/.."

mkdir -p tests/bin

echo "=== Building unit tests: OTA activity ==="
g++ -std=c++17 -Wall -Wextra -Werror -pthread -DOTA_ACTIVITY_HOST_TEST \
    -I src/app \
    tests/test_ota_activity.cpp src/app/ota_activity.cpp \
    -o tests/bin/test_ota_activity

echo "=== Running unit tests: OTA activity ==="
./tests/bin/test_ota_activity
echo

echo "=== Building unit tests: pad layout ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -include tests/Arduino.h -include tests/board_config.h \
    -I tests -I src/app -I ~/Arduino/libraries/lvgl/src \
    tests/test_pad_layout.cpp \
    -o tests/bin/test_pad_layout

echo "=== Running unit tests: pad layout ==="
./tests/bin/test_pad_layout
echo

echo "=== Building unit tests: numeric rocker zones ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -include tests/Arduino.h -include tests/board_config.h \
    -I tests -I src/app \
    tests/test_numericrocker_zones.cpp \
    -o tests/bin/test_numericrocker_zones

echo "=== Running unit tests: numeric rocker zones ==="
./tests/bin/test_numericrocker_zones
echo

echo "=== Building unit tests: button shadow color ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -I src/app \
    tests/test_button_shadow_color.cpp \
    -o tests/bin/test_button_shadow_color

echo "=== Running unit tests: button shadow color ==="
./tests/bin/test_button_shadow_color
echo

echo "=== Building compile check: sparkline without HA history ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -Wno-unused-parameter -Wno-unused-variable -Wno-unused-but-set-variable \
    -Wno-format-truncation \
    -DHAS_HA_HISTORY=0 -DHAS_MCP=0 \
    -include tests/Arduino.h -include tests/log_manager.h -include tests/board_config.h \
    -I tests -I src/app \
    -I ~/Arduino/libraries/ArduinoJson/src \
    -I ~/Arduino/libraries/lvgl/src \
    -c src/app/widgets/sparkline_widget.cpp \
    -o tests/bin/sparkline_no_ha.o
echo

echo "=== Building unit tests: expr_eval ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    tests/test_expr_eval.cpp \
    src/app/expr_eval.cpp \
    -o tests/bin/test_expr_eval -lm

echo "=== Running unit tests: expr_eval ==="
./tests/bin/test_expr_eval
echo

echo "=== Building unit tests: battery ADC percentage curve ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -DHAS_SENSOR_BATTERY_ADC=0 \
    -include tests/Arduino.h -include tests/board_config.h \
    -I tests -I src/app \
    tests/test_battery_adc_sensor.cpp \
    src/app/sensors/battery_adc_sensor.cpp \
    -o tests/bin/test_battery_adc_sensor -lm
echo "=== Running unit tests: battery ADC percentage curve ==="
./tests/bin/test_battery_adc_sensor
echo

echo "=== Building unit tests: deferred dispatch slot ==="
g++ -std=c++17 -Wall -Wextra -Werror -pthread \
    -I tests -I src/app \
    tests/test_deferred_dispatch_slot.cpp \
    -o tests/bin/test_deferred_dispatch_slot

echo "=== Running unit tests: deferred dispatch slot ==="
./tests/bin/test_deferred_dispatch_slot
echo

echo "=== Building integration tests: pad resolve request lifetime ==="
g++ -std=c++17 -Wall -Wextra -Werror -Wno-deprecated-declarations \
    -include tests/pad_resolve_overrides/Arduino.h -include tests/log_manager.h -include tests/board_config.h \
    -I tests/pad_resolve_overrides -I tests -I src/app -I ~/Arduino/libraries/ArduinoJson/src \
    tests/test_pad_resolve_request_lifetime.cpp \
    src/app/main_loop_bridge.cpp src/app/pad_resolve_request.cpp tests/stubs.cpp \
    -o tests/bin/test_pad_resolve_request_lifetime

echo "=== Running integration tests: pad resolve request lifetime ==="
./tests/bin/test_pad_resolve_request_lifetime
echo

echo "=== Building unit tests: ha_stats_resample ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -I src/app \
    tests/test_ha_stats_resample.cpp \
    src/app/ha_stats_resample.cpp \
    -o tests/bin/test_ha_stats_resample -lm

echo "=== Running unit tests: ha_stats_resample ==="
./tests/bin/test_ha_stats_resample
echo

echo "=== Building unit tests: ha_service_delivery ==="
g++ -std=c++17 -Wall -Wextra -Werror -Wno-deprecated-declarations \
    -DHAS_MCP=1 \
    -include tests/board_config.h \
    -I tests -I src/app \
    -I ~/Arduino/libraries/ArduinoJson/src \
    tests/test_ha_service_delivery.cpp \
    src/app/ha_service_delivery.cpp \
    -o tests/bin/test_ha_service_delivery

echo "=== Running unit tests: ha_service_delivery ==="
./tests/bin/test_ha_service_delivery
echo

echo "=== Building compile check: ha_service_delivery without MCP ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -DHAS_MCP=0 \
    -include tests/board_config.h \
    -I tests -I src/app \
    -c src/app/ha_service_delivery.cpp \
    -o tests/bin/ha_service_delivery_no_mcp.o
g++ -std=c++17 -Wall -Wextra -Werror \
    -DHAS_MCP=0 \
    -include tests/board_config.h \
    -I tests -I src/app \
    -c src/app/mcp_press_button.cpp \
    -o tests/bin/mcp_press_button_no_mcp.o
echo

echo "=== Building unit tests: MCP press-button orchestration ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -DHAS_MCP=1 \
    -include tests/board_config.h \
    -I tests -I src/app \
    -I ~/Arduino/libraries/ArduinoJson/src \
    tests/test_mcp_press_button.cpp \
    src/app/mcp_press_button.cpp \
    -o tests/bin/test_mcp_press_button

echo "=== Running unit tests: MCP press-button orchestration ==="
./tests/bin/test_mcp_press_button
echo

echo "=== Building integration tests: expr_binding ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -include tests/log_manager.h -include tests/board_config.h \
    -I tests -I src/app \
    -I ~/Arduino/libraries/ArduinoJson/src \
    tests/test_expr_binding.cpp \
    src/app/binding_template.cpp \
    src/app/expr_eval.cpp \
    tests/stubs.cpp \
    -o tests/bin/test_expr_binding -lm

echo "=== Running integration tests: expr_binding ==="
./tests/bin/test_expr_binding
echo

echo "=== Building unit tests: binding_template ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -include tests/log_manager.h -include tests/board_config.h \
    -I src/app \
    tests/test_binding_template.cpp \
    src/app/binding_template.cpp \
    tests/stubs.cpp \
    -o tests/bin/test_binding_template -lm

echo "=== Running unit tests: binding_template ==="
./tests/bin/test_binding_template
echo

echo "=== Building unit tests: brew template DSL ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -DHAS_SCALE=1 \
    -include tests/board_config.h \
    -I tests -I src/app -I src/app/device_classes/coffee_scale/brew \
    -I ~/Arduino/libraries/ArduinoJson/src \
    tests/test_brew_template_dsl.cpp \
    src/app/device_classes/coffee_scale/brew/brew_template_dsl.cpp \
    -o tests/bin/test_brew_template_dsl

echo "=== Running unit tests: brew template DSL ==="
./tests/bin/test_brew_template_dsl
echo

echo "=== Building unit tests: music transport ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -I src/app \
    tests/test_music_transport.cpp src/app/music_transport.cpp \
    -o tests/bin/test_music_transport

echo "=== Running unit tests: music transport ==="
./tests/bin/test_music_transport
echo

echo "=== Building unit tests: music command ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -I src/app \
    tests/test_music_command.cpp src/app/music_command.cpp \
    -o tests/bin/test_music_command

echo "=== Running unit tests: music command ==="
./tests/bin/test_music_command
echo

echo "=== Building unit tests: Voice Assistant WAV ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -I src/app \
    tests/test_voice_wav.cpp src/app/device_classes/voice_assistant/voice_wav.cpp \
    -o tests/bin/test_voice_wav

echo "=== Running unit tests: Voice Assistant WAV ==="
./tests/bin/test_voice_wav
echo

echo "=== Building unit tests: Voice Assistant transcription request ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -I src/app \
    tests/test_voice_transcription_request.cpp src/app/device_classes/voice_assistant/voice_transcription_request.cpp \
    -o tests/bin/test_voice_transcription_request

echo "=== Running unit tests: Voice Assistant transcription request ==="
./tests/bin/test_voice_transcription_request
echo

echo "=== Building unit tests: Voice Assistant TTS request ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -I src/app \
    tests/test_voice_tts_request.cpp src/app/device_classes/voice_assistant/voice_tts_request.cpp \
    -o tests/bin/test_voice_tts_request

echo "=== Running unit tests: Voice Assistant TTS request ==="
./tests/bin/test_voice_tts_request
echo

echo "=== Building unit tests: Voice Assistant auto-stop ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -I src/app \
    tests/test_voice_auto_stop.cpp src/app/device_classes/voice_assistant/voice_auto_stop.cpp \
    -o tests/bin/test_voice_auto_stop

echo "=== Running unit tests: Voice Assistant auto-stop ==="
./tests/bin/test_voice_auto_stop
echo

echo "=== Building unit tests: MP3 metadata ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -I src/app \
    tests/test_mp3_metadata.cpp src/app/mp3_metadata.cpp \
    -o tests/bin/test_mp3_metadata

echo "=== Running unit tests: MP3 metadata ==="
./tests/bin/test_mp3_metadata
echo

echo "=== Building unit tests: music catalog ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -I tests -I src/app \
    tests/test_music_catalog.cpp src/app/music_catalog.cpp src/app/mp3_metadata.cpp \
    -o tests/bin/test_music_catalog

echo "=== Running unit tests: music catalog ==="
./tests/bin/test_music_catalog
echo

echo "=== Building unit tests: music binding ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -DHAS_AUDIO=1 -DHAS_SOUND_PLAYER=1 -DHAS_MCP=1 \
    -include tests/board_config.h -include tests/log_manager.h \
    -I tests -I src/app \
    -I ~/Arduino/libraries/ArduinoJson/src \
    tests/test_music_binding.cpp src/app/music_binding.cpp src/app/binding_template.cpp tests/stubs.cpp \
    -o tests/bin/test_music_binding

echo "=== Running unit tests: music binding ==="
./tests/bin/test_music_binding
echo

echo "=== Building integration tests: timer binding target without MQTT ==="
g++ -std=c++17 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
    -include tests/timer_test_overrides/board_config.h -include tests/log_manager.h \
    -I tests/timer_test_overrides -I tests -I src/app \
    -I ~/Arduino/libraries/ArduinoJson/src \
    -I ~/Arduino/libraries/lvgl/src \
    tests/test_timer_binding_target.cpp \
    src/app/timer_binding.cpp \
    src/app/timer_engine.cpp \
    src/app/binding_template.cpp \
    tests/stubs.cpp \
    -o tests/bin/test_timer_binding_target -lm -pthread -Wl,--gc-sections

echo "=== Running integration tests: timer binding target without MQTT ==="
./tests/bin/test_timer_binding_target
echo

echo "=== Building unit tests: health_table_builder ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -include tests/log_manager.h -include tests/board_config.h \
    -I src/app \
    -I ~/Arduino/libraries/ArduinoJson/src \
    tests/test_health_table_builder.cpp \
    src/app/health_table_builder.cpp \
    tests/stubs.cpp \
    -o tests/bin/test_health_table_builder -lm

echo "=== Running unit tests: health_table_builder ==="
./tests/bin/test_health_table_builder
echo

echo "=== Building integration tests: pad_binding ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -include tests/log_manager.h -include tests/board_config.h \
    -I src/app \
    tests/test_pad_binding.cpp \
    src/app/binding_template.cpp \
    src/app/pad_binding.cpp \
    src/app/expr_eval.cpp \
    tests/stubs.cpp \
    -o tests/bin/test_pad_binding -lm

echo "=== Running integration tests: pad_binding ==="
./tests/bin/test_pad_binding
echo

echo "=== Building unit tests: label_style ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -include tests/log_manager.h -include tests/board_config.h \
    -I src/app \
    tests/test_label_style.cpp \
    src/app/label_style.cpp \
    tests/stubs.cpp \
    -o tests/bin/test_label_style -lm

echo "=== Running unit tests: label_style ==="
./tests/bin/test_label_style
echo

echo "=== Building unit tests: pad_confirmation ==="
g++ -std=c++17 -Wall -Wextra -Werror -Wno-deprecated-declarations \
    -include tests/log_manager.h -include tests/board_config.h \
    -I tests -I src/app \
    -I ~/Arduino/libraries/ArduinoJson/src \
    -I ~/Arduino/libraries/lvgl/src \
    tests/test_pad_confirmation.cpp \
    src/app/pad_validate.cpp \
    src/app/action_validate.cpp \
    tests/stubs.cpp \
    -o tests/bin/test_pad_confirmation

echo "=== Running unit tests: pad_confirmation ==="
./tests/bin/test_pad_confirmation
echo

echo "=== Building unit tests: widget_common ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -I src/app \
    tests/test_widget_common.cpp \
    -o tests/bin/test_widget_common -lm

echo "=== Running unit tests: widget_common ==="
./tests/bin/test_widget_common
echo

echo "=== Building unit tests: image_rgba_source ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -I src/app \
    tests/test_image_rgba_source.cpp \
    -o tests/bin/test_image_rgba_source

echo "=== Running unit tests: image_rgba_source ==="
./tests/bin/test_image_rgba_source
echo

echo "=== Building unit tests: minimp3_scratch ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -I src/app \
    tests/test_minimp3_scratch.cpp \
    -o tests/bin/test_minimp3_scratch -lm

echo "=== Running unit tests: minimp3_scratch ==="
./tests/bin/test_minimp3_scratch
echo

echo "=== Building unit tests: audio resampler ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -I tests -I src/app \
    tests/test_audio_resampler.cpp \
    -o tests/bin/test_audio_resampler

echo "=== Running unit tests: audio resampler ==="
./tests/bin/test_audio_resampler
echo

echo "=== Building unit tests: music transport ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -I src/app \
    tests/test_music_transport.cpp \
    src/app/music_transport.cpp \
    -o tests/bin/test_music_transport

echo "=== Running unit tests: music transport ==="
./tests/bin/test_music_transport
echo

echo "=== Building unit tests: music catalog ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -I src/app \
    tests/test_music_catalog.cpp \
    src/app/music_catalog.cpp src/app/mp3_metadata.cpp \
    -o tests/bin/test_music_catalog

echo "=== Running unit tests: music catalog ==="
./tests/bin/test_music_catalog
echo

echo "=== Building unit tests: tone alert overlay ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -I src/app \
    tests/test_tone_alert_overlay.cpp \
    -o tests/bin/test_tone_alert_overlay -lm

echo "=== Running unit tests: tone alert overlay ==="
./tests/bin/test_tone_alert_overlay
echo

echo "=== Building unit tests: audio gain ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -I src/app \
    tests/test_audio_gain.cpp \
    -o tests/bin/test_audio_gain

echo "=== Running unit tests: audio gain ==="
./tests/bin/test_audio_gain
echo

echo "=== Building unit tests: key_sequence ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    tests/test_key_sequence.cpp \
    src/app/key_sequence.cpp \
    -o tests/bin/test_key_sequence

echo "=== Running unit tests: key_sequence ==="
./tests/bin/test_key_sequence
echo

echo "=== Building unit tests: action_parse ==="
g++ -std=c++17 -Wall -Wextra -Werror -Wno-deprecated-declarations \
    -include tests/log_manager.h -include tests/board_config.h \
    -I tests -I src/app \
    -I ~/Arduino/libraries/ArduinoJson/src \
    tests/test_action_parse.cpp \
    src/app/actions.cpp \
    src/app/music_command.cpp \
    src/app/action_parse.cpp \
    src/app/action_list.cpp \
    src/app/action_continuation.cpp \
    src/app/pad_cycle.cpp \
    src/app/action_registry.cpp \
    src/app/binding_template.cpp \
    tests/stubs.cpp \
    -o tests/bin/test_action_parse

echo "=== Running unit tests: action_parse ==="
./tests/bin/test_action_parse
echo

echo "=== Building unit tests: camera capture action ==="
g++ -std=c++17 -Wall -Wextra -Werror -Wno-deprecated-declarations \
    -DHAS_CAMERA=1 \
    -include tests/log_manager.h -include tests/board_config.h \
    -I tests -I src/app \
    -I ~/Arduino/libraries/ArduinoJson/src \
    tests/test_camera_capture_action.cpp \
    src/app/action_continuation.cpp \
    src/app/action_registry.cpp \
    src/app/binding_template.cpp \
    tests/stubs.cpp \
    -o tests/bin/test_camera_capture_action

echo "=== Running unit tests: camera capture action ==="
./tests/bin/test_camera_capture_action
echo

echo "=== Building unit tests: action continuation ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -include tests/log_manager.h -include tests/board_config.h \
    -I tests -I src/app -I ~/Arduino/libraries/ArduinoJson/src \
    tests/test_action_continuation.cpp \
    src/app/action_list.cpp \
    src/app/action_continuation.cpp \
    tests/stubs.cpp \
    -o tests/bin/test_action_continuation

echo "=== Running unit tests: action continuation ==="
./tests/bin/test_action_continuation
echo

echo "=== Building unit tests: pad_cycle ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -include tests/board_config.h \
    -I src/app \
    tests/test_pad_cycle.cpp \
    src/app/pad_cycle.cpp \
    -o tests/bin/test_pad_cycle

echo "=== Running unit tests: pad_cycle ==="
./tests/bin/test_pad_cycle
echo

echo "=== Building unit tests: pad_cache_transaction ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -include tests/Arduino.h -include tests/board_config.h \
    -I tests -I src/app \
    tests/test_pad_cache_transaction.cpp \
    src/app/pad_cache_transaction.cpp \
    -o tests/bin/test_pad_cache_transaction

echo "=== Running unit tests: pad_cache_transaction ==="
./tests/bin/test_pad_cache_transaction
echo

echo "=== Building size guard: action_sizes ==="
g++ -std=c++17 -Wall -Wextra -Wno-unused \
    -include tests/log_manager.h -include tests/board_config.h \
    -I src/app \
    -I ~/Arduino/libraries/ArduinoJson/src \
    tests/test_action_sizes.cpp \
    tests/stubs.cpp \
    -o tests/bin/test_action_sizes

echo "=== Running size guard: action_sizes ==="
./tests/bin/test_action_sizes
echo

echo "=== Building unit tests: shutter_session_actions ==="
g++ -std=c++17 -Wall -Wextra -Werror -Wno-deprecated-declarations \
    -include tests/log_manager.h -include tests/board_config.h \
    -I tests -I src/app -I src/app/device_classes/shutter_tester \
    -I ~/Arduino/libraries/ArduinoJson/src \
    tests/test_shutter_session_actions.cpp \
    src/app/actions.cpp \
    src/app/music_command.cpp \
    src/app/action_parse.cpp \
    src/app/action_continuation.cpp \
    src/app/pad_cycle.cpp \
    src/app/action_registry.cpp \
    src/app/binding_template.cpp \
    tests/stubs.cpp \
    -o tests/bin/test_shutter_session_actions

echo "=== Running unit tests: shutter_session_actions ==="
./tests/bin/test_shutter_session_actions
echo

echo "=== Building unit tests: wifi_reconnect ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -I tests -I src/app \
    tests/test_wifi_reconnect.cpp \
    src/app/wifi_reconnect.cpp \
    -o tests/bin/test_wifi_reconnect

echo "=== Running unit tests: wifi_reconnect ==="
./tests/bin/test_wifi_reconnect
echo

echo "=== Building unit tests: timer_format ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -include tests/board_config.h \
    -I tests -I src/app \
    -I ~/Arduino/libraries/ArduinoJson/src \
    tests/test_timer_format.cpp \
    tests/stubs.cpp \
    -o tests/bin/test_timer_format

echo "=== Running unit tests: timer_format ==="
./tests/bin/test_timer_format
echo

echo "=== Building unit tests: screen_saver_schedule ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -I src/app \
    tests/test_screen_saver_schedule.cpp \
    -o tests/bin/test_screen_saver_schedule

echo "=== Running unit tests: screen_saver_schedule ==="
./tests/bin/test_screen_saver_schedule
echo

echo "=== Building unit tests: timer_commands ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -include tests/timer_test_overrides/board_config.h \
    -include tests/log_manager.h \
    -I tests/timer_test_overrides -I tests -I src/app \
    -I ~/Arduino/libraries/ArduinoJson/src \
    tests/test_timer_commands.cpp \
    src/app/timer_command.cpp \
    src/app/timer_mcp_adapter.cpp \
    src/app/timer_engine.cpp \
    tests/stubs.cpp \
    -o tests/bin/test_timer_commands -lm -pthread

echo "=== Running unit tests: timer_commands ==="
./tests/bin/test_timer_commands pre-init-controls
./tests/bin/test_timer_commands mutex-failure
./tests/bin/test_timer_commands
echo

echo "=== Building unit tests: timer_config ==="
g++ -std=c++17 -Wall -Wextra -Werror -Wno-deprecated-declarations \
    -include tests/timer_test_overrides/board_config.h \
    -include tests/log_manager.h -include tests/Arduino.h \
    -I tests/timer_test_overrides -I tests -I src/app \
    -I ~/Arduino/libraries/ArduinoJson/src \
    tests/test_timer_config.cpp \
    src/app/timer_config.cpp \
    src/app/config_psram.cpp \
    src/app/actions.cpp \
    src/app/music_command.cpp \
    src/app/action_parse.cpp \
    src/app/action_continuation.cpp \
    src/app/pad_cycle.cpp \
    src/app/action_registry.cpp \
    tests/stubs.cpp \
    -o tests/bin/test_timer_config -lm -pthread

echo "=== Running unit tests: timer_config ==="
./tests/bin/test_timer_config config-mutex-failure
./tests/bin/test_timer_config save-mutex-failure
./tests/bin/test_timer_config
echo

echo "=== Running unit tests: timer_action_editor ==="
node tests/test_timer_action_editor.js
node tests/test_portal_action_editor_cycle.js
node tests/test_portal_action_picker.js
node tests/test_portal_action_list.js
node tests/test_portal_timer_binding.js
node tests/test_portal_pad_dialog_transaction.js
node tests/test_portal_pad_import.js
node tests/test_portal_pad_dirty.js
python3 tests/test_timer_mcp_integration.py
node --check src/app/web/portal_action_editor.js
node --check src/app/web/portal_binding_validator.js
node --check src/app/web/portal_pad_dialog.js
node --check src/app/web/portal_config_actions.js
echo

echo "=== Building unit tests: action_bindings ==="
g++ -std=c++17 -Wall -Wextra -Werror -Wno-deprecated-declarations \
    -DHAS_AUDIO=1 -DHAS_SOUND_PLAYER=1 \
    -include tests/log_manager.h -include tests/board_config.h \
    -I tests -I src/app \
    -I ~/Arduino/libraries/ArduinoJson/src \
    tests/test_action_bindings.cpp \
    src/app/actions.cpp \
    src/app/music_command.cpp \
    src/app/action_parse.cpp \
    src/app/action_list.cpp \
    src/app/action_continuation.cpp \
    src/app/action_registry.cpp \
    src/app/binding_template.cpp \
    src/app/pad_cycle.cpp \
    tests/stubs.cpp \
    -o tests/bin/test_action_bindings -lm

echo "=== Running unit tests: action_bindings ==="
./tests/bin/test_action_bindings
echo

echo "=== Building unit tests: action_registry_value_field ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -include tests/log_manager.h -include tests/board_config.h \
    -I src/app \
    -I ~/Arduino/libraries/ArduinoJson/src \
    tests/test_action_registry_value_field.cpp \
    src/app/action_registry.cpp \
    src/app/binding_template.cpp \
    tests/stubs.cpp \
    -o tests/bin/test_action_registry_value_field -lm

echo "=== Running unit tests: action_registry_value_field ==="
./tests/bin/test_action_registry_value_field
echo

echo "=== Building unit tests: component_registry ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -include tests/log_manager.h -include tests/board_config.h \
    -I tests -I src/app \
    tests/test_component_registry.cpp \
    src/app/component_registry.cpp \
    tests/stubs.cpp \
    -o tests/bin/test_component_registry

echo "=== Running unit tests: component_registry ==="
./tests/bin/test_component_registry
echo

echo "=== Running guard: portal component capacity matrix ==="
python3 ./tests/test_portal_component_capacity_matrix.py
echo

echo "=== Building unit tests: shutter_curtain_stats ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -I src/app -I src/app/device_classes/shutter_tester \
    tests/test_shutter_curtain_stats.cpp \
    src/app/device_classes/shutter_tester/shutter_curtain_stats.cpp \
    -o tests/bin/test_shutter_curtain_stats -lm

echo "=== Running unit tests: shutter_curtain_stats ==="
./tests/bin/test_shutter_curtain_stats
echo

echo "=== Building unit tests: shutter_capture ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -DIS_SHUTTER_TESTER=true \
    -include tests/log_manager.h \
    -I tests -I src/app -I src/app/device_classes/shutter_tester \
    tests/test_shutter_capture.cpp \
    -o tests/bin/test_shutter_capture

echo "=== Running unit tests: shutter_capture ==="
./tests/bin/test_shutter_capture
echo

echo "=== Building unit tests: shutter_measure ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -DIS_SHUTTER_TESTER=true \
    -include tests/log_manager.h \
    -I tests -I src/app -I src/app/device_classes/shutter_tester \
    tests/test_shutter_measure.cpp \
    tests/stubs.cpp \
    -o tests/bin/test_shutter_measure -lm

echo "=== Running unit tests: shutter_measure ==="
./tests/bin/test_shutter_measure
echo

echo "=== Building unit tests: shutter_binding ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -DHAS_DISPLAY=true -DIS_SHUTTER_TESTER=true \
    -include tests/log_manager.h \
    -I tests -I src/app -I src/app/device_classes/shutter_tester \
    tests/test_shutter_binding.cpp \
    tests/stubs.cpp \
    -o tests/bin/test_shutter_binding -lm

echo "=== Running unit tests: shutter_binding ==="
./tests/bin/test_shutter_binding
echo

echo "=== Building unit tests: list_provider ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -include tests/log_manager.h -include tests/board_config.h \
    -I src/app \
    tests/test_list_provider.cpp \
    src/app/list_provider.cpp \
    src/app/list_binding.cpp \
    src/app/binding_template.cpp \
    tests/stubs.cpp \
    -o tests/bin/test_list_provider

echo "=== Running unit tests: list_provider ==="
./tests/bin/test_list_provider
echo

echo "=== Building unit tests: net_activity ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -I tests -I src/app \
    tests/test_net_activity.cpp \
    src/app/net_activity.cpp \
    -o tests/bin/test_net_activity

echo "=== Running unit tests: net_activity ==="
./tests/bin/test_net_activity
echo

echo "=== Building unit tests: shutter_session ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -Wno-deprecated-declarations \
    -DIS_SHUTTER_TESTER=true \
    -include tests/log_manager.h \
    -I tests/shutter_session_overrides \
    -I tests \
    -I src -I src/app -I src/app/device_classes/shutter_tester \
    -I ~/Arduino/libraries/ArduinoJson/src \
    tests/test_shutter_session.cpp \
    src/app/device_classes/shutter_tester/shutter_session.cpp \
    src/app/device_classes/shutter_tester/shutter_curtain_stats.cpp \
    tests/stubs.cpp \
    -o tests/bin/test_shutter_session -lm

echo "=== Running unit tests: shutter_session ==="
./tests/bin/test_shutter_session
echo

echo "=== Building unit tests: epaper_battery ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -I src/app \
    tests/test_epaper_battery.cpp \
    -o tests/bin/test_epaper_battery

echo "=== Running unit tests: epaper_battery ==="
./tests/bin/test_epaper_battery
echo

echo "=== Building unit tests: epaper_next_client_logic ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -I src/app \
    tests/test_epaper_next_client_logic.cpp \
    src/app/device_classes/epaper/epaper_next_client_logic.cpp \
    src/app/device_classes/epaper/epaper_transport_crc32.cpp \
    -o tests/bin/test_epaper_next_client_logic

echo "=== Running unit tests: epaper_next_client_logic ==="
./tests/bin/test_epaper_next_client_logic
echo

echo "=== Building conformance tests: epaper_media ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -I src/app \
    tests/test_epaper_media_conformance.cpp \
    src/app/device_classes/epaper/epaper_media_validation.cpp \
    src/app/device_classes/epaper/epaper_transport_crc32.cpp \
    -o tests/bin/test_epaper_media_conformance -lz

echo "=== Running conformance tests: epaper_media ==="
./tests/bin/test_epaper_media_conformance
echo

echo "=== Building guard: coffee_scale command length ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -DIS_COFFEE_SCALE=true \
    -include tests/board_config.h \
    -I src/app \
    tests/test_coffee_scale_cmd_len.cpp \
    -o tests/bin/test_coffee_scale_cmd_len

echo "=== Running guard: coffee_scale command length ==="
./tests/bin/test_coffee_scale_cmd_len
echo

echo "=== Running guard: brew storage and deferred tare ==="
bash ./tests/test_brew_storage_and_tare_guards.sh
echo

echo "=== Building unit tests: expose_dry_down ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -DIS_DARKROOM_TIMER=1 -DHAS_AUDIO=0 \
    -I tests -I src/app -I src/app/device_classes/darkroom_timer \
    tests/test_expose_dry_down.cpp \
    -o tests/bin/test_expose_dry_down -lm

echo "=== Running unit tests: expose_dry_down ==="
./tests/bin/test_expose_dry_down
echo

echo "=== Building unit tests: meter_mag ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -DIS_DARKROOM_TIMER=1 \
    -I tests -I src/app -I src/app/device_classes/darkroom_timer \
    tests/test_meter_mag.cpp \
    -o tests/bin/test_meter_mag -lm

echo "=== Running unit tests: meter_mag ==="
./tests/bin/test_meter_mag
echo

echo "=== Building unit tests: print_log ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -DIS_DARKROOM_TIMER=1 \
    -I tests/print_log_overrides \
    -I tests -I src/app -I src/app/device_classes/darkroom_timer \
    -I ~/Arduino/libraries/ArduinoJson/src \
    tests/test_print_log.cpp \
    -o tests/bin/test_print_log -lm

echo "=== Running unit tests: print_log ==="
./tests/bin/test_print_log
echo

echo "=== Building unit tests: mcp_result_strings ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -I ~/Arduino/libraries/ArduinoJson/src \
    tests/test_mcp_result_strings.cpp \
    -o tests/bin/test_mcp_result_strings

echo "=== Running unit tests: mcp_result_strings ==="
./tests/bin/test_mcp_result_strings
echo

echo "=== Building unit tests: mcp_device_identity ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -DHAS_MCP=1 \
    -include tests/Arduino.h -include tests/board_config.h \
    -I tests -I src/app \
    -I ~/Arduino/libraries/ArduinoJson/src \
    tests/test_mcp_device_identity.cpp \
    src/app/mcp_device_identity.cpp \
    -o tests/bin/test_mcp_device_identity

echo "=== Running unit tests: mcp_device_identity ==="
./tests/bin/test_mcp_device_identity
echo

echo "=== Building unit tests: filesystem health ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -DUSE_SD_STORAGE=0 \
    -include tests/board_config.h \
    -I tests -I src/app \
    tests/test_fs_health.cpp src/app/fs_health.cpp \
    -o tests/bin/test_fs_health_littlefs
g++ -std=c++17 -Wall -Wextra -Werror \
    -DUSE_SD_STORAGE=1 -DHAS_SD_CARD=1 \
    -include tests/board_config.h \
    -I tests -I src/app \
    tests/test_fs_health.cpp src/app/fs_health.cpp \
    -o tests/bin/test_fs_health_sdmmc

echo "=== Running unit tests: filesystem health ==="
./tests/bin/test_fs_health_littlefs
./tests/bin/test_fs_health_sdmmc
echo

echo "=== Building unit tests: filesystem health telemetry ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -include tests/Arduino.h -include tests/board_config.h \
    -I tests -I src/app -I ~/Arduino/libraries/ArduinoJson/src \
    tests/test_fs_health_telemetry.cpp \
    -o tests/bin/test_fs_health_telemetry

echo "=== Running unit tests: filesystem health telemetry ==="
./tests/bin/test_fs_health_telemetry
echo

echo "=== Building unit tests: storage policy ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -DUSE_SD_STORAGE=0 \
    -I tests/storage_policy_overrides -I tests -I src/app \
    tests/test_storage_policy.cpp \
    -o tests/bin/test_storage_policy_littlefs
g++ -std=c++17 -Wall -Wextra -Werror \
    -DUSE_SD_STORAGE=1 -DHAS_SD_CARD=1 \
    -I tests/storage_policy_overrides -I tests -I src/app \
    tests/test_storage_policy.cpp \
    -o tests/bin/test_storage_policy_sdmmc

echo "=== Running unit tests: storage policy ==="
./tests/bin/test_storage_policy_littlefs
./tests/bin/test_storage_policy_sdmmc
echo

echo "=== Building unit tests: SD factory reset storage cleanup ==="
g++ -std=c++17 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
    -I tests/factory_reset_overrides -I tests -I src/app \
    tests/test_factory_reset_storage.cpp \
    -o tests/bin/test_factory_reset_storage -Wl,--gc-sections

echo "=== Running unit tests: SD factory reset storage cleanup ==="
./tests/bin/test_factory_reset_storage
echo

echo "=== Checking compile-time storage flag invariant ==="
printf '\n' | g++ -std=c++17 -x c++ -fsyntax-only \
    -DHAS_SD_CARD=0 -DUSE_SD_STORAGE=0 -I src/app -include board_config.h -
printf '\n' | g++ -std=c++17 -x c++ -fsyntax-only \
    -DHAS_SD_CARD=1 -DUSE_SD_STORAGE=0 -I src/app -include board_config.h -
printf '\n' | g++ -std=c++17 -x c++ -fsyntax-only \
    -DHAS_SD_CARD=1 -DUSE_SD_STORAGE=1 -I src/app -include board_config.h -
if printf '\n' | g++ -std=c++17 -x c++ -fsyntax-only \
    -DHAS_SD_CARD=0 -DUSE_SD_STORAGE=1 -I src/app -include board_config.h - 2>/dev/null; then
    echo "Expected USE_SD_STORAGE without HAS_SD_CARD to fail compilation" >&2
    exit 1
fi
echo

echo "=== Building compile check: mcp_device_identity without MCP ==="
g++ -std=c++17 -Wall -Wextra -Werror \
    -DHAS_MCP=0 \
    -include tests/board_config.h \
    -I tests -I src/app \
    -c src/app/mcp_device_identity.cpp \
    -o tests/bin/mcp_device_identity_no_mcp.o
echo

echo "=== Running guard: branding mirror (C++ <-> bash) ==="
./tests/test_branding_mirror.sh
echo

echo "=== Running guard: asset bundler variant matrix ==="
./tests/test_asset_variant_matrix.sh
echo

echo "=== Running guard: widget schema parity (describe <-> parse) ==="
python3 tools/lint_widget_schema.py
echo

echo "=== Running guard: camera preview widget wiring ==="
./tests/test_camera_preview_widget.sh
./tests/test_camera_motion.sh
echo

echo "=== Running guard: MCP binding-scheme parity (register <-> describe) ==="
./tests/test_mcp_scheme_parity.sh
echo

echo "=== Running guard: action catalog completeness ==="
./tests/test_action_catalog_completeness.sh
echo

echo "=== Running guard: MCP storage browser parity ==="
./tests/test_mcp_storage_parity.sh
echo

echo "=== Running guard: display-safe telemetry heap walks ==="
./tests/test_display_safe_telemetry.sh
echo

echo "=== Running guard: required SD validation before Wi-Fi initialization ==="
./tests/test_sd_wifi_order.sh
echo

echo "=== Running guard: photoframe conformance-vector producer drift ==="
./tests/test_photoframe_vector_drift.sh
echo

echo "=== All tests passed ==="
