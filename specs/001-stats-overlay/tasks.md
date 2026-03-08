# Tasks: Real-Time Stats Overlay

**Input**: Design documents from `/specs/001-stats-overlay/`
**Prerequisites**: plan.md (required), spec.md (required for user stories), research.md, data-model.md, contracts/

**Tests**: Validation tasks are REQUIRED. Include automated tests when practical and add explicit manual verification tasks whenever hardware, host software, or platform-specific backends make full automation impractical.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

## Path Conventions

- Single project paths are used: `src/`, `docs/`, `tests/`, and `specs/`

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Create the test and build scaffolding needed for the feature branch.

- [X] T001 Enable CTest and add stats-overlay test entry points in `CMakeLists.txt` and `tests/CMakeLists.txt`
- [X] T002 [P] Create stats-overlay test harness files in `tests/stats_overlay/test_stats_overlay_config.c` and `tests/stats_overlay/test_stats_overlay_runtime.c`

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Build the shared preference, state, and drawing infrastructure that every story depends on.

**⚠️ CRITICAL**: No user story work can begin until this phase is complete

- [X] T003 Add stats preference fields and config parsing scaffolding in `src/config.h` and `src/config.c`
- [X] T004 [P] Create shared overlay state and formatter interfaces in `src/stats_overlay.h` and `src/stats_overlay.c`
- [X] T005 [P] Create in-tree bitmap font and draw helpers in `src/stats_overlay_font.h` and `src/stats_overlay_draw.c`
- [X] T006 Add session lifecycle hooks for overlay state in `src/main.c`, `src/platform.h`, and `src/platform.c`
- [X] T007 Add foundational config and formatter coverage in `tests/CMakeLists.txt`, `tests/stats_overlay/test_stats_overlay_config.c`, and `tests/stats_overlay/test_stats_overlay_runtime.c`
- [X] T008 Add backend capability and warning plumbing in `src/connection.h`, `src/connection.c`, and `src/stats_overlay.h`

**Checkpoint**: Shared stats-overlay infrastructure is ready; user story work can begin.

---

## Phase 3: User Story 1 - Monitor Session Health (Priority: P1) 🎯 MVP

**Goal**: Let users enable the overlay before starting a stream and see the live metric panel update during the session.

**Independent Test**: Enable stats before starting a stream, start the session, and verify that the overlay appears in the top-left within 5 seconds and updates the full metric set during playback on SDL, X11, and AML backends.

### Validation for User Story 1

- [ ] T009 [P] [US1] Add enabled-overlay automated coverage in `tests/stats_overlay/test_stats_overlay_runtime.c`
- [ ] T010 [US1] Record SDL, X11, and AML enabled-overlay manual checks in `specs/001-stats-overlay/quickstart.md`

### Implementation for User Story 1

- [ ] T011 [US1] Add `-stats` enable resolution and session-start locking in `src/config.c` and `src/main.c`
- [ ] T012 [US1] Populate live stream metrics in `src/connection.c`, `src/video/ffmpeg.c`, and `src/stats_overlay.c`
- [ ] T013 [P] [US1] Render the overlay in the SDL presenter in `src/sdl.c` and `src/sdl.h`
- [ ] T014 [P] [US1] Render the overlay in the X11/EGL presenter in `src/video/x11.c` and `src/video/egl.c`
- [ ] T015 [P] [US1] Render the overlay in the AML presenter in `src/video/aml.c` and `src/stats_overlay_draw.c`
- [ ] T016 [US1] Match overlay labels and line ordering to the contract in `src/stats_overlay.c`

**Checkpoint**: User Story 1 is complete when an enabled session shows the live stats overlay on supported SDL, X11-class, and AML backends.

---

## Phase 4: User Story 2 - Preserve the Existing Viewing Experience (Priority: P2)

**Goal**: Keep the overlay fully hidden by default and allow users to override enabled configs with an explicit disable path.

