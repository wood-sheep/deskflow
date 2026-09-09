#include "platform/PortalClipboard.h"

#include "arch/Arch.h"
#include "base/Log.h"
#include "deskflow/Clipboard.h"
#include "deskflow/ClipboardLimits.h"

#include <QBuffer>
#include <QDataStream>
#include <QElapsedTimer>
#include <QImage>
#include <QTest>
#include <QtEndian>
#include <fcntl.h>
#include <limits>
#include <unistd.h>

class PortalClipboardTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void initTestCase();
  void textRoundTrip();
  void imageRoundTrip();
  void desktopScreenshotRoundTrip_data();
  void desktopScreenshotRoundTrip();
  void topDownBitmapRoundTrip_data();
  void topDownBitmapRoundTrip();
  void rejectsInvalidTopDownDimensions_data();
  void rejectsInvalidTopDownDimensions();
  void rejectsMalformedImages();
  void prefersImagesAndRejectsUnknownMime();
  void writesAndClosesPipeBeforeReportingSuccess();
  void rejectsOversizedTextWithoutTruncating();
  void rejectsCompressedImageWithLargeDimensions();
  void clipboardPolicyIncludesTextHtmlAndCombinedSize();
  void pipeReadAcceptsExactLimit();
  void pipeReadRejectsOversizeWithoutReturningPrefix();
  void pipeReadTimeoutDropsPartialData();
  void pipeWriteTimeoutIsBounded();

private:
  Arch m_arch;
  Log m_log;
};

using deskflow::PortalClipboard;

void PortalClipboardTests::initTestCase()
{
  m_arch.init();
}

void PortalClipboardTests::textRoundTrip()
{
  const QByteArray text = QStringLiteral("剪贴板测试\nDeskflow").toUtf8();
  QCOMPARE(PortalClipboard::decodeFormat(IClipboard::Format::Text, text), text);
  QCOMPARE(PortalClipboard::encodeFormat(IClipboard::Format::Text, text), text);
}

void PortalClipboardTests::imageRoundTrip()
{
  QImage image(320, 180, QImage::Format_RGB32);
  image.fill(QColor(43, 127, 201));
  image.setPixelColor(100, 100, QColor(240, 40, 90));
  QByteArray png;
  QBuffer buffer(&png);
  QVERIFY(buffer.open(QIODevice::WriteOnly));
  QVERIFY(image.save(&buffer, "PNG"));
  const auto dib = PortalClipboard::decodeFormat(IClipboard::Format::Bitmap, png);
  QVERIFY(!dib.isEmpty());
  const auto output = PortalClipboard::encodeFormat(IClipboard::Format::Bitmap, dib);
  QImage restored;
  QVERIFY(restored.loadFromData(output, "PNG"));
  QCOMPARE(restored.size(), image.size());
  QCOMPARE(restored.pixelColor(0, 0), image.pixelColor(0, 0));
  QCOMPARE(restored.pixelColor(100, 100), image.pixelColor(100, 100));
}

void PortalClipboardTests::rejectsMalformedImages()
{
  QVERIFY(PortalClipboard::decodeFormat(IClipboard::Format::Bitmap, "not png").isEmpty());
  QVERIFY(PortalClipboard::encodeFormat(IClipboard::Format::Bitmap, QByteArray(4, '\xff')).isEmpty());
  QVERIFY(PortalClipboard::encodeFormat(IClipboard::Format::Bitmap, {}).isEmpty());
}

void PortalClipboardTests::desktopScreenshotRoundTrip_data()
{
  QTest::addColumn<QSize>("dimensions");
  QTest::newRow("1080p") << QSize(1920, 1080);
  QTest::newRow("reported-screenshot") << QSize(2035, 1207);
  QTest::newRow("1440p") << QSize(2560, 1440);
}

void PortalClipboardTests::desktopScreenshotRoundTrip()
{
  QFETCH(QSize, dimensions);
  QImage image(dimensions, QImage::Format_RGB32);
  image.fill(QColor(43, 127, 201));
  QByteArray png;
  QBuffer buffer(&png);
  QVERIFY(buffer.open(QIODevice::WriteOnly));
  QVERIFY(image.save(&buffer, "PNG"));
  const auto dib = PortalClipboard::decodeFormat(IClipboard::Format::Bitmap, png);
  QVERIFY(!dib.isEmpty());
  const auto encoded = PortalClipboard::encodeFormat(IClipboard::Format::Bitmap, dib);
  QVERIFY(!encoded.isEmpty());
  QImage restored;
  QVERIFY(restored.loadFromData(encoded, "PNG"));
  QCOMPARE(restored.size(), dimensions);
  QCOMPARE(restored.pixelColor(0, 0), image.pixelColor(0, 0));
}

