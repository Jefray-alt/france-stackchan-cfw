# <Specific outcome>

Use this template for medium and large code or configuration changes. Small
changes and purely documentation changes do not require a plan file unless the
user requests one. When needed, copy it to a descriptive lowercase, hyphenated
filename under `docs/plan/`. Replace placeholders and remove unused sections.
This template is not an active task.

**Status:** Planned / in progress / verified / blocked.

**Outcome:** <What the user or developer can do after this change.>

## Baseline and scope

- Request and accepted requirements: <Reference or concise statement.>
- Branch and starting commit: <Recorded checkout state.>
- Existing local edits to preserve: <Paths, or none.>
- In scope: <One specific outcome.>
- Out of scope: <Only boundaries needed to prevent scope drift.>
- Scope assessment: <Outcomes, complexity, dependencies, and validation needs.
  If large, flag the scope and recommend a more contained outcome.>

## Requirements and decisions

- <Observable behavior and important edge cases.>
- <Design choice, reason, and affected source/header boundaries.>
- <Timing, memory, hardware, or dependency impact where relevant.>

## Ordered tasks

### T1: <Reviewable behavior or outcome>

**Scope and ownership:** <Files or responsibility; include relevant tests and docs.>

**Dependencies:** <Earlier tasks, hardware, or none.>

**Acceptance:** <Concrete observable criteria.>

**Checks:** <Exact commands and behavioral/hardware checks. State whether a
test uploads to a device. For docs, explain why a failing test is inapplicable.>

**Proposed commit if authorized:** `<type>(<scope>): <imperative description>`

**Status and evidence:** <Pending, commands with results, and review findings.>

Repeat for cohesive tasks as needed. A small indivisible change can use one
task; state why. Complete and review each task before starting the next. Commit
per task when commits are authorized; otherwise keep task boundaries recorded.

## Review and validation

- Review mode: <Self-review or independent reviewer and task brief.>
- For delegated review: <Round 1-3, verdict, findings, and corrections.>
- Build/tests: <Exact commands, exit results, and environment.>
- Hardware: <Board, firmware revision or local candidate, scenarios, observations.>
- Limitations: <Checks not run and why; distinguish from passing checks.>

## Handoff

- <Implemented outcome and remaining limitations.>
- <Task-to-commit mapping if commits were requested, otherwise uncommitted.>
- <Roadmap update only for an actual completed product capability.>
