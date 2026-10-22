/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2015 - 2016 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "common/Enums.h"
#include "deskflow/ClipboardChunk.h"
#include "deskflow/StreamChunker.h"
#include "server/ClientProxy1_5.h"

#include <memory>
#include <string>


class Server;
class IEventQueue;

//! Proxy for client implementing protocol version 1.6
class ClientProxy1_6 : public ClientProxy1_5
{
public:
  ClientProxy1_6(const std::string &name, deskflow::IStream *adoptedStream, Server *server, IEventQueue *events);
  ~ClientProxy1_6() override;

  void setClipboard(ClipboardID id, const IClipboard *clipboard) override;
  bool recvClipboard() override;

  //! True while clipboard data is still being received from or sent to the client.
  bool hasActiveTransfer() const override;

private:
  void reportTransfer(
      deskflow::core::ClipboardTransferDirection direction, deskflow::core::ClipboardTransferPhase phase, ClipboardID id, uint64_t done = 0,
      uint64_t total = 0, deskflow::core::ClipboardTransferFailure failure = deskflow::core::ClipboardTransferFailure::None
  );

  IEventQueue *m_events;
  std::string m_clipboardDataCached;
  ClipboardChunkAssemblyState m_clipboardChunkState;
  std::shared_ptr<StreamChunker::Sender> m_clipboardSender;
};
