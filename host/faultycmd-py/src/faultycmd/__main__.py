"""Module entry point so ``python -m faultycmd ...`` and PyInstaller
single-file binaries route through the same place as the
``faultycmd`` console script.

Uses an absolute import (`faultycmd.cli`) instead of a relative one
(`.cli`) so the PyInstaller bootloader — which loses package context
when executing a frozen `__main__.py` directly — can resolve it.
The absolute form works equally well under `python -m faultycmd`."""

from faultycmd.cli import _wrap_main

if __name__ == "__main__":
    # Use _wrap_main (not main) so `python -m faultycmd` gets the same
    # styled error messages + exit codes the installed `faultycmd`
    # console script does — VersionMismatchError → exit 3, etc. Bare
    # `main()` would let the click group raise a Python traceback
    # straight to the terminal, which is unfriendly especially on
    # Windows where `python -m faultycmd` is the recommended invocation
    # when the user-install Scripts directory isn't on PATH.
    _wrap_main()
