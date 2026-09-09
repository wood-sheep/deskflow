/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/PortalClipboard.h"

#include "base/Log.h"
#include "common/Settings.h"
#include "deskflow/ClipboardLimits.h"
#include "platform/EiClipboard.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <QBuffer>
#include <QByteArrayList>
#include <QDataStream>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QList>
#include <QPair>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>
#include <QVarLengthArray>
#include <QtEndian>

namespace deskflow {

static constexpr int kBmpSignatureSize = 2;
static constexpr quint32 kBmpFileHeaderSize = 14;
static constexpr quint32 kMinDibHeaderSize = 12;

QByteArray PortalClipboard::formatMimeTypes(const char *const *mimeTypes)
{
  if (!mimeTypes || !mimeTypes[0])
    return QByteArrayLiteral("(none)");

  QByteArrayList parts;
  for (int i = 0; mimeTypes[i]; ++i)
    parts.append(mimeTypes[i]);
  return parts.join(", ");
}

const PortalClipboard::SupportedMime *PortalClipboard::findSupportedMime(const char *mime)
{
  if (!mime)
    return nullptr;

  for (const auto &entry : kSupportedMimes) {
    if (g_strcmp0(mime, entry.mime) == 0)
      return &entry;
  }

  return nullptr;
}

const PortalClipboard::SupportedMime *PortalClipboard::pickSupportedMime(const char *const *available)
{
  if (!available)
    return nullptr;

  for (const auto &entry : kSupportedMimes) {
    if (g_strv_contains(available, entry.mime))
      return &entry;
  }

  return nullptr;
}

QByteArray PortalClipboard::dibToBmp(const QByteArray &dib)
{
  if (dib.size() < static_cast<qint64>(sizeof(quint32)))
    return {};

  quint32 headerSize;
  std::memcpy(&headerSize, dib.constData(), sizeof(headerSize));
  headerSize = qFromLittleEndian(headerSize);
  if (headerSize < kMinDibHeaderSize || headerSize > static_cast<quint32>(dib.size()))
    return {};

  const auto fileSize = static_cast<quint32>(kBmpFileHeaderSize + dib.size());
  const quint32 pixelOffset = kBmpFileHeaderSize + headerSize;

  QByteArray bmp;
  QDataStream ds(&bmp, QIODevice::WriteOnly);
  ds.setByteOrder(QDataStream::LittleEndian);
  ds.writeRawData("BM", kBmpSignatureSize);
  ds << fileSize;
  ds << quint32(0);
  ds << pixelOffset;
  ds.writeRawData(dib.constData(), static_cast<int>(dib.size()));
  return bmp;
}

QByteArray PortalClipboard::bmpToDib(const QByteArray &bmp)
{
  if (bmp.size() < kBmpFileHeaderSize)
    return {};

  return bmp.mid(kBmpFileHeaderSize);
}

QImage PortalClipboard::readBoundedImage(const QByteArray &bytes, const QByteArray &format)
{
  QBuffer source;
  source.setData(bytes);
  source.open(QIODevice::ReadOnly);
  QImageReader reader(&source, format);
  const auto size = reader.size();
  const qint64 width = size.width();
  const qint64 rawHeight = size.height();
  const qint64 height = format == "BMP" && rawHeight < 0 ? -rawHeight : rawHeight;
  const qint64 pixels = width * height;
  if (width <= 0 || height <= 0 || pixels > (static_cast<qint64>(ClipboardLimits::maximumBytes) - 64) / 4) {
    LOG_WARN("skipping clipboard image with oversized or invalid dimensions: %dx%d", size.width(), size.height());
    return {};
  }
  return reader.read();
}

QByteArray PortalClipboard::encodeFormat(IClipboard::Format format, const QByteArray &data)
{
  if (data.isEmpty() || data.size() > static_cast<qint64>(ClipboardLimits::forFormat(format)))
    return {};

  if (format == IClipboard::Format::Bitmap) {
    const auto bmpFile = dibToBmp(data);
    if (bmpFile.isEmpty()) {
      LOG_WARN("clipboard bitmap data is malformed");
      return {};
    }

    const auto image = readBoundedImage(bmpFile, "BMP");
    if (image.isNull()) {
      LOG_WARN("failed to decode clipboard bitmap");
      return {};
    }

    QByteArray png;
    QBuffer buf(&png);
    buf.open(QIODevice::WriteOnly);
    if (!image.save(&buf, "PNG")) {
      LOG_WARN("failed to encode clipboard image as png");
      return {};
    }

    return png;
  }
  return data;
}

QByteArray PortalClipboard::decodeFormat(IClipboard::Format format, const QByteArray &bytes)
{
  if (bytes.isEmpty() || bytes.size() > static_cast<qint64>(ClipboardLimits::forFormat(format)))
    return {};

  if (format == IClipboard::Format::Bitmap) {
    const auto image = readBoundedImage(bytes, "PNG");
    if (image.isNull()) {
      LOG_WARN("failed to decode clipboard png");
      return {};
    }

    QByteArray bmp;
    QBuffer buf(&bmp);
    buf.open(QIODevice::WriteOnly);
    if (!image.save(&buf, "BMP")) {
      LOG_WARN("failed to encode clipboard image as bmp");
      return {};
    }

    return bmpToDib(bmp);
  }
  return bytes;
}

QByteArray PortalClipboard::readSelectionBytes(XdpSession *session, const char *mime, qint64 maxBytes)
{
  const int fd = xdp_session_selection_read(session, mime);
  if (fd < 0) {
    LOG_ERR("failed to read clipboard selection: invalid fd");
    return {};
  }

  return readPipeBytes(fd, maxBytes);
}

QByteArray PortalClipboard::readPipeBytes(int fd, qint64 maxBytes)
{
  QFile pipe;
  if (!pipe.open(fd, QIODevice::ReadOnly | QIODevice::Unbuffered, QFileDevice::AutoCloseHandle)) {
    LOG_WARN("failed to wrap clipboard pipe");
    ::close(fd);
    return {};
  }

  maxBytes = std::min(maxBytes, static_cast<qint64>(ClipboardLimits::maximumBytes));
  const int flags = fcntl(fd, F_GETFL);
  if (maxBytes <= 0 || flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
    return {};

  QElapsedTimer timer;
  timer.start();
  QByteArray contents;
  contents.reserve(std::min<qint64>(maxBytes, kChunkBytes));
  std::array<char, kChunkBytes> buffer;
  while (timer.elapsed() < kReadTimeoutMs) {
    pollfd pfd{fd, POLLIN, 0};
    const auto ready = poll(&pfd, 1, std::max(1, kReadTimeoutMs - static_cast<int>(timer.elapsed())));
    if (ready < 0 && errno == EINTR)
      continue;
    if (ready <= 0)
      break;

    const auto count = ::read(fd, buffer.data(), std::min<qint64>(buffer.size(), maxBytes - contents.size() + 1));
    if (count == 0)
      return contents;
    if (count < 0) {
      if (errno == EAGAIN || errno == EINTR)
        continue;
      return {};
    }
    if (count > maxBytes - contents.size()) {
      LOG_WARN("skipping oversized local clipboard, limit: %lld bytes", static_cast<long long>(maxBytes));
      return {};
    }
    contents.append(buffer.data(), count);
  }
  LOG_WARN("skipping incomplete clipboard read after timeout");
  return {};
}

bool PortalClipboard::publishWithHelper(EiClipboard *cache)
{
  const bool allFormats = Settings::value(Settings::Client::WaylandClipboardHelper).toBool();
  if (!allFormats && !Settings::value(Settings::Client::WaylandClipboardImageHelper).toBool())
    return false;

  cache->open(0);
  const bool hasImage = cache->has(IClipboard::Format::Bitmap);
  const auto format = hasImage ? IClipboard::Format::Bitmap : IClipboard::Format::Text;
  const bool supported = cache->has(format) && (hasImage || allFormats);
  const auto raw = supported ? QByteArray::fromStdString(cache->get(format)) : QByteArray();
  cache->close();
  if (!supported)
    return false;

  const auto executable = QStandardPaths::findExecutable(QStringLiteral("wl-copy"));
  const auto encoded = encodeFormat(format, raw);
  if (executable.isEmpty() || encoded.isEmpty()) {
    LOG_WARN("Wayland clipboard helper unavailable or clipboard rejected");
    return allFormats;
  }

  const auto normalized = decodeFormat(format, encoded);
  if (normalized.isEmpty())
    return allFormats;

  cache->open(0);
  cache->empty();
  cache->add(format, normalized.toStdString());
  cache->close();

  QProcess helper;
  helper.setUnixProcessParameters(QProcess::UnixProcessFlag::CloseFileDescriptors);
  helper.setStandardOutputFile(QProcess::nullDevice());
  helper.setStandardErrorFile(QProcess::nullDevice());
  const auto mime = hasImage ? QStringLiteral("image/png") : QStringLiteral("text/plain;charset=utf-8");
  helper.start(executable, {QStringLiteral("--type"), mime});
  if (!helper.waitForStarted(500))
    return allFormats;
  helper.write(encoded);
  helper.closeWriteChannel();
  if (!helper.waitForFinished(500) || helper.exitStatus() != QProcess::NormalExit || helper.exitCode() != 0) {
    LOG_WARN("Wayland clipboard helper failed; clipboard not published");
    return allFormats;
  }

  LOG_DEBUG("published clipboard through Wayland data control, bytes: %lld", static_cast<long long>(encoded.size()));
  return true;
}

void PortalClipboard::claimOwnership(EiClipboard *cache, XdpSession *session)
{
  if (!cache || !session)
    return;

  if (publishWithHelper(cache))
    return;

  cache->open(0);
  QVarLengthArray<const char *, std::size(kSupportedMimes) + 1> mimeTypes;
  for (const auto &entry : kSupportedMimes) {
    if (cache->has(entry.format))
      mimeTypes.append(entry.mime);
  }
  cache->close();

  if (mimeTypes.isEmpty()) {
    LOG_DEBUG("clipboard cache empty, nothing to claim");
    return;
  }
  mimeTypes.append(nullptr);

  LOG_DEBUG("claiming clipboard, mimes: %s", formatMimeTypes(mimeTypes.data()).constData());
  xdp_session_set_selection(session, mimeTypes.data());
}

void PortalClipboard::serveSelectionTransfer(EiClipboard *cache, XdpSession *session, const char *mime, uint32_t serial)
{
  LOG_DEBUG("clipboard selection transfer requested, mime: %s, serial: %u", mime, serial);

  const auto *requested = findSupportedMime(mime);
  if (!requested || !cache) {
    LOG_DEBUG("rejecting clipboard selection, unsupported mime: %s", mime);
    xdp_session_selection_write_done(session, serial, false);
    return;
  }

  cache->open(0);
  QByteArray raw;
  const bool hasFormat = cache->has(requested->format);
  if (hasFormat)
    raw = QByteArray::fromStdString(cache->get(requested->format));
  cache->close();

  const auto data = encodeFormat(requested->format, raw);
  if (data.isEmpty()) {
    LOG_DEBUG("clipboard has no data for mime: %s", mime);
    xdp_session_selection_write_done(session, serial, false);
    return;
  }

  const int fd = xdp_session_selection_write(session, serial);
  if (fd < 0) {
    LOG_WARN("failed to open clipboard selection write fd");
    xdp_session_selection_write_done(session, serial, false);
    return;
  }

  const bool success = writeSelectionBytes(fd, data);
  xdp_session_selection_write_done(session, serial, success);
}

bool PortalClipboard::writeSelectionBytes(int fd, const QByteArray &data)
{
  QFile pipe;
  if (!pipe.open(fd, QIODevice::WriteOnly | QIODevice::Unbuffered, QFileDevice::AutoCloseHandle)) {
    LOG_WARN("failed to wrap clipboard pipe");
    ::close(fd);
    return false;
  }

  const int flags = fcntl(fd, F_GETFL);
  if (data.size() > static_cast<qint64>(ClipboardLimits::maximumBytes) || flags < 0 ||
      fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
    return false;

  QElapsedTimer timer;
  timer.start();

  const char *buf = data.constData();
  qint64 total = data.size();
  qint64 written = 0;
  while (written < total) {
    pollfd pfd{fd, POLLOUT, 0};
    const int remaining = kWriteTimeoutMs - static_cast<int>(timer.elapsed());
    if (remaining <= 0) {
      LOG_WARN("skipping clipboard write after timeout");
      return false;
    }
    const auto ready = poll(&pfd, 1, remaining);
    if (ready < 0 && errno == EINTR)
      continue;
    if (ready <= 0) {
      LOG_ERR("timed out writing clipboard selection");
      return false;
    }

    qint64 n = ::write(fd, buf + written, std::min(kChunkBytes, total - written));
    if (n < 0 && (errno == EAGAIN || errno == EINTR))
      continue;
    if (n <= 0) {
      LOG_ERR("clipboard pipe write returned %lld", static_cast<long long>(n));
      return false;
    }
    written += n;
  }

  pipe.close();
  LOG_DEBUG("clipboard selection transfer complete, bytes: %lld", static_cast<long long>(written));
  return true;
}

bool PortalClipboard::readSelectionIntoCache(
    EiClipboard *cache, XdpSession *session, const char *const *mimeTypes, qint64 maxBytes
)
{
  if (!cache || !session || !mimeTypes || !mimeTypes[0])
    return false;

  if (!pickSupportedMime(mimeTypes)) {
    LOG_DEBUG("clipboard no supported mime types: %s", formatMimeTypes(mimeTypes).constData());
    return false;
  }

  QList<QPair<IClipboard::Format, QByteArray>> reads;
  const auto revision = cache->revision();
  QSet<IClipboard::Format> seen;
  for (const auto &entry : kSupportedMimes) {
    if (seen.contains(entry.format))
      continue;
    if (!g_strv_contains(mimeTypes, entry.mime))
      continue;

    seen.insert(entry.format);
    const auto formatLimit = std::min(maxBytes, static_cast<qint64>(ClipboardLimits::forFormat(entry.format)));
    auto bytes = readSelectionBytes(session, entry.mime, formatLimit);
    if (bytes.isEmpty()) {
      LOG_DEBUG("clipboard read returned no data for mime: %s", entry.mime);
      continue;
    }

    if (entry.format != IClipboard::Format::Bitmap) {
      while (bytes.endsWith('\0'))
        bytes.chop(1);
      bytes.replace("\r\n", "\n");
    }

    auto data = decodeFormat(entry.format, bytes);
    if (data.isEmpty())
      continue;

    reads.append({entry.format, std::move(data)});
  }

  if (reads.isEmpty()) {
    LOG_DEBUG("clipboard read produced no data, leaving existing clipboard intact");
    return false;
  }

  cache->open(0);
  if (cache->revision() != revision) {
    cache->close();
    return false;
  }
  bool changed = false;
  for (int formatIndex = 0; formatIndex < static_cast<int>(IClipboard::Format::TotalFormats); ++formatIndex) {
    const auto format = static_cast<IClipboard::Format>(formatIndex);
    const auto incoming =
        std::find_if(reads.cbegin(), reads.cend(), [format](const auto &entry) { return entry.first == format; });
    const bool received = incoming != reads.cend();
    if (cache->has(format) != received || (received && cache->get(format) != incoming->second.toStdString())) {
      changed = true;
      break;
    }
  }
  if (!changed) {
    cache->close();
    LOG_DEBUG("clipboard contents unchanged, ignoring ownership notification");
    return false;
  }
  cache->empty();
  for (const auto &[format, data] : reads)
    cache->add(format, data.toStdString());
  cache->close();

  LOG_DEBUG("clipboard read local selection, formats: %lld", static_cast<long long>(reads.size()));
  return true;
}

} // namespace deskflow
