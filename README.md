# Faulty Cat

Faulty Cat is a low-cost Electromagnetic Fault Injection (EMFI) tool, designed specifically for self-study and hobbiest research.

## Thanks / Contributors

Faulty Cat based in PicoEMP is a community-focused project, with major contributions from:
* Colin O'Flynn (original HW design, simple Python demo)
* [stacksmashing](https://twitter.com/ghidraninja) (C firmware for full PIO feature-set)
* [Lennert Wouters](https://twitter.com/LennertWo) (C improvements, first real demo)
* [@nilswiersma](https://github.com/nilswiersma) (Triggering/C improvements)

## Background

The [ChipSHOUTER](http://www.chipshouter.com) is a high-end Electromagnetic Fault Injection (EMFI) tool designed by Colin
at [NewAE Technology](http://www.newae.com). While not the first commercially available EMFI tool, ChipSHOUTER was the first
"easily purchasable" (even if expensive) tool with extensive open documentation. The tool was *not* open-source, but it
did contain a variety of detailed description of the design and architecture in the
[User Manual](https://github.com/newaetech/ChipSHOUTER/tree/master/documentation). The ChipSHOUTER design optimization focused in rough order on (1) safe operation, (2) high performance, (3) usability, and finally (4) cost. This results in a tool that covers many use-cases, but may be overkill (and too costly) for many. In additional, acquiring the safety testing/certification is not cheap, and must be accounted for in the product sale price.

The Faulty Cat tries to fill in the gap that ChipSHOUTER leaves at the lower end of the spectrum. This Faulty Cat project is *not* the
ChipSHOUTER. Instead it's designed to present a "bare bones" tool that has a design optimization focused in rough order of (1) safe
operation, (2) cost, (3) usability, (4) performance. Despite the focus on safety and low-cost, it works *suprisingly* well. It is also
*not* sold as a complete product - you are responsible for building it, ensuring it meets any relevant safety requirements/certifications,
and we completely disclaim all liability for what happens next. Please **only** use Faulty Cat where you are building and controlling it
yourself, with total understanding of the operation and risks. It is *not* designed to be used in professional or educational environments,
where tools are expected to meet safety certifications (ChipSHOUTER was designed for these use-cases).

As an open-source project it also collects inputs from various community members, and welcomes your contributions! It also has various remixes of it, including:

**IMPORTANT**: The plastic shield is critical for safe operation. While the output itself is isolated from the input connections, you will still **easily shock yourself** on the exposed high-voltage capacitor and circuitry. **NEVER** operate the device without the shield.

### Programming the FaultyCat

You'll need to program the Faulty Cat with the firmware in the [firmware](firmware) directory. You can run other tasks on the microcontroller
as well.

### Useful References

If you don't know where to start with FI, you may find a couple chapters of the [Hardware Hacking Handbook](https://nostarch.com/hardwarehacking) useful.

You can see a demo of PicoEMP being used on a real attack in this [TI CC SimpleLink attack demo](https://github.com/KULeuven-COSIC/SimpleLink-FI/blob/main/notebooks/5_ChipSHOUTER-PicoEMP.ipynb).

**WARNING**: The high voltage will be applied across the SMA connector. If an injection tip (coil) is present, it will absorb most of the power. If you leave the SMA connector open, you will present a high voltage pulse across this SMA and could shock yourself. Do NOT touch the output SMA tip as a general "best practice", and treat the output as if it has a high voltage present.

The full ChipSHOUTER detects the missing connector tip and refuses to power up the high voltage, the PicoEMP does not have this failsafe!

## About the High Voltage Isolation

Most EMFI tools generate high voltages (similar to a camera flash). Many previous designs of open-source EMFI tools would work well, but [exposed the user to high voltages](https://github.com/RedBalloonShenanigans/BADFET). This was fine provided you use the tool correctly, but of course there is always a risk of grabbing the electrically "hot" tool! This common design choice happens because the easiest way to design an EMFI tool is with "low-side switching" (there is a very short mention of these design choices as well in my [book](https://www.nostarch.com/hardwarehacking) if you are curious). With low-side switching the output connector is always "hot", which presents a serious shock hazard.

Faulty Cat gets around this problem by floating the high-voltage side, meaning there is no electrical path between the EMFI probe output and the input voltage ground. With the isolated high voltage output we can use the simple "low-side switching" in a safe manner. Some current will still flow due to the high-frequency spikes, so this isn't *perfect*, but it works well enough in practice (well enough you will shock yourself less often).

The caveat here is for this to work you also need to isolate your gate drive. There are a variety of [solutions to this](https://www.analog.com/en/technical-articles/powering-the-isolated-side-of-your-half-bridge-configuration.html), with the simplist being a gate drive transformer (GDT). The PicoEMP uses the transformer architecture, with some simplifications to further reduce BOM count.

More details of the design are available in the [hardware](hardware) folder.

### Technical Differences between Faulty Cat and ChipSHOUTER and PicoEMP

The main differences from a technical standpoint:

* ChipSHOUTER uses a much more powerful high voltage circuit and transformer (up to ~30W vs ~0.2W) that gives it
  almost unlimited glitch delivery, typically limited by your probe tip. The PicoEMP is slower to recover, typically ~1 to 4 seconds between
  glitches.

* ChipSHOUTER has a larger internal energy storage & more powerful output drivers.

* ChipSHOUTER has a controlled high-voltage setting from 150V to 500V. PicoEMP generates ~250V, there is some feedback but it's uncalibrated.
  **NOTE**: The PicoEMP allows some control of output pulse size by instead controlling the drive signal. This is less reliable (more variability
  in the output), but meets the goal of using the lowest-cost control method.

## License

This work is licensed under a [Creative Commons Attribution-ShareAlike 3.0 International License][cc-by-sa].

[cc-by-sa]: http://creativecommons.org/licenses/by-sa/3.0/
[cc-by-sa-image]: https://licensebuttons.net/l/by-sa/3.0/88x31.png
[cc-by-sa-shield]: https://img.shields.io/badge/License-CC%20BY--SA%203.0-lightgrey.svg

## License
Electronic Cats invests time and resources in providing this open-source design. Please support Electronic Cats and open-source hardware by purchasing products from Electronic Cats!
