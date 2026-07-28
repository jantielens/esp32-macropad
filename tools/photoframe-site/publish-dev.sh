#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
cd "$script_dir"

tag="${1:-dev}"
image="${PHOTOFRAME_IMAGE:-ghcr.io/jantielens/epaper-photoframe-site}:$tag"

if ! command -v docker >/dev/null 2>&1; then
  echo "Docker is not installed." >&2
  exit 1
fi

docker_error="$(docker info 2>&1 >/dev/null || true)"
if [[ -n "$docker_error" ]]; then
  if [[ "$docker_error" == *"permission denied"* ]]; then
    echo "Docker is running, but this shell cannot access it." >&2
    echo "Restart VS Code after joining the docker group, or run:" >&2
    echo "  sg docker -c './publish-dev.sh $tag'" >&2
  else
    printf 'Docker is unavailable:\n%s\n' "$docker_error" >&2
  fi
  exit 1
fi

echo "Building $image for linux/amd64..."
docker build \
  --platform linux/amd64 \
  --label org.opencontainers.image.source=https://github.com/jantielens/esp32-macropad \
  --tag "$image" \
  .

echo "Pushing $image..."
if ! docker push "$image"; then
  cat >&2 <<'EOF'

Push failed. Authenticate in this terminal with a GitHub personal access token
that has write:packages, then rerun the script:
  docker login ghcr.io -u jantielens
EOF
  exit 1
fi

echo
echo "Published $image"