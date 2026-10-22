/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 - 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "StatusBar.h"
#include "common/Constants.h"
#include "common/Settings.h"

#include <QEvent>
#include <QLabel>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>

using deskflow::core::ClipboardTransferDirection;
using deskflow::core::ClipboardTransferFailure;
using deskflow::core::ClipboardTransferPhase;

StatusBar::StatusBar(QWidget *parent)
    : QStatusBar{parent},
      m_btnFingerprint{new QPushButton(this)},
      m_lblSecurityIcon{new QLabel(this)},
      m_lblStatus{new QLabel(this)},
      m_lblClipboard{new QLabel(this)},
      m_progressClipboard{new QProgressBar(this)},
      m_btnUpdate{new QPushButton(this)},
      m_clipboardHideTimer{new QTimer(this)},
      m_retryTimer{new QTimer(this)}
{
  static const auto btnHeight = height() - 2;
  static const auto btnSize = QSize(btnHeight, btnHeight);
  static const auto iconSize = QSize(fontMetrics().height() + 2, fontMetrics().height() + 2);

  m_btnFingerprint->setFlat(true);
  m_btnFingerprint->setIcon(QIcon::fromTheme(QStringLiteral("fingerprint")));
  m_btnFingerprint->setFixedSize(btnSize);
  m_btnFingerprint->setIconSize(iconSize);
  insertPermanentWidget(0, m_btnFingerprint);
  connect(m_btnFingerprint, &QPushButton::clicked, this, &StatusBar::requestShowMyFingerprints);

  m_lblSecurityIcon->setVisible(false);
  m_lblSecurityIcon->setFixedSize(iconSize);
  m_lblSecurityIcon->setScaledContents(true);
  insertPermanentWidget(1, m_lblSecurityIcon);

  m_lblStatus->setText(tr("%1 is not running").arg(kAppName));
  insertPermanentWidget(2, m_lblStatus, 1);

  m_lblClipboard->setVisible(false);
  insertPermanentWidget(3, m_lblClipboard);
  m_progressClipboard->setVisible(false);
  m_progressClipboard->setFixedWidth(120);
  m_progressClipboard->setFixedHeight(btnHeight);
  m_progressClipboard->setTextVisible(false);
  m_progressClipboard->setToolTip(tr("Clipboard transfer progress"));
  insertPermanentWidget(4, m_progressClipboard);

  m_btnUpdate->setVisible(false);
  m_btnUpdate->setFlat(true);
  m_btnUpdate->setLayoutDirection(Qt::RightToLeft);
  m_btnUpdate->setIcon(QIcon::fromTheme(QStringLiteral("software-updates-release")));
  m_btnUpdate->setFixedHeight(btnHeight);
  m_btnUpdate->setIconSize(iconSize);
  insertPermanentWidget(5, m_btnUpdate);
  connect(m_btnUpdate, &QPushButton::clicked, this, &StatusBar::requestUpdateVersion);

  m_retryTimer->setInterval(1000);
  m_retryTimer->setSingleShot(false);
  connect(m_retryTimer, &QTimer::timeout, this, &StatusBar::updateTimerLabel);

  m_clipboardHideTimer->setSingleShot(true);
  connect(m_clipboardHideTimer, &QTimer::timeout, this, &StatusBar::hideClipboardTransfer);

  updateText();
  adjustSize();
}

