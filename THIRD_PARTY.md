# Credits and third-party notices

This VR development port is based on
[antoxa2584x/nfsu2-sw](https://github.com/antoxa2584x/nfsu2-sw), starting from
revision `6e19726414e829d808f7acb0258c0902f04a923a`, with the current local
Windows PCVR and Meta Quest 3 development changes maintained by DR-89.
The inherited Switch/Linux code and engineering notes retain their upstream
attribution.

The vendored [xboxrecomp](https://github.com/sp00nznet/xboxrecomp) toolkit
retains its [MIT license](xboxrecomp/LICENSE),
[third-party notices](xboxrecomp/NOTICE),
[additional license texts](xboxrecomp/LICENSES/README.md) and
[contributor credits](xboxrecomp/CONTRIBUTORS.md). Some components have their
own licenses; consult those notices and individual source headers.
The restored generator modules and their source revision are listed in
[android/UPSTREAM-REPAIRS.txt](android/UPSTREAM-REPAIRS.txt).

SDL, OpenXR-SDK and glslang are fetched separately at the revisions listed in
[android/dependencies.json](android/dependencies.json). Android importer
dependency notices are retained in
[importer-third-party-notices.txt](android/app/src/main/assets/importer-third-party-notices.txt).
This publication does not replace or extend the licenses of upstream files.

The new cabin and racing-glove assets were generated with Tripo for this VR
port. Their source models, fitting steps and runtime formats are documented in
[assets/cockpits/README.md](assets/cockpits/README.md). They contain no original
game geometry, textures or external logos. The gameplay still uses the user's
locally imported original game assets.

Need for Speed and Underground are trademarks of Electronic Arts. Meta Quest
is a trademark of Meta. This is an unofficial fan project and is not affiliated
with or endorsed by Electronic Arts, Meta or Microsoft. No original game
content is distributed in this repository.
