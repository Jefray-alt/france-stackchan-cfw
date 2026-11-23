# Make controller behavior testable on the host

**Status:** Complete; both tasks passed independent review.

**Outcome:** Run deterministic controller regression tests on a Mac without a
CoreS3, using the same state logic as the firmware.

## Baseline and scope

- Request: One plan includes the focused refactor and unit tests. Preserve atomic
  commits; the user clarified that one plan does not mean one commit.
- Date: 2026-11-23 (Asia/Manila).
- Starting checkout: `main` at `3cecacaf89e57bb874abaa801a2603f0b96c421a`;
  the existing untracked file was this requested plan; no other local edits.
- Scope: Controller state/timing extraction, native PlatformIO/Unity tests, and
  the documentation needed to run and maintain them.
- Defer renderer/blink extraction, new features, CI, and device test uploads.
- Estimated total added and deleted lines: 450-750 across code, tests, and docs.
- Created `refactor/controller-unit-tests` at the starting revision; at routing,
  the only untracked file was this plan. Keep it with the task throughout implementation.
- Delivery update: the user requested one squashed commit and a merge request.
  This supersedes the original two-commit delivery plan and authorizes committing
  the reviewed T1/T2 changes together, pushing the task branch, and opening a
  GitHub pull request against `main`.
- Remote `main` is `3313304`; the original local baseline also contains the
  separate, unpushed device-validation documentation commit `3cecaca`. Delivery
  rebases only the controller-test work onto remote `main`, preserving that
  earlier commit on local `main` and the reviewed snapshot branch
  `refactor/controller-unit-tests-reviewed`. Production, tests, and configuration
  are identical to the reviewed candidate. Backlog/roadmap suite corrections
  are adapted to the remote documentation baseline and self-reviewed.

## Requirements and decisions

- Keep `RobotController` responsible for interaction orchestration. Extract only
  its state decisions into a small owned C++ helper; keep touch reads, rendering,
  serial logging, and `millis()` access at the hardware boundary.
- Use explicit press events and `uint32_t` timestamps. Test elapsed time through
  controlled inputs and unsigned subtraction, including 32-bit rollover.
- Firmware and tests must compile the same production helper. Avoid copying
  transition logic into tests, mocking all of M5Unified, or adding a general
  dependency-injection framework.
- Preserve awake state cycling, the 30-second inactivity threshold, press
  precedence over timeout, and restoration of the previous awake state.
- Establish initialization and same-state assignment behavior from the current
  implementation before extraction. Report any discovered behavior defect
  separately rather than silently changing it during the refactor.
- Keep renderer animation behavior and public controller entry points compatible.
- Add a separate native environment with Unity. Compile only hardware-independent
  production sources there; exclude Arduino `main.cpp`, M5Unified, and rendering.
  Keep normal firmware builds targeting `m5stack-cores3` by default.
- Keep the original T1/T2 implementation and review boundaries. Deliver both
  reviewed tasks in one squashed commit per the user's latest request.
- Each task includes its relevant documentation. Commit and push only the
  reviewed task changes. No firmware upload is part of this plan.

### Established controller behavior before extraction

- Construction starts in `IDLE`, remembers `IDLE` as the last awake state, and
  starts inactivity at timestamp zero.
- `begin()` initializes the renderer and resets only the inactivity timestamp
  to zero. It retains the current and last awake states. Consequently, startup
  uptime counts toward inactivity, and calling `begin()` after 30 seconds can
  make the next unpressed update sleep immediately. Preserve this initialization
  caveat; changing it would be a separate behavior decision.
- A same-state `setState()` returns without refreshing inactivity, notifying
  the renderer, or logging. Actual transitions record their timestamp; only
  awake transitions replace the remembered awake state.
- An unpressed update sleeps at elapsed time `>= 30000`; a pressed update
  cycles awake state or restores the last awake state, taking precedence over
  timeout. The temporary zero assigned before a press transition is overwritten
  by that transition for every defined state.

## Ordered tasks

### T1: Extract controller decisions with runnable native coverage

**Scope and ownership:** `RobotController` header/source, a small state helper
under `include/` and `src/`, `platformio.ini`, a controller test suite under
`test/`, this plan, and minimal README/test/workflow documentation updates.
Replace obsolete statements that no automated suite exists where applicable.
Following independent review, main authorized a narrow scope extension to
`docs/feature-backlog.md` and `docs/roadmap.md`: correct stale test-suite wording
without changing product capability or completion status.

**Dependencies:** Configured implementer, developer, and independent reviewer
available at runtime; PlatformIO and a host C++ compiler.

**Acceptance:** Native tests exercise startup, the full awake cycle, inactivity
sleep, and wake into each previous awake state. Hardware integration uses the
tested helper and preserves transition logging and renderer updates. Firmware
compiles. A failure in a core transition is detected by a meaningful test.

