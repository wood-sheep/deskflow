/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2014 - 2016 Symless Ltd
 * SPDX-FileCopyrightText: (C) 2014 Ryan Chapman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "platform/OSXClipboardAnyBitmapConverter.h"

//! Convert to/from a TIFF pasteboard flavor
//!
//! macOS screenshots and copied images place TIFF (public.tiff) on the
//! pasteboard. The BMP-only converter therefore never sees them, so image
//! clipboard sharing was broken in the macOS->Windows direction, and received
//! DIBs were published only as com.microsoft.bmp which macOS apps do not
//! consume. This converter bridges TIFF (via ImageIO/CoreGraphics) and the
//! DIB format used on the wire / by the Windows clipboard.
class OSXClipboardTIFFConverter : public OSXClipboardAnyBitmapConverter
{
public:
  OSXClipboardTIFFConverter() = default;
  ~OSXClipboardTIFFConverter() override = default;

  // IOSXClipboardConverter overrides
  CFStringRef getOSXFormat() const override;

  // OSXClipboardAnyBitmapConverter overrides
  std::string doFromIClipboard(const std::string &) const override;
  std::string doToIClipboard(const std::string &) const override;
};
