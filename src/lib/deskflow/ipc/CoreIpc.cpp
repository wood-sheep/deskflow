/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "CoreIpc.h"

#include "CoreIpcServer.h"

#include <QMetaEnum>

void ipcSendToClient(const QString &command, const QString &args)
{
  // Queued because callers may not be on the main thread,
  // and QLocalSocket can only be written to from its owning thread.
  auto &server = deskflow::core::ipc::CoreIpcServer::instance();
  QMetaObject::invokeMethod(
      &server, [command, args] { deskflow::core::ipc::CoreIpcServer::instance().broadcastCommand(command, args); },
      Qt::QueuedConnection
  );
}

void ipcSendConnectionState(deskflow::core::ConnectionState state)
{
  const auto metaEnum = QMetaEnum::fromType<deskflow::core::ConnectionState>();
  ipcSendToClient(QStringLiteral("connectionState"), metaEnum.valueToKey(static_cast<int>(state)));
}

void ipcSendClipboardProgress(const deskflow::core::ClipboardTransferStatus &status)
{
  const auto args = QStringLiteral("%1,%2,%3,%4,%5,%6")
                        .arg(static_cast<int>(status.direction))
                        .arg(status.clipboardId)
                        .arg(static_cast<int>(status.phase))
                        .arg(status.done)
                        .arg(status.total)
                        .arg(static_cast<int>(status.failure));

  // Queued because callers run on the core thread, but never replayed later.
  auto &server = deskflow::core::ipc::CoreIpcServer::instance();
  QMetaObject::invokeMethod(
      &server, [args] { deskflow::core::ipc::CoreIpcServer::instance().broadcastTransientCommand(
                            QStringLiteral("clipboardProgress"), args
                        ); },
      Qt::QueuedConnection
  );
}
