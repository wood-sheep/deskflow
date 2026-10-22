/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <cstddef>

//! Splits a clipboard payload into chunks and meters them across event loop turns.
/*!
 * Sending a multi-megabyte clipboard in one go monopolises the core event
 * loop, which also forwards mouse and keyboard input. The pacer hands out at
 * most `kMaxChunksPerTurn` chunks per turn so queued input events can be
 * dispatched in between.
 */
class ClipboardChunkPacer
{
public:
  static constexpr size_t kChunkSize = 512 * 1024;
  static constexpr size_t kMaxChunksPerTurn = 4;

  explicit ClipboardChunkPacer(size_t size) : m_size(size)
  {
  }

  //! Hands out the next slice, or returns false once the payload is exhausted.
  bool next(size_t &offset, size_t &length);

  //! True when this turn's chunk budget is spent and more data remains.
  bool shouldYield() const
  {
    return m_offset < m_size && m_chunksThisTurn >= kMaxChunksPerTurn;
  }

  size_t sent() const
  {
    return m_offset;
  }

  size_t total() const
  {
    return m_size;
  }

private:
  size_t m_size = 0;
  size_t m_offset = 0;
  size_t m_chunksThisTurn = 0;
};

inline bool ClipboardChunkPacer::next(size_t &offset, size_t &length)
{
  if (m_offset >= m_size) {
    return false;
  }

  offset = m_offset;
  length = m_size - m_offset;
  if (length > kChunkSize) {
    length = kChunkSize;
  }
  m_offset += length;
  ++m_chunksThisTurn;
  return true;
}
