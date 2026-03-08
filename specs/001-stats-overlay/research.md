# Research: Real-Time Stats Overlay

## Decision: Expose the feature as `-stats`, `-nostats`, and `stats = true|false`

**Rationale**: Existing boolean streaming options already use long-only CLI flags and identically named config keys. Adding both `-stats` and `-nostats` preserves current precedence behavior even when a loaded config enables the overlay and a one-off session needs to disable it.

**Alternatives considered**:

- Config key only: rejected because it would not satisfy the clarified requirement for CLI control.
- `-stats` only: rejected because it would not allow command-line disable when config files enable the feature.

## Decision: Keep telemetry collection in a shared session-local stats module

**Rationale**: The shared streaming layer already provides timing data through official stream callbacks and decode-unit metadata, while Moonlight documentation notes that some latency or frame-drop figures vary by client and decoder path. A shared snapshot model lets the application reuse one formatting path while allowing individual metrics to remain unavailable on specific backends.

**Alternatives considered**:

- Recompute or format telemetry separately inside each renderer: rejected because it would duplicate logic and drift across backends.
- Log-only stats output: rejected because the feature requires an on-screen overlay.

## Decision: Render through backend-specific presenters, not a cross-backend overlay shortcut

**Rationale**: Final frame ownership already lives in backend-specific files such as `src/sdl.c` and `src/video/x11.c`, while hardware renderers are isolated behind dedicated shared libraries. Keeping compositing inside the presenter that owns the final frame respects the constitution's platform-isolation rule and makes unsupported backends an explicit capability decision instead of a hidden regression risk.

**Alternatives considered**:

- Inject a generic overlay layer into the shared streaming loop: rejected because it would leak rendering assumptions across unrelated backends.
- Require every backend to gain overlay support in one change: rejected because it would force a wide, high-risk patch across hardware-specific renderers.

## Decision: Use a small in-tree bitmap text renderer

**Rationale**: The repository currently has no font or text-layout dependency, and adding one would expand the runtime surface across multiple Linux targets. A fixed-width in-tree bitmap font keeps the overlay readable, deterministic, and portable without introducing a new mandatory third-party runtime library.

**Alternatives considered**:

- Add SDL_ttf, Xft, or another font library: rejected because it increases dependency, packaging, and compatibility burden.
- Use backend-specific native font APIs only: rejected because it would create inconsistent output and leave non-windowed backends behind.

## Decision: Refresh cached overlay text on a bounded timer and keep a fixed line order

**Rationale**: The spec requires visible updates within 2 seconds, not per-frame recomputation. Regenerating text on a bounded cadence such as 500 ms keeps CPU cost predictable, avoids per-packet formatting churn, and works cleanly with the clarified rule that missing metrics stay in place and display `Unavailable`.

**Alternatives considered**:

- Rebuild every line on every rendered frame: rejected because it adds unnecessary work on the latency-sensitive path.
- Hide missing metrics or collapse to a smaller summary: rejected because the spec now requires stable line positions with explicit `Unavailable` values.

## Decision: Validate with a mix of lightweight automation and manual streaming checks

**Rationale**: The project currently has no dedicated automated test suite, but config precedence and line formatting can still be validated with focused automation or compile-time harnesses. Manual streaming sessions remain necessary to verify actual on-screen behavior, backend capability handling, and performance impact on representative hardware.

**Alternatives considered**:

- Manual validation only: rejected because config precedence and formatting regressions are practical to automate.
- Full automation for runtime overlay behavior: rejected because renderer availability and hardware-specific paths make that unrealistic for this repository.
