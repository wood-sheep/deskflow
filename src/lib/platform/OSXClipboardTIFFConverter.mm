/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2014 - 2016 Symless Ltd
 * SPDX-FileCopyrightText: (C) 2014 Ryan Chapman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/OSXClipboardTIFFConverter.h"

#include "base/Log.h"

#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>

#include <QtEndian>

namespace
{
//! Standard BITMAPINFOHEADER size (Windows GDI).
constexpr uint32_t kBiSize = 40;
//! 32bpp BGRA mask (byte order matches CGBitmapContext RGBA8888 little-endian).
constexpr uint32_t kMaskR = 0x00FF0000;
constexpr uint32_t kMaskG = 0x0000FF00;
constexpr uint32_t kMaskB = 0x000000FF;
constexpr uint32_t kMaskA = 0xFF000000;

//! Decode a DIB (BITMAPINFOHEADER + pixels) into a CGImage.
//!
//! Supports 24/32bpp BI_RGB DIBs (the form MSWindowsClipboardBitmapConverter
//! normalizes to). Negative height means top-down rows; positive height means
//! bottom-up, which CGBitmapContext cannot represent directly, so the rows are
//! flipped on the way in.
CGImageRef dibToCGImage(const std::string &dib)
{
  if (dib.size() < kBiSize) {
    LOG_DEBUG("clipboard.tiff rejecting dib, too small: %zu bytes", dib.size());
    return nullptr;
  }
  const auto *raw = reinterpret_cast<const uint8_t *>(dib.data());
  const auto biSize = qFromLittleEndian<uint32_t>(raw);
  if (biSize < kBiSize || biSize > dib.size()) {
    LOG_DEBUG("clipboard.tiff rejecting dib, bad header size: %u", biSize);
    return nullptr;
  }
  const auto width = static_cast<int>(qFromLittleEndian<int32_t>(raw + 4));
  const auto rawHeight = qFromLittleEndian<int32_t>(raw + 8);
  const auto planes = qFromLittleEndian<uint16_t>(raw + 12);
  const auto bitCount = qFromLittleEndian<uint16_t>(raw + 14);
  const auto compression = qFromLittleEndian<uint32_t>(raw + 16);
  if (width <= 0 || rawHeight == 0 || planes != 1 || (bitCount != 24 && bitCount != 32) ||
      compression != 0 /* BI_RGB */) {
    LOG_DEBUG(
        "clipboard.tiff rejecting dib: %dx%d bpp=%u comp=%u planes=%u", width, rawHeight, bitCount, compression,
        planes
    );
    return nullptr;
  }
  const auto height = std::abs(rawHeight);
  const auto bytesPerRow = ((width * bitCount + 31) / 32) * 4;
  const size_t expected = static_cast<size_t>(biSize) + static_cast<size_t>(bytesPerRow) * height;
  if (dib.size() < expected) {
    LOG_DEBUG("clipboard.tiff rejecting dib, truncated pixels: need=%zu have=%zu", expected, dib.size());
    return nullptr;
  }
  const auto *pixels = raw + biSize;

  CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
  CGContextRef ctx = CGBitmapContextCreate(
      nullptr, width, height, 8, width * 4, cs,
      kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little /* BGRA */
  );
  CGColorSpaceRelease(cs);
  if (ctx == nullptr) {
    LOG_DEBUG("clipboard.tiff could not create bitmap context");
    return nullptr;
  }

  auto *dst = static_cast<uint8_t *>(CGBitmapContextGetData(ctx));
  if (bitCount == 32) {
    for (int y = 0; y < height; ++y) {
      const int srcY = rawHeight > 0 ? (height - 1 - y) : y; // flip bottom-up rows
      const auto *src = pixels + static_cast<size_t>(srcY) * bytesPerRow;
      memcpy(dst + static_cast<size_t>(y) * width * 4, src, static_cast<size_t>(width) * 4);
    }
  } else {
    // 24bpp: expand BGR triplets to BGRA.
    for (int y = 0; y < height; ++y) {
      const int srcY = rawHeight > 0 ? (height - 1 - y) : y;
      const auto *src = pixels + static_cast<size_t>(srcY) * bytesPerRow;
      auto *out = dst + static_cast<size_t>(y) * width * 4;
      for (int x = 0; x < width; ++x) {
        out[x * 4 + 0] = src[x * 3 + 0]; // B
        out[x * 4 + 1] = src[x * 3 + 1]; // G
        out[x * 4 + 2] = src[x * 3 + 2]; // R
        out[x * 4 + 3] = 0xFF;           // A
      }
    }
  }

  CGImageRef image = CGBitmapContextCreateImage(ctx);
  CGContextRelease(ctx);
  return image;
}

//! Encode a CGImage into a standard 32bpp BI_RGB top-down DIB.
std::string cgImageToDib(CGImageRef image)
{
  if (image == nullptr) {
    return std::string();
  }
  const auto width = static_cast<int>(CGImageGetWidth(image));
  const auto height = static_cast<int>(CGImageGetHeight(image));
  if (width <= 0 || height <= 0) {
    return std::string();
  }

  CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
  CGContextRef ctx = CGBitmapContextCreate(
      nullptr, width, height, 8, width * 4, cs,
      kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little /* BGRA */
  );
  CGColorSpaceRelease(cs);
  if (ctx == nullptr) {
    LOG_DEBUG("clipboard.tiff could not create bitmap context");
    return std::string();
  }
  CGContextDrawImage(ctx, CGRectMake(0, 0, width, height), image);
  const auto *src = static_cast<const uint8_t *>(CGBitmapContextGetData(ctx));

  const uint32_t bytesPerRow = static_cast<uint32_t>(width) * 4;
  const uint32_t pixelArraySize = bytesPerRow * static_cast<uint32_t>(height);
  std::string dib;
  dib.resize(kBiSize + pixelArraySize);
  auto *raw = reinterpret_cast<uint8_t *>(dib.data());
  qToLittleEndian(kBiSize, raw);
  qToLittleEndian(static_cast<int32_t>(width), raw + 4);
  qToLittleEndian(static_cast<int32_t>(height), raw + 8); // positive = top-down
  qToLittleEndian<uint16_t>(1, raw + 12);                 // planes
  qToLittleEndian<uint16_t>(32, raw + 14);                // bit count
  qToLittleEndian<uint32_t>(0, raw + 16);                 // BI_RGB
  qToLittleEndian<uint32_t>(pixelArraySize, raw + 20);    // size image
  qToLittleEndian<int32_t>(2835, raw + 24);               // X ppm (72 dpi)
  qToLittleEndian<int32_t>(2835, raw + 28);               // Y ppm
  qToLittleEndian<uint32_t>(0, raw + 32);                 // clr used
  qToLittleEndian<uint32_t>(0, raw + 36);                 // clr important

  memcpy(raw + kBiSize, src, pixelArraySize);
  CGContextRelease(ctx);
  return dib;
}
} // namespace

