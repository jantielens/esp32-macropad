#!/usr/bin/env bash
# Portal API integration tests — curl-based HTTP smoke tests for ESP32 web portal.
# Validates shell page, nav API, fragment endpoints, component config CRUD,
# CORS preflight, static assets, and error handling against a live device.
#
# Usage:
#   ./tools/test-portal-api.sh [--user USER:PASS] [DEVICE_IP]
#   DEVICE_IP=192.168.1.87 ./tools/test-portal-api.sh
#   PORTAL_AUTH=admin:pass ./tools/test-portal-api.sh 192.168.1.87
#
# Exit code: 0 if all tests pass, 1 on any failure, 2 on usage error.

set -euo pipefail

TIMEOUT=10
PASS_COUNT=0
FAIL_COUNT=0

# Temp files for curl output
BODY_FILE=$(mktemp)
BODY_FILE2=$(mktemp)
HDR_FILE=$(mktemp)
trap 'rm -f "$BODY_FILE" "$BODY_FILE2" "$HDR_FILE"' EXIT

# Defaults from environment
DEVICE_IP="${DEVICE_IP:-}"
AUTH_CREDS="${PORTAL_AUTH:-}"

# Parse arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        --user)
            [[ $# -lt 2 ]] && { echo "Error: --user requires USER:PASS argument" >&2; exit 2; }
            AUTH_CREDS="$2"
            shift 2
            ;;
        -*)
            echo "Unknown option: $1" >&2
            echo "Usage: $0 [--user USER:PASS] [DEVICE_IP]" >&2
            exit 2
            ;;
        *)
            DEVICE_IP="$1"
            shift
            ;;
    esac
done

if [[ -z "$DEVICE_IP" ]]; then
    echo "Usage: $0 [--user USER:PASS] DEVICE_IP" >&2
    echo "  or set DEVICE_IP environment variable" >&2
    exit 2
fi

BASE="http://${DEVICE_IP}"

# Build common curl options
CURL_OPTS=(-s --compressed --max-time "$TIMEOUT")
if [[ -n "$AUTH_CREDS" ]]; then
    CURL_OPTS+=(-u "$AUTH_CREDS")
fi

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

pass() {
    PASS_COUNT=$((PASS_COUNT + 1))
    echo "✓ $1"
}

fail() {
    FAIL_COUNT=$((FAIL_COUNT + 1))
    if [[ -n "${2:-}" ]]; then
        echo "✗ $1 ($2)"
    else
        echo "✗ $1"
    fi
}

# Perform a curl request and return the HTTP status code.
# On connection failure, returns "000".
do_curl() {
    local code
    code=$(curl "${CURL_OPTS[@]}" -w "%{http_code}" "$@" 2>/dev/null) || code="000"
    # curl -w prints the status after any body written to stdout;
    # since we redirect body to -o file, $code should just be the 3-digit code.
    echo "$code"
}

# GET request — writes body to BODY_FILE, headers to HDR_FILE, prints status code
http_get() {
    do_curl -o "$BODY_FILE" -D "$HDR_FILE" "${BASE}$1"
}

# GET request — writes body to BODY_FILE2 (for comparison), prints status code
http_get2() {
    do_curl -o "$BODY_FILE2" -D "$HDR_FILE" "${BASE}$1"
}

# POST request with body — prints status code
http_post() {
    do_curl -o "$BODY_FILE" -D "$HDR_FILE" \
        -X POST -H "Content-Type: application/json" \
        -d "$2" "${BASE}$1"
}

# DELETE request — prints status code
http_delete() {
    do_curl -o "$BODY_FILE" -D "$HDR_FILE" \
        -X DELETE "${BASE}$1"
}

# OPTIONS request — prints status code
http_options() {
    do_curl -o "$BODY_FILE" -D "$HDR_FILE" \
        -X OPTIONS -H "Origin: https://example.com" \
        "${BASE}$1"
}

# Check if a header is present (case-insensitive)
header_present() {
    grep -qi "$1" "$HDR_FILE" 2>/dev/null
}

# ---------------------------------------------------------------------------
# Connectivity check
# ---------------------------------------------------------------------------

echo "Starting portal API integration tests against ${DEVICE_IP}..."
echo ""

status=$(http_get "/")
if [[ "$status" == "000" ]]; then
    echo "✗ Cannot connect to ${DEVICE_IP} — aborting"
    exit 1
fi

# ---------------------------------------------------------------------------
# Test 1: Shell page
# ---------------------------------------------------------------------------

# status already captured from connectivity check
if [[ "$status" == "200" ]] && header_present "text/html"; then
    pass "Shell page (GET /)"
else
    fail "Shell page (GET /)" "status=$status"
fi

# ---------------------------------------------------------------------------
# Test 2: Nav API
# ---------------------------------------------------------------------------

