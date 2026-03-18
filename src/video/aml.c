/*
 * This file is part of Moonlight Embedded.
 *
 * Copyright (C) 2015-2017 Iwan Timmer
 * Copyright (C) 2016 OtherCrashOverride, Daniel Mehrwald
 *
 * Moonlight is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Moonlight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Moonlight; if not, see <http://www.gnu.org/licenses/>.
 */

#include <Limelight.h>

#include <sys/utsname.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <codec.h>
#include <errno.h>
#include <pthread.h>
#include <sys/mman.h>
#include <poll.h>
#include <dlfcn.h>
#include <time.h>

#include <linux/videodev2.h>
#include <linux/fb.h>

#include "../stats_overlay.h"
#include "../util.h"
#include "video.h"

#define SYNC_OUTSIDE 0x02
#define UCODE_IP_ONLY_PARAM 0x08
#define MAX_WRITE_ATTEMPTS 5
#define EAGAIN_SLEEP_TIME 2 * 1000
#define AML_DQBUF_POLL_TIMEOUT_MS 20
#define AML_DEBUG_INTERVAL_US (1000ULL * 1000ULL)
#define AML_DEFAULT_DELAY_LIMIT_MS 16
#define AML_DEBUG_MILESTONE_COUNT 3
#define AML_LOW_LATENCY_PENDING_FRAMES 2
#define AML_MAX_PENDING_FRAMES 4
#define AML_FALLBACK_RESYNC_THRESHOLD_MS 24.0
#define AML_LOW_LATENCY_RESYNC_ARM_US (125ULL * 1000ULL)
#define AML_RESYNC_STARTUP_GRACE_US (5ULL * 1000ULL * 1000ULL)
#define AML_RESYNC_COOLDOWN_US (1000ULL * 1000ULL)
#define AML_STEADY_STATE_PENDING_FRAMES 1
#define AML_STARTUP_PENDING_FRAMES 2
#define AML_SUBMIT_THROTTLE_TIMEOUT_FRAMES 2ULL
#define AML_DEFAULT_FRAME_DURATION_US 16667ULL

static codec_para_t codecParam = { 0 };
static pthread_t displayThread;
static int videoFd = -1;
static volatile bool done = false;
static pthread_mutex_t pendingDecodeSubmitMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t pendingDecodeSubmitCond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t amlDebugMutex = PTHREAD_MUTEX_INITIALIZER;
static int overlayFd = -1;
static uint32_t* overlayPixels = NULL;
static size_t overlayMapSize = 0;
static size_t overlayStridePixels = 0;
static size_t overlayPageCount = 0;
static int overlayWidth = 0;
static int overlayHeight = 0;
static uint64_t pendingDecodeSubmitTimesUs[256];
static unsigned int pendingDecodeSubmitHead = 0;
static unsigned int pendingDecodeSubmitCount = 0;
static bool overlayEnabled = false;
static bool overlayReady = false;
static bool overlayWarningEmitted = false;
static uint32_t overlayFgColor = 0;
static uint32_t overlayBgColor = 0;
static int amlTargetDelayMs = AML_DEFAULT_DELAY_LIMIT_MS;
static bool amlDebugEnabled = false;
static uint64_t amlLastResyncRequestUs = 0;
static uint64_t amlLowLatencyResyncEligibleSinceUs = 0;
static bool amlAwaitingIdr = false;
static double amlLatestDecodeLatencyMs = 0.0;
static int amlConfiguredRateX100 = 0;
static unsigned int amlConfiguredFrameDurationTicks = 0;
static uint64_t amlConfiguredFrameDurationUs = AML_DEFAULT_FRAME_DURATION_US;
static int amlFracRatePolicy = -1;
static bool amlDisplayTrackingEnabled = false;
static bool amlPlaybackStarted = false;
void *pkt_buf = NULL;
size_t pkt_buf_size = 0;

typedef int (*AmlCodecSetVideoDelayLimitedMs)(codec_para_t*, int);
typedef int (*AmlCodecGetVideoCurDelayMs)(codec_para_t*, int*);
typedef int (*AmlCodecGetVideoCurDelayFrames)(codec_para_t*, int*);
typedef int (*AmlCodecDisableSlowsync)(codec_para_t*, int);
typedef int (*AmlCodecSetSyncEnable)(codec_para_t*, int);

typedef struct _AML_OPTIONAL_APIS {
  AmlCodecSetVideoDelayLimitedMs set_video_delay_limited_ms;
  AmlCodecGetVideoCurDelayMs get_video_cur_delay_ms;
  AmlCodecGetVideoCurDelayFrames get_video_cur_delay_frames;
  AmlCodecDisableSlowsync disable_slowsync;
  AmlCodecSetSyncEnable set_syncenable;
} AML_OPTIONAL_APIS;

typedef struct _AML_DEBUG_METRICS {
  uint64_t window_started_us;
  uint64_t codec_write_total_us;
  uint64_t codec_write_stall_us;
  uint64_t dqbuf_poll_timeout_count;
  unsigned int codec_write_count;
  unsigned int codec_write_eagain_count;
  unsigned int decoded_frame_count;
  unsigned int submit_fifo_depth_max;
  double decode_latency_total_ms;
  double decode_latency_max_ms;
  double video_delay_total_ms;
  unsigned int video_delay_sample_count;
  int video_delay_max_ms;
  int video_delay_last_ms;
  char display_mode[64];
} AML_DEBUG_METRICS;

typedef struct _AML_DEBUG_SNAPSHOT {
  bool captured;
  unsigned int elapsed_seconds;
  double frame_rate;
  double avg_decode_latency_ms;
  double max_decode_latency_ms;
  double avg_video_delay_ms;
  int max_video_delay_ms;
  int last_video_delay_ms;
  double avg_write_ms;
  double avg_stall_ms;
  unsigned int write_eagain_count;
  uint64_t dqbuf_poll_timeout_count;
  unsigned int submit_fifo_depth_max;
  char display_mode[64];
} AML_DEBUG_SNAPSHOT;

static AML_OPTIONAL_APIS amlOptionalApis;
static AML_DEBUG_METRICS amlDebugMetrics;
static AML_DEBUG_SNAPSHOT amlDebugSnapshots[AML_DEBUG_MILESTONE_COUNT];
static uint64_t amlDebugSessionStartedUs = 0;
static const unsigned int amlDebugMilestoneSeconds[AML_DEBUG_MILESTONE_COUNT] = { 60U, 180U, 300U };

/** Returns true when the session is running with debug logging enabled. */
static bool aml_debug_enabled(void) {
  return amlDebugEnabled;
}

/** Returns the configured one-frame video-delay target for the active stream. */
static int aml_target_delay_ms(int redrawRate) {
  if (redrawRate <= 0)
    return AML_DEFAULT_DELAY_LIMIT_MS;

  // Clamp the target to one frame so 60 FPS aims for roughly 16 ms while slower modes keep a realistic floor.
  return redrawRate > 1000 ? 1 : 1000 / redrawRate;
}

