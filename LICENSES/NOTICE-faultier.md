# NOTICE — `third_party/faultier`

Upstream repository: https://github.com/hextreeio/faultier
Pinned commit: `1c78f3eb0261cac8b0add8d8047775e42942faf0`

## Status

**The upstream repository `hextreeio/faultier` has NO LICENSE file at the
repository root** (verified 2026-04-23 via the GitHub REST API — the
`license` field returned `null`, and no `LICENSE`, `LICENSE.md`,
`LICENSE.txt`, or `COPYING` file is present).

Under default copyright law in most jurisdictions (including the US,
which governs GitHub's ToS), source code published without an explicit
license is **"All Rights Reserved"** by the author.

## Consequence for this project (FaultyCat v3 firmware)

1. **No code from `third_party/faultier/` is compiled into the v3
   firmware.** The submodule is present as a *reference-only* tree for
   humans to read.
2. The CMake build system excludes it with `EXCLUDE_FROM_ALL` and does
   not include its headers on the compile path.
3. No source file in this repository is derived by literal copy,
   modification, or translation of `hextreeio/faultier` sources.
4. Where our design ideas were informed by reading `faultier`, we
   credit the concept in `docs/PORTING.md` and re-implement from
   scratch with original code under BSD-3-Clause.

## If upstream adds a permissive license

If `hextreeio/faultier` gains a MIT / BSD / Apache / similar license,
this NOTICE should be updated and specific files can be considered for
a clean port with attribution, per the terms of the new license.

Until then: **treat the submodule tree as read-only reference
documentation, not as a code source.**