status=$(http_get "/api/portal/nav")
if [[ "$status" == "200" ]] && python3 -c "import json,sys; d=json.load(sys.stdin); assert isinstance(d.get('categories'),list)" < "$BODY_FILE" 2>/dev/null; then
    pass "Nav API (GET /api/portal/nav)"
else
    fail "Nav API (GET /api/portal/nav)" "status=$status"
fi

# ---------------------------------------------------------------------------
# Test 3: Fragment endpoints (known)
# ---------------------------------------------------------------------------

FRAGMENTS=(
    wifi device-name network mode brightness pad-editor
    button-defaults swipe-actions boot-actions timers mqtt ble
    volume screensaver sensor-data version-info ota-update
    manual-upload ha-discovery welcome thresholds sounds
)

frag_pass=0
frag_fail=0
frag_fail_ids=()

for frag in "${FRAGMENTS[@]}"; do
    status=$(http_get "/api/section/${frag}")
    if [[ "$status" == "200" ]]; then
        frag_pass=$((frag_pass + 1))
    else
        frag_fail=$((frag_fail + 1))
        frag_fail_ids+=("${frag}:${status}")
    fi
done

if [[ $frag_fail -eq 0 ]]; then
    pass "Fragment endpoints (${frag_pass} fragments)"
else
    fail "Fragment endpoints (${frag_pass}/${#FRAGMENTS[@]} OK)" "${frag_fail_ids[*]}"
fi

# ---------------------------------------------------------------------------
# Test 4: Fragment unknown → 404
# ---------------------------------------------------------------------------

status=$(http_get "/api/section/nonexistent")
if [[ "$status" == "404" ]]; then
    pass "Fragment unknown (GET /api/section/nonexistent → 404)"
else
    fail "Fragment unknown" "expected 404, got $status"
fi

# ---------------------------------------------------------------------------
# Test 5: Component config GET
# ---------------------------------------------------------------------------

# Components known to have get_config handlers (all HAS_DISPLAY gated)
CONFIG_COMPONENTS=(boot-actions swipe-actions timers button-defaults)

cfg_pass=0
cfg_fail=0
cfg_fail_ids=()

for comp in "${CONFIG_COMPONENTS[@]}"; do
    status=$(http_get "/api/component/${comp}/config")
    if [[ "$status" == "200" ]]; then
        cfg_pass=$((cfg_pass + 1))
    else
        cfg_fail=$((cfg_fail + 1))
        cfg_fail_ids+=("${comp}:${status}")
    fi
done

if [[ $cfg_fail -eq 0 ]]; then
    pass "Component config GET (${cfg_pass} components)"
else
    fail "Component config GET (${cfg_pass}/${#CONFIG_COMPONENTS[@]} OK)" "${cfg_fail_ids[*]}"
fi

# ---------------------------------------------------------------------------
# Test 6: Component config round-trip (POST then GET, verify match)
# ---------------------------------------------------------------------------

ROUNDTRIP_COMPONENTS=(boot-actions swipe-actions)
rt_pass=0
rt_fail=0
rt_fail_ids=()

for comp in "${ROUNDTRIP_COMPONENTS[@]}"; do
    # Step 1: GET current config
    status=$(http_get "/api/component/${comp}/config")
    if [[ "$status" != "200" ]]; then
        rt_fail=$((rt_fail + 1))
        rt_fail_ids+=("${comp}:GET1=${status}")
        continue
    fi
    original=$(cat "$BODY_FILE")

    # Step 2: POST the same config back
    status=$(http_post "/api/component/${comp}/config" "$original")
    if [[ "$status" != "200" ]]; then
        rt_fail=$((rt_fail + 1))
        rt_fail_ids+=("${comp}:POST=${status}")
        continue
    fi

    # Step 3: GET config again
    status=$(http_get2 "/api/component/${comp}/config")
    if [[ "$status" != "200" ]]; then
        rt_fail=$((rt_fail + 1))
        rt_fail_ids+=("${comp}:GET2=${status}")
        continue
    fi

    # Step 4: Compare (normalize JSON to handle key ordering)
    before=$(python3 -c "import json,sys; print(json.dumps(json.load(sys.stdin),sort_keys=True))" < "$BODY_FILE2" 2>/dev/null || echo "ERR")
    after_orig=$(python3 -c "import json,sys; print(json.dumps(json.load(sys.stdin),sort_keys=True))" <<< "$original" 2>/dev/null || echo "ERR")

    if [[ "$before" == "$after_orig" && "$before" != "ERR" ]]; then
        rt_pass=$((rt_pass + 1))
    else
        rt_fail=$((rt_fail + 1))
        rt_fail_ids+=("${comp}:mismatch")
    fi
done

if [[ $rt_fail -eq 0 && $rt_pass -ge 2 ]]; then
    pass "Config round-trip (${rt_pass} components)"
