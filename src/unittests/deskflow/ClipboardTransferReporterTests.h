/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QTest>

class ClipboardTransferReporterTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void ignoresPrimarySelection();
  void ignoresSmallTransfers();
  void reportsLargeTransferStartAndEnd();
  void throttlesIntermediateUpdates();
  void reportsFinalPhaseEvenBelowMinimum();
  void failureAlwaysReported();
};
