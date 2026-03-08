# Implementation Plan: Real-Time Stats Overlay

**Branch**: `001-stats-overlay` | **Date**: 2026-03-08 | **Spec**: [spec.md](./spec.md)  
**Input**: Feature specification from `/specs/001-stats-overlay/spec.md`

## Summary

Add an optional session-start stats overlay by extending config and CLI parsing with a `stats` toggle, collecting live streaming telemetry into a shared session snapshot, and rendering a fixed multi-line overlay through backend-specific presentation code. The design preserves existing defaults and precedence rules, confines renderer differences to backend files, and documents graceful fallback on backends that cannot safely composite text.

## Technical Context

**Language/Version**: C99, CMake 3.6  
**Primary Dependencies**: libgamestream client layer, Moonlight Common C / Limelight callbacks, FFmpeg-backed decode paths, SDL2, X11/EGL/VAAPI/VDPAU, optional embedded video backends (PI/MMAL/IMX/AML/RK)  
**Storage**: In-memory session state plus existing config files (`moonlight.conf`, host configs, user config paths)  
**Testing**: No existing automated test suite; feature validation will combine build/config smoke coverage with targeted manual streaming sessions on supported backends  
**Target Platform**: Embedded Linux and Linux desktop backends supported by Moonlight Embedded  
**Project Type**: CLI streaming client with platform-specific audio/video/input backends  
**Performance Goals**: Preserve 60 FPS streaming behavior, surface overlay values within 2 seconds, and keep overlay-induced latency or frame-rate regression under 2%  
**Constraints**: Backward-compatible CLI/config contract, no new mandatory runtime dependency for text rendering, session setting locked at stream start, bounded memory use, graceful fallback on unsupported backends  
**Scale/Scope**: One optional stats feature spanning config parsing, shared streaming telemetry, selected video presenters, and synced user documentation for a single CLI application

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

- **CLI and config compatibility**: Introduce `-stats`, `-nostats`, and `stats = true|false` without changing existing commands or defaults. This is backward compatible because current behavior remains hidden by default. Update `src/main.c` help text, `docs/README.pod`, `moonlight.conf`, and `README.md`.
- **Platform isolation**: Keep stats collection and formatting in shared `src/` code, and keep drawing/compositing inside backend-specific presenters such as `src/sdl.c`, `src/video/x11.c`, and shared X11/EGL helpers. Embedded backends in `src/video/` stay isolated; if a backend lacks a safe text-compositing path, it must preserve streaming and expose a documented unsupported or no-op path rather than importing cross-backend assumptions.
- **Streaming performance**: Overlay generation must use cached formatted text and a bounded refresh cadence rather than per-packet string work. Regression signals are achieved FPS, queue delay, decode time, render time, dropped frames, CPU cost, and user-visible latency during 30-minute comparison runs.
- **Validation plan**: Add automated coverage where practical around config precedence and overlay text formatting, then run manual streaming validation on SDL and X11-class backends plus at least one embedded backend to verify either overlay support or graceful unsupported behavior.
- **Documentation sync**: `README.md`, `docs/README.pod`, `moonlight.conf`, and CLI help output all require changes; no contributor or install docs require updates beyond those files.

## Project Structure

### Documentation (this feature)

```text
specs/001-stats-overlay/
├── plan.md
├── research.md
├── data-model.md
├── quickstart.md
├── contracts/
│   └── stats-overlay.md
└── tasks.md
```

### Source Code (repository root)

```text
src/
├── config.c
├── config.h
├── connection.c
├── connection.h
├── loop.c
├── main.c
├── platform.c
├── platform.h
├── sdl.c
├── sdl.h
├── audio/
├── input/
└── video/
    ├── aml.c
    ├── egl.c
    ├── ffmpeg.c
    ├── imx.c
    ├── mmal.c
    ├── pi.c
    ├── rk.c
    ├── sdl.c
    ├── video.h
    └── x11.c

docs/
└── README.pod

CMakeLists.txt
README.md
moonlight.conf
third_party/moonlight-common-c/
```

**Structure Decision**: Keep the feature inside the existing single-project CMake CLI layout. Shared stats state and formatting belong in `src/`, config/help handling stays in `src/config.c`, `src/config.h`, and `src/main.c`, and actual overlay drawing remains inside the backend presentation files that already own final frame output.

## Phase 0 Research Summary

- Control surface follows existing long-option and config naming: `-stats`, `-nostats`, and `stats = true|false`.
- Telemetry is modeled as a shared, session-local snapshot populated from official streaming timing and callback data rather than duplicated per backend.
- Overlay drawing uses backend-specific presenters with a minimal in-tree text path to avoid a new mandatory font dependency.
- Unsupported or partially instrumented backends stay operational and explicitly report unavailable metrics or unsupported overlay capability.
- Validation combines config and formatter automation with manual streaming checks on representative backends.

## Phase 1 Design Summary

- `data-model.md` defines the session preference, live stats snapshot, overlay presentation state, and backend capability model.
- `contracts/stats-overlay.md` defines the CLI/config surface, session-locking rules, overlay content contract, and unsupported-backend behavior.
- `quickstart.md` captures build, enable/disable, and manual validation flows for the feature branch.
- `update-agent-context.sh codex` must run after this plan is written so agent guidance reflects the C99 + CMake + backend overlay design.

## Post-Design Constitution Check

- **CLI and config compatibility**: PASS. The design adds opt-in flags and config only, and preserves current defaults and precedence.
- **Platform isolation**: PASS. Shared stats logic is separated from renderer-specific drawing, and unsupported backends are treated as explicit capability gaps rather than shared hacks.
- **Streaming performance**: PASS. The design uses cached formatted text, bounded refresh cadence, and explicit regression metrics tied to the spec.
- **Validation plan**: PASS. The design includes practical automation targets plus required manual host and backend checks.
- **Documentation sync**: PASS. The planned deliverables and touched source/docs cover all required user-facing surfaces.
