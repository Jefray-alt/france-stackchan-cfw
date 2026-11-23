# Product brief: CoreS3 Stack-chan firmware

**Status:** Initial working brief inferred from the existing firmware. The
proposed MVP boundary below needs confirmation before future feature selection;
this documentation migration does not approve new product work.

## Purpose and audience

Build an expressive desktop companion on the M5Stack CoreS3 while keeping the
embedded control flow understandable to its developer. The first user is the
builder interacting with the robot through its touch display.

## Current foundation

The repository contains Arduino firmware that draws a face, changes its visual
state on touch, animates blinking, and implements inactivity-based sleep and
wake logic. This is a source-code observation, not confirmation of behavior on
the physical device. Current validation status belongs in the
[roadmap](roadmap.md).

## Proposed first milestone

A user can:

1. Power on the CoreS3 and see the idle face.
2. Tap through idle, listening, thinking, and speaking visual states.
3. See the face blink while awake.
4. Leave it inactive for about 30 seconds to show a sleeping face.
5. Wake it by touch, restore the previous awake state, and continue interacting.

These outcomes consolidate behavior already represented in the code. Their
correctness still needs verification; they do not claim an audio pipeline or
an AI conversation feature.

## Proposed boundary

Microphone capture, speech recognition, generated responses, speech playback,
Wi-Fi services, credentials, servo motion, persistent settings, and support for
additional boards are later candidates. Select and design any of these only
when requested; do not infer approval from a state named `LISTENING` or
`SPEAKING`.

## Engineering principles

- Keep initialization, interaction decisions, and rendering responsibilities clear.
- Keep updates responsive; schedule animation and inactivity with elapsed time.
- Make transitions observable through useful serial messages.
- Distinguish successful builds from verified device behavior.
- Add dependencies and abstractions only for an actual feature need.
