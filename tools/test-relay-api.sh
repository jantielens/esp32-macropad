#!/usr/bin/env bash
# Darkroom-timer relay API smoke test.
# Usage: ./tools/test-relay-api.sh <device-ip> [shelly-host] [portal-password]
#   device-ip       IP of the jc4880p433-darkroom device
#   shelly-host     IP/host of your Shelly relay (default 192.168.1.50)
#   portal-password optional web-portal password (HTTP basic auth)
set -euo pipefail

DEV="${1:?usage: test-relay-api.sh <device-ip> [shelly-host] [portal-password]}"
SHELLY="${2:-192.168.1.50}"
PASS="${3:-}"
BASE="http://${DEV}"

AUTH=()
[[ -n "$PASS" ]] && AUTH=(-u "admin:${PASS}")

say() { printf '\n=== %s ===\n' "$1"; }

say "1. GET current relay config"
curl -fsS "${AUTH[@]}" "${BASE}/api/relay" | tee /tmp/relay-before.json; echo

say "2. PUT a 4-slot Shelly config (host=${SHELLY})"
read -r -d '' BODY <<JSON || true
{
  "enlarger_on":   {"type":"shelly","shelly_host":"${SHELLY}","shelly_relay":0,"shelly_on":true},
  "enlarger_off":  {"type":"shelly","shelly_host":"${SHELLY}","shelly_relay":0,"shelly_on":false},
  "safelight_on":  {"type":"shelly","shelly_host":"${SHELLY}","shelly_relay":1,"shelly_on":true},
  "safelight_off": {"type":"shelly","shelly_host":"${SHELLY}","shelly_relay":1,"shelly_on":false}
}
JSON
curl -fsS "${AUTH[@]}" -X PUT -H 'Content-Type: application/json' \
     --data "$BODY" "${BASE}/api/relay"; echo

say "3. GET back — confirm round-trip persisted"
curl -fsS "${AUTH[@]}" "${BASE}/api/relay" | tee /tmp/relay-after.json; echo

say "4. Large-body / chunk-boundary check (pad host to ~2KB of JSON)"
PAD=$(printf 'x%.0s' {1..2000})
curl -fsS "${AUTH[@]}" -X PUT -H 'Content-Type: application/json' \
     --data "{\"enlarger_on\":{\"type\":\"shelly\",\"shelly_host\":\"${SHELLY}\",\"shelly_relay\":0,\"shelly_on\":true,\"_pad\":\"${PAD}\"}}" \
     "${BASE}/api/relay" && echo "  -> accepted (chunked body parsed OK)"

say "5. Oversized-body rejection (expect HTTP 413)"
BIG=$(printf 'x%.0s' {1..5000})
code=$(curl -s -o /dev/null -w '%{http_code}' "${AUTH[@]}" -X PUT \
       -H 'Content-Type: application/json' \
       --data "{\"_pad\":\"${BIG}\"}" "${BASE}/api/relay")
echo "  -> HTTP ${code} (expect 413)"

echo
echo "Smoke test done. Restore your real config from /tmp/relay-before.json if needed:"
echo "  curl ${AUTH:+-u admin:***} -X PUT -H 'Content-Type: application/json' --data @/tmp/relay-before.json ${BASE}/api/relay"