/** Resets the per-second AML debug counters while preserving static display metadata. */
static void aml_debug_reset_window(uint64_t now_us) {
  char display_mode[sizeof(amlDebugMetrics.display_mode)];

  // Preserve the cached display mode string so it can be printed in every summary without re-reading sysfs.
  snprintf(display_mode, sizeof(display_mode), "%s", amlDebugMetrics.display_mode);
  memset(&amlDebugMetrics, 0, sizeof(amlDebugMetrics));
  amlDebugMetrics.window_started_us = now_us;
  snprintf(amlDebugMetrics.display_mode, sizeof(amlDebugMetrics.display_mode), "%s", display_mode);
}

/** Stores the current one-second AML debug window as a milestone snapshot. */
static void aml_debug_fill_snapshot_locked(AML_DEBUG_SNAPSHOT* snapshot, uint64_t now_us, unsigned int elapsed_seconds) {
  double elapsed_window_seconds;

  // Keep the exit summary numerically comparable to the once-per-second AML debug log line.
  elapsed_window_seconds = amlDebugMetrics.window_started_us != 0 && now_us > amlDebugMetrics.window_started_us ?
      (now_us - amlDebugMetrics.window_started_us) / 1000000.0 : 0.0;

  snapshot->captured = true;
  snapshot->elapsed_seconds = elapsed_seconds;
  snapshot->frame_rate = elapsed_window_seconds > 0.0 ? amlDebugMetrics.decoded_frame_count / elapsed_window_seconds : 0.0;
  snapshot->avg_decode_latency_ms = amlDebugMetrics.decoded_frame_count != 0 ?
      amlDebugMetrics.decode_latency_total_ms / amlDebugMetrics.decoded_frame_count : 0.0;
  snapshot->max_decode_latency_ms = amlDebugMetrics.decode_latency_max_ms;
  snapshot->avg_video_delay_ms = amlDebugMetrics.video_delay_sample_count != 0 ?
      amlDebugMetrics.video_delay_total_ms / amlDebugMetrics.video_delay_sample_count : 0.0;
  snapshot->max_video_delay_ms = amlDebugMetrics.video_delay_max_ms;
  snapshot->last_video_delay_ms = amlDebugMetrics.video_delay_last_ms;
  snapshot->avg_write_ms = amlDebugMetrics.codec_write_count != 0 ?
      (amlDebugMetrics.codec_write_total_us / 1000.0) / amlDebugMetrics.codec_write_count : 0.0;
  snapshot->avg_stall_ms = amlDebugMetrics.codec_write_count != 0 ?
      (amlDebugMetrics.codec_write_stall_us / 1000.0) / amlDebugMetrics.codec_write_count : 0.0;
  snapshot->write_eagain_count = amlDebugMetrics.codec_write_eagain_count;
  snapshot->dqbuf_poll_timeout_count = amlDebugMetrics.dqbuf_poll_timeout_count;
  snapshot->submit_fifo_depth_max = amlDebugMetrics.submit_fifo_depth_max;
  snprintf(snapshot->display_mode, sizeof(snapshot->display_mode), "%s", amlDebugMetrics.display_mode);
}

/** Captures 1/3/5-minute AML debug snapshots the first time each milestone is crossed. */
static void aml_debug_capture_milestones_locked(uint64_t now_us) {
  unsigned int elapsed_seconds;
  unsigned int i;

  if (amlDebugSessionStartedUs == 0)
    return;

  elapsed_seconds = (unsigned int) ((now_us - amlDebugSessionStartedUs) / 1000000ULL);
  for (i = 0; i < AML_DEBUG_MILESTONE_COUNT; i++) {
    // Capture the first one-second window that reaches each milestone so the exit summary mirrors what the live logs showed then.
    if (!amlDebugSnapshots[i].captured && elapsed_seconds >= amlDebugMilestoneSeconds[i])
      aml_debug_fill_snapshot_locked(&amlDebugSnapshots[i], now_us, elapsed_seconds);
  }
}

/** Reads and trims a short AML display-mode string from sysfs when available. */
static bool aml_read_display_mode(char* buffer, size_t buffer_size) {
  static const char* paths[] = {
    "/sys/class/display/mode",
    "/sys/class/amhdmitx/amhdmitx0/disp_mode",
  };
  size_t i;

  for (i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
    char raw_mode[64];
    int raw_length;
    size_t len;

    raw_length = read_file((char*) paths[i], raw_mode, sizeof(raw_mode) - 1);
    if (raw_length <= 0)
      continue;

    raw_mode[raw_length] = '\0';
    len = strcspn(raw_mode, "\r\n");
    raw_mode[len] = '\0';
    if (raw_mode[0] == '\0')
      continue;

    snprintf(buffer, buffer_size, "%s", raw_mode);
    return true;
  }

  return false;
}

/** Parses the trailing refresh-rate token from an AML mode string into Hz x100. */
static bool aml_parse_refresh_rate_x100(const char* display_mode, int* refresh_rate_x100) {
  const char* hz;
  const char* rate_start;
  char* end;
  double parsed_rate;
  char rate_buffer[16];
  size_t rate_length;

  if (display_mode == NULL || refresh_rate_x100 == NULL)
    return false;

  hz = strstr(display_mode, "hz");
  if (hz == NULL)
    return false;

  rate_start = hz;
  while (rate_start > display_mode && (isdigit((unsigned char) rate_start[-1]) || rate_start[-1] == '.'))
    rate_start--;
  if (rate_start == hz)
    return false;

  // Copy the numeric suffix into a bounded buffer so strtod() can parse both integer and fractional rates.
  rate_length = (size_t) (hz - rate_start);
  if (rate_length >= sizeof(rate_buffer))
    return false;

  memcpy(rate_buffer, rate_start, rate_length);
  rate_buffer[rate_length] = '\0';
  parsed_rate = strtod(rate_buffer, &end);
  if (end == rate_buffer || *end != '\0' || parsed_rate <= 0.0)
    return false;

  *refresh_rate_x100 = (int) (parsed_rate * 100.0 + 0.5);
  return true;
}

/** Reads the AML fractional refresh-rate policy when the HDMI driver exposes it. */
static bool aml_read_frac_rate_policy(int* frac_rate_policy) {
  char buffer[16];
  int length;
  char* end;
  long parsed_value;

  if (frac_rate_policy == NULL)
    return false;

  length = read_file("/sys/class/amhdmitx/amhdmitx0/frac_rate_policy", buffer, sizeof(buffer) - 1);
  if (length <= 0)
    return false;

  buffer[length] = '\0';
  parsed_value = strtol(buffer, &end, 10);
  if (end == buffer)
    return false;

  *frac_rate_policy = (int) parsed_value;
  return true;
}

/** Returns the common fractional HDMI refresh rate x100 for a nominal integer mode. */
static int aml_fractional_refresh_x100_for_nominal(int nominal_refresh_hz) {
  switch (nominal_refresh_hz) {
  case 24:
    return 2398;
  case 30:
    return 2997;
  case 60:
    return 5994;
  case 120:
    return 11988;
  default:
    return 0;
  }
}

