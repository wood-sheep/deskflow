/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025-2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "CoreIpcServer.h"

#include "base/Log.h"
#include "common/Constants.h"

#include <climits>

#include <QLocalSocket>

namespace deskflow::core::ipc {

static CoreIpcServer *s_instance = nullptr;

CoreIpcServer::CoreIpcServer(QObject *parent) : IpcServer(parent, kCoreIpcName, QStringLiteral("core"))
{
  assert(s_instance == nullptr);
  s_instance = this;
}

CoreIpcServer &CoreIpcServer::instance()
{
  assert(s_instance != nullptr);
  return *s_instance;
}

void CoreIpcServer::processCommand(QLocalSocket *clientSocket, const QString &command, const QStringList &parts)
{
  if (command == QStringLiteral("stop")) {
    LOG_DEBUG("core ipc server got stop message");
    writeToClientSocket(clientSocket, QStringLiteral("ok"));
    broadcastCommand(QStringLiteral("bye"));
    Q_EMIT stopProcessRequested();
    return;
  }
  if (command == QStringLiteral("gesture")) {
    if (parts.size() != 2) {
      LOG_WARN("core ipc gesture message has invalid field count");
      return;
    }

    const auto fields = parts.at(1).split(',');
    if (fields.size() != 6) {
      LOG_WARN("core ipc gesture message has invalid field count");
      return;
    }

    bool valid = true;
    int values[5] = {};
    for (int i = 0; i < 5; ++i) {
      bool ok = false;
      values[i] = fields.at(i).toInt(&ok);
      valid = valid && ok;
    }
    bool sequenceOk = false;
    const auto sequence = fields.at(5).toUInt(&sequenceOk);
    valid = valid && sequenceOk;

    if (!valid || values[0] < 0 || values[0] > 3 || values[1] < 0 || values[1] > 3 || values[2] < 0 ||
        values[2] > 255 || values[3] < INT16_MIN || values[3] > INT16_MAX || values[4] < INT16_MIN ||
        values[4] > INT16_MAX) {
      LOG_WARN("core ipc gesture message contains invalid values");
      return;
    }

    Q_EMIT gestureReceived(values[0], values[1], values[2], values[3], values[4], sequence);
    return;
  }
  LOG_WARN("core ipc server got unknown command: %s", command.toUtf8().constData());
}

} // namespace deskflow::core::ipc
