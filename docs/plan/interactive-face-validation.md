# Record interactive-face device validation

**Status:** Verified (documentation checks complete).

> Historical record from before the controller extraction and native test suite.
> Test-suite and delivery statements below describe that earlier task. Current
> coverage and pending device checks are recorded in the
> [controller-test plan](controller-unit-tests.md).

**Outcome:** Record the user's CoreS3 validation and reconcile the milestone
status across the backlog, roadmap, and product brief.

## Baseline and scope

- Request: After reviewing the device checklist, the user confirmed, "I have
  validated it on the device," and requested a documentation update.
- Date: 2026-11-23 (Asia/Manila).
- Starting checkout: `main` at `33133046ccb33d918f1c9c945725052e7bb60c67`;
  no existing local edits.
- Scope: This validation record, backlog, roadmap, and product brief. Keep the
  historical migration record unchanged.
- Estimated total added and deleted lines: fewer than 200; documentation only.

## Requirements and decisions

- Accept the user's confirmation as device validation of the interactive-face
  milestone discussed in this chat: startup, touch cycling, inactivity sleep,
  previous-state restoration, and awake blinking with recovery after wake.
- Attribute hardware evidence to the user. No assistant-operated device check
  or firmware upload occurred. Exact flashed revision, individual observations,
  measured timings, and serial logs were not supplied.
- The reviewed source baseline includes the sleep/wake fix in `3313304`;
  replace the stale description of that work as uncommitted.
- Record the manual validation without claiming an automated suite exists.
  Future features still require selection; this update does not select one.

## Ordered tasks

### T1: Record validation and reconcile milestone status

**Scope and ownership:** This plan, `docs/feature-backlog.md`, `docs/roadmap.md`,
and `docs/product-brief.md`. One task keeps the evidence and status consistent.

**Dependencies:** The user's device-validation confirmation above.

**Acceptance:** Core face behavior is recorded as user-validated on CoreS3;
sleep/wake status reflects the committed fix; manual evidence and automated
coverage are distinct; later candidates remain unselected.

**Checks:** Inspect the complete diff; check local Markdown links, whitespace,
final newlines, and consistency. A failing firmware test would not validate
this documentation-only change. No build or upload is required.

**Authorized commit:** `docs: record interactive face device validation`

**Status and evidence:** Complete. Backlog, roadmap, and product brief now
record the completed milestone and link to this evidence record.

## Review and validation

- Review mode: Main-session self-review; no independent reviewer.
- Earlier source verification in this chat:
  `/Users/france/.platformio/penv/bin/pio run -e m5stack-cores3` passed (exit 0)
  at the starting revision. RAM: 27,708 bytes; flash: 514,475 bytes.
  Resolved Espressif 32 8.0.0 and M5Unified 0.3.0.
- Hardware: User-reported CoreS3 validation on 2026-11-23, covering the milestone
  summarized above. Detailed per-scenario logs were not supplied.
- No executable automated suite or separate lint command is configured.
- Documentation checks passed (exit 0): Python checked all four documents,
  eight local Markdown links, trailing whitespace, and final newlines.
- `git diff --check` passed (exit 0). Self-review covered the full tracked diff
  and this new file, including evidence attribution and remaining work.

## Handoff

- Interactive-face milestone recorded as complete based on the user's device
  confirmation. Automated regression coverage remains open; no later feature
  was selected.
- The user approved the documentation and authorized a local commit. T1's four
  documentation files are included in this commit; no push was requested.
