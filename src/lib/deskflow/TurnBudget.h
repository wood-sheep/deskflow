/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <cstddef>
#include <cstdint>

//! Caps how much of a receivable stream is handled in a single event loop turn.
/*!
 * Input forwarding and clipboard transfer share the core event loop. Draining
 * a whole buffered clipboard in one turn postpones queued mouse events for
 * the duration of the transfer; handling a bounded slice per turn keeps
 * input latency low while still making steady progress.
 */
class TurnBudget
{
public:
  static constexpr uint32_t kMaxMessages = 8;
  static constexpr size_t kMaxBytes = 2 * 1024 * 1024;

  void beginTurn()
  {
    m_messages = 0;
    m_bytes = 0;
  }

  //! Records one parsed message of `bytes` and returns false once the budget is spent.
  bool consume(size_t bytes)
  {
    ++m_messages;
    m_bytes += bytes;
    return !exhausted();
  }

  bool exhausted() const
  {
    return m_messages >= kMaxMessages || m_bytes >= kMaxBytes;
  }

  uint32_t messages() const
  {
    return m_messages;
  }

  size_t bytes() const
  {
    return m_bytes;
  }

private:
  uint32_t m_messages = 0;
  size_t m_bytes = 0;
};
