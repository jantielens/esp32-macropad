#!/usr/bin/env bash
# Deploy the photoframe site to an existing Azure App Service (Linux, Python 3.11).
#
# Prereqs:
#   - Azure CLI installed and logged in:  az login
#   - You already created the Web App (Linux, runtime Python 3.11).
#   - ./config.local.json is filled in (its contents become the CONFIG_JSON setting).
#
# Usage:
#   deploy/azure-deploy.sh <resource-group> <app-name>
#
# What it does (idempotent):
#   1. Sets the startup command (uvicorn on $PORT).
#   2. Sets app settings: CONFIG_JSON (from config.local.json), SECRET_KEY,
#      COOKIE_SECURE=1, SCM_DO_BUILD_DURING_DEPLOYMENT=true.
#   3. Zip-deploys this folder (Oryx runs pip install -r requirements.txt).
#
# Re-run any time to push an update. Secrets live only in app settings, never in git.
set -euo pipefail
cd "$(dirname "$0")/.."   # -> tools/photoframe-site

RG="${1:?Usage: azure-deploy.sh <resource-group> <app-name>}"
APP="${2:?Usage: azure-deploy.sh <resource-group> <app-name>}"

if [[ ! -f config.local.json ]]; then
  echo "config.local.json not found; fill it in first (cp config.example.json config.local.json)." >&2
  exit 1
fi

command -v az >/dev/null || { echo "Azure CLI (az) not found. Install it and run 'az login'." >&2; exit 1; }

# Compact the config to a single-line JSON string for the app setting.
CONFIG_JSON_VALUE="$(python3 -c 'import json,sys; print(json.dumps(json.load(open("config.local.json")),separators=(",",":")))')"

# Reuse an existing SECRET_KEY if one is already set on the app; otherwise mint one
# (a stable key keeps login sessions valid across restarts/redeploys).
EXISTING_SECRET="$(az webapp config appsettings list -g "$RG" -n "$APP" \
  --query "[?name=='SECRET_KEY'].value | [0]" -o tsv 2>/dev/null || true)"
if [[ -z "$EXISTING_SECRET" || "$EXISTING_SECRET" == "None" ]]; then
  SECRET_KEY_VALUE="$(python3 -c 'import secrets; print(secrets.token_hex(32))')"
  echo "Generated a new SECRET_KEY."
else
  SECRET_KEY_VALUE="$EXISTING_SECRET"
  echo "Reusing existing SECRET_KEY."
fi

echo "==> Setting startup command"
az webapp config set -g "$RG" -n "$APP" \
  --startup-file 'python -m uvicorn app:app --host 0.0.0.0 --port $PORT' >/dev/null

echo "==> Setting app settings"
az webapp config appsettings set -g "$RG" -n "$APP" --settings \
  "CONFIG_JSON=$CONFIG_JSON_VALUE" \
  "SECRET_KEY=$SECRET_KEY_VALUE" \
  "COOKIE_SECURE=1" \
  "SCM_DO_BUILD_DURING_DEPLOYMENT=true" >/dev/null

echo "==> Zip-deploying code"
TMP_ZIP="$(mktemp --suffix=.zip)"
trap 'rm -f "$TMP_ZIP"' EXIT
# mktemp leaves a 0-byte file behind; zip would treat it as a corrupt archive to
# update, so remove it and let zip create a fresh archive.
rm -f "$TMP_ZIP"
# Package only what the app needs; exclude local-only secrets and caches.
zip -r -q "$TMP_ZIP" . \
  -x '*.pyc' -x '*/__pycache__/*' -x '.venv/*' \
  -x 'config.local.json' -x '.secret_key' -x 'deploy/*' -x '.git/*'

az webapp deploy -g "$RG" -n "$APP" --src-path "$TMP_ZIP" --type zip --track-status false >/dev/null

URL="https://$(az webapp show -g "$RG" -n "$APP" --query defaultHostName -o tsv)"
echo "==> Done. App: $URL"
echo "    Device pull URL: $URL/api/next?device_id=E1003-1&key=<api_key>"
