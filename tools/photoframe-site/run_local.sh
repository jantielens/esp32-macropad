#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

if [[ -z "${VIRTUAL_ENV:-}" ]]; then
  if [[ ! -d .venv ]]; then
    python3 -m venv .venv
    # shellcheck disable=SC1091
    source .venv/bin/activate
    pip install --upgrade pip
    pip install -r requirements.txt
  else
    # shellcheck disable=SC1091
    source .venv/bin/activate
  fi
fi

export PHOTOFRAME_DATA_DIR="${PHOTOFRAME_DATA_DIR:-$PWD/data}"
exec uvicorn app:app --host 127.0.0.1 --port "${PORT:-8080}" --reload