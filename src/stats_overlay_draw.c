#include "stats_overlay_font.h"
#include "stats_overlay.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/** Returns the 5-bit glyph row for a supported overlay character. */
static uint8_t stats_overlay_glyph_row(char ch, unsigned int row) {
  switch (ch) {
  case 'A': case 'a': { static const uint8_t g[] = { 0x04, 0x0A, 0x11, 0x1F, 0x11, 0x11, 0x11 }; return g[row]; }
  case 'B': case 'b': { static const uint8_t g[] = { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E }; return g[row]; }
  case 'C': case 'c': { static const uint8_t g[] = { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E }; return g[row]; }
  case 'D': case 'd': { static const uint8_t g[] = { 0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C }; return g[row]; }
  case 'E': case 'e': { static const uint8_t g[] = { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F }; return g[row]; }
  case 'F': case 'f': { static const uint8_t g[] = { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 }; return g[row]; }
  case 'G': case 'g': { static const uint8_t g[] = { 0x0F, 0x10, 0x10, 0x17, 0x11, 0x11, 0x0F }; return g[row]; }
  case 'H': case 'h': { static const uint8_t g[] = { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 }; return g[row]; }
  case 'I': case 'i': { static const uint8_t g[] = { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E }; return g[row]; }
  case 'J': case 'j': { static const uint8_t g[] = { 0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0E }; return g[row]; }
  case 'K': case 'k': { static const uint8_t g[] = { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 }; return g[row]; }
  case 'L': case 'l': { static const uint8_t g[] = { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F }; return g[row]; }
  case 'M': case 'm': { static const uint8_t g[] = { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 }; return g[row]; }
  case 'N': case 'n': { static const uint8_t g[] = { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 }; return g[row]; }
  case 'O': case 'o': { static const uint8_t g[] = { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E }; return g[row]; }
  case 'P': case 'p': { static const uint8_t g[] = { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 }; return g[row]; }
  case 'Q': case 'q': { static const uint8_t g[] = { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D }; return g[row]; }
  case 'R': case 'r': { static const uint8_t g[] = { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 }; return g[row]; }
  case 'S': case 's': { static const uint8_t g[] = { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E }; return g[row]; }
  case 'T': case 't': { static const uint8_t g[] = { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 }; return g[row]; }
  case 'U': case 'u': { static const uint8_t g[] = { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E }; return g[row]; }
  case 'V': case 'v': { static const uint8_t g[] = { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 }; return g[row]; }
  case 'W': case 'w': { static const uint8_t g[] = { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A }; return g[row]; }
  case 'X': case 'x': { static const uint8_t g[] = { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 }; return g[row]; }
  case 'Y': case 'y': { static const uint8_t g[] = { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 }; return g[row]; }
  case 'Z': case 'z': { static const uint8_t g[] = { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F }; return g[row]; }
  case '0': { static const uint8_t g[] = { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E }; return g[row]; }
  case '1': { static const uint8_t g[] = { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E }; return g[row]; }
  case '2': { static const uint8_t g[] = { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F }; return g[row]; }
  case '3': { static const uint8_t g[] = { 0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E }; return g[row]; }
  case '4': { static const uint8_t g[] = { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 }; return g[row]; }
  case '5': { static const uint8_t g[] = { 0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E }; return g[row]; }
  case '6': { static const uint8_t g[] = { 0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E }; return g[row]; }
  case '7': { static const uint8_t g[] = { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 }; return g[row]; }
  case '8': { static const uint8_t g[] = { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E }; return g[row]; }
  case '9': { static const uint8_t g[] = { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E }; return g[row]; }
  case ' ': { static const uint8_t g[] = { 0, 0, 0, 0, 0, 0, 0 }; return g[row]; }
  case ':': { static const uint8_t g[] = { 0, 0x04, 0, 0, 0x04, 0, 0 }; return g[row]; }
  case '.': { static const uint8_t g[] = { 0, 0, 0, 0, 0, 0x06, 0x06 }; return g[row]; }
  case ',': { static const uint8_t g[] = { 0, 0, 0, 0, 0, 0x04, 0x08 }; return g[row]; }
  case '-': { static const uint8_t g[] = { 0, 0, 0, 0x1F, 0, 0, 0 }; return g[row]; }
  case '/': { static const uint8_t g[] = { 0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10 }; return g[row]; }
  case '(': { static const uint8_t g[] = { 0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02 }; return g[row]; }
  case ')': { static const uint8_t g[] = { 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08 }; return g[row]; }
  case '%': { static const uint8_t g[] = { 0x19, 0x19, 0x02, 0x04, 0x08, 0x13, 0x13 }; return g[row]; }
  case '=': { static const uint8_t g[] = { 0, 0x1F, 0, 0, 0x1F, 0, 0 }; return g[row]; }
  default: { static const uint8_t g[] = { 0x1F, 0x01, 0x02, 0x04, 0x04, 0, 0x04 }; return g[row]; }
  }
}

