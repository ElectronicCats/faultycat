# Faulty Cat

Faulty Cat is a low-cost Electromagnetic Fault Injection (EMFI) tool, designed specifically for self-study and hobbiest research.

<a href="https://github.com/ElectronicCats/FaultyCat/wiki">
  <p align="center">
  <img src="https://github.com/ElectronicCats/FaultyCat/assets/40640735/bd1966a8-1dd1-4355-b788-5885f66f081d" height="400" />
    </p>
</a>

<p align=center>
<a href="https://electroniccats.com/store/faulty-cat/">
  <img src="https://electroniccats.com/wp-content/uploads/badge_store.png" width="200" height="104" />
</a>

<a href="https://github.com/ElectronicCats/faultycat/wiki">
  <img src="https://github.com/ElectronicCats/flipper-shields/assets/44976441/6aa7f319-3256-442e-a00d-33c8126833ec" width="200" height="104" />
</a>

<p align=center>
  <a href="https://labs.ksec.co.uk/product-category/electronic-cat/">
    <img src="https://cdn.ksec.co.uk/ksec-solutions/ksec-W-BW-MV-small-clipped.png" width="200" />
  </a>
</p>

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

