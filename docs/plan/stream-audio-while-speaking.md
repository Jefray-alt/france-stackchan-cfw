# Stream microphone audio while speaking

**Status:** Approved on 2026-11-24. T1 passed independent review; T2 is pending.

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
  10,958 ms. Their separate 60-second inference took 1,312 ms. The task is ready
  for its authorized `feat(backend): accept streaming pcm uploads` commit.

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

**Status and evidence.** Pending.

## Review and validation

- Plan reviewed against the current capture path, controller and backend contract.
  T1 used the configured developer and independent reviewer; review round 1
  passed. T2 will use a fresh pair after the T1 commit.
- Native fixtures and the firmware build cover decisions and integration checks.
  Device measurement is needed for real microphone continuity, network timing,
  queue behavior under Wi-Fi stalls, and memory headroom. Flashing via
  `pio run -e m5stack-cores3 -t upload` requires authorized device work.

## Handoff

- Implementation and task commits are authorized by the user's approval.
  T1 checks and independent review passed. T2 and hardware validation are pending.
