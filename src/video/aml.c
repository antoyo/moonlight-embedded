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
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <codec.h>
#include <errno.h>
#include <pthread.h>
#include <sys/mman.h>

#include <linux/videodev2.h>
#include <linux/fb.h>

#include "../stats_overlay.h"
#include "../util.h"
#include "video.h"

#define SYNC_OUTSIDE 0x02
#define UCODE_IP_ONLY_PARAM 0x08
#define MAX_WRITE_ATTEMPTS 5
#define EAGAIN_SLEEP_TIME 2 * 1000

static codec_para_t codecParam = { 0 };
static pthread_t displayThread;
static int videoFd = -1;
static volatile bool done = false;
static int overlayFd = -1;
static uint32_t* overlayPixels = NULL;
static size_t overlayMapSize = 0;
static size_t overlayStridePixels = 0;
static int overlayWidth = 0;
static int overlayHeight = 0;
static bool overlayEnabled = false;
static bool overlayReady = false;
static bool overlayWarningEmitted = false;
static uint32_t overlayFgColor = 0;
static uint32_t overlayBgColor = 0;
void *pkt_buf = NULL;
size_t pkt_buf_size = 0;

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
  overlayWidth = 0;
  overlayHeight = 0;
}

/** Draws the current stats overlay into the AML OSD framebuffer. */
static void aml_overlay_render(void) {
  if (!overlayEnabled || !overlayReady || overlayPixels == NULL)
    return;

  // Refresh the formatted stats text on its normal cadence, but repaint the cached overlay every frame so
  // AML video-plane updates cannot leave the OSD temporarily blank between metric refreshes.
  stats_overlay_runtime_refresh();
  if (stats_overlay_runtime_is_visible())
    stats_overlay_draw_argb32(overlayPixels, overlayStridePixels, overlayWidth, overlayHeight,
        stats_overlay_runtime_state(), overlayFgColor, overlayBgColor);
}

/** Drains AML display buffers and refreshes the OSD overlay once per presented frame. */
void* aml_display_thread(void* unused) {
  while (!done) {
    struct v4l2_buffer vbuf = { 0 };
    uint64_t render_started_us;
    uint64_t render_completed_us;
    vbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (ioctl(videoFd, VIDIOC_DQBUF, &vbuf) < 0) {
      if (errno == EAGAIN) {
        usleep(500);
        continue;
      }
      fprintf(stderr, "VIDIOC_DQBUF failed: %d\n", errno);
      break;
    }

    render_started_us = LiGetMicroseconds();
    if (ioctl(videoFd, VIDIOC_QBUF, &vbuf) < 0) {
      fprintf(stderr, "VIDIOC_QBUF failed: %d\n", errno);
      break;
    }

    aml_overlay_render();
    render_completed_us = LiGetMicroseconds();
    // LiGetMicroseconds() returns microseconds, so divide by 1000 to store AML present cost in milliseconds.
    stats_overlay_runtime_note_render((render_completed_us - render_started_us) / 1000.0, render_completed_us);
  }
  printf("Display thread terminated\n");
  return NULL;
}

/** Initializes the AML codec path and optional OSD overlay plane. */
int aml_setup(int videoFormat, int width, int height, int redrawRate, void* context, int drFlags) {
  PSTATS_OVERLAY_PREFERENCE stats_pref = context;
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
  codecParam.am_sysinfo.rate = 96000 / redrawRate;
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
    pthread_join(displayThread, NULL);
    close(videoFd);
  }

  codec_close(&codecParam);
  free(pkt_buf);
  aml_overlay_destroy();
  stats_overlay_runtime_stop();
}

/** Submits a decode unit to AML and records the live overlay timing samples. */
int aml_submit_decode_unit(PDECODE_UNIT decodeUnit) {
  uint64_t decode_started_us;

  ensure_buf_size(&pkt_buf, &pkt_buf_size, decodeUnit->fullLength);
  if (overlayEnabled)
    stats_overlay_runtime_note_decode_unit(decodeUnit);

  int written = 0, length = 0, errCounter = 0, api;
  PLENTRY entry = decodeUnit->bufferList;
  do {
    memcpy(pkt_buf+length, entry->data, entry->length);
    length += entry->length;
    entry = entry->next;
  } while (entry != NULL);

  // AML amcodec expects PTS in milliseconds, so convert the Moonlight microsecond timestamp by dividing by 1000.
  // presentationTimeUs is in microseconds; amcodec expects milliseconds.
  codec_checkin_pts(&codecParam, decodeUnit->presentationTimeUs / 1000);
  decode_started_us = LiGetMicroseconds();
  while (length > 0) {
    api = codec_write(&codecParam, pkt_buf+written, length);
    if (api < 0) {
      if (errno != EAGAIN) {
        fprintf(stderr, "codec_write() error: %x %d\n", errno, api);
        codec_reset(&codecParam);
        break;
      } else {
        if (++errCounter == MAX_WRITE_ATTEMPTS) {
          fprintf(stderr, "codec_write() timeout\n");
          break;
        }
        usleep(EAGAIN_SLEEP_TIME);
      }
    } else {
      written += api;
      length -= api;
    }
  }

  if (overlayEnabled) {
    // LiGetMicroseconds() returns microseconds, so divide by 1000 to store AML submit cost in milliseconds.
    stats_overlay_runtime_note_decoded_frame((LiGetMicroseconds() - decode_started_us) / 1000.0);
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
