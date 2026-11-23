# CoreS3 audio record and replay

**Status:** In progress; implementation approved on 2026-11-23.

**Outcome:** Tap the awake CoreS3 to record a two-second clip with its built-in
microphone, hear it automatically through its speaker, and return to idle while
the face continues updating.

## Baseline and scope

- Request: start audio input/output. The user selected “Record and replay a
  short clip” on 2026-11-23 and approved this plan with “Please start.”
- Branch and starting commit: `main` at
  `27315b34946ea01e4149d45977048a5a56010a29`.
- Existing local edits to preserve: none at inspection.
- Implementation branch: `feat/audio-record-replay`, created after approval.
- In scope: one local record/replay interaction, bounded RAM buffering, face
  integration, error recovery, native decision tests, and device instructions.
- Out of scope: speech recognition, AI, networking, saved recordings, streaming,
  simultaneous capture/playback, volume UI, and external audio hardware.
- Estimated total added and deleted lines: 700–950 across code, tests, and docs,
  including this plan. Reassess scope if the estimate exceeds 1,000.

## Requirements and decisions

- A distinct press while idle starts one recording. Capture 32,000 signed
  16-bit mono samples at 16 kHz (two seconds, 64,000 bytes). Reuse a fixed buffer;
  no per-cycle allocation. Keep its contents valid until playback releases it.
- Show `LISTENING` during capture, `THINKING` during handoff if needed, and
  `SPEAKING` during playback. Return to `IDLE` after playback completes. This
  deliberately replaces the manual touch-driven visual state cycle.
- Ignore presses during an active cycle, including a held starting touch;
  do not enqueue recordings or interrupt playback. A new press after completion
  starts a fresh cycle. Never replay an incomplete or previous clip after failure.
- Sleep after 30 seconds idle. A press while sleeping wakes to idle only;
  the next distinct press records. Do not sleep during a healthy audio cycle.
  Completion and failure refresh the inactivity timer, including across rollover.
- Disable the speaker before capture and the microphone before playback.
  Turn off active audio when returning to idle or handling failure. Use the
  existing M5Unified dependency, without new libraries or custom audio tasks.
- Queue each operation once and poll completion from updates; do not add
  loops or delays waiting for capture/playback. Inspect library lifecycle waits
  and verify their effect on-device; do not promise zero-latency driver switching.
- Detect initialization/request failures and give each active operation a
  five-second elapsed-time watchdog. Log failures once, release hardware/buffer
  ownership safely, return to idle, and allow a later press to retry. Never
  overwrite a buffer still owned by a driver; document library cleanup limits.
- Use a named, moderate initial speaker volume, proposed `128` on the M5Unified
  0–255 scale. Device validation determines whether gain/volume needs adjustment.
- `RobotController` owns the interaction and face notifications;
  `RobotStateMachine` owns testable decisions; a focused audio header/source owns
  M5Unified calls and buffer lifetime. Keep `main.cpp` to initialization/update
  wiring, `FaceRenderer` to drawing, and `RobotState.h` to shared state values.
- Preserve blinking and keep Serial state messages consistent with the face.
  No artificial thinking delay or new face UI is required.

### Research and dependency baseline

- Installed dependency inspected: M5Unified `0.3.0`. Dependencies remain unpinned
  in this slice; record resolved versions with implementation validation.
