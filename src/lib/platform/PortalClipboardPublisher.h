#pragma once

#include "deskflow/IClipboard.h"

#include <QByteArray>
#include <QString>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

namespace deskflow {

class EiClipboard;

class PortalClipboardPublisher
{
public:
  using Encoder = std::function<QByteArray(IClipboard::Format, const QByteArray &)>;
  explicit PortalClipboardPublisher(EiClipboard *cache, QString executable = {}, Encoder encoder = {});
  ~PortalClipboardPublisher();
  void submit(IClipboard::Format format, std::string data, uint64_t revision);
  void cancel();
  void shutdown();

private:
  struct Request
  {
    IClipboard::Format format;
    std::string data;
    uint64_t revision;
    uint64_t generation;
  };

  bool current(const Request &request);
  void run();

  EiClipboard *m_cache;
  QString m_executable;
  Encoder m_encoder;
  std::mutex m_mutex;
  std::condition_variable m_wakeup;
  std::optional<Request> m_pending;
  uint64_t m_generation = 0;
  bool m_stopping = false;
  std::thread m_worker;
};

}