elif [[ $rt_fail -eq 0 ]]; then
    pass "Config round-trip (${rt_pass} component(s))"
else
    fail "Config round-trip (${rt_pass}/${#ROUNDTRIP_COMPONENTS[@]} OK)" "${rt_fail_ids[*]}"
fi

# ---------------------------------------------------------------------------
# Test 7: CORS preflight
# ---------------------------------------------------------------------------

status=$(http_options "/api/component/test/config")
if [[ "$status" == "200" || "$status" == "204" ]]; then
    # Check for CORS headers
    if header_present "Access-Control-Allow-Origin" && \
       header_present "Access-Control-Allow-Methods" && \
       header_present "Access-Control-Allow-Headers"; then
        pass "CORS preflight (OPTIONS /api/component/test/config)"
    else
        fail "CORS preflight" "status=$status but missing Access-Control headers"
    fi
else
    fail "CORS preflight" "expected 200 or 204, got $status"
fi

# ---------------------------------------------------------------------------
# Test 8: DELETE config
# ---------------------------------------------------------------------------

# No component currently has delete_config — test the 404 paths
del_ok=true

# Unknown component → 404
status=$(http_delete "/api/component/nonexistent/config")
if [[ "$status" != "404" ]]; then
    del_ok=false
fi

# Known component without delete handler → 404
status=$(http_delete "/api/component/wifi/config")
if [[ "$status" != "404" ]]; then
    del_ok=false
fi

if [[ "$del_ok" == true ]]; then
    pass "DELETE config (404 for unknown and no-handler components)"
else
    fail "DELETE config" "unexpected status code(s)"
fi

# ---------------------------------------------------------------------------
# Test 9: Custom action dispatch
# ---------------------------------------------------------------------------

# Display component has custom actions: wake (POST), sleep (GET/POST), etc.
status=$(http_post "/api/component/display/wake" "{}")
if [[ "$status" == "200" || "$status" == "204" ]]; then
    pass "Custom action dispatch (POST /api/component/display/wake)"
elif [[ "$status" == "404" ]]; then
    # Display component may not be registered on this build
    fail "Custom action dispatch" "display component not found (status=404)"
else
    fail "Custom action dispatch" "status=$status"
fi

# ---------------------------------------------------------------------------
# Test 10: Unknown component config → 404
# ---------------------------------------------------------------------------

status=$(http_get "/api/component/nonexistent/config")
if [[ "$status" == "404" ]]; then
    pass "Unknown component (GET /api/component/nonexistent/config → 404)"
else
    fail "Unknown component" "expected 404, got $status"
fi

# ---------------------------------------------------------------------------
# Test 11: Malformed POST (no crash — no 500 errors)
# ---------------------------------------------------------------------------

# Target: a component with save_config_body
MALFORM_TARGET="boot-actions"
malform_ok=true

# 11a: Empty body (501 = AsyncWebServer "no handler" default, acceptable)
status=$(http_post "/api/component/${MALFORM_TARGET}/config" "")
if [[ "$status" == "500" ]]; then
    malform_ok=false
fi

# 11b: Invalid JSON
status=$(http_post "/api/component/${MALFORM_TARGET}/config" "not-json")
if [[ "$status" == "500" ]]; then
    malform_ok=false
fi

# 11c: Oversized body (generate >4KB of data — the default max_size)
oversized=$(python3 -c "print('{\"x\":\"' + 'A'*5000 + '\"}')")
status=$(http_post "/api/component/${MALFORM_TARGET}/config" "$oversized")
if [[ "$status" == "500" ]]; then
    malform_ok=false
fi

if [[ "$malform_ok" == true ]]; then
    pass "Malformed POST (empty, invalid JSON, oversized — no 500s)"
else
    fail "Malformed POST" "server returned 5xx"
fi

# ---------------------------------------------------------------------------
# Test 12: Static assets
# ---------------------------------------------------------------------------

ASSETS=("/portal.js" "/bootstrap.min.css" "/portal-custom.css")
asset_pass=0
asset_fail=0
asset_fail_ids=()

for asset in "${ASSETS[@]}"; do
    status=$(http_get "$asset")
    if [[ "$status" == "200" ]]; then
        asset_pass=$((asset_pass + 1))
    else
        asset_fail=$((asset_fail + 1))
        asset_fail_ids+=("${asset}:${status}")
    fi
done

if [[ $asset_fail -eq 0 ]]; then
    pass "Static assets (${asset_pass} files)"
else
    fail "Static assets (${asset_pass}/${#ASSETS[@]} OK)" "${asset_fail_ids[*]}"
fi

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------

echo ""
TOTAL=$((PASS_COUNT + FAIL_COUNT))
echo "Passed: ${PASS_COUNT}, Failed: ${FAIL_COUNT} (${TOTAL} tests)"

if [[ $FAIL_COUNT -gt 0 ]]; then
    exit 1
fi
