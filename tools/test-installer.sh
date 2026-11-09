#!/usr/bin/env bash
# Run the installer's JavaScript unit tests with line coverage (>= 90%) over
# installer/js, excluding the DOM wiring (app.js, restore-app.js).
# Needs Node 22 or newer.
set -euo pipefail
cd "$(dirname "$0")/.."

exec node --test \
  --experimental-test-coverage \
  --test-coverage-include='installer/js/**' \
  --test-coverage-exclude='installer/js/app.js' \
  --test-coverage-exclude='installer/js/restore-app.js' \
  --test-coverage-lines=90 \
  'installer/test/*.test.js'
