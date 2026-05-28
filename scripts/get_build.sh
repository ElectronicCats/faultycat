#!/usr/bin/env bash
#
# scripts/get_build.sh — pico-sdk equivalent of Minino's get_build.sh.
#
# Bumps the version literals in the source tree to match the requested
# tag, then builds the FaultyCat firmware (RP2040 UF2 + ELF) and the
# host CLI/TUI Python distributions, leaving every release-shaped
# artifact under `build/apps/faultycat_fw/` (covered by the existing
# `/**/build/` gitignore entry).
#
# Output layout (all under `build/apps/faultycat_fw/`):
#
#   faultycat_vX.Y.Z.W.uf2         drag onto the BOOTSEL volume to flash
#   faultycmd-X.Y.Z.W-py3-none-any.whl   PEP 440 forbids `v` in wheel names,
#   faultycmd-X.Y.Z.W.tar.gz             so the host pkgs keep the bare number
#
# Invocation:
#   ./scripts/get_build.sh vX.Y.Z.W
#   ./scripts/get_build.sh vX.Y.Z.W-rc1
#   ./scripts/get_build.sh                 (parses CMakeLists.txt as a fallback)
#
# WHAT THE SCRIPT MODIFIES (intentionally):
#   - CMakeLists.txt                                  project(VERSION X.Y.Z.W)
#   - host/faultycmd-py/pyproject.toml                version = "X.Y.Z.W"
#   - host/faultycmd-py/src/faultycmd/__init__.py     __version__ = "X.Y.Z.W"
#
# This is the same defensive bump the CI workflow used to perform inline;
# running it here means a local `./scripts/get_build.sh v3.0.0.5` actually
# burns "v3.0.0.5" into the firmware's `firmware_version.h` (via CMake's
# configure_file from `project(VERSION ...)`) instead of building with
# whatever the working tree was carrying. If you want to keep your tree
# untouched, run the script in a fresh git worktree or revert with
# `git checkout -- CMakeLists.txt host/faultycmd-py/...` afterwards.
#
# Prereqs the caller MUST install before running this:
#   - cmake, ninja, gcc-arm-none-eabi, libnewlib-arm-none-eabi,
#     libstdc++-arm-none-eabi-newlib  (firmware UF2)
#   - python3, `pip install build`                                  (sdist + wheel)
#
# Run from the repo root.

set -euo pipefail

# -----------------------------------------------------------------------------
# Resolve version + tag
# -----------------------------------------------------------------------------

INPUT_ARG="${1:-}"

if [ -z "${INPUT_ARG}" ]; then
    # Fallback: parse the existing `    VERSION X.Y.Z.W` line from
    # CMakeLists.txt and add the `v` prefix back so the output naming
    # is consistent with CI invocations.
    PARSED=$(grep -oE '^[[:space:]]*VERSION[[:space:]]+[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+' \
                  CMakeLists.txt \
              | head -1 \
              | awk '{print $2}')
    INPUT_ARG="v${PARSED}"
fi

# Normalise: TAG always carries the leading `v`, VERSION never does.
# Accept both `v3.0.0.0` and `3.0.0.0` inputs.
TAG="${INPUT_ARG}"
[[ "${TAG}" =~ ^v ]] || TAG="v${TAG}"
VERSION="${TAG#v}"
# Pre-release suffix (e.g. `-rc1`) is allowed in tag/filename but not
# in the numeric CMake / pyproject literal.
VERSION_NUMERIC="${VERSION%%-*}"

