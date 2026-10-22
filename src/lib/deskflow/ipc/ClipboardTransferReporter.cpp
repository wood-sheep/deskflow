/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ClipboardTransferReporter.h"

#include "CoreIpc.h"

namespace deskflow::core::ipc {

namespace {

bool isFinalPhase(ClipboardTransferPhase phase)
{
  return phase == ClipboardTransferPhase::Done || phase == ClipboardTransferPhase::Failed ||
         phase == ClipboardTransferPhase::Cancelled;
}

} // namespace

ClipboardTransferReporter &ClipboardTransferReporter::instance()
{
  static ClipboardTransferReporter reporter;
  return reporter;
}

ClipboardTransferReporter::ClipboardTransferReporter(Clock clock, Sink sink)
    : m_clock(std::move(clock)), m_sink(std::move(sink))
{
  if (!m_clock) {
    m_clock = [] { return std::chrono::steady_clock::now(); };
  }
  if (!m_sink) {
    m_sink = [](const ClipboardTransferStatus &status) { ipcSendClipboardProgress(status); };
  }
}

void ClipboardTransferReporter::report(const ClipboardTransferStatus &status)
{
  // Only the clipboard itself is user initiated; the primary selection changes
  // constantly while selecting text and must never raise a notification.
  if (static_cast<int>(status.clipboardId) != 0) {
    return;
  }

  // A failure is always worth showing. Other updates only matter for transfers
  // large enough to be noticed; their final phase must still get through, or
  // the progress bar would stay on screen.
  if (status.phase != ClipboardTransferPhase::Failed && status.total < kMinReportedBytes) {
    const bool finalPhase =
        status.phase == ClipboardTransferPhase::Done || status.phase == ClipboardTransferPhase::Cancelled;
    if (!m_inProgress || !finalPhase) {
      return;
    }
  }

  if (status.phase != ClipboardTransferPhase::Failed) {
    // Progress can arrive without a preceding start (receive side adopts it).
    m_inProgress = true;
  }

  uint64_t percent = 0;
  if (!shouldReport(status, percent)) {
    m_last = status;
    m_lastPercent = percent;
    return;
  }

  m_sink(status);

  m_hasEmitted = true;
  m_last = status;
  m_lastPercent = percent;
  m_lastEmitTime = m_clock();

  if (isFinalPhase(status.phase)) {
    m_inProgress = false;
  }
}

bool ClipboardTransferReporter::shouldReport(const ClipboardTransferStatus &status, uint64_t &percent)
{
  percent = status.total != 0 ? status.done * 100 / status.total : 0;

  if (status.phase != m_last.phase || !m_hasEmitted) {
    return true;
  }
  if (isFinalPhase(status.phase)) {
    return true;
  }
  if (status.phase == ClipboardTransferPhase::Preparing) {
    return false; // one "preparing" sample is enough until bytes move
  }
  if (status.total != 0 && status.done >= status.total) {
    return true;
  }

  const auto now = m_clock();
  const bool intervalElapsed = now - m_lastEmitTime >= kMinInterval;
  const bool progressed = percent >= m_lastPercent + kMinPercentDelta;
  return intervalElapsed && progressed;
}

} // namespace deskflow::core::ipc
