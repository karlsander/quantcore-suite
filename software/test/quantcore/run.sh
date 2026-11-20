#!/bin/sh
set -eu
cd "$(dirname "$0")"
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT
src=../../src/quantcore
for test in protocol suite; do
  "${CXX:-c++}" -std=c++17 -O2 -Wall -Wextra -I"$src" \
    "$test.cpp" "$src/pitch.cpp" "$src/core.cpp" "$src/qwire.cpp" "$src/wire.cpp" -o "$build/$test"
  "$build/$test"
done
