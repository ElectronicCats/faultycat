# FaultyCat releases — versioning, build, distribution

This document describes how the FaultyCat firmware (RP2040 UF2) and
the matching host package (`faultycmd` CLI + TUI) are versioned,
built, and shipped. It covers:

  - the version scheme used by the project (`vX.Y.Z.W`),
  - where the version lives in the source tree and how it propagates
    into the running firmware,
  - the wire-protocol fields the host uses to check parity with the
    flashed firmware,
  - how to cut a release as a maintainer,
  - how to install a specific release as an end user.

---

## Version scheme

Releases are tagged `vMAJOR.MINOR.PATCH.TWEAK`. Four numeric
segments, mandatory `v` prefix on the git tag. Pre-release variants
append a hyphen-suffix (`v3.0.0.0-rc1`).

| Segment | Bumped when… |
|---|---|
| MAJOR | Incompatible wire-protocol change (PING reply shape, opcode renumbering, CDC interface re-layout). Operators must re-flash + reinstall together. |
| MINOR | New protocol command or new host subcommand that the firmware also gained; remains backwards-compatible at the *transport* level (old host can still PING new firmware) but the host's Exact-match gate refuses the pairing anyway. |
| PATCH | Bug fix or polish that does not add or remove protocol surface. |
| TWEAK | Reserved for hotfixes-of-patch and for distinguishing identical-looking re-spins (e.g. a CI re-run that produced a different binary). |

The Exact-match policy below means the host and firmware must agree
on **all four** segments. The four-segment shape gives a release the
flexibility to ship a host-only patch or a firmware-only patch
without bumping the user-visible MAJOR/MINOR.

Wheel and sdist filenames carry the bare semver per PEP 440
(`faultycmd-3.0.0.0-py3-none-any.whl`); the `v` prefix is preserved
in everything user-facing (git tag, UF2 filename, GitHub release
title, artifact name in CI).

## Where the version lives in the source tree

Three places carry the version literal. The release workflow
(`.github/workflows/release.yml`) and the local helper
(`scripts/get_build.sh`) both rewrite all three on every tag.

| File | Literal | Constraint |
|---|---|---|
| `CMakeLists.txt` | `project(faultycat VERSION X.Y.Z.W ...)` | CMake's `project()` accepts up to 4 numeric components, rejects the `v` prefix and pre-release suffix. |
| `host/faultycmd-py/pyproject.toml` | `version = "X.Y.Z.W"` | PEP 440 accepts a 4-segment release version, rejects the `v` prefix. |
| `host/faultycmd-py/src/faultycmd/__init__.py` | `__version__ = "X.Y.Z.W"` | Mirrored from pyproject so `faultycmd.__version__` is reachable without importing `importlib.metadata`. |

CMake's `project(VERSION ...)` is the single source of truth on the
firmware side. The top-level CMakeLists.txt then runs
`configure_file(cmake/firmware_version.h.in ...)` to generate
`firmware_version.h`:

```c
#define FW_VERSION_MAJOR  3u
#define FW_VERSION_MINOR  0u
#define FW_VERSION_PATCH  0u
#define FW_VERSION_TWEAK  0u
#define FW_VERSION_STR    "3.0.0.0"
#define FW_VERSION_BCD    /* packed 0xMMmp for USB bcdDevice */
```

The generated header is exposed as a CMake `INTERFACE` library
(`firmware_version`) that targets link against to pull the include
path in. Linking `firmware_version` is the only thing a new
firmware-side consumer has to do — no copy-pasted literals, no
sdkconfig-style indirection.

## How the firmware advertises its version to the host

The host has three ways to ask the firmware which version it is
running. Each is consumed by a different protocol client:

### PING reply (CDC0 EMFI / CDC1 Crowbar)

The PING handler in `services/host_proto/{emfi,crowbar}_proto/*.c`
embeds the four version bytes directly in the reply payload:

```
SOF  CMD|0x80  LEN_LE     payload (6 bytes)              CRC_LE
0xFA 0x81      0x06 0x00  'F' family MAJ MIN PATCH TWEAK CC CC
```

`family` is `'4'` for emfi_proto and `'5'` for crowbar_proto so a
host that ever sees a stray reply on the wrong CDC can detect the
mismatch. Firmware that predates this change replies with the legacy
4-byte payload (`'F', family, 0, 0`); the host detects the short
reply and surfaces it as a distinct `<pre-versioning>` mismatch
instead of treating the trailing zeros as `0.0.0.0`.