**Independent Test**: Start a stream with stats disabled by default and with `-nostats`, and verify that no overlay text, placeholder region, or draw work appears during the session.

### Validation for User Story 2

- [ ] T017 [P] [US2] Add disabled-path and precedence automated coverage in `tests/stats_overlay/test_stats_overlay_config.c`
- [ ] T018 [US2] Record manual no-overlay verification steps in `specs/001-stats-overlay/quickstart.md`

### Implementation for User Story 2

- [ ] T019 [US2] Add `-nostats` override and help output in `src/config.c`, `src/config.h`, and `src/main.c`
- [ ] T020 [US2] Skip overlay allocation and draw work for disabled sessions in `src/stats_overlay.c`, `src/sdl.c`, `src/video/x11.c`, and `src/video/aml.c`
- [ ] T021 [P] [US2] Document config and CLI disable behavior in `docs/README.pod` and `moonlight.conf`

**Checkpoint**: User Story 2 is complete when disabled sessions remain visually unchanged and CLI/config precedence produces the expected off state.

---

## Phase 5: User Story 3 - Remain Useful When Data Is Incomplete (Priority: P3)

**Goal**: Keep the overlay stable, readable, and non-disruptive when metrics are unavailable or the visible video area changes.

**Independent Test**: Run a session with missing metrics or display changes and verify that the overlay keeps the full line order, shows `Unavailable` where needed, stays anchored, and falls back gracefully on unsupported backends.

### Validation for User Story 3

- [ ] T022 [P] [US3] Add unavailable-metric and stable-line-order automated coverage in `tests/stats_overlay/test_stats_overlay_runtime.c`
- [ ] T023 [US3] Record manual unavailable-metric, resize, AML, and unsupported-backend checks in `specs/001-stats-overlay/quickstart.md`

### Implementation for User Story 3

- [ ] T024 [US3] Preserve fixed metric positions and `Unavailable` placeholders in `src/stats_overlay.c`
- [ ] T025 [P] [US3] Keep the overlay anchored during resize or rotation in `src/sdl.c`, `src/video/x11.c`, and `src/video/aml.c`
- [ ] T026 [P] [US3] Add unsupported-backend fallback notices in `src/platform.c`, `src/platform.h`, and `src/connection.c`
- [ ] T027 [US3] Enforce overlay footprint and refresh-cadence limits in `src/stats_overlay_draw.c` and `src/stats_overlay.c`

**Checkpoint**: User Story 3 is complete when the overlay remains stable under partial telemetry, resize/rotation, and unsupported-backend conditions.

---

## Phase 6: Polish & Cross-Cutting Concerns

**Purpose**: Finish documentation, validation capture, and full-branch cleanup.

- [ ] T028 [P] Update feature overview documentation in `README.md` and `specs/001-stats-overlay/contracts/stats-overlay.md`
- [ ] T029 [P] Capture full branch validation evidence in `specs/001-stats-overlay/quickstart.md` and `specs/001-stats-overlay/checklists/requirements.md`
- [ ] T030 Tune overlay performance after end-to-end validation in `src/stats_overlay.c`, `src/sdl.c`, `src/video/x11.c`, and `src/video/aml.c`
- [ ] T031 Verify build and test wiring in `CMakeLists.txt`, `tests/CMakeLists.txt`, and `AGENTS.md`

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies; start immediately.
- **Foundational (Phase 2)**: Depends on Setup; blocks all user story work.
- **User Story 1 (Phase 3)**: Depends on Foundational completion.
- **User Story 2 (Phase 4)**: Depends on Foundational completion.
- **User Story 3 (Phase 5)**: Depends on Foundational completion and the shared overlay path from User Story 1.
- **Polish (Phase 6)**: Depends on the stories you intend to ship being complete.

### User Story Dependencies

- **US1 (P1)**: MVP path; no dependency on other user stories.
- **US2 (P2)**: Independent from US1 after Foundational, but must preserve the same config and renderer contracts, including AML disable behavior.
- **US3 (P3)**: Extends the active overlay path from US1 with resilience and fallback behavior across SDL, X11, AML, and remaining unsupported backends.

