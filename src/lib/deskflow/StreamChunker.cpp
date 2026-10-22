/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2013 - 2016 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/StreamChunker.h"

#include "base/Event.h"
#include "base/IEventQueue.h"
#include "base/Log.h"
#include "deskflow/ClipboardChunk.h"

#include <QString>

namespace {

//! Event payload keeping the Sender alive while a paced turn is queued.
class SenderTick : public EventData
{
public:
  explicit SenderTick(std::shared_ptr<StreamChunker::Sender> sender) : m_sender(std::move(sender))
  {
  }

  std::shared_ptr<StreamChunker::Sender> sender() const
  {
    return m_sender;
  }

private:
  std::shared_ptr<StreamChunker::Sender> m_sender;
};

} // namespace

StreamChunker::Sender::Sender(
    deskflow::IStream *stream, std::string data, ClipboardID id, uint32_t sequence, IEventQueue *events,
    void *eventTarget, ProgressCallback progress
)
    : m_stream(stream),
      m_data(std::move(data)),
      m_id(id),
      m_sequence(sequence),
      m_events(events),
      m_target(eventTarget),
      m_progress(std::move(progress)),
      m_pacer(m_data.size())
{
}

StreamChunker::Sender::~Sender() = default;

void StreamChunker::Sender::detach()
{
  m_progress = nullptr;
}

void StreamChunker::Sender::cancel()
{
  m_cancelled = true;
}

void StreamChunker::Sender::start(const std::shared_ptr<Sender> &self)
{
  // send first message (data size)
  std::string dataSize = QString::number(m_data.size()).toStdString();
  ClipboardChunk *sizeMessage = ClipboardChunk::start(m_id, m_sequence, dataSize);
  ClipboardChunk::send(m_stream, sizeMessage);
  delete sizeMessage;

  sendNext(self);
}

void StreamChunker::Sender::scheduleNext(const std::shared_ptr<Sender> &self)
{
  m_events->addEvent(Event(EventTypes::ClipboardSending, m_target, new SenderTick(self)));
}

void StreamChunker::Sender::sendNext(const std::shared_ptr<Sender> &self)
{
  if (m_cancelled) {
    LOG_DEBUG("clipboard send cancelled");
    return;
  }

  size_t offset = 0;
  size_t length = 0;
  while (m_pacer.next(offset, length)) {
    std::string chunk(m_data.data() + offset, length);
    ClipboardChunk *dataChunk = ClipboardChunk::data(m_id, m_sequence, chunk);
    ClipboardChunk::send(m_stream, dataChunk);
    delete dataChunk;

    // Report bytes that are actually on the wire; the end chunk below reports
    // completion, so this must stay below the total.
    const bool finished = m_pacer.sent() >= m_pacer.total();
    if (m_progress != nullptr && !finished) {
      m_progress(m_pacer.sent(), m_pacer.total());
    }

    if (m_pacer.shouldYield()) {
      scheduleNext(self);
      return;
    }
  }

  if (m_cancelled) {
    return;
  }

  // send last message, then report completion with the final byte count
  ClipboardChunk *end = ClipboardChunk::end(m_id, m_sequence);
  ClipboardChunk::send(m_stream, end);
  delete end;

  if (m_progress != nullptr) {
    m_progress(m_pacer.sent(), m_pacer.total());
  }

  LOG_DEBUG("sent clipboard size=%d", m_pacer.sent());
}

std::shared_ptr<StreamChunker::Sender> StreamChunker::sendClipboard(
    deskflow::IStream *stream, std::string data, ClipboardID id, uint32_t sequence, IEventQueue *events,
    void *eventTarget, ProgressCallback progress
)
{
  auto sender =
      std::make_shared<Sender>(stream, std::move(data), id, sequence, events, eventTarget, std::move(progress));
  sender->start(sender);
  return sender;
}

void StreamChunker::handleEvent(deskflow::IStream *stream, EventData *data)
{
  if (auto *tick = dynamic_cast<SenderTick *>(data); tick != nullptr) {
    tick->sender()->sendNext(tick->sender());
    return;
  }

  ClipboardChunk::send(stream, data);
}
