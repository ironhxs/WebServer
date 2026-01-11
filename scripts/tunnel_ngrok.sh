#!/usr/bin/env bash
set -euo pipefail

PORT="${1:-9006}"

if ! command -v ngrok >/dev/null 2>&1; then
  echo "ngrok not found. Install it from https://ngrok.com/ and run:"
  echo "  ngrok config add-authtoken <YOUR_TOKEN>"
  exit 1
fi

ngrok http "${PORT}"