### CDC2 shell `version` command

The text shell on the scanner CDC accepts `version` and replies:

```
SHELL: VERSION 3.0.0.0
```

This is what the `ScannerClient` probes on connect — it never sends
the binary PING because the scanner CDC speaks the line-oriented
shell protocol.

### Diag banner

Every operator that connects to the scanner CDC (`picocom`, `screen`,
or the TUI's diag tail) sees a banner that includes the version
line:

```
========================================
FaultyCat v3 — F8 diag (composite scanner CDC + unified shell)
Firmware version: 3.0.0.0
========================================
```

Useful for an at-a-glance check without typing anything.

### USB bcdDevice

The USB device descriptor's `bcdDevice` field is computed from
`FW_VERSION_BCD` and packed as `0xMMmp` (MAJOR encoded as BCD in the
high byte, MINOR in the high nibble of the low byte, PATCH in the
low nibble). The TWEAK component does not fit in the 16-bit field
and is intentionally dropped from `bcdDevice`; it remains reachable
via PING / `version` / the banner. Operators reading `lsusb -v` see
the first three segments without re-opening any of the CDC ports.

## Host-side parity gate

`faultycmd.version_check` is the small module that compares the
firmware's reported version against `faultycmd.__version__`. The
policy is **Exact**: every one of the four segments must match.
Patches and tweaks count; if the firmware and host disagree on any
segment, the host refuses to operate.

Every protocol client (`EmfiClient`, `CrowbarClient`, `CampaignClient`,
`ScannerClient`) runs the probe in its `open()` method. A mismatch
raises `VersionMismatchError` and closes the underlying serial port
so the caller never holds a half-open client. The CLI maps the
exception to exit code `3` and prints a styled message; the TUI
shows it via `App.notify()` and renders a red `✗` in the header
subtitle.

Two escape hatches:

  - `faultycmd --ignore-version-mismatch <subcommand>` flips a global
    flag the version_check module consults; the open() still probes
    and stores the firmware version (so the TUI can still display
    "mismatch — override active"), but the assertion is skipped.
  - Test code passes `check_firmware_version=False` when constructing
    a client with a `FakeSerial` factory. Production code paths
    (`EmfiClient.discover()` etc. used by the CLI and TUI) always
    enable the check.

The override is intentional. It lets a developer iterate on a
hand-built UF2 against a hand-built host package without needing to
re-tag the repo for every smoke test. It is **not** intended for
end users — a mismatched pairing in field use is almost always a
half-applied upgrade and produces silently-wrong results if the wire
protocol drifted.

## Cutting a release (maintainer guide)

The release workflow lives at `.github/workflows/release.yml`. It
fires on every tag matching `v*.*.*.*` (and `v*.*.*.*-*` for
pre-releases). Three jobs run, in two parallel + one downstream:

### `build` (ubuntu-24.04) — UF2 + wheel + sdist

  1. Validates the tag shape and rejects malformed tags up-front so a
     typo doesn't ship a broken release.
  2. Installs the cross-compile toolchain (`gcc-arm-none-eabi`,
     `libnewlib-arm-none-eabi`, `libstdc++-arm-none-eabi-newlib`,
     `cmake`, `ninja`) and the Python build backend.
  3. Runs `./scripts/get_build.sh ${TAG}` which:
       - rewrites the three version literals in the workflow checkout
         (NOT committed back to `main`),
       - re-runs `cmake --preset fw-release` so `firmware_version.h`
         is regenerated with `PROJECT_VERSION_*` matching the tag,
       - builds the firmware (`build/fw-release/apps/faultycat_fw/faultycat.uf2`),
       - builds the host sdist + wheel via `python -m build`,
       - copies the UF2 to
         `build/apps/faultycat_fw/faultycat_${TAG}.uf2` and the host
         distributions to the same directory.
  4. Uploads `build/apps/faultycat_fw/` as artifact
     `faultycat-release-${TAG}`.

### `build-windows-exe` (windows-latest) — standalone `.exe`

Runs in parallel with `build`. Bumps the same Python version literals
(skipping CMakeLists, since this job doesn't build firmware), installs
`pyinstaller` via the `[build-binary]` extra, then produces a
single-file `faultycmd.exe` with:

```text
pyinstaller --name faultycmd --onefile -p src \
  --collect-all faultycmd --collect-all textual \
  --collect-all rich --collect-all click \
  src/faultycmd/__main__.py
```

The resulting `.exe` is renamed `faultycmd_${TAG}.exe` and uploaded as
artifact `faultycmd-windows-${TAG}`. It bundles Python + every host
dependency (~30-50 MB) so the end user doesn't need a Python install
on Windows — they download the `.exe`, drop it anywhere, and run it.

### `release` (ubuntu-24.04) — publish draft

`needs: [build, build-windows-exe]`. Downloads both artifacts to
`./release/`, then publishes a **draft** GitHub Release with every
file attached. `generateReleaseNotes: true` pulls the changelog from
PRs and commits since the previous tag; an additional "Highlights"
section in the body lists the filtered `feat/fix/perf/refactor/ci`
commits since the previous non-suffix tag.

The release is created as a draft so the maintainer reviews it, edits
the notes if needed, and clicks publish manually.

### To cut `v3.0.1.0`

```bash
git checkout main
# Optional: bump the literals locally for repo consistency — not
# required, the workflow re-bumps them defensively during the build.
sed -i -E 's/^(\s*VERSION\s+)[0-9.]+/\13.0.1.0/' CMakeLists.txt
sed -i -E 's/^(version = )"[^"]+"/\1"3.0.1.0"/' host/faultycmd-py/pyproject.toml
sed -i -E 's/^(__version__ = )"[^"]+"/\1"3.0.1.0"/' host/faultycmd-py/src/faultycmd/__init__.py
git commit -am "chore(release): bump to 3.0.1.0"

git tag v3.0.1.0
git push origin main
git push origin v3.0.1.0
```

Watch the **Create Release on Tag** workflow under the Actions tab;
when it goes green a new draft release appears under
[Releases](https://github.com/ElectronicCats/faultycat/releases). Open
the draft, sanity-check the auto-generated notes, then publish.

This project mirrors Minino's pattern of **not** committing the
workflow's defensive bump back to `main`. That means the literals in
`main` reflect the last *manually* committed bump, which may lag the
latest tag. Use the optional `sed` block above to keep them in sync
with each tag.

### Dry-running a release locally

The same script CI runs is callable from the repo root:

```bash
./scripts/get_build.sh v3.0.1.0
ls build/apps/faultycat_fw/
# faultycat_v3.0.1.0.uf2
# faultycmd-3.0.1.0-py3-none-any.whl
# faultycmd-3.0.1.0.tar.gz
```

The script does the same in-place version bump CI does, so a
follow-up `git diff` shows exactly what the workflow will write into
its checkout. Revert with:

```bash
git checkout -- \
  CMakeLists.txt \
  host/faultycmd-py/pyproject.toml \
  host/faultycmd-py/src/faultycmd/__init__.py
```

if the tree should stay clean.

The Windows `.exe` is NOT produced by `scripts/get_build.sh` because
PyInstaller's output is platform-specific (a Linux runner can't
produce a Windows `.exe` without cross-compile gymnastics we
intentionally don't run locally). The release workflow's
`build-windows-exe` job is what builds it on a `windows-latest`
runner. To reproduce locally on a Windows machine:

```cmd
cd host\faultycmd-py
python -m venv .venv && .venv\Scripts\activate
pip install -e .[build-binary]
pyinstaller --name faultycmd --onefile -p src ^
  --collect-all faultycmd --collect-all textual ^
  --collect-all rich --collect-all click ^
  src\faultycmd\__main__.py
dist\faultycmd.exe --version
```

Pre-release tags work the same way:

```bash
./scripts/get_build.sh v3.1.0.0-rc1
# faultycat_v3.1.0.0-rc1.uf2  ← suffix preserved in filenames
# faultycmd-3.1.0.0-py3-none-any.whl  ← bare semver (no suffix)
#                                       wheel filenames can't carry
#                                       the suffix without PEP 440
#                                       gymnastics; deferred.
```

## Installing a release (end-user guide)

Each GitHub Release ships four files. Pick the firmware UF2 plus the
host distribution that matches your platform:

  1. **Flash the firmware.** Hold the BOOTSEL button on the FaultyCat
     while plugging the USB cable. The board appears as a
     mass-storage volume named `RPI-RP2`. Drag
     `faultycat_v3.0.1.0.uf2` onto that volume; the board reboots
     into the new firmware automatically and re-enumerates as
     `1209:fa17`.

  2. **Install the matching host CLI/TUI.**

     - **On Windows:** download `faultycmd_v3.0.1.0.exe` and drop it
       wherever is convenient — Desktop, project directory, a folder
       already on `PATH`. The `.exe` bundles Python and every
       dependency, so no `pip` / venv setup is needed.

       From the same directory the `.exe` lives in (PowerShell or
       `cmd.exe`), the `.\` prefix is required so the shell runs the
       local file instead of searching `PATH`:

       ```powershell
       .\faultycmd_v3.0.1.0.exe info
       .\faultycmd_v3.0.1.0.exe tui
       ```

       **Easier:** rename the file to something shorter
       (e.g. `faultycmd.exe`), and the invocations become
       `.\faultycmd.exe info`. And if you put that renamed file in a
       folder that is already on `PATH`, you can drop the `.\` and
       just type `faultycmd info` from anywhere.

     - **On Linux / macOS, or on Windows with Python already
       installed:**

       ```bash
       pip install faultycmd-3.0.1.0-py3-none-any.whl
       # or, from source:
       pip install faultycmd-3.0.1.0.tar.gz
       ```

  3. **Verify parity.**

     ```bash
     faultycmd info
     # Expected: `firmware: v3.0.1.0 (match)`
     ```

If `faultycmd info` reports a mismatch, the most common cause is
that you upgraded one side without the other — either re-flash the
matching UF2 or re-install the matching wheel. The mismatch message
includes both versions so it's clear which one needs to move.

## Entry points — `faultycmd`, `python -m faultycmd`, and the `.exe`

All three invocations route through `faultycmd.cli:_wrap_main` (not
the bare `main` click group). That keeps the user-visible behaviour
symmetric:

| Invocation                         | What runs                                |
|------------------------------------|------------------------------------------|
| `faultycmd ...`                    | pip-installed console script, configured in `pyproject.toml` `[project.scripts]` |
| `python -m faultycmd ...`          | `src/faultycmd/__main__.py`              |
| `faultycmd_vX.Y.Z.W.exe ...`       | PyInstaller-frozen `__main__.py`         |

The wrapper catches `VersionMismatchError` (→ exit 3),
`EngineError` / `CampaignError` / `ScannerError` (→ exit 2 with
styled message), and `FileNotFoundError` (→ exit 2). Anything else
propagates as a Python traceback. Both `python -m faultycmd` and
the standalone `.exe` are documented as Windows-friendly fallbacks
when the user-install `Scripts/` directory is not on `PATH`.

## Related files

| File | Purpose |
|---|---|
| `CMakeLists.txt` | `project(... VERSION ...)` — firmware-side SSoT. |
| `cmake/firmware_version.h.in` | Template `configure_file` fills with `PROJECT_VERSION_*`. |
| `host/faultycmd-py/src/faultycmd/__init__.py` | `__version__` literal — host SSoT. |
| `host/faultycmd-py/src/faultycmd/__main__.py` | `python -m faultycmd` + PyInstaller `.exe` entry. Calls `_wrap_main`. |
| `host/faultycmd-py/src/faultycmd/cli.py` | `_wrap_main` exception wrapper + click groups. |
| `host/faultycmd-py/src/faultycmd/version_check.py` | Host-side parse + assert + global override. |
| `host/faultycmd-py/src/faultycmd/protocols/_base.py` | `_probe_and_check_firmware_version()` on `open()`. |
| `host/faultycmd-py/src/faultycmd/protocols/scanner.py` | Shell-version probe for CDC2. |
| `host/faultycmd-py/pyproject.toml` | `version = "..."` + `[project.scripts]` → `_wrap_main`. |
| `scripts/get_build.sh` | Local + CI build wrapper (firmware UF2 + host sdist/wheel; NOT the Windows `.exe`). |
| `.github/workflows/release.yml` | Tag-driven release workflow (`build` + `build-windows-exe` + `release` jobs). |
| `services/host_proto/{emfi,crowbar}_proto/*.c` | 6-byte PING reply. |
| `apps/faultycat_fw/main.c` | Diag banner + `version` shell command. |
| `usb/src/usb_descriptors.c` | `bcdDevice` derived from `FW_VERSION_BCD`. |
