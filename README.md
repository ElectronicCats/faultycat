# Faulty Cat

Faulty Cat is a low-cost Electromagnetic Fault Injection (EMFI) tool, designed specifically for self-study and hobbiest research.

<a href="https://electroniccats.com/store/faulty-cat/">
  <p align="center">
  <img src="https://electroniccats.com/wp-content/uploads/badge_store.png" height="104"  />
  </p>
</a>

This Faulty Cat project is *not* the ChipSHOUTER. Instead, it's designed to present a "bare bones" tool that has a design optimization focused in rough order of (1) safe operation, (2) cost, (3) usability, (4) performance. Despite the focus on safety and low cost, it works *surprisingly* well. It is also *not* sold as a complete product - you are responsible for building it, ensuring it meets any relevant safety requirements/certifications, and we completely disclaim all liability for what happens next. Please **only** use Faulty Cat where you are building and controlling it yourself, with total understanding of the operation and risks. It is *not* designed to be used in professional or educational environments, where tools are expected to meet safety certifications (ChipSHOUTER was designed for these use-cases).

**IMPORTANT**: The plastic shield is critical for safe operation. While the output itself is isolated from the input connections, you will still **easily shock yourself** on the exposed high-voltage capacitor and circuitry. **NEVER** operate the device without the shield.

As an open-source project, it also collects inputs from various community members, and welcomes your contributions! It also has various remixes of it, including:

## Thanks / Contributors

Faulty Cat based in PicoEMP is a community-focused project, with major contributions from:
* Colin O'Flynn (original HW design, simple Python demo)
* [stacksmashing](https://twitter.com/ghidraninja) (C firmware for full PIO feature set)
* [Lennert Wouters](https://twitter.com/LennertWo) (C improvements, first real demo)
* [@nilswiersma](https://github.com/nilswiersma) (Triggering/C improvements)


### Programming the Faulty Cat

Follow the [first steps section](https://github.com/ElectronicCats/FaultyCat/wiki/3.-First-Steps) of the Faulty Cat wiki in order to program it.  You can run other tasks on the microcontroller
as well.

##  Wiki and Getting Started
For more information about Faulty Cat's background, the difference between Faulty Cat to other products, and how it works please visit: [**Getting Started in our Wiki**](https://github.com/ElectronicCats/FaultyCat/wiki)

<a href="https://github.com/ElectronicCats/FaultyCat/wiki">
  <p align="center">
  <img src="https://github.com/ElectronicCats/FaultyCat/assets/40640735/bd1966a8-1dd1-4355-b788-5885f66f081d" height="400" />
    </p>
</a>

### Useful References

If you don't know where to start with FI, you may find a couple of chapters of the [Hardware Hacking Handbook](https://nostarch.com/hardwarehacking) useful.

You can see a demo of PicoEMP being used on a real attack in this [TI CC SimpleLink attack demo](https://github.com/KULeuven-COSIC/SimpleLink-FI/blob/main/notebooks/5_ChipSHOUTER-PicoEMP.ipynb).

**WARNING**: The high voltage will be applied across the SMA connector. If an injection tip (coil) is present, it will absorb most of the power. If you leave the SMA connector open, you will present a high voltage pulse across this SMA and could shock yourself. Do NOT touch the output SMA tip as a general "best practice", and treat the output as if it has a high voltage present.

The full ChipSHOUTER detects the missing connector tip and refuses to power up the high voltage, the PicoEMP does not have this failsafe!

## License

This work is licensed under a [Creative Commons Attribution-ShareAlike 3.0 International License][cc-by-sa].

[cc-by-sa]: http://creativecommons.org/licenses/by-sa/3.0/
[cc-by-sa-image]: https://licensebuttons.net/l/by-sa/3.0/88x31.png
[cc-by-sa-shield]: https://img.shields.io/badge/License-CC%20BY--SA%203.0-lightgrey.svg


Electronic Cats invests time and resources in providing this open-source design. Please support Electronic Cats and open-source hardware by purchasing products from Electronic Cats!

