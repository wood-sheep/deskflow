#include "platform/PortalClipboardPublisher.h"

#include "arch/Arch.h"
#include "base/Log.h"
#include "deskflow/Clipboard.h"
#include "platform/EiClipboard.h"

#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <condition_variable>
#include <future>

class PortalClipboardPublisherTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void initTestCase();
  void submitDoesNotWaitAndOnlyLatestPendingPublishes();
  void localCopySupersedesInFlightConversion();
  void cancelAndShutdownDiscardPending();
  void previousOwnerSurvivesUntilReplacementIsReady();
  void cacheTransactionsAreAtomic();
  void cacheAssignmentPreservesFormatsAndInvalidatesOldPublication();

private:
  Arch m_arch;
  Log m_log;
};

using deskflow::EiClipboard;
using deskflow::PortalClipboardPublisher;

class PausedEncoder
{
public:
  QByteArray operator()(IClipboard::Format, const QByteArray &data)
  {
    std::unique_lock lock(m_mutex);
    m_calls.append(QByteArray(data.constData(), data.size()));
    m_started = true;
    m_changed.notify_all();
    m_changed.wait(lock, [this] { return m_released; });
    return data;
  }

  bool waitUntilStarted()
  {
    std::unique_lock lock(m_mutex);
    return m_changed.wait_for(lock, std::chrono::seconds(2), [this] { return m_started; });
  }

  void release()
  {
    std::scoped_lock lock(m_mutex);
    m_released = true;
    m_changed.notify_all();
  }

  QList<QByteArray> calls()
  {
    std::scoped_lock lock(m_mutex);
    return m_calls;
  }

private:
  std::mutex m_mutex;
  std::condition_variable m_changed;
  QList<QByteArray> m_calls;
  bool m_started = false;
  bool m_released = false;
};

static uint64_t setText(EiClipboard &cache, const QByteArray &data)
{
  cache.open(0);
  cache.empty();
  cache.add(IClipboard::Format::Text, data.toStdString());
  const auto revision = cache.revision();
  cache.close();
  return revision;
}

static QString createHelper(const QTemporaryDir &directory)
{
  QFile helper(directory.filePath("helper"));
  if (!helper.open(QIODevice::WriteOnly))
    return {};
  helper.write("#!/bin/sh\ncat > '" + directory.filePath("result").toUtf8() + "'\nexec sleep 5\n");
  helper.close();
  helper.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
  return helper.fileName();
}

static QByteArray result(const QTemporaryDir &directory)
{
  QFile output(directory.filePath("result"));
  if (!output.open(QIODevice::ReadOnly))
    return {};
  return output.readAll();
}

void PortalClipboardPublisherTests::initTestCase()
{
  m_arch.init();
}

void PortalClipboardPublisherTests::submitDoesNotWaitAndOnlyLatestPendingPublishes()
{
  QTemporaryDir directory;
  EiClipboard cache(kClipboardClipboard);
  PausedEncoder encoder;
  PortalClipboardPublisher publisher(&cache, createHelper(directory), std::ref(encoder));
  publisher.submit(IClipboard::Format::Text, "first", setText(cache, "first"));
  const bool started = encoder.waitUntilStarted();
  QElapsedTimer timer;
  timer.start();
  publisher.submit(IClipboard::Format::Text, "discarded", setText(cache, "discarded"));
  publisher.submit(IClipboard::Format::Text, "latest", setText(cache, "latest"));
  const auto elapsed = timer.elapsed();
  encoder.release();
  QVERIFY(started);
  QVERIFY(elapsed < 50);
  QTRY_COMPARE_WITH_TIMEOUT(result(directory), QByteArray("latest"), 2000);
  publisher.shutdown();
  QCOMPARE(encoder.calls(), QList<QByteArray>({"first", "latest"}));
}

void PortalClipboardPublisherTests::localCopySupersedesInFlightConversion()
{
  QTemporaryDir directory;
  EiClipboard cache(kClipboardClipboard);
  PausedEncoder encoder;
  PortalClipboardPublisher publisher(&cache, createHelper(directory), std::ref(encoder));
  publisher.submit(IClipboard::Format::Text, "remote", setText(cache, "remote"));
  const bool started = encoder.waitUntilStarted();
  setText(cache, "local");
  encoder.release();
  QTest::qWait(100);
  publisher.shutdown();
  QVERIFY(started);
  QVERIFY(!QFile::exists(directory.filePath("result")));
  cache.open(0);
  const auto text = cache.get(IClipboard::Format::Text);
  cache.close();
  QCOMPARE(text, std::string("local"));
}

