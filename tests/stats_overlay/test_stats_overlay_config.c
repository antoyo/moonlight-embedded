#include "stats_overlay.h"

#include <assert.h>

/** Verifies preference defaulting, precedence, and session locking. */
int main(void) {
  STATS_OVERLAY_PREFERENCE pref;

  stats_overlay_pref_init(&pref);
  assert(pref.enabled == false);
  assert(pref.source == STATS_OVERLAY_SOURCE_DEFAULT);
  assert(pref.locked_for_session == false);

  stats_overlay_pref_apply(&pref, true, STATS_OVERLAY_SOURCE_CONFIG);
  assert(pref.enabled == true);
  assert(pref.source == STATS_OVERLAY_SOURCE_CONFIG);

  stats_overlay_pref_apply(&pref, false, STATS_OVERLAY_SOURCE_CLI);
  assert(pref.enabled == false);
  assert(pref.source == STATS_OVERLAY_SOURCE_CLI);

  stats_overlay_pref_lock(&pref);
  stats_overlay_pref_apply(&pref, true, STATS_OVERLAY_SOURCE_CONFIG);
  assert(pref.enabled == false);
  assert(pref.source == STATS_OVERLAY_SOURCE_CLI);

  stats_overlay_pref_unlock(&pref);
  stats_overlay_pref_apply(&pref, true, STATS_OVERLAY_SOURCE_CLI);
  assert(pref.enabled == true);
  assert(pref.source == STATS_OVERLAY_SOURCE_CLI);

  return 0;
}
