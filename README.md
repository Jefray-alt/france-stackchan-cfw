# France Stack-chan firmware

Custom firmware for an M5Stack CoreS3, built with PlatformIO, the Arduino
framework, and M5Unified. The current source implements a touch-driven face
with idle, listening, thinking, and sleeping interaction states. An idle tap
streams microphone frames to a disposable local Whisper backend until a
sustained pause, with no firmware recording-duration cap. The returned transcript appears once in Serial.
The earlier bounded tap-to-transcribe path was confirmed on CoreS3 on 2026-11-24;
the new streaming path still needs device validation. See the
[device confirmation](docs/plan/speech-silence-stop.md#cores3-validation-follow-up).
Earlier local audio record/replay was user-validated on 2026-11-24.

## Repository

- `platformio.ini`: board, framework, dependencies, USB flags, and serial speeds.
- `src/main.cpp`: hardware initialization and the main update loop.
- `src/RobotController.cpp`: touch input, rendering, and state-change logging.
- `src/RobotStateMachine.cpp`: hardware-independent state and inactivity decisions.
- `src/RobotAudio.cpp`: reusable PSRAM capture ring, bounded record/replay clip, and audio lifecycle.
- `src/RobotTranscription.cpp`: persistent Wi-Fi/socket worker and stream/result handoff.
- `src/TranscriptionUpload.cpp`: incremental chunked upload and concurrent response handling.
- `src/TranscriptionProtocol.cpp`: fixed-size HTTP framing and UTF-8 validation.
- `src/FaceRenderer.cpp`: face drawing and blink timing.
- `include/`: project headers and the shared `RobotState` enum.
- `lib/`: project-specific libraries; currently contains a placeholder README.
- `test/`: native Unity controller tests and fixture instructions.
- `docs/`: planning documents and the development workflow.
- `.codex/agents/`: implementation coordinator, developer, and reviewer roles.

## Development

Install PlatformIO Core or the PlatformIO IDE extension for VS Code. Run the
commands below from this repository's root, where `platformio.ini` lives.
If working from the parent `stackchan` folder, first run
`cd france-stackchan-cfw`.

Check the CLI and build:

```sh
pio --version
pio run -e m5stack-cores3
```

The first build downloads the configured platform, toolchain, and M5Unified
dependencies. Build output goes under `.pio/`, which is ignored by Git.
If `pio` is unavailable in your shell, use the PlatformIO terminal in VS Code.
For a standard IDE-managed installation, the equivalent executable is
`~/.platformio/penv/bin/pio`.

The default `m5stack-cores3` environment selects `espressif32`, board
`m5stack-cores3`, Arduino, and `m5stack/M5Unified`. A separate `native` environment
runs host tests. M5Unified is pinned to 0.3.0 for its microphone buffer-release
callback.
Other dependency versions remain unpinned; record resolved versions when
investigating differences between machines.

### Upload and serial output

Connect the intended CoreS3 by USB, then list serial ports:

```sh
pio device list
```

Uploading replaces the firmware on the selected device. When ready to flash:

```sh
pio run -e m5stack-cores3 -t upload
pio device monitor -e m5stack-cores3 -b 115200
```

If multiple devices are connected, supply `--upload-port <device-port>` to the
upload command and `--port <device-port>` to the monitor command, replacing
the placeholder with the port from `pio device list`. Close any other serial
monitor before uploading. Exit the monitor with Ctrl+C.

The configured upload speed is 460800 baud and serial output is 115200 baud.
Startup prints `Robot initialized`; state transitions print
`Robot state changed: <number>`. State numbers follow the declaration order
in `include/RobotState.h`.

### Tap to transcribe

Update the [local backend](backend/README.md) before using this streaming firmware.
It supports HTTP/1.1 chunked PCM while preserving fixed-length compatibility.
Configure Wi-Fi as below. Tap while idle, then speak during the 16 kHz mono
capture. Start within three seconds. Five consecutive seconds of silence after
activity ends capture; shorter pauses keep it open. Leading and trailing audio
through the exact silence-stop frame are sent in order while capture runs.
There is no firmware duration or upload-length cap. The placeholder backend
currently accepts at most 1,920,000 PCM bytes (60 seconds including silence),
with five-second body-idle and 65-second total body deadlines. A live backend
rejection stops capture and reports an error; audio is never retried automatically.

The face stays listening during capture and changes to thinking after a valid
capture ends. The service runs Whisper only after the final HTTP chunk. Success
prints `Transcript: <text>` once, preserving UTF-8 bytes, then returns idle.
A quiet tap, less than 120 ms of detected activity, or an empty transcript
prints `No speech detected` once. Quiet or brief-noise capture closes the
partially uploaded request without its final chunk, so the backend discards it
without inference. Capture failures and timeouts use the same cancellation path.
There is no automatic replay. The separate `startRecording()`/`startPlayback()`
API remains available with its reusable ten-second clip and finalized sample count.

Detection uses mean absolute energy on 20 ms frames with a threshold of
100 PCM units. The threshold and sample-based windows are tunable in
`include/RobotAudio.h`. Detailed tuning observations for quiet speech and room
noise remain unrecorded. Steady noise can keep the energy gate open, and Whisper
can produce text from noise. AI response and generated audio remain later work.

Busy presses and held touches do not queue recordings. After 30 seconds idle,
the face sleeps; a sleeping press wakes to idle, and the next distinct press
records. Completion and failure refresh inactivity. A disconnected or
unconfigured idle press prints a diagnostic without capturing audio for later.
Capture fails after one second without a completed microphone frame, measured
from capture start or the last driver completion. A healthy stream can continue
past eleven seconds. A separate 30-second result deadline starts after valid
capture ends. Failures print one diagnostic and return idle. After cancellation
or a request timeout,
new recordings remain blocked until the worker acknowledges socket cleanup;
late transcripts are discarded. Retry with a fresh press after cleanup and
connectivity recovery. M5Unified driver cleanup and SDK socket close may wait
inside library calls; a logical watchdog does not preempt those calls.

### Local transcription setup

The disposable [backend setup](backend/README.md) describes installing local
Whisper, choosing a model and starting the Node service. Inference needs no
internet after setup. With configuration, the controller starts the persistent
network worker at boot without waiting for Wi-Fi association.

Copy the firmware example and edit only the ignored local copy:

```sh
cp include/LocalConfig.example.h include/LocalConfig.h
```

Set `ROBOT_WIFI_SSID`, `ROBOT_WIFI_PASSWORD` (empty for an open network), and
`ROBOT_TRANSCRIPTION_BASE_URL`. The URL must be `http://` plus the computer's
numeric LAN IPv4 address, an optional port, and an optional trailing slash;
for example `http://192.168.1.23:3000`. Paths, queries, hostnames, HTTPS and
redirects are unsupported. Bind the backend to the computer's LAN address or
`0.0.0.0` as described in its README. `localhost` on CoreS3 refers to CoreS3,
not the computer. This plain HTTP path is for a trusted LAN. Never commit
`include/LocalConfig.h` or real Wi-Fi credentials; only the placeholder example
is tracked. Missing configuration still compiles; an idle transcription press
reports it as unconfigured, and face/sleep/wake remain usable.

One persistent worker schedules Wi-Fi attempts every ten seconds without
waiting for association in the main loop. A disconnected idle tap fails
immediately. A ready tap starts capture and one HTTP/1.1 chunked
`POST /v1/transcriptions` together, with `application/octet-stream` and no
`Content-Length`. The streaming path allocates a long-lived 64-frame PSRAM ring:
40,960 PCM bytes, or 1.28 seconds at 16 kHz, 16-bit mono. It allocates no full
clip. The driver releases each 320-sample frame before main-task classification
publishes it to the worker. A partially written frame remains immutable until
all PCM bytes are accepted by the socket; the slot can then be reused without
a backend acknowledgment. Queue exhaustion fails the whole session rather than
block capture or transcribe audio with gaps.

The worker checks incoming responses and disconnects during capture, including
while writes stall. Its nonblocking socket has a one-second connection limit,
a five-second stalled-I/O limit, and a 25-second first-response wait after the
final chunk for local inference. There is no total request clock starting at
capture admission. HTTP response headers are limited to 1,024 bytes total and
255 bytes per line; successful plain UTF-8 text is limited to 2,048 bytes, with
explicit Content-Length and no transfer/content encoding. No redirects or
automatic request retries are performed.

The main task keeps polling after a logical timeout. It stops and joins the
microphone immediately on cancellation, but the ring remains borrowed until the
worker acknowledges socket close and its final read. The SDK disables SO_LINGER;
close can wait beyond the interaction deadline, and an exceptional close failure
keeps the worker and ring occupied while cleanup retries. A new distinct tap is
required after cleanup. Request IDs discard stale results; a late cancellation
cannot print a second diagnostic or affect the next session. Explicit UTF-8 byte
lengths preserve embedded NUL in Serial output.

### Verification

A successful build checks compilation and linking. The
[streaming plan](docs/plan/stream-audio-while-speaking.md) records native transport
checks and pending device measurements. The
[CoreS3 confirmation](docs/plan/speech-silence-stop.md#cores3-validation-follow-up)
records the user-reported device result. The
[transcription checklist](docs/plan/tap-to-transcribe.md#t3-connect-tap-recording-to-transcript-results)
covers detailed regression checks for microphone quality, failure recovery,
sleep/wake and blinking, Wi-Fi resource use, and LAN operation with external
internet disconnected.

Run deterministic controller tests on the host (requires a C++ compiler):

```sh
pio test -e native
```

This uses Unity and compiles the same state helper as the firmware, with no
device connection or upload. See [test/README](test/README) for fixtures,
coverage, and hardware limitations. There is no separate lint command.

## Planning and development workflow

Read [AGENTS.md](AGENTS.md) and the shared
[development workflow](docs/agent-workflow.md) before implementation.

- [Product brief](docs/product-brief.md): purpose, current boundary, and assumptions.
- [Feature backlog](docs/feature-backlog.md): observed capabilities and proposed work.
- [Roadmap](docs/roadmap.md): source implementation status and validation gaps.
- [Task plans](docs/plan/): focused requirements, decisions, tasks, and evidence.
- [Plan template](docs/plan/template.md): for medium and large code/configuration
  changes. Small changes and documentation-only work do not require a plan file.

### Development agents

The project includes the source workflow's three agent roles:

- [implementer](.codex/agents/implementer.toml): coordinates tasks and reviews;
  retains `gpt-6.1-sol` with `xhigh` reasoning.
- [developer](.codex/agents/developer.toml): implements the assigned task.
- [code_reviewer](.codex/agents/code_reviewer.toml): independently reviews it.

Code changes route through the implementer by default. Developer and reviewer
model settings remain unpinned and are chosen per task. The
[agent workflow](docs/agent-workflow.md#agent-routing-and-review) describes
selection, review rounds, and commit authorization. If the new roles are not
visible in Codex, reload the project or restart Codex before using them; their
presence on disk does not verify live availability.

## Workflow origin

The planning structure and task/review protocol were adapted from
[Jefray-alt/doc-ai-node](https://github.com/Jefray-alt/doc-ai-node/tree/dfb48852723950fe559c61e93e4267ab87217c54),
at commit `dfb48852723950fe559c61e93e4267ab87217c54`. Its root `AGENTS.md`,
`README.md`, `docs/agent-workflow.md`, `docs/product-brief.md`,
`docs/feature-backlog.md`, and `docs/roadmap.md` are the source counterparts.

Web application setup guides and completed task histories were replaced by
firmware guidance and a fresh plan template. The three custom agent TOML files
were also copied and adapted for firmware validation, local edit preservation,
and commit authorization. The source's PR-validation skill was not installed.
