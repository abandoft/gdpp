#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "Usage: $0 URL DESTINATION" >&2
  exit 2
fi

url="$1"
destination="$2"
partial="${destination}.partial.$$"

cleanup() {
  rm -f "$partial"
}
trap cleanup EXIT

mkdir -p "$(dirname "$destination")"
curl --fail --location --show-error \
  --connect-timeout 30 \
  --retry 5 --retry-all-errors --retry-delay 2 \
  --output "$partial" \
  "$url"
mv -f "$partial" "$destination"
trap - EXIT
