# Feature Specification: Real-Time Stats Overlay

**Feature Branch**: `001-stats-overlay`  
**Created**: 2026-03-08  
**Status**: Draft  
**Input**: User description: "I want to add the following feature to this project: real-time stats output at the top-left of the screen, similar to what you can get in moonlight-qt. This should only be shown when the config for that is enabled." Additional user-provided example output defines the expected metric set and display content.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Monitor Session Health (Priority: P1)

A user who wants immediate visibility into stream quality enables stats output in configuration and starts a streaming session. The user can then see a compact panel of live session metrics in the top-left corner while playing.

**Why this priority**: The primary value of the feature is giving users instant feedback about stream performance without leaving the session or guessing why quality changes occur.

**Independent Test**: Enable the stats setting, start a stream, and verify that a compact stats panel appears in the top-left of the visible video area and updates while the session is active.

**Acceptance Scenarios**:

1. **Given** stats output is enabled for the session, **When** the user starts streaming and video becomes visible, **Then** a stats panel appears in the top-left corner within 5 seconds.
2. **Given** stats output is enabled and all expected telemetry is available, **When** the panel is shown, **Then** it displays labeled values for stream resolution, stream frame rate, codec, incoming network frame rate, decoding frame rate, rendering frame rate, host processing latency minimum/maximum/average, network drop rate, jitter drop rate, average network latency with variance, average decoding time, average queue delay, and average rendering time.
3. **Given** stats output is enabled and a stream is active, **When** frame rate, bitrate, latency, or packet quality changes, **Then** the panel reflects the updated values within 2 seconds.
4. **Given** stats output is enabled, **When** the streaming session ends, **Then** the stats panel disappears with the session output.

---

### User Story 2 - Preserve the Existing Viewing Experience (Priority: P2)

A user who does not want an on-screen overlay leaves the setting disabled and continues streaming with no added on-screen text or layout changes.

**Why this priority**: The feature must remain strictly opt-in so current users do not lose screen space or see unexpected overlays.

**Independent Test**: Start a stream with stats output disabled and verify that no stats text, placeholder region, or reserved overlay area appears at any point during the session.

**Acceptance Scenarios**:

1. **Given** stats output is disabled, **When** the user starts a stream, **Then** no stats panel is shown anywhere on the screen.
2. **Given** stats output is disabled, **When** the user streams under otherwise identical conditions, **Then** the session presentation remains unchanged apart from the absence of stats output.

---

### User Story 3 - Remain Useful When Data Is Incomplete (Priority: P3)

A user with stats enabled still receives a stable overlay even when some metrics are temporarily unavailable or the visible video area changes during the session.

**Why this priority**: Missing or changing telemetry should not make the overlay misleading, unreadable, or disruptive.

**Independent Test**: Run a session where one or more metrics are unavailable or where the visible video area changes, and verify that the overlay stays on-screen, keeps updating available values, and clearly marks unavailable values.

**Acceptance Scenarios**:

1. **Given** stats output is enabled and one or more metrics are unavailable, **When** the overlay refreshes, **Then** available values continue updating and unavailable values are clearly identified as unavailable.
2. **Given** stats output is enabled and the visible video area changes because of rotation, resizing, or display mode changes, **When** the overlay is redrawn, **Then** it remains anchored to the top-left of the visible video area without extending off-screen.

### Edge Cases

