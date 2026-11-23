# Roadmap

Updated on 2026-11-24 for user-confirmed CoreS3 audio validation.
This roadmap has no delivery estimates. Future work remains
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

## Completed: local audio record and replay

Local two-second microphone capture and automatic speaker replay replace manual
state cycling. Busy presses are ignored, sleep is idle-only, and a sleeping
press wakes to idle. The 12 current native tests cover decisions, recovery, and
timing; firmware compiles. On 2026-11-24 the user confirmed audio is now working
through hardware validation on CoreS3. The local record/replay milestone is
complete based on that confirmation; see the
[validation record](plan/audio-record-replay.md#hardware-validation-follow-up).

## Remaining verification detail

Device evidence is user-reported. The exact flashed revision, Serial logs,
cycle counts, and individual results for clip tails, busy touches, sleep/wake,
blink recovery, and driver cleanup were not supplied. The earlier third-attempt
playback issue has no confirmed diagnosis in this record. No assistant-operated
device check or firmware upload was performed for this documentation update.

## Development workflow

The planning and review documents are adapted from `doc-ai-node`; see the
[migration plan](plan/development-workflow.md) for the source revision,
decisions, and checks. That migration did not validate hardware; the later user
confirmation and controller-test results are recorded separately above.

## Next

No further feature is selected. Choose the next outcome from the
[backlog](feature-backlog.md) and prepare its own approved plan. Retain the audio
checklist for regression checks when firmware changes.
