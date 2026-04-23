#!/usr/bin/env bash
# tools/bootstrap.sh — prepare third_party/ for a fresh checkout.
#
# Responsibility: after a `git clone`, get the submodules and their
# nested submodules (tinyusb under pico-sdk) populated so `cmake` works.
# Does NOT install toolchains (cmake, ninja, arm-none-eabi-gcc) — warns
# if they're missing but leaves the fix to the developer.

set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_root"

echo "==> FaultyCat v3 bootstrap"
echo "    repo: $repo_root"

echo "==> Fetching top-level submodules (pinned commits; shallow where practical)"
git submodule update --init \
    third_party/pico-sdk \
    third_party/debugprobe \
    third_party/blueTag \
    third_party/free-dap \
    third_party/faultier

# pico-sdk pulls tinyusb, btstack, cyw43-driver, mbedtls, etc. For our
# USB work we only strictly need tinyusb, but pico_sdk_init() references
# every nested module it knows about — just init them all.
echo "==> Fetching nested submodules under pico-sdk"
git -C third_party/pico-sdk submodule update --init --recursive

# Soft toolchain check — report but don't fail.
missing=0
for cmd in cmake ninja arm-none-eabi-gcc; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        echo "WARN: required tool '$cmd' not found in PATH"
        missing=1
    fi
done

if [ "$missing" -eq 1 ]; then
    cat <<'EOF'

Install hints:
  Debian / Ubuntu :  sudo apt install cmake ninja-build gcc-arm-none-eabi
  Fedora          :  sudo dnf install cmake ninja-build arm-none-eabi-gcc-cs
  macOS (brew)    :  brew install cmake ninja && brew install --cask gcc-arm-embedded
  Arch            :  sudo pacman -S cmake ninja arm-none-eabi-gcc arm-none-eabi-newlib

EOF
fi

echo
echo "✓ third_party/ ready"
echo "  Next:  cmake --preset fw-debug && cmake --build build/fw-debug"
echo "  UF2 lands at: build/fw-debug/apps/faultycat_fw/faultycat.uf2"
