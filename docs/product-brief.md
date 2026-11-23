# Product brief: CoreS3 Stack-chan firmware

**Status:** Original interactive-face milestone completed with user-reported
CoreS3 validation on 2026-11-23. Local audio record/replay was user-validated on
CoreS3 on 2026-11-24; detailed post-refactor regression observations remain
unrecorded.
Future product scope remains subject to the user's priorities.

## Purpose and audience

Build an expressive desktop companion on the M5Stack CoreS3 while keeping the
embedded control flow understandable to its developer. The first user is the
builder interacting with the robot through its touch display.

## Current foundation

The repository contains Arduino firmware that draws a face, changes its visual
state on touch, animates blinking, and implements inactivity-based sleep and
wake logic. The user confirmed the original milestone on CoreS3; evidence and
limitations are in the [validation record](plan/interactive-face-validation.md).
Local two-second recording and automatic replay are implemented with native
state/timing tests and user-confirmed hardware validation on 2026-11-24; see the
[audio validation record](plan/audio-record-replay.md#hardware-validation-follow-up).
Current host coverage and pending device checks belong in the [roadmap](roadmap.md).

## Completed first milestone

A user can:

1. Power on the CoreS3 and see the idle face.
2. Tap through idle, listening, thinking, and speaking visual states.
3. See the face blink while awake.
4. Leave it inactive for about 30 seconds to show a sleeping face.
5. Wake it by touch, restore the previous awake state, and continue interacting.

These outcomes describe the visual interaction behavior previously validated
by the user. Detailed touch/blink/sleep/wake regression observations for the
current firmware remain unrecorded.
The current audio interaction replaces manual state cycling; see the
[audio plan](plan/audio-record-replay.md). AI conversation remains a later candidate.

## Proposed boundary

Beyond local record/replay, speech recognition, generated responses, Wi-Fi
services, credentials, servo motion, persistent settings, and support for
additional boards are later candidates. Select and design any of these only
when requested; do not infer approval from a state named `LISTENING` or
`SPEAKING`.

## Engineering principles

- Keep initialization, interaction decisions, and rendering responsibilities clear.
- Keep updates responsive; schedule animation and inactivity with elapsed time.
- Make transitions observable through useful serial messages.
- Distinguish successful builds from verified device behavior.
- Add dependencies and abstractions only for an actual feature need.