**Checks:** Run `~/.platformio/penv/bin/pio test -e native` and
`~/.platformio/penv/bin/pio run -e m5stack-cores3`; record exact outcomes.
Demonstrate that a deliberate transition defect makes a focused test fail, then
restore production code and verify passing tests. Inspect the complete diff,
new files, documentation links, and `git diff --check`. Neither command uploads.

**Proposed commit:** `refactor(controller): isolate state logic with native tests`

**Status and evidence:** Complete; independent review passed in round 2 of 3.
At T1 review, changes remained uncommitted. `RobotStateMachine` owns decisions with explicit
press events and 32-bit timestamps; `RobotController` retains touch/millis
access, renderer updates, and
Serial output. Its public entry points are unchanged. The helper reports actual
transitions, which alone trigger renderer notifications and logging.

- Test-first scaffold: `~/.platformio/penv/bin/pio test -e native` exited 1 with
  the expected missing `RobotStateMachine.h`, before the helper existed. This
  established wiring, not behavioral RED evidence.
- Behavioral RED: temporarily changed the helper's first press from
  `IDLE -> LISTENING` to `IDLE -> THINKING`. The same command exited 1;
  `test_presses_cycle_through_all_awake_states` failed with `Expected 1 Was 2`.
  Nine cases passed; PlatformIO also reported the failed process as SIGHUP.
- Restored GREEN: `~/.platformio/penv/bin/pio test -e native` exited 0 with
  10/10 Unity cases passing after restoring the transition and integrating the
  controller. Coverage includes startup, the four-state cycle, inactivity sleep,
  wake to each awake state, same-state assignments, and preserved `begin()`.
- `~/.platformio/penv/bin/pio run -e m5stack-cores3` exited 0. RAM: 27,708 /
  327,680 bytes (8.5%); flash: 514,575 / 6,553,600 bytes (7.9%). Two upstream
  M5GFX unused-variable warnings appeared; no project warnings were reported.
- `git diff --check` exited 0. `python3
  /tmp/controller-unit-tests-t1-checks.py` exited 0: all 11 tracked/untracked
  candidate files passed whitespace/final-newline checks, 24 local documentation
  paths/anchors resolved, native production objects contained only the helper,
  and renderer/main/shared enum remained unchanged. Documentation/configuration
  were validated by these checks and the runnable commands; a failing behavior
  test is not applicable to prose/link changes.
- Frozen boundary: `T1-controller-unit-tests`, full tracked/untracked diff saved
  as `/tmp/controller-unit-tests-t1.patch`. This includes the pre-existing
  requested plan in full. No files staged or committed; no device upload.
- Independent review requested changes only for stale no-suite statements in
  the backlog and roadmap; no code defects were reported. Corrected those
  statements and retained product completion status. Review independently
  confirmed native 10/10 and firmware compilation. This correction changes only
  documentation, so behavioral RED and another firmware build are inapplicable.
  `git diff --check` and `python3 /tmp/controller-unit-tests-t1-checks.py` exited
  0: 13 candidate files passed whitespace checks and 33 local paths/anchors
  resolved; production/configuration/test content is unchanged from the snapshot.
  The updated frozen diff is `/tmp/controller-unit-tests-t1.v2.patch`; the
  original snapshot is retained.
- Independent round 2 returned `PASS`; no remaining findings. The reviewer
  verified all 13 frozen paths, unchanged production/configuration/tests,
  whitespace, and 33 local paths/anchors. No further build was needed for prose.

**Worker selection:** T1 developer: `gpt-6.1-sol` / `xhigh`; the extraction needs
careful preservation of initialization, same-state assignments, timing, and
hardware orchestration. The implementer confirmed the branch, revision, and
sole pre-existing untracked plan before routing this task.
T1 independent reviewer selection: `gpt-6.1-sol` / `xhigh`, chosen separately to
compare the original timing, initialization, and same-state semantics with the
helper, firmware boundary, and native source filtering.

### T2: Cover inactivity boundaries and repeated transitions

**Scope and ownership:** Controller test suite, relevant fixture/coverage
documentation, and this plan. Production changes require an identified defect
and an updated scope decision; they are not implied by adding tests.

**Dependencies:** T1 completed and independently reviewed.

**Acceptance:** Tests cover just before and at the inactivity threshold,
interaction refreshing the timer, a press at the timeout boundary, no additional
transition without another press event, repeated sleep/wake cycles from every
awake state, and inactivity across 32-bit timer rollover. Tests use explicit
timestamps, no real sleeps, and observable state assertions.

**Checks:** Run the native suite and inspect its individual cases. Demonstrate
that a deliberate timeout-boundary defect is caught, restore it, and rerun.
Run `git diff --check`; verify test instructions and scope. Rebuild firmware if
production/configuration changes or unresolved integration concerns warrant it.
Record physical CoreS3 touch/sleep/wake/blink checks separately; host tests do
not establish display appearance, touch detection, or blink recovery.

**Proposed commit:** `test(controller): cover timing boundaries and wake cycles`

