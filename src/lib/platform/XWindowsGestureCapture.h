/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/GestureTypes.h"

#include <cstdint>
#include <functional>

class IEventQueue;

//! Captures Linux touchpad gestures and emits normalized Deskflow events.
//! Uses libinput on Linux for gesture detection.
class XWindowsGestureCapture
{
public:
  using GestureHandler = std::function<void(const GestureEvent &)>;

  XWindowsGestureCapture(IEventQueue *events, void *eventTarget);
  explicit XWindowsGestureCapture(GestureHandler handler);
  XWindowsGestureCapture(XWindowsGestureCapture const &) = delete;
  XWindowsGestureCapture(XWindowsGestureCapture &&) = delete;
  ~XWindowsGestureCapture();

  XWindowsGestureCapture &operator=(XWindowsGestureCapture const &) = delete;
  XWindowsGestureCapture &operator=(XWindowsGestureCapture &&) = delete;

  bool start();
  void stop();

private:
  void handleEvent(void *event);
  void emitGesture(int type, int phase, uint8_t fingers, int16_t deltaX, int16_t deltaY);

  IEventQueue *m_events = nullptr;
  void *m_eventTarget = nullptr;
  GestureHandler m_handler;
  void *m_libinputContext = nullptr;  // libinput context (Linux only)
  void *m_libinputMonitor = nullptr;   // libinput monitor thread
  bool m_tracking = false;
  uint8_t m_fingers = 0;
  int32_t m_deltaX = 0;
  int32_t m_deltaY = 0;
  uint32_t m_sequence = 0;
};
