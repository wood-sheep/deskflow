/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "common/Enums.h"

#include <chrono>
#include <functional>

namespace deskflow::core::ipc {

//! Throttles clipboard transfer status updates before sending them to the GUI.
/*!
 * The core event loop is shared by input and clipboard handling, so status
 * updates must stay cheap. Intermediate updates are rate limited; phase
 * changes (start, done, failure) are always reported. Transfers below
 * `kMinReportedBytes` are ignored entirely so ordinary text copies stay silent.
 */
class ClipboardTransferReporter
{
public:
  using Clock = std::function<std::chrono::steady_clock::time_point()>;
  using Sink = std::function<void(const ClipboardTransferStatus &)>;

  static constexpr uint64_t kMinReportedBytes = 1024 * 1024;
  static constexpr std::chrono::milliseconds kMinInterval{250};
  static constexpr unsigned kMinPercentDelta = 5;

  static ClipboardTransferReporter &instance();

  explicit ClipboardTransferReporter(Clock clock = {}, Sink sink = {});

  void report(const ClipboardTransferStatus &status);

private:
  bool shouldReport(const ClipboardTransferStatus &status, uint64_t &percent);

  Clock m_clock;
  Sink m_sink;
  bool m_hasEmitted = false;
  bool m_inProgress = false;
  ClipboardTransferStatus m_last;
  std::chrono::steady_clock::time_point m_lastEmitTime;
  uint64_t m_lastPercent = 0;
};

} // namespace deskflow::core::ipc
