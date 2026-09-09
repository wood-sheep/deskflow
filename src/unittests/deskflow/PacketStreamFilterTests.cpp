#include "arch/Arch.h"
#include "base/EventQueue.h"
#include "base/Log.h"
#include "deskflow/PacketStreamFilter.h"
#include "deskflow/ProtocolTypes.h"

#include <QByteArray>
#include <QTest>
#include <QtEndian>
#include <cstring>

class MemoryPacketStream : public deskflow::IStream
{
public:
  QByteArray bytes;
  int reads = 0;
  void close() override
  {
    bytes.clear();
  }
  uint32_t read(void *target, uint32_t maximum) override
  {
    ++reads;
    const auto count = std::min<qsizetype>(maximum, bytes.size());
    if (target)
      std::memcpy(target, bytes.constData(), count);
    bytes.remove(0, count);
    return static_cast<uint32_t>(count);
  }
  void write(const void *, uint32_t) override
  {
  }
  void flush() override
  {
  }
  void shutdownInput() override
  {
    close();
  }
  void shutdownOutput() override
  {
  }
  void *getEventTarget() const override
  {
    return const_cast<MemoryPacketStream *>(this);
  }
  bool isReady() const override
  {
    return !bytes.isEmpty();
  }
  uint32_t getSize() const override
  {
    return bytes.size();
  }
};

class TestPacketFilter : public PacketStreamFilter
{
public:
  TestPacketFilter(IEventQueue *events, MemoryPacketStream *stream) : PacketStreamFilter(events, stream, false)
  {
  }
  void receive()
  {
    filterEvent(Event(EventTypes::StreamInputReady, getEventTarget()));
  }
};

class PacketStreamFilterTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase()
  {
    m_arch.init();
  }
  void bulkReadPreservesClipboardAndFollowingWheel();
  void fragmentedHeaderAndPayload();
  void rejectsOversizedPacketHeader();

private:
  Arch m_arch;
  Log m_log;
};

static QByteArray packet(const QByteArray &payload)
{
  QByteArray framed(4, '\0');
  qToBigEndian<quint32>(payload.size(), framed.data());
  return framed + payload;
}

void PacketStreamFilterTests::bulkReadPreservesClipboardAndFollowingWheel()
{
  EventQueue events;
  MemoryPacketStream stream;
  TestPacketFilter filter(&events, &stream);
  const QByteArray clipboard(512 * 1024, 'x');
  const QByteArray wheel("DMWM\0\0\0x", 8);
  stream.bytes = packet(clipboard) + packet(wheel);
  filter.receive();
  QVERIFY(stream.reads <= 10);
  QByteArray received(clipboard.size(), '\0');
  QCOMPARE(filter.read(received.data(), received.size()), static_cast<uint32_t>(clipboard.size()));
  QCOMPARE(received, clipboard);
  received.resize(wheel.size());
  QCOMPARE(filter.read(received.data(), received.size()), static_cast<uint32_t>(wheel.size()));
  QCOMPARE(received, wheel);
  QVERIFY(!filter.isReady());
}

void PacketStreamFilterTests::fragmentedHeaderAndPayload()
{
  EventQueue events;
  MemoryPacketStream stream;
  TestPacketFilter filter(&events, &stream);
  const auto framed = packet("DMWM1234");
  for (qsizetype index = 0; index < framed.size(); ++index) {
    stream.bytes.append(framed[index]);
    filter.receive();
    QCOMPARE(filter.isReady(), index == framed.size() - 1);
  }
  QByteArray received(8, '\0');
  QCOMPARE(filter.read(received.data(), received.size()), uint32_t(8));
  QCOMPARE(received, QByteArray("DMWM1234"));
}

void PacketStreamFilterTests::rejectsOversizedPacketHeader()
{
  EventQueue events;
  MemoryPacketStream stream;
  TestPacketFilter filter(&events, &stream);
  stream.bytes.resize(4);
  qToBigEndian<quint32>(PROTOCOL_MAX_MESSAGE_LENGTH + 1, stream.bytes.data());
  filter.receive();
  QVERIFY(!filter.isReady());
}

QTEST_GUILESS_MAIN(PacketStreamFilterTests)
#include "PacketStreamFilterTests.moc"
