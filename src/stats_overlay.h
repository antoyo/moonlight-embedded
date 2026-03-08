#ifndef MOONLIGHT_STATS_OVERLAY_H
#define MOONLIGHT_STATS_OVERLAY_H

#include <Limelight.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define STATS_OVERLAY_MAX_LINES 11
#define STATS_OVERLAY_MAX_LINE_LENGTH 192

enum stats_overlay_option_source {
  STATS_OVERLAY_SOURCE_DEFAULT = 0,
  STATS_OVERLAY_SOURCE_CONFIG = 1,
  STATS_OVERLAY_SOURCE_CLI = 2,
};

typedef struct _STATS_OVERLAY_PREFERENCE {
  bool enabled;
  enum stats_overlay_option_source source;
  bool locked_for_session;
} STATS_OVERLAY_PREFERENCE, *PSTATS_OVERLAY_PREFERENCE;

typedef struct _STATS_OVERLAY_VALUE {
  bool available;
  double value;
} STATS_OVERLAY_VALUE, *PSTATS_OVERLAY_VALUE;

typedef struct _STATS_OVERLAY_SNAPSHOT {
  int video_width;
  int video_height;
  double video_fps;
  char codec[16];

  STATS_OVERLAY_VALUE incoming_network_fps;
  STATS_OVERLAY_VALUE decoding_fps;
  STATS_OVERLAY_VALUE rendering_fps;
  STATS_OVERLAY_VALUE host_latency_min_ms;
  STATS_OVERLAY_VALUE host_latency_max_ms;
  STATS_OVERLAY_VALUE host_latency_avg_ms;
  STATS_OVERLAY_VALUE network_drop_pct;
  STATS_OVERLAY_VALUE jitter_drop_pct;
  STATS_OVERLAY_VALUE network_latency_avg_ms;
  STATS_OVERLAY_VALUE network_latency_variance_ms;
  STATS_OVERLAY_VALUE decode_time_avg_ms;
  STATS_OVERLAY_VALUE queue_delay_avg_ms;
  STATS_OVERLAY_VALUE render_time_avg_ms;

  uint64_t updated_at_ms;
} STATS_OVERLAY_SNAPSHOT, *PSTATS_OVERLAY_SNAPSHOT;

typedef struct _STATS_OVERLAY_CAPABILITY {
  bool supports_overlay;
  bool supports_partial_metrics;
  const char* unsupported_reason;
} STATS_OVERLAY_CAPABILITY, *PSTATS_OVERLAY_CAPABILITY;

typedef struct _STATS_OVERLAY_STATE {
  bool visible;
  bool warning_emitted;
  unsigned int refresh_interval_ms;
  unsigned int line_count;
  uint64_t last_format_ms;
  char formatted_lines[STATS_OVERLAY_MAX_LINES][STATS_OVERLAY_MAX_LINE_LENGTH];
} STATS_OVERLAY_STATE, *PSTATS_OVERLAY_STATE;

void stats_overlay_pref_init(PSTATS_OVERLAY_PREFERENCE pref);
void stats_overlay_pref_apply(PSTATS_OVERLAY_PREFERENCE pref, bool enabled, enum stats_overlay_option_source source);
void stats_overlay_pref_lock(PSTATS_OVERLAY_PREFERENCE pref);
void stats_overlay_pref_unlock(PSTATS_OVERLAY_PREFERENCE pref);

void stats_overlay_snapshot_init(PSTATS_OVERLAY_SNAPSHOT snapshot);
void stats_overlay_snapshot_set_stream(PSTATS_OVERLAY_SNAPSHOT snapshot, int width, int height, double fps, const char* codec);
void stats_overlay_snapshot_set_value(PSTATS_OVERLAY_VALUE value, bool available, double metric);

void stats_overlay_capability_init(PSTATS_OVERLAY_CAPABILITY capability, bool supports_overlay, bool supports_partial_metrics, const char* unsupported_reason);

void stats_overlay_init(PSTATS_OVERLAY_STATE state);
void stats_overlay_session_start(PSTATS_OVERLAY_STATE state, const PSTATS_OVERLAY_PREFERENCE pref, const PSTATS_OVERLAY_CAPABILITY capability);
void stats_overlay_session_stop(PSTATS_OVERLAY_STATE state);
bool stats_overlay_should_warn(PSTATS_OVERLAY_STATE state, const PSTATS_OVERLAY_PREFERENCE pref, const PSTATS_OVERLAY_CAPABILITY capability);
bool stats_overlay_update(PSTATS_OVERLAY_STATE state, const PSTATS_OVERLAY_SNAPSHOT snapshot, uint64_t now_ms);
size_t stats_overlay_measure_width(const STATS_OVERLAY_STATE* state);
size_t stats_overlay_measure_height(const STATS_OVERLAY_STATE* state);
void stats_overlay_clear_box(uint32_t* pixels, size_t stride_pixels, size_t width, size_t height, uint32_t color);
void stats_overlay_draw_yuv420(uint8_t* const planes[3], const int linesize[3], int width, int height, const STATS_OVERLAY_STATE* state);

void stats_overlay_runtime_configure(const PSTATS_OVERLAY_PREFERENCE pref, const PSTATS_OVERLAY_CAPABILITY capability, int width, int height, double fps, const char* codec);
void stats_overlay_runtime_stop(void);
void stats_overlay_runtime_note_decode_unit(const PDECODE_UNIT decode_unit);
void stats_overlay_runtime_note_decoded_frame(double decode_time_ms);
void stats_overlay_runtime_note_render(double render_time_ms, uint64_t render_completed_us);
bool stats_overlay_runtime_refresh(void);
const STATS_OVERLAY_STATE* stats_overlay_runtime_state(void);
bool stats_overlay_runtime_is_visible(void);

#endif
