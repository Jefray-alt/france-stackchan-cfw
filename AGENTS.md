# Project planning and development rules

## Planning documents

- [Product brief](docs/product-brief.md): goals, first milestone, and assumptions.
- [Feature backlog](docs/feature-backlog.md): capabilities, candidates, and priorities.
- [Roadmap](docs/roadmap.md): implementation status and validation gaps.
- `docs/plan/<task>.md`: current requirements, decisions, ordered tasks, and evidence.

Before implementation, create or update a focused Markdown plan under
`docs/plan/`, using a descriptive lowercase, hyphenated filename. Start with
[the plan template](docs/plan/template.md). Update the same file as scope,
decisions, and task status change; do not duplicate detailed requirements in
the roadmap or backlog. For a small documentation change, keep the plan brief.

## Workflow

1. Understand the request and inspect the relevant source and checkout state.
2. For new product direction, clarify the brief and priorities before coding.
3. Detail only the current requested outcome and its acceptance criteria.
4. Publish an ordered task and proposed commit plan before implementation.
5. Implement and review one cohesive task at a time, with its tests and docs.
6. Record validation and limitations; update the roadmap only when a product
   capability is actually completed.

Follow [the development workflow](docs/agent-workflow.md) for startup checks,
review, and handoff. For code-change requests, the main session routes work to
the configured `implementer` by default, passing user requirements and decisions.
Planning, exploration, and small documentation/configuration tasks can remain
in the main session. The implementer uses fresh `developer` and independent
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
- Commit or push only when requested or already authorized. For authorized
  commits, stage exact task paths and keep cohesive task commits separate.
- Use Conventional Commits: `<type>(<optional scope>): <imperative description>`.
  Keep descriptions lowercase with no trailing period. Use standard types
  such as `feat`, `fix`, `docs`, `refactor`, `test`, `build`, `ci`, and `chore`.
- Mark breaking changes with `!` and explain them in a `BREAKING CHANGE:` footer.
