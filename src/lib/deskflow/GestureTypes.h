/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <cstdint>

enum class GestureType : uint8_t
{
  SwipeLeft = 0,
  SwipeRight,
  SwipeUp,
  SwipeDown
};

enum class GesturePhase : uint8_t
{
  Begin = 0,
  Update,
  End,
  Cancel
};

//! A normalized multi-touch gesture event.
//!
//! This structure is intentionally POD because gesture events are transported
//! through the existing malloc/free based event queue.
struct GestureEvent
{
  GestureType type = GestureType::SwipeLeft;
  GesturePhase phase = GesturePhase::Cancel;
  uint8_t fingers = 0;
  int16_t deltaX = 0;
  int16_t deltaY = 0;
  uint32_t sequence = 0;
};
