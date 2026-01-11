#!/usr/bin/env bash
set -euo pipefail

PORT="${1:-9006}"
if [ $# -gt 0 ]; then
  shift
fi

if ! command -v cloudflared >/dev/null 2>&1; then
  echo "cloudflared not found. Install it from https://developers.cloudflare.com/cloudflare-one/connections/connect-apps/"
  exit 1
fi

cloudflared tunnel --url "http://localhost:${PORT}" "$@"
