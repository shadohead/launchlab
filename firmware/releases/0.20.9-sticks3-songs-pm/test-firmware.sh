#!/usr/bin/env bash
# Firmware host suite (no device). See scripts/firmware_tests.py for options:
# --serial for live one-at-a-time output, -k NAME to select, -v for all output.
set -euo pipefail
exec python3 "$(dirname "$0")/firmware_tests.py" "$@"