/** Writes a filled rectangle into the luma plane while clipping to frame bounds. */
static void stats_overlay_fill_y(uint8_t* y_plane, int stride, int frame_width, int frame_height,
    int x0, int y0, int box_width, int box_height, uint8_t value) {
  int x;
  int y;

  for (y = 0; y < box_height; y++) {
    int py = y0 + y;
    if (py < 0 || py >= frame_height)
      continue;

    for (x = 0; x < box_width; x++) {
      int px = x0 + x;
      if (px < 0 || px >= frame_width)
        continue;

      y_plane[py * stride + px] = value;
    }
  }
}

/** Draws a single bitmap glyph into the luma plane. */
static void stats_overlay_draw_glyph_y(uint8_t* y_plane, int stride, int frame_width, int frame_height, int x0, int y0, char ch) {
  unsigned int row;
  unsigned int col;

  for (row = 0; row < 7; row++) {
    uint8_t bits = stats_overlay_glyph_row(ch, row);
    for (col = 0; col < 5; col++) {
      if (bits & (1U << (4 - col)))
        // Expand each bitmap pixel into a 3x3 block so the text stays readable on TVs and fullscreen sessions.
        stats_overlay_fill_y(y_plane, stride, frame_width, frame_height,
            x0 + ((int) col * STATS_OVERLAY_FONT_SCALE),
            y0 + ((int) row * STATS_OVERLAY_FONT_SCALE),
            STATS_OVERLAY_FONT_SCALE, STATS_OVERLAY_FONT_SCALE, 235);
    }
  }
}

/** Returns the maximum overlay line width in bitmap-font pixels. */
size_t stats_overlay_measure_width(const STATS_OVERLAY_STATE* state) {
  size_t max_length = 0;
  size_t i;

  // The widest cached line determines the reserved draw box for all backends.
  for (i = 0; i < state->line_count; i++) {
    size_t line_length = strlen(state->formatted_lines[i]);
    if (line_length > max_length)
      max_length = line_length;
  }

  return max_length * STATS_OVERLAY_FONT_WIDTH;
}

/** Returns the total overlay height in bitmap-font pixels. */
size_t stats_overlay_measure_height(const STATS_OVERLAY_STATE* state) {
  return state->line_count * STATS_OVERLAY_FONT_HEIGHT;
}

/** Fills a rectangular pixel region with a single color. */
void stats_overlay_clear_box(uint32_t* pixels, size_t stride_pixels, size_t width, size_t height, uint32_t color) {
  size_t x;
  size_t y;

  // Clearing the entire box avoids stale glyph pixels when the text shrinks between refreshes.
  for (y = 0; y < height; y++) {
    for (x = 0; x < width; x++)
      pixels[y * stride_pixels + x] = color;
  }
}

/** Draws the formatted overlay text directly onto a YUV420 frame. */
void stats_overlay_draw_yuv420(uint8_t* const planes[3], const int linesize[3], int width, int height, const STATS_OVERLAY_STATE* state) {
  int margin = 8 * STATS_OVERLAY_FONT_SCALE;
  int line;
  int text_width = (int)stats_overlay_measure_width(state);
  int text_height = (int)stats_overlay_measure_height(state);
  int clear_width = (STATS_OVERLAY_MAX_LINE_LENGTH * STATS_OVERLAY_FONT_WIDTH) + (8 * STATS_OVERLAY_FONT_SCALE);
  int clear_height = (STATS_OVERLAY_MAX_LINES * STATS_OVERLAY_FONT_HEIGHT) + (8 * STATS_OVERLAY_FONT_SCALE);

  if (state->line_count == 0 || planes[0] == NULL || linesize[0] <= 0)
    return;

  // Clear the full reserved overlay region so shorter future lines cannot leave stale pixels behind.
  stats_overlay_fill_y(planes[0], linesize[0], width, height, margin - (4 * STATS_OVERLAY_FONT_SCALE),
      margin - (4 * STATS_OVERLAY_FONT_SCALE), clear_width, clear_height, 24);

  // Draw a dark backing box sized to the currently formatted content for good contrast.
  stats_overlay_fill_y(planes[0], linesize[0], width, height, margin - (4 * STATS_OVERLAY_FONT_SCALE),
      margin - (4 * STATS_OVERLAY_FONT_SCALE), text_width + (8 * STATS_OVERLAY_FONT_SCALE),
      text_height + (8 * STATS_OVERLAY_FONT_SCALE), 24);

  // Render each line into the luma plane using the fixed-width bitmap font.
  for (line = 0; line < (int) state->line_count; line++) {
    int x = margin;
    int y = margin + (line * STATS_OVERLAY_FONT_HEIGHT);
    const char* cursor = state->formatted_lines[line];

    while (*cursor != '\0') {
      stats_overlay_draw_glyph_y(planes[0], linesize[0], width, height, x, y, *cursor);
      x += STATS_OVERLAY_FONT_WIDTH;
      cursor++;
    }
  }
}

