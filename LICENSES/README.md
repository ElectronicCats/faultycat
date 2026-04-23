# LICENSES/

This directory holds:

1. The license under which **this project's own firmware code** is
   released: **BSD-3-Clause** — see the root `LICENSE` file.
2. Verbatim copies of every **upstream** license of the vendored
   dependencies in `third_party/`, named `UPSTREAM-<dep>.txt`.
3. A `NOTICE-<dep>.md` for any dependency that requires special
   legal commentary (currently: `NOTICE-faultier.md`).

| Dependency (path)            | Upstream license | Copy in this dir                     |
|------------------------------|------------------|--------------------------------------|
| `third_party/pico-sdk`       | BSD-3-Clause     | `UPSTREAM-pico-sdk.txt`              |
| `third_party/debugprobe`     | MIT              | `UPSTREAM-debugprobe.txt`            |
| `third_party/blueTag`        | MIT              | `UPSTREAM-blueTag.txt`               |
| `third_party/free-dap`       | BSD-3-Clause     | `UPSTREAM-free-dap.txt`              |
| `third_party/cmsis-dap/`     | Apache-2.0 (ARM) | `UPSTREAM-cmsis-dap-headers.txt`     |
| `third_party/faultier`       | **NONE** ⚠︎      | see `NOTICE-faultier.md`             |

## The hardware itself

The FaultyCat **hardware** (KiCad project under `/Hardware/`) is
licensed under **CC BY-SA 3.0** by Electronic Cats, as a remix of
ChipSHOUTER PicoEMP by Colin O'Flynn. That license covers the
KiCad/PCB files only; it does **not** apply to the firmware in this
repository, which is BSD-3-Clause.
