# Quickstart: Real-Time Stats Overlay

## Build

```bash
cmake -S /home/bouanto/projects/moonlight-embedded -B /home/bouanto/projects/moonlight-embedded/build
cmake --build /home/bouanto/projects/moonlight-embedded/build
```

## Enable Through Config

Add the new setting to a loaded config file:

```ini
stats = true
```

Then start a stream with the normal workflow:

```bash
/home/bouanto/projects/moonlight-embedded/build/moonlight stream <host>
```

## Enable or Disable Per Session From the CLI

Enable for one session:

```bash
/home/bouanto/projects/moonlight-embedded/build/moonlight stream -stats <host>
```

Disable for one session even if a config file enables it:

```bash
/home/bouanto/projects/moonlight-embedded/build/moonlight stream -nostats <host>
```

## Manual Validation Flow

1. Start a stream with `-stats -platform sdl` and confirm the overlay appears in the top-left within 5 seconds.
2. Confirm the SDL overlay shows the full metric list in stable line order while values continue updating during playback.
3. Start a stream with `-stats -platform x11` and confirm the same top-left overlay appears and keeps the same label order.
4. Confirm missing metrics render as `Unavailable` without collapsing the line list on both SDL and X11.
5. Change the config value while the stream is active and confirm the current session does not change.
6. Start a new session and confirm the updated value applies.
7. Start a session with `-nostats` and confirm no overlay or placeholder is rendered.
8. Start a stream with `-stats -platform aml`; confirm the stream continues and a single unsupported-backend warning is emitted instead of drawing text.

## Performance Validation

1. Capture a 30-minute baseline session without the overlay.
2. Capture an equivalent 30-minute session with `-stats`.
3. Compare achieved FPS, queue delay, decode time, render time, dropped frames, and user-observable latency.
4. Confirm overlay enablement does not introduce more than a 2% regression.
