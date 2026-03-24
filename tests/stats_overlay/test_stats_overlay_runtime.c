#include "stats_overlay.h"
#include "stats_overlay_font.h"

#include <assert.h>
#include <string.h>

/** Verifies formatting cadence, fixed line order, and unsupported-backend warnings. */
int main(void) {
  STATS_OVERLAY_PREFERENCE pref;
  STATS_OVERLAY_CAPABILITY capability;
  STATS_OVERLAY_SNAPSHOT snapshot;
  STATS_OVERLAY_STATE state;

  stats_overlay_pref_init(&pref);
  stats_overlay_pref_apply(&pref, true, STATS_OVERLAY_SOURCE_CLI);
  stats_overlay_capability_init(&capability, true, true, NULL);
  stats_overlay_snapshot_init(&snapshot);
  stats_overlay_snapshot_set_stream(&snapshot, 1920, 1080, 60.0, "HEVC");
  stats_overlay_snapshot_set_value(&snapshot.decoding_fps, true, 59.5);
  stats_overlay_snapshot_set_value(&snapshot.rendering_fps, true, 60.0);
  stats_overlay_snapshot_set_value(&snapshot.skipped_fps, true, 2.0);
  stats_overlay_snapshot_set_value(&snapshot.skipped_frames_total, true, 7.0);
  stats_overlay_snapshot_set_value(&snapshot.host_latency_avg_ms, true, 2.5);
  stats_overlay_snapshot_set_value(&snapshot.host_latency_min_ms, true, 2.0);
  stats_overlay_snapshot_set_value(&snapshot.host_latency_max_ms, true, 3.0);
  stats_overlay_snapshot_set_value(&snapshot.network_latency_avg_ms, true, 4.0);
  stats_overlay_snapshot_set_value(&snapshot.network_latency_variance_ms, true, 1.0);
  stats_overlay_snapshot_set_value(&snapshot.frame_assembly_delay_avg_ms, true, 5.0);
  stats_overlay_snapshot_set_value(&snapshot.observed_stream_to_display_latency_avg_ms, true, 19.5);
  stats_overlay_snapshot_set_value(&snapshot.estimated_end_to_end_latency_avg_ms, true, 56.83);
  stats_overlay_snapshot_set_value(&snapshot.decoder_backlog_latency_avg_ms, false, 0.0);
  stats_overlay_init(&state);
  stats_overlay_session_start(&state, &pref, &capability);

  assert(stats_overlay_update(&state, &snapshot, 1000) == true);
  assert(state.line_count == STATS_OVERLAY_MAX_LINES);
  assert(strstr(state.formatted_lines[0], "1920x1080") != NULL);
  assert(strstr(state.formatted_lines[0], "HEVC") != NULL);
  assert(strstr(state.formatted_lines[1], "Unavailable") != NULL);
  assert(strstr(state.formatted_lines[2], "59.50") != NULL);
  assert(strstr(state.formatted_lines[3], "60.00") != NULL);
  assert(strstr(state.formatted_lines[4], "2.00 FPS") != NULL);
  assert(strstr(state.formatted_lines[4], "7 total") != NULL);
  assert(strstr(state.formatted_lines[9], "5.00 ms") != NULL);
  assert(strstr(state.formatted_lines[10], "19.50 ms") != NULL);
  assert(strstr(state.formatted_lines[11], "56.83 ms") != NULL);
  assert(stats_overlay_measure_width(&state) > 0);
  assert(stats_overlay_measure_height(&state) == STATS_OVERLAY_MAX_LINES * STATS_OVERLAY_FONT_HEIGHT);
  assert(strstr(state.formatted_lines[12], "Unavailable") != NULL);
  assert(strcmp(state.formatted_lines[0], "Video stream: 1920x1080 60.00 FPS (Codec: HEVC)") == 0);
  assert(strcmp(state.formatted_lines[1], "Incoming frame rate from network: Unavailable") == 0);
  assert(strcmp(state.formatted_lines[2], "Decoding frame rate: 59.50") == 0);
  assert(strcmp(state.formatted_lines[3], "Rendering frame rate: 60.00") == 0);
  assert(strcmp(state.formatted_lines[4], "Skipped frames during decoder recovery: 2.00 FPS (7 total)") == 0);
  assert(strcmp(state.formatted_lines[9], "Average frame assembly delay: 5.00 ms") == 0);
  assert(strcmp(state.formatted_lines[10], "Observed stream-to-display latency: 19.50 ms") == 0);
  assert(strcmp(state.formatted_lines[11], "Estimated end-to-end latency (modeled): 56.83 ms") == 0);
  assert(strcmp(state.formatted_lines[12], "Decoder backlog latency: Unavailable") == 0);

  assert(stats_overlay_update(&state, &snapshot, 1200) == false);
  assert(stats_overlay_update(&state, &snapshot, 1600) == true);

  stats_overlay_capability_init(&capability, false, false, "Unsupported");
  stats_overlay_session_start(&state, &pref, &capability);
  assert(stats_overlay_should_warn(&state, &pref, &capability) == true);
  assert(stats_overlay_should_warn(&state, &pref, &capability) == false);

  return 0;
}
