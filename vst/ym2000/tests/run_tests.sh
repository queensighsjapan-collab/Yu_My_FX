#!/bin/bash
# Regression tests against the original HTML version.
#   usage: tests/run_tests.sh /path/to/ms2000-synth.html
# needs: g++, node, python3+numpy; for the FX tests also playwright (Chromium) -> NODE_PATH must point to it.
set -e
HTML=${1:?path to ms2000-synth.html}
cd "$(dirname "$0")"
mkdir -p out
g++ -O2 -std=c++17 -o engine_test engine_test.cpp ../Source/Engine.cpp
for f in scripts/[a-z]*.txt; do
  n=$(basename "$f" .txt)
  node ref_core.js "$HTML" "$f" out/$n.ref.f32
  ./engine_test "$f" out/$n.cpp.f32
  python3 compare.py out/$n.cpp.f32 out/$n.ref.f32 "$n"
done
