/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/GestureTypes.h"

#include <cstdint>
#include <functional>

class IEventQueue;

//! Captures AppKit gesture notifications and emits normalized Deskflow events.
class OSXGestureCapture
{
public:
  using GestureHandler = std::function<void(const GestureEvent &)>;

  OSXGestureCapture(IEventQueue *events, void *eventTarget);
  explicit OSXGestureCapture(GestureHandler handler);
  OSXGestureCapture(OSXGestureCapture const &) = delete;
  OSXGestureCapture(OSXGestureCapture &&) = delete;
  ~OSXGestureCapture();

  OSXGestureCapture &operator=(OSXGestureCapture const &) = delete;
  OSXGestureCapture &operator=(OSXGestureCapture &&) = delete;

  bool start();
  void stop();

private:
  void handleEvent(void *event);
  void emitGesture(int type, int phase, uint8_t fingers, int16_t deltaX, int16_t deltaY);

  IEventQueue *m_events = nullptr;
  void *m_eventTarget = nullptr;
  GestureHandler m_handler;
  void *m_globalMonitor = nullptr;
  void *m_localMonitor = nullptr;
  bool m_tracking = false;
  uint8_t m_fingers = 0;
  int32_t m_deltaX = 0;
  int32_t m_deltaY = 0;
  uint32_t m_sequence = 0;
};
