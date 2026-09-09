#include "platform/EiGestureHandler.h"

#include <linux/input-event-codes.h>
#include <utility>

namespace deskflow {

EiGestureHandler::EiGestureHandler(SendKey sendKey) : m_sendKey(std::move(sendKey))
{
}

void EiGestureHandler::tap(uint32_t key)
{
  m_sendKey(key, true);
  m_sendKey(key, false);
}

void EiGestureHandler::reset()
{
  if (m_switcherActive) {
    m_sendKey(KEY_LEFTALT, false);
    m_switcherActive = false;
  }
}

void EiGestureHandler::handle(const GestureEvent &event, Clock::time_point now)
{
  if (event.fingers != 3)
    return;

  if (event.phase == GesturePhase::Cancel) {
    reset();
    return;
  }

  const bool horizontal = event.type == GestureType::SwipeLeft || event.type == GestureType::SwipeRight;
  if (horizontal) {
    switch (event.phase) {
    case GesturePhase::Begin:
      if (m_switcherActive)
        return;
      m_switcherActive = true;
      m_lastUpdate = now;
      m_sendKey(KEY_LEFTALT, true);
      if (event.type == GestureType::SwipeLeft)
        m_sendKey(KEY_LEFTSHIFT, true);
      tap(KEY_TAB);
      if (event.type == GestureType::SwipeLeft)
        m_sendKey(KEY_LEFTSHIFT, false);
      break;
    case GesturePhase::Update:
      if (!m_switcherActive || now - m_lastUpdate < std::chrono::milliseconds(80))
        return;
      m_lastUpdate = now;
      tap(event.type == GestureType::SwipeLeft ? KEY_LEFT : KEY_RIGHT);
      break;
    case GesturePhase::End:
    case GesturePhase::Cancel:
      reset();
      break;
    }
    return;
  }

  if (event.phase != GesturePhase::End)
    return;

  reset();
  if (event.type != GestureType::SwipeUp && event.type != GestureType::SwipeDown)
    return;

  m_sendKey(KEY_LEFTMETA, true);
  tap(event.type == GestureType::SwipeUp ? KEY_W : KEY_D);
  m_sendKey(KEY_LEFTMETA, false);
}

} // namespace deskflow
