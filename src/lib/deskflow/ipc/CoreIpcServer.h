/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025-2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "IpcServer.h"

#include <QObject>
#include <QSet>
#include <cstdint>

class QLocalSocket;

namespace deskflow::core::ipc {

class CoreIpcServer : public IpcServer
{
  Q_OBJECT

public:
  explicit CoreIpcServer(QObject *parent);

  static CoreIpcServer &instance();

Q_SIGNALS:
  void gestureReceived(int type, int phase, int fingers, int deltaX, int deltaY, uint32_t sequence);

private:
  void processCommand(QLocalSocket *clientSocket, const QString &command, const QStringList &parts) override;
};

} // namespace deskflow::core::ipc
