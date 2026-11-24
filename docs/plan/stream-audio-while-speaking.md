# Stream microphone audio while speaking

**Status:** Approved on 2026-11-24. T1 and T2 passed independent review and are
delivered as separate task commits. Physical streaming validation is pending.

**Outcome:** After a tap, the CoreS3 sends microphone audio to the local backend
while the user speaks. The streaming firmware has no fixed recording duration.
The backend returns one final transcript after capture ends.

## Baseline and scope

- Starting checkout was `main` at `27e0cf1f3aadcd36d88b84dc0b393100bcc03cc2`,
  with only this untracked plan. The feature branch
  `feat/stream-audio-while-speaking` starts at the same revision in the sibling
  `france-stackchan-cfw-stream-audio` worktree. This plan was moved there.
- The user approved the plan and asked to start implementation on 2026-11-24.
  They requested a new branch and worktree and asked that the plan live there.
  Approval record: “I think this alreadt looks good we can start with the
  implementation, create a new branch and worktree and make sure to bring the
  current plan file to it and don't leave it in the main branch”.
- The current firmware collects 20 ms microphone frames in a 320,000-byte PSRAM
  clip and starts an HTTP upload after silence or the ten-second cap. The local
  backend receives the whole body before invoking Whisper. See the
  [current behavior and device evidence](speech-silence-stop.md#cores3-validation-follow-up).
- The user requested that the streaming firmware drop the recording cap and
  that the placeholder backend own any upload limit.
- This is a medium change across the backend contract, firmware capture buffer,
  network worker and controller. Scope covers the existing trusted-LAN setup.
  Wake-word detection will have its own spec. Live partial transcripts and
  changes to speech detection thresholds are outside this plan.

## Requirements and decisions

- A ready idle tap starts capture and a single upload session together. Keep the
  16 kHz, signed 16-bit little-endian mono PCM and 20 ms frames. Keep the
  three-second initial quiet window, 120 ms of cumulative activity needed for
  speech, and five-second post-activity pause. Remove the ten-second sample cap
  from streaming capture. A healthy stream can continue while speech and network
  progress continue and the backend accepts it. The face stays in `LISTENING`
  during capture, changes to `THINKING` after a valid capture ends, and returns
  idle after one final Serial result. Busy taps, disconnected taps and sleep/wake
  retain their existing behavior.
- Send ordered frames as they become available through one HTTP/1.1 chunked
  `POST /v1/transcriptions`. Use `application/octet-stream` and omit
  `Content-Length`. Finish with the zero-length HTTP chunk after the last
  accepted frame. A quiet tap or brief noise can send audio before classification.
  If capture ends without enough speech, close the upload before the final chunk,
  discard the partial audio on the server, skip inference, and report no speech
  once. The same cancellation path covers capture failures and timeouts.
  [HTTP chunked transfer coding](https://www.rfc-editor.org/rfc/rfc9112.html#section-7.1)
- Extend the existing endpoint so fixed-length firmware remains compatible.
  Accumulate decoded PCM in a bounded in-memory buffer. Invoke the existing
  Whisper path after normal completion and validation of an even body of at
  least 3,840 bytes. The placeholder backend's proposed chunked limit is
  1,920,000 bytes (60 seconds of PCM including captured silence). Enforce it as
  data arrives and return HTTP 413 when exceeded. Use a five-second body idle
  deadline and a 65-second total body deadline for chunked uploads. The backend
  can revise these limits independently of firmware. Keep the fixed-length
  320,000-byte maximum and five-second body deadline. Reject ambiguous or
  malformed framing and discard disconnected or timed-out bodies. Retain one
  active request across both modes and the existing response format. Verify the
  existing 20-second inference deadline with a longer local Whisper fixture.
  [Existing backend contract](../../backend/README.md#cores3-and-replacement-contract)
- Use a reusable PSRAM queue sized initially for 64 frames, or 40 KiB of PCM
  (1.28 seconds at the current rate). This allows capture during the existing
  one-second connection attempt and absorbs brief send stalls. The capture
  driver releases a frame before the audio code classifies it and exposes it
  to the worker. On a partial socket write, retain the slot and resume at the
  first unsent byte. Once the socket has accepted all PCM bytes from the slot
  and all readers have released it, return the slot to the capture queue
  immediately. The queue stays allocated and slot reuse needs no backend
  acknowledgment. On queue exhaustion, fail the session without blocking
  capture or sending a transcript for audio with gaps. The streaming path uses
  no full-clip allocation. Keep the existing standalone record/replay path
  available to its callers.
- Keep socket I/O in the existing worker and recognize disconnects or backend
  rejection during capture. Replace the 11-second elapsed capture watchdog
  with a capture progress watchdog. Its proposed threshold is one second
  without a completed microphone frame, measured from capture start or the last
  completed frame. Apply network timeouts to connection, stalled writes and the
  final response. Start the separate 30-second result deadline when valid
  capture ends. A worker failure stops capture and reports once. After any
  cancellation or logical timeout, a new session needs a fresh tap after driver
  and socket cleanup. Late results cannot print or affect the next session.
  Partial audio is never retried automatically.

## Ordered tasks

### T1. Accept incremental PCM uploads in the backend

**Scope and ownership.** `backend/server.mjs`, `backend/server.test.mjs`,
the backend contract in `backend/README.md`, and this task's evidence.

**Dependencies.** None. Deploy this backend before the streaming firmware.

**Acceptance.** A client writes part of the body, keeps the request open, and
the backend receives those bytes during the pause. Inference starts exactly
once after a valid final chunk and receives byte-for-byte PCM in the WAV body.
Cover a stream above the old limit, the new exact maximum, one PCM sample over
the maximum, invalid framing, truncated/canceled streams, idle and total
deadlines, release of admission, and existing fixed-length behavior.

**Checks.** Show a failing streaming case first, then run
`node --test backend/server.test.mjs` and a paced local Whisper smoke test with
the repo's existing fixture and setup. Measure local inference separately with
a 60-second PCM fixture and record whether it completes within the existing
deadline. These checks do not flash hardware.

**Proposed commit.** `feat(backend): accept streaming pcm uploads`

**Status and evidence.** Implemented on 2026-11-24 and independently reviewed
with **PASS** in round **1 of 3**. The reviewer used the configured
`code_reviewer` role with separately selected `gpt-6.1-sol` and `xhigh`
reasoning. Baseline is
`27e0cf1f3aadcd36d88b84dc0b393100bcc03cc2` on
`feat/stream-audio-while-speaking`, with only this untracked approved plan at
task startup. The developer role leaves model/effort selection unset; the
selected runtime was `gpt-6.1-sol` with `xhigh` reasoning. No dependencies or
changes outside the four owned files were needed.

- The server accepts only HTTP/1.1 chunked or the existing fixed-length framing,
  with their separate bounds/deadlines. It accumulates decoded PCM in a bounded
  buffer and leaves the existing final-only Whisper path unchanged. Completion
  validates the complete HTTP message, PCM minimum and sample alignment. Abort,
  timeout and overflow cleanup release the shared admission slot.
- RED before server edits:
  `/Users/france/.nvm/versions/node/v24.22.3/bin/node --test --test-name-pattern='chunked PCM arrives' backend/server.test.mjs`
  exited **1**. The test directly observed the first 640 decoded PCM bytes on
  the server during an open request; the old backend prematurely returned 400,
  failing `an open valid stream must remain admitted`.
- GREEN:
  `/Users/france/.nvm/versions/node/v24.22.3/bin/node --test backend/server.test.mjs`
  exited **0**, **44/44** tests passed. Coverage includes early receipt without
  inference/response, no inference with all bytes but no final chunk, sample
  splits across HTTP chunks, byte-for-byte WAV, a 320,640-byte stream, exact
  1,920,000-byte maximum, live 413 at 1,920,002 bytes before the client finishes,
  empty/short/odd PCM, malformed/ambiguous framing, truncated/final-chunk-missing
  streams, disconnect/shutdown cancellation, idle-reset and total deadlines,
  shared admission/retry and fixed-length regressions. Deadline tests inject
  short timer values; production defaults remain five seconds idle and
  65 seconds total for streams, five seconds total for fixed-length bodies.
- The temporary reproducible local harness was run with
  `/Users/france/.nvm/versions/node/v24.22.3/bin/node /tmp/stackchan-t1-local-smoke.mjs`
  from the feature worktree and exited **0**. It used the cached `whisper-cli`,
  `ggml-base.bin` and `jfk.wav` under
  `/Users/france/.cache/stackchan-whisper-t1`, without reading `backend/.env`.
  For 320,000 bytes sent as 640-byte frames at 20 ms intervals, first decoded
  PCM arrived at **7 ms**, capture ended at **10,568 ms**, inference began at
  **10,571 ms**, and HTTP 200 with the expected JFK words completed at
  **10,972 ms**. It asserted zero inference calls throughout capture and exactly
  one afterward. A separate 1,920,000-byte fixture, made by repeating the
  ten-second PCM six times, completed the real inference path in **1,299 ms**,
  within its unchanged **20,000 ms** deadline (647 transcript bytes).
- Both Node source syntax checks and `git diff --check` exited **0**. The
  README's extracted smoke JavaScript passed Node's syntax check; local Markdown
  link targets and whitespace in the untracked plan were checked separately.
  Documentation was updated through contract/path/command consistency checks;
  a failing documentation test would not add useful evidence.
- No firmware was changed, so firmware/native builds were not relevant to T1.
  No device was flashed or tested. This proves local HTTP/inference behavior,
  not CoreS3 microphone continuity, Wi-Fi timing, or memory headroom. T2 and its
  hardware checks remain pending.
- The independent reviewer reran all 44 tests, source syntax and documentation
  checks successfully, and additional malformed chunk/trailer probes returned
  400 with no inference. Their real Whisper smoke observed first PCM at 7 ms,
  capture end at 10,566 ms and inference at 10,569 ms; HTTP 200 completed at
  10,958 ms. Their separate 60-second inference took 1,312 ms. Committed as
  `158ab35 feat(backend): accept streaming pcm uploads`.

### T2. Stream CoreS3 audio during tap-to-talk

**Scope and ownership.** `RobotAudio`, `RobotTranscription`, `RobotController`,
`RobotStateMachine`, their focused native tests and relevant device docs.

**Dependencies.** T1 and its reviewed contract.

**Acceptance.** Backend timestamps show PCM arriving before the device ends
capture. Continuous capture passes the old ten-second sample cap and 11-second
watchdog, with bounded memory and audio ordered through queue wrap. Partial
socket writes retain their slots until all PCM bytes are accepted, then release
them for reuse. Silence-stop delivers the exact accepted tail. Quiet/noise,
missing microphone progress, backend rejection during capture, connection
errors and queue exhaustion cancel cleanly. The next distinct tap works after
cleanup and the face stays responsive throughout.

**Checks.** Show failing native cases first where practical. Run
`pio test -e native`, `pio run -e m5stack-cores3`, and the backend suite.
Exercise streaming beyond 60 seconds of simulated capture with a test consumer
that keeps draining frames, and verify the progress watchdog across `millis()`
rollover.
These checks do not flash hardware. On an authorized CoreS3 run, check paced
speech beyond 11 seconds followed by silence, tail integrity, a server rejection
while still listening, repeated cycles, a brief stall and a stall beyond queue
capacity, Wi-Fi loss and recovery, blink and sleep/wake.
Record first backend PCM arrival versus capture end, queue high-water mark,
minimum free internal heap, and PSRAM use during repeated streams.

**Proposed commit.** `feat(audio): stream cores3 microphone frames during capture`

**Status and evidence.** Implemented on 2026-11-24 and independently reviewed
with **PASS** in round **2 of 3**, after correcting two findings from round 1.
The configured reviewer used separately selected `gpt-6.2-astra` with `xhigh`
reasoning for the concurrent ownership and finalization review. This is
implementation/host evidence; physical streaming capability is still
unverified. The implementer owns the atomic task commit; the developer did not
stage, commit, push, delegate or flash.

- T2 baseline: clean `feat/stream-audio-while-speaking` at
  `158ab356209d8853850235704ead8fa37632ed78` in
  `/Users/france/apps/stackchan/france-stackchan-cfw-stream-audio`. T1 dependency
  is committed as `158ab35 feat(backend): accept streaming pcm uploads`.
  The approved outcome, backend limits, speech thresholds and standalone
  record/replay API are preserved. Ownership is the four audio/controller/
  transport/state components, new `TranscriptionUpload` helper, their native
  fixtures/tests, native source-filter integration, README, test/README and
  this evidence. No backend, renderer, main loop or workflow file changed.
- The configured developer role leaves model/effort unpinned; the selected
  runtime is `gpt-6.1-sol` with `xhigh` reasoning. Rationale: cross-task buffer
  lifetimes, callback ownership, incremental partial writes and timing cleanup.
  Earlier context is T1's final-only chunked contract and the approved
  three-second quiet / 120 ms activity / five-second pause behavior.
- The streaming path allocates a reusable 64-frame / 40,960-byte PSRAM ring,
  never a full clip. Driver-completed, main-classified and worker-released
  watermarks preserve order and immutable ownership through wrap. The driver
  callback does no classification or socket work. Its release timestamp drives
  the one-second progress watchdog; delayed main observation does not reset it
  to a newer time. Streaming counters have no sample/elapsed duration cap.
- One worker request starts with capture. `TranscriptionUpload` is the actual
  incremental HTTP sender shared with native tests through a small nonblocking
  I/O interface. It drains multiple immediately available segments per poll,
  retains partial-write offsets, and releases each slot after the last PCM
  byte is accepted, before its HTTP suffix or any response acknowledgment.
  It checks incoming rejection/EOF during capture and stalled writes. The
  worker keeps its one-second connect limit, five-second stalled-I/O limit and
  25-second first-response wait after the terminal chunk. The independent
  30-second controller deadline starts at valid capture completion.
- The final chunk is possible only after microphone join, a valid classification
  boundary and all accepted PCM frames drain. Extra captured/unclassified tail
  is excluded. Quiet, brief noise, failed capture, logical abort and queue
  exhaustion cancel by closing without that chunk. Distinct CANCELLED facts
  prevent a fast or late quiet-upload acknowledgment from replacing no-speech
  with an error. Genuine matching failures take precedence even alongside
  RECORDED/NO_SPEECH. A fast successful final result is cached through THINKING.
  Unexpected success before request finalization fails protocol validation.
- Error notification is published before socket destruction, so even an
  exceptional close delay can stop main-task capture and report once. The ring
  remains borrowed until poll acknowledges the closed socket and last worker
  read; a logical timeout grants no early admission. No automatic retry is used.
- RED, before production edits:
  `/Users/france/.platformio/penv/bin/pio test -e native -f test_controller`
  exited **1**. New behavioral test
  `test_backend_rejection_during_capture_stops_listening_once` failed because
  the old LISTENING state ignored a matching worker failure; the prior 19 cases
  passed. PlatformIO labeled the failing Unity executable ERRORED after its
  failure exit. Later fixture iterations exposed a stale constructor timestamp
  in repeated simulated sessions; the fixture now starts each upload clock
  at its actual simulated session time.
- Independent review identified two transport edge cases: a status line begun
  before request finalization could be recognized as successful afterward, and
  readable data could reset a response deadline before it was checked. The
  correction records whether any response bytes were read before the terminal
  chunk; a later HTTP 200 then fails protocol validation regardless of when its
  status line is recognized. Response expiry is checked before reading, so
  elapsed deadlines win over available data or a complete transcript. These
  changes are confined to the upload helper, focused regressions and evidence;
  acceptance criteria, ownership, baseline and backend dependency are unchanged.
- Correction RED before helper edits:
  `/Users/france/.platformio/penv/bin/pio test -e native -f test_stream`
  exited **1**, with the three new regressions returning unexpected success
  and all nine existing stream cases passing. Log:
  `/tmp/stackchan-t2-corrections-red.log`. The final protocol regression splits
  `HTTP/1.1 200 OK` from its CRLF/headers/body across the last capture frame and
  terminal chunk, isolating protocol detection from the separate idle timeout.
  Readable-data regressions fail at exactly 25000/5000 ms and succeed at
  24999/4999 ms, with ordinary timestamps and unsigned rollover. Final GREEN
  below includes all three regression cases; independent round 2 passed.
- GREEN:
  `/Users/france/.platformio/penv/bin/pio test -e native` exited **0**,
  **49/49** cases passed: 22 controller, eight bounded audio, seven response
  parser and 12 streaming cases. The shared production ring/sender test
  captures 70 seconds of simulated speech plus five seconds of exact silence
  tail, sends more than the backend's 60-second cap to a draining test consumer,
  checks byte-for-byte little-endian PCM through wrap and one 40,960-byte
  allocation, and proves the firmware itself has no duration/length cap.
  Further cases cover delayed driver handoff, partial PCM retention/immediate
  slot requeue before suffix/response, exact joined tail, quiet/noise/minimum
  cumulative activity, queue exhaustion, allocation/start/refill/stopped-driver
  failure, fragmented live 413/EOF/write error, premature 200, logical abort,
  repeated reuse, 999/1000 ms progress boundaries and rollover, 70-second
  listening, actual frame time, 29999/30000 ms result boundaries, resettable
  five-second I/O stalls, 25-second first response, partial-response stalls,
  failure/finalization races, cancellation acknowledgment races, stale IDs,
  sleep/wake and fresh taps after modeled cleanup.
- `/Users/france/.nvm/versions/node/v24.22.3/bin/node --test backend/server.test.mjs`
  exited **0**, **44/44** cases passed with unchanged backend source and limits.
  Latest suite log: `/tmp/stackchan-t2-backend.log`.
  T1 already records paced local Whisper and separate 60-second inference;
  those model smoke checks were not repeated for this firmware-only task.
- `/Users/france/.platformio/penv/bin/pio run -e m5stack-cores3` exited **0**
  with `include/LocalConfig.h` absent: RAM **34,772 / 327,680 bytes**, flash
  **610,131 / 6,553,600 bytes**. A second configured build ran via
  `python3 /tmp/stackchan-t2-configured-build.py`, exited **0**, RAM
  **54,700 bytes**, flash **1,101,643 bytes**. The script exclusively creates
  the absent ignored config using `Path.open('x')`, dummy SSID/password and
  numeric `http://127.0.0.1:3000`, runs the same absolute pio build with this
  worktree as `cwd`, and removes the file in `finally`. It refuses an existing
  file without reading or overwriting it. Final absence was checked explicitly.
  The unconfigured link can dead-strip worker code; the configured link verifies
  the real socket/RTOS adapter is included. Resolved build dependencies:
  Espressif32 **8.0.0**, Arduino ESP32 **3.3.12**, M5Unified **0.3.0**,
  M5GFX **0.3.0**. Existing M5GFX unused-variable warnings remain; no project
  compilation warnings were observed. These size figures are static linker
  output, not runtime free-heap/PSRAM measurements.
- `git diff --check` and `python3 /tmp/stackchan-t2-doc-check.py` exited **0**.
  The latter checks local Markdown targets and full-content whitespace,
  including untracked files. Documentation was
  validated by command/path/contract consistency; a RED test adds no useful
  evidence for prose. The complete tracked plus untracked binary diff and
  SHA-256 manifest are frozen at `/tmp/stackchan-t2-candidate.diff` and
  `/tmp/stackchan-t2-candidate.json`; the handoff reports their exact hashes.
- Native tests execute the production ring and HTTP exchange helper, plus the
  production state decisions. Their socket acceptance/close acknowledgment and
  microphone completion are controlled fixtures. They do not execute the real
  connect/select/SDK-close calls, exceptional close retries, Wi-Fi association,
  RobotController Serial calls or FreeRTOS scheduling. Configured compile/link
  and source inspection cover adapter integration; real concurrency, DMA/I2S
  continuity and timing still need hardware. No full RTOS simulation was added.
- **Hardware unavailable / no upload authorized.** Pending on the intended CoreS3:
  first backend PCM timestamp versus capture end; speech past eleven seconds
  then the full silence tail; live server rejection while listening; repeated
  streams; brief stalls and stalls beyond ring capacity; Wi-Fi loss/recovery;
  blink and sleep/wake; queue high-water; minimum free internal heap and PSRAM
  use. No physical behavior or metrics are claimed from these builds.
- Independent review round 2 found no remaining actionable issues. The reviewer
  reran all 49 native tests, all 44 backend tests, the unconfigured firmware
  build, full-file hashes, whitespace, local links and Markdown anchors. Their
  separate 16-case production-sender probe rejected successful responses begun
  during capture or a partial terminal write, enforced the exact 25,000 ms and
  5,000 ms response boundaries, and accepted the one-millisecond-inside controls.
  The configured build log confirmed the real adapter links. The reviewed
  implementation is delivered in
  `feat(audio): stream cores3 microphone frames during capture`.


## Review and validation

- Plan reviewed against the current capture path, controller and backend contract.
  T1 used the configured developer and independent reviewer; review round 1
  passed. T2 used a fresh developer/reviewer pair after T1 commit `158ab35`,
  reused the pair for corrections, and passed round 2 of the three-round budget.
- Native fixtures and the firmware build cover decisions and integration checks.
  Device measurement is needed for real microphone continuity, network timing,
  queue behavior under Wi-Fi stalls, and memory headroom. Flashing via
  `pio run -e m5stack-cores3 -t upload` requires authorized device work.

### T3. Three-second post-activity silence cutoff follow-up

- **Baseline, dependency, and approval.** Started clean at
  `1c72ff8eb5a7ff9d7f0e5786b989c905d5a82ed9` on
  `feat/stream-audio-while-speaking`, after T1/T2. The user approved on
  2026-11-24: “Approved and apply, also already tested the hardware and is
  working as expected”.
- **Current behavior and scope.** The shared
  `RobotAudio::kHangoverSamples` is now 48,000 samples (3 seconds at 16 kHz),
  applying to standalone and streaming capture. Initial quiet, activity
  minimum, threshold, standalone ten-second cap, stream limits, and transport
  timeouts stay unchanged. The five-second requirement and measurements above
  document the original T1/T2 implementation; T3 supersedes only that current
  pause length. README and test fixture descriptions now say three seconds.
- **RED.** Before changing the production constant, both focused commands
  exited **1** with the new 2.98/3.00-second boundary and tail expectations
  failing against 80,000 samples. The cap-before-silence fixture passed in the
  audio run.

  - `/Users/france/.platformio/penv/bin/pio test -e native -f test_audio`
  - `/Users/france/.platformio/penv/bin/pio test -e native -f test_stream`
- **GREEN.** `/Users/france/.platformio/penv/bin/pio test -e native` exited
  **0**, **49/49** cases passed. `/Users/france/.platformio/penv/bin/pio run
  -e m5stack-cores3` exited **0** using the existing local configuration:
  RAM **54,700 / 327,680 bytes**, flash **1,101,623 / 6,553,600 bytes**. The
  build reported no project compilation warnings. `include/LocalConfig.h`
  existed and was left untouched; no unconfigured build was run. No upload was
  performed.
- **Hardware report limits.** The user's report records that hardware testing
  worked as expected, but gave no flashed revision, exact timing, or individual
  scenarios. It does not establish three-second timing on device or exhaustive
  streaming behavior. This follow-up makes no such claims.
- **Status and review.** Completed with independent **PASS** in round **1 of
  3**. The configured developer used `gpt-6-luna` with `xhigh` reasoning for
  this small, specified change; the independently selected reviewer used
  `gpt-6.1-sol` with `medium` reasoning for the timing and evidence check. The
  reviewer reran all 49 native tests, the CoreS3 build, diff/whitespace and
  changed-document link/path checks, and verified all six frozen file hashes.
  No actionable findings remained. The implementer then added this review
  record and checked that metadata-only amendment before the authorized
  `fix(audio): shorten silence stop to three seconds` commit. No upload or
  push was performed.

## Handoff

- Implementation and task commits are authorized by the user's approval.
  T1 is committed as `158ab35` after checks and independent review. T2 is
  delivered as `feat(audio): stream cores3 microphone frames during capture`
  after its checks and independent round 2 PASS. Physical streaming validation
  remains pending. Neither task was pushed or flashed.
