/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2010 - 2018, 2024 - 2025 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2002 - 2007 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QObject>

#include <cstdint>

namespace deskflow::client {
Q_NAMESPACE
enum class ErrorType : uint8_t
{
  NoError,
  AlreadyConnected,
  HostnameError,
  GenericError
};
Q_ENUM_NS(ErrorType)
} // namespace deskflow::client

namespace deskflow::core {
Q_NAMESPACE

enum class ProcessState
{
  Starting,
  Started,
  Stopping,
  Stopped,
  RetryPending
};
Q_ENUM_NS(ProcessState)

enum class ConnectionState
{
  Disconnected,
  Connecting,
  Connected,
  Listening
};
Q_ENUM_NS(ConnectionState)

enum class ConnectionRefusal
{
  IncompatibleVersion,
  AlreadyConnected,
  UnknownClient,
  ProtocolError
};
Q_ENUM_NS(ConnectionRefusal)

//! Direction of a clipboard transfer relative to the local core process.
enum class ClipboardTransferDirection
{
  Send,
  Receive
};
Q_ENUM_NS(ClipboardTransferDirection)

enum class ClipboardTransferPhase
{
  Preparing, //!< reading/converting the clipboard, size may be unknown yet
  Active,    //!< bytes are moving
  Done,
  Failed,
  Cancelled
};
Q_ENUM_NS(ClipboardTransferPhase)

enum class ClipboardTransferFailure
{
  None,
  Oversize,
  Corrupt,
  PeerDisconnected
};
Q_ENUM_NS(ClipboardTransferFailure)

struct ClipboardTransferStatus
{
  ClipboardTransferDirection direction = ClipboardTransferDirection::Send;
  uint8_t clipboardId = 0;
  ClipboardTransferPhase phase = ClipboardTransferPhase::Preparing;
  quint64 done = 0;
  quint64 total = 0;
  ClipboardTransferFailure failure = ClipboardTransferFailure::None;
};

} // namespace deskflow::core

Q_DECLARE_METATYPE(deskflow::core::ClipboardTransferStatus)
