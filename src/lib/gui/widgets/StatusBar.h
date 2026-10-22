/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 - 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QStatusBar>

#include "common/Enums.h"

class QPushButton;
class QLabel;
class QProgressBar;

using ProcessState = deskflow::core::ProcessState;
using ConnectionState = deskflow::core::ConnectionState;
using ClipboardTransferStatus = deskflow::core::ClipboardTransferStatus;

class StatusBar : public QStatusBar
{
  Q_OBJECT
public:
  explicit StatusBar(QWidget *parent = nullptr);
  void setStatus(ConnectionState connectionState, ProcessState processState, bool isServer);
  void setServerClients(const QStringList &clients);
  void setSecurityIconVisible(bool visible);
  void setConnectionInterval(int newInterval);
  bool securityIconVisible() const;
  void updateSecurityInfo(bool encrypted);
  void setSecurityIcon(bool encrypted);
  void setSecurityLevel(const QString &securityLevel);
  void setBtnFingerprintVisible(bool visible);
  void updateFound(const QString &version);

  //! Shows clipboard transfer progress; only large transfers reach this point.
  void setClipboardTransfer(const ClipboardTransferStatus &status);

  //! Human readable form of a transfer state, shared with notifications.
  static QString clipboardTransferText(const ClipboardTransferStatus &status);

Q_SIGNALS:
  void requestShowMyFingerprints();
  void requestUpdateVersion();

protected:
  void changeEvent(QEvent *e) override;

private:
  void updateText();
  void updateTimerLabel();
  void hideClipboardTransfer();
  static QString formatBytes(quint64 bytes);

  QPushButton *m_btnFingerprint = nullptr;
  QLabel *m_lblSecurityIcon = nullptr;
  QLabel *m_lblStatus = nullptr;
  QLabel *m_lblClipboard = nullptr;
  QProgressBar *m_progressClipboard = nullptr;
  QPushButton *m_btnUpdate = nullptr;
  QTimer *m_clipboardHideTimer = nullptr;
  bool m_encrypted = false;
  QString m_securityLevel;
  int m_connectionInterval = -1;
  QTimer *m_retryTimer = nullptr;
};