/** Detects the AML stream cadence in Hz x100, honoring fractional HDMI policy only when it matches the stream FPS. */
static int aml_detect_stream_rate_x100(int redrawRate) {
  char display_mode[64];
  int detected_rate_x100;
  int frac_rate_policy;

  if (redrawRate <= 0)
    return 0;

  detected_rate_x100 = redrawRate * 100;
  amlFracRatePolicy = -1;
  if (!aml_read_display_mode(display_mode, sizeof(display_mode)))
    return detected_rate_x100;
  if (!aml_parse_refresh_rate_x100(display_mode, &detected_rate_x100))
    return redrawRate * 100;

  // Only trust the display mode when it describes the same cadence as the requested stream FPS.
  if ((detected_rate_x100 + 50) / 100 != redrawRate)
    return redrawRate * 100;

  if (aml_read_frac_rate_policy(&frac_rate_policy)) {
    int fractional_rate_x100;

    amlFracRatePolicy = frac_rate_policy;
    if (frac_rate_policy != 0 && detected_rate_x100 == redrawRate * 100) {
      // AML often reports 1080p60hz while the HDMI block actually runs at 59.94 Hz under fractional mode.
      fractional_rate_x100 = aml_fractional_refresh_x100_for_nominal(redrawRate);
      if (fractional_rate_x100 != 0)
        detected_rate_x100 = fractional_rate_x100;
    }
  }

  return detected_rate_x100;
}

/** Converts the detected AML stream cadence into the 96 kHz frame-duration ticks expected by amcodec. */
static unsigned int aml_frame_duration_ticks(int redrawRate) {
  int detected_rate_x100 = aml_detect_stream_rate_x100(redrawRate);

  if (detected_rate_x100 <= 0)
    return redrawRate > 0 ? 96000U / (unsigned int) redrawRate : 1600U;

  amlConfiguredRateX100 = detected_rate_x100;
  return (96000U * 100U + (unsigned int) (detected_rate_x100 / 2)) / (unsigned int) detected_rate_x100;
}

/** Discovers optional AML low-latency helper entry points from the linked amcodec library. */
static void aml_optional_apis_init(void) {
  void* handle = dlopen(NULL, RTLD_LAZY);

  if (handle == NULL)
    return;

  // Resolve optional helpers from the already running process so the AML backend does not add new load-time dependencies.
  amlOptionalApis.set_video_delay_limited_ms = (AmlCodecSetVideoDelayLimitedMs) dlsym(handle, "codec_set_video_delay_limited_ms");
  amlOptionalApis.get_video_cur_delay_ms = (AmlCodecGetVideoCurDelayMs) dlsym(handle, "codec_get_video_cur_delay_ms");
  amlOptionalApis.get_video_cur_delay_frames = (AmlCodecGetVideoCurDelayFrames) dlsym(handle, "codec_get_video_cur_delay_frames");
  amlOptionalApis.disable_slowsync = (AmlCodecDisableSlowsync) dlsym(handle, "codec_disalbe_slowsync");
  amlOptionalApis.set_syncenable = (AmlCodecSetSyncEnable) dlsym(handle, "codec_set_syncenable");
  dlclose(handle);
}

/** Applies optional AML low-latency controls when the runtime amcodec build exposes them. */
static void aml_apply_latency_controls(void) {
  if (amlOptionalApis.set_syncenable != NULL) {
    int ret = amlOptionalApis.set_syncenable(&codecParam, 0);
    if (ret != 0 && aml_debug_enabled())
      printf("AML debug: codec_set_syncenable(0) failed: %d\n", ret);
  }

  if (amlOptionalApis.disable_slowsync != NULL) {
    int ret = amlOptionalApis.disable_slowsync(&codecParam, 1);
    if (ret != 0 && aml_debug_enabled())
      printf("AML debug: codec_disalbe_slowsync failed: %d\n", ret);
  }

  if (amlOptionalApis.set_video_delay_limited_ms != NULL) {
    int ret = amlOptionalApis.set_video_delay_limited_ms(&codecParam, amlTargetDelayMs);
    if (ret != 0 && aml_debug_enabled())
      printf("AML debug: codec_set_video_delay_limited_ms(%d) failed: %d\n", amlTargetDelayMs, ret);
  }
}

/** Updates the high-water mark for queued AML submit timestamps. */
static void aml_debug_note_submit_depth(unsigned int submit_depth) {
  pthread_mutex_lock(&amlDebugMutex);

  if (submit_depth > amlDebugMetrics.submit_fifo_depth_max)
    amlDebugMetrics.submit_fifo_depth_max = submit_depth;

  pthread_mutex_unlock(&amlDebugMutex);
}

/** Records the cost of a codec_write loop, including how much time was spent stalled on EAGAIN. */
static void aml_debug_note_codec_write(uint64_t write_elapsed_us, uint64_t stall_elapsed_us, unsigned int eagain_count) {
  pthread_mutex_lock(&amlDebugMutex);

  if (amlDebugMetrics.window_started_us == 0)
    amlDebugMetrics.window_started_us = LiGetMicroseconds();

  amlDebugMetrics.codec_write_total_us += write_elapsed_us;
  amlDebugMetrics.codec_write_stall_us += stall_elapsed_us;
  amlDebugMetrics.codec_write_eagain_count += eagain_count;
  amlDebugMetrics.codec_write_count++;

  pthread_mutex_unlock(&amlDebugMutex);
}

/** Notes that the AML display thread waited a poll interval without a decoded frame becoming ready. */
static void aml_debug_note_dqbuf_poll_timeout(void) {
  pthread_mutex_lock(&amlDebugMutex);

  if (amlDebugMetrics.window_started_us == 0)
    amlDebugMetrics.window_started_us = LiGetMicroseconds();

  amlDebugMetrics.dqbuf_poll_timeout_count++;

  pthread_mutex_unlock(&amlDebugMutex);
}

/** Stores the latest AML submit-to-output latency sample so recovery logic works even when debug logging is off. */
static void aml_record_decode_latency_sample(double decode_latency_ms) {
  pthread_mutex_lock(&amlDebugMutex);

  amlLatestDecodeLatencyMs = decode_latency_ms;

  pthread_mutex_unlock(&amlDebugMutex);
}

