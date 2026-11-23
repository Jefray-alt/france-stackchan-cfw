# Adapt the development workflow

**Status:** Complete. The Markdown workflow and three source agent roles are
adapted and validated; the user authorized local commits for the workflow files.

**Outcome:** Bring the planning and review workflow from `Jefray-alt/doc-ai-node`
into this firmware repository, with commands and conventions that match the
current PlatformIO project.

## Baseline and scope

- Source: `Jefray-alt/doc-ai-node` at commit
  `dfb48852723950fe559c61e93e4267ab87217c54`.
- Destination: the root of `france-stackchan-cfw`, on `main` at
  `fda5b16` when this work started. The existing checkout was retained for this
  documentation migration.
- Existing user change: `src/RobotController.cpp`; preserve it byte for byte.
- Adapt the root README, AGENTS instructions, workflow, product brief, backlog,
  and roadmap. Add a reusable plan template.
- Replace web application commands and conventions with PlatformIO, Arduino,
  C++, M5Unified, and CoreS3 guidance.
- Record product direction as inferred from the existing firmware, with future
  capabilities explicitly proposed rather than approved.
- Do not import the source application's backend/frontend READMEs or completed
  implementation plans. Their commands, history, and completion claims do not
  apply here. The follow-up explicitly includes its three custom agent files;
  the PR-validation skill remains outside scope.

Expected size: fewer than 1,000 added and deleted lines across this change.

## Decisions

- Preserve the source's focused plans, ordered tasks, review criteria,
  Conventional Commits, and three-round limit for delegated reviews.
- Copy `implementer`, `developer`, and `code_reviewer` into `.codex/agents/`.
  Restore default code-change routing through the implementer, using fresh
  developer/reviewer pairs. Keep planning and this configuration bootstrap in
  the main session; do not claim that installing files loads live roles.
- Preserve the source implementer's `gpt-6.1-sol` / `xhigh` configuration and
  unpinned worker settings. Match worker selection instructions to the workflow.
- Allow work alongside unrelated local edits when files can be isolated safely.
  Stop for overlapping changes or conflicts rather than requiring a clean tree
  for every request.
- Respect the current checkout and existing branch conventions. Use task
  branches for new implementation work when practical; do not require an extra
  approval for reversible branch creation. The user authorized local commits
  for this work; no push was requested.
- Firmware behavior changes need build evidence and relevant behavioral checks.
  Documentation uses link, path, command, and consistency checks; an artificial
  failing test would not validate this migration.

## Ordered tasks

### T1: Establish firmware planning documents

**Scope:** README, product brief, feature backlog, roadmap, this plan, and the
plan template. No dependency.

**Acceptance:** All commands target `m5stack-cores3`; repository paths exist;
implemented behavior and proposed work are distinguished; no imported web
application completion claims remain.

**Checks:** Compare the documents with `platformio.ini`, `src/`, and `include/`;
check relative Markdown links and command availability.

**Commit:** `docs: establish firmware planning documents`

Include README, brief, backlog, roadmap, and the reusable plan template. The
active migration plan will be included with T3's final validation record.

- [x] Write and check the adapted planning documents.

### T2: Establish firmware implementation and review rules

**Scope:** AGENTS instructions and shared development workflow. Depends on T1.

**Acceptance:** Instructions link to the planning documents, preserve scoped
tasks and evidence-based reviews, describe build and hardware validation, and
describe the installed roles without claiming live activation. Existing user
changes remain intact.

**Checks:** Review instruction consistency, relative links, whitespace, and Git
status. Compare the existing modified source with its recorded baseline.

**Commit:** `docs: adapt the development and review workflow`

- [x] Write and check the adapted workflow.

### T3: Copy the agent configuration and finalize the migration

**Scope:** The three `.codex/agents/*.toml` files and this migration plan.
Depends on the planning and workflow documentation. Reconcile README and
workflow references before committing T1 and T2.

**Acceptance:** Preserve source role names and model settings; align task
ownership, commit authorization, firmware checks, and review rounds with the
local workflow. Reviewer instructions forbid edits, commits, nested delegation,
and hardware uploads. Preserve the existing firmware diff and index ownership.

**Checks:** Parse all agent files with Python `tomllib`, verify required fields,
source model settings, instruction links, Markdown consistency, and whitespace.
Inspect staged paths and each resulting commit. A failing firmware test would
not validate these configuration changes; the earlier passing build remains
valid because no firmware or build settings are changed.

**Commit:** `chore: add the development workflow agents`

- [x] Add and validate the three agent files.
- [x] Package T1, T2, and T3 as separate exact-path commits, excluding
  `src/RobotController.cpp`.

## Validation and handoff

- Self-review completed against the source workflow and local firmware; no
  independent reviewer was requested or used.
- All eight new Markdown files passed local relative-link and heading-anchor
  checks, code-fence balance, trailing-whitespace, and final-newline checks.
- PlatformIO Core 6.2.0 resolved the documented environment through
  `pio project config --json-output`. CLI help confirmed the build, upload,
  monitor, and future test options used in the guidance.
- `/Users/france/.platformio/penv/bin/pio run -e m5stack-cores3` passed with
  exit 0. The build resolved Espressif 32 8.0.0 and M5Unified 0.3.0; reported
  RAM usage was 27,708 bytes and flash usage was 514,487 bytes. This is build
  evidence for the existing local firmware, not a behavioral test.
- `git diff --check` passed. Only the original `src/RobotController.cpp`
  modification remains among tracked changes. Its SHA-256 stayed
  `9478b58bbd49d377a04490110f22cf0b54eb5e5a5230633204c9e3a21ad5a830`.
- No firmware upload or hardware interaction was performed. Touch, rendering,
  inactivity, wake, and blink checks remain pending.
- No product feature was marked complete by this migration. Commit authorization
  was received after the initial Markdown adaptation. The active checkout stays
  on `main`; no remote changes or hardware uploads are included.

### Agent configuration validation

- Source `main` still resolves to the same recorded commit. Copied all three
  agent files from that revision and adapted their instructions to the local
  workflow, firmware checks, and user-authorized commit policy.
- Python 3.11.7 `tomllib` parsed all three TOML files. Required string fields,
  role names, implementer model/effort, and unset worker model/effort passed
  checks. Reviewer and developer mutation/delegation restrictions were checked.
- All eight Markdown files and 26 local links passed link and heading checks.
  All eleven Markdown/TOML files passed whitespace and final-newline checks.
- Self-review checked the complete candidate and its consistency with the
  source roles. This bootstrap did not spawn workers or exercise the installed
  custom roles; live role loading and model execution remain unverified.
- Firmware and build settings were unchanged, so the prior successful build
  was not repeated. The existing source checksum and empty Git index were
  rechecked before staging.

### Task-to-commit mapping

| Task | Commit | Included files |
|---|---|---|
| T1 | `fd56a6b` — `docs: establish firmware planning documents` | README, product brief, backlog, roadmap, and plan template |
| T2 | `f4e5c1a` — `docs: adapt the development and review workflow` | AGENTS instructions and shared workflow |
| T3 | This commit — `chore: add the development workflow agents` | Three agent TOML files and this final migration record |

The only firmware modification belongs to the existing user work in
`src/RobotController.cpp`; it is preserved and excluded from all three commits.
Custom-role activation and physical device checks remain unverified.
