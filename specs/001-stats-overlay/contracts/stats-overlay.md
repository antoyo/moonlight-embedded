# Stats Overlay Contract

## CLI and Config Surface

| Surface | Name | Type | Default | Contract |
|---------|------|------|---------|----------|
| CLI flag | `-stats` | boolean enable | disabled | Enables the on-screen stats overlay for the next stream session |
| CLI flag | `-nostats` | boolean disable | n/a | Disables the on-screen stats overlay for the next stream session and overrides earlier config or CLI enable input |
| Config key | `stats` | `true` or `false` | `false` | Persists the preferred overlay state in config files |

## Resolution Rules

1. Built-in default is overlay disabled.
2. Loaded config files resolve in the same order as existing config processing.
3. Later command-line input overrides earlier config-derived values.
4. The final resolved value is locked when streaming starts and does not change until the next session.

## Runtime Behavior

- When enabled and supported by the resolved backend, the overlay appears in the top-left of the visible video area within 5 seconds of the first video frame.
- When disabled, no stats text, placeholder region, or reserved layout space may appear.
- The overlay remains visible only for the active stream session.
- Every metric line remains present in a fixed order; unavailable values render as `Unavailable`.
- If the backend cannot support overlay drawing, the stream continues, the overlay remains hidden, and the client emits a single user-visible notice explaining that the backend does not support the stats overlay.

## Required Overlay Content

The overlay must present labeled values equivalent to the following line set whenever the telemetry is available:

```text
Video stream: <width>x<height> <fps> FPS (Codec: <codec>)
Incoming frame rate from network: <value>
Decoding frame rate: <value>
Rendering frame rate: <value>
Host processing latency min/max/average: <min>/<max>/<avg>
Frames dropped by your network connection: <value>
Frames dropped due to network jitter: <value>
Average network latency: <avg> (variance: <value>)
Average decoding time: <value>
Average queue delay: <value>
Average rendering time (including monitor V-sync latency): <value>
```

## Documentation Obligations

- `src/main.c` help output must describe `-stats` and `-nostats`.
- `docs/README.pod` must describe the new CLI flags and the `stats` config key.
- `moonlight.conf` must include the new config entry in the shipped example file.
- `README.md` must mention that the client supports an optional real-time stats overlay and point users to the detailed docs.