// clang-format off
void StatusBar::setStatus(ConnectionState connectionState, ProcessState processState, bool isServer)
{
  if (m_retryTimer->isActive())
    m_retryTimer->stop();
  setSecurityIconVisible(false);
  switch (processState) {
    using enum ProcessState;
    case Starting:
      m_connectionInterval = -1;
      m_lblStatus->setText(tr("%1 is starting...").arg(kAppName));
      break;

    case RetryPending:
      m_connectionInterval = -1;
      m_lblStatus->setText(tr("%1 will retry in a moment...").arg(kAppName));
      break;

    case Stopping:
        m_connectionInterval = -1;
      m_lblStatus->setText(tr("%1 is stopping...").arg(kAppName));
      break;

    case Stopped:
      m_connectionInterval = -1;
      m_lblStatus->setText(tr("%1 is not running").arg(kAppName));
      break;

    case Started: {
      switch (connectionState) {
        using enum ConnectionState;

        case Listening: {
          if (isServer) {
            setSecurityIconVisible(true);
            m_lblStatus->setText(tr("%1 is waiting for clients").arg(kAppName));
          }
          break;
        }

        case Connecting:
          updateTimerLabel();
          if (Settings::value(Settings::Client::DynamicConnectionRetry).toBool())
            m_retryTimer->start();
          break;

        case Connected: {
          setSecurityIconVisible(true);
          m_connectionInterval = -1;
          if (!isServer) {
            m_lblStatus->setText(tr("%1 is connected as client of %2")
                                     .arg(kAppName, Settings::value(Settings::Client::RemoteHost).toString()));
          }
          break;
        }

        case Disconnected:
          m_lblStatus->setText(tr("%1 is disconnected").arg(kAppName));
          m_connectionInterval = -1;
          break;
      }
    }
  }
}
// clang-format on
void StatusBar::setServerClients(const QStringList &clients)
{
  if (clients.isEmpty()) {
    m_lblStatus->setText(tr("%1 is waiting for clients").arg(kAppName));
    m_lblStatus->setToolTip("");
    return;
  }
  const auto clientCount = static_cast<int>(clients.size());
  static const auto comma = QStringLiteral(", ");
  static const auto newLine = QStringLiteral("\n");
  //: Shown when in server mode and at least 1 client is connected
  //: %1 is replaced by the app name
  //: %2 will be a list of at least one client
  //: %n will be replaced by the number of clients (n is >=1), it is not requried to be in the translation
  const auto text = tr("%1 is connected, with %n client(s): %2", "", clientCount).arg(kAppName, clients.join(comma));
  m_lblStatus->setText(text);

  const auto toolTipString = clientCount == 1 ? "" : tr("Clients:\n %1").arg(clients.join(newLine));
  m_lblStatus->setToolTip(toolTipString);
}

void StatusBar::setSecurityIconVisible(bool visible)
{
  m_lblSecurityIcon->setVisible(visible);
}

void StatusBar::setConnectionInterval(int newInterval)
{
  m_connectionInterval = newInterval;
}

bool StatusBar::securityIconVisible() const
{
  return m_lblSecurityIcon->isVisible();
}

void StatusBar::setBtnFingerprintVisible(bool visible)
{
  m_btnFingerprint->setVisible(visible);
}

void StatusBar::updateFound(const QString &version)
{
  m_btnUpdate->setVisible(true);
  m_btnUpdate->setToolTip(tr("A new version v%1 is available").arg(version));
}

void StatusBar::setClipboardTransfer(const ClipboardTransferStatus &status)
{
  using enum ClipboardTransferPhase;

  if (status.phase == Done || status.phase == Cancelled || status.phase == Failed) {
    // A zero-size "done" only clears a preparing state (nothing was transferred).
    if (status.phase == Cancelled || (status.phase == Done && status.total == 0)) {
      hideClipboardTransfer();
      return;
    }

    m_lblClipboard->setText(clipboardTransferText(status));
    m_lblClipboard->setVisible(true);
    m_progressClipboard->setVisible(status.phase == Failed);
    if (status.phase == Failed) {
      m_progressClipboard->setRange(0, 1);
      m_progressClipboard->setValue(1);
    }

    // Keep the outcome visible long enough to read it.
    m_clipboardHideTimer->start(status.phase == Failed ? 8000 : 3000);
    return;
  }

  m_clipboardHideTimer->stop();
  m_lblClipboard->setText(clipboardTransferText(status));
  m_lblClipboard->setVisible(true);
  m_progressClipboard->setVisible(true);

  if (status.phase == Preparing || status.total == 0) {
    m_progressClipboard->setRange(0, 0); // indeterminate
  } else {
    // Use permille so small percentage steps still move the bar.
    constexpr int kPermille = 1000;
    m_progressClipboard->setRange(0, kPermille);
    m_progressClipboard->setValue(static_cast<int>(status.done * kPermille / status.total));
  }
}

