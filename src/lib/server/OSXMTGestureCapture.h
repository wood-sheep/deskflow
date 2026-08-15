/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/GestureTypes.h"

#include <cstdint>
#include <functional>
#include <mutex>

//! Captures multi-touch trackpad gestures using the private
//! MultitouchSupport.framework (MTDevice API).
//!
//! Unlike the AppKit NSEvent-based capture (OSXGestureCapture), this path:
//!   - works while the app is in the background (no frontmost requirement)
//!   - delivers raw per-finger coordinates, so swipe direction is reliable
//! These events are injected into the same PrimaryScreenGesture event stream
//! that the GUI IPC gestures use, so downstream handling is unchanged.
class OSXMTGestureCapture
{
public:
  using GestureHandler = std::function<void(const GestureEvent &)>;

  explicit OSXMTGestureCapture(GestureHandler handler);
  OSXMTGestureCapture(OSXMTGestureCapture const &) = delete;
  OSXMTGestureCapture &operator=(OSXMTGestureCapture const &) = delete;
  ~OSXMTGestureCapture();

  //! Starts capturing; returns false if the trackpad device is unavailable.
  bool start();
  void stop();

  //! Invoked by the MultitouchSupport callback thread.
  void onContacts(void *contacts, int numContacts);

private:
  GestureHandler m_handler;
  void *m_device = nullptr; // MTDeviceRef
  std::mutex m_mutex;
  bool m_tracking = false;
  double m_deltaX = 0.0;
  double m_deltaY = 0.0;
  double m_lastX = 0.0;
  double m_lastY = 0.0;
  double m_lastEmitted = 0.0;
  uint32_t m_sequence = 0;
};