**Status and evidence:** Complete; independent review passed in round 1 of 3.
Baseline: `refactor/controller-unit-tests` at
`3cecacaf89e57bb874abaa801a2603f0b96c421a`, with the reviewed uncommitted T1
candidate retained. T2 changes only the test suite, test README, and this plan.
No separate behavior defect was found; production/configuration are unchanged.

- Baseline `~/.platformio/penv/bin/pio test -e native` exited 0 (10/10).
  Extended coverage adds explicit 29999/30000 ms assertions, press refresh and
  timeout precedence, no extra transition without another press, and rollover
  from `0xFFFFF000` through `0xFFFFFFFF` to 25903/25904. Each awake-state wake
  test now runs three sleep/wake cycles and checks the refreshed wake timer.
- Sensitivity RED: temporarily changed only `>= 30000` to `> 30000` in the
  production helper. The native command exited 1, with ten named assertion
  failures: inactivity, threshold, refresh, timeout precedence, rollover, all
  four wake cases, and same-state assignment. PlatformIO also reported SIGBUS
  after those assertions; the assertion failures establish mutation detection.
  Output: `/tmp/controller-unit-tests-t2-red.log`.
- Restored GREEN: the same native command exited 0 with all 15 cases passing.
  The helper was restored byte-for-byte to T1 SHA256
  `2d8388b31be70546a83bf14d6ffc973d65ce071757014c232f0bcf72fc42133b`.
- `git diff --check` and `python3 /tmp/controller-unit-tests-t1-checks.py`
  exited 0: 13 candidate paths passed whitespace/final-newline checks, 33 local
  documentation links resolved, and native objects still contain only the
  helper. T1 production/configuration hashes remain unchanged. Firmware was
  not rebuilt: T2 has no lasting production/configuration changes or unresolved
  integration concern; T1's independently verified build remains applicable.
- Frozen T2-only diff: `/tmp/controller-unit-tests-t2.patch`; complete candidate
  diff: `/tmp/controller-unit-tests-t2-candidate.patch`. No staging, commit,
  push, or upload. CoreS3 touch/display/Serial/blink checks remain unavailable.
- Independent review returned `PASS` without findings. The reviewer reran all
  15 native cases (exit 0), checked exact rollover arithmetic, mutation assertion
  output, frozen hashes, owned scope, whitespace, and 33 local paths/anchors.

**Worker selection:** T2 developer: `gpt-6.1-sol` / `medium`; this is a clear,
bounded extension of deterministic tests against the reviewed helper. T2
independent reviewer: `gpt-6.1-sol` / `xhigh`, chosen separately to assess timing
boundaries, rollover arithmetic, and whether assertions catch meaningful defects.

## Review and validation

- T1 passed independent review in round 2 after the documentation correction.
  Native tests and firmware compilation passed. T2 passed independent review
  in round 1, including an independent 15/15 native run.
- Environment: PlatformIO Core 6.2.0, native platform 1.2.1, Unity 2.6.1, Apple
  clang 21.0.0 on arm64 macOS; firmware Espressif32 8.0.0, Arduino 3.3.12,
  M5Unified 0.3.0. Resolved versions are recorded here, not newly pinned.
- Live agent definitions expose `implementer`, `developer`, and `code_reviewer`,
  matching the inspected `.codex/agents/` files. The implementer is pinned to
  `gpt-6.1-sol` with `xhigh` reasoning. No role configuration changes are needed.
- Follow the configured implementer routing and fresh
  developer/reviewer pair per task, with the three-round review limit.
- No physical CoreS3 was used or made available to this task. Touch event/hold
  detection, displayed states, Serial output, and repeated sleep/wake blink
  recovery remain unverified on this candidate. Native tests establish state
  decisions only; earlier device validation does not validate this refactor.
- Existing initialization caveat is preserved and documented above. No separate
  controller behavior defect was identified or repaired during T1.

## Handoff

- T1 and T2 passed independent review. Their task snapshots preserve the original
  review boundaries. The user's delivery request combines them in one commit:
  `refactor(controller): isolate state logic with native regression tests`.
- Delivery branch: `refactor/controller-unit-tests`; target: `main`. Push and
  pull-request creation are authorized. No device upload was performed.
- Delivery-only plan updates were self-reviewed; the independently reviewed
  implementation, tests, and configuration remain unchanged.
- Delivery checks: reviewed production/test/configuration hashes match; all
  task paths pass whitespace checks, and local documentation links resolve
  against the remote baseline. The separate device-validation commit is excluded.
- Final whitespace, all 13 candidate paths, 33 documentation paths/anchors,
  native source isolation, and unchanged renderer/main/shared enum checks passed.
- No product capability has changed, so no roadmap completion update is needed.
- Documentation follow-up: the user subsequently requested restoring the earlier
  device-validation evidence on this branch. See the
  [reconciliation plan](reconcile-validation-status.md). This supersedes the
  delivery exclusion of that documentation above; original validation evidence
  remains historical and device checks after the refactor remain pending.
