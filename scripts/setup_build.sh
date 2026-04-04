#!/usr/bin/env bash

set -euo pipefail

if [[ $# -lt 2 || $# -gt 3 ]]; then
    echo "Usage: $0 <gcc|clang> <debug|release> [builddir]" >&2
    exit 1
fi

compiler="$1"
profile="$2"
builddir="${3:-build/${profile}-${compiler}}"

case "$compiler" in
    gcc)
        cc_bin="gcc"
        cxx_bin="g++"
        ;;
    clang)
        cc_bin="clang"
        cxx_bin="clang++"
        ;;
    *)
        echo "Unsupported compiler '$compiler'. Expected 'gcc' or 'clang'." >&2
        exit 1
        ;;
esac

case "$profile" in
    debug)
        meson_args=(
            "--buildtype=debug"
            "-Db_ndebug=false"
            "-Db_lto=false"
            "-Dnative_optimizations=false"
            "-Dstrip=false"
        )
        ;;
    release)
        meson_args=(
            "--buildtype=release"
            "-Db_ndebug=true"
            "-Db_lto=true"
            "-Dnative_optimizations=true"
            "-Dstrip=true"
        )
        ;;
    *)
        echo "Unsupported profile '$profile'. Expected 'debug' or 'release'." >&2
        exit 1
        ;;
esac

mkdir -p "$(dirname "$builddir")"

if [[ -d "$builddir/meson-private" ]]; then
    env CC="$cc_bin" CXX="$cxx_bin" meson setup "$builddir" --reconfigure "${meson_args[@]}"
else
    env CC="$cc_bin" CXX="$cxx_bin" meson setup "$builddir" "${meson_args[@]}"
fi

echo "Configured $profile build with $compiler in $builddir"
