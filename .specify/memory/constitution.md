# Moonlight Embedded Constitution

## Core Principles

### I. CLI Compatibility
The documented CLI and config contract MUST remain stable unless a breaking change is explicitly approved. Changes to pairing, discovery, streaming, quitting, mapping, or config parsing MUST document user impact and update docs in the same change.

### II. Platform Isolation
Platform-specific audio, video, and input code MUST stay within existing backend boundaries or dedicated CMake-built libraries. Changes MUST preserve feature gates and MUST NOT break unrelated targets through cross-backend assumptions.

### III. Streaming Performance
Streaming-path changes MUST favor low latency, bounded memory use, and graceful degradation. Changes to packet flow, decode, render, input, or AV sync MUST state expected performance impact and how regressions will be detected.

### IV. Validation
Every change MUST include validation proportional to risk. Shared protocol, connection, config, and backend-selection changes MUST add automated coverage where practical and manual validation where hardware or environment constraints require it.

### V. Scoped Changes
Pull requests MUST stay scoped, avoid incidental churn, and update affected docs, config, or contributor guidance in the same patch. Specs, plans, and tasks MUST tie each change to a concrete user scenario and touched files.

## Engineering Constraints

- Production code MUST fit the existing C99 + CMake toolchain unless this constitution is amended.
- Hardware- or environment-specific dependencies MUST remain optional and honor existing CMake feature flags.
- User-visible behavior changes MUST keep `README.md`, `docs/README.pod`, and shipped config/install artifacts aligned.
- New third-party code MUST include maintenance and licensing rationale.

## Delivery Workflow

1. Specs MUST identify the affected user journey, CLI/config surface, target platforms, and failure modes.
2. Plans MUST identify impacted backends, compatibility risks, validation, and required doc updates.
3. Tasks MUST include validation work; if automation is impractical, they MUST name the required manual hardware or host verification.
4. Review MUST reject changes without compatibility, validation, or doc-sync evidence.

## Governance

This constitution overrides conflicting repository practices. Amendments MUST include documented rationale and synced templates or guidance. Compliance review is mandatory for specs, plans, tasks, and pull requests.

- MAJOR: remove or redefine a principle, or introduce a governance change that invalidates existing workflows.
- MINOR: add a principle or materially expand mandatory guidance.
- PATCH: clarify wording, formatting, or other non-semantic guidance.
- Reviewers MUST verify the Constitution Check and validation trace before approval.
- Authors MUST update affected docs and templates in the same change.
- Exceptions MUST be documented in Complexity Tracking and approved in review.

**Version**: 1.0.2 | **Ratified**: 2026-03-08 | **Last Amended**: 2026-03-08
