#include "stats_overlay.h"

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
  stats_overlay_init(&state);
  stats_overlay_session_start(&state, &pref, &capability);

  assert(stats_overlay_update(&state, &snapshot, 1000) == true);
  assert(state.line_count == STATS_OVERLAY_MAX_LINES);
  assert(strstr(state.formatted_lines[0], "1920x1080") != NULL);
  assert(strstr(state.formatted_lines[0], "HEVC") != NULL);
  assert(strstr(state.formatted_lines[1], "Unavailable") != NULL);
  assert(strstr(state.formatted_lines[2], "59.50") != NULL);
  assert(strstr(state.formatted_lines[3], "60.00") != NULL);
  assert(stats_overlay_measure_width(&state) > 0);
  assert(stats_overlay_measure_height(&state) == STATS_OVERLAY_MAX_LINES * 8);
  assert(strstr(state.formatted_lines[10], "Unavailable") != NULL);
  assert(strcmp(state.formatted_lines[0], "Video stream: 1920x1080 60.00 FPS (Codec: HEVC)") == 0);
  assert(strcmp(state.formatted_lines[1], "Incoming frame rate from network: Unavailable") == 0);
  assert(strcmp(state.formatted_lines[2], "Decoding frame rate: 59.50") == 0);
  assert(strcmp(state.formatted_lines[3], "Rendering frame rate: 60.00") == 0);

  assert(stats_overlay_update(&state, &snapshot, 1200) == false);
  assert(stats_overlay_update(&state, &snapshot, 1600) == true);

  stats_overlay_capability_init(&capability, false, false, "Unsupported");
  stats_overlay_session_start(&state, &pref, &capability);
  assert(stats_overlay_should_warn(&state, &pref, &capability) == true);
  assert(stats_overlay_should_warn(&state, &pref, &capability) == false);

  return 0;
}
