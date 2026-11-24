# Record speech beyond two seconds

**Status:** Five-second pause test/docs follow-up passed independent review.
CoreS3 validation remains pending.

The original implementation record below describes commit `ddb6132`, including
its historical 800 ms pause and 500-unit threshold evidence. Current production
settings are a five-second pause and 100-unit threshold; see the approved
[follow-up](#approved-five-second-pause-follow-up) for current scope and checks.

**Outcome:** A tap starts speech capture. The robot records and transcribes
speech that continues beyond the current two-second cutoff. A sustained pause
ends the clip, with a 10-second safety cap on capture.

## Baseline and scope

- Request: Remove the two-second recording limit so the user can finish an
  utterance and have its later words transcribed.
- Inspected baseline: `main` at `ae1f7f0fcbb2bffe024d3822713ba4881d1a6309`,
  matching `origin/main`.
- Implementation branch: `feat/speech-silence-stop` in a separate worktree.
- Existing local edits: The untracked plan draft was the only change in the
  original checkout. It was copied into the new worktree for this task.
- Approval: On 2026-11-24, after receiving the revised plan, the user said
  “Move to a new branch and worktree and implement this”. The request approves
  the revised plan and its task commit. Device upload remains pending approval.
- The [tap-to-transcribe baseline](tap-to-transcribe.md) still has pending
  CoreS3 validation. Verify the full tap-to-transcript path on the new firmware.
- In scope: CoreS3 audio capture and silence-stop behavior, variable-length clip
  handoff to the retained playback API and transcription, the local backend's
  request contract, and focused checks and docs.
- Out of scope: Streaming recognition, cloud services, a new hardware mic,
  changing the tap-to-start interaction, or broad audio quality tuning.
- Scope assessment: Medium change across audio capture, transcription framing,
  backend request validation, and tests. One task delivers the updated wire
  contract and the firmware that uses it.

## Requirements and decisions

### Capture and state transitions

- Retain tap-to-start and the existing listening, thinking, and idle flow.
  Measure energy on 20 ms PCM frames using the existing audio path. Keep the
  energy threshold and stop timings tunable in firmware. Tune the threshold on
  CoreS3 using ordinary speech, quiet speech, and room noise.
- Allow an initial 3 seconds for the first above-threshold frame. After that
  frame, continue until 800 ms of consecutive below-threshold frames. Each
  above-threshold frame resets the silence timer. Require at least 120 ms of
  cumulative above-threshold audio before submitting. A quiet tap or a clip
  below that minimum returns to idle with one no-speech diagnostic and zero
  uploads. Measure these windows from completed PCM samples. The durations are
  initial settings for device validation.
- End capture at 160,000 samples (10 seconds of 16 kHz mono PCM), even if sound
  continues. Retain the captured leading and trailing audio through the stop
  frame. Submit a valid clip exactly once at either stop condition. Replace the
  five-second listening watchdog with an 11-second wall-clock watchdog from
  the start of listening. Accept a finalized clip already available on that
  update. If capture is still pending at the deadline, stop it, report a
  capture timeout, and return to idle without submitting. Use unsigned elapsed
  time for the watchdog. A new tap can start after cleanup.
- Preserve gap-free capture by keeping consecutive microphone requests queued
  and using M5Unified's buffer-release callback to track completed chunks. The
  callback stays short by publishing completed ranges and refilling the queue
  as needed. Run energy detection and controller transitions outside the
  callback. Treat a failed refill that would interrupt capture as a capture
  error. Once the stop frame is selected, discard any samples after it and wait
  for microphone ownership to end before exposing the finalized clip.

### Buffer and transcription contract

- Use one reusable 320,000-byte PCM buffer in PSRAM. If it cannot be allocated,
  report capture failure and return to idle without submitting. Pass the actual
  sample count with the borrowed clip and retain it until the worker acknowledges
  cleanup. The retained playback API uses the same sample count. The tap flow
  continues directly from capture to transcription.
- Send the exact PCM byte count in `Content-Length` and as the request body.
  `POST /v1/transcriptions` accepts signed 16-bit little-endian mono PCM at
  16 kHz with an even `Content-Length` from 3,840 bytes (1,920 samples) through
  320,000 bytes inclusive. Return HTTP 413 above the cap and HTTP 400 for other
  invalid lengths, unsupported framing, or incomplete bodies. Pass the exact
  body to the existing WAV writer so the RIFF and data sizes match the clip.
- Keep accepting existing 64,000-byte requests and document updating the local
  backend before using the new firmware. Verify the maximum clip with the real
  local model under the existing five-second body, 20-second inference, and
  30-second firmware request deadlines.

## Ordered tasks

### T1: Add bounded variable-length capture and silence stop

**Scope and ownership:** `RobotAudio`, `RobotController`, `RobotStateMachine`,
and `RobotTranscription`, focused native tests and source selection as needed,
`backend/server.mjs`, backend tests and contract docs, and the user-facing
recording instructions.

**Dependencies:** M5Unified's microphone buffer-release API, available in the
installed 0.3.0 version. CoreS3 with PSRAM and a configured local Whisper
backend are needed for end-to-end validation.

**Acceptance:** A single tap captures an uninterrupted phrase that extends
beyond two seconds and the transcript includes its later words. A pause shorter
than 800 ms keeps the same clip open. A sustained pause ends it once. Continuous
above-threshold audio reaches exactly 160,000 samples and submits once. Onset
delay, quiet taps, brief noise, mic stalls, and allocation failures follow the
outcomes above. Upload and the playback API use exactly the finalized length.
Repeated taps after completion or failure work, and the face remains responsive.

**Checks:**

- `pio test -e native` for the initial-silence, minimum-activity, hangover,
  sample-cap, and watchdog boundaries, including rollover, exactly-once
  submission, and retry after a stalled capture.
- `node --test backend/server.test.mjs` for minimum, legacy, and maximum clips,
  rejected undersized, odd, oversized, and incomplete bodies, and the exact WAV
  header and PCM bytes. Run a separate 10-second local-model smoke test and
  record the response and elapsed time.
- `pio run -e m5stack-cores3` for the firmware compile check. On an authorized
  CoreS3 firmware build, exercise speech beyond two seconds, delayed onset, a
  short pause followed by more speech, a quiet tap, room noise, and continuous
  speech to the cap. Check repeated capture and upload with Wi-Fi active for
  memory stability, response timing, and recovery. The listed build and test
  commands leave the device untouched. Upload only during authorized device
  validation.

**Proposed commit if authorized:** `feat(audio): stop speech capture after silence`

**Status and evidence:** Implemented in the approved isolated worktree. Native
sample/state tests, backend tests, real local-model maximum-clip smoke, and
missing/placeholder-configuration firmware builds pass. Hardware checks remain
pending; no device upload ran. Independent review round 1 passed. The
authorized task commit follows the review.

**Implementation:** Two 320-sample microphone requests stay queued, with the
release callback publishing completed sample ranges and refilling a freed slot.
The main task processes mean absolute frame energy, selects the stop frame, and
joins microphone ownership before exposing a clip. A preempted initial queue or
failed refill is a capture error. The single 320,000-byte PSRAM allocation is
reused; borrowed clips block capture/playback/stop mutation until transport
acknowledgment. Playback and HTTP use the finalized sample count. The backend
allocates the declared body length and wraps exactly those bytes in WAV.
M5Unified is pinned to the freshly resolved/tested 0.3.0 callback API.

**Additional owned files:** `platformio.ini`, `test/test_audio/test_main.cpp`,
`test/support/M5Unified.h`, `test/support/esp_heap_caps.h`, `test/README`, root
`README.md`, and concise product brief/backlog/roadmap updates. Renderer and main
loop sources are unchanged. Dependencies introduce no new library.

## Review and validation

- Developer configuration: `gpt-6.1-sol`, `xhigh`, selected for async capture,
  buffer ownership, timing, and end-to-end framing.
- Independent reviewer configuration: `gpt-6.1-sol`, `xhigh`, selected separately
  for callback ownership, clip finalization, deadlines, and contract validation.
- Review round 1 of 3: `PASS`, with no actionable findings. The configured
  reviewer verified all 19 frozen file hashes and the full tracked/untracked
  patch. Independent checks passed for 34 native cases, 33 backend tests, JS
  syntax, whitespace, local documentation links, and the unconfigured firmware
  build (RAM 34,724 bytes; flash 609,207 bytes). The real maximum-clip model
  smoke returned HTTP 200 in 674 ms with both later clauses. The reviewed source
  stayed unchanged; the implementer recorded this verdict before committing.
- Starting state in the worktree: `feat/speech-silence-stop` at
  `ae1f7f0fcbb2bffe024d3822713ba4881d1a6309`; only this untracked approved plan
  was present. The original checkout and ignored credentials were preserved.
- RED before production changes:
  - `~/.platformio/penv/bin/pio test -e native`: exit 1. The updated controller
    test expected capture still pending at 10,999 ms, but the old five-second
    watchdog transitioned early; 24 other existing cases passed.
  - `~/.platformio/penv/bin/pio test -e native -f test_audio`: exit 1. Four
    new production-audio fixture cases failed: one queued request instead of two,
    32,000-sample requests instead of 320-sample frames, and refill failure
    incorrectly becoming a recorded clip. No production edits preceded these.
  - `~/.nvm/versions/node/v24.22.3/bin/node --test backend/server.test.mjs`:
    exit 1; minimum input returned 400, intermediate/max input returned 413,
    and odd 64,001-byte input returned 413 instead of 400 (27 pass/5 fail,
    including the failed parent subtest).
- GREEN on the candidate:
  - `~/.platformio/penv/bin/pio test -e native`: exit 0, 34/34 cases
    (19 controller, 8 production-audio fixture, 7 protocol). Includes exact sample
    windows, delayed onset, minimum activity, short-pause reset, 160,000-sample
    cap, discarded tail, delayed release callback, driver join before borrowing,
    PSRAM failure/reuse, failed refill/retry, no-speech zero-upload decisions,
    and 11-second watchdog boundaries/rollover with finalized-clip precedence.
  - `~/.nvm/versions/node/v24.22.3/bin/node --test backend/server.test.mjs`:
    exit 0, 33/33 tests. Minimum, intermediate, legacy and max PCM/WAV checks,
    malformed/duplicate/chunked/encoded/odd/short/oversized framing and partial
    min/legacy/max bodies, plus prior process and deadline coverage pass.
  - `~/.nvm/versions/node/v24.22.3/bin/node --check backend/server.mjs` and
    `~/.nvm/versions/node/v24.22.3/bin/node --check backend/server.test.mjs`:
    both exit 0.
  - `~/.platformio/penv/bin/pio run -e m5stack-cores3`: exit 0 both without
    `LocalConfig.h` and with temporary fake settings only. The fake file was
    removed afterward. Missing config: RAM 34,724/327,680 bytes (10.6%), flash
    609,207/6,553,600 (9.3%). Fake config: RAM 54,660/327,680 (16.7%), flash
    1,100,007/6,553,600 (16.8%). These link figures exclude the 320,000-byte
    runtime PSRAM allocation. Fresh resolution: espressif32 8.0.0, Arduino
    3.3.12, M5Unified 0.3.0, M5GFX 0.3.0. Only two existing M5GFX unused-variable
    warnings were emitted.
  - Separate Node 24 loopback smoke used `createTranscriptionServer` with its
    unchanged five-second body/20-second inference deadlines, a 30-second fetch
    deadline, local `~/.cache/stackchan-whisper-t1/whisper.cpp/build/bin/whisper-cli`,
    `models/ggml-base.bin`, and the first 160,000 PCM samples of `samples/jfk.wav`.
    The WAV format was checked as mono PCM16/16 kHz, and exactly 320,000 bytes
    were POSTed. Exit 0, HTTP 200 in 370 ms. Transcript: “And so my fellow
    Americans, ask not what your country can do for you, ask what you can do
    for your country.” An assertion on both later clauses passed. This is a
    local backend/model check. CoreS3 capture and Wi-Fi validation remain pending. The public
    maximum-clip command is in [backend/README](../../backend/README.md#offline-recognition-smoke-test).
  - `git diff --check`: exit 0. Owned new files also checked directly for
    whitespace; local Markdown paths/anchors and updated contract checked.
- Hardware: no CoreS3 capture or upload was performed or authorized. Pending:
  uninterrupted phrase after two seconds, ordinary/quiet speech and room-noise
  threshold tuning, delayed onset, short pauses, quiet tap, continuous sound to
  the cap, repeated capture/upload with Wi-Fi, PSRAM/stack/heap headroom,
  watchdog cleanup/retry, and Serial/face/blink/sleep/wake agreement. Native
  fixtures model queue ownership and completed PCM; they do not prove I2S/DMA
  continuity, real thread scheduling or microphone quality.
- Limitations: Initial mean absolute energy threshold is 500 PCM units. Loud
  steady noise can keep capture open until the cap; quiet speech can remain
  below threshold. Driver `end()` and SDK close may wait inside library calls.
  Record threshold/timing observations during device validation.

## Handoff

- T1 passed independent review and automated checks. The implementation is
  committed as `feat(audio): stop speech capture after silence`; the task commit
  contains this plan, firmware, backend, tests, and matching docs.
- The original checkout retains its untracked plan draft on `main`. The isolated
  `feat/speech-silence-stop` worktree contains the task.
- Device validation remains pending. Capture uses an initial energy threshold
  of 500 PCM units and the approved timing settings, all of which need CoreS3
  observations. Update the backend before using the firmware.
- No device upload or push was performed.


## Approved five-second pause follow-up

- Task: T1 tests/docs follow-up. Baseline: `feat/speech-silence-stop` at
  `d090d59ca36a64a98ddca5306741be197b7ace52` in the isolated
  `/tmp/stackchan-pause-tests.Mk163C` worktree; starting tracked/untracked state
  was clean. The original checkout is outside this task.
- Approval: On 2026-11-24, after the inline scope to align tests/docs and run
  native tests plus the CoreS3 build, the user said “well we changed it 5 seconds
  so update those tests or document”. Production settings already contain the
  five-second pause (80,000 samples) and 100-unit activity threshold.
- Scope/ownership: Only `test/test_audio/test_main.cpp`, `test/README`,
  `README.md`, and this plan. No production, backend, dependency, secret
  configuration, or device upload changes. Proposed atomic commit:
  `test(audio): align coverage with five-second pauses`.
- Dependencies/prior context: Retain the committed 3-second onset window,
  120 ms cumulative activity minimum, 10-second cap, queue/clip ownership,
  tail trim, and retry behavior from the original task. Existing production
  settings are the source of the updated expected behavior.
- Acceptance: Assert recording at 249 quiet frames (4.98 seconds) and finalize
  at 250 (5.00 seconds); 99-unit frames remain below threshold and 100-unit
  frames count as activity. Preserve late-onset, insufficient-activity,
  tail-trim/driver-join, borrow, reuse and retry coverage with exact lengths.
  Pause reset completes at 496 frames (9.92 seconds), below the cap. The
  existing cap/ownership case also checks late-onset speech reaching the cap
  after only 249 quiet frames. Docs explain that the cap can shorten the wait.
- Expected checks: Focused audio RED, full native GREEN, CoreS3 compile,
  documentation paths/consistency, and `git diff --check`. Backend behavior is
  unchanged, so the backend suite does not need to be repeated.
- RED before edits: `~/.platformio/penv/bin/pio test -e native -f test_audio`
  exited 1. Five old cases failed: capture and pause-reset expected RECORDED
  too soon; quiet-noise expected NO_SPEECH at the old energy boundary;
  slow-update tail expected driver join too soon; retry expected RECORDED too
  soon. Three cases passed. The runner reported SIGTRAP after Unity failures.
- GREEN: `~/.platformio/penv/bin/pio test -e native` exited 0; all 34 cases
  passed (19 controller, 8 audio, 7 protocol). No production changes were needed.
- Firmware compile: `~/.platformio/penv/bin/pio run -e m5stack-cores3` exited
  0 without local secret configuration. RAM: 34,724/327,680 bytes (10.6%);
  flash: 609,211/6,553,600 bytes (9.3%). Two existing M5GFX unused-variable
  warnings were emitted. No upload ran.
- Documentation: local Markdown paths and the new follow-up anchor checked;
  current pause/threshold descriptions agree with committed production settings.
  `git diff --check` exited 0; no untracked candidate files are present.
- Review round 1 of 3: `PASS`, with no actionable findings. Independent
  `code_reviewer` on `gpt-6.1-sol`/`xhigh` verified the frozen patch and four
  file hashes, reran all 34 native cases and the CoreS3 build, and checked all
  20 local Markdown links/anchors plus whitespace. Only the approved four
  files changed; original checkout stayed clean. Developer used
  `gpt-6.1-sol`/`medium` for the scoped fixture/docs adjustment.
- Handoff: T1 is complete for the atomic commit
  `test(audio): align coverage with five-second pauses`. The main session
  owns the branch push and PR update.
- Hardware: No upload or hardware checks performed. Pending CoreS3 validation:
  five-second pauses where the cap permits, cap-shortened waits, ordinary/quiet
  speech and room noise at threshold 100, repeated capture/upload, and actual
  microphone continuity/ownership. Passing fixtures and compilation do not
  establish these device behaviors.