### Within Each User Story

- Automated validation tasks should be written before the implementation tasks they cover.
- Manual validation tasks must be completed before the story is considered done.
- Shared state and contract wiring must exist before backend rendering tasks start.
- Backend rendering tasks can proceed in parallel once the shared snapshot and formatter behavior are in place, including AML-specific renderer work.

---

## Parallel Opportunities

- **Setup**: `T002` can run while `T001` is being prepared if the `tests/` layout is agreed.
- **Foundational**: `T004` and `T005` can run in parallel after `T003`.
- **US1**: `T009` and `T010` can run together; `T013`, `T014`, and `T015` can run in parallel after `T011` and `T012`.
- **US2**: `T017` and `T018` can run together; `T021` can run in parallel with `T020` after `T019`.
- **US3**: `T022` and `T023` can run together; `T025` and `T026` can run in parallel after `T024`.
- **Polish**: `T028` and `T029` can run in parallel.

---

## Parallel Example: User Story 1

```bash
# Validation prep
Task: "Add enabled-overlay automated coverage in tests/stats_overlay/test_stats_overlay_runtime.c"
Task: "Record SDL, X11, and AML enabled-overlay manual checks in specs/001-stats-overlay/quickstart.md"

# Backend presentation work
Task: "Render the overlay in the SDL presenter in src/sdl.c and src/sdl.h"
Task: "Render the overlay in the X11/EGL presenter in src/video/x11.c and src/video/egl.c"
Task: "Render the overlay in the AML presenter in src/video/aml.c and src/stats_overlay_draw.c"
```

## Parallel Example: User Story 2

```bash
# Validation prep
Task: "Add disabled-path and precedence automated coverage in tests/stats_overlay/test_stats_overlay_config.c"
Task: "Record manual no-overlay verification steps in specs/001-stats-overlay/quickstart.md"

# Post-override follow-up
Task: "Skip overlay allocation and draw work for disabled sessions in src/stats_overlay.c, src/sdl.c, src/video/x11.c, and src/video/aml.c"
Task: "Document config and CLI disable behavior in docs/README.pod and moonlight.conf"
```

## Parallel Example: User Story 3

```bash
# Validation prep
Task: "Add unavailable-metric and stable-line-order automated coverage in tests/stats_overlay/test_stats_overlay_runtime.c"
Task: "Record manual unavailable-metric, resize, AML, and unsupported-backend checks in specs/001-stats-overlay/quickstart.md"

# Resilience follow-up
Task: "Keep the overlay anchored during resize or rotation in src/sdl.c, src/video/x11.c, and src/video/aml.c"
Task: "Add unsupported-backend fallback notices in src/platform.c, src/platform.h, and src/connection.c"
```

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Phase 1: Setup.
2. Complete Phase 2: Foundational.
3. Complete Phase 3: User Story 1.
4. Stop and validate the enabled overlay path on supported backends before expanding scope.

### Incremental Delivery

1. Finish Setup + Foundational to establish config, shared state, drawing helpers, and test harnesses.
2. Deliver US1 as the first usable increment.
3. Deliver US2 to lock down the default-hidden and explicit-disable experience.
4. Deliver US3 to harden unavailable-metric, resize, and unsupported-backend behavior.
5. Finish with Phase 6 polish and full validation capture.

### Parallel Team Strategy

1. One developer can own shared state and config wiring while another prepares tests during Phases 1 and 2.
2. After Foundational completes, US1 and US2 can proceed in parallel.
3. US3 should begin once the active overlay path from US1 is stable.

---

## Notes

- Every task follows the required checklist format with an ID, optional `[P]`, optional `[US#]`, and explicit file paths.
- The story phases are structured so each user story can be validated independently.
- Manual validation remains mandatory because backend coverage depends on host and hardware availability.
