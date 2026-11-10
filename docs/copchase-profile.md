# Native profile read provenance

`copchase_profile.{h,cpp}` proves which save file supplied an exact native
deserialization buffer. It neither reads payload memory nor accesses the
filesystem. It observes successful kernel reads, records destination ranges,
and produces a candidate external-record ID only when coverage is complete.
It never uses a profile display name, selected menu row or last-read file.

Startup takes a canonical absolute `.../UDATA/4541005a` title root. The host
must resolve that root and actual opened-file paths through the OS. Windows
`GetFinalPathNameByHandle` extended drive and UNC forms are accepted; POSIX
paths are case-sensitive. Lexical path parsing rejects traversal, component
aliases, alternate data streams, relative paths, extra directory depth,
wrong title roots and invalid UID shapes. Each source must be exactly
`<root>/<12 hexadecimal UID>/<filename>`. `SaveMeta.xbx`, `SaveImage.xbx`
and `TitleMeta.xbx` are excluded. Resolving symlinks is the host's job: a
pure path parser cannot determine which file a symlink actually opened.

The tracker retains at most 64 segments for at most 30 seconds each. Its
observer receives every successful read, including reads whose path is
unavailable or unrelated. An intersecting new destination range removes
all intersecting older segments before it records any new provenance.
Therefore a partial overwrite cannot combine new bytes with stale proof.
A complete fresh read can establish a new identity for a reused buffer.
Zero-byte reads overwrite nothing. Pointer/offset wrap, backwards clock,
capacity overflow and explicit reset discard proof conservatively.

A claim requires gap-free coverage of the requested buffer from one exact
resolved file and one handle, with continuous source file offsets. Reads
may arrive out of order, and a whole-file read may enclose the requested
payload. A separate header destination is allowed; the native decoder's
header checks are independently verified. A claim consumes intersecting
segments on success or failure, so the proof cannot be reused. The module
returns the first source offset and does not guess whether it should be 8.

The stable ID is `4541005a_<UID>_<filename digest>`. UID and ASCII filename
letters are normalized to lowercase; other UTF-8 bytes remain exact. The
digest is the first 128 bits of SHA256.
This is 54 safe ASCII characters and fits the records module's 64-character
limit. Copies of the same UID/file retain identity across PCVR and Quest;
renaming the payload filename intentionally creates a different identity.
`proof.container_uid` also exposes the separately verified 12-hex container
UID; it does not by itself prove that a container can only hold one profile.
The supplied full ID is stable across restarts, not across filename renames.
The configured title root is an authorization boundary, not part of the
portable ID. Separate storage namespaces with intentionally reused UID/file
pairs need an additional host namespace before sharing external records.

## Native commit boundary

Static Xbox evidence is recorded in
`build/copchase-re/settlement/PROFILE-IDENTITY.md`, `save-task.asm` and
`profile-decoder-final.asm`. The accepted edge is whole-profile decoder
`0x1413C0` returning to `0xC3758`, manager `0x406C90`, mode other than 4,
task `[0x3F9D14]`, payload `[task+0x1C]`, length `[task+0x54]==0xD218`,
header `[task+0x28]==0x10D`, matching header payload length, and task error
`[task+0x50]==0`. Only after the original decoder returns with loaded flag
`[0x406CC0]==1` may the host commit the candidate ID. Rollback restoration
at return `0x141428`, mode-4 car import, new unsaved profiles and unproven
decodes do not establish persistence. Reset/new-profile paths must clear
the host's committed identity as well as pending tracker proof.

The global observer/claim wrappers use a mutex and monotonic host clock.
The explicit tracker API accepts injected timestamps for deterministic
tests and requires caller serialization. Neither API opens or changes
retail saves, game cash or mod records. Host/kernel/CMake wiring is separate.

## Verification

`tests/copchase_profile_test.cpp` uses deliberately non-dereferenceable
synthetic addresses. It covers complete and fragmented loads, independent
header buffers, read order, gaps, offset discontinuities, mixed UID/file/
handle sources, overlap invalidation, complete buffer reuse, one-shot claims,
age and clock wrap, range overflow, the 64-segment bound, explicit resets,
path traversal and namespace attacks, Windows case/extended UNC forms,
POSIX containment and the fixed cross-platform SHA256 identity vector.
No original save content is required or read by these tests.

Verified on 2026-11-09: 105 x64 checks passed with MSVC `/O2 /W4`;
the same module and test compiled and linked for Android ARM64/API30 using
NDK29 and `-Wall -Wextra -Werror`. The ARM64 executable was not run on a
device. Runtime profile-load integration remains a separate host check.