- The session starts before all live metrics are available.
- A metric becomes temporarily unavailable because of a transient connection or decoder issue.
- The visible video area changes during the session because of rotation, windowed playback, or a display mode change.
- The stream is shown on a small display where a large overlay could obscure gameplay.
- Multiple configuration sources are loaded and the final resolved setting differs from the base configuration file.
- Some metrics may legitimately remain at zero or near-zero values during a healthy session and must still be displayed as valid readings.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The system MUST provide a user-configurable setting that enables or disables on-screen real-time streaming stats for a session.
- **FR-002**: The default behavior MUST keep the stats overlay hidden unless the resolved configuration explicitly enables it.
- **FR-003**: When enabled, the system MUST display a compact stats panel anchored to the top-left of the visible video area during active streaming sessions.
- **FR-004**: The stats panel MUST present a consistent multi-line, labeled, human-readable summary of live stream-health metrics while the session is active.
- **FR-005**: When the underlying telemetry is available, the stats panel MUST display all of the following metric categories:
  - active video stream resolution, frame rate, and codec
  - incoming frame rate from the network
  - decoding frame rate
  - rendering frame rate
  - host processing latency minimum, maximum, and average
  - frames dropped by the network connection as a percentage
  - frames dropped due to network jitter as a percentage
  - average network latency and latency variance
  - average decoding time
  - average queue delay
  - average rendering time, including display synchronization delay
- **FR-006**: When enabled, the stats panel MUST refresh often enough that significant metric changes are reflected within 2 seconds.
- **FR-007**: If an individual metric is unavailable, the system MUST continue the session and clearly indicate that the affected value is unavailable instead of showing stale or misleading data.
- **FR-008**: The overlay MUST remain fully within the visible video area, remain readable across supported display modes, and occupy no more than 15% of the visible screen width or 10% of the visible screen height.
- **FR-009**: When stats output is disabled, the system MUST not render any stats text, placeholder region, or other persistent visual artifact during startup, active streaming, or shutdown.
- **FR-010**: Existing configuration precedence rules MUST determine whether stats output is shown when multiple configuration sources are used, and the user-facing documentation MUST explain how to enable and disable the feature.
- **FR-011**: Any platform-specific limitation that prevents the overlay from being shown MUST fail gracefully by preserving the stream session and clearly documenting the limitation.
- **FR-012**: Enabling stats output MUST not reduce achieved stream frame rate or increase user-observable latency by more than 2% during equivalent 30-minute comparison sessions.
- **FR-013**: Help text, sample configuration, and user-facing documentation MUST describe the setting, the overlay behavior, and the meaning of each displayed metric.

### Example Overlay Content

When all referenced telemetry is available, the overlay should convey information equivalent to the following example:

```text
Video stream: 2560x1440 60.00 FPS (Codec: HEVC)
Incoming frame rate from network: 60.00 FPS
Decoding frame rate: 60.00 FPS
Rendering frame rate: 60.00 FPS
Host processing latency min/max/average: 2.5/4.3/2.6 ms
Frames dropped by your network connection: 0.00%
Frames dropped due to network jitter: 0.00%
Average network latency: 1 ms (variance: 1 ms)
Average decoding time: 0.38 ms
Average queue delay: 8.19 ms
Average rendering time (including monitor V-sync latency): 0.30 ms
```

### Key Entities *(include if feature involves data)*

- **Stats Overlay Preference**: The resolved session setting that determines whether live stats are shown or hidden.
- **Live Stats Snapshot**: A time-stamped collection of the currently available stream metrics shown in the overlay.
- **Streaming Display State**: The current visible video bounds and orientation used to keep the overlay positioned correctly.

## Assumptions

- The feature applies to active streaming sessions and does not affect pairing, listing, quitting, or input-mapping commands.
- The overlay is passive and informational only; users are not expected to interact with it while streaming.
- The feature uses the existing project configuration workflow and remains disabled by default to preserve current behavior.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: In 100% of validation sessions with stats enabled, users see the stats panel in the top-left of the visible video area within 5 seconds of the first video frame.
- **SC-002**: In at least 95% of observed metric changes during validation sessions, the displayed values update within 2 seconds.
- **SC-003**: In 100% of validation sessions where the referenced telemetry is available, every required metric category is shown with a distinct label and current value.
- **SC-004**: In 100% of regression sessions with stats disabled, no persistent on-screen stats elements appear during startup, active streaming, or shutdown.
- **SC-005**: On representative supported hardware, enabling stats output changes achieved frame rate or user-observable latency by no more than 2% compared with an otherwise identical session without stats output.
- **SC-006**: Documentation, help text, and configuration guidance affected by the feature are updated in the same change set.