/** Records AML pipeline-latency samples and emits a once-per-second summary when debug is active. */
static void aml_debug_note_frame(double decode_latency_ms, int video_delay_ms) {
  uint64_t now_us = LiGetMicroseconds();

  pthread_mutex_lock(&amlDebugMutex);

  if (amlDebugMetrics.window_started_us == 0)
    aml_debug_reset_window(now_us);

  amlDebugMetrics.decoded_frame_count++;
  amlDebugMetrics.decode_latency_total_ms += decode_latency_ms;
  if (decode_latency_ms > amlDebugMetrics.decode_latency_max_ms)
    amlDebugMetrics.decode_latency_max_ms = decode_latency_ms;

  if (video_delay_ms >= 0) {
    amlDebugMetrics.video_delay_total_ms += video_delay_ms;
    amlDebugMetrics.video_delay_sample_count++;
    amlDebugMetrics.video_delay_last_ms = video_delay_ms;
    if (video_delay_ms > amlDebugMetrics.video_delay_max_ms)
      amlDebugMetrics.video_delay_max_ms = video_delay_ms;
  }

  aml_debug_capture_milestones_locked(now_us);

  if (aml_debug_enabled() && now_us >= amlDebugMetrics.window_started_us + AML_DEBUG_INTERVAL_US) {
    double elapsed_seconds = (now_us - amlDebugMetrics.window_started_us) / 1000000.0;
    double frame_rate = elapsed_seconds > 0.0 ? amlDebugMetrics.decoded_frame_count / elapsed_seconds : 0.0;
    double avg_decode_latency_ms = amlDebugMetrics.decoded_frame_count != 0 ?
        amlDebugMetrics.decode_latency_total_ms / amlDebugMetrics.decoded_frame_count : 0.0;
    double avg_write_ms = amlDebugMetrics.codec_write_count != 0 ?
        (amlDebugMetrics.codec_write_total_us / 1000.0) / amlDebugMetrics.codec_write_count : 0.0;
    double avg_stall_ms = amlDebugMetrics.codec_write_count != 0 ?
        (amlDebugMetrics.codec_write_stall_us / 1000.0) / amlDebugMetrics.codec_write_count : 0.0;
    double avg_video_delay_ms = amlDebugMetrics.video_delay_sample_count != 0 ?
        amlDebugMetrics.video_delay_total_ms / amlDebugMetrics.video_delay_sample_count : 0.0;

    // Emit one compact line so long-running Vero V sessions can reveal whether latency is drifting or being clamped.
    printf("AML debug: %.1f fps, submit->output avg %.2f ms max %.2f ms, "
           "amcodec delay avg %.2f ms max %d ms last %d ms, codec_write avg %.2f ms stall %.2f ms, "
           "write EAGAIN %u, DQBUF poll timeouts %llu, submit FIFO max %u%s%s\n",
        frame_rate, avg_decode_latency_ms, amlDebugMetrics.decode_latency_max_ms,
        avg_video_delay_ms, amlDebugMetrics.video_delay_max_ms, amlDebugMetrics.video_delay_last_ms,
        avg_write_ms, avg_stall_ms, amlDebugMetrics.codec_write_eagain_count,
        (unsigned long long) amlDebugMetrics.dqbuf_poll_timeout_count, amlDebugMetrics.submit_fifo_depth_max,
        amlDebugMetrics.display_mode[0] != '\0' ? ", display mode " : "",
        amlDebugMetrics.display_mode[0] != '\0' ? amlDebugMetrics.display_mode : "");
    aml_debug_reset_window(now_us);
  }

  pthread_mutex_unlock(&amlDebugMutex);
}

/** Prints one AML milestone snapshot in the same format as the live debug line. */
static void aml_debug_print_snapshot(const char* label, const AML_DEBUG_SNAPSHOT* snapshot) {
  if (!snapshot->captured) {
    printf("AML debug snapshot %s: not reached\n", label);
    return;
  }

  printf("AML debug snapshot %s (%us): %.1f fps, submit->output avg %.2f ms max %.2f ms, "
         "amcodec delay avg %.2f ms max %d ms last %d ms, codec_write avg %.2f ms stall %.2f ms, "
         "write EAGAIN %u, DQBUF poll timeouts %llu, submit FIFO max %u%s%s\n",
      label, snapshot->elapsed_seconds, snapshot->frame_rate,
      snapshot->avg_decode_latency_ms, snapshot->max_decode_latency_ms,
      snapshot->avg_video_delay_ms, snapshot->max_video_delay_ms, snapshot->last_video_delay_ms,
      snapshot->avg_write_ms, snapshot->avg_stall_ms, snapshot->write_eagain_count,
      (unsigned long long) snapshot->dqbuf_poll_timeout_count, snapshot->submit_fifo_depth_max,
      snapshot->display_mode[0] != '\0' ? ", display mode " : "",
      snapshot->display_mode[0] != '\0' ? snapshot->display_mode : "");
}

/** Logs the AML display mode and the availability of optional latency-control APIs. */
static void aml_debug_log_setup_state(void) {
  pthread_mutex_lock(&amlDebugMutex);

  memset(amlDebugSnapshots, 0, sizeof(amlDebugSnapshots));
  amlDebugSessionStartedUs = LiGetMicroseconds();
  if (!aml_read_display_mode(amlDebugMetrics.display_mode, sizeof(amlDebugMetrics.display_mode)))
    amlDebugMetrics.display_mode[0] = '\0';

  aml_debug_reset_window(amlDebugSessionStartedUs);
  pthread_mutex_unlock(&amlDebugMutex);

  if (!aml_debug_enabled())
    return;

  printf("AML debug: target video delay %d ms, overlay %s, optional APIs delay_limit=%s cur_delay_ms=%s "
         "cur_delay_frames=%s disable_slowsync=%s syncenable_off=%s, pts_checkin=on, source_rate=%d.%02d Hz "
         "(frame_duration=%u, frac_policy=%s)%s%s\n",
      amlTargetDelayMs, overlayEnabled ? "on" : "off",
      amlOptionalApis.set_video_delay_limited_ms != NULL ? "yes" : "no",
      amlOptionalApis.get_video_cur_delay_ms != NULL ? "yes" : "no",
      amlOptionalApis.get_video_cur_delay_frames != NULL ? "yes" : "no",
      amlOptionalApis.disable_slowsync != NULL ? "yes" : "no",
      amlOptionalApis.set_syncenable != NULL ? "yes" : "no",
      amlConfiguredRateX100 / 100, abs(amlConfiguredRateX100 % 100),
      amlConfiguredFrameDurationTicks,
      amlFracRatePolicy >= 0 ? (amlFracRatePolicy != 0 ? "on" : "off") : "unknown",
      amlDebugMetrics.display_mode[0] != '\0' ? ", display mode " : "",
      amlDebugMetrics.display_mode[0] != '\0' ? amlDebugMetrics.display_mode : "");
}

/** Prints the recorded AML 1/3/5-minute snapshots when the session exits. */
static void aml_debug_log_exit_summary(void) {
  AML_DEBUG_SNAPSHOT snapshots[AML_DEBUG_MILESTONE_COUNT];

  if (!aml_debug_enabled())
    return;

  pthread_mutex_lock(&amlDebugMutex);

  memcpy(snapshots, amlDebugSnapshots, sizeof(snapshots));

  pthread_mutex_unlock(&amlDebugMutex);

  printf("AML debug milestone summary:\n");
  aml_debug_print_snapshot("1m", &snapshots[0]);
  aml_debug_print_snapshot("3m", &snapshots[1]);
  aml_debug_print_snapshot("5m", &snapshots[2]);
}

/** Packs RGBA bytes into the active AML framebuffer channel layout. */
static uint32_t aml_pack_fb_color(const struct fb_var_screeninfo* var, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  uint32_t color = 0;
  uint32_t max_value;

  if (var->red.length != 0) {
    max_value = (1U << var->red.length) - 1U;
    color |= (((uint32_t) r * max_value) / 255U) << var->red.offset;
  }
  if (var->green.length != 0) {
    max_value = (1U << var->green.length) - 1U;
    color |= (((uint32_t) g * max_value) / 255U) << var->green.offset;
  }
  if (var->blue.length != 0) {
    max_value = (1U << var->blue.length) - 1U;
    color |= (((uint32_t) b * max_value) / 255U) << var->blue.offset;
  }
  if (var->transp.length != 0) {
    max_value = (1U << var->transp.length) - 1U;
    color |= (((uint32_t) a * max_value) / 255U) << var->transp.offset;
  }

  return color;
}

