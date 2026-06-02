#!/usr/bin/env bash
# Run the photoframe site locally against real cloud blob storage.
#
# Prereqs (once):
#   python3 -m venv .venv && source .venv/bin/activate
#   pip install -r requirements.txt
#
# Configure once:
#   cp config.example.json config.local.json   # then edit with your SAS URLs + hashes
#   python3 hash_password.py                    # mint a password_hash to paste in
#
# Then just:
#   ./run_local.sh
#
# This script auto-loads ./config.local.json and persists a dev SECRET_KEY to
# ./.secret_key (gitignored) so restarts don't invalidate your login session.
# Override either by exporting CONFIG_JSON / SECRET_KEY before running.
set -euo pipefail
cd "$(dirname "$0")"

# Default CONFIG_JSON to the local config file if not already set.
if [[ -z "${CONFIG_JSON:-}" ]]; then
  if [[ -f config.local.json ]]; then
    export CONFIG_JSON="$PWD/config.local.json"
  else
    echo "No CONFIG_JSON set and ./config.local.json not found." >&2
    echo "Run: cp config.example.json config.local.json  (then edit it)" >&2
    exit 1
  fi
fi

# Persist a stable dev SECRET_KEY so sessions survive restarts.
if [[ -z "${SECRET_KEY:-}" ]]; then
  if [[ ! -f .secret_key ]]; then
    python3 -c 'import secrets; print(secrets.token_hex(32))' > .secret_key
    chmod 600 .secret_key
  fi
  SECRET_KEY="$(cat .secret_key)"
  export SECRET_KEY
fi

exec uvicorn app:app --host 127.0.0.1 --port 8080 --reload
