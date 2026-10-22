/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2013 - 2016 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "base/Event.h"
#include "deskflow/ClipboardChunkPacer.h"
#include "deskflow/ClipboardTypes.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

class IEventQueue;
namespace deskflow {
class IStream;
}

//! Sends a clipboard payload in paced chunks.
/*!
 * A `Sender` owns the serialized clipboard data and posts itself back onto the
 * event queue until the whole payload has been written, so input events queued
 * behind a large clipboard are dispatched between batches. It is reference
 * counted because queued turns must observe cancellation safely; a newer
 * clipboard cancels the previous sender so their chunks cannot interleave.
 */
class StreamChunker
{
public:
  using ProgressCallback = std::function<void(uint64_t done, uint64_t total)>;

  class Sender
  {
  public:
    Sender(
        deskflow::IStream *stream, std::string data, ClipboardID id, uint32_t sequence, IEventQueue *events,
        void *eventTarget, ProgressCallback progress = {}
    );

    ~Sender();

    //! Queues the first turn. `self` must be the owning shared pointer.
    void start(const std::shared_ptr<Sender> &self);

    //! Writes this turn's chunks plus the end marker once the payload is drained.
    void sendNext(const std::shared_ptr<Sender> &self);

    //! Stops the transfer at the next turn boundary.
    void cancel();

    //! Drops the progress callback so a destroyed owner cannot be called back.
    void detach();

  private:
    void scheduleNext(const std::shared_ptr<Sender> &self);

    deskflow::IStream *m_stream;
    std::string m_data;
    ClipboardID m_id;
    uint32_t m_sequence;
    IEventQueue *m_events;
    void *m_target;
    ProgressCallback m_progress;

    ClipboardChunkPacer m_pacer;
    bool m_cancelled = false;
  };

  static std::shared_ptr<Sender> sendClipboard(
      deskflow::IStream *stream, std::string data, ClipboardID id, uint32_t sequence, IEventQueue *events,
      void *eventTarget, ProgressCallback progress = {}
  );

  //! Handles a `ClipboardSending` event payload (a chunk or a paced turn).
  static void handleEvent(deskflow::IStream *stream, EventData *data);
};