/** Emits a single warning when AML overlay composition cannot be initialized. */
static void aml_warn_overlay(const char* reason) {
  if (overlayWarningEmitted)
    return;

  fprintf(stderr, "Stats overlay unavailable on AML: %s\n", reason);
  overlayWarningEmitted = true;
}

/** Clears the mapped AML overlay framebuffer. */
static void aml_clear_overlay(void) {
  if (overlayPixels != NULL)
    memset(overlayPixels, 0, overlayMapSize);
}

/** Returns how long AML submit throttling should wait before falling back to the existing backlog recovery path. */
static uint64_t aml_submit_throttle_timeout_us(void) {
  return amlConfiguredFrameDurationUs != 0 ?
      amlConfiguredFrameDurationUs * AML_SUBMIT_THROTTLE_TIMEOUT_FRAMES :
      AML_DEFAULT_FRAME_DURATION_US * AML_SUBMIT_THROTTLE_TIMEOUT_FRAMES;
}

/** Returns the latency level where AML should abandon the current decoder history and resync to a fresh IDR. */
static double aml_resync_latency_threshold_ms(void) {
  return amlConfiguredFrameDurationUs != 0 ?
      (amlConfiguredFrameDurationUs * 3.0) / 2000.0 :
      AML_FALLBACK_RESYNC_THRESHOLD_MS;
}

/** Resets the FIFO of AML submit timestamps used to estimate hardware decode latency. */
static void aml_reset_decode_submit_times(void) {
  pthread_mutex_lock(&pendingDecodeSubmitMutex);

  pendingDecodeSubmitHead = 0;
  pendingDecodeSubmitCount = 0;
  amlPlaybackStarted = false;
  pthread_cond_broadcast(&pendingDecodeSubmitCond);

  pthread_mutex_unlock(&pendingDecodeSubmitMutex);

  pthread_mutex_lock(&amlDebugMutex);

  amlLatestDecodeLatencyMs = 0.0;

  pthread_mutex_unlock(&amlDebugMutex);
}

/** Marks AML playback as active once the display path starts draining frames. */
static void aml_mark_playback_started(void) {
  pthread_mutex_lock(&pendingDecodeSubmitMutex);

  amlPlaybackStarted = true;
  pthread_cond_broadcast(&pendingDecodeSubmitCond);

  pthread_mutex_unlock(&pendingDecodeSubmitMutex);
}

/** Waits briefly for AML to drain back to the desired low-latency queue depth before submitting another frame. */
static void aml_wait_for_submit_capacity(void) {
  struct timespec deadline;
  uint64_t wait_timeout_us;

  if (!amlDisplayTrackingEnabled)
    return;

  wait_timeout_us = aml_submit_throttle_timeout_us();
  if (clock_gettime(CLOCK_REALTIME, &deadline) != 0)
    return;

  deadline.tv_sec += (time_t) (wait_timeout_us / 1000000ULL);
  deadline.tv_nsec += (long) ((wait_timeout_us % 1000000ULL) * 1000ULL);
  if (deadline.tv_nsec >= 1000000000L) {
    deadline.tv_sec++;
    deadline.tv_nsec -= 1000000000L;
  }

  pthread_mutex_lock(&pendingDecodeSubmitMutex);

  // Allow a short two-frame startup pre-roll, then hold AML at one submitted frame in steady state.
  while (!done &&
         amlDisplayTrackingEnabled &&
         pendingDecodeSubmitCount >= (amlPlaybackStarted ? AML_STEADY_STATE_PENDING_FRAMES : AML_STARTUP_PENDING_FRAMES)) {
    if (pthread_cond_timedwait(&pendingDecodeSubmitCond, &pendingDecodeSubmitMutex, &deadline) == ETIMEDOUT)
      break;
  }

  pthread_mutex_unlock(&pendingDecodeSubmitMutex);
}

/** Queues the submit timestamp for a compressed frame entering the AML decoder pipeline. */
static void aml_push_decode_submit_time(uint64_t submit_time_us) {
  unsigned int index;
  unsigned int queue_depth;

  if (submit_time_us == 0)
    return;

  pthread_mutex_lock(&pendingDecodeSubmitMutex);

  if (pendingDecodeSubmitCount == (sizeof(pendingDecodeSubmitTimesUs) / sizeof(pendingDecodeSubmitTimesUs[0]))) {
    pendingDecodeSubmitHead = (pendingDecodeSubmitHead + 1) % (sizeof(pendingDecodeSubmitTimesUs) / sizeof(pendingDecodeSubmitTimesUs[0]));
    pendingDecodeSubmitCount--;
  }

  // The AML decoder outputs frames in decode order, so a FIFO gives the closest per-frame submit-to-output estimate available.
  index = (pendingDecodeSubmitHead + pendingDecodeSubmitCount) % (sizeof(pendingDecodeSubmitTimesUs) / sizeof(pendingDecodeSubmitTimesUs[0]));
  pendingDecodeSubmitTimesUs[index] = submit_time_us;
  pendingDecodeSubmitCount++;
  queue_depth = pendingDecodeSubmitCount;

  pthread_mutex_unlock(&pendingDecodeSubmitMutex);
  aml_debug_note_submit_depth(queue_depth);
}

/** Pops the oldest submit timestamp once the AML pipeline finishes a frame. */
static bool aml_pop_decode_submit_time(uint64_t* submit_time_us) {
  bool have_sample;

  pthread_mutex_lock(&pendingDecodeSubmitMutex);

  if (pendingDecodeSubmitCount == 0)
    have_sample = false;
  else {
    *submit_time_us = pendingDecodeSubmitTimesUs[pendingDecodeSubmitHead];
    pendingDecodeSubmitHead = (pendingDecodeSubmitHead + 1) % (sizeof(pendingDecodeSubmitTimesUs) / sizeof(pendingDecodeSubmitTimesUs[0]));
    pendingDecodeSubmitCount--;
    have_sample = true;
    pthread_cond_broadcast(&pendingDecodeSubmitCond);
  }

  pthread_mutex_unlock(&pendingDecodeSubmitMutex);
  return have_sample;
}

/** Returns the current number of AML frames queued between submit and display completion. */
static unsigned int aml_pending_submit_depth(void) {
  unsigned int pending_depth;

  pthread_mutex_lock(&pendingDecodeSubmitMutex);

  pending_depth = pendingDecodeSubmitCount;

  pthread_mutex_unlock(&pendingDecodeSubmitMutex);
  return pending_depth;
}

/** Returns the most recent AML submit-to-output latency sample in milliseconds. */
static double aml_latest_decode_latency_ms(void) {
  double latest_latency_ms;

  pthread_mutex_lock(&amlDebugMutex);

  latest_latency_ms = amlLatestDecodeLatencyMs;

  pthread_mutex_unlock(&amlDebugMutex);
  return latest_latency_ms;
}

