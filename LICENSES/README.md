# LICENSES/

This directory holds:

1. The license under which **this project's own firmware code** is
   released: **BSD-3-Clause** — see the root `LICENSE` file.
2. Verbatim copies of every **upstream** license that covers code
   vendored or referenced by this project, named `UPSTREAM-<dep>.txt`.

| Dependency                   | Used as                | Upstream license | Copy in this dir                     |
|------------------------------|------------------------|------------------|--------------------------------------|
| `third_party/pico-sdk`       | submodule              | BSD-3-Clause     | `UPSTREAM-pico-sdk.txt`              |
| `third_party/debugprobe`     | submodule              | MIT              | `UPSTREAM-debugprobe.txt`            |
| `third_party/free-dap`       | submodule              | BSD-3-Clause     | `UPSTREAM-free-dap.txt`              |
| `third_party/Unity`          | submodule (host tests) | MIT              | `UPSTREAM-Unity.txt`                 |
| CMSIS-DAP headers (in-tree)  | vendored headers       | Apache-2.0 (ARM) | `UPSTREAM-cmsis-dap-headers.txt`     |
| blueTag                      | algorithmic reference  | MIT              | `UPSTREAM-blueTag.txt`               |

## The hardware itself

The FaultyCat **hardware** (KiCad project under `/Hardware/`) is
licensed under **CC BY-SA 3.0** by Electronic Cats, as a remix of
ChipSHOUTER PicoEMP by Colin O'Flynn. That license covers the
KiCad/PCB files only; it does **not** apply to the firmware in this
repository, which is BSD-3-Clause.
