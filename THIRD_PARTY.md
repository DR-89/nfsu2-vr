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

FFmpeg, SDL, OpenXR-SDK and glslang are fetched separately at the revisions listed in
[android/dependencies.json](android/dependencies.json). Android importer
dependency notices are retained in
[importer-third-party-notices.txt](android/app/src/main/assets/importer-third-party-notices.txt).
The Quest and PCVR players use libavformat, libavcodec, libswresample, libswscale and libavutil from FFmpeg 7.1.5 under LGPL 2.1 or later. The release includes the corresponding FFmpeg source archive. The exact codec configuration and rebuild steps are in [the media build script](tools/build-vr-vp6.ps1).

The new cabin and racing-glove assets were generated with Tripo for this VR
port. Their source models, fitting steps and runtime formats are documented in
[assets/cockpits/README.md](assets/cockpits/README.md). They contain no original
game geometry, textures or external logos. The gameplay still uses the user's
locally imported original game assets.

Need for Speed and Underground are trademarks of Electronic Arts. Meta Quest
is a trademark of Meta. This is an unofficial fan project and is not affiliated
with or endorsed by Electronic Arts, Meta or Microsoft. No original game
content is distributed in this repository.

The native community integration retains dependency licenses and author notices
under [src/community/vendor](src/community/vendor), including the nfsu2-web
Free Roam/auth code, cJSON, networking libraries and optional asset converters.
The consolidated [runtime notices](android/app/src/main/assets/runtime-third-party-notices.txt)
ship in both binaries. Optional community artwork and audio remain separate;
their preparation tools and notices do not grant redistribution rights to the
underlying assets.
