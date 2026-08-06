/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/OSXGestureCapture.h"

#include "base/Event.h"
#include "base/EventTypes.h"
#include "base/IEventQueue.h"
#include "base/Log.h"
#include "common/Settings.h"
#include "deskflow/GestureTypes.h"

#include <AppKit/NSEvent.h>

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <utility>

namespace
{
constexpr int kSwipeThreshold = 60;

uint8_t countTouches(NSEvent *event)
{
  NSSet *touches = [event touchesMatchingPhase:NSTouchPhaseAny inView:nil];
  return static_cast<uint8_t>(touches != nil ? touches.count : 0);
}

GestureType typeForDelta(int32_t deltaX, int32_t deltaY)
{
  if (std::abs(deltaX) >= std::abs(deltaY)) {
    return deltaX < 0 ? GestureType::SwipeLeft : GestureType::SwipeRight;
  }
  return deltaY < 0 ? GestureType::SwipeUp : GestureType::SwipeDown;
}
}

OSXGestureCapture::OSXGestureCapture(IEventQueue *events, void *eventTarget)
    : m_events(events),
      m_eventTarget(eventTarget)
{
}

OSXGestureCapture::OSXGestureCapture(GestureHandler handler) : m_handler(std::move(handler))
{
}

OSXGestureCapture::~OSXGestureCapture()
{
  stop();
}

bool OSXGestureCapture::start()
{
  if (m_globalMonitor != nullptr || m_localMonitor != nullptr) {
    return true;
  }

  const NSEventMask mask = NSEventMaskGesture | NSEventMaskSwipe | NSEventMaskBeginGesture | NSEventMaskEndGesture;

  id globalMonitor = [NSEvent addGlobalMonitorForEventsMatchingMask:mask handler:^(NSEvent *event) {
    this->handleEvent((void *)event);
  }];
  id localMonitor = [NSEvent addLocalMonitorForEventsMatchingMask:mask handler:^NSEvent *(NSEvent *event) {
    this->handleEvent((void *)event);
    return event;
  }];

  m_globalMonitor = (void *)globalMonitor;
  m_localMonitor = (void *)localMonitor;
  LOGC(
      Settings::value(Settings::Log::GestureDiagnostics).toBool(),
      (CLOG_INFO "gesture.capture monitors installed global=%d local=%d", globalMonitor != nil, localMonitor != nil)
  );
  return globalMonitor != nil || localMonitor != nil;
}

void OSXGestureCapture::stop()
{
  if (m_globalMonitor != nullptr) {
    [NSEvent removeMonitor:(id)m_globalMonitor];
    m_globalMonitor = nullptr;
  }
  if (m_localMonitor != nullptr) {
    [NSEvent removeMonitor:(id)m_localMonitor];
    m_localMonitor = nullptr;
  }

  m_tracking = false;
  m_fingers = 0;
  m_deltaX = 0;
  m_deltaY = 0;
}

void OSXGestureCapture::handleEvent(void *eventData)
{
  auto *event = (NSEvent *)eventData;
  const auto type = event.type;

  // AppKit deliberately does not expose the touch collection to a global
  // monitor. NSEventTypeSwipe is already the system's classified swipe
  // gesture, so treating it as a three-finger gesture avoids dropping every
  // event because touchesMatchingPhase: returns an empty set outside a view.
  if (type == NSEventTypeSwipe) {
    const auto deltaX = static_cast<int32_t>(event.deltaX);
    const auto deltaY = static_cast<int32_t>(event.deltaY);
    if (deltaX == 0 && deltaY == 0) {
      LOGC(
          Settings::value(Settings::Log::GestureDiagnostics).toBool(),
          (CLOG_INFO "gesture.capture swipe ignored: no direction")
      );
      return;
    }

    ++m_sequence;
    LOGC(
        Settings::value(Settings::Log::GestureDiagnostics).toBool(),
        (CLOG_INFO "gesture.capture swipe delta=%d,%d sequence=%u", deltaX, deltaY, m_sequence)
    );
    emitGesture(
        static_cast<int>(typeForDelta(deltaX, deltaY)), static_cast<int>(GesturePhase::End), 3,
        static_cast<int16_t>(std::clamp(deltaX, INT16_MIN, INT16_MAX)),
        static_cast<int16_t>(std::clamp(deltaY, INT16_MIN, INT16_MAX))
    );
    return;
  }

  const auto fingers = countTouches(event);

  if (type == NSEventTypeBeginGesture) {
    m_tracking = fingers == 3;
    m_fingers = fingers;
    m_deltaX = 0;
    m_deltaY = 0;
    ++m_sequence;
    if (m_tracking) {
      emitGesture(
          static_cast<int>(GestureType::SwipeRight), static_cast<int>(GesturePhase::Begin), m_fingers, 0, 0
      );
    }
    return;
  }

  if (type == NSEventTypeEndGesture) {
    if (m_tracking) {
      if (std::abs(m_deltaX) >= kSwipeThreshold || std::abs(m_deltaY) >= kSwipeThreshold) {
        emitGesture(
            static_cast<int>(typeForDelta(m_deltaX, m_deltaY)), static_cast<int>(GesturePhase::End), m_fingers,
            static_cast<int16_t>(std::clamp(m_deltaX, INT16_MIN, INT16_MAX)),
            static_cast<int16_t>(std::clamp(m_deltaY, INT16_MIN, INT16_MAX))
        );
      } else {
        emitGesture(
            static_cast<int>(GestureType::SwipeRight), static_cast<int>(GesturePhase::Cancel), m_fingers, 0, 0
        );
      }
    }
    m_tracking = false;
    m_fingers = 0;
    m_deltaX = 0;
    m_deltaY = 0;
    return;
  }

  if (m_tracking && fingers == 3) {
    m_deltaX += static_cast<int32_t>(event.deltaX);
    m_deltaY += static_cast<int32_t>(event.deltaY);
    emitGesture(
        static_cast<int>(typeForDelta(m_deltaX, m_deltaY)), static_cast<int>(GesturePhase::Update), m_fingers,
        static_cast<int16_t>(std::clamp(m_deltaX, INT16_MIN, INT16_MAX)),
        static_cast<int16_t>(std::clamp(m_deltaY, INT16_MIN, INT16_MAX))
    );
  }
}

void OSXGestureCapture::emitGesture(int type, int phase, uint8_t fingers, int16_t deltaX, int16_t deltaY)
{
  LOGC(
      Settings::value(Settings::Log::GestureDiagnostics).toBool(),
      (CLOG_INFO "gesture.capture emit type=%d phase=%d fingers=%d delta=%d,%d sequence=%u", type, phase, fingers,
       deltaX, deltaY, m_sequence)
  );
  const GestureEvent gesture{
      static_cast<GestureType>(type), static_cast<GesturePhase>(phase), fingers, deltaX, deltaY, m_sequence
  };
  if (m_handler) {
    m_handler(gesture);
    return;
  }

  auto *event = static_cast<GestureEvent *>(malloc(sizeof(GestureEvent)));
  *event = gesture;
  m_events->addEvent(Event(EventTypes::PrimaryScreenGesture, m_eventTarget, event));
}
