/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "EdgeArrowOverlay.h"

#include <QMetaObject>
#include <QPainter>
#include <QTimer>

namespace
{
constexpr int kArrowSize = 64;  // window size in pixels
constexpr int kArrowInset = 24; // distance from the screen edge
constexpr int kFadeMs = 250;    // hide fade duration
} // namespace

EdgeArrowOverlay::EdgeArrowOverlay(QWidget *parent) : QWidget(parent), m_direction(0)
{
  setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool | Qt::WindowDoesNotAcceptFocus);
  setAttribute(Qt::WA_TranslucentBackground);
  setAttribute(Qt::WA_ShowWithoutActivating);
  setAttribute(Qt::WA_TransparentForMouseEvents, true);
  resize(kArrowSize, kArrowSize);

  m_fadeTimer = new QTimer(this);
  m_fadeTimer->setSingleShot(true);
  m_fadeTimer->setInterval(kFadeMs);
  connect(m_fadeTimer, &QTimer::timeout, this, &EdgeArrowOverlay::hide);
}

EdgeArrowOverlay::~EdgeArrowOverlay() = default;

void EdgeArrowOverlay::showHint(int direction, int screenX, int screenY, int screenW, int screenH)
{
  QMetaObject::invokeMethod(
      this,
      [this, direction, screenX, screenY, screenW, screenH] {
        doShowHint(direction, screenX, screenY, screenW, screenH);
      },
      Qt::QueuedConnection
  );
}

void EdgeArrowOverlay::hideHint()
{
  QMetaObject::invokeMethod(this, [this] { doHideHint(); }, Qt::QueuedConnection);
}

void EdgeArrowOverlay::doShowHint(int direction, int screenX, int screenY, int screenW, int screenH)
{
  m_direction = direction;
  positionWindow(screenX, screenY, screenW, screenH);
  show();
  raise();
}

void EdgeArrowOverlay::doHideHint()
{
  m_fadeTimer->stop();
  hide();
}

void EdgeArrowOverlay::positionWindow(int screenX, int screenY, int screenW, int screenH)
{
  int x = 0;
  int y = 0;
  switch (m_direction) {
  case 0: // left
    x = screenX + kArrowInset - kArrowSize;
    y = screenY + (screenH - kArrowSize) / 2;
    break;
  case 1: // right
    x = screenX + screenW - kArrowInset;
    y = screenY + (screenH - kArrowSize) / 2;
    break;
  case 2: // up
    x = screenX + (screenW - kArrowSize) / 2;
    y = screenY + kArrowInset - kArrowSize;
    break;
  case 3: // down
    x = screenX + (screenW - kArrowSize) / 2;
    y = screenY + screenH - kArrowInset;
    break;
  default:
    return;
  }
  move(x, y);
}

void EdgeArrowOverlay::paintEvent(QPaintEvent *)
{
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(Qt::NoPen);

  // translucent rounded backdrop
  painter.setBrush(QColor(30, 30, 30, 160));
  painter.drawRoundedRect(rect().adjusted(4, 4, -4, -4), 14, 14);

  // arrow triangle
  const QPointF center(kArrowSize / 2.0, kArrowSize / 2.0);
  const qreal r = kArrowSize * 0.28;
  QPolygonF tri;
  switch (m_direction) {
  case 0: // left
    tri << QPointF(center.x() - r, center.y()) << QPointF(center.x() + r, center.y() - r)
        << QPointF(center.x() + r, center.y() + r);
    break;
  case 1: // right
    tri << QPointF(center.x() + r, center.y()) << QPointF(center.x() - r, center.y() - r)
        << QPointF(center.x() - r, center.y() + r);
    break;
  case 2: // up
    tri << QPointF(center.x(), center.y() - r) << QPointF(center.x() - r, center.y() + r)
        << QPointF(center.x() + r, center.y() + r);
    break;
  case 3: // down
    tri << QPointF(center.x(), center.y() + r) << QPointF(center.x() - r, center.y() - r)
        << QPointF(center.x() + r, center.y() - r);
    break;
  default:
    return;
  }
  painter.setBrush(QColor(255, 255, 255, 235));
  painter.drawPolygon(tri);
}