- [Official CoreS3 microphone example](https://docs.m5stack.com/en/arduino/m5cores3/mic)
  demonstrates alternating microphone/speaker operation.
- [Official M5Unified microphone example](https://github.com/m5stack/M5Unified/blob/master/examples/Basic/Microphone/Microphone.ino)
  and installed `Mic_Class`/`Speaker_Class` sources were inspected for queued
  capture, playback, completion, and lifecycle behavior. Adapt their hardware
  usage; their blocking demonstration loops do not satisfy this interaction.

## Ordered tasks

### T1: Add bounded microphone and speaker transport

**Scope and ownership:** New `include/RobotAudio.h` and `src/RobotAudio.cpp`
or equivalently focused names; this plan's transport evidence. No interaction
change yet. Keep the transport interface small and expose completion/failure
facts for the controller.

**Dependencies:** Existing M5Unified/CoreS3 environment; approval below.

**Acceptance:** Fixed two-second PCM buffer; mutually exclusive capture/playback;
one request per operation; reliable completion and explicit failure reporting;
safe buffer ownership and cleanup; repeated use does not allocate per cycle.
The unused transport must leave the existing firmware behavior unchanged.

**Checks:** `pio run -e m5stack-cores3`, `pio test -e native`, and
`git diff --check`. Inspect queue return values, completion semantics, memory
use, and cleanup against the resolved library. Demonstrate RED/GREEN for
testable transport behavior where practical; hardware capture and sound require
device observations and cannot be established by a host test or compilation.
Neither listed PlatformIO command uploads firmware.

**Proposed commit:** `feat(audio): add bounded cores3 recording and playback`

**Status and evidence:** Complete; independent review round 1 PASS.

- Owned files: `include/RobotAudio.h`, `src/RobotAudio.cpp`, and this T1 evidence.
  Baseline remains `27315b3` on `feat/audio-record-replay`; the approved untracked
  plan was the only pre-existing edit. No controller or interaction files changed.
- `RobotAudio` keeps a noncopyable 64,000-byte member buffer. Its status exposes
  capture completion, playback completion, and latched initialization/request or
  stopped-driver failures. Busy capture requests are rejected; playback requires
  `RECORDED`, so cancellation/failure cannot replay old or incomplete samples.
  T2 will own the five-second elapsed watchdog and once-per-failure logging.
- Source inspection, rather than a synthetic RED test, validates this hardware
  boundary: the existing native build excludes M5Unified and the transport.
  A mock that restated these calls would not establish I2S/task ownership or sound.
  Meaningful decision RED/GREEN belongs to T2's native state-machine tests.
- Resolved M5Unified `0.3.0`, `src/utility/Mic_Class.inl:788–801` publishes a
  completed capture with a release-store only after filling all elements;
  `:1234–1334` returns whether a request was queued and may wait if both slots
  are occupied. This transport starts from empty queues and submits once.
  `:1142–1222` synchronously joins capture before clearing pending requests.
- `Speaker_Class.inl:739–819` releases the source and clears the channel before
  final I2S output drains. `:193–235` configures DMA frame length/count;
  `:565` allocates a mixer block of `dma_buf_len` 32-bit words. After queue
  emptiness the transport polls an unsigned elapsed drain allowance of
  `ceil(2 * (dma_buf_count + 1) * dma_buf_len * 1000 / sample_rate)` ms,
  conservatively allowing two 16-bit samples per word for the DMA plus mixer.
  Default CoreS3 output is 48 kHz, 8 buffers of 256 frames: 96 ms. This is a
  capacity-based allowance, not an audible-completion API or scheduling guarantee;
  complete clip tails still require device observation. It does not wait out the
  clip duration in an update loop.
- `Speaker_Class.inl:1156–1211` joins its task before clearing buffer pointers;
  `:1214–1237` only queues stop markers. Cleanup therefore uses `end()`, never
  treats `stop()` or queue emptiness alone as permission to overwrite the buffer,
  and invalidates clip status only after cleanup returns. No application heap
  allocation, task, wait loop, or delay was added. Library begin/end still allocate
  and release I2S resources/tasks; lifecycle locks/joins have no overall deadline
  and speaker I2S writes can use `portMAX_DELAY`. A T2 watchdog can request cleanup
  but cannot preempt a stuck library call. Switch/cleanup latency and recovery
  remain explicit hardware validation gaps.
- Validation: `/Users/france/.platformio/penv/bin/pio run -e m5stack-cores3`
  exited 0; RAM 27,708/327,680 and flash 514,575/6,553,600 bytes, unchanged
  because T1 adds no transport instance or call. Integration memory is a T2
  check. Espressif32 `8.0.0`, Arduino `3.3.12`, M5Unified `0.3.0`; a full compile
  showed only the two existing M5GFX unused-variable warnings. The initial
  concurrent build/native run caused PlatformIO cache cleanup diagnostics;
  both exited 0 and were repeated sequentially without those diagnostics.
  `/Users/france/.platformio/penv/bin/pio test -e native` exited 0, 15/15 tests.
  `git diff --check` exited 0; separate new-file whitespace inspection passed.
  No upload or hardware observations; intelligibility, full tail, volume 128,
  ten cycles, responsiveness, and cleanup/failure recovery remain pending.
- Independent T1 review: `code_reviewer`, `gpt-6.1-sol`/`xhigh`, round 1/3
  PASS. It rechecked buffer ownership, the 96 ms DMA allowance, lifecycle
  limits, both PlatformIO commands, whitespace, and documentation links.

### T2: Integrate the record/replay interaction and regression coverage

**Scope and ownership:** `include/RobotController.h`, `src/RobotController.cpp`,
`include/RobotStateMachine.h`, `src/RobotStateMachine.cpp`, minimal initialization
in `src/main.cpp` if required, `test/test_controller/test_main.cpp`, `test/README`,
`README.md`, this plan, and concise status corrections in the product brief and
backlog. Update the roadmap only to reflect actual implemented progress and
pending hardware evidence. Change `platformio.ini` only if native source
selection genuinely requires it; no dependency/platform upgrades.

**Dependencies:** T1 independently reviewed and committed.

**Acceptance:** The interaction and all edge cases above work with simulated
completion/failure inputs in native tests. Notifications and driver requests
occur once per transition. Retain meaningful inactivity/rollover regression
coverage; replace obsolete manual cycling expectations. Audio unavailability
leaves the face, idle sleep, and wake usable. README describes the new gesture
and device validation, distinguishing firmware implementation from verification.

**Checks:** First demonstrate meaningful failing native tests for the new
decisions, then pass `pio test -e native` and
`pio run -e m5stack-cores3`; run `git diff --check` and check documentation paths,
links, and commands. Use `~/.platformio/penv/bin/pio` if `pio` is absent from PATH.
No uploads are included in this plan's implementation authorization.

**Hardware checks, pending separately authorized upload:** On the intended
CoreS3, record intelligible speech and hear the complete clip at reasonable
volume; check ten consecutive cycles, rapid presses and held touches while
busy, continued blinking, Serial/face agreement, idle timeout, repeated wake
and record cycles, and absence of resets, stuck states, or sustained distortion.
Record firmware revision, resolved dependencies, observations, and any gaps.

**Proposed commit:** `feat(audio): connect touch recording to automatic replay`

**Status and evidence:** Pending.

## Review and validation

- Approval record: on 2026-11-23 the user replied “Please start” to the published
  plan and explicit approval request. This approves T1/T2, their proposed
  interaction/defaults, checks, independent review, and separate atomic commits.
  Hardware upload and push remain outside the approved scope.
- Route to the loaded `implementer` role (`gpt-6.1-sol`, `xhigh`)
  with this plan, user decisions, baseline, and approval record. It uses fresh
  developer/independent reviewer pairs per task, serially, with explicit runtime
  selections and at most three review rounds per task per the workflow.
- Planning review: main-session self-review. T1 used a fresh developer and
  independent reviewer, both `gpt-6.1-sol`/`xhigh`, for driver ownership risk.
- Baseline checks on 2026-11-23: `~/.platformio/penv/bin/pio test -e native`
  passed 15/15 tests (exit 0); `~/.platformio/penv/bin/pio run -e m5stack-cores3`
  passed (exit 0), RAM 27,708/327,680 bytes, flash 514,575/6,553,600 bytes.
  Resolved Espressif32 `8.0.0`, Arduino `3.3.12`, M5Unified `0.3.0`.
  Two existing M5GFX unused-variable warnings appeared; no project-source errors.
  `git diff --check` passed; the new plan's whitespace and existing ownership
  paths were checked separately. Only this untracked plan was added.
- T1 build/native checks and independent review passed; T2 remains pending.
- Hardware: no device checks or uploads performed. The pre-existing controller
  refactor's device regression gap remains until relevant checks are observed.

## Handoff

- Plan approved; implementation routed through the configured implementer.
- Commit T1 after its checks and independent PASS, then implement/review/commit
  T2 separately. Update this file with task-to-commit mapping and evidence.
- No push authorized. Report any hardware checks still outstanding explicitly.
