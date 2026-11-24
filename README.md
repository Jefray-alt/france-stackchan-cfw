# France Stack-chan firmware

Custom firmware for an M5Stack CoreS3, built with PlatformIO, the Arduino
framework, and M5Unified. The current source implements a touch-driven face
with idle, listening, thinking, and sleeping interaction states. An idle tap
records until a sustained pause (at most ten seconds) and sends the clip to a
disposable local Whisper backend; the returned transcript appears once in Serial. This path is implemented with
host tests and firmware builds; CoreS3 transcription validation is pending.
Earlier local audio record/replay was user-validated on 2026-11-24.

## Repository

- `platformio.ini`: board, framework, dependencies, USB flags, and serial speeds.
- `src/main.cpp`: hardware initialization and the main update loop.
- `src/RobotController.cpp`: touch input, rendering, and state-change logging.
- `src/RobotStateMachine.cpp`: hardware-independent state and inactivity decisions.
- `src/RobotAudio.cpp`: reusable PSRAM PCM buffer and exclusive microphone/speaker lifecycle.
- `src/RobotTranscription.cpp`: bounded Wi-Fi/socket worker and clip/result handoff.
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

Update the [local backend](backend/README.md) before using this firmware; the
new service still accepts the earlier 64,000-byte recordings. Configure Wi-Fi
as below. Tap while idle, then speak during the 16 kHz mono capture. Start
within three seconds. A pause shorter than 800 ms keeps the clip open; 800 ms
of silence ends it. Continuous sound stops at exactly ten seconds. Leading
and trailing audio through the stop frame are retained.

The listening face changes to thinking while one request is processed. Success
prints `Transcript: <text>` once, preserving UTF-8 bytes, then returns idle.
A quiet tap, less than 120 ms of detected activity, or an empty transcript
prints `No speech detected` once. Quiet/noise captures below that minimum
return idle without uploading. There is no automatic replay; the retained
playback API uses the same finalized clip length.

Detection uses mean absolute energy on 20 ms frames, initially a threshold of
500 PCM units. The threshold and sample-based windows are tunable in
`include/RobotAudio.h` and need CoreS3 checks with ordinary/quiet speech and room
noise. This energy gate cannot reliably distinguish speech from steady noise;
Whisper can also produce text from noise. AI response and generated audio
remain later work.

Busy presses and held touches do not queue recordings. After 30 seconds idle,
the face sleeps; a sleeping press wakes to idle, and the next distinct press
records. Completion and failure refresh inactivity. A disconnected or
unconfigured idle press prints a diagnostic without capturing audio for later.
Capture has an 11-second watchdog from the start of listening. Transcription
has a separate 30-second watchdog. Failures print one diagnostic and return idle. After a request timeout,
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
waiting for association in the main loop. A disconnected submission fails
immediately. There is one request and one result slot; the worker borrows the
reusable 320,000-byte PSRAM PCM buffer instead of copying it, passing only
the actual finalized sample count. Allocation failure returns idle without
uploading. The main task keeps polling results even after an interaction times out: only an
acknowledgment after socket close releases the clip for a new recording.
Request IDs reject stale results, and explicit UTF-8 byte lengths preserve
embedded NUL in Serial output.

The direct nonblocking socket has a one-second connection limit, a five-second
upload/response-progress idle limit, a 25-second first-response wait for local
inference, and a shared 30-second request deadline. No redirects or automatic
request retries are performed. HTTP headers are limited to 1,024 bytes total
and 255 bytes per line; successful plain UTF-8 text is limited to 2,048 bytes,
with explicit Content-Length and no transfer/content encoding. Socket close
finishes before result publication. The installed SDK disables SO_LINGER;
close can wait beyond the 30-second interaction deadline. An exceptional close
failure keeps the worker and clip occupied while it retries cleanup. Actual
SDK latency, worker stack/heap headroom, Wi-Fi behavior and repeated requests
still require CoreS3 checks. A logical timeout does not establish that the
worker has released the clip.

### Verification

A successful build checks compilation and linking. Touch, display, sleep,
wake, and blink behavior also need verification on the physical CoreS3.
Use the [silence-stop checklist](docs/plan/speech-silence-stop.md) and
[transcription checklist](docs/plan/tap-to-transcribe.md#t3-connect-tap-recording-to-transcript-results)
for ten real phrases, microphone quality, failure/cleanup/retry, repeated
sleep/wake and blinking, Wi-Fi heap/stack/task headroom, and LAN operation with
external internet disconnected. These checks remain pending; the earlier
[audio validation](docs/plan/audio-record-replay.md#hardware-validation-follow-up)
is historical evidence for record/replay, not this transcription path.

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
