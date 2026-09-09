#pragma once

#include "deskflow/GestureTypes.h"

#include <chrono>
#include <functional>

namespace deskflow {

class EiGestureHandler
{
public:
  using Clock = std::chrono::steady_clock;
  using SendKey = std::function<void(uint32_t, bool)>;

  explicit EiGestureHandler(SendKey sendKey);
  void handle(const GestureEvent &event, Clock::time_point now = Clock::now());
  void reset();
  bool isActive() const
  {
    return m_switcherActive;
  }

private:
  void tap(uint32_t key);

  SendKey m_sendKey;
  bool m_switcherActive = false;
  Clock::time_point m_lastUpdate{};
};

} // namespace deskflow
