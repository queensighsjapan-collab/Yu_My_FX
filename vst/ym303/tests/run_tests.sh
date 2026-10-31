#!/bin/bash
# Regression tests against the browser version (synthesizer/yu-my-303.html).
#   usage: tests/run_tests.sh /path/to/yu-my-303.html
# needs: g++, node, python3+numpy. The sequencer test also needs playwright (Chromium) -> NODE_PATH must point to it.
set -e
HTML=${1:?path to yu-my-303.html}
cd "$(dirname "$0")"
mkdir -p out
g++ -O2 -std=c++17 -o engine_test engine_test.cpp ../Source/Engine.cpp
for f in scripts/*.txt; do
  n=$(basename "$f" .txt)
  node ref_core.js "$HTML" "$f" out/$n.ref.f32
  ./engine_test "$f" out/$n.cpp.f32
  python3 compare.py out/$n.cpp.f32 out/$n.ref.f32 "$n"
done
g++ -O2 -std=c++17 -o seq_test seq_test.cpp ../Source/Sequencer.cpp ../Source/Bank.cpp
node ref_seq.js "$HTML" out/seq.ref.json
./seq_test out/seq.ref.json
