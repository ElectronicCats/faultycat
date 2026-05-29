# third_party/cmsis-dap/

Verbatim copies of the ARM **CMSIS-DAP** reference headers. These are
used by `services/daplink_usb/` (F7) to expose the standard CMSIS-DAP
v2 vendor interface and v1 HID interface over USB.

## Provenance

Source: https://github.com/ARM-software/CMSIS_5 @ tag `5.9.0`

| File                        | Upstream path                                   |
|-----------------------------|-------------------------------------------------|
| `include/DAP.h`             | `CMSIS/DAP/Firmware/Include/DAP.h`              |
| `include/DAP_config.h`      | `CMSIS/DAP/Firmware/Config/DAP_config.h` (template — our board-specific overrides will live in `usb/include/dap_config_board.h` in F7, never by editing this file in place) |
| `LICENSE`                   | CMSIS_5 root `LICENSE.txt` (Apache-2.0)         |

Why a copy and not a submodule: CMSIS_5 is a ~500 MB mono-repo with
dozens of unrelated sub-projects (DSP, RTOS, NN, etc.); we only need
two headers. A directory copy is ~25 KB and the version is pinned by
the commit that touched this directory.

## Updating

If a later CMSIS-DAP version is needed, do **not** hand-edit these
files. Instead:

1. Pick a new `CMSIS_5` tag.
2. Re-run the fetch in `tools/bootstrap.sh` with the new tag (the
   script parameterises the tag).
3. Commit the refreshed `include/` plus updated `LICENSE` and bump
   the tag reference in this README.