CFStringRef OSXClipboardTIFFConverter::getOSXFormat() const
{
  return CFSTR("public.tiff");
}

std::string OSXClipboardTIFFConverter::doFromIClipboard(const std::string &dib) const
{
  CGImageRef image = dibToCGImage(dib);
  if (image == nullptr) {
    return std::string();
  }

  CFMutableDataRef data = CFDataCreateMutable(kCFAllocatorDefault, 0);
  CGImageDestinationRef dest = CGImageDestinationCreateWithData(data, CFSTR("public.tiff"), 1, nullptr);
  std::string result;
  if (dest != nullptr) {
    CGImageDestinationAddImage(dest, image, nullptr);
    if (CGImageDestinationFinalize(dest)) {
      result.assign(reinterpret_cast<const char *>(CFDataGetBytePtr(data)), CFDataGetLength(data));
    } else {
      LOG_DEBUG("clipboard.tiff tiff encoding failed");
    }
    CFRelease(dest);
  }
  CFRelease(data);
  CGImageRelease(image);
  return result;
}

std::string OSXClipboardTIFFConverter::doToIClipboard(const std::string &tiff) const
{
  if (tiff.empty()) {
    return std::string();
  }
  CFDataRef data = CFDataCreate(
      kCFAllocatorDefault, reinterpret_cast<const uint8_t *>(tiff.data()), tiff.size()
  );
  CGImageSourceRef source = CGImageSourceCreateWithData(data, nullptr);
  CFRelease(data);
  if (source == nullptr) {
    LOG_DEBUG("clipboard.tiff could not decode tiff source");
    return std::string();
  }
  CGImageRef image = CGImageSourceCreateImageAtIndex(source, 0, nullptr);
  CFRelease(source);
  if (image == nullptr) {
    LOG_DEBUG("clipboard.tiff could not decode tiff image");
    return std::string();
  }

  std::string dib = cgImageToDib(image);
  CGImageRelease(image);
  return dib;
}
