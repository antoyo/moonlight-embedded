/*
 * This file is part of Moonlight Embedded.
 *
 * Copyright (C) 2015-2017 Iwan Timmer
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

#define _GNU_SOURCE

#include "platform.h"

#include "util.h"

#include "audio/audio.h"
#include "video/video.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <dlfcn.h>

typedef bool(*ImxInit)();

/** Reads the active AML display mode string from sysfs. */
static bool platform_aml_read_display_mode(char* buffer, size_t buffer_size) {
  static const char* paths[] = {
    "/sys/class/display/mode",
    "/sys/class/amhdmitx/amhdmitx0/disp_mode",
  };
  size_t i;

  for (i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
    char raw_mode[64];
    int raw_length = read_file((char*) paths[i], raw_mode, sizeof(raw_mode) - 1);
    size_t trimmed_length;

    if (raw_length <= 0)
      continue;

    raw_mode[raw_length] = '\0';
    trimmed_length = strcspn(raw_mode, "\r\n");
    raw_mode[trimmed_length] = '\0';
    if (raw_mode[0] == '\0')
      continue;

    snprintf(buffer, buffer_size, "%s", raw_mode);
    return true;
  }

  return false;
}

/** Parses the refresh-rate suffix from an AML display mode string into Hz x100. */
static bool platform_aml_parse_refresh_rate_x100(const char* display_mode, int* refresh_rate_x100) {
  const char* hz;
  const char* rate_start;
  char rate_buffer[16];
  char* end;
  double parsed_rate;
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

  // Copy only the trailing numeric rate token so strtod() can parse both 60 and 59.94 mode strings.
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

/** Reads the AML HDMI fractional refresh-rate policy when the driver exposes it. */
static bool platform_aml_read_frac_rate_policy(int* frac_rate_policy) {
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

/** Returns the common fractional AML HDMI refresh rate x100 for a nominal integer mode. */
static int platform_aml_fractional_refresh_x100_for_nominal(int nominal_refresh_hz) {
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

/** Detects the AML display refresh rate in Hz x100 using both the mode string and fractional-rate policy. */
static int platform_aml_detect_refresh_rate_x100(void) {
  char display_mode[64];
  int refresh_rate_x100;
  int frac_rate_policy;

  if (!platform_aml_read_display_mode(display_mode, sizeof(display_mode)))
    return 0;
  if (!platform_aml_parse_refresh_rate_x100(display_mode, &refresh_rate_x100))
    return 0;

  if (platform_aml_read_frac_rate_policy(&frac_rate_policy) &&
      frac_rate_policy != 0 &&
      refresh_rate_x100 % 100 == 0) {
    int fractional_rate_x100 = platform_aml_fractional_refresh_x100_for_nominal(refresh_rate_x100 / 100);

    // AML commonly reports 1080p60hz while the HDMI block actually runs at 59.94 Hz with fractional mode enabled.
    if (fractional_rate_x100 != 0)
      return fractional_rate_x100;
  }

  return refresh_rate_x100;
}

/** Selects the first supported runtime backend that matches the requested name. */
enum platform platform_check(char* name) {
  bool std = strcmp(name, "auto") == 0;
  #ifdef HAVE_IMX
  if (std || strcmp(name, "imx") == 0) {
    void *handle = dlopen("libmoonlight-imx.so", RTLD_NOW | RTLD_GLOBAL);
    ImxInit video_imx_init = (ImxInit) dlsym(RTLD_DEFAULT, "video_imx_init");
    if (handle != NULL) {
      if (video_imx_init())
        return IMX;
    }
  }
  #endif
  #ifdef HAVE_PI
  if (std || strcmp(name, "pi") == 0) {
    void *handle = dlopen("libmoonlight-pi.so", RTLD_NOW | RTLD_GLOBAL);
    if (handle != NULL && dlsym(RTLD_DEFAULT, "bcm_host_init") != NULL)
      return PI;
  }
  #endif
  #ifdef HAVE_MMAL
  if (std || strcmp(name, "mmal") == 0) {
    void *handle = dlopen("libmoonlight-mmal.so", RTLD_NOW | RTLD_GLOBAL);
    if (handle != NULL && dlsym(RTLD_DEFAULT, "bcm_host_init") != NULL)
      return MMAL;
  }
  #endif
  #ifdef HAVE_AML
  if (std || strcmp(name, "aml") == 0) {
    int amvideo_available;
    void *handle = dlopen("libmoonlight-aml.so", RTLD_LAZY | RTLD_GLOBAL);
    amvideo_available = access("/dev/amvideo", F_OK);
    if (handle != NULL && amvideo_available != -1)
      return AML;
    if (!std) {
      if (handle == NULL) {
        const char* err = dlerror();
        fprintf(stderr, "Failed to load libmoonlight-aml.so: %s\n", err != NULL ? err : "unknown dlopen error");
      } else if (amvideo_available == -1) {
        fprintf(stderr, "AML backend loaded but /dev/amvideo is unavailable\n");
      }
    }
  }
  #endif
  #ifdef HAVE_ROCKCHIP
  if (std || strcmp(name, "rk") == 0) {
    void *handle = dlopen("libmoonlight-rk.so", RTLD_NOW | RTLD_GLOBAL);
    if (handle != NULL && dlsym(RTLD_DEFAULT, "mpp_init") != NULL)
      return RK;
  }
  #endif
  #ifdef HAVE_X11
  bool x11 = strcmp(name, "x11") == 0;
  bool vdpau = strcmp(name, "x11_vdpau") == 0;
  bool vaapi = strcmp(name, "x11_vaapi") == 0;
  if (std || x11 || vdpau || vaapi) {
    int init = x11_init(std || vdpau, std || vaapi);
    #ifdef HAVE_VAAPI
    if (init == INIT_VAAPI)
      return X11_VAAPI;
    #endif
    #ifdef HAVE_VDPAU
    if (init == INIT_VDPAU)
      return X11_VDPAU;
    #endif
    #ifdef HAVE_SDL
    return SDL;
    #else
    return X11;
    #endif
  }
  #endif
  #ifdef HAVE_SDL
  if (std || strcmp(name, "sdl") == 0)
    return SDL;
  #endif

  if (strcmp(name, "fake") == 0)
    return FAKE;

  return 0;
}

/** Applies backend-specific display setup before streaming starts. */
void platform_start(enum platform system) {
  switch (system) {
  #ifdef HAVE_AML
  case AML:
    write_bool("/sys/class/graphics/fb0/blank", true);
    //write_bool("/sys/class/graphics/fb1/blank", true);
    write_bool("/sys/class/video/disable_video", false);
    break;
  #endif
  #if defined(HAVE_PI) || defined(HAVE_MMAL)
  case PI:
    write_bool("/sys/class/graphics/fb0/blank", true);
    break;
  #endif
  }
}

/** Restores backend-specific display state after streaming stops. */
void platform_stop(enum platform system) {
  switch (system) {
  #ifdef HAVE_AML
  case AML:
    write_bool("/sys/class/graphics/fb0/blank", false);
    //write_bool("/sys/class/graphics/fb1/blank", false);
    break;
  #endif
  #if defined(HAVE_PI) || defined(HAVE_MMAL)
  case PI:
    write_bool("/sys/class/graphics/fb0/blank", false);
    break;
  #endif
  }
}

/** Returns the video renderer callbacks for the chosen backend. */
DECODER_RENDERER_CALLBACKS* platform_get_video(enum platform system) {
  switch (system) {
  #ifdef HAVE_X11
  case X11:
    return &decoder_callbacks_x11;
  #ifdef HAVE_VAAPI
  case X11_VAAPI:
    return &decoder_callbacks_x11_vaapi;
  #endif
  #ifdef HAVE_VDPAU
  case X11_VDPAU:
    return &decoder_callbacks_x11_vdpau;
  #endif
  #endif
  #ifdef HAVE_SDL
  case SDL:
    return &decoder_callbacks_sdl;
  #endif
  #ifdef HAVE_IMX
  case IMX:
    return (PDECODER_RENDERER_CALLBACKS) dlsym(RTLD_DEFAULT, "decoder_callbacks_imx");
  #endif
  #ifdef HAVE_PI
  case PI:
    return (PDECODER_RENDERER_CALLBACKS) dlsym(RTLD_DEFAULT, "decoder_callbacks_pi");
  #endif
  #ifdef HAVE_MMAL
  case MMAL:
    return (PDECODER_RENDERER_CALLBACKS) dlsym(RTLD_DEFAULT, "decoder_callbacks_mmal");
  #endif
  #ifdef HAVE_AML
  case AML:
    return (PDECODER_RENDERER_CALLBACKS) dlsym(RTLD_DEFAULT, "decoder_callbacks_aml");
  #endif
  #ifdef HAVE_ROCKCHIP
  case RK:
    return (PDECODER_RENDERER_CALLBACKS) dlsym(RTLD_DEFAULT, "decoder_callbacks_rk");
  #endif
  }
  return NULL;
}

/** Returns the audio renderer callbacks for the chosen backend. */
AUDIO_RENDERER_CALLBACKS* platform_get_audio(enum platform system, char* audio_device) {
  switch (system) {
  case FAKE:
      return NULL;
  #ifdef HAVE_SDL
  case SDL:
    return &audio_callbacks_sdl;
  #endif
  #ifdef HAVE_PI
  case PI:
    if (audio_device == NULL || strcmp(audio_device, "local") == 0 || strcmp(audio_device, "hdmi") == 0)
      return (PAUDIO_RENDERER_CALLBACKS) dlsym(RTLD_DEFAULT, "audio_callbacks_omx");
    // fall-through
  #endif
  default:
    // Prefer PulseAudio when available, then fall back to the platform default sink.
    #ifdef HAVE_PULSE
    if (audio_pulse_init(audio_device))
      return &audio_callbacks_pulse;
    #endif
    #ifdef HAVE_ALSA
    return &audio_callbacks_alsa;
    #endif
    #ifdef __FreeBSD__
    return &audio_callbacks_oss;
    #endif
  }
  return NULL;
}

/** Reports whether a backend is a good fit for the requested codec. */
bool platform_prefers_codec(enum platform system, enum codecs codec) {
  switch (codec) {
  case CODEC_H264:
    // H.264 is always supported
    return true;
  case CODEC_HEVC:
    switch (system) {
    case AML:
    case RK:
    case X11_VAAPI:
    case X11_VDPAU:
      return true;
    }
    return false;
  case CODEC_AV1:
    return false;
  }
  return false;
}

/** Returns a user-facing name for the chosen backend. */
char* platform_name(enum platform system) {
  switch(system) {
  case PI:
    return "Raspberry Pi (Broadcom)";
  case MMAL:
    return "Raspberry Pi (Broadcom) MMAL";
  case IMX:
    return "i.MX6 (MXC Vivante)";
  case AML:
    return "AMLogic VPU";
  case RK:
    return "Rockchip VPU";
  case X11:
    return "X Window System (software decoding)";
  case X11_VAAPI:
    return "X Window System (VAAPI)";
  case X11_VDPAU:
    return "X Window System (VDPAU)";
  case SDL:
    return "SDL2 (software decoding)";
  case FAKE:
    return "Fake (no a/v output)";
  default:
    return "Unknown";
  }
}

/** Describes whether the chosen backend can composite the stats overlay. */
void platform_get_overlay_capability(enum platform system, PSTATS_OVERLAY_CAPABILITY capability) {
  switch (system) {
  case SDL:
  case X11:
  case AML:
    stats_overlay_capability_init(capability, true, true, NULL);
    break;
  case X11_VDPAU:
  case X11_VAAPI:
    stats_overlay_capability_init(capability, false, true, "The selected X11 hardware-decoding backend cannot safely composite the stats overlay.");
    break;
  default:
    stats_overlay_capability_init(capability, false, false, "This backend does not support the stats overlay yet.");
    break;
  }
}

/** Returns the client's measured display refresh rate x100 when the backend can detect it. */
int platform_get_client_refresh_rate_x100(enum platform system) {
  switch (system) {
  case AML:
    // The AML detector combines the mode string with frac_rate_policy, so it can distinguish true 59.94 Hz output from nominal 60 Hz labels.
    return platform_aml_detect_refresh_rate_x100();
  default:
    return 0;
  }
}

/** Returns a backend-specific stream FPS that better matches the active display cadence. */
int platform_get_recommended_stream_fps(enum platform system, int requested_fps) {
  switch (system) {
  case AML: {
    int refresh_rate_x100 = platform_aml_detect_refresh_rate_x100();

    if (requested_fps == 60 && refresh_rate_x100 >= 5990 && refresh_rate_x100 <= 5998)
      return 59;
    return requested_fps;
  }
  default:
    return requested_fps;
  }
}
