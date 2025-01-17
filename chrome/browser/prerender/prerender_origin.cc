// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/prerender/prerender_origin.h"

#include "base/macros.h"
#include "base/metrics/histogram.h"
#include "chrome/browser/prerender/prerender_manager.h"

namespace prerender {

namespace {

const char* kOriginNames[] = {
  "[Deprecated] Link Rel Prerender (original)",
  "[Deprecated] Omnibox (original)",
  "GWS Prerender",
  "[Deprecated] Omnibox (conservative)",
  "[Deprecated] Omnibox (exact)",
  "Omnibox",
  "None",
  "Link Rel Prerender (same domain)",
  "Link Rel Prerender (cross domain)",
  "Local Predictor",
  "External Request",
  "Instant",
  "Link Rel Next",
  "Max",
};
static_assert(arraysize(kOriginNames) == ORIGIN_MAX + 1,
              "prerender origin name count mismatch");

}  // namespace

const char* NameFromOrigin(Origin origin) {
  DCHECK(static_cast<int>(origin) >= 0 &&
         origin <= ORIGIN_MAX);
  return kOriginNames[origin];
}

}  // namespace prerender
