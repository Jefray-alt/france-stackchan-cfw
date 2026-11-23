# Roadmap

This roadmap records the source baseline inspected on 2026-11-23. It has no
delivery estimates. Future work remains subject to the user's priorities.

## Current: establish a reliable interactive face

**Outcome:** A CoreS3 displays an expressive face, responds to touch, blinks
while awake, sleeps after inactivity, and wakes into its prior awake state.
This milestone is inferred from existing code; see the working
[product brief](product-brief.md).

| Area | Observed implementation | Validation status at migration |
|---|---|---|
| Hardware foundation | PlatformIO environment `m5stack-cores3`, Arduino, M5Unified, USB serial flags | Configuration inspected; physical operation not verified by this migration. |
| Startup and update loop | `M5.begin`, serial initialization, `M5.update`, robot updates, and a 10 ms loop delay | Present in source; device startup check pending. |
| Face states | Idle, listening, thinking, speaking, and sleeping enum values and rendering | Present in source; visual verification pending. |
| Touch transitions | Cycles awake states and handles touch while sleeping | Present in source; repeated interaction check pending. |
| Inactivity and wake | 30-second threshold and previous-awake-state tracking | In progress: `src/RobotController.cpp` had uncommitted user edits at migration; no completion claim. |
| Blinking | Approximately 3 seconds open and 150 ms closed while awake | Present in source; sleep/wake regression check pending. |
| Automated checks | `test/` contains only its PlatformIO placeholder README | No executable automated suite configured. |

## Development workflow

The planning and review documents are adapted from `doc-ai-node`; see the
[migration plan](plan/development-workflow.md) for the source revision,
decisions, and checks. This documentation work does not complete a firmware
feature or validate hardware behavior.

The existing local firmware passed `pio run -e m5stack-cores3` during the
migration. Hardware checks in the table above remain pending.

## Next

No new implementation feature is selected. When requested, choose one outcome
from the [backlog](feature-backlog.md), create its plan, verify it, and update
this roadmap with evidence. Do not automatically begin an audio, network, or AI
integration.
