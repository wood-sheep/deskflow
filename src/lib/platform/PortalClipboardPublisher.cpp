#include "platform/PortalClipboardPublisher.h"

#include "base/Log.h"
#include "deskflow/ScrollDiagnostics.h"
#include "platform/EiClipboard.h"
#include "platform/PortalClipboard.h"

#include <QElapsedTimer>
#include <QProcess>
#include <QStandardPaths>

namespace deskflow {

PortalClipboardPublisher::PortalClipboardPublisher(EiClipboard *cache, QString executable, Encoder encoder)
    : m_cache(cache),
      m_executable(executable.isEmpty() ? QStandardPaths::findExecutable(QStringLiteral("wl-copy")) : executable),
      m_encoder(encoder ? std::move(encoder) : PortalClipboard::encodeFormat),
      m_worker([this] { run(); })
{
}

PortalClipboardPublisher::~PortalClipboardPublisher()
{
  shutdown();
}

void PortalClipboardPublisher::submit(IClipboard::Format format, std::string data, uint64_t revision)
{
  std::scoped_lock lock{m_mutex};
  if (m_stopping)
    return;
  m_pending = Request{format, std::move(data), revision, ++m_generation};
  m_wakeup.notify_one();
}

void PortalClipboardPublisher::cancel()
{
  std::scoped_lock lock{m_mutex};
  ++m_generation;
  m_pending.reset();
  m_wakeup.notify_one();
}

void PortalClipboardPublisher::shutdown()
{
  {
    std::scoped_lock lock{m_mutex};
    m_stopping = true;
    m_pending.reset();
    m_wakeup.notify_one();
  }
  if (m_worker.joinable())
    m_worker.join();
}

bool PortalClipboardPublisher::current(const Request &request)
{
  std::scoped_lock lock{m_mutex};
  return !m_stopping && request.generation == m_generation;
}

void PortalClipboardPublisher::run()
{
  std::unique_ptr<QProcess> active;
  while (true) {
    std::optional<Request> request;
    {
      std::unique_lock lock{m_mutex};
      m_wakeup.wait(lock, [this] { return m_stopping || m_pending.has_value(); });
      if (m_stopping)
        break;
      request = std::move(m_pending);
      m_pending.reset();
    }
    logScrollTiming("clipboard-prepare-start");
    const auto raw = QByteArray::fromRawData(request->data.data(), static_cast<qsizetype>(request->data.size()));
    const auto encoded = m_encoder(request->format, raw);
    if (encoded.isEmpty() || !current(*request))
      continue;
    const auto normalized = PortalClipboard::decodeFormat(request->format, encoded);
    if (normalized.isEmpty() || !current(*request))
      continue;
    logScrollTiming("clipboard-prepare-end");
    if (m_executable.isEmpty()) {
      LOG_WARN("Wayland clipboard helper unavailable; publication skipped");
      continue;
    }
    auto helper = std::make_unique<QProcess>();
    helper->setUnixProcessParameters(QProcess::UnixProcessFlag::CloseFileDescriptors);
    helper->setStandardOutputFile(QProcess::nullDevice());
    helper->setStandardErrorFile(QProcess::nullDevice());
    const auto mime = request->format == IClipboard::Format::Bitmap ? QStringLiteral("image/png")
                                                                    : QStringLiteral("text/plain;charset=utf-8");
    m_cache->open(0);
    const bool accepted =
        current(*request) && m_cache->replaceIfCurrent(request->revision, request->format, normalized.toStdString());
    if (accepted)
      helper->start(m_executable, {QStringLiteral("--foreground"), QStringLiteral("--type"), mime});
    m_cache->close();
    if (!accepted)
      continue;
    if (!helper->waitForStarted(500)) {
      LOG_WARN("Wayland clipboard helper failed to start");
      continue;
    }
    helper->write(encoded);
    helper->closeWriteChannel();
    QElapsedTimer timer;
    timer.start();
    while (helper->bytesToWrite() > 0 && timer.elapsed() < 500 && current(*request) &&
           m_cache->revision() == request->revision) {
      helper->waitForBytesWritten(10);
      if (helper->state() == QProcess::NotRunning)
        break;
    }
    if (helper->bytesToWrite() > 0 || !current(*request) || m_cache->revision() != request->revision ||
        helper->state() == QProcess::NotRunning) {
      helper->kill();
      helper->waitForFinished(500);
      continue;
    }
    if (active) {
      if (active->state() != QProcess::NotRunning && !active->waitForFinished(500)) {
        active->kill();
        active->waitForFinished(500);
      }
    }
    active = std::move(helper);
    LOG_DEBUG("published clipboard asynchronously, bytes: %lld", static_cast<long long>(encoded.size()));
    logScrollTiming("clipboard-published");
  }
  if (active) {
    active->kill();
    active->waitForFinished(500);
  }
}

}
