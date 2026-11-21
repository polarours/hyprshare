#!/usr/bin/env bash
# Run the CI matrix locally. Exits non-zero on the first failure, so the
# question "does it pass CI?" can be answered on your machine before pushing.
# Usage: ci/local-ci.sh [debug|release ...]   (default: both)
set -euo pipefail

cd "$(dirname "$0")/.."

if [ "$#" -gt 0 ]; then
  presets=("$@")
else
  presets=(debug release)
fi

for preset in "${presets[@]}"; do
  echo "::: configure $preset"
  cmake --preset "$preset"
  echo "::: build $preset"
  cmake --build --preset "$preset"
  echo "::: test $preset"
  ctest --preset "$preset"
done

echo "all presets passed"