QString StatusBar::clipboardTransferText(const ClipboardTransferStatus &status)
{
  using enum ClipboardTransferPhase;

  const bool sending = status.direction == ClipboardTransferDirection::Send;
  switch (status.phase) {
  case Preparing:
    return sending ? tr("Preparing to send clipboard…") : tr("Receiving clipboard…");

  case Active: {
    if (status.total == 0) {
      return sending ? tr("Sending clipboard…") : tr("Receiving clipboard…");
    }
    return sending
               ? tr("Sending clipboard %1 of %2").arg(formatBytes(status.done), formatBytes(status.total))
               : tr("Receiving clipboard %1 of %2").arg(formatBytes(status.done), formatBytes(status.total));
  }

  case Done:
    return sending ? tr("Clipboard sent (%1)").arg(formatBytes(status.total))
                   : tr("Clipboard received (%1)").arg(formatBytes(status.total));

  case Failed:
    switch (status.failure) {
    case ClipboardTransferFailure::Oversize:
      return tr("Clipboard not shared: data exceeds the configured size limit");
    case ClipboardTransferFailure::Corrupt:
      return tr("Clipboard transfer failed: data was corrupt");
    case ClipboardTransferFailure::PeerDisconnected:
      return tr("Clipboard transfer failed: the other machine disconnected");
    case ClipboardTransferFailure::None:
      return tr("Clipboard transfer failed");
    }
    break;

  case Cancelled:
    break;
  }

  return tr("Clipboard transfer");
}

QString StatusBar::formatBytes(quint64 bytes)
{
  return QLocale().formattedDataSize(static_cast<qint64>(bytes));
}

void StatusBar::hideClipboardTransfer()
{
  m_lblClipboard->clear();
  m_lblClipboard->setVisible(false);
  m_progressClipboard->setVisible(false);
  m_progressClipboard->setRange(0, 1);
  m_progressClipboard->setValue(0);
}

void StatusBar::changeEvent(QEvent *e)
{
  QStatusBar::changeEvent(e);
  if (e->type() == QEvent::LanguageChange)
    updateText();
}

void StatusBar::updateText()
{
  m_btnFingerprint->setToolTip(tr("View local fingerprint"));
  m_btnUpdate->setText(tr("Update available"));
  setSecurityLevel(m_securityLevel);
}

void StatusBar::updateTimerLabel()
{
  QString text;
  if (m_connectionInterval < 2 || !Settings::value(Settings::Client::DynamicConnectionRetry).toBool()) {
    text = tr("%1 is connecting...").arg(kAppName);
  } else {
    text = tr("%1 is waiting %2 seconds before the next retry").arg(kAppName, QString::number(m_connectionInterval));
    m_connectionInterval--;
  }
  m_lblStatus->setText(text);
}

void StatusBar::setSecurityIcon(bool encrypted)
{
  const auto icon = QIcon::fromTheme(encrypted ? QIcon::ThemeIcon::SecurityHigh : QIcon::ThemeIcon::SecurityLow);
  m_lblSecurityIcon->setPixmap(icon.pixmap(QSize(32, 32)));
  m_encrypted = encrypted;
  setSecurityLevel(m_securityLevel);
}

void StatusBar::setSecurityLevel(const QString &securityLevel)
{
  m_securityLevel = securityLevel;
  const auto txt = m_encrypted ? tr("%1 Encryption Enabled").arg(m_securityLevel) : tr("Encryption Disabled");
  m_lblSecurityIcon->setToolTip(txt);
}
