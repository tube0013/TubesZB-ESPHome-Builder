#!/usr/bin/env bash
set -euo pipefail

# Collect staged manifest YAMLs (compatible with macOS bash 3.x)
files=()
while IFS= read -r file; do
  [ -n "$file" ] && files+=("$file")
done < <(git diff --name-only --cached -- 'manifests/*.yaml')

if [ ${#files[@]} -eq 0 ]; then
  echo "No staged manifest changes; skipping ESPHome validation."
  exit 0
fi

if ! command -v docker >/dev/null 2>&1; then
  echo "Docker is required for ESPHome validation. Install Docker and try again." >&2
  exit 1
fi

echo "Validating ${#files[@]} manifest(s) with ESPHome..."
docker run --rm \
  -v "$PWD":/config \
  -w /config \
  ghcr.io/esphome/esphome \
  esphome config "${files[@]}"
