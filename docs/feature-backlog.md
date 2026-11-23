# Feature backlog

This backlog starts from the current firmware. The priority order is proposed,
not an approved implementation queue. Pick only the outcome requested by the
user and detail it in a [task plan](plan/template.md).

## Validated foundation

The user confirmed the original interactive-face milestone on CoreS3 on
2026-11-23; see the [validation record](plan/interactive-face-validation.md).
That evidence predates the controller refactor. Device regression checks on
the refactored firmware remain pending.

| Original priority | Capability | Status |
|---|---|---|
| 1 | Boot and draw the idle face | Complete; previously user-validated on CoreS3. |
| 2 | Cycle awake states by touch | Complete; previously user-validated on CoreS3. |
| 3 | Sleep after inactivity and wake by touch | Complete; original fix committed in `3313304` and user-validated. Controller decisions now have native regression coverage. |
| 4 | Blink while awake | Complete; previously user-validated on CoreS3, including wake recovery. |

## Remaining verification work

| Original priority | Capability | Status and next evidence |
|---|---|---|
| 5 | Reproducible regression checks | 15 native Unity controller tests pass; see the [controller test plan](plan/controller-unit-tests.md). Repeat physical touch/display/Serial/sleep/wake/blink checks after the refactor. Renderer/blink logic has no automated coverage. |

Keep detailed acceptance criteria and any confirmed defects in the active task
plan. No new implementation feature is selected.

## Later candidates

| Candidate | Decision needed before implementation |
|---|---|
| Audio input and output | Choose the interaction, microphone/speaker usage, and buffering requirements. |
| Speech and AI conversation | Choose local versus remote processing, latency expectations, connectivity, and credential handling. |
| Servo movement | Confirm connected hardware, pins, power, and mechanical limits. |
| Persistent preferences | Define which settings survive reboot and how the user changes them. |
| Dependency version pins | Select known-good versions from a verified build and device check. |
| Additional boards | Select target hardware and define which capabilities must work there. |

All six candidates remain unselected; none is a prerequisite for the completed
interactive-face milestone.
