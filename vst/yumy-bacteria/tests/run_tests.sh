#!/bin/bash
# Regression tests: C++ engine vs the JavaScript original.
#   usage: tests/run_tests.sh /path/to/yumy-bacteria.html
# needs: g++, node, python3+numpy
HTML=${1:?path to yumy-bacteria.html}
cd "$(dirname "$0")"
mkdir -p out
g++ -O2 -std=c++17 -ffp-contract=off -o engine_test engine_test.cpp ../Source/Engine.cpp || exit 1
fail=0
for f in scripts/*.txt; do
  n=$(basename "$f" .txt)
  node ref_core.js "$HTML" "$f" out/$n.ref.f32 || { echo "FAIL ref $n"; fail=1; continue; }
  ./engine_test "$f" out/$n.cpp.f32 || { echo "FAIL cpp $n"; fail=1; continue; }
  python3 compare.py out/$n.cpp.f32 out/$n.ref.f32 "$n" || { echo "FAIL $n"; fail=1; }
done
exit $fail
