# Faulty Cat

> ## Firmware v3 — rewritten from scratch
>
> This repository ships **firmware v3** for the existing FaultyCat
> v2.x hardware. It is a from-scratch rewrite of the original v2.x
> firmware, not an evolution of it — same board, new stack.

Faulty Cat is a low-cost Electromagnetic Fault Injection (EMFI) tool, designed specifically for self-study and hobbiest research.

<a href="https://github.com/ElectronicCats/FaultyCat/wiki">
  <p align="center">
  <img src="https://github.com/user-attachments/assets/99890265-0602-4206-a05e-5c75bb6a386d" height="400" />
    </p>
</a>

<p align=center>
<a href="https://electroniccats.com/store/faulty-cat/">
  <img src="https://github.com/user-attachments/assets/f6414773-c8fa-4c99-9c8b-05a1dc297fb3" width="200" height="104" />
</a>

<a href="https://github.com/ElectronicCats/faultycat/wiki">
  <img src="https://github.com/ElectronicCats/flipper-shields/assets/44976441/6aa7f319-3256-442e-a00d-33c8126833ec" width="200" height="104" />
</a>
</p>

<p align=center>
  Also available at distributors:
</p>
<p align=center>
  <a href="https://labs.ksec.co.uk/product-category/electronic-cat/">
    <img src="https://cdn.ksec.co.uk/ksec-solutions/ksec-W-BW-MV-small-clipped.png" width="200" />
  </a>
</p>

