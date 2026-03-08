# Data Model: Real-Time Stats Overlay

## Overview

The feature introduces a session-scoped preference, a live telemetry snapshot, a presentation state, and a backend capability record. These entities are in-memory only and exist for the lifetime of a stream session.

## Entity: StatsOverlayPreference

| Field | Type | Description |
|-------|------|-------------|
| `enabled` | boolean | Final resolved decision for whether the overlay should be shown for the session |
| `source` | enum | Where the final value came from: `default`, `config_file`, or `cli` |
| `locked_for_session` | boolean | Indicates that the value has been frozen for the active session |

### Validation Rules

- Default value is `false`.
- CLI input may override config-file input in either direction.
- Once streaming begins, the resolved value cannot change until the next session.

### State Transitions

`default` -> `resolved before stream start` -> `locked during active session` -> `discarded when session ends`

## Entity: LiveStatsSnapshot

| Field | Type | Description |
|-------|------|-------------|
| `video_width` | integer | Active streamed width |
| `video_height` | integer | Active streamed height |
| `video_fps` | decimal | Streamed frame rate |
| `codec` | enum | Active codec label such as `H264`, `HEVC`, or `AV1` |
| `incoming_network_fps` | decimal or unavailable | Frames per second arriving from the network |
| `decoding_fps` | decimal or unavailable | Frames per second successfully decoded |
| `rendering_fps` | decimal or unavailable | Frames per second presented to the display path |
| `host_latency_min_ms` | decimal or unavailable | Minimum host processing latency |
| `host_latency_max_ms` | decimal or unavailable | Maximum host processing latency |
| `host_latency_avg_ms` | decimal or unavailable | Average host processing latency |
| `network_drop_pct` | decimal or unavailable | Frames dropped by the network connection |
| `jitter_drop_pct` | decimal or unavailable | Frames dropped because of network jitter |
| `network_latency_avg_ms` | decimal or unavailable | Average network latency |
| `network_latency_variance_ms` | decimal or unavailable | Network latency variance |
| `decode_time_avg_ms` | decimal or unavailable | Average decode time |
| `queue_delay_avg_ms` | decimal or unavailable | Average queue delay before presentation |
| `render_time_avg_ms` | decimal or unavailable | Average render time including display synchronization delay |
| `updated_at` | monotonic timestamp | Time of the most recent snapshot refresh |

### Validation Rules

- Numeric timing values must be greater than or equal to zero when available.
- Percentage values must remain within `0.00` to `100.00`.
- Missing values are represented independently from numeric zero so valid zero-like readings are preserved.
- Field order is fixed so the overlay line order remains stable.

## Entity: OverlayPresentationState

| Field | Type | Description |
|-------|------|-------------|
| `visible` | boolean | Whether the overlay should currently be drawn |
| `anchor` | enum | Screen anchor; fixed to `top_left` |
| `refresh_interval_ms` | integer | Maximum time between formatted overlay refreshes |
| `formatted_lines` | ordered list of strings | Cached text lines ready for drawing |
| `warning_emitted` | boolean | Whether an unsupported-backend or partial-metrics warning has already been surfaced |

### Validation Rules

- `refresh_interval_ms` must be less than or equal to `2000`.
- `formatted_lines` always include the full metric set in the same order.
- Any unavailable field must render as `Unavailable`.
- The final overlay bounds must stay within the visible video area and within the footprint limits defined by the spec.

### State Transitions

`hidden` -> `visible during active stream` -> `hidden on session stop`

## Entity: BackendOverlayCapability

| Field | Type | Description |
|-------|------|-------------|
| `platform` | enum | Resolved playback backend such as `SDL`, `X11`, `X11_VAAPI`, `PI`, `MMAL`, `IMX`, `AML`, or `RK` |
| `supports_overlay` | boolean | Whether the backend can safely draw the overlay |
| `supports_partial_metrics` | boolean | Whether some metrics may be unavailable even though drawing is supported |
| `unsupported_reason` | string or empty | Human-readable reason when overlay drawing is not supported |

### Validation Rules

- Unsupported capability must never terminate or block a stream session.
- If `supports_overlay` is `false`, the application must keep streaming and surface the limitation once.
- Partial metric support does not change line order or disable the overlay.

## Relationships

- `StatsOverlayPreference` controls whether `OverlayPresentationState.visible` can become `true`.
- `LiveStatsSnapshot` feeds `OverlayPresentationState.formatted_lines`.
- `BackendOverlayCapability` constrains whether `OverlayPresentationState` can draw the overlay or must fall back to a documented unsupported path.
