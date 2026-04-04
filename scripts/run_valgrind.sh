#!/usr/bin/env bash

set -euo pipefail

builddir="${1:-$(pwd)/build/debug-gcc}"
report_dir="${2:-$(pwd)/reports/valgrind}"
shift 2 || true

binary="$builddir/elephantshrew"

if ! command -v valgrind >/dev/null 2>&1; then
    echo "valgrind is not installed. Install it with: sudo apt-get install -y valgrind" >&2
    exit 127
fi

if [[ ! -x "$binary" ]]; then
    echo "Missing executable '$binary'. Configure and build that directory first." >&2
    exit 1
fi

mkdir -p "$report_dir"

report_file="$report_dir/$(basename "$builddir").log"

if [[ $# -eq 0 ]]; then
    set -- -s
fi

LD_LIBRARY_PATH="/usr/local/lib:${LD_LIBRARY_PATH:-}" \
valgrind \
    --tool=memcheck \
    --leak-check=full \
    --show-leak-kinds=all \
    --track-origins=yes \
    --num-callers=30 \
    --log-file="$report_file" \
    "$binary" "$@"

echo "Valgrind report written to $report_file"
