#include "stats_overlay.h"

#include <stdio.h>
#include <string.h>
#include <sys/time.h>

/** Returns the current wall-clock time in milliseconds for cache timestamps. */
static uint64_t stats_overlay_now_ms(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return ((uint64_t) tv.tv_sec * 1000ULL) + ((uint64_t) tv.tv_usec / 1000ULL);
}

/** Formats a metric value or the required Unavailable placeholder. */
static void format_value(char* buffer, size_t buffer_size, const PSTATS_OVERLAY_VALUE value, const char* suffix) {
  if (!value->available) {
    snprintf(buffer, buffer_size, "Unavailable");
    return;
  }

  if (suffix && suffix[0] != '\0')
    snprintf(buffer, buffer_size, "%.2f%s", value->value, suffix);
  else
    snprintf(buffer, buffer_size, "%.2f", value->value);
}

/** Initializes the session preference with the default disabled state. */
void stats_overlay_pref_init(PSTATS_OVERLAY_PREFERENCE pref) {
  pref->enabled = false;
  pref->source = STATS_OVERLAY_SOURCE_DEFAULT;
  pref->locked_for_session = false;
}

/** Applies a config or CLI preference unless the session has already locked it. */
void stats_overlay_pref_apply(PSTATS_OVERLAY_PREFERENCE pref, bool enabled, enum stats_overlay_option_source source) {
  if (pref->locked_for_session)
    return;

  pref->enabled = enabled;
  pref->source = source;
}

/** Locks the overlay preference for the active session. */
void stats_overlay_pref_lock(PSTATS_OVERLAY_PREFERENCE pref) {
  pref->locked_for_session = true;
}

/** Unlocks the overlay preference after the active session ends. */
void stats_overlay_pref_unlock(PSTATS_OVERLAY_PREFERENCE pref) {
  pref->locked_for_session = false;
}

/** Clears the live snapshot and seeds its codec label. */
void stats_overlay_snapshot_init(PSTATS_OVERLAY_SNAPSHOT snapshot) {
  memset(snapshot, 0, sizeof(*snapshot));
  snprintf(snapshot->codec, sizeof(snapshot->codec), "Unknown");
}

/** Stores the stream identity fields used by the formatted overlay header. */
void stats_overlay_snapshot_set_stream(PSTATS_OVERLAY_SNAPSHOT snapshot, int width, int height, double fps, const char* codec) {
  snapshot->video_width = width;
  snapshot->video_height = height;
  snapshot->video_fps = fps;
  snprintf(snapshot->codec, sizeof(snapshot->codec), "%s", codec ? codec : "Unknown");
  snapshot->updated_at_ms = stats_overlay_now_ms();
}

/** Updates a single overlay metric value and availability bit. */
void stats_overlay_snapshot_set_value(PSTATS_OVERLAY_VALUE value, bool available, double metric) {
  value->available = available;
  value->value = metric;
}

/** Records whether the current backend can draw the overlay. */
void stats_overlay_capability_init(PSTATS_OVERLAY_CAPABILITY capability, bool supports_overlay, bool supports_partial_metrics, const char* unsupported_reason) {
  capability->supports_overlay = supports_overlay;
  capability->supports_partial_metrics = supports_partial_metrics;
  capability->unsupported_reason = unsupported_reason;
}

/** Resets the overlay presentation state before a session starts. */
void stats_overlay_init(PSTATS_OVERLAY_STATE state) {
  memset(state, 0, sizeof(*state));
  state->refresh_interval_ms = 500;
}

/** Starts a session-visible state only when both preference and backend allow it. */
void stats_overlay_session_start(PSTATS_OVERLAY_STATE state, const PSTATS_OVERLAY_PREFERENCE pref, const PSTATS_OVERLAY_CAPABILITY capability) {
  state->visible = pref->enabled && capability->supports_overlay;
  state->warning_emitted = false;
  state->line_count = 0;
  state->last_format_ms = 0;
}

/** Clears any active overlay state after a session stops. */
void stats_overlay_session_stop(PSTATS_OVERLAY_STATE state) {
  state->visible = false;
  state->warning_emitted = false;
  state->line_count = 0;
  state->last_format_ms = 0;
}

