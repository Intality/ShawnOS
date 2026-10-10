#!/usr/bin/env sh
set -eu

iso="${1:-build/myos-x86_64.iso}"
log="${2:-build/smoke-serial.log}"

mkdir -p "$(dirname "$log")"
rm -f "$log"

if ! command -v qemu-system-x86_64 >/dev/null 2>&1; then
    printf 'qemu-system-x86_64 is required for the smoke test.\n' >&2
    exit 127
fi

set +e
timeout 20s qemu-system-x86_64 \
    -M q35 \
    -m 256M \
    -cdrom "$iso" \
    -boot d \
    -display none \
    -serial "file:$log" \
    -no-reboot \
    -no-shutdown
status=$?
set -e

if [ "$status" -ne 0 ] && [ "$status" -ne 124 ]; then
    printf 'QEMU exited unexpectedly with status %s.\n' "$status" >&2
    test -f "$log" && cat "$log" >&2
    exit "$status"
fi

if grep -q 'MYOS: boot ok v0.1.0' "$log"; then
    printf 'Smoke test passed: kernel reached first boot marker.\n'
else
    printf 'Smoke test failed: boot marker not found in serial log.\n' >&2
    test -f "$log" && cat "$log" >&2
    exit 1
fi

