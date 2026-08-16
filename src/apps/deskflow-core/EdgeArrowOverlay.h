/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QWidget>

class QTimer;

//! Floating arrow hint shown at a screen edge while the cursor dwells there.
//!
//! Lives on the Qt main thread (the server core runs on a worker thread) and
//! is driven via queued invocations, so it never blocks or races the server
//! event loop. Borderless, translucent, top-most and focus-less.
class EdgeArrowOverlay : public QWidget
{
  Q_OBJECT

public:
  explicit EdgeArrowOverlay(QWidget *parent = nullptr);
  ~EdgeArrowOverlay() override;

  //! Show the hint arrow at the given edge of the screen rectangle.
  //! Safe to call from any thread.
  void showHint(int direction, int screenX, int screenY, int screenW, int screenH);
  //! Hide the hint arrow. Safe to call from any thread.
  void hideHint();

protected:
  void paintEvent(QPaintEvent *) override;

private Q_SLOTS:
  void doShowHint(int direction, int screenX, int screenY, int screenW, int screenH);
  void doHideHint();

private:
  void positionWindow(int screenX, int screenY, int screenW, int screenH);

  int m_direction = 0; // 0=left 1=right 2=up 3=down
  QTimer *m_fadeTimer = nullptr;
};
