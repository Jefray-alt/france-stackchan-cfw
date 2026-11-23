# Development and review workflow

This protocol adapts the planning and review flow from `doc-ai-node` to this
PlatformIO firmware project. It covers code and documentation changes. The main
session owns requirements, planning, implementation coordination, and handoff.
Code-change requests route through the configured `implementer` by default.
Use [AGENTS.md](../AGENTS.md) for repository conventions and
[README.md](../README.md) for setup and commands.

## Before starting

Inspect the checkout before editing:

```sh
git status --porcelain=v1 --untracked-files=all
git symbolic-ref --quiet --short HEAD
git rev-parse HEAD
```

Record the starting branch, revision, and existing local edits in the plan.
Unrelated local modifications do not automatically block work. Identify the
owned files and preserve all other changes. If the request overlaps an edited
file, inspect the diff and determine whether the intended work is clear; ask
for clarification only if ownership or intent remains ambiguous. Stop for
unresolved merge conflicts. Do not stash, discard, or commit existing work as
a shortcut.

For new implementation work on `main`, prefer a task branch named
`<type>/<task>` when it can be created without disturbing the checkout. Continue
an existing task branch when appropriate. If HEAD is detached, establish a
task branch before editing rather than leave changes without a branch. Inspect
state again after branch changes. These checks should protect work, not force
unnecessary branch switching during an active task.

## Plan one outcome

Create or update `docs/plan/<task>.md` before implementation. Use the
[template](plan/template.md) and publish a concise ordered task plan to the user.
For each task, specify:

- Task ID, concrete outcome, scope, and owned files or responsibilities.
- Dependencies and accepted requirements.
- Observable acceptance criteria and expected checks.
- Proposed Conventional Commit, if commits are requested.

Split substantive work into cohesive, independently reviewable tasks. Keep the
implementation, relevant tests, and docs for each outcome together. A small,
indivisible change can be one task; say why. Do not add tasks solely to increase
their count or detail unrelated future features.

Estimate total added and deleted lines across the full plan, including tests
and docs. If it appears likely to exceed 1,000 lines and that larger scope is
not already authorized, propose a smaller outcome before implementation.
This is a planning guardrail, not a hard implementation cutoff. Revise the plan
if discovery changes scope or dependencies.

## Implement and verify one task at a time

1. Confirm acceptance criteria, baseline, ownership, and relevant existing changes.
2. For testable behavior, demonstrate a meaningful failure before the fix when
   practical. Then implement the minimum change and show that it passes.
3. For hardware-only behavior, record the reproduction and expected device
   observations. Explain unavailable hardware rather than invent a host test.
4. For documentation or configuration, explain when a failing test adds no
   value and use appropriate inspection, command, or consistency checks.
5. Run the relevant checks and freeze the candidate for review. Include new
   files, exact commands and exit results, and hardware evidence or gaps.
6. Resolve review findings before advancing to the next task. Commit each task
   separately only when authorized; otherwise leave changes uncommitted and
   retain task boundaries in the plan.

The existing build command is:

```sh
pio run -e m5stack-cores3
```

Inspect compiler output and, where relevant, memory usage. There is no automated
suite or lint command configured yet. If adding PlatformIO tests, document the
runner, target, fixtures, and exact invocation. Some device test commands upload
test firmware; describe that effect before running them within an authorized
hardware task. Do not run a test upload as a routine documentation check.

Prefer controlled elapsed-time inputs for timing tests where a test seam exists.
For changes to inactivity, state transitions, or blinking, include boundary
conditions, repeated cycles, and unsigned timer rollover when relevant.

### Hardware checklist

Use the checks relevant to the change on the intended CoreS3. Record the board,
candidate revision or local changes, observations, and any deviations. These
are expected behaviors to verify, not claims that this migration tested them.

- Startup shows the idle face and prints `Robot initialized` at 115200 baud.
- Distinct presses cycle `IDLE -> LISTENING -> THINKING -> SPEAKING -> IDLE`.
  Holding a touch does not create unintended extra transitions.
- Awake colors and shapes match the selected state: white idle, cyan listening,
  yellow thinking, and green speaking.
- Awake blinking closes the eyes briefly, with roughly 3 seconds open and
  150 ms closed. Interaction remains responsive.
- After about 30 seconds without interaction, the face sleeps. Touch near the
  timeout should refresh inactivity rather than cause an unexpected transition.
- A press while sleeping restores the prior awake state; normal blinking and
  touch cycling continue. Repeat sleep/wake from each awake state.
