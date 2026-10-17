# Development source publication

This is the public development source for **DR-89/nfsu2-vr**, first published
on October 14, 2026 and updated October 17. The project targets **Windows PCVR** and **Meta Quest 3
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
- Original generated Tripo cabin and racing-glove models, their fitted runtime
  assets, preparation scripts and fit metadata under `assets/cockpits/`.
  These are newly generated assets, not extracted game models or textures.

## What is excluded from Git

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

The Git repository contains source only. The development release also offers
a Meta Quest 3 APK and Windows PCVR package, with no original game assets.
Supply your own supported Xbox USA / NTSC-U copy of Need for Speed: Underground 2.
Both built applications offer game-data import; the PCVR application prompts
for an ISO/XISO automatically when the required data is absent. Source builds
still require generating the private game code locally. Do not upload your
game inputs, generated C or saves to the repository or its issues.

The platform notes describe previous development tests and unresolved issues.
Their references to `reports/`, captures and local APKs are historical evidence
paths, not files included in this publication. A successful build or unit test
does not establish complete gameplay, compatibility or performance testing.

Checks run for the initial October 14 publication: all 13 synthetic Python input-validation
tests passed, along with 12 native regression executables covering fences,
surface lookup, register shadowing, indices and packet decoding, texture
conversion, dispatch waits, culling, projection, feedback and shader generation.
Python source syntax, PowerShell script syntax and publication links were
checked. These are focused checks, not a full platform rebuild or an end-to-end
headset test of the published snapshot.

See [README.md](README.md) for setup, platform links and credits.

## October 14 binary release update

The PCVR first-run importer passed 43 synthetic native assertions, a full
import of the user's local USA ISO with SHA-256 verification, and inspection
of the native setup dialog, file picker and cancellation. Both the Windows
Release executable and Quest debug APK were rebuilt. No new complete gameplay
or performance acceptance test is claimed. Native dependency notices are
bundled in both packages; the APK also retains importer dependency notices.

The release's initial source tag is retained. For the updated binaries use
the matching source ZIP and exact commit identified in BUILD-INFO.json and
the release notes, rather than the initial tag's automatic source links.

## October 15 cockpit release

The new tag identifies the exact source snapshot for this release. Its source
archive includes the generated cabin and gloves, while game inputs, private
memory/image captures and generated retail C remain excluded.

Native Quest testing accepted the cockpit/camera fit, controls, mirrors,
vibration, instruments, intact pause menu and stable grip contact. Focused
Windows and Quest tests cover steering/contact transitions, projection, UI and
mirror geometry. PCVR's new cockpit behavior still needs a separate headset
test; neither complete compatibility nor stable 72 FPS is claimed.

Both platforms were rebuilt for this release. All 15 Windows regression
executables, four native Quest test executables, 32 Android importer tests and
13 Python input-validation tests passed. The APK retains the previous signing
certificate and advances to version code 2. Detailed validation and artifact
checksums accompany the release in BUILD-INFO.json and SHA256SUMS.txt.

## October 17 settings and vehicle-cockpit release

This snapshot adds persistent pause-menu VR settings, SDL gamepad input,
per-vehicle cabin fitting, body/driver-proxy and mirror corrections, seated
instruments, the console map and cockpit-only window-tint removal. The public
source retains only handwritten code, generated cabin/glove assets and
synthetic fixtures. Retail geometry/memory/draw captures used for the 29-car
regressions are private and are not part of the repository or downloads.

Both platform builds complete. Seventeen Windows regression executables,
32 Android importer unit tests and 13 Python input tests pass for this release.
The native Quest cockpit suite passed on the headset before packaging, with
29-car body/driver/mirror fixtures and 6,417 captured draw records. User testing
confirmed recent seat, mirror, gauge, map and clear-window behavior. The final
APK rebuild advances package metadata to 0.3-dev / version code 3 and restores
the previously released textured glove asset after a local diagnostic overwrite;
its signing certificate matches the previous release. The headset was no
longer connected for another final-package launch. PCVR headset acceptance
and stable 72 FPS across gameplay remain outstanding.

Packaging now rejects invalid glove files or checksum/embedded-asset mismatches.
The restored glove passes format, malformed-input and steering/contact tests.
