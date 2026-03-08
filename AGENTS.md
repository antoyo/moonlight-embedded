# moonlight-embedded Development Guidelines

Auto-generated from all feature plans. Last updated: 2026-03-08

## Active Technologies

- C99, CMake 3.6 + libgamestream client layer, Moonlight Common C / Limelight callbacks, FFmpeg-backed decode paths, SDL2, X11/EGL/VAAPI/VDPAU, optional embedded video backends (PI/MMAL/IMX/AML/RK) (001-stats-overlay)

## Project Structure

```text
src/
src/audio/
src/input/
src/video/
docs/
specs/
```

## Commands

- `cmake -S . -B build`
- `cmake --build build`
- `rg -n "<pattern>" src docs moonlight.conf`

## Code Style

C99 + CMake: keep CLI/config changes backward compatible, keep backend-specific rendering inside the existing platform files, and avoid new mandatory runtime dependencies when a small in-tree helper will do.

## Recent Changes

- 001-stats-overlay: Added C99, CMake 3.6 + libgamestream client layer, Moonlight Common C / Limelight callbacks, FFmpeg-backed decode paths, SDL2, X11/EGL/VAAPI/VDPAU, optional embedded video backends (PI/MMAL/IMX/AML/RK)

<!-- MANUAL ADDITIONS START -->
## Coding Rules
- Every function MUST have a doc-comment.
- Functions longer than 10 lines MUST contain inline comments.
- Complex logic MUST be commented.
<!-- MANUAL ADDITIONS END -->