- Serial state messages agree with the rendered state; repeated use causes no
  unexpected resets or frozen display.

If hardware is unavailable, report which checks remain pending. A successful
compile does not establish that these observations pass.

## Agent routing and review

Project-scoped role files live under `.codex/agents/`:

- `implementer.toml`: coordinates ordered tasks, review rounds, and authorized
  commits. Uses the source configuration: `gpt-6.1-sol` with `xhigh` reasoning.
- `developer.toml`: implements one task and supplies the frozen candidate and
  validation evidence. Model and reasoning are selected per task.
- `code_reviewer.toml`: independently reviews the frozen candidate and reports
  findings. Model and reasoning are selected separately from the developer.

For code-change requests, the main session passes requirements and decisions to
the implementer. Planning and exploration remain in the main session. Small
documentation/configuration work, including installing these roles, can stay
in the main session with an explicitly identified self-review.

Check the live role definitions before routing implementation. Files on disk
are configuration, not proof that the session has loaded them. If new roles or
settings are not visible, reload the project or restart Codex and check again.
If a required role or configuration remains unavailable, report the blocker
and stop routed implementation; do not silently substitute another role or
label a generic worker with a custom role name and claim its settings applied.
Self-review must never be reported as independent review.

### One task and review pair at a time

- The implementer coordinates the task plan and shared checkout. The main
  session publishes the ordered plan before workers start.
- For each task, use a fresh developer and independent reviewer, then reuse
  that pair for corrections. Workers must not delegate further.
- Give the developer the task ID, baseline, requirements, acceptance criteria,
  file ownership, dependencies, expected checks, and relevant prior decisions.
- Give the reviewer a neutral brief with the same requirements, frozen candidate
  scope, tracked and untracked changes, and validation evidence. The reviewer
  independently inspects the candidate and runs suitable checks without changing
  source, staging, committing, pushing, or flashing hardware.
- Select developer and reviewer settings independently using the guidance below.
- The reviewer returns `PASS`, `CHANGES_REQUESTED`, or `BLOCKED`, with concrete
  findings, evidence, and the current review round.

### Model and reasoning selection

Preserve the source implementer's pinned model and effort in its role file.
Leave worker model/effort fields unset so the implementer can select each pair.
Before every worker spawn, record the supported model, reasoning effort, and a
brief rationale, then pass both explicitly with `fork_turns=none`. A prompt or
task name alone does not select the runtime configuration.

Use the source workflow's starting points, adjusting to scope and risk:

- `gpt-6-luna` with `xhigh` for simple, well-specified work.
- `gpt-6.1-sol` with `medium` for routine coding with clear requirements.
- `gpt-6.1-sol` with `xhigh` for complex changes, debugging, or design decisions.

Choose the reviewer independently based on difficulty and potential impact.
Agent-file model/effort values take precedence over spawn choices; do not edit
worker files to express a per-task preference. If the selected configuration
is unsupported, rejected, or pinned by a stale loaded role, report the blocker
instead of silently changing it. Reuse the same pair for corrections; model
selection does not reset the review budget.

### Review limit

Allow three delegated review rounds per task: the initial review plus two
re-reviews. Count every dispatched review, including `BLOCKED`. Reuse the same
pair for corrections. If round three does not pass, preserve the changes and
report unresolved findings and required decisions; do not reset the budget by
renaming the task or changing reviewers. An explicit user request to retry
starts a new three-round budget with the findings carried forward.

## Finish and hand off

Check the complete diff and new files, review whitespace, and compare the final
checkout state with the recorded baseline:

```sh
git diff --check
git diff --stat
git status --short
```

Also inspect whitespace and content in new files, which `git diff --check`
omits until they are tracked. Do not stage files solely to make a check see them.

If commits are authorized, complete review and required checks, stage only the
task's owned changes, and make its Conventional Commit before the next task.
When a file contains unrelated existing edits, stage only the owned changes.
Confirm no unexpected changes entered the commit. The implementer never pushes;
the main session may push only when explicitly authorized.

Update the active plan with outcomes, checks, review mode, and limitations.
Update [the roadmap](roadmap.md) only for actual product progress; document
partial implementation and remaining verification separately. Do not mark a
feature complete because its code compiles or its documentation was updated.

Report what changed, why, what passed, and what remains unverified. Include
task-to-commit mapping when commits were requested. Do not automatically start
the next feature, add workflow dependencies, or change agent configuration
beyond the requested scope.
