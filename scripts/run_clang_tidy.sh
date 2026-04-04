#!/usr/bin/env bash

set -euo pipefail

src_root="${1:-$(pwd)}"
builddir="${2:-$src_root/build/debug-clang}"
report_dir="${3:-$src_root/reports/clang-tidy}"

if ! command -v clang-tidy >/dev/null 2>&1; then
    echo "clang-tidy is not installed. Install it with: sudo apt-get install -y clang-tidy" >&2
    exit 127
fi

if [[ ! -f "$builddir/compile_commands.json" ]]; then
    echo "Missing compile_commands.json in $builddir. Configure and build that directory first." >&2
    exit 1
fi

mkdir -p "$report_dir"
report_file="$report_dir/$(basename "$builddir").txt"

mapfile -t source_files < <(cd "$src_root" && rg --files src -g '*.cpp' | sort)

if [[ ${#source_files[@]} -eq 0 ]]; then
    echo "No source files found under $src_root/src" >&2
    exit 1
fi

: > "$report_file"

status=0
for file in "${source_files[@]}"; do
    echo "=== $file ===" >> "$report_file"
    if ! clang-tidy -p "$builddir" "$src_root/$file" >> "$report_file" 2>&1; then
        status=1
    fi
    echo >> "$report_file"
done

echo "clang-tidy report written to $report_file"
exit "$status"
