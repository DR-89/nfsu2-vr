# CopChase guest-to-record-worker ordering

`copchase_record_flow.{h,cpp}` is the serialized game-thread coordinator used
by `copchase_bridge.inl`. It performs no file IO and never waits for the worker.
The worker's bounded session queue can return `FULL` before accepting an event.
The coordinator retains the exact event in a fixed four-entry queue and retries
it before selecting another profile/model or authorizing another pursuit.

The important case is a queued `START`: disabling gameplay while waiting causes
the game to publish `CANCEL`. That cancellation must not disappear just because
the new frame is empty or `START` has not reached the worker. The coordinator
binds it to the identity captured by `START` and preserves `START → CANCEL`.
The next real pursuit therefore obtains a new durable session token. A completed
terminal result waiting for capacity is preserved when later context loss emits
another cancellation; it is neither overwritten nor moved to the next profile.

Profile reload, world/player changes, model changes and missing identity cancel
an unfinished pursuit before worker selection changes. Repeated changes while
blocked do not add repeated cancellations. An unexpected callback error latches
a hold rather than dropping the payload or enabling more pursuits. Normal
bridge operation needs at most two pending lifecycle events, because gameplay
is held until the queue drains; four slots provide a bounded defensive limit.

Call `before` to obtain both the current identity frame and the ready/heat state.
Always call `after` with consumed gameplay events, including while `before`
returns false. The original frame's settlement metrics remain available to the
owned bust/fine adapter; the coordinator does not invent counters or alter cash.

`copchase_record_flow_holding()` distinguishes a recoverable readiness or
capacity wait from missing identity or a latched error. The host feeds it to
`CopChaseGameGuards.settlement_pending`, preserving the user's enabled setting.
That guard freezes pursuit timers, counters and ownership instead of resetting
the state. An accepted terminal still holds until the worker queue drains:
`ready/heat_ready` alone may describe the preceding model heat while the file
flush runs. The hold ends only after the committed result is reflected.

`tests/copchase_record_flow_test.cpp` forces backpressure deterministically and
checks event identity/order, new tokens, terminal preservation across profile
changes, repeated world/model changes, empty-frame cancellation and worker IO
holds. It also runs the production callback adapter against the actual session
worker in a unique temporary directory and verifies token/heat persistence.
It does not launch the game or write retail saves.

Verified on 2026-11-09: 75 MSVC x64 checks passed with `/O2 /W4 /WX`.
Android NDK 29 compiled and linked the helper, tests and real session/records
worker for ARM64/API30 with `-Wall -Wextra -Werror`; no Quest execution is
claimed by that build check.
