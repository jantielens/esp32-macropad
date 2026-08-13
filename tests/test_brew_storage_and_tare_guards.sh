#!/bin/bash
# Guard Coffee Scale brew persistence and deferred-tare sequencing.

set -euo pipefail
cd "$(dirname "$0")/.."

for required_command in grep; do
	if ! command -v "$required_command" >/dev/null 2>&1; then
		echo "FAIL: required command is unavailable: $required_command" >&2
		exit 1
	fi
done

LOG_SOURCE="src/app/device_classes/coffee_scale/brew/brew_log.cpp"
MANAGER_SOURCE="src/app/device_classes/coffee_scale/brew/brew_manager.cpp"
CLASS_SOURCE="src/app/device_classes/coffee_scale/coffee_scale_device_class.cpp"

if ! grep -q 'static bool ensure_brew_log_dir()' "$LOG_SOURCE" \
	|| ! grep -q 'Storage.mkdir(BREW_LOG_DIR)' "$LOG_SOURCE"; then
	echo "FAIL: brew logs must create their storage directory before writing." >&2
	exit 1
fi

if ! grep -q 'if (!ensure_brew_log_dir()) return 0;' "$LOG_SOURCE"; then
	echo "FAIL: brew-log save and import must fail cleanly if their directory is unavailable." >&2
	exit 1
fi

if ! grep -q 'void brew_log_init()' "$LOG_SOURCE" \
	|| ! grep -q 's_prefs.begin(BREW_LOG_NVS_NAMESPACE, false)' "$LOG_SOURCE" \
	|| ! grep -q 'brew_log_init();' "$CLASS_SOURCE"; then
	echo "FAIL: brew logs must initialize their NVS counter during Coffee Scale setup." >&2
	exit 1
fi

if grep -q 'Brew saved to flash' "$MANAGER_SOURCE"; then
	echo "FAIL: brew save success message must not claim flash storage." >&2
	exit 1
fi

if ! grep -q 'const uint16_t saved_id = brew_log_save' "$MANAGER_SOURCE" \
	|| ! grep -q 'if (saved_id)' "$MANAGER_SOURCE"; then
	echo "FAIL: brew manager must report save success only after brew_log_save succeeds." >&2
	exit 1
fi

if ! grep -q 'static bool                 s_waiting_for_tare = false;' "$MANAGER_SOURCE" \
	|| ! grep -q 'if (s_waiting_for_tare)' "$MANAGER_SOURCE" \
	|| ! grep -q 'strcmp(scale_get_status(), "taring") == 0' "$MANAGER_SOURCE"; then
	echo "FAIL: brew auto-start must wait for deferred tare completion." >&2
	exit 1
fi

echo "PASS: brew storage and deferred-tare guards"
