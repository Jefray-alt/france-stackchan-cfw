# Feature backlog

This backlog starts from the current firmware. The priority order is proposed,
not an approved implementation queue. Pick only the outcome requested by the
user and detail it in a [task plan](plan/template.md).

## Current foundation and verification work

| Priority | Capability | Source status and next evidence |
|---|---|---|
| 1 | Boot and draw the idle face | Implemented in `main.cpp` and `FaceRenderer`; verify startup and rendering on a CoreS3. |
| 2 | Cycle awake states by touch | Implemented in `RobotController`; verify the order, one transition per press, and continued responsiveness. |
| 3 | Sleep after inactivity and wake by touch | Logic exists, with local work in progress at migration time; verify timing, previous-state restoration, and repeated sleep/wake cycles before declaring complete. |
| 4 | Blink while awake | Implemented in `FaceRenderer`; verify the blink cadence, sleeping appearance, and recovery after wake. |
| 5 | Reproducible regression checks | No automated suite exists. Consider focused state/timing tests when changing that behavior; establish a device smoke-check record. |

Priorities 1-4 are verification targets for existing behavior, not instructions
to rewrite it. Keep detailed acceptance criteria and any confirmed defects in
the active task plan.

## Later candidates

| Candidate | Decision needed before implementation |
|---|---|
| Audio input and output | Choose the interaction, microphone/speaker usage, and buffering requirements. |
| Speech and AI conversation | Choose local versus remote processing, latency expectations, connectivity, and credential handling. |
| Servo movement | Confirm connected hardware, pins, power, and mechanical limits. |
| Persistent preferences | Define which settings survive reboot and how the user changes them. |
| Dependency version pins | Select known-good versions from a verified build and device check. |
| Additional boards | Select target hardware and define which capabilities must work there. |

None of the later candidates is a commitment or a prerequisite for this
documentation migration.
