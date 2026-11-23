# Reconcile device validation and controller test status

**Status:** Complete; documentation checks and self-review passed.

**Outcome:** The current branch's planning docs distinguish the previously
validated interactive face, passing native controller tests, and pending device
regression checks for the controller refactor.

## Baseline and scope

- Request: Update documentation on the controller-test branch as well.
- Date: 2026-11-23 (Asia/Manila).
- Branch: `refactor/controller-unit-tests` at
  `7e13c46f2a3e9e5d67356d2caae465f3f1ca9123`; clean checkout.
- Scope: Backlog, roadmap, product brief, restored historical device-validation
  record, a follow-up note in the controller-test plan, and this plan.
- Estimated total added/deleted lines: 200-300; documentation only.
- No firmware, test, configuration, or new product scope changes.

## Requirements and decisions

- Restore the user-reported CoreS3 evidence from local commit `3cecaca`, with a
  historical note. Preserve its original observations and limitations.
- Record the original interactive-face milestone as user-validated; the exact
  flashed revision and detailed scenario logs were not supplied.
- Record 15 passing native tests and the successful firmware build from the
  controller-test plan. Device touch/display/Serial/blink checks after that
  refactor remain pending; renderer/blink logic has no automated coverage.
- Keep all six later candidates unselected. No new hardware validation is claimed.

## Ordered tasks

### T1: Reconcile planning documentation

**Scope and ownership:** The six documentation paths listed above. One task
keeps cross-document status and evidence consistent.

**Dependencies:** Existing device-validation record and controller-test results.

**Acceptance:** Local links resolve, stale migration-era status is corrected,
and historical device evidence is distinct from current host/device coverage.

**Checks:** `git diff --check`, local Markdown link/anchor and whitespace checks,
complete tracked/untracked diff review, and unchanged production/test/configuration
content. Behavioral RED, another firmware build, and device upload do not apply
to prose changes. Review mode is main-session self-review.

**Delivery:** Incorporate this documentation task into the existing squashed
commit and PR, after checking that the remote branch still matches the baseline.
Use a lease for the authorized branch update; preserve local `main`.

**Status and evidence:** Complete. The backlog, roadmap, and brief record the
earlier user-validated milestone, passing host tests, and current device gap.
The restored evidence record is unchanged apart from its historical note.

## Review and validation

- `git diff --check`: exit 0.
- `python3 /tmp/reconcile-validation-status-checks.py`: exit 0; six docs passed
  whitespace/final-newline checks, 14 local links/anchors resolved, historical
  record fidelity and exact documentation-only scope were verified. Production,
  tests, configuration, and local `main` remain unchanged.
- Main-session self-review covered the complete tracked/untracked diff and
  evidence attribution. This documentation update was not independently reviewed.
- Existing native test/build results are reused; no new tests, build, device
  checks, or uploads were needed or performed for this prose change.

## Handoff

- Documentation task included in the existing squashed implementation commit;
  delivery remains on `refactor/controller-unit-tests` in
  [PR #1](https://github.com/Jefray-alt/france-stackchan-cfw/pull/1).
- Earlier device validation is restored; checks after the refactor remain
  pending, and all six later feature candidates remain unselected.
