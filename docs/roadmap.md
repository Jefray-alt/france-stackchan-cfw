# Roadmap

Updated on 2026-11-23 to reconcile earlier device validation and current native
controller tests. This roadmap has no delivery estimates. Future work remains
subject to the user's priorities.

## Completed: establish a reliable interactive face

**Outcome:** A CoreS3 displays an expressive face, responds to touch, blinks
while awake, sleeps after inactivity, and wakes into its prior awake state.
The user confirmed the original milestone on CoreS3; see the historical
[validation record](plan/interactive-face-validation.md) and
[product brief](product-brief.md). The reviewed baseline included the sleep/wake
fix in `3313304`; the exact flashed revision and detailed scenario logs were
not supplied.

| Area | Observed implementation | Validation evidence |
|---|---|---|
| Hardware foundation | PlatformIO environment `m5stack-cores3`, Arduino, M5Unified, USB serial flags | Original milestone user-validated on CoreS3; refactored firmware compiles. |
| Startup and update loop | `M5.begin`, serial initialization, `M5.update`, robot updates, and a 10 ms loop delay | Previously user-validated on CoreS3. |
| Face states | Idle, listening, thinking, speaking, and sleeping enum values and rendering | Previously user-validated on CoreS3. |
| Touch transitions | Cycles awake states and handles touch while sleeping | Previously user-validated; refactored state decisions covered by native tests. |
| Inactivity and wake | 30-second threshold and previous-awake-state tracking | Original fix committed in `3313304` and user-validated; native boundary/rollover/wake tests pass. |
| Blinking | Approximately 3 seconds open and 150 ms closed while awake | Previously user-validated, including wake recovery; no automated renderer/blink coverage. |
| Automated checks | Native Unity controller suite; see [test instructions](../test/README) | 15/15 pass; results and limitations are in the [controller test plan](plan/controller-unit-tests.md). |

## Current verification gap

The controller extraction preserves the existing decisions and passes native
tests and `pio run -e m5stack-cores3`. Those results do not validate physical
behavior after the refactor. Repeat the [CoreS3 checklist](agent-workflow.md#hardware-checklist)
for touch/hold detection, displayed states, Serial output, and repeated sleep/wake
with blink recovery. These checks remain pending on this candidate.

Earlier device evidence remains user-reported. No new device check or firmware
upload was performed for the controller extraction or this documentation update.

## Development workflow

The planning and review documents are adapted from `doc-ai-node`; see the
[migration plan](plan/development-workflow.md) for the source revision,
decisions, and checks. That migration did not validate hardware; the later user
confirmation and controller-test results are recorded separately above.

## Next

No new implementation feature is selected. When requested, choose one outcome
from the [backlog](feature-backlog.md), create its plan, verify it, and update
this roadmap with evidence. Do not automatically begin an audio, network, or AI
integration.