/** Returns true once per session when an enabled overlay is unsupported. */
bool stats_overlay_should_warn(PSTATS_OVERLAY_STATE state, const PSTATS_OVERLAY_PREFERENCE pref, const PSTATS_OVERLAY_CAPABILITY capability) {
  if (!pref->enabled || capability->supports_overlay || state->warning_emitted)
    return false;

  state->warning_emitted = true;
  return true;
}

/** Refreshes the cached overlay lines on the configured cadence. */
bool stats_overlay_update(PSTATS_OVERLAY_STATE state, const PSTATS_OVERLAY_SNAPSHOT snapshot, uint64_t now_ms) {
  char buffer_a[32];
  char buffer_b[32];
  char buffer_c[32];

  if (!state->visible)
    return false;

  // Reuse the cached text until the refresh window expires to keep formatting off the hot path.
  if (state->last_format_ms != 0 && now_ms < state->last_format_ms + state->refresh_interval_ms)
    return false;

  // Keep the line order fixed so unavailable metrics do not shift the visible panel.
  snprintf(state->formatted_lines[0], sizeof(state->formatted_lines[0]),
      "Video stream: %dx%d %.2f FPS (Codec: %s)",
      snapshot->video_width, snapshot->video_height, snapshot->video_fps, snapshot->codec);

  format_value(buffer_a, sizeof(buffer_a), &snapshot->incoming_network_fps, "");
  snprintf(state->formatted_lines[1], sizeof(state->formatted_lines[1]), "Incoming frame rate from network: %s", buffer_a);

  format_value(buffer_a, sizeof(buffer_a), &snapshot->decoding_fps, "");
  snprintf(state->formatted_lines[2], sizeof(state->formatted_lines[2]), "Decoding frame rate: %s", buffer_a);

  format_value(buffer_a, sizeof(buffer_a), &snapshot->rendering_fps, "");
  snprintf(state->formatted_lines[3], sizeof(state->formatted_lines[3]), "Rendering frame rate: %s", buffer_a);

  format_value(buffer_a, sizeof(buffer_a), &snapshot->host_latency_min_ms, " ms");
  format_value(buffer_b, sizeof(buffer_b), &snapshot->host_latency_max_ms, " ms");
  format_value(buffer_c, sizeof(buffer_c), &snapshot->host_latency_avg_ms, " ms");
  snprintf(state->formatted_lines[4], sizeof(state->formatted_lines[4]), "Host processing latency min/max/average: %s/%s/%s", buffer_a, buffer_b, buffer_c);

  format_value(buffer_a, sizeof(buffer_a), &snapshot->network_drop_pct, "%%");
  snprintf(state->formatted_lines[5], sizeof(state->formatted_lines[5]), "Frames dropped by your network connection: %s", buffer_a);

  format_value(buffer_a, sizeof(buffer_a), &snapshot->jitter_drop_pct, "%%");
  snprintf(state->formatted_lines[6], sizeof(state->formatted_lines[6]), "Frames dropped due to network jitter: %s", buffer_a);

  format_value(buffer_a, sizeof(buffer_a), &snapshot->network_latency_avg_ms, " ms");
  format_value(buffer_b, sizeof(buffer_b), &snapshot->network_latency_variance_ms, " ms");
  snprintf(state->formatted_lines[7], sizeof(state->formatted_lines[7]), "Average network latency: %s (variance: %s)", buffer_a, buffer_b);

  format_value(buffer_a, sizeof(buffer_a), &snapshot->decode_time_avg_ms, " ms");
  snprintf(state->formatted_lines[8], sizeof(state->formatted_lines[8]), "Average decoding time: %s", buffer_a);

  format_value(buffer_a, sizeof(buffer_a), &snapshot->queue_delay_avg_ms, " ms");
  snprintf(state->formatted_lines[9], sizeof(state->formatted_lines[9]), "Average queue delay: %s", buffer_a);

  format_value(buffer_a, sizeof(buffer_a), &snapshot->render_time_avg_ms, " ms");
  snprintf(state->formatted_lines[10], sizeof(state->formatted_lines[10]), "Average rendering time (including monitor V-sync latency): %s", buffer_a);

  state->line_count = STATS_OVERLAY_MAX_LINES;
  state->last_format_ms = now_ms;
  return true;
}
