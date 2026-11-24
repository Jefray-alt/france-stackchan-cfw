# Feature backlog

This backlog starts from the current firmware. The priority order is proposed,
not an approved implementation queue. Pick only the outcome requested by the
user and detail it in a [task plan](plan/template.md).

## Validated foundation

The user confirmed the original interactive-face milestone on CoreS3 on
2026-11-23; see the [validation record](plan/interactive-face-validation.md).
That evidence predates the controller refactor. The user also confirmed local
audio record/replay working on CoreS3 on 2026-11-24; detailed current-firmware
touch/display/Serial/sleep/wake/blink regression observations remain unrecorded.

| Original priority | Capability | Status |
|---|---|---|
| 1 | Boot and draw the idle face | Complete; previously user-validated on CoreS3. |
| 2 | Cycle awake states by touch | Complete; previously user-validated on CoreS3. |
| 3 | Sleep after inactivity and wake by touch | Complete; original fix committed in `3313304` and user-validated. Controller decisions now have native regression coverage. |
| 4 | Blink while awake | Complete; previously user-validated on CoreS3, including wake recovery. |

## Remaining verification work

| Original priority | Capability | Status and next evidence |
|---|---|---|
| 5 | Reproducible regression checks | 19 controller, 8 audio and 7 protocol native cases cover current transcription decisions, timing and framing. Device transcription and detailed interaction regressions remain pending; see the [transcription plan](plan/tap-to-transcribe.md). |

Keep detailed acceptance criteria and any confirmed defects in the active task
plan. The selected local two-second record/replay milestone is complete with
user-confirmed CoreS3 validation; evidence and limits are in the
[audio validation record](plan/audio-record-replay.md#hardware-validation-follow-up).

## Implemented; device validation pending

Tap-to-transcribe replaces automatic replay with a recording that ends after
silence (up to ten seconds), one local Whisper request and a Serial transcript. Host tests, backend tests and
firmware builds pass. Ten real phrases, microphone quality, cleanup/retry,
Wi-Fi resource use and disconnected-internet LAN operation remain pending in
the [transcription](plan/tap-to-transcribe.md) and
[silence-stop](plan/speech-silence-stop.md) plans. The earlier audio validation
remains historical evidence.

## Later candidates

| Candidate | Decision needed before implementation |
|---|---|
| AI conversation after transcription | Choose response generation, audio output, latency and processing boundaries. |
| Servo movement | Confirm connected hardware, pins, power, and mechanical limits. |
| Persistent preferences | Define which settings survive reboot and how the user changes them. |
| Dependency version pins | Select known-good versions from a verified build and device check. |
| Additional boards | Select target hardware and define which capabilities must work there. |

All five candidates remain unselected; none is a prerequisite for the completed
interactive-face milestone.