void PortalClipboardTests::topDownBitmapRoundTrip_data()
{
  QTest::addColumn<QSize>("dimensions");
  QTest::addColumn<int>("bitCount");
  QTest::newRow("mac-798x220-32bpp") << QSize(798, 220) << 32;
  QTest::newRow("mac-790x404-24bpp") << QSize(790, 404) << 24;
  QTest::newRow("mac-1164x322-32bpp") << QSize(1164, 322) << 32;
  QTest::newRow("mac-2202x1536-32bpp") << QSize(2202, 1536) << 32;
}

void PortalClipboardTests::topDownBitmapRoundTrip()
{
  QFETCH(QSize, dimensions);
  QFETCH(int, bitCount);
  const int bytesPerPixel = bitCount / 8;
  const int stride = (dimensions.width() * bytesPerPixel + 3) & ~3;
  const int pixelBytes = stride * dimensions.height();
  QByteArray dib;
  QDataStream header(&dib, QIODevice::WriteOnly);
  header.setByteOrder(QDataStream::LittleEndian);
  header << quint32(40) << qint32(dimensions.width()) << qint32(-dimensions.height());
  header << quint16(1) << quint16(bitCount) << quint32(0) << quint32(pixelBytes);
  header << qint32(0) << qint32(0) << quint32(0) << quint32(0);
  dib.append(QByteArray(pixelBytes, '\0'));
  for (int rowIndex = 0; rowIndex < dimensions.height(); ++rowIndex) {
    const QColor color = rowIndex < dimensions.height() / 2 ? QColor(230, 40, 70) : QColor(20, 90, 210);
    auto *row = dib.data() + 40 + rowIndex * stride;
    for (int columnIndex = 0; columnIndex < dimensions.width(); ++columnIndex) {
      auto *pixel = row + columnIndex * bytesPerPixel;
      pixel[0] = static_cast<char>(color.blue());
      pixel[1] = static_cast<char>(color.green());
      pixel[2] = static_cast<char>(color.red());
      if (bytesPerPixel == 4)
        pixel[3] = static_cast<char>(255);
    }
  }

  QByteArray bmp;
  QDataStream fileHeader(&bmp, QIODevice::WriteOnly);
  fileHeader.setByteOrder(QDataStream::LittleEndian);
  fileHeader.writeRawData("BM", 2);
  fileHeader << quint32(dib.size() + 14) << quint32(0) << quint32(54);
  bmp.append(dib);
  QImage reference;
  QVERIFY(reference.loadFromData(bmp, "BMP"));
  QCOMPARE(reference.size(), dimensions);

  const auto png = PortalClipboard::encodeFormat(IClipboard::Format::Bitmap, dib);
  QVERIFY(!png.isEmpty());
  QImage restored;
  QVERIFY(restored.loadFromData(png, "PNG"));
  QCOMPARE(restored.size(), dimensions);
  QCOMPARE(restored.pixelColor(0, 0), QColor(230, 40, 70));
  QCOMPARE(restored.pixelColor(dimensions.width() - 1, dimensions.height() - 1), QColor(20, 90, 210));
}

void PortalClipboardTests::rejectsInvalidTopDownDimensions_data()
{
  QTest::addColumn<QSize>("dimensions");
  QTest::newRow("oversized") << QSize(3840, -2160);
  QTest::newRow("minimum-height") << QSize(1, std::numeric_limits<int>::min());
  QTest::newRow("negative-width") << QSize(-10, -10);
  QTest::newRow("zero-height") << QSize(10, 0);
  QTest::newRow("zero-width") << QSize(0, -10);
}

void PortalClipboardTests::rejectsInvalidTopDownDimensions()
{
  QFETCH(QSize, dimensions);
  QByteArray dib(44, '\0');
  qToLittleEndian<quint32>(40, dib.data());
  qToLittleEndian<qint32>(dimensions.width(), dib.data() + 4);
  qToLittleEndian<qint32>(dimensions.height(), dib.data() + 8);
  qToLittleEndian<quint16>(1, dib.data() + 12);
  qToLittleEndian<quint16>(32, dib.data() + 14);
  qToLittleEndian<quint32>(4, dib.data() + 20);
  QVERIFY(PortalClipboard::encodeFormat(IClipboard::Format::Bitmap, dib).isEmpty());
}

void PortalClipboardTests::prefersImagesAndRejectsUnknownMime()
{
  const char *mimes[]{"text/plain", "image/png", nullptr};
  QCOMPARE(PortalClipboard::pickSupportedMime(mimes)->format, IClipboard::Format::Bitmap);
  QVERIFY(!PortalClipboard::findSupportedMime("application/octet-stream"));
  QVERIFY(!PortalClipboard::pickSupportedMime(nullptr));
}

