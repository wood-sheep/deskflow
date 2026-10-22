/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ClipboardTransferReporterTests.h"

#include "deskflow/ClipboardTypes.h"
#include "deskflow/ipc/ClipboardTransferReporter.h"

#include <QVector>

namespace {

using namespace deskflow::core;
using namespace deskflow::core::ipc;

std::chrono::steady_clock::time_point g_now{};

std::chrono::steady_clock::time_point testClock()
{
  return g_now;
}

ClipboardTransferStatus makeStatus(
    ClipboardTransferPhase phase, uint64_t done, uint64_t total, ClipboardID id = 0,
    ClipboardTransferFailure failure = ClipboardTransferFailure::None
)
{
  ClipboardTransferStatus status;
  status.direction = ClipboardTransferDirection::Send;
  status.clipboardId = id;
  status.phase = phase;
  status.done = done;
  status.total = total;
  status.failure = failure;
  return status;
}

} // namespace

void ClipboardTransferReporterTests::ignoresPrimarySelection()
{
  QVector<ClipboardTransferStatus> emitted;
  ClipboardTransferReporter reporter(testClock, [&](const auto &s) { emitted.append(s); });

  reporter.report(makeStatus(ClipboardTransferPhase::Active, 0, 10 * 1024 * 1024, 1));
  reporter.report(makeStatus(ClipboardTransferPhase::Done, 10 * 1024 * 1024, 10 * 1024 * 1024, 1));

  QVERIFY(emitted.isEmpty());
}

void ClipboardTransferReporterTests::ignoresSmallTransfers()
{
  QVector<ClipboardTransferStatus> emitted;
  ClipboardTransferReporter reporter(testClock, [&](const auto &s) { emitted.append(s); });

  reporter.report(makeStatus(ClipboardTransferPhase::Active, 0, 1024));
  reporter.report(makeStatus(ClipboardTransferPhase::Done, 1024, 1024));

  QVERIFY(emitted.isEmpty());
}

void ClipboardTransferReporterTests::reportsLargeTransferStartAndEnd()
{
  g_now = std::chrono::steady_clock::time_point{};
  QVector<ClipboardTransferStatus> emitted;
  ClipboardTransferReporter reporter(testClock, [&](const auto &s) { emitted.append(s); });

  reporter.report(makeStatus(ClipboardTransferPhase::Active, 0, 4 * 1024 * 1024));
  reporter.report(makeStatus(ClipboardTransferPhase::Done, 4 * 1024 * 1024, 4 * 1024 * 1024));

  QCOMPARE(emitted.size(), 2);
  QCOMPARE(emitted.at(0).phase, ClipboardTransferPhase::Active);
  QCOMPARE(emitted.at(1).phase, ClipboardTransferPhase::Done);
}

void ClipboardTransferReporterTests::throttlesIntermediateUpdates()
{
  g_now = std::chrono::steady_clock::time_point{};
  QVector<ClipboardTransferStatus> emitted;
  ClipboardTransferReporter reporter(testClock, [&](const auto &s) { emitted.append(s); });

  constexpr uint64_t total = 4 * 1024 * 1024;
  reporter.report(makeStatus(ClipboardTransferPhase::Active, 0, total));
  QCOMPARE(emitted.size(), 1);

  // More than 5% progress, but no time has passed yet: dropped.
  reporter.report(makeStatus(ClipboardTransferPhase::Active, total / 10, total));
  QCOMPARE(emitted.size(), 1);

  // Time passed, but less than 5% since the last emit: dropped.
  g_now += std::chrono::milliseconds(300);
  reporter.report(makeStatus(ClipboardTransferPhase::Active, total / 10 + total / 100, total));
  QCOMPARE(emitted.size(), 1);

  // Both gates satisfied: reported.
  reporter.report(makeStatus(ClipboardTransferPhase::Active, total / 5, total));
  QCOMPARE(emitted.size(), 2);
}

void ClipboardTransferReporterTests::reportsFinalPhaseEvenBelowMinimum()
{
  g_now = std::chrono::steady_clock::time_point{};
  QVector<ClipboardTransferStatus> emitted;
  ClipboardTransferReporter reporter(testClock, [&](const auto &s) { emitted.append(s); });

  constexpr uint64_t total = 4 * 1024 * 1024;
  reporter.report(makeStatus(ClipboardTransferPhase::Active, 0, total));
  QCOMPARE(emitted.size(), 1);

  // A final phase with a smaller byte count must still be delivered.
  reporter.report(makeStatus(ClipboardTransferPhase::Done, 0, 0));
  QCOMPARE(emitted.size(), 2);
  QCOMPARE(emitted.at(1).phase, ClipboardTransferPhase::Done);
}

void ClipboardTransferReporterTests::failureAlwaysReported()
{
  g_now = std::chrono::steady_clock::time_point{};
  QVector<ClipboardTransferStatus> emitted;
  ClipboardTransferReporter reporter(testClock, [&](const auto &s) { emitted.append(s); });

  reporter.report(makeStatus(
      ClipboardTransferPhase::Failed, 0, 512, 0, ClipboardTransferFailure::Oversize
  ));

  QCOMPARE(emitted.size(), 1);
  QCOMPARE(emitted.at(0).failure, ClipboardTransferFailure::Oversize);
}

QTEST_MAIN(ClipboardTransferReporterTests)
