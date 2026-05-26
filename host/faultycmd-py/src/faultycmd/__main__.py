"""Module entry point so ``python -m faultycmd ...`` and PyInstaller
single-file binaries route through the same place as the
``faultycmd`` console script.

Uses an absolute import (`faultycmd.cli`) instead of a relative one
(`.cli`) so the PyInstaller bootloader — which loses package context
when executing a frozen `__main__.py` directly — can resolve it.
The absolute form works equally well under `python -m faultycmd`."""

from faultycmd.cli import main

if __name__ == "__main__":
    main()