/** Draws the formatted overlay text into a 32-bit framebuffer-backed ARGB plane. */
void stats_overlay_draw_argb32(uint32_t* pixels, size_t stride_pixels, int width, int height, const STATS_OVERLAY_STATE* state,
    uint32_t fg_color, uint32_t bg_color) {
  int margin = 8 * STATS_OVERLAY_FONT_SCALE;
  int line;
  int clear_width = STATS_OVERLAY_MAX_LINE_LENGTH * STATS_OVERLAY_FONT_WIDTH;

  if (state->line_count == 0 || pixels == NULL || stride_pixels == 0)
    return;

  for (line = 0; line < (int) state->line_count; line++) {
    int x = margin;
    int y = margin + (line * STATS_OVERLAY_FONT_HEIGHT);
    int line_width = (int) strlen(state->formatted_lines[line]) * STATS_OVERLAY_FONT_WIDTH;
    int row;
    const char* cursor = state->formatted_lines[line];

    // Repaint only the area covered by the current line so glyph changes do not leave stale pixels behind.
    for (row = 0; row < STATS_OVERLAY_FONT_HEIGHT; row++) {
      int draw_y = y + row;
      int col;

      if (draw_y < 0 || draw_y >= height)
        continue;

      for (col = 0; col < line_width; col++) {
        int draw_x = x + col;

        if (draw_x >= 0 && draw_x < width)
          pixels[(draw_y * stride_pixels) + draw_x] = bg_color;
      }
    }

    // Clear only the trailing portion of the reserved line width so shorter updates do not blink the full overlay box.
    for (row = 0; row < STATS_OVERLAY_FONT_HEIGHT; row++) {
      int draw_y = y + row;
      int col;

      if (draw_y < 0 || draw_y >= height)
        continue;

      for (col = line_width; col < clear_width; col++) {
        int draw_x = x + col;

        if (draw_x >= 0 && draw_x < width)
          pixels[(draw_y * stride_pixels) + draw_x] = 0;
      }
    }

    while (*cursor != '\0') {
      unsigned int row;
      unsigned int col;

      for (row = 0; row < 7; row++) {
        uint8_t bits = stats_overlay_glyph_row(*cursor, row);
        for (col = 0; col < 5; col++) {
          size_t py;
          size_t px;

          if ((bits & (1U << (4 - col))) == 0)
            continue;

          for (py = 0; py < STATS_OVERLAY_FONT_SCALE; py++) {
            for (px = 0; px < STATS_OVERLAY_FONT_SCALE; px++) {
              int draw_x = x + ((int) col * STATS_OVERLAY_FONT_SCALE) + (int) px;
              int draw_y = y + ((int) row * STATS_OVERLAY_FONT_SCALE) + (int) py;

              if (draw_x >= 0 && draw_x < width && draw_y >= 0 && draw_y < height)
                pixels[(draw_y * stride_pixels) + draw_x] = fg_color;
            }
          }
        }
      }

      x += STATS_OVERLAY_FONT_WIDTH;
      cursor++;
    }
  }

  for (line = (int) state->line_count; line < STATS_OVERLAY_MAX_LINES; line++) {
    int y = margin + (line * STATS_OVERLAY_FONT_HEIGHT);
    int row;

    // Clear now-unused lines so stale text disappears without wiping the active lines above.
    for (row = 0; row < STATS_OVERLAY_FONT_HEIGHT; row++) {
      int draw_y = y + row;
      int col;

      if (draw_y < 0 || draw_y >= height)
        continue;

      for (col = 0; col < clear_width; col++) {
        int draw_x = margin + col;

        if (draw_x >= 0 && draw_x < width)
          pixels[(draw_y * stride_pixels) + draw_x] = 0;
      }
    }
  }
}
