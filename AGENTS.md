# Project planning and development rules

## Planning documents

- [Product brief](docs/product-brief.md): goals, first milestone, and assumptions.
- [Feature backlog](docs/feature-backlog.md): capabilities, candidates, and priorities.
- [Roadmap](docs/roadmap.md): implementation status and validation gaps.
- `docs/plan/<task>.md`: current requirements, decisions, ordered tasks, and evidence.

Create or update a focused Markdown plan under `docs/plan/` for medium and large
code or configuration changes. This includes substantive refactors, changes
across components, and work with significant design decisions or dependencies.
Small, isolated changes do not require a plan file. Purely documentation changes
do not require a plan file, regardless of size, unless the user requests one.
For work without a plan file, state scope, checks, and commit intent briefly in
the conversation or PR.

When a plan is required, use a descriptive lowercase, hyphenated filename and
start with [the plan template](docs/plan/template.md). Update the same file as
scope, decisions, and task status change; do not duplicate detailed requirements
in the roadmap or backlog.

## Workflow

1. Understand the request and inspect the relevant source and checkout state.
2. For new product direction, clarify the brief and priorities before coding.
3. Detail only the current requested outcome and its acceptance criteria.
4. For medium and large changes, publish ordered tasks and proposed commits in
   a plan. For small or documentation-only changes, give a brief inline scope
   and commit intent without creating a plan file.
5. Present the plan or inline scope to the user and wait for explicit approval
   before implementation. Publishing a plan, choosing a feature, or clarifying
   requirements does not count as approval. Record approval in the plan or task
   brief before routing implementation or starting development agents.
6. Implement and review one cohesive task at a time, with its tests and docs.
7. Record validation and limitations; update the roadmap only when a product
   capability is actually completed.

Before approval, limit work to inspection, research, and preparing or revising
the plan. If the scope or approach changes materially, present the revised plan
and wait for approval again before implementing the affected work.

Follow [the development workflow](docs/agent-workflow.md) for startup checks,
review, and handoff. For code-change requests, the main session routes work to
the configured `implementer` after plan approval, passing user requirements,
decisions, and the approval record. Planning, exploration, and small
documentation/configuration tasks can remain in the main session. The
implementer uses fresh `developer` and independent
`code_reviewer` pairs, one task at a time, without worker delegation.

The roles are configured under `.codex/agents/`. Describe independent review
accurately; self-review is not independent review. If a required configured role
or selected model is unavailable, report the limitation and stop routed code
implementation instead of silently substituting one. Installing or editing
role files does not prove that the current session has loaded them.

## Scope and local changes

- Keep each plan focused on one outcome; defer unrelated fixes and features.
- Estimate total added and deleted lines across code, tests, and docs. If the
  requested plan appears likely to exceed 1,000 lines, propose a smaller slice
  before implementation, unless the user already authorized the larger scope.
  Splitting tasks does not reduce the total estimate.
- Do not expand the agreed product scope or start the next feature automatically.
- Preserve existing user edits. Work alongside unrelated changes when safely
  isolated; stop for overlapping changes, merge conflicts, or ambiguous ownership.
- Do not stash, discard, stage, or commit someone else's work to satisfy a guard.
- Keep documentation lightweight and proportional to the request.

## Firmware conventions

- Target the existing `m5stack-cores3` environment in `platformio.ini`.
- Keep `main.cpp` focused on initialization and the update loop;
  `RobotController` owns interaction/state decisions, `FaceRenderer` owns drawing
  and animation, and `RobotState.h` owns shared state values.
- Put declarations in `include/` and implementations in `src/`; use `lib/` for
  genuine project-specific libraries when needed.
- Match the existing C++ style: two-space indentation, braces on their own
  lines, `#pragma once` in headers, and blank lines between logical blocks.
- Prefer elapsed-time scheduling in update methods. Handle `millis()` rollover
  with unsigned subtraction; avoid adding blocking delays to interaction or
  animation paths.
- Keep controller and renderer state consistent through transitions. When
  touching sleep/wake logic, verify repeated cycles and blink recovery.
- Avoid unnecessary heap allocation in frequent update/render paths. Add
  dependencies, tasks, or abstractions only when the feature needs them.
- Keep credentials out of committed files if connectivity is introduced.

## Validation

- For firmware changes, run `pio run -e m5stack-cores3` and relevant behavior
  checks. A build is not proof of device behavior.
- For a testable behavior change, first demonstrate a meaningful failing test
  when practical, then pass it with the implementation. Use focused timing or
  state tests where they add value; do not create tests that mirror the code.
- Run `pio test -e native` for controller state/timing tests; see
  [test/README](test/README) for fixtures and host limitations. There is no
  separate lint command. Record hardware checks for behavior outside host tests.
- Validate documentation through paths, links, commands, and consistency.
- Identify commands that flash hardware. Run uploads only within the user's
  authorized device work; report unperformed hardware checks explicitly.

## Git branches and commits

- Use `<type>/<task>` for new task branches, with lowercase, hyphenated names,
  such as `fix/sleep-wake-state` or `docs/development-workflow`.
- Preserve an existing task checkout and unrelated local changes. Do not switch
  branches just to satisfy a convention when it risks mixing another task's work.
- Atomic commits are mandatory: each commit must contain one cohesive,
  independently reviewable task, including its relevant code, tests, and docs.
  Split unrelated changes into separate commits; one plan may require multiple
  commits.
- A request to implement a change authorizes its atomic task commits unless the
  user explicitly requests no commits. When a plan exists, follow its proposed
  commit boundaries. Do not leave completed tasks uncommitted merely because
  the implementation request did not repeat "commit". Push only when requested
  or already authorized.
- Finish the task's required checks and review, stage only its exact owned
  changes, and commit it before starting the next task. Preserve existing user
  edits and unrelated work.
- Keep task commits separate. Combine or squash them, or amend an earlier
  task's commit to absorb a later task, only when the user explicitly requests
  it. A request for one plan or one PR does not imply one commit.
- Use Conventional Commits: `<type>(<optional scope>): <imperative description>`.
  Keep descriptions lowercase with no trailing period. Use standard types
  such as `feat`, `fix`, `docs`, `refactor`, `test`, `build`, `ci`, and `chore`.
- Mark breaking changes with `!` and explain them in a `BREAKING CHANGE:` footer.
