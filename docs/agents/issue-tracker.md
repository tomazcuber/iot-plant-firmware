# Issue tracker: GitHub Issues

Firmware issues live as GitHub Issues on `tomazcuber/iot-plant-firmware`, managed with the `gh` CLI.

The only exception is the MQTT-backend plan, which lives as local markdown under `.scratch/mqtt-backend/` (its `MAP.md` and tickets). Don't add new features to `.scratch/`.

## Conventions

- One issue per vertical slice. The body has the sections `## Parent` (spec link, ADRs, parent issue), `## What to build`, `## Acceptance criteria` (split into `### Agent-verifiable` and `### Bench (human)` when a bench step exists) and `## Blocked by` (a list of `#N`).
- Triage state is a GitHub label (see `triage-labels.md` for the role strings).
- A bench checklist is ticked by the user; an issue with one is closed only after the bench passes.
- PRs close the slice issues they implement (`Closes #N`) and only reference umbrella issues (`Refs #N`).

## When a skill says "publish to the issue tracker"

`gh issue create --title "<title>" --body-file <file> --label <triage-label>`

## When a skill says "fetch the relevant ticket"

`gh issue view <N>`. The user will normally pass the issue number directly.
