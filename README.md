# France Stack-chan firmware

Custom firmware for an M5Stack CoreS3, built with PlatformIO, the Arduino
framework, and M5Unified. The current source implements a touch-driven face
with idle, listening, thinking, speaking, and sleeping states. Listening,
thinking, and speaking are visual states; audio and AI integration are future
candidates.

## Repository

- `platformio.ini`: board, framework, dependencies, USB flags, and serial speeds.
- `src/main.cpp`: hardware initialization and the main update loop.
- `src/RobotController.cpp`: touch input, rendering, and state-change logging.
- `src/RobotStateMachine.cpp`: hardware-independent state and inactivity decisions.
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
runs host tests. Dependency versions are currently unpinned; record resolved
versions when investigating differences between machines.

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

### Verification

A successful build checks compilation and linking. Touch, display, sleep,
wake, and blink behavior also need verification on the physical CoreS3.
Use the [hardware checklist](docs/agent-workflow.md#hardware-checklist) and
record what actually ran in the task plan when required, or in the handoff or PR.

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
