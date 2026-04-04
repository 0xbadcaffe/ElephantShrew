#!/usr/bin/env bash

set -euo pipefail

src_root="${1:-$(pwd)}"
builddir="${2:-$src_root/build/debug-gcc}"
report_dir="${3:-$src_root/reports/cppcheck}"

if ! command -v cppcheck >/dev/null 2>&1; then
    echo "cppcheck is not installed. Install it with: sudo apt-get install -y cppcheck" >&2
    exit 127
fi

if [[ ! -f "$builddir/compile_commands.json" ]]; then
    echo "Missing compile_commands.json in $builddir. Configure and build that directory first." >&2
    exit 1
fi

mkdir -p "$report_dir"

text_report="$report_dir/$(basename "$builddir").txt"
xml_report="$report_dir/$(basename "$builddir").xml"

cppcheck_args=(
    "--project=$builddir/compile_commands.json"
    "--enable=all"
    "--check-level=exhaustive"
    "--inconclusive"
    "--std=c++20"
    "--inline-suppr"
    "--suppress=missingIncludeSystem"
)

cppcheck "${cppcheck_args[@]}" --output-file="$text_report"
cppcheck "${cppcheck_args[@]}" --xml --xml-version=2 1>/dev/null 2>"$xml_report"

echo "cppcheck text report written to $text_report"
echo "cppcheck XML report written to $xml_report"
