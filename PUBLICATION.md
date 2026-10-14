# Development source publication

This is the initial public source snapshot for **DR-89/nfsu2-vr**, published
on October 14, 2026. The project targets **Windows PCVR** and **Meta Quest 3
standalone** using Vulkan and OpenXR.

**DEV BUILD — NOT 100% TESTED.** Bugs, crashes, freezes, rendering errors,
audio issues, stuttering and performance problems may occur. Features and
compatibility are incomplete. Stable VR frame rates are not guaranteed.

## What is included

- The current handwritten port and runtime sources, including the local PCVR
  and Quest 3 development changes.
- Build and code-generation tools, dependency pins, configuration examples,
  regression tests and engineering documentation.
- The vendored xboxrecomp toolkit with its license texts and attribution.

## What is excluded

- Retail game files, disc images, `default.xbe` and the `NFSUNDER` data directory.
- Generated retail C, disassembly output and extracted executable sections.
- Game artwork and logos, textures, models, music, movies and game recordings.
- Built applications, APKs, libraries, caches, save files and private reports.
- Local toolchains, downloaded dependencies, device configuration and credentials.

The source history starts with this audited snapshot. The older local Git
history is not imported. The inherited branded Switch icon and loading image
are omitted; the loading header contains only a uniform, synthetic background,
and the Switch build defaults to its standard toolchain icon.

The only binary test fixtures are a tiny synthetic archive, a generated test
XBE and a generated sine-wave audio file. These contain no retail game data.
Format identifiers, checksums, compatibility signatures and port regression
tests remain in the source so it can recognize and validate user-supplied files.

## Building and testing

This publication is **source only**, not a ready-to-play download. Supply your
own supported Xbox USA / NTSC-U copy of Need for Speed: Underground 2 and
generate the private game code locally before building either platform.
Do not upload those inputs or outputs to this repository or its issues.

The platform notes describe previous development tests and unresolved issues.
Their references to `reports/`, captures and local APKs are historical evidence
paths, not files included in this publication. A successful build or unit test
does not establish complete gameplay, compatibility or performance testing.

Checks run on this publication copy: all 13 synthetic Python input-validation
tests passed, along with 12 native regression executables covering fences,
surface lookup, register shadowing, indices and packet decoding, texture
conversion, dispatch waits, culling, projection, feedback and shader generation.
Python source syntax, PowerShell script syntax and publication links were
checked. These are focused checks, not a full platform rebuild or an end-to-end
headset test of the published snapshot.

See [README.md](README.md) for setup, platform links and credits.