void PortalClipboardTests::writesAndClosesPipeBeforeReportingSuccess()
{
  int descriptors[2];
  QVERIFY(pipe2(descriptors, O_NONBLOCK) == 0);
  const QByteArray content = QStringLiteral("剪贴板 pipe flush regression").toUtf8();
  const bool success = PortalClipboard::writeSelectionBytes(descriptors[1], content);
  QByteArray received(content.size(), '\0');
  const auto receivedSize = read(descriptors[0], received.data(), received.size());
  char extra;
  const auto finalSize = read(descriptors[0], &extra, 1);
  close(descriptors[0]);
  QVERIFY(success);
  QCOMPARE(receivedSize, content.size());
  QCOMPARE(received, content);
  QCOMPARE(finalSize, 0);
}

void PortalClipboardTests::rejectsOversizedTextWithoutTruncating()
{
  const QByteArray exact(deskflow::ClipboardLimits::maximumTextBytes, 'x');
  QCOMPARE(PortalClipboard::decodeFormat(IClipboard::Format::Text, exact), exact);
  QVERIFY(PortalClipboard::decodeFormat(IClipboard::Format::Text, exact + 'x').isEmpty());
  QVERIFY(PortalClipboard::encodeFormat(IClipboard::Format::HTML, exact + 'x').isEmpty());
}

void PortalClipboardTests::rejectsCompressedImageWithLargeDimensions()
{
  QImage image(3840, 2160, QImage::Format_RGB32);
  image.fill(Qt::white);
  QByteArray png;
  QBuffer buffer(&png);
  QVERIFY(buffer.open(QIODevice::WriteOnly));
  QVERIFY(image.save(&buffer, "PNG"));
  QVERIFY(png.size() < static_cast<qint64>(deskflow::ClipboardLimits::maximumBytes));
  QVERIFY(PortalClipboard::decodeFormat(IClipboard::Format::Bitmap, png).isEmpty());
}

void PortalClipboardTests::clipboardPolicyIncludesTextHtmlAndCombinedSize()
{
  Clipboard clipboard;
  clipboard.open(0);
  clipboard.add(IClipboard::Format::Text, std::string(deskflow::ClipboardLimits::maximumTextBytes, 'x'));
  clipboard.close();
  QVERIFY(deskflow::ClipboardLimits::allows(&clipboard));
  clipboard.open(0);
  clipboard.add(IClipboard::Format::HTML, std::string(deskflow::ClipboardLimits::maximumTextBytes + 1, 'x'));
  clipboard.close();
  QVERIFY(!deskflow::ClipboardLimits::allows(&clipboard));
  clipboard.open(0);
  clipboard.empty();
  clipboard.add(IClipboard::Format::Bitmap, std::string(deskflow::ClipboardLimits::maximumBytes - 12, 'x'));
  clipboard.close();
  QVERIFY(deskflow::ClipboardLimits::allows(&clipboard));
  clipboard.open(0);
  clipboard.add(IClipboard::Format::Text, "x");
  clipboard.close();
  QVERIFY(!deskflow::ClipboardLimits::allows(&clipboard));
}

void PortalClipboardTests::pipeReadAcceptsExactLimit()
{
  int descriptors[2];
  QVERIFY(pipe(descriptors) == 0);
  QCOMPARE(write(descriptors[1], "1234", 4), 4);
  close(descriptors[1]);
  QCOMPARE(PortalClipboard::readPipeBytes(descriptors[0], 4), QByteArray("1234"));
}

void PortalClipboardTests::pipeReadRejectsOversizeWithoutReturningPrefix()
{
  int descriptors[2];
  QVERIFY(pipe(descriptors) == 0);
  QCOMPARE(write(descriptors[1], "12345", 5), 5);
  close(descriptors[1]);
  QVERIFY(PortalClipboard::readPipeBytes(descriptors[0], 4).isEmpty());
}

void PortalClipboardTests::pipeReadTimeoutDropsPartialData()
{
  int descriptors[2];
  QVERIFY(pipe(descriptors) == 0);
  QCOMPARE(write(descriptors[1], "partial", 7), 7);
  QElapsedTimer timer;
  timer.start();
  const auto result = PortalClipboard::readPipeBytes(descriptors[0], 1024);
  close(descriptors[1]);
  QVERIFY(result.isEmpty());
  QVERIFY(timer.elapsed() < 1500);
}

void PortalClipboardTests::pipeWriteTimeoutIsBounded()
{
  int descriptors[2];
  QVERIFY(pipe(descriptors) == 0);
  QElapsedTimer timer;
  timer.start();
  const bool result = PortalClipboard::writeSelectionBytes(descriptors[1], QByteArray(1024 * 1024, 'x'));
  close(descriptors[0]);
  QVERIFY(!result);
  QVERIFY(timer.elapsed() < 1500);
}

QTEST_GUILESS_MAIN(PortalClipboardTests)
#include "PortalClipboardTests.moc"