void PortalClipboardPublisherTests::cancelAndShutdownDiscardPending()
{
  QTemporaryDir directory;
  EiClipboard cache(kClipboardClipboard);
  PausedEncoder encoder;
  PortalClipboardPublisher publisher(&cache, createHelper(directory), std::ref(encoder));
  publisher.submit(IClipboard::Format::Text, "old", setText(cache, "old"));
  const bool started = encoder.waitUntilStarted();
  publisher.submit(IClipboard::Format::Text, "pending", setText(cache, "pending"));
  publisher.cancel();
  encoder.release();
  publisher.shutdown();
  QVERIFY(started);
  QCOMPARE(encoder.calls(), QList<QByteArray>({"old"}));
  QVERIFY(!QFile::exists(directory.filePath("result")));
}

void PortalClipboardPublisherTests::previousOwnerSurvivesUntilReplacementIsReady()
{
  QTemporaryDir directory;
  const auto processPath = directory.filePath("process").toUtf8();
  const auto statusPath = directory.filePath("status").toUtf8();
  QFile helper(directory.filePath("helper"));
  QVERIFY(helper.open(QIODevice::WriteOnly));
  helper.write(
      "#!/bin/sh\nprevious=$(cat '" + processPath +
      "' 2>/dev/null)\ncat >/dev/null\n"
      "if [ -n \"$previous\" ]; then\nsleep 0.1\n"
      "if kill -0 \"$previous\" 2>/dev/null; then printf alive; else printf missing; fi > '" +
      statusPath + "'\nfi\nprintf '%s' $$ > '" + processPath + "'\nexec sleep 5\n"
  );
  helper.close();
  helper.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
  EiClipboard cache(kClipboardClipboard);
  PortalClipboardPublisher publisher(&cache, helper.fileName());
  publisher.submit(IClipboard::Format::Text, "first", setText(cache, "first"));
  QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(QString::fromUtf8(processPath)), 2000);
  publisher.submit(IClipboard::Format::Text, "second", setText(cache, "second"));
  QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(QString::fromUtf8(statusPath)), 2000);
  QFile status(QString::fromUtf8(statusPath));
  QVERIFY(status.open(QIODevice::ReadOnly));
  QCOMPARE(status.readAll(), QByteArray("alive"));
}

void PortalClipboardPublisherTests::cacheTransactionsAreAtomic()
{
  EiClipboard cache(kClipboardClipboard);
  setText(cache, "before");
  cache.open(0);
  auto reader = std::async(std::launch::async, [&cache] {
    cache.open(0);
    const auto text = cache.get(IClipboard::Format::Text);
    cache.close();
    return text;
  });
  const auto state = reader.wait_for(std::chrono::milliseconds(20));
  cache.empty();
  cache.add(IClipboard::Format::Text, "after");
  cache.close();
  QCOMPARE(state, std::future_status::timeout);
  QCOMPARE(reader.get(), std::string("after"));
}

void PortalClipboardPublisherTests::cacheAssignmentPreservesFormatsAndInvalidatesOldPublication()
{
  EiClipboard cache(kClipboardClipboard);
  const auto oldRevision = setText(cache, "old");
  Clipboard source;
  source.open(123);
  source.empty();
  source.add(IClipboard::Format::Text, "new");
  source.add(IClipboard::Format::HTML, "<p>new</p>");
  QCOMPARE(source.getSize(IClipboard::Format::Text), size_t(3));
  source.close();
  QVERIFY(cache.assign(&source));
  QVERIFY(!cache.replaceIfCurrent(oldRevision, IClipboard::Format::Text, "stale"));
  cache.open(0);
  const auto text = cache.get(IClipboard::Format::Text);
  const auto html = cache.get(IClipboard::Format::HTML);
  const auto timestamp = cache.getTime();
  cache.close();
  QCOMPARE(text, std::string("new"));
  QCOMPARE(html, std::string("<p>new</p>"));
  QCOMPARE(timestamp, IClipboard::Time(123));
}

QTEST_GUILESS_MAIN(PortalClipboardPublisherTests)
#include "PortalClipboardPublisherTests.moc"
