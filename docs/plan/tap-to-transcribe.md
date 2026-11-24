# Tap to transcribe with a disposable local backend

**Status:** Implementation complete; all three tasks independently reviewed and
committed. This plan records the original two-second implementation. For the
later user-confirmed device result, see the
[CoreS3 validation follow-up](speech-silence-stop.md#cores3-validation-follow-up).

**Outcome:** Tap the CoreS3, speak a short phrase during its two-second capture,
and read the returned transcript in Serial while the face remains responsive.
A small Node.js backend in this repository runs local Whisper through
`whisper.cpp`; transcription needs no paid API or conversational LLM.

## Baseline and scope

- Request: on 2026-11-24, the user requested planning for tap-to-transcribe and
  authorized a repository folder for a small, testing-only backend that will
  be replaced later. The current decision is Node.js + local Whisper through
  `whisper.cpp`, selected by “We can opt for whisper for now, lets update the
  plan.” This replaces the earlier hosted OpenAI choice. The user deferred
  speech-driven stopping to a separate plan; retain two-second capture here.
- Starting checkout: clean `main` at
  `bc8d439e2d387f8b80630d92db478a1402325de8`; no existing local edits.
- Planning branch: `feat/tap-to-transcribe`. Only this plan is being added
  before implementation approval; `backend/` will be created in T1. At this
  revision, the existing untracked draft is the only local change and is being
  updated in place.
- Approved implementation worktree:
  `/Users/france/apps/stackchan/france-stackchan-cfw-tap-to-transcribe`, using
  `feat/tap-to-transcribe` from the recorded baseline. The original checkout
  will return to `main` during implementation. Preserve the existing unrelated
  `france-stackchan-cfw-scope-guidance` worktree. After all task commits and
  reviews, remove only this task worktree and select the completed feature
  branch in the original checkout so the work remains available for inspection.
- In scope: one local backend endpoint, Wi-Fi configuration, bounded recording
  upload, transcript logging, responsive interaction, and failure recovery.
- Out of scope: AI replies, speech synthesis, streaming, voice activity
  detection, longer recordings, on-screen transcripts, deployment, databases,
  user accounts, persistent settings, hosted transcription/API credentials,
  a persistent Whisper service, and a general provider/plugin framework.
- Scope assessment: the backend is small; networking and buffer ownership make
  the firmware work a medium change across components. Retaining the existing
  two-second capture and using Serial contain this milestone. Longer utterances
  can be planned separately after the complete path works.

## Requirements and decisions

### Interaction

- An idle press starts the existing two-second, 16 kHz, signed 16-bit mono
  capture. Preserve the fixed 64,000-byte sample buffer and existing gain and
  speaker settings; do not combine unrelated audio adjustments with this work.
- Successful capture enters `THINKING` and submits one transcription request.
  Print `Transcript: <text>` once on success, then return to `IDLE`.
  Automatic speaker replay is replaced in this interaction; no speaking phase
  or generated audio is part of this milestone. Retain the existing playback
  transport without adding a mode selector.
- An empty successful transcript prints a no-speech message and returns idle;
  transcription accuracy, including silence handling, needs a real model check.
  Whisper can produce text from silence/noise; an empty-result path alone does
  not establish reliable speech detection. No VAD is added in this milestone.
- Ignore busy/held touches; never queue another recording. Idle-only sleep
  remains 30 seconds. A sleeping press wakes to idle, and the next distinct
  press records. Completion and failure refresh inactivity.
- Preserve blinking and consistent face/Serial states. Wi-Fi connection,
  upload, and transcription waiting must not run blocking waits in the update
  loop.
- Missing configuration, unavailable Wi-Fi/backend, rejected requests,
  malformed/oversized responses, and timeouts produce one useful diagnostic,
  return to idle, and permit a later retry after transport cleanup. Never print
  an earlier request's transcript as a new result.

### Replaceable backend contract

- `POST /v1/transcriptions` accepts `Content-Type: application/octet-stream`
  and exactly 64,000 bytes of little-endian signed 16-bit mono PCM at 16 kHz.
  Require the matching `Content-Length`; reject unsupported framing/format
  rather than adding multipart parsing on the device or server.
- The backend adds a standard 44-byte PCM WAV header in memory and pipes the
  resulting WAV to local `whisper-cli`. The device does not depend on Whisper
  arguments, model files, or installation paths.
- Success is HTTP 200 with `Content-Type: text/plain; charset=utf-8` and a
  `Content-Length`. The transcript is at most 2,048 UTF-8 bytes; empty is valid.
  Fail explicitly rather than silently truncating a longer result. Plain text
  avoids a firmware JSON dependency. Preserve multilingual UTF-8 in Serial.
- Errors use a non-200 status and a short plain-text message: 400 invalid
  length/body, 413 oversized input, 415 unsupported media type, 503 busy,
  502 local inference failure/invalid output, and 504 inference timeout. Unknown
  paths/methods receive 404/405. Keep raw process diagnostics out of responses.
- A replacement backend needs only this contract and a new configured base
  URL. Local HTTP uses a trusted LAN; audio stays between the device and the
  computer running this backend.

### Minimal Node.js backend

- Use Node.js 24, plain JavaScript ES modules, and built-in `node:http`,
  `node:child_process`, filesystem checks, and `node:test`; no framework, SDK,
  transpiler, container, or npm runtime packages. The external `whisper-cli`
  executable and model are required local dependencies. Target
  `backend/server.mjs`, `backend/server.test.mjs`, `backend/.env.example`, and
  `backend/README.md`.
  Keep local inference in a small function with a testable process boundary.
- Configuration: `WHISPER_CLI` (absolute executable path), `WHISPER_MODEL`
  (absolute model path), `WHISPER_LANGUAGE` (default `en`), `HOST` (default
  `127.0.0.1`), and `PORT` (3000). Fail startup clearly for invalid configuration,
  a missing/nonexecutable CLI, or an unreadable model. For device testing,
  document binding to the computer's LAN interface or `0.0.0.0`, and configuring
  that computer's LAN IPv4 address on CoreS3; `localhost` on CoreS3 is not the PC.
- Propose the multilingual `base` model (`ggml-base.bin`, approximately 142 MiB
  on disk), stored outside Git. Language is configurable; pin English for the
  first short-phrase tests rather than relying on short-clip auto-detection.
  Changing the local model or language does not change the device contract.
- Launch one `whisper-cli` process per accepted recording, using asynchronous
  `spawn` with an argument array and no shell. Supply `--model <path>`,
  `--language <language>`, `--file -`, `--output-txt`, `--output-file -`,
  `--no-prints`, and `--no-timestamps`. Pipe the bounded WAV to stdin, close it,
  and collect transcript-only stdout. Do not construct shell commands or parse
  diagnostic logs as transcripts. No API key, remote request, or temporary audio
  file is needed; no automatic inference retries.
- The process loads the model for each request. This keeps the test backend
  small without supervising a persistent model service. Measure startup plus
  inference latency on the actual computer; report if the chosen model cannot
  fit the deadlines rather than silently extending them.
- Bound the incoming body to 64,000 bytes and its receipt to five seconds.
  Admit one transcription at a time and reject extra work immediately. Give
  process startup, stdin writing, inference, and output collection one shared
  20-second deadline. On timeout, disconnect, or server shutdown, terminate the
  child, escalate to forced termination after one second if needed, and wait
  for process/stdio closure before releasing admission. Handle spawn failures,
  early exit, broken stdin, output overflow, and nonzero exit without leaks or
  stale results. Never treat a termination request as proof the child exited.
- Bound stdout while reading, allowing at most 2,048 UTF-8 transcript bytes
  plus 64 bytes for surrounding whitespace; reject invalid UTF-8/overflow and
  trim surrounding whitespace before returning. Drain stderr without logging
  audio/transcripts or retaining unbounded diagnostics. No disk recordings.
- Load local paths/settings with
  `node --env-file=backend/.env backend/server.mjs` from the repository root.
  Ignore `backend/.env` and a reserved `backend/.local/` artifact directory in
  Git; commit only examples. Document a verified CLI release/commit, model
  source, build/install prerequisites, one-time model download, and a real
  offline smoke command. Do not vendor model weights or third-party source.
  Normal inference must work without internet after setup. This disposable
  service has no public hosting or production authentication scope.

### Firmware boundaries and resources

- Keep `main.cpp` limited to startup/update wiring. `RobotController` owns
  interaction, `RobotStateMachine` owns testable decisions, `FaceRenderer` owns
  animation, and focused `RobotTranscription.h/.cpp` owns Wi-Fi/HTTP work.
- Configure Wi-Fi SSID/password and the backend's numeric IPv4 base URL through
  an ignored `include/LocalConfig.h`, with a committed
  `include/LocalConfig.example.h`. A missing local file must still compile,
  report that transcription is unconfigured, and preserve face/sleep/wake use.
  Never commit Wi-Fi credentials. Firmware needs no transcription API key.
- Poll Wi-Fi connection/reconnection with elapsed-time scheduling, without
  blocking startup or a loop waiting to connect. A disconnected press should
  fail clearly rather than silently storing audio for later transmission.
- Because the installed Arduino HTTP client is synchronous, use one persistent
  FreeRTOS networking task with capacity for one request and one result. The
  update loop only submits/polls. Only that task owns its network client; only
  the main task updates M5 audio, face, state, and Serial.
- Expose a read-only completed clip view from `RobotAudio`. Borrow its existing
  buffer until the worker acknowledges release; do not allocate a second full
  recording. Do not invalidate/reuse it for a new capture while a worker reads
  it, including after a logical timeout. Use synchronized ownership/result
  handoff, and discard late results from a completed/failed interaction.
- Keep capture's five-second watchdog; give transcription a separate 30-second
  elapsed deadline with unsigned rollover handling. Configure explicit connect
  and I/O timeouts inside the worker, disable redirects/retries, and enforce
  response-size limits while reading. A logical timeout is not proof a blocking
  library call has stopped: reject new captures until release is acknowledged;
  never force-delete a task holding the audio buffer. Review and document actual
  socket cleanup bounds against the installed library.
- Allocate task/queue/response storage once where practical; handle allocation
  failure. Record firmware RAM/flash and device heap/task headroom under Wi-Fi.
  Keep networking types out of the native state-decision boundary. No platform
  upgrade or firmware third-party dependency is proposed.

### Research and environment

- The [whisper.cpp project](https://github.com/ggml-org/whisper.cpp) supports
  macOS and documents local model setup. Its
  [model list](https://github.com/ggml-org/whisper.cpp/blob/master/models/README.md)
  lists multilingual `base` at 142 MiB. These are runtime/model artifacts,
  independent of the Node application and excluded from commits.
- The [CLI guide](https://github.com/ggml-org/whisper.cpp/blob/master/examples/cli/README.md)
  documents model/language, input/output, and quiet/timestamp flags. Planning
  source inspection of `examples/common-whisper.cpp` confirms WAV stdin for
  `--file -`; `examples/cli/cli.cpp` supports `--output-file -` and suppresses
  duplicate segment callbacks in that mode. Verify these capabilities against
  the actual installed version during T1; source inspection is not a smoke test.
- Whisper's CLI guide documents false transcripts on silence/noise and suggests
  a fixed language as one mitigation. VAD and tuning beyond baseline testing
  remain deferred; do not equate successful process exit with accurate speech.
- [Node built-in globals](https://nodejs.org/download/release/latest-v24.x/docs/api/globals.html)
  and [CLI options](https://nodejs.org/api/cli.html) cover the Node runtime and
  environment-file loading. Use the
  [child process API](https://nodejs.org/download/release/latest-v24.x/docs/api/child_process.html)
  for asynchronous inference and process cleanup; no SDK is needed.
- Planning inspection found Node `v24.22.3` at
  `/Users/france/.nvm/versions/node/v24.22.3/bin/node`; `node` is absent from this
  shell's PATH. Activate the installed Node environment or use that executable
  for checks. Node needs no new installation. Host architecture is `arm64`;
  `whisper-cli` was not found on PATH or in the usual Homebrew executable
  locations during planning. Confirm/install the CLI and download the model
  during T1; neither was installed or downloaded for this plan revision.
- Installed Arduino `HTTPClient` exposes separate connect and I/O timeouts;
  source inspection shows its network calls and response processing wait
  synchronously. Those calls belong outside the interaction loop.

## Ordered tasks

### T1: Add the disposable transcription backend

**Scope and ownership:** `backend/` files listed above, `.gitignore` backend
configuration/artifact entries, local Whisper setup instructions, and this
plan's T1 evidence. No firmware behavior changes.

**Dependencies:** Explicit implementation approval, installed Node, and the
contract above. Local setup needs `whisper-cli` and model weights; automated
checks use controlled inference/child fixtures without downloading a model.

**Acceptance:** A valid request produces local Whisper's transcript using the
specified WAV metadata. Input, concurrency, deadline, disconnect, and process
error paths are bounded and release resources. Document local runtime/model
setup, one backend start command, offline tests, LAN configuration, and the
replacement contract. No npm runtime packages, paid API, or persistent service.

**Checks:** First show meaningful failing endpoint tests, then pass
`node --test backend/server.test.mjs`. Cover generated WAV bytes, UTF-8/empty
success, wrong/short/large bodies, extra concurrent work, missing CLI/model,
spawn/nonzero-exit errors, timeout/forced termination, disconnect/shutdown,
stdin errors, and invalid/oversized output. Use a controlled child fixture to
exercise process cleanup and ensure admission stays held until closure. Inject
short deadlines in tests rather than sleeping for production deadlines. Run
`node --check backend/server.mjs`, `git diff --check`, and doc/link checks.
Run one real local-model smoke test with a known short speech fixture; record
CLI version/commit, model, expected/observed transcript, and elapsed time. Use a
documented fixture with permission to reuse it; keep synthetic process tests
distinct from recognition evidence. No paid API, device access, or upload is
needed. Record a missing runtime/model or unavailable speech fixture as a gap.

**Proposed commit if authorized:** `feat(backend): add disposable transcription endpoint`

**Status and evidence:** Complete; independent review PASS in round 1 of 3.
No firmware changes are part of T1. Committed with the proposed T1 subject after
the checks and review below.

- T1 baseline: `feat/tap-to-transcribe` at
  `bc8d439e2d387f8b80630d92db478a1402325de8` in the approved dedicated worktree.
  Startup checks (`git status --porcelain=v1 --untracked-files=all`,
  `git symbolic-ref --quiet --short HEAD`, `git rev-parse HEAD`) exited 0;
  only this approved, untracked plan existed. No conflicting owned edits.
- Developer selection/runtime: `gpt-6.1-sol`, `xhigh`, selected for the async
  process lifecycle, cancellation/closure, bounded input/output and admission.
  Role configuration was confirmed by the coordinator; no worker delegation.
- Owned candidate: `backend/server.mjs`, `backend/server.test.mjs`,
  `backend/.env.example`, `backend/README.md`, `.gitignore` backend entries,
  and this plan's T1 evidence. The HTTP contract and Node 24 are dependencies;
  the approved local CLI/model setup is isolated from automated tests.
- The endpoint implements exact PCM framing/media, in-memory WAV, single
  admission from body receipt through child/stdio closure, fixed five-second
  body and shared 20-second process deadlines, SIGTERM then SIGKILL after one
  second, sanitized errors, bounded/fatal-decoded UTF-8 and drained stderr.
  Shutdown/disconnect cancel active work; no shell, retry, npm packages or
  saved recordings. CLI/model configuration is validated at startup.
- Meaningful RED on 2026-11-24:
  `/Users/france/.nvm/versions/node/v24.22.3/bin/node --test backend/server.test.mjs`
  exited 1 against a minimal 501 scaffold: the endpoint test expected 200 but
  got 501. That test asserts the prescribed spawn argument array, every WAV
  field and all 64,000 PCM bytes, UTF-8 trimming and response byte length.
  The scaffold was replaced by the implementation after this failure.
- GREEN: the same command exited 0 with 28 tests (17 top-level cases plus
  subtests), none skipped. The first full pass took 1,151 ms; the final
  bounded-buffer implementation pass took 1,162 ms. Covers WAV/args, empty and
  split-multibyte/max-size UTF-8, input media/framing/short/oversized bodies,
  unknown route/method, body deadline/retry, admission, missing CLI and thrown
  spawn, early/nonzero exit, invalid/oversized output, two MiB stderr draining,
  stdin/stdout/stderr errors, deadline while stdin stalls, disconnect and
  shutdown. A real Node child ignores SIGTERM and is killed with SIGKILL;
  its PID is confirmed absent before retry. A controlled child emits `exit`
  while stdio remains open: requests stay 503 until `close`, demonstrating
  that a termination request/process exit alone cannot release admission.
  One intermediate suite run exited 1 due to the test client rejecting an
  undefined header before sending; header omission in the fixture was fixed.
- Syntax/whitespace checks exited 0:
  `/Users/france/.nvm/versions/node/v24.22.3/bin/node --check backend/server.mjs`,
  `/Users/france/.nvm/versions/node/v24.22.3/bin/node --check backend/server.test.mjs`,
  and `git diff --check`. A Python documentation check covers new-file
  whitespace/final newlines/fences, relative links, required contract/settings
  and CLI flags; ignore rules were checked for `.env`/`.local` versus the
  tracked example. These checks are appropriate for docs/config; no artificial
  failing documentation test was added.
- Local runtime setup completed outside the disposable worktree at
  `/Users/france/.cache/stackchan-whisper-t1/`; nothing must be moved before
  worktree cleanup. No `backend/.env` was created. Exact successful setup:

  ```sh
  /usr/bin/python3 -m pip install --target /Users/france/.cache/stackchan-whisper-t1/build-tools cmake
  git clone --depth 1 --branch v1.9.5 https://github.com/ggml-org/whisper.cpp.git /Users/france/.cache/stackchan-whisper-t1/whisper.cpp
  /Users/france/.cache/stackchan-whisper-t1/build-tools/cmake/data/bin/cmake -S /Users/france/.cache/stackchan-whisper-t1/whisper.cpp -B /Users/france/.cache/stackchan-whisper-t1/whisper.cpp/build -DCMAKE_BUILD_TYPE=Release -DWHISPER_BUILD_TESTS=OFF
  /Users/france/.cache/stackchan-whisper-t1/build-tools/cmake/data/bin/cmake --build /Users/france/.cache/stackchan-whisper-t1/whisper.cpp/build --target whisper-cli -j 4
  mkdir -p /Users/france/.cache/stackchan-whisper-t1/models
  bash /Users/france/.cache/stackchan-whisper-t1/whisper.cpp/models/download-ggml-model.sh base /Users/france/.cache/stackchan-whisper-t1/models
  ```

  All exited 0. CMake is 4.4.5, compiler AppleClang 21.0.0.21000101. Build
  warnings were upstream deprecation/OpenMP-not-found/ARM option and one
  `stb_vorbis.c` pointer comparison warning, with no build failure. The initial
  model download command exited 1 because the explicit destination directory
  did not exist; creating it and rerunning succeeded. No network setup blocker.
- Verified CLI: release tag `v1.9.5`, commit
  `d1be6fde11ac6e0407606b4e42fe72d34add8037`; `whisper-cli --version`
  exited 0 and reports `1.9.5-dev`. Source inspection plus the real smoke
  confirms stdin WAV, quiet/timestampless transcript-only stdout and all
  prescribed flags against that installed version.
- Model: multilingual `ggml-base.bin`, 147,951,465 bytes; SHA-1
  `465707469ff3a37a2b9b8d8f89f2f99de7299dac` matches the pinned upstream
  model list. SHA-256
  `60ed5bc3dd14eea856493d334349b405782ddcaf0028d4b5df4088345fba2efe`.
  Official download script uses the project's Hugging Face model repository.
- Recognition fixture: the pinned source's `samples/jfk.wav`, mono PCM16
  16 kHz, SHA-256
  `59dfb9a4acb36fe2a2affc14bacbee2920ff435cb13cc314a08c13f66ba7860e`.
  It ships in the MIT-licensed source; the
  [recording's source/reuse record](https://commons.wikimedia.org/wiki/File:JFK_inaugural_address.ogg)
  identifies the federal inaugural address as public domain. No audio is
  vendored. The documented smoke parses RIFF chunks, asserts the format and
  posts exactly its first 64,000 sample bytes without a temporary audio file.
- Real local-model smoke on Apple M2/arm64, macOS 26.6.2, Node 24.22.3:

  ```sh
  env WHISPER_CLI=/Users/france/.cache/stackchan-whisper-t1/whisper.cpp/build/bin/whisper-cli WHISPER_MODEL=/Users/france/.cache/stackchan-whisper-t1/models/ggml-base.bin /Users/france/.nvm/versions/node/v24.22.3/bin/node backend/server.mjs
  env PATH=/Users/france/.nvm/versions/node/v24.22.3/bin:/usr/bin:/bin:/usr/sbin:/sbin WHISPER_ROOT=/Users/france/.cache/stackchan-whisper-t1 /bin/bash /Users/france/.cache/stackchan-whisper-t1/smoke.sh
  ```

  The server listened on `127.0.0.1:3000`. `smoke.sh` contains the exact
  offline smoke code block from `backend/README.md`. The reference phrase
  begins “And so, my fellow Americans”; the two-second crop cuts the last
  word. Observed HTTP 200, text `And so my fellow America`, first latency
  6,156 ms including CLI/model startup. The initial whole-word `Americans`
  assertion exited 1. The documented assertion now accepts the recognized
  prefix and explicitly records the missing suffix; it is not an exact
  accuracy pass. Two subsequent runs exited 0 with the same text, 283 ms and
  281 ms (warm OS/Metal caches, still a fresh process/model load per request).
  All were below the unchanged 20-second inference deadline. The server was
  stopped with SIGTERM and exited 0. Recognition used local files/processes
  only after setup; external network interfaces were not disabled.
- Limitations: this validates one local clip and backend process behavior,
  not microphone quality, silence/noise rejection, general accuracy or any
  CoreS3 behavior. No firmware build/native test was needed for backend-only
  T1; no device, hardware upload, paid API, staging, commit or push occurred.
  Hardware and external-internet-disconnected LAN checks remain in T3.
- Continuation after a session interruption preserved the existing candidate and
  RED/GREEN/model evidence without implementation edits. Fresh developer checks
  passed: 28/28 backend tests (1,250 ms), both Node syntax checks, whitespace,
  and documentation/configuration checks. The frozen patch SHA-256 was
  `8d86f678ba05541c55dc16d02153c230337289abe0d8893a698ebf641e11cbc9`.
- Independent review: `code_reviewer`, `gpt-6.1-sol` / `xhigh`, selected for
  async process lifecycle, bounded storage, cancellation and protocol risks.
  Round 1 of 3 returned PASS with no findings. The reviewer independently
  passed 28/28 tests (1,218 ms), both syntax checks, whitespace, frozen hashes,
  docs/configuration checks, and a real local-model smoke: HTTP 200,
  `And so my fellow America`, 423 ms. No source edits were made during review.
- Unrelated Finder metadata `.DS_Store` appeared during continuation and is
  preserved outside the candidate and task commits. No device upload or push.
- T1 commit subject: `feat(backend): add disposable transcription endpoint`.


### T2: Add bounded firmware transcription transport

**Scope and ownership:** `include/RobotTranscription.h`,
`src/RobotTranscription.cpp`, completed-clip access in `RobotAudio.h/.cpp`,
`include/LocalConfig.example.h`, firmware secret ignore entry, transport setup
instructions in `README.md`, and this plan's T2 evidence. Keep the existing
record/replay interaction active until T3; do not initialize unused networking.

**Dependencies:** T1 reviewed and committed; agreed HTTP contract.

**Acceptance:** One worker/one request, safe clip borrowing and result handoff,
bounded response storage, explicit timeouts, absent-config build behavior, and
no network waiting in controller/render paths. Preserve existing record/replay
behavior while the new transport remains unused. Review actual library cleanup
semantics and error ownership; report device-dependent claims as unverified.

**Checks:** `pio test -e native`, `pio run -e m5stack-cores3`,
`git diff --check`, and configuration/link inspection. Use focused RED/GREEN
checks for genuine host-testable decisions introduced here; do not add mocks
that merely restate HTTP/FreeRTOS calls. Driver behavior and Wi-Fi memory usage
require hardware checks after integration in T3. Neither command uploads.

**Proposed commit if authorized:** `feat(network): add bounded transcription transport`

**Status and evidence:** Complete; independent review PASS in round 1 of 3.
Committed with the proposed T2 subject after the checks and review below.
No T3 interaction changes. Approval is the 2026-11-24 record below.

- T2 baseline: `feat/tap-to-transcribe` at
  `771f260066b3a1d29e740f227a460e4153703952` in the approved dedicated worktree.
  Startup checks exited 0; the only unrelated local file was untracked
  `.DS_Store`, preserved and excluded. T1 is the committed dependency.
- Developer runtime: `gpt-6.1-sol` / `xhigh`, selected for concurrent worker/clip
  ownership, bounded framing, deadlines and cleanup. No delegation, staging,
  commit, push or upload by the developer. The coordinator owns task commits
  after independent review.
- Scope/ownership refinement: add focused `include/TranscriptionProtocol.h`,
  `src/TranscriptionProtocol.cpp`, `test/test_transcription/test_main.cpp`,
  minimal native source/test filters in `platformio.ini`, and `test/README`.
  These test the actual production parser and numeric URL decisions without
  pretending that socket/RTOS mocks prove device behavior. Other owned files
  remain the transport, audio clip access, local example/ignore entry, README
  setup and T2 evidence. No controller, renderer, state, main, gains, speaker
  settings, dependencies or platform versions changed.
- The main-task API explicitly initializes, submits and polls one persistent
  worker. Queues, one result buffer and parser storage are bounded; the task's
  6,144-byte stack is allocated once and allocation failures are reported.
  Network types remain inside the transport implementation. Merely compiling
  or constructing the transport starts no networking; it remains unused until
  T3. Wi-Fi association/reconnect attempts are worker-scheduled every ten
  seconds, with no main-task wait; disconnected submissions fail immediately.
- Only a completed recording may be borrowed, read-only, from the existing
  64,000-byte audio buffer. Recording, playback and stop cannot invalidate it
  while borrowed. A result token is published only after socket cleanup, and
  only the main task's poll copies the result and releases the clip. A logical
  interaction timeout cannot release it or admit a new recording. Request IDs
  and explicit transcript byte length allow T3 to discard stale results and
  print UTF-8 containing NUL without C-string truncation.
- Source-driven transport choice, accepted by the coordinator within the
  approved bounds: installed `HTTPClient.cpp::handleHeaderResponse()` uses
  growing `String`/`readStringUntil` and per-progress idle timeouts. Installed
  `Network/src/NetworkClient.cpp::write()` has ten one-second select retries
  that reset after partial progress; `connect()` also has unchecked internal
  allocations and setsockopt error returns before socket ownership/close.
  Therefore a small worker-local lwIP socket performs numeric IPv4,
  nonblocking connect/send/recv, always retaining its descriptor through close.
  This adds no HTTP framework, DNS lookup, third-party dependency or platform
  upgrade. Every request has one-second connect, five-second upload/progress
  idle, 25-second first-response and shared 30-second elapsed deadlines.
  There are no redirects or automatic request retries. Headers are capped at
  1,024 total bytes and 255 per line; exact Content-Length/plain UTF-8 text is
  capped at 2,048 bytes, with no transfer/content encoding or silent truncation.
- Cleanup limits: installed ESP-IDF headers report 5.5.5; CoreS3 sdkconfig
  disables `LWIP_SO_LINGER`, so no unsupported linger option is set. The
  matching IDF v5.5.5 lwIP submodule is
  `fd432e4ee2cfb7f7f1c7eb7227e0173412e7b84e`;
  [its close implementation](https://github.com/espressif/esp-lwip/blob/fd432e4ee2cfb7f7f1c7eb7227e0173412e7b84e/src/api/api_msg.c)
  uses `SO_SNDTIMEO` (five seconds here) for FIN-allocation waits on memory
  shortage, with 500 ms polling; its header default is 20 seconds without a
  send timeout. These source-level waits plus SDK scheduling are not a measured
  hard bound. Direct `lwip_close` bypasses VFS bookkeeping: it frees the
  descriptor only after successful prepare-delete, and retains it on failure.
  Close finishes before acknowledgment; an exceptional close error
  retains ownership and retries cleanup instead of granting new admission.
  Thus cleanup can outlast the logical deadline, and a stuck SDK may leave
  admission held. Actual close latency/resource reuse remain hardware checks.
- Meaningful RED: `/Users/france/.platformio/penv/bin/pio test -e native`
  exited 1 against an inert protocol scaffold. The existing 12 controller
  cases passed and all seven new protocol cases failed (the harness also
  reported SIGEMT after the failures). Failures establish numeric URL
  acceptance, fragmented multilingual/empty UTF-8, exact/max-size bodies and
  NUL length, incomplete/non-200 response behavior, malformed/ambiguous media
  or framing, fixed storage limits and invalid UTF-8. The scaffold was replaced
  only after this run. GREEN: the same command exited 0, 19/19 cases, initially
  1.530 seconds and finally 1.826 seconds after style/canary refinements.
  No failing docs/config test was invented; appropriate checks are below.
- Firmware compile checks use `/Users/france/.platformio/penv/bin/pio run -e
  m5stack-cores3` (no upload). Missing configuration and a temporary exact copy
  of `LocalConfig.example.h` both passed, exit 0, at 25.191 and 24.754 seconds;
  RAM 96,596/327,680 (29.5%), flash 610,855/6,553,600 (9.3%). The placeholder
  local copy was removed in `finally`; no real credentials were used. Because
  T2 remains unused, linker garbage collection omits the networking path from
  these final images; those sizes are not integrated Wi-Fi headroom.
- A separate example-config link probe retained `begin`, `submit` and `poll`
  with linker undefined-symbol flags, exit 0, 24.023 seconds: RAM
  116,524/327,680 (35.6%), flash 1,101,935/6,553,600 (16.8%). This verifies the
  complete transport links without wiring it into the interaction or running
  networking. After the direct-close correction, final missing-config build
  passed (exit 0, 16.272 seconds; same ordinary-image sizes) and the retained
  example probe passed (exit 0, 23.830 seconds; RAM 116,524, flash 1,101,931).
  Only a comment was clarified after these compiles; runtime code is identical.
  The probe does not include runtime task/Wi-Fi heap use. The exact
  temporary procedure, with cleanup even on failure, was:

  ```sh
  python3 - <<'PYPROBE'
  from pathlib import Path
  import os, subprocess
  p = Path('include/LocalConfig.h')
  assert not p.exists(), 'preserve existing local configuration'
  env = os.environ.copy()
  env['PLATFORMIO_BUILD_FLAGS'] = '-D ARDUINO_USB_MODE=1 -D ARDUINO_USB_CDC_ON_BOOT=1 -Wl,-u,_ZN18RobotTranscription5beginEv -Wl,-u,_ZN18RobotTranscription6submitER10RobotAudiom -Wl,-u,_ZN18RobotTranscription4pollERNS_6ResultE'
  try:
      p.write_bytes(Path('include/LocalConfig.example.h').read_bytes())
      result = subprocess.run(['/Users/france/.platformio/penv/bin/pio', 'run', '-e', 'm5stack-cores3'], env=env)
  finally:
      p.unlink()
  raise SystemExit(result.returncode)
  PYPROBE
  ```

- Resolved versions from `pio pkg list -e m5stack-cores3` (exit 0): espressif32
  8.0.0, framework-arduinoespressif32 4.30312.261116 (Arduino 3.3.12), CoreS3
  framework libs 3.3.12, M5Unified/M5GFX 0.3.0, Xtensa/RISC-V toolchains
  14.2.0+20260121, esptool package 2.50400.0 (tool 5.4.0); native Unity 2.6.1.
  Dependency versions remain unpinned. The build emitted two existing M5GFX
  unused-variable warnings; no project compile error or new project warning.
- Final documentation/configuration checks passed (exit 0):
  `python3 /Users/france/.cache/stackchan-t2-evidence/docs-config-check.py`
  checks all 13 owned files including untracked content for whitespace/final
  newline, Markdown fences and relative link targets, secret/example ignore
  behavior, removal of the temporary config, preserved `.DS_Store`, and no
  controller/state/main/renderer/gain changes. `git diff --check` passed.
  An earlier inspection assertion matched SO_LINGER in a comment rather than
  a setsockopt call; the assertion was narrowed and rerun successfully.
  The cache directory contains the complete tracked/untracked candidate patch,
  file-hash manifest, exact check summary and final example build log, outside
  the disposable worktree. No actual credentials or `.DS_Store` enter them.
- Hardware is unavailable and no upload was authorized. Real concurrent clip
  borrowing/release, failure/allocation paths, Wi-Fi association and recovery,
  socket latency/cleanup, worker stack high-water mark, free heap over repeated
  requests, microphone quality and retained record/replay need device checks.
  Parser tests and builds do not establish those observations. T3 must keep
  polling after logical timeout, reject new captures until acknowledgment,
  discard stale IDs and print using the supplied byte length.

- Independent review: `code_reviewer`, `gpt-6.1-sol` / `xhigh`, selected for
  socket timing, RTOS ownership, bounded parsing and release handoff. Round 1
  of 3 returned PASS with no findings. The reviewer independently verified all
  13 frozen files/hashes, 19/19 native tests, missing-config firmware build
  (RAM 96,596; flash 610,855), configured transport link probe (RAM 116,524;
  flash 1,101,931), whitespace and documentation/configuration checks. No source
  edits or uploads occurred; device-only limitations remain as recorded above.
- T2 commit subject: `feat(network): add bounded transcription transport`.

### T3: Connect tap recording to transcript results

**Scope and ownership:** `RobotController.h/.cpp`, `RobotStateMachine.h/.cpp`,
minimal `main.cpp` wiring if needed, `test/test_controller/test_main.cpp`,
`test/README`, `README.md`, this plan, and concise brief/backlog/roadmap status.
Change native source selection in `platformio.ini` only if needed for meaningful
pure transport/state tests. Renderer changes are not expected.

**Dependencies:** T2 reviewed and committed.

**Acceptance:** The complete interaction and failure cases above work with
simulated network facts; successful results print once. Native tests cover busy
touches, exactly-once submission, late/stale results, buffer-release gating,
capture versus transcription deadlines, retry, ten repeated cycles, and
sleep/wake/timer rollover. Documentation describes the changed interaction and
distinguishes implemented behavior from actual hardware/local-model validation.

**Checks:** Demonstrate meaningful failing behavior tests first, then pass
`pio test -e native`, `pio run -e m5stack-cores3`, the backend suite, and
`git diff --check`; check docs and record RAM/flash. Use
`~/.platformio/penv/bin/pio` if needed. No listed check flashes hardware.

**Hardware/model checklist:** With a separately authorized CoreS3 upload and
configured backend/local model, record firmware revision and dependencies;
transcribe ten short phrases; observe blinking, Serial/face agreement,
busy/held touches, idle sleep and repeated wake. Test missing/bad configuration, backend stopped,
Wi-Fi loss/reconnection, delayed responses, timeout cleanup/retry, and absence
of stale transcripts, resets, task growth, or declining free heap. Confirm
speech is intelligible to Whisper, and record empty/noisy-clip outcomes without
claiming VAD. Verify the local path works with external internet unavailable
while the LAN remains connected; synthetic/mocked success is insufficient.
If unavailable, record these as pending rather than marking full validation.

**Proposed commit if authorized:** `feat(robot): transcribe tapped recordings through local backend`

**Status and evidence:** Complete; independent review PASS in round 1 of 3.
Committed with the proposed T3 subject after the checks and review below.
Device transcription validation remains pending. The developer did not stage,
commit, push or upload.

- T3 baseline: `feat/tap-to-transcribe` at
  `8e117289c5d26c1b6680987d0c0e4f37e61c8a5d` in the approved dedicated worktree.
  Startup branch/revision/status checks exited 0. Only unrelated untracked
  `.DS_Store` was present; it remains preserved outside the candidate.
  T1/T2 were already independently reviewed and committed. Developer runtime:
  `gpt-6.1-sol` / `xhigh`, selected for integration timing, exactly-once actions,
  stale IDs and borrow/release safety; no nested delegation.
- The controller now initializes transport without waiting for association,
  rejects unavailable idle taps before capture, submits once after recording,
  and polls on every update, including after logical timeout. State inputs are
  pure readiness/pending/result-ID facts. Once-only completion actions select
  exact-length `Serial.write` for transcripts (including UTF-8/NUL) or one
  `No speech transcribed` message. The controller owns error diagnostics;
  late results cannot print or start a coincident recording. New capture stays
  blocked until the worker acknowledgment releases the clip. Separate capture
  and request watchdogs are five and 30 seconds. No T2 interface/production,
  main, renderer, audio gain/speaker, platform or dependency changes were needed.
- Meaningful RED before production edits:
  `/Users/france/.platformio/penv/bin/pio test -e native` exited 1. The original
  12 controller and seven protocol cases passed; two added assertions failed:
  thinking incorrectly ended after the old five-second audio watchdog, and
  `PLAYING` incorrectly entered speaking. The runner also reported SIGINT
  after those failures. The RED log and reproducible test-only patch are in
  `/Users/france/.cache/stackchan-t3-evidence/`.
- GREEN: the same native command exited 0, 25/25 cases (18 controller and seven
  protocol), final duration 1.613 seconds. Tests count one capture/submit/
  transcript-or-no-speech action per cycle over ten cycles, busy/held touches,
  immediate and worker failure, readiness rejection, stale/late IDs,
  acknowledgment-gated retry, independent deadline boundaries, repeated
  sleep/wake, inactivity refresh and unsigned rollover. No driver/RTOS/Serial
  mocks were added; actual hardware call/print counts remain a device check.
- Final `/Users/france/.platformio/penv/bin/pio run -e m5stack-cores3` passed
  without local configuration (exit 0, 22.95 seconds): RAM 98,692/327,680
  (30.1%), flash 606,903/6,553,600 (9.3%). The same command with a temporary exact
  `LocalConfig.example.h` copy passed (exit 0, 24.43 seconds): RAM 118,620
  (36.2%), flash 1,097,675 (16.7%). The configured image retains integrated
  networking; neither figure measures runtime task/Wi-Fi heap use. The exact
  procedure is `python3 /Users/france/.cache/stackchan-t3-evidence/run-final-checks.py`;
  it asserts the local config is absent, copies the placeholder, and removes
  it in `finally`. No real credentials or upload were used.
- `/Users/france/.nvm/versions/node/v24.22.3/bin/node --test backend/server.test.mjs`
  passed, exit 0, 28/28, 1,212.645 ms. No backend changes or repeated model
  installation/smoke were needed; T1's real local-model evidence and accuracy
  limit remain above. `pio pkg list -e m5stack-cores3` passed, exit 0: unchanged
  espressif32 8.0.0, Arduino 3.3.12/package 4.30312.261116, M5Unified/M5GFX 0.3.0.
  Dependencies remain unpinned. Final builds emitted the existing M5GFX unused
  variable warnings, with no new project warning or compile error.
- Documentation/configuration uses inspection rather than an invented failing
  test: `python3 /Users/france/.cache/stackchan-t3-evidence/docs-config-check.py`
  and `git diff --check` passed, exit 0. The script checks all 11 owned files,
  relative links/anchors, fences/whitespace, exact integration call sites,
  ignored example/local configuration behavior, no leftover local config or
  backend environment/artifacts, preserved `.DS_Store`, and unchanged T2/main/
  renderer/audio/platform files. Complete candidate diff, file hashes, exact
  command/exit records and logs live outside the disposable worktree in the
  same cache directory. No task-owned server, smoke or inference child remains.
- Hardware is unavailable and no upload was authorized. Ten real phrases,
  intelligible microphone input, actual Serial byte/print counts, face/blink/
  busy-touch/sleep-wake behavior, concurrent ownership/cleanup, Wi-Fi loss and
  recovery, allocation failures, heap/worker-stack/task stability and LAN use
  with external internet disconnected remain pending. Builds and pure state/
  protocol/backend tests do not establish those observations. README and
  product status documents identify implementation separately from validation.

- Independent review: `code_reviewer`, `gpt-6.1-sol` / `xhigh`, selected for
  integrated timing, stale results and clip ownership. Round 1 of 3 returned
  PASS with no actionable findings after inspecting all 11 frozen files and
  transport/audio boundaries. Independent checks passed: 25/25 native cases
  (1.927 seconds), 28/28 backend tests (1,175.600 ms), missing-config build
  (RAM 98,692; flash 606,903), example-config build (RAM 118,620; flash
  1,097,675), all frozen hashes, 33 documentation links/anchors, configuration
  and whitespace. No source edits, upload or push occurred during review.
- T3 commit subject: `feat(robot): transcribe tapped recordings through local backend`.

## Review and validation

- Implementation approval received on 2026-11-24: “looks good, make sure to
  start working on a worktree and delete the worktree after implemetation”.
  This approves the revised local-Whisper plan, T1/T2/T3, their separate task
  commits/checks/reviews, and task-worktree creation/removal. It does not
  authorize pushing or device uploads.
- Planning review: main-session self-review. Implementation uses independent
  review as specified below.
- Route implementation to the configured `implementer` (`gpt-6.1-sol`, `xhigh`). It
  must verify live role availability, then use a fresh developer/independent
  reviewer pair per task, serially, per the development workflow.
- Each task requires its checks, independent PASS, evidence, and separate atomic
  commit before the next task. Select worker models/effort explicitly and
  preserve the three-round review limit. Never silently substitute roles.
- No firmware/native/backend test, model inference, installation/download,
  paid provider request, or device upload was performed during planning.
  Documentation validation is recorded below.
- Initial draft checks on 2026-11-24 passed (exit 0): Python checked template
  sections, T1/T2/T3 order, pending-approval status, Markdown link targets,
  whitespace, final newline, and code fences. `git diff --check` passed; the
  untracked plan's whitespace was checked separately by Python. Node 24.22.3
  confirmed availability of HTTP, test runner, fetch, FormData, Blob, and abort
  timeout APIs. These checks do not establish backend or device behavior.
- Whisper revision checks on 2026-11-24 passed (exit 0): Python checked template
  sections, task order, six Markdown links, retained recording/contract/deadline
  requirements, removal of hosted configuration, deferred VAD, approval status,
  whitespace, final newline, and fences. Node 24.22.3 confirmed HTTP, async
  process creation, filesystem checks, and test-runner APIs. `git diff --check`
  passed; Python checked the untracked draft separately. Self-review checked
  local-model setup, process cancellation/closure, and unchanged device scope.
- Device uploads require separate authorization under the repository's hardware
  workflow. Local recognition validation needs a working CLI/model and speech
  fixture; report it as pending if unavailable. No push requested.
- Update the roadmap for the actual completed capability and evidence. An
  implementation with pending hardware checks must say so explicitly.

## Handoff

- Plan prepared, revised for local Whisper, and approved on 2026-11-24.
- T1 (`771f260`), T2 (`8e11728`) and T3 are complete with separate task commits
  and independent PASS verdicts, each in review round 1 of 3. T3 uses the
  commit subject recorded above. Device transcription validation remains
  pending as recorded above. No upload or push was performed.
- Runtime/model setup and task evidence live outside the disposable worktree
  under `/Users/france/.cache/stackchan-whisper-t1/`,
  `/Users/france/.cache/stackchan-t2-evidence/` and
  `/Users/france/.cache/stackchan-t3-evidence/`. No local configuration, backend
  environment file or task-owned process must be preserved from the worktree.
- Implement in the dedicated worktree, then remove it after clean committed
  completion and return the original checkout to the feature branch.
- Retain the original audio validation record as historical evidence. Use this
  plan for the new interaction and its hardware checklist.
