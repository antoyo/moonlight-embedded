#include "stats_overlay.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

typedef struct _STATS_OVERLAY_WINDOW {
  uint64_t started_at_ms;
  unsigned int incoming_frames;
  unsigned int decoded_frames;
  unsigned int rendered_frames;
  double decode_time_total_ms;
  double render_time_total_ms;
  double queue_delay_total_ms;
  unsigned int queue_delay_count;
  double host_latency_total_ms;
  unsigned int host_latency_count;
  double host_latency_min_ms;
  double host_latency_max_ms;
  RTP_VIDEO_STATS last_video_stats;
  bool have_last_video_stats;
} STATS_OVERLAY_WINDOW, *PSTATS_OVERLAY_WINDOW;

static pthread_mutex_t stats_overlay_runtime_mutex = PTHREAD_MUTEX_INITIALIZER;
static STATS_OVERLAY_STATE stats_overlay_runtime_state_data;
static STATS_OVERLAY_SNAPSHOT stats_overlay_runtime_snapshot;
static STATS_OVERLAY_PREFERENCE stats_overlay_runtime_pref;
static STATS_OVERLAY_CAPABILITY stats_overlay_runtime_capability;
static STATS_OVERLAY_WINDOW stats_overlay_runtime_window;

/** Returns the current wall-clock time in milliseconds for cache timestamps. */
static uint64_t stats_overlay_now_ms(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return ((uint64_t) tv.tv_sec * 1000ULL) + ((uint64_t) tv.tv_usec / 1000ULL);
}

