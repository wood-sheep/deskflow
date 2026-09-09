#include "io/StreamBuffer.h"

#include <QByteArray>
#include <QTest>

class StreamBufferTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void repeatedUnalignedPacketsDoNotRetainConsumedPrefixes()
  {
    constexpr uint32_t payloadSize = 64 * 1024;
    constexpr uint32_t headerSize = 18;
    constexpr uint32_t packetCount = 128;
    const QByteArray header(headerSize, 'h');
    const QByteArray payload(payloadSize, 'p');
    StreamBuffer buffer;
    for (uint32_t packet = 0; packet < packetCount; ++packet) {
      buffer.write(header.constData(), headerSize);
      buffer.write(payload.constData(), payloadSize);
    }
    for (uint32_t packet = 0; packet < packetCount; ++packet) {
      QCOMPARE(QByteArray(static_cast<const char *>(buffer.peek(headerSize)), headerSize), header);
      buffer.pop(headerSize);
      QCOMPARE(QByteArray(static_cast<const char *>(buffer.peek(payloadSize)), payloadSize), payload);
      buffer.pop(payloadSize);
      QVERIFY(buffer.m_headUsed <= payloadSize + headerSize);
    }
    QCOMPARE(buffer.getSize(), uint32_t(0));
  }

  void compactionPreservesUnreadDataAndSubsequentWrites()
  {
    QByteArray source(12000, '\0');
    for (qsizetype index = 0; index < source.size(); ++index)
      source[index] = static_cast<char>(index % 251);
    StreamBuffer buffer;
    buffer.write(source.constData(), 5000);
    buffer.pop(3000);
    buffer.write(source.constData() + 5000, 7000);
    QCOMPARE(QByteArray(static_cast<const char *>(buffer.peek(6000)), 6000), source.mid(3000, 6000));
    buffer.pop(6000);
    QCOMPARE(QByteArray(static_cast<const char *>(buffer.peek(3000)), 3000), source.mid(9000));
    buffer.pop(3000);
    buffer.write("wheel", 5);
    QCOMPARE(QByteArray(static_cast<const char *>(buffer.peek(5)), 5), QByteArray("wheel"));
  }
};

QTEST_GUILESS_MAIN(StreamBufferTests)
#include "StreamBufferTests.moc"
