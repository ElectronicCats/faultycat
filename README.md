# Faulty Cat

> ## Firmware and host tools have moved
>
> This repository now holds the **hardware design** only. The
> firmware (v2, a from-scratch rewrite for the existing FaultyCat
> v2.x hardware) has moved to
> [FaultyCat-Firmware](https://github.com/ElectronicCats/FaultyCat-Firmware),
> and the Python host CLI/TUI (`faultycmd`) has moved to
> [faultycat-TUI](https://github.com/ElectronicCats/faultycat-TUI/).
> See the **Firmware Repository** and **Python Tools Repository**
> sections below.

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

> **How the glitching works.** Firmware v3 ships two physical
> fault-injection techniques — **EMFI** (electromagnetic) and
> **Crowbar** (voltage glitching) — each available as **direct**
> single-shot fires or as parameter-swept **Campaigns**. For the
> full breakdown of what each engine does, the parameter matrix
> that differentiates them, and how the four combinations are
> driven from the host tool, see `docs/GLITCHING.md` in the
> [firmware repository](https://github.com/ElectronicCats/FaultyCat-Firmware).

We have created this project in KiCad and looking for alternatives to some components, we have left aside the Raspberry Pico board to use the RP2040 directly in the design. Tested in our laboratory before going on sale, even so, it is a product that must be handled with care, read the instructions for use.

Please **only** use Faulty Cat when you have purchased it from us and control it yourself, with full knowledge of the operation and risks. It is *not* designed for use in professional or educational environments, where tools are expected to meet safety certifications.


**IMPORTANT**: The plastic shield is critical for safe operation. While the output itself is isolated from the input connections, you will still **easily shock yourself** on the exposed high-voltage capacitor and circuitry. **NEVER** operate the device without the shield.

As an open-source project and as a remix of the project [ChipSHOUTER PicoEMP](https://github.com/newaetech/chipshouter-picoemp), it also collects inputs from various community members, and welcomes your contributions!

###  NEW FEATURES AVAILABLE ON V2.1⚡😼

- Voltage glitching.
- Trigger using dedicated pins, available in the new pinout.
- Trigger voltage reference, for more accurate response every time a voltage glitch is attempted to be sent.
- Analog input to monitor the target device’s status during the glitching process.
- JTAG/SWD scanner. **(Firmware v3.0 status: `scan swd` is the only
  scanner verb publicly exposed. JTAG scan, direct JTAG verbs, and
  direct SWD verbs are gated as WIP for v3.1 — see
  `docs/JTAG_INTERNALS.md` in the
  [firmware repository](https://github.com/ElectronicCats/FaultyCat-Firmware)
  for the details.)**

### Two attack engines (EMFI and Crowbar)

Firmware v3 exposes two physical fault-injection techniques (EMFI
and Crowbar voltage glitching) and two operational modes (direct
single-shot fires and Campaign parameter sweeps over delay/width/
power). They compose freely — both engines work in both modes.
The full matrix, wire-protocol routing, and host-CLI map live in
`docs/GLITCHING.md` in the
[firmware repository](https://github.com/ElectronicCats/FaultyCat-Firmware).


## Thanks / Contributors

Faulty Cat based in PicoEMP is a community-focused project, with major contributions from:
* Colin O'Flynn (original HW design, simple Python demo)
* [stacksmashing](https://twitter.com/ghidraninja) (C firmware for full PIO feature set)
* [Lennert Wouters](https://twitter.com/LennertWo) (C improvements, first real demo)
* [@nilswiersma](https://github.com/nilswiersma) (Triggering/C improvements)


## Firmware Repository

The FaultyCat firmware has been moved to a different repository, to
have a better version control, and issue tracking you will find it
here:

https://github.com/ElectronicCats/FaultyCat-Firmware

That repository covers building and flashing the firmware (BOOTSEL
button and magic-baud remote BOOTSEL paths), the release scheme, and
the full architecture / safety / glitching-engine documentation
(`docs/ARCHITECTURE.md`, `docs/SAFETY.md`, `docs/GLITCHING.md`,
`docs/JTAG_INTERNALS.md`, `docs/RELEASES.md`, etc.).

## Python Tools Repository

The `faultycmd` host CLI/TUI has been moved to a different
repository, to have a better version control, and issue tracking you
will find it here:

https://github.com/ElectronicCats/faultycat-TUI/

That repository covers installing and using `faultycmd` (venv setup
on Linux, Windows, macOS, hotkeys, trigger polarity, trigger
timeout).

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
