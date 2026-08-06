/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/OSXGestureCapture.h"

#include "base/Event.h"
#include "base/EventTypes.h"
#include "base/IEventQueue.h"
#include "deskflow/GestureTypes.h"

#include <AppKit/NSEvent.h>

#include <algorithm>
#include <climits>
#include <cstdlib>

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
  const auto fingers = countTouches(event);
  const auto type = event.type;

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

  if (type == NSEventTypeSwipe) {
    if (fingers != 3) {
      return;
    }

    const auto deltaX = static_cast<int32_t>(event.deltaX);
    const auto deltaY = static_cast<int32_t>(event.deltaY);
    if (std::abs(deltaX) < kSwipeThreshold && std::abs(deltaY) < kSwipeThreshold) {
      return;
    }

    ++m_sequence;
    emitGesture(
        static_cast<int>(typeForDelta(deltaX, deltaY)), static_cast<int>(GesturePhase::End), fingers,
        static_cast<int16_t>(std::clamp(deltaX, INT16_MIN, INT16_MAX)),
        static_cast<int16_t>(std::clamp(deltaY, INT16_MIN, INT16_MAX))
    );
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
  auto *event = static_cast<GestureEvent *>(malloc(sizeof(GestureEvent)));
  event->type = static_cast<GestureType>(type);
  event->phase = static_cast<GesturePhase>(phase);
  event->fingers = fingers;
  event->deltaX = deltaX;
  event->deltaY = deltaY;
  event->sequence = m_sequence;
  m_events->addEvent(Event(EventTypes::PrimaryScreenGesture, m_eventTarget, event));
}
