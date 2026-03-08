#include "stats_overlay_font.h"
#include "stats_overlay.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/** Returns the maximum overlay line width in bitmap-font pixels. */
size_t stats_overlay_measure_width(const PSTATS_OVERLAY_STATE state) {
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
size_t stats_overlay_measure_height(const PSTATS_OVERLAY_STATE state) {
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
