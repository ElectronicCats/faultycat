# Faulty Cat

Faulty Cat is a low-cost Electromagnetic Fault Injection (EMFI) tool, designed specifically for self-study and hobbiest research.

<a href="https://electroniccats.com/store/faulty-cat/">
  <p align="center">
  <img src="https://electroniccats.com/wp-content/uploads/badge_store.png" height="104"  />
  </p>
</a>

Faulty Cat is a high-end Electromagnetic Fault Injection (EMFI) tool a remix of the project [ChipSHOUTER PicoEMP](https://github.com/newaetech/chipshouter-picoemp) design optimization focused in rough order on (1) safe operation, (2) high performance, (3) usability, and finally (4) cost. This results in a tool that covers many use-cases, but may be overkill (and too costly) for many. 

We have created this project in KiCad and looking for alternatives to some components, we have left aside the Raspberry Pico board to use the RP2040 directly in the design. Tested in our laboratory before going on sale, even so, it is a product that must be handled with care, read the instructions for use.

Please **only** use Faulty Cat when you have purchased it from us and control it yourself, with full knowledge of the operation and risks. It is *not* designed for use in professional or educational environments, where tools are expected to meet safety certifications.


**IMPORTANT**: The plastic shield is critical for safe operation. While the output itself is isolated from the input connections, you will still **easily shock yourself** on the exposed high-voltage capacitor and circuitry. **NEVER** operate the device without the shield.

As an open-source project and as a remix of the project [ChipSHOUTER PicoEMP](https://github.com/newaetech/chipshouter-picoemp), it also collects inputs from various community members, and welcomes your contributions! It also has various remixes of it, including:

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

If you don't know where to start with Electromagnetic Fault Injection (EMFI), you may find a couple of chapters of the [Hardware Hacking Handbook](https://nostarch.com/hardwarehacking) useful.

You can see a demo of PicoEMP being used on a real attack in this [TI CC SimpleLink attack demo](https://github.com/KULeuven-COSIC/SimpleLink-FI/blob/main/notebooks/5_ChipSHOUTER-PicoEMP.ipynb).

**WARNING**: The high voltage will be applied across the SMA connector. If an injection tip (coil) is present, it will absorb most of the power. If you leave the SMA connector open, you will present a high voltage pulse across this SMA and could shock yourself. Do NOT touch the output SMA tip as a general "best practice", and treat the output as if it has a high voltage present.

## License

This project FaultyCat is adapted from [ChipSHOUTER PicoEMP](https://github.com/newaetech/chipshouter-picoemp) by [Colin O'Flynn](https://github.com/colinoflynn) is licensed under CC BY-SA 3.0, "FaultyCat" contains modifications such as: porting the project to Kicad, modifying BOM and dimensions is licensed under CC BY-SA 3.0 by ElectronicCats.

Electronic Cats invests time and resources in providing this open-source design. Please support Electronic Cats and open-source hardware by purchasing products from Electronic Cats!

