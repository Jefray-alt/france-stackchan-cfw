# Roadmap

Updated on 2026-11-24 to record user-confirmed tap-to-transcribe on CoreS3.
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

## Completed: tap to transcribe with silence-stop

An idle tap records until sustained silence or a ten-second cap and submits
one variable-length request to the disposable local Whisper backend. Quiet or
brief-noise captures return idle without uploading. Thinking remains responsive.
A transcript or no-speech message prints once, then the robot returns idle.
Readiness rejects disconnected taps. Separate 11/30-second watchdogs recover
capture/request failures. Worker ownership prevents buffer reuse after a logical
timeout. The controller discards late results by request ID and handles idle
sleep and wake-to-idle.

The user confirmed the flow works on CoreS3 on 2026-11-24. All 34 native cases
(19 controller, 8 audio, 7 protocol) and the firmware build passed for the
five-second pause settings. Earlier backend validation passed 33 tests, and a
real ten-second local-model request returned words beyond the old two-second
cutoff. See the [CoreS3 confirmation and automated evidence](plan/speech-silence-stop.md#cores3-validation-follow-up).

## Remaining verification detail

Device evidence is user-reported. The exact flashed revision, Serial logs,
cycle counts, and per-scenario results for clip tails, busy touches, sleep/wake,
blink recovery, and driver cleanup remain unrecorded. The earlier third-attempt
playback issue has no confirmed diagnosis in this record.

## Development workflow

The planning and review documents are adapted from `doc-ai-node`; see the
[migration plan](plan/development-workflow.md) for the source revision,
decisions, and checks. That migration did not validate hardware; the later user
confirmation and controller-test results are recorded separately above.

## Next

Detailed device regression scenarios are listed in the
[transcription checklist](plan/tap-to-transcribe.md#t3-connect-tap-recording-to-transcript-results).
Future feature choices are in the [backlog](feature-backlog.md).
