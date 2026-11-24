# Roadmap

Updated on 2026-11-24 for implemented tap-to-transcribe with pending device validation.
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

## Implemented: tap to transcribe; device validation pending

An idle tap records until sustained silence or a ten-second cap and submits
one variable-length request to the disposable local Whisper backend. Quiet or
brief-noise captures return idle without uploading. Thinking remains responsive;
a transcript or no-speech message prints once, then the robot returns idle. Readiness rejects disconnected taps,
separate 11/30-second watchdogs recover capture/request failures, and pending
worker ownership prevents buffer reuse after logical timeout. Late IDs are
discarded; idle sleep and wake-to-idle remain.

All 34 native cases (19 controller, 8 audio, 7 protocol), 33 backend tests and both
missing/example-configuration firmware builds pass. A real ten-second
local-model clip returned HTTP 200 with words beyond the old two-second cutoff.
No CoreS3 transcription upload or validation ran: ten phrases, microphone quality, physical touch,
Serial/face/blink agreement, repeated sleep/wake, failure cleanup/retry, Wi-Fi
heap/stack/task stability and LAN operation without external internet remain
pending. See the [silence-stop](plan/speech-silence-stop.md) and
[transcription](plan/tap-to-transcribe.md) plans for evidence and the hardware
checklist. This does not claim the device milestone complete.

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

Validate the implemented transcription path on CoreS3 when a device upload is
authorized. No further feature is selected. Choose later outcomes from the
[backlog](feature-backlog.md) and prepare their own approved plans.