/** Returns true when the AML pipeline backlog is high enough that low-latency recovery should kick in. */
static bool aml_should_request_resync(unsigned int pending_depth) {
  double latest_latency_ms;
  double resync_latency_threshold_ms;
  uint64_t now_us;

  if (!amlDisplayTrackingEnabled)
    return false;

  if (pending_depth < AML_LOW_LATENCY_PENDING_FRAMES) {
    amlLowLatencyResyncEligibleSinceUs = 0;
    return false;
  }

  now_us = LiGetMicroseconds();
  if (amlDebugSessionStartedUs != 0 && now_us < amlDebugSessionStartedUs + AML_RESYNC_STARTUP_GRACE_US) {
    amlLowLatencyResyncEligibleSinceUs = 0;
    return false;
  }
  if (amlLastResyncRequestUs != 0 && now_us < amlLastResyncRequestUs + AML_RESYNC_COOLDOWN_US) {
    amlLowLatencyResyncEligibleSinceUs = 0;
    return false;
  }

  latest_latency_ms = aml_latest_decode_latency_ms();
  resync_latency_threshold_ms = aml_resync_latency_threshold_ms();
  if (pending_depth < AML_MAX_PENDING_FRAMES && latest_latency_ms < resync_latency_threshold_ms) {
    amlLowLatencyResyncEligibleSinceUs = 0;
    return false;
  }

  if (amlLowLatencyResyncEligibleSinceUs == 0) {
    // Arm recovery only after the queue remains above the low-latency threshold long enough to rule out transient spikes.
    amlLowLatencyResyncEligibleSinceUs = now_us;
    return false;
  }
  if (now_us < amlLowLatencyResyncEligibleSinceUs + AML_LOW_LATENCY_RESYNC_ARM_US)
    return false;

  // Throttle reset/IDR requests so a single burst of late frames cannot trap the session in a reset loop.
  amlLastResyncRequestUs = now_us;
  amlLowLatencyResyncEligibleSinceUs = 0;
  return true;
}

/** Returns true while AML is waiting for a fresh IDR frame to resume low-latency playback cleanly. */
static bool aml_should_drop_for_resync(const PDECODE_UNIT decodeUnit) {
  if (!amlAwaitingIdr)
    return false;

  if (decodeUnit->frameType == FRAME_TYPE_IDR) {
    // Forget stale submit timestamps so the next latency samples start from the new decoder anchor frame.
    aml_reset_decode_submit_times();
    amlAwaitingIdr = false;
    if (aml_debug_enabled())
      printf("AML debug: received IDR after backlog drain, resuming normal submit path\n");
    return false;
  }

  return true;
}

/** Returns the number of vertically stacked framebuffer pages exposed by the AML OSD plane. */
static size_t aml_overlay_page_count(const struct fb_var_screeninfo* var) {
  if (var->yres == 0 || var->yres_virtual <= var->yres)
    return 1;

  // Android-style fbdev stacks commonly expose double or triple buffering by stacking pages vertically.
  return (var->yres_virtual + var->yres - 1) / var->yres;
}

/** Returns true when a framebuffer page lies fully inside the mapped AML OSD memory. */
static bool aml_overlay_page_fits(size_t page_index) {
  size_t page_row_offset = page_index * (size_t) overlayHeight * overlayStridePixels;
  size_t page_pixel_count;

  if (overlayPixels == NULL || overlayStridePixels == 0 || overlayHeight <= 0)
    return false;

  // The last row start plus one visible page must stay inside the mmap or we risk scribbling past the buffer.
  page_pixel_count = page_row_offset + ((size_t) overlayHeight * overlayStridePixels);
  return page_pixel_count <= overlayMapSize / sizeof(*overlayPixels);
}