Faulty Cat is a high-end Electromagnetic Fault Injection (EMFI) tool a remix of the project [ChipSHOUTER PicoEMP](https://github.com/newaetech/chipshouter-picoemp) design optimization focused in rough order on (1) safe operation, (2) high performance, (3) usability, and finally (4) cost. This results in a tool that covers many use-cases, but may be overkill (and expensive) for many.

We have created this project in KiCad and looking for alternatives to some components, we have left aside the Raspberry Pico board to use the RP2040 directly in the design. Tested in our laboratory before going on sale, even so, it is a product that must be handled with care, read the instructions for use.

Please **only** use Faulty Cat when you have purchased it from us and control it yourself, with full knowledge of the operation and risks. It is *not* designed for use in professional or educational environments, where tools are expected to meet safety certifications.


**IMPORTANT**: The plastic shield is critical for safe operation. While the output itself is isolated from the input connections, you will still **easily shock yourself** on the exposed high-voltage capacitor and circuitry. **NEVER** operate the device without the shield.

As an open-source project and as a remix of the project [ChipSHOUTER PicoEMP](https://github.com/newaetech/chipshouter-picoemp), it also collects inputs from various community members, and welcomes your contributions!

###  NEW FEATURES AVAILABLE ON V2.1⚡😼

- Voltage glitching.
- Trigger using dedicated pins, available in the new pinout.
- Trigger voltage reference, for more accurate response every time a voltage glitch is attempted to be sent.
- Analog input to monitor the target device’s status during the glitching process.
- JTAG/SWD scanner.


## Thanks / Contributors

Faulty Cat based in PicoEMP is a community-focused project, with major contributions from:
* Colin O'Flynn (original HW design, simple Python demo)
* [stacksmashing](https://twitter.com/ghidraninja) (C firmware for full PIO feature set)
* [Lennert Wouters](https://twitter.com/LennertWo) (C improvements, first real demo)
* [@nilswiersma](https://github.com/nilswiersma) (Triggering/C improvements)


### Programming the Faulty Cat

Two flashing paths are supported on firmware **v3**:

1. **Physical BOOTSEL button.** Hold the BOOTSEL button while plugging
   the USB cable; the RP2040 enumerates as a USB mass-storage device
   (`RPI-RP2`). Drag the `.uf2` onto it, or run `tools/flash.sh` which
   handles the copy automatically.
2. **Magic baud 1200 — remote BOOTSEL.** Open any of the four CDC ports
   exposed by the v3 firmware at **1200 baud** and the device reboots
   into the bootrom mass-storage mode without touching the button.
   `tools/flash.sh` uses this when the device is already enumerated.
   From `faultycmd` the equivalent is `faultycmd reflash <path-to.uf2>`
   (F11-0f).

Background on the bootrom mode and the button location lives in the
legacy
[Bootloader mode section](https://github.com/ElectronicCats/faultycat/wiki/1.-Understanding-Faulty-Cat#bootloader-mode)
of the wiki (the **mechanism** is the same in v3 — only the firmware
on top changed).

### Building the firmware

The fastest way to build the firmware from source is the official
**Raspberry Pi Pico** extension for VS Code — it installs the
toolchain (cmake, ninja, arm-none-eabi-gcc) and the pico-sdk for
you, and runs the cmake configure/build steps from the editor.

1. Install the
   [Raspberry Pi Pico](https://marketplace.visualstudio.com/items?itemName=raspberry-pi.raspberry-pi-pico)
   extension from the VS Code Marketplace.
2. Clone this repository **with submodules**:
   `git clone --recursive <repo-url>`
   (or, after a plain clone, run
   `git submodule update --init --recursive`).
3. Open the cloned folder in VS Code, then run
   **Raspberry Pi Pico: Import Project** from the command palette
   (Ctrl+Shift+P) and point it at this folder.
4. Hit **Compile** in the status bar. The resulting `.uf2` lands
   under `build/.../apps/faultycat_fw/faultycat.uf2`.

Flash that `.uf2` using one of the paths in **Programming the
Faulty Cat** above.

### Documentation

Everything that explains how the project is built, how to install
the host tool, and the safety/operational contracts is collected
here. Click through for the full document:

| Document | What it covers |
|---|---|
| [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) | Layering (HAL → drivers → services → apps), tree map, USB composite layout, `faultycmd` host-side module map. |
| [`docs/HARDWARE_V2.md`](docs/HARDWARE_V2.md) | GPIO → function map for the v2.1 / v2.2 board the v3 firmware runs on. |
| [`docs/PORTING.md`](docs/PORTING.md) | Per-file legacy→rewrite migration table (what was rewritten, what was discarded, what survives as reference). |
| [`docs/SAFETY.md`](docs/SAFETY.md) | High-voltage safety contract for the EMFI / crowbar drivers (signed by maintainer before each HV-touching commit). |
| [`docs/MUTEX_INTERNALS.md`](docs/MUTEX_INTERNALS.md) | SWD bus cooperative mutex + Campaign manager wire stack (F9). |
| [`docs/JTAG_INTERNALS.md`](docs/JTAG_INTERNALS.md) | JTAG/SWD scanner, BusPirate-compat shell, flashrom serprog (F8). |
| [`host/faultycmd-py/README.md`](host/faultycmd-py/README.md) | **Install + usage of the `faultycmd` CLI and TUI** — venv setup on Linux, Windows (PowerShell / CMD / Git Bash), macOS. Hotkeys, trigger polarity, trigger timeout. |
| [`LICENSES/README.md`](LICENSES/README.md) | License overview for vendored code (pico-sdk, debugprobe, free-dap, Unity, CMSIS-DAP headers). |

If you only want to flash a board and drive it from the host tool,
the path is: this README → `host/faultycmd-py/README.md` (install
+ quickstart) → `docs/SAFETY.md` (read once before the first HV
fire).


### Useful References

If you don't know where to start with Electromagnetic Fault Injection (EMFI), you may find a couple of chapters of the [Hardware Hacking Handbook](https://nostarch.com/hardwarehacking) useful.

You can see a demo of PicoEMP being used on a real attack in this [TI CC SimpleLink attack demo](https://github.com/KULeuven-COSIC/SimpleLink-FI/blob/main/notebooks/5_ChipSHOUTER-PicoEMP.ipynb).

**WARNING**: The high voltage will be applied across the SMA connector. If an injection tip (coil) is present, it will absorb most of the power. If you leave the SMA connector open, you will present a high voltage pulse across this SMA and could shock yourself. Do NOT touch the output SMA tip as a general "best practice", and treat the output as if it has a high voltage present.

## How to contribute <img src="https://electroniccats.com/wp-content/uploads/2018/01/fav.png" height="35"><img src="https://raw.githubusercontent.com/gist/ManulMax/2d20af60d709805c55fd784ca7cba4b9/raw/bcfeac7604f674ace63623106eb8bb8471d844a6/github.gif" height="30">
 Contributions are welcome!

Please read the document  [**Contribution Manual**](https://github.com/ElectronicCats/electroniccats-cla/blob/main/electroniccats-contribution-manual.md)  which will show you how to contribute your changes to the project.

✨ Thanks to all our [contributors](https://github.com/ElectronicCats/faultycat/graphs/contributors)! ✨

See [**_Electronic Cats CLA_**](https://github.com/ElectronicCats/electroniccats-cla/blob/main/electroniccats-cla.md) for more information.

See the  [**community code of conduct**](https://github.com/ElectronicCats/electroniccats-cla/blob/main/electroniccats-community-code-of-conduct.md) for a vision of the community we want to build and what we expect from it.

## License

This project FaultyCat is adapted from [ChipSHOUTER PicoEMP](https://github.com/newaetech/chipshouter-picoemp) by [Colin O'Flynn](https://github.com/colinoflynn) is licensed under CC BY-SA 3.0, "FaultyCat" contains modifications such as: porting the project to Kicad, modifying BOM and dimensions is licensed under CC BY-SA 3.0 by ElectronicCats.

Electronic Cats invests time and resources in providing this open-source design. Please support Electronic Cats and open-source hardware by purchasing products from Electronic Cats!
