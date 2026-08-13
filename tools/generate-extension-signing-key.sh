#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "Usage: $0 <private-key.pem>" >&2
    exit 2
fi

PRIVATE_KEY=$1
PUBLIC_KEY="${PRIVATE_KEY%.pem}.public.pem"

if [[ -e "$PRIVATE_KEY" || -e "$PUBLIC_KEY" ]]; then
    echo "Refusing to overwrite an existing key file" >&2
    exit 1
fi

mkdir -p "$(dirname "$PRIVATE_KEY")"
umask 077
openssl ecparam -name prime256v1 -genkey -noout -out "$PRIVATE_KEY"
openssl ec -in "$PRIVATE_KEY" -pubout -out "$PUBLIC_KEY"
echo "Generated $PRIVATE_KEY and $PUBLIC_KEY"