/** Opens and maps an AML OSD framebuffer for stats overlay composition. */
static bool aml_overlay_init(void) {
  static const char* devices[] = { "/dev/fb0" };
  static const char* blank_paths[] = { "/sys/class/graphics/fb0/blank" };
  size_t i;

  for (i = 0; i < sizeof(devices) / sizeof(devices[0]); i++) {
    struct fb_fix_screeninfo fix = { 0 };
    struct fb_var_screeninfo var = { 0 };
    int fd = open(devices[i], O_RDWR);

    if (fd < 0)
      continue;
    if (ioctl(fd, FBIOGET_FSCREENINFO, &fix) < 0 || ioctl(fd, FBIOGET_VSCREENINFO, &var) < 0) {
      close(fd);
      continue;
    }
    if (var.bits_per_pixel != 32 || fix.line_length == 0 || var.xres == 0 || var.yres == 0) {
      close(fd);
      continue;
    }

    // Some AML kernels leave smem_len unset, so derive the mapping size from the virtual height when needed.
    overlayMapSize = fix.smem_len != 0 ? fix.smem_len : (size_t) fix.line_length * var.yres_virtual;
    overlayPixels = mmap(NULL, overlayMapSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (overlayPixels == MAP_FAILED) {
      overlayPixels = NULL;
      close(fd);
      continue;
    }

    overlayFd = fd;
    overlayStridePixels = fix.line_length / sizeof(uint32_t);
    overlayPageCount = aml_overlay_page_count(&var);
    overlayWidth = var.xres;
    overlayHeight = var.yres;
    overlayFgColor = aml_pack_fb_color(&var, 255, 255, 255, 255);
    overlayBgColor = aml_pack_fb_color(&var, 0, 0, 0, var.transp.length != 0 ? 160 : 255);

    // The AML video plane is separate from fb0, so keep fb0 visible for OSD composition.
    write_bool((char*) blank_paths[i], false);
    aml_clear_overlay();
    return true;
  }

  return false;
}

/** Tears down the mapped AML overlay framebuffer. */
static void aml_overlay_destroy(void) {
  if (overlayPixels != NULL) {
    aml_clear_overlay();
    munmap(overlayPixels, overlayMapSize);
    overlayPixels = NULL;
  }
  if (overlayFd >= 0) {
    close(overlayFd);
    overlayFd = -1;
  }
  overlayMapSize = 0;
  overlayStridePixels = 0;
  overlayPageCount = 0;
  overlayWidth = 0;
  overlayHeight = 0;
}

/** Draws the current stats overlay into the AML OSD framebuffer. */
static void aml_overlay_render(void) {
  size_t page_index;

  if (!overlayEnabled || !overlayReady || overlayPixels == NULL)
    return;

  // Refresh the formatted stats text on its normal cadence, but repaint the cached overlay every frame so
  // AML video-plane updates cannot leave the OSD temporarily blank between metric refreshes.
  stats_overlay_runtime_refresh();
  if (!stats_overlay_runtime_is_visible())
    return;

  // Some Amlogic fbdev stacks expose double or triple buffered OSD pages using yres_virtual/yoffset.
  // Mirror the cached overlay into every page so page flips cannot hide it between frames.
  for (page_index = 0; page_index < (overlayPageCount != 0 ? overlayPageCount : 1); page_index++) {
    uint32_t* page_pixels;

    if (!aml_overlay_page_fits(page_index))
      break;

    page_pixels = overlayPixels + (page_index * (size_t) overlayHeight * overlayStridePixels);
    stats_overlay_draw_argb32(page_pixels, overlayStridePixels, overlayWidth, overlayHeight,
        stats_overlay_runtime_state(), overlayFgColor, overlayBgColor);
  }
}

/** Waits for an AML decoded frame to become available without busy-spinning on VIDIOC_DQBUF. */
static bool aml_wait_for_frame_ready(void) {
  struct pollfd pfd = { 0 };

  pfd.fd = videoFd;
  pfd.events = POLLIN | POLLPRI;

  while (!done) {
    int ret = poll(&pfd, 1, AML_DQBUF_POLL_TIMEOUT_MS);

    if (ret > 0)
      return true;
    if (ret == 0) {
      if (aml_debug_enabled())
        aml_debug_note_dqbuf_poll_timeout();
      continue;
    }
    if (errno == EINTR)
      continue;

    fprintf(stderr, "poll() on AML video device failed: %d\n", errno);
    return false;
  }

  return false;
}

/** Drains AML display buffers and refreshes the OSD overlay once per presented frame. */
void* aml_display_thread(void* unused) {
  while (!done) {
    struct v4l2_buffer vbuf = { 0 };
    uint64_t frame_completed_us;
    uint64_t render_completed_us;
    uint64_t submit_started_us = 0;
    int video_delay_ms = -1;
    vbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (!aml_wait_for_frame_ready())
      break;

    if (ioctl(videoFd, VIDIOC_DQBUF, &vbuf) < 0) {
      if (errno == EAGAIN) {
        if (aml_debug_enabled())
          aml_debug_note_dqbuf_poll_timeout();
        continue;
      }
      fprintf(stderr, "VIDIOC_DQBUF failed: %d\n", errno);
      break;
    }

    // A dequeued AML capture buffer corresponds to a frame that has completed the hardware decode/display pipeline.
    frame_completed_us = LiGetMicroseconds();
    aml_mark_playback_started();
    if (aml_pop_decode_submit_time(&submit_started_us) && frame_completed_us >= submit_started_us) {
      double decode_latency_ms = (frame_completed_us - submit_started_us) / 1000.0;

      // Keep low-latency recovery armed from live pipeline samples regardless of whether verbose logging is enabled.
      aml_record_decode_latency_sample(decode_latency_ms);

      if (amlOptionalApis.get_video_cur_delay_ms != NULL &&
          amlOptionalApis.get_video_cur_delay_ms(&codecParam, &video_delay_ms) != 0) {
        video_delay_ms = -1;
      }

      // Amlogic does not expose per-frame decoder work time, so estimate it as submit-to-output pipeline time.
      // This includes hardware decode plus decoder-side buffering, which is a better latency proxy than codec_write() cost.
      if (overlayEnabled)
        stats_overlay_runtime_note_decoded_frame(decode_latency_ms);
      if (aml_debug_enabled())
        aml_debug_note_frame(decode_latency_ms, video_delay_ms);
    } else if (overlayEnabled) {
      stats_overlay_runtime_note_decoded_output();
    } else if (amlOptionalApis.get_video_cur_delay_ms != NULL &&
               amlOptionalApis.get_video_cur_delay_ms(&codecParam, &video_delay_ms) == 0 &&
               aml_debug_enabled()) {
      aml_debug_note_frame(0.0, video_delay_ms);
    }

    if (ioctl(videoFd, VIDIOC_QBUF, &vbuf) < 0) {
      fprintf(stderr, "VIDIOC_QBUF failed: %d\n", errno);
      break;
    }

    aml_overlay_render();
    render_completed_us = LiGetMicroseconds();
    // Use the dequeue timestamp as the frame completion point so queue delay includes AML decode buffering.
    // The small delta to render_completed_us only measures overlay/compositor bookkeeping after the frame finished.
    stats_overlay_runtime_note_render((render_completed_us - frame_completed_us) / 1000.0, frame_completed_us);
  }
  printf("Display thread terminated\n");
  return NULL;
}

/** Initializes the AML codec path and optional OSD overlay plane. */
int aml_setup(int videoFormat, int width, int height, int redrawRate, void* context, int drFlags) {
  PVIDEO_RENDERER_CONTEXT video_context = context;
  PSTATS_OVERLAY_PREFERENCE stats_pref = video_context != NULL ? video_context->stats_overlay : NULL;
  const char* codec = "H264";

  codecParam.handle             = -1;
  codecParam.cntl_handle        = -1;
  codecParam.audio_utils_handle = -1;
  codecParam.sub_handle         = -1;
  codecParam.has_video          = 1;
  codecParam.noblock            = 0;
  codecParam.stream_type        = STREAM_TYPE_ES_VIDEO;
  codecParam.am_sysinfo.param   = 0;
  done = false;
  videoFd = -1;
  amlDebugEnabled = video_context != NULL && video_context->debug_enabled;
  amlLastResyncRequestUs = 0;
  amlLowLatencyResyncEligibleSinceUs = 0;
  amlAwaitingIdr = false;
  amlLatestDecodeLatencyMs = 0.0;
  amlConfiguredRateX100 = redrawRate * 100;
  amlConfiguredFrameDurationTicks = 0;
  amlConfiguredFrameDurationUs = redrawRate > 0 ? (1000000ULL / (uint64_t) redrawRate) : AML_DEFAULT_FRAME_DURATION_US;
  amlFracRatePolicy = -1;
  amlDisplayTrackingEnabled = false;
  amlPlaybackStarted = false;
  amlTargetDelayMs = aml_target_delay_ms(redrawRate);
  aml_optional_apis_init();
  aml_reset_decode_submit_times();
  overlayWarningEmitted = false;
  overlayEnabled = stats_pref != NULL && stats_pref->enabled;
  overlayReady = false;

#ifdef STREAM_TYPE_FRAME
  codecParam.dec_mode           = STREAM_TYPE_FRAME;
#endif

#ifdef FRAME_BASE_PATH_AMLVIDEO_AMVIDEO
  codecParam.video_path         = FRAME_BASE_PATH_AMLVIDEO_AMVIDEO;
#endif

  if (videoFormat & VIDEO_FORMAT_MASK_H264) {
    if (width > 1920 || height > 1080) {
      codecParam.video_type = VFORMAT_H264_4K2K;
      codecParam.am_sysinfo.format = VIDEO_DEC_FORMAT_H264_4K2K;
    } else {
      codecParam.video_type = VFORMAT_H264;
      codecParam.am_sysinfo.format = VIDEO_DEC_FORMAT_H264;

      // Workaround for decoding special case of C1, 1080p, H264
      int major, minor;
      struct utsname name;
      uname(&name);
      int ret = sscanf(name.release, "%d.%d", &major, &minor);
      if (ret == 2 && !(major > 3 || (major == 3 && minor >= 14)) && width == 1920 && height == 1080)
          codecParam.am_sysinfo.param = (void*) UCODE_IP_ONLY_PARAM;
    }
  } else if (videoFormat & VIDEO_FORMAT_MASK_H265) {
    codec = "HEVC";
    codecParam.video_type = VFORMAT_HEVC;
    codecParam.am_sysinfo.format = VIDEO_DEC_FORMAT_HEVC;
#ifdef CODEC_TAG_AV1
  } else if (videoFormat & VIDEO_FORMAT_MASK_AV1) {
    codec = "AV1";
    codecParam.video_type = VFORMAT_AV1;
    codecParam.am_sysinfo.format = VIDEO_DEC_FORMAT_AV1;
#endif
  } else {
    printf("Video format not supported\n");
    return -1;
  }

  codecParam.am_sysinfo.width = width;
  codecParam.am_sysinfo.height = height;
  codecParam.am_sysinfo.rate = aml_frame_duration_ticks(redrawRate);
  amlConfiguredFrameDurationTicks = codecParam.am_sysinfo.rate;
  amlConfiguredFrameDurationUs = amlConfiguredRateX100 > 0 ?
      (100000000ULL + (uint64_t) (amlConfiguredRateX100 / 2)) / (uint64_t) amlConfiguredRateX100 :
      (redrawRate > 0 ? (1000000ULL / (uint64_t) redrawRate) : AML_DEFAULT_FRAME_DURATION_US);
  codecParam.am_sysinfo.param = (void*) ((size_t) codecParam.am_sysinfo.param | SYNC_OUTSIDE);

  if (overlayEnabled) {
    STATS_OVERLAY_CAPABILITY capability;

    stats_overlay_capability_init(&capability, true, true, NULL);
    stats_overlay_runtime_configure(stats_pref, &capability, width, height, redrawRate, codec);
    overlayReady = aml_overlay_init();
    if (!overlayReady) {
      aml_warn_overlay("no 32-bit OSD framebuffer could be mapped");
      stats_overlay_runtime_stop();
    }
  }

  int ret;
  if ((ret = codec_init(&codecParam)) != 0) {
    fprintf(stderr, "codec_init error: %x\n", ret);
    return -2;
  }

  if ((ret = codec_set_freerun_mode(&codecParam, 1)) != 0) {
    fprintf(stderr, "Can't set Freerun mode: %x\n", ret);
    return -2;
  }
  aml_apply_latency_controls();
  aml_debug_log_setup_state();

  char vfm_map[2048] = {};
  char* eol;
  if (read_file("/sys/class/vfm/map", vfm_map, sizeof(vfm_map) - 1) > 0 && (eol = strchr(vfm_map, '\n'))) {
    *eol = 0;

    // If amlvideo is in the pipeline, we must spawn a display thread
    printf("VFM map: %s\n", vfm_map);
    if (strstr(vfm_map, "amlvideo")) {
      printf("Using display thread for amlvideo pipeline\n");

      videoFd = open("/dev/video10", O_RDONLY | O_NONBLOCK);
      if (videoFd < 0) {
        fprintf(stderr, "Failed to open video device: %d\n", errno);
        return -3;
      }

      amlDisplayTrackingEnabled = true;
      pthread_create(&displayThread, NULL, aml_display_thread, NULL);
    }
  }

  ensure_buf_size(&pkt_buf, &pkt_buf_size, INITIAL_DECODER_BUFFER_SIZE);

  return 0;
}

/** Releases AML decode resources and any mapped overlay framebuffer. */
void aml_cleanup() {
  if (videoFd >= 0) {
    done = true;
    pthread_mutex_lock(&pendingDecodeSubmitMutex);
    amlDisplayTrackingEnabled = false;
    pthread_cond_broadcast(&pendingDecodeSubmitCond);
    pthread_mutex_unlock(&pendingDecodeSubmitMutex);
    pthread_join(displayThread, NULL);
    close(videoFd);
    videoFd = -1;
  }

  aml_debug_log_exit_summary();
  codec_close(&codecParam);
  free(pkt_buf);
  pkt_buf = NULL;
  pkt_buf_size = 0;
  aml_reset_decode_submit_times();
  aml_overlay_destroy();
  stats_overlay_runtime_stop();
}

/** Submits a decode unit to AML and records the live overlay timing samples. */
int aml_submit_decode_unit(PDECODE_UNIT decodeUnit) {
  unsigned int pending_depth;
  uint64_t write_started_us;
  uint64_t submit_started_us;
  uint64_t write_completed_us;
  uint64_t stalled_us = 0;
  unsigned int eagain_count = 0;

  ensure_buf_size(&pkt_buf, &pkt_buf_size, decodeUnit->fullLength);

  if (aml_should_drop_for_resync(decodeUnit)) {
    if (aml_debug_enabled())
      printf("AML debug: dropping non-IDR frame while waiting for backlog recovery IDR\n");
    return DR_OK;
  }

  pending_depth = aml_pending_submit_depth();
  if (aml_should_request_resync(pending_depth)) {
    if (aml_debug_enabled()) {
      // Ask the host for a fresh decoder anchor while letting the current AML pipeline drain to avoid visible reset artifacts.
      printf("AML debug: pending frame backlog reached %u, draining to the next IDR without decoder reset%s\n",
          pending_depth, decodeUnit->frameType == FRAME_TYPE_IDR ? "" : " and requesting IDR");
    }

    if (decodeUnit->frameType != FRAME_TYPE_IDR) {
      amlAwaitingIdr = true;
      LiRequestIdrFrame();
      return DR_OK;
    }

    aml_reset_decode_submit_times();
  }

  if (overlayEnabled)
    stats_overlay_runtime_note_decode_unit(decodeUnit);
  aml_wait_for_submit_capacity();

  int written = 0, length = 0, errCounter = 0, api;
  PLENTRY entry = decodeUnit->bufferList;
  do {
    memcpy(pkt_buf+length, entry->data, entry->length);
    length += entry->length;
    entry = entry->next;
  } while (entry != NULL);

  // AML amcodec expects PTS in milliseconds; keep feeding timestamps because some AML pipelines stall without them.
  codec_checkin_pts(&codecParam, decodeUnit->presentationTimeUs / 1000);
  submit_started_us = LiGetMicroseconds();
  write_started_us = submit_started_us;
  while (length > 0) {
    api = codec_write(&codecParam, pkt_buf+written, length);
    if (api < 0) {
      if (errno != EAGAIN) {
        fprintf(stderr, "codec_write() error: %x %d\n", errno, api);
        codec_reset(&codecParam);
        break;
      } else {
        uint64_t stall_started_us = LiGetMicroseconds();

        if (++errCounter == MAX_WRITE_ATTEMPTS) {
          fprintf(stderr, "codec_write() timeout\n");
          break;
        }
        eagain_count++;
        usleep(EAGAIN_SLEEP_TIME);
        stalled_us += LiGetMicroseconds() - stall_started_us;
      }
    } else {
      written += api;
      length -= api;
    }
  }
  write_completed_us = LiGetMicroseconds();

  if (aml_debug_enabled())
    aml_debug_note_codec_write(write_completed_us - write_started_us, stalled_us, eagain_count);

  if (amlDisplayTrackingEnabled && length == 0) {
    // Record the submit start time so the display thread can estimate AML pipeline latency even when the overlay is disabled.
    aml_push_decode_submit_time(submit_started_us);
  }

  return length ? DR_NEED_IDR : DR_OK;
}

DECODER_RENDERER_CALLBACKS decoder_callbacks_aml = {
  .setup = aml_setup,
  .cleanup = aml_cleanup,
  .submitDecodeUnit = aml_submit_decode_unit,

  // We may delay in aml_submit_decode_unit() for a while, so we can't set CAPABILITY_DIRECT_SUBMIT
  .capabilities = CAPABILITY_REFERENCE_FRAME_INVALIDATION_HEVC,
};
