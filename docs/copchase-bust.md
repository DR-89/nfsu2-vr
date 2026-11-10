# Owned native pause and fine confirmation

`copchase_bust.{h,cpp}` coordinates one BUSTED event's original-game pause,
host VR confirmation, native fine transaction and ordinary resume. It does
not render UI, inject button states, open saves, or set a fabricated pause
flag. The host advances it on the guest/main thread and copies its status
to a thread-safe VR bridge. UI inputs carry the exact durable session token
and return to the game thread before calling `copchase_bust_confirm`.

An arm captures verified profile/world/player generations plus world, Car,
director and controller addresses. It waits up to five seconds for safe
driving. A pre-existing user pause, dialog or modal is never taken over.
The checked host context supplies cinematic/stream/network eligibility;
the controller also rechecks the director pointer/state, original pending
and disabled-driving bytes, the Car controller and connected-controller
byte. It calls the original input handler once and waits up to five seconds
for actual root `UI_Pause.fng` with director state6. Only then are `ready`
and `owned_pause` true. A read of the current native cash value provides
the display balance; it is not a cached balance used for payment.

Explicit confirmation requires the same durable token, identity and owned
root pause. `copchase_fine` arms only at this point. Its identity callback
is wrapped to recheck the entire modal ownership context before and during
the transaction, so a nested dialog or changed profile after a balance
getter cannot silently authorize a charge. The helper performs the verified
native cash operation once. Pre-mutation I/O failure may be retried only
by another explicit confirmation; abandoned unattempted work preserves the
token watermark. A possibly post-mutation failure latches permanently and
must never be retried by recreating this controller in the same host session.

After payment, the status exposes `paid` and the host closes its VR panel.
The controller waits for `context.modal_visible==0` before requesting
ordinary Pause Resume once. It waits up to five seconds for actual HUD and
director state4 before completion. Changed identity, a lost/nested root,
clock reversal or failed/expired transition relinquishes ownership and
never blindly resumes another screen or repeats a charge. Waiting for a
person to confirm a correctly owned root pause has no inactivity timeout.
Destroying the controller performs no implicit resume or payment.

## Original ABI evidence

SHA-pinned original Xbox disassembly and a detailed audit are saved in
`build/copchase-re/pause/NATIVE-PAUSE.md` and adjacent `.asm` files.

| Operation | Original call | ABI |
| --- | --- | --- |
| Pause request | `1F2B50(Car, controller, 0xBD, 0)` | ECX=Car; three stack words; callee `RET 0xC` |
| Restore owned pause | `1E45F0(director, 1)` | ECX=director; one stack word; callee `RET 4` |
| Queued restore event | `1E4590(director, 8, 1)` | Called by `1E45F0`; callee `RET 8` |

The event-name table at `3A4690` stores `{0xBD,0x358588}` and the string
`JOY_EVENT_PAUSE`. Car callback registration `1F5E50` proves the Car context
and controller `[Car+8]`. Pause input queues director event7 after native
eligibility checks; driving state4 handler `1FB420` preserves prior state,
pauses native audio and enters state6. State6 handler `1F6920` opens the
pause package. The owned-pause exit uses `1E45F0`, which queues event8 with
argument1. State6 restores the saved state at director+0x24 through `1F35B0`
after native audio restoration. Both state6 and saved state4 are checked
before payment and exit. Native state transitions remove the package and
reset input; no director state is written by the adapter.

The earlier audit incorrectly called package event `451E768E` Resume.
That event queues event11, whose race branch resets the director and queues
event22. It is not used. The corrected one-argument ABI is independently
confirmed in `build/copchase-re/quickrace/native-restart-events.asm` and
`build/copchase-re/pause/resume-recheck.asm` (function SHA-256
`3b1e9ef4f2db39fbe896eacd64033030cef4d939e3026562f4515a5d61a9cde7`).

Career functions `AFBD0`/`C1AF0` are not this pause resume route. The ASI's
custom `C0B5C480/481/490/491` message hashes depend on PC-only hooks; this
adapter does not pass them to an unverified Xbox dialog builder.

## Integration and verification

`CopChaseBustHost.fine.call` must support the two audited calls above in
addition to the native cash getter/add functions used by `copchase_fine`.
Both pause and exit use their verified nonzero thiscall contexts. Callback completion is
distinct from the guest EAX value; no success boolean is inferred from EAX
for these void/queued methods. `context.root_pause` must use the actual
active frontend-package list, not a cached package or host overlay flag.
`context.can_pause` includes the existing cinematic/stream/network guards.

`tests/copchase_bust_test.cpp` checks exact native call arguments, one-shot
ownership, delayed root observation, pre-existing user pause/dialogs,
controller/pending gates, explicit durable tokens, duplicate confirmations,
live balance clamping, zero cash, every identity change, nested dialogs
inside payment callbacks, pre-write retries/abandonment, post-write uncertain
latching, transition timeouts, failed calls and delayed VR panel closure.
No game or original save is launched or modified by these synthetic tests.

Failures after an observed owned pause publish a separate, non-chargeable
notice. `failure_visible/failure_dismissable` are true only for the same
identity and strict original root pause. Nested dialogs hide the notice until
that root returns; another profile/world or a return to driving discards it.
`copchase_bust_dismiss(token,now)` acknowledges the notice without charging or
sending a Resume event, leaving the ordinary pause menu to the user. An
uncertain cash mutation stays latched after dismissal. The panel distinguishes
unknown payment status from confirmed payment followed by failed resume.

The host sets `QuestCopChaseBust.failed` from `failure_visible`, and `uncertain`
only for `COPCHASE_BUST_PAYMENT_UNCERTAIN`. It snapshots status before consuming
a UI token and routes failure acknowledgement to `dismiss`, otherwise to
`confirm`. A new failure/error state on the same token requires a fresh button
release before A; the held payment press cannot acknowledge the resulting
error. `tests/copchase_panel_test.c` covers that handshake and panel bounds.

Verified on 2026-11-09: all 245 MSVC x64 `/O2 /W4 /WX` checks passed. Android
NDK 29 compiled and linked the same module and test for ARM64/API30 with
`-Wall -Wextra -Werror`; this is not a Quest device execution claim.
