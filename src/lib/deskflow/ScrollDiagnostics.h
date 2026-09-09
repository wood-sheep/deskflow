#pragma once

#include "base/Log.h"
#include "common/Settings.h"

#include <chrono>

namespace deskflow {

inline void logScrollTiming(const char *stage, int horizontal = 0, int vertical = 0)
{
  if (!Settings::value(Settings::Log::ScrollDiagnostics).toBool())
    return;
  const auto timestamp =
      std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch())
          .count();
  LOG_INFO("scroll.timing stage=%s us=%lld x=%d y=%d", stage, static_cast<long long>(timestamp), horizontal, vertical);
}

}
