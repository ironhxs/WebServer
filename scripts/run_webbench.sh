#!/usr/bin/env bash
set -euo pipefail

URL="${1:-http://localhost:9006/}"
CONC="${2:-10000}"
TIME="${3:-10}"

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WEBBENCH_DIR="$ROOT_DIR/tests/benchmark/webbench-1.5"

if [ ! -d "$WEBBENCH_DIR" ]; then
  echo "webbench source not found at $WEBBENCH_DIR"
  exit 1
fi

cd "$WEBBENCH_DIR"
make
if [ -x "./webbench" ]; then
  ./webbench -c "$CONC" -t "$TIME" "$URL"
elif [ -x "$ROOT_DIR/bin/webbench" ]; then
  "$ROOT_DIR/bin/webbench" -c "$CONC" -t "$TIME" "$URL"
else
  echo "webbench binary not found. Expected ./webbench or $ROOT_DIR/bin/webbench"
  exit 1
fi
