/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "common/Enums.h"

#include <QString>

void ipcSendToClient(const QString &command, const QString &args = "");
void ipcSendConnectionState(deskflow::core::ConnectionState state);

//! Sends a clipboard transfer update to the GUI, dropping it when no GUI is attached.
/*!
 * Progress updates are transient: queuing them for a future GUI would only
 * deliver stale state (and the pending queue is never trimmed).
 */
void ipcSendClipboardProgress(const deskflow::core::ClipboardTransferStatus &status);
