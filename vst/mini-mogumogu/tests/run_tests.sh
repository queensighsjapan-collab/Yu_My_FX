#!/bin/bash
# Compare the C++ engine with the browser version's Web Audio graph (rendered offline in Chromium).
#   usage: tests/run_tests.sh /path/to/mini-mogumogu.html
# needs: g++, python3 + numpy + playwright (Chromium)
HTML=${1:?path to mini-mogumogu.html}
cd "$(dirname "$0")"
mkdir -p out
g++ -O2 -std=c++17 -o engine_test engine_test.cpp ../Source/Engine.cpp || exit 1
fail=0
for f in scripts/*.txt; do
  n=$(basename "$f" .txt)
  python3 ref_render.py "$HTML" "$f" out/$n.ref.f32 || exit 1
  ./engine_test "$f" out/$n.cpp.f32
  python3 compare.py out/$n.cpp.f32 out/$n.ref.f32 "$n" || fail=1
done
exit $fail