/** Resets the rolling metric window used for overlay refresh calculations. */
static void stats_overlay_window_reset(PSTATS_OVERLAY_WINDOW window, uint64_t now_ms) {
  memset(window, 0, sizeof(*window));
  window->started_at_ms = now_ms;
  window->host_latency_min_ms = 0.0;
  window->host_latency_max_ms = 0.0;
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

/** Configures the shared runtime overlay state for a newly starting session. */
void stats_overlay_runtime_configure(const PSTATS_OVERLAY_PREFERENCE pref, const PSTATS_OVERLAY_CAPABILITY capability, int width, int height, double fps, const char* codec) {
  uint64_t now_ms = stats_overlay_now_ms();

  pthread_mutex_lock(&stats_overlay_runtime_mutex);

  stats_overlay_runtime_pref = *pref;
  stats_overlay_runtime_capability = *capability;
  stats_overlay_init(&stats_overlay_runtime_state_data);
  stats_overlay_snapshot_init(&stats_overlay_runtime_snapshot);
  stats_overlay_snapshot_set_stream(&stats_overlay_runtime_snapshot, width, height, fps, codec);
  stats_overlay_session_start(&stats_overlay_runtime_state_data, &stats_overlay_runtime_pref, &stats_overlay_runtime_capability);
  stats_overlay_window_reset(&stats_overlay_runtime_window, now_ms);

  pthread_mutex_unlock(&stats_overlay_runtime_mutex);
}

/** Stops and clears the shared runtime overlay state after a session ends. */
void stats_overlay_runtime_stop(void) {
  pthread_mutex_lock(&stats_overlay_runtime_mutex);

  stats_overlay_session_stop(&stats_overlay_runtime_state_data);
  stats_overlay_window_reset(&stats_overlay_runtime_window, 0);

  pthread_mutex_unlock(&stats_overlay_runtime_mutex);
}

/** Records incoming frame timing information from the assembled decode unit. */
void stats_overlay_runtime_note_decode_unit(const PDECODE_UNIT decode_unit) {
  uint64_t now_us = LiGetMicroseconds();
  double host_latency_ms = decode_unit->frameHostProcessingLatency / 10.0;

  pthread_mutex_lock(&stats_overlay_runtime_mutex);

  stats_overlay_runtime_window.incoming_frames++;

  if (decode_unit->enqueueTimeUs != 0 && now_us >= decode_unit->enqueueTimeUs) {
    stats_overlay_runtime_window.queue_delay_total_ms += (now_us - decode_unit->enqueueTimeUs) / 1000.0;
    stats_overlay_runtime_window.queue_delay_count++;
  }

  if (decode_unit->frameHostProcessingLatency != 0) {
    // Track min/max/average from the host-supplied per-frame latency sample.
    if (stats_overlay_runtime_window.host_latency_count == 0 || host_latency_ms < stats_overlay_runtime_window.host_latency_min_ms)
      stats_overlay_runtime_window.host_latency_min_ms = host_latency_ms;
    if (stats_overlay_runtime_window.host_latency_count == 0 || host_latency_ms > stats_overlay_runtime_window.host_latency_max_ms)
      stats_overlay_runtime_window.host_latency_max_ms = host_latency_ms;

    stats_overlay_runtime_window.host_latency_total_ms += host_latency_ms;
    stats_overlay_runtime_window.host_latency_count++;
  }

  pthread_mutex_unlock(&stats_overlay_runtime_mutex);
}

/** Records a decoded frame and its measured decoder cost. */
void stats_overlay_runtime_note_decoded_frame(double decode_time_ms) {
  pthread_mutex_lock(&stats_overlay_runtime_mutex);

  stats_overlay_runtime_window.decoded_frames++;
  stats_overlay_runtime_window.decode_time_total_ms += decode_time_ms;

  pthread_mutex_unlock(&stats_overlay_runtime_mutex);
}

/** Records a presented frame and its measured render cost. */
void stats_overlay_runtime_note_render(double render_time_ms) {
  pthread_mutex_lock(&stats_overlay_runtime_mutex);

  stats_overlay_runtime_window.rendered_frames++;
  stats_overlay_runtime_window.render_time_total_ms += render_time_ms;

  pthread_mutex_unlock(&stats_overlay_runtime_mutex);
}

/** Refreshes live metrics from the rolling counters and Moonlight transport APIs. */
bool stats_overlay_runtime_refresh(void) {
  uint64_t now_ms = stats_overlay_now_ms();
  uint32_t rtt = 0;
  uint32_t rtt_variance = 0;
  const RTP_VIDEO_STATS* video_stats = LiGetRTPVideoStats();
  bool updated = false;

  pthread_mutex_lock(&stats_overlay_runtime_mutex);

  if (!stats_overlay_runtime_state_data.visible) {
    pthread_mutex_unlock(&stats_overlay_runtime_mutex);
    return false;
  }

  if (stats_overlay_runtime_window.started_at_ms == 0)
    stats_overlay_runtime_window.started_at_ms = now_ms;

  if (now_ms >= stats_overlay_runtime_window.started_at_ms + stats_overlay_runtime_state_data.refresh_interval_ms) {
    double elapsed_seconds = (now_ms - stats_overlay_runtime_window.started_at_ms) / 1000.0;

    if (elapsed_seconds > 0.0) {
      stats_overlay_snapshot_set_value(&stats_overlay_runtime_snapshot.incoming_network_fps, true,
          stats_overlay_runtime_window.incoming_frames / elapsed_seconds);
      stats_overlay_snapshot_set_value(&stats_overlay_runtime_snapshot.decoding_fps, true,
          stats_overlay_runtime_window.decoded_frames / elapsed_seconds);
      stats_overlay_snapshot_set_value(&stats_overlay_runtime_snapshot.rendering_fps, true,
          stats_overlay_runtime_window.rendered_frames / elapsed_seconds);
    }

    // Populate averages only when samples exist so the overlay can still render Unavailable placeholders.
    stats_overlay_snapshot_set_value(&stats_overlay_runtime_snapshot.decode_time_avg_ms,
        stats_overlay_runtime_window.decoded_frames != 0,
        stats_overlay_runtime_window.decoded_frames != 0 ?
            stats_overlay_runtime_window.decode_time_total_ms / stats_overlay_runtime_window.decoded_frames : 0.0);
    stats_overlay_snapshot_set_value(&stats_overlay_runtime_snapshot.render_time_avg_ms,
        stats_overlay_runtime_window.rendered_frames != 0,
        stats_overlay_runtime_window.rendered_frames != 0 ?
            stats_overlay_runtime_window.render_time_total_ms / stats_overlay_runtime_window.rendered_frames : 0.0);
    stats_overlay_snapshot_set_value(&stats_overlay_runtime_snapshot.queue_delay_avg_ms,
        stats_overlay_runtime_window.queue_delay_count != 0,
        stats_overlay_runtime_window.queue_delay_count != 0 ?
            stats_overlay_runtime_window.queue_delay_total_ms / stats_overlay_runtime_window.queue_delay_count : 0.0);

    if (stats_overlay_runtime_window.host_latency_count != 0) {
      stats_overlay_snapshot_set_value(&stats_overlay_runtime_snapshot.host_latency_min_ms, true, stats_overlay_runtime_window.host_latency_min_ms);
      stats_overlay_snapshot_set_value(&stats_overlay_runtime_snapshot.host_latency_max_ms, true, stats_overlay_runtime_window.host_latency_max_ms);
      stats_overlay_snapshot_set_value(&stats_overlay_runtime_snapshot.host_latency_avg_ms, true,
          stats_overlay_runtime_window.host_latency_total_ms / stats_overlay_runtime_window.host_latency_count);
    } else {
      stats_overlay_snapshot_set_value(&stats_overlay_runtime_snapshot.host_latency_min_ms, false, 0.0);
      stats_overlay_snapshot_set_value(&stats_overlay_runtime_snapshot.host_latency_max_ms, false, 0.0);
      stats_overlay_snapshot_set_value(&stats_overlay_runtime_snapshot.host_latency_avg_ms, false, 0.0);
    }

    if (video_stats != NULL && stats_overlay_runtime_window.have_last_video_stats) {
      uint32_t delta_total = (video_stats->packetCountVideo + video_stats->packetCountFec) -
          (stats_overlay_runtime_window.last_video_stats.packetCountVideo + stats_overlay_runtime_window.last_video_stats.packetCountFec);
      uint32_t delta_failed = video_stats->packetCountFecFailed - stats_overlay_runtime_window.last_video_stats.packetCountFecFailed;
      uint32_t delta_oos = video_stats->packetCountOOS - stats_overlay_runtime_window.last_video_stats.packetCountOOS;

      stats_overlay_snapshot_set_value(&stats_overlay_runtime_snapshot.network_drop_pct, delta_total != 0,
          delta_total != 0 ? (delta_failed * 100.0) / delta_total : 0.0);
      stats_overlay_snapshot_set_value(&stats_overlay_runtime_snapshot.jitter_drop_pct, delta_total != 0,
          delta_total != 0 ? (delta_oos * 100.0) / delta_total : 0.0);
    } else {
      stats_overlay_snapshot_set_value(&stats_overlay_runtime_snapshot.network_drop_pct, false, 0.0);
      stats_overlay_snapshot_set_value(&stats_overlay_runtime_snapshot.jitter_drop_pct, false, 0.0);
    }

    if (video_stats != NULL) {
      stats_overlay_runtime_window.last_video_stats = *video_stats;
      stats_overlay_runtime_window.have_last_video_stats = true;
    }

    if (LiGetEstimatedRttInfo(&rtt, &rtt_variance)) {
      stats_overlay_snapshot_set_value(&stats_overlay_runtime_snapshot.network_latency_avg_ms, true, rtt / 2.0);
      stats_overlay_snapshot_set_value(&stats_overlay_runtime_snapshot.network_latency_variance_ms, true, rtt_variance / 2.0);
    } else {
      stats_overlay_snapshot_set_value(&stats_overlay_runtime_snapshot.network_latency_avg_ms, false, 0.0);
      stats_overlay_snapshot_set_value(&stats_overlay_runtime_snapshot.network_latency_variance_ms, false, 0.0);
    }

    stats_overlay_window_reset(&stats_overlay_runtime_window, now_ms);
  }

  updated = stats_overlay_update(&stats_overlay_runtime_state_data, &stats_overlay_runtime_snapshot, now_ms);

  pthread_mutex_unlock(&stats_overlay_runtime_mutex);
  return updated;
}

/** Returns the current cached runtime overlay state for renderers. */
const STATS_OVERLAY_STATE* stats_overlay_runtime_state(void) {
  return &stats_overlay_runtime_state_data;
}

/** Returns whether the current session should display the overlay. */
bool stats_overlay_runtime_is_visible(void) {
  return stats_overlay_runtime_state_data.visible;
}
