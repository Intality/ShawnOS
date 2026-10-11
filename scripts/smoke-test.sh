#!/usr/bin/env sh
set -eu

iso="${1:-build/myos-x86_64.iso}"
log="${2:-build/smoke-serial.log}"
boot_timeout="${MYOS_SMOKE_TIMEOUT:-60}"

case "$boot_timeout" in
    ''|*[!0-9]*)
        printf 'MYOS_SMOKE_TIMEOUT must be a positive number of seconds.\n' >&2
        exit 2
        ;;
esac
if [ "$boot_timeout" -eq 0 ]; then
    printf 'MYOS_SMOKE_TIMEOUT must be greater than zero.\n' >&2
    exit 2
fi

mkdir -p "$(dirname "$log")"
rm -f "$log"

if ! command -v qemu-system-x86_64 >/dev/null 2>&1; then
    printf 'qemu-system-x86_64 is required for the smoke test.\n' >&2
    exit 127
fi

qemu_pid=''
cleanup() {
    if [ -n "$qemu_pid" ]; then
        kill "$qemu_pid" 2>/dev/null || :
        wait "$qemu_pid" 2>/dev/null || :
    fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

timeout "${boot_timeout}s" qemu-system-x86_64 \
    -M q35 \
    -m 256M \
    -cdrom "$iso" \
    -boot d \
    -display none \
    -serial "file:$log" \
    -no-reboot \
    -no-shutdown &
qemu_pid=$!

while kill -0 "$qemu_pid" 2>/dev/null; do
    if [ -f "$log" ] && grep -q 'MYOS: boot ok v0.1.0' "$log"; then
        printf 'Smoke test passed: kernel reached first boot marker.\n'
        exit 0
    fi
    sleep 1
done

status=0
wait "$qemu_pid" || status=$?
qemu_pid=''

if [ "$status" -ne 0 ] && [ "$status" -ne 124 ]; then
    printf 'QEMU exited unexpectedly with status %s.\n' "$status" >&2
    test -f "$log" && cat "$log" >&2
    exit "$status"
fi

if [ -f "$log" ] && grep -q 'MYOS: boot ok v0.1.0' "$log"; then
    printf 'Smoke test passed: kernel reached first boot marker.\n'
else
    printf 'Smoke test failed: boot marker not found in serial log.\n' >&2
    test -f "$log" && cat "$log" >&2
    exit 1
fi