if [[ ! "${VERSION_NUMERIC}" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "ERROR: input '${INPUT_ARG}' does not parse as v?MAJOR.MINOR.PATCH.TWEAK[-suffix]" >&2
    echo "Usage: $0 vX.Y.Z.W[-suffix]" >&2
    exit 1
fi

echo "==> Building FaultyCat release ${TAG}"
echo "    Numeric version for code literals: ${VERSION_NUMERIC}"

# -----------------------------------------------------------------------------
# Bump version literals in the source tree
#
# CMake re-reads `project(VERSION ...)` on every configure, so editing
# CMakeLists.txt + re-running `cmake --preset fw-release` is enough to
# propagate ${VERSION_NUMERIC} into firmware_version.h (via
# configure_file), the USB bcdDevice (via FW_VERSION_BCD), the PING
# reply bytes on CDC0/CDC1, and the `SHELL: VERSION ...` reply on CDC2.
# pyproject.toml + __init__.py keep the host CLI/TUI in lock-step.
# -----------------------------------------------------------------------------

echo "==> Bumping version literals to ${VERSION_NUMERIC}"
sed -i -E \
    "s/^([[:space:]]*VERSION[[:space:]]+)[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+/\\1${VERSION_NUMERIC}/" \
    CMakeLists.txt
sed -i -E \
    "s/^(version = )\"[^\"]+\"/\\1\"${VERSION_NUMERIC}\"/" \
    host/faultycmd-py/pyproject.toml
sed -i -E \
    "s/^(__version__ = )\"[^\"]+\"/\\1\"${VERSION_NUMERIC}\"/" \
    host/faultycmd-py/src/faultycmd/__init__.py

echo "    CMakeLists.txt:        $(grep -E '^\s*VERSION\s+[0-9]' CMakeLists.txt | head -1 | xargs)"
echo "    pyproject.toml:        $(grep '^version' host/faultycmd-py/pyproject.toml | xargs)"
echo "    __init__.py:           $(grep '^__version__' host/faultycmd-py/src/faultycmd/__init__.py | xargs)"

# -----------------------------------------------------------------------------
# Output directory under build/ (gitignored)
# -----------------------------------------------------------------------------

OUT_DIR="build/apps/faultycat_fw"
mkdir -p "${OUT_DIR}"

# -----------------------------------------------------------------------------
# Firmware (RP2040 UF2 + ELF)
# -----------------------------------------------------------------------------

echo "==> Configuring firmware (cmake --preset fw-release)"
# Re-configure so PROJECT_VERSION is re-read from the freshly-bumped
# CMakeLists.txt; this is what regenerates firmware_version.h with the
# matching FW_VERSION_STR / FW_VERSION_BCD / FW_VERSION_{MAJOR,...}.
cmake --preset fw-release

echo "==> Building firmware"
cmake --build build/fw-release --parallel

echo "==> Inspecting firmware artifact"
ls -la build/fw-release/apps/faultycat_fw/
# `arm-none-eabi-size` reads section sizes from the ELF — useful CI
# diagnostic even though we no longer ship the ELF as a release asset
# (it just confirmed the image is sane before we copy the UF2).
arm-none-eabi-size build/fw-release/apps/faultycat_fw/faultycat.elf

echo "==> Publishing firmware artifact as ${OUT_DIR}/faultycat_${TAG}.uf2"
cp build/fw-release/apps/faultycat_fw/faultycat.uf2 \
   "${OUT_DIR}/faultycat_${TAG}.uf2"

# -----------------------------------------------------------------------------
# Host CLI/TUI (sdist + wheel). PEP 440 forbids the `v` prefix in
# Python distribution filenames, so the wheel/sdist keep the bare
# numeric version (e.g. `faultycmd-3.0.0.1-py3-none-any.whl`). They
# land in the same OUT_DIR so the GitHub Release attaches everything
# in a single upload step.
# -----------------------------------------------------------------------------

echo "==> Building host Python sdist + wheel"
ABS_OUT_DIR="$(realpath "${OUT_DIR}")"
(
    cd host/faultycmd-py
    python -m build --sdist --wheel --outdir "${ABS_OUT_DIR}"
)

# -----------------------------------------------------------------------------
# Show what we made
# -----------------------------------------------------------------------------

echo "==> Done. Contents of ${OUT_DIR}/:"
ls -la "${OUT_DIR}/"
