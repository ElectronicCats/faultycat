"""Module entry point so ``python -m faultycmd ...`` and PyInstaller
single-file binaries route through the same place as the
``faultycmd`` console script."""
from .cli import main

if __name__ == "__main__":
    main()
