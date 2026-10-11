#!/usr/bin/env sh
set -eu

missing=0

need() {
    if ! command -v "$1" >/dev/null 2>&1; then
        printf 'missing: %s\n' "$1" >&2
        missing=1
    fi
}

need make
need curl
need tar
need gunzip
need timeout
need xorriso
need qemu-system-x86_64
need python3
need c++

if command -v clang++ >/dev/null 2>&1; then
    :
elif command -v x86_64-elf-g++ >/dev/null 2>&1; then
    :
else
    printf 'missing: clang++ or x86_64-elf-g++\n' >&2
    missing=1
fi

if [ "$missing" -ne 0 ]; then
    printf '\nInstall the missing tools, then rerun make.\n' >&2
    exit 1
fi

printf 'Tool check passed.\n'

