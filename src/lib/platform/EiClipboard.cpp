/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Red Hat, Inc.
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/EiClipboard.h"
#include "base/Log.h"
#include <array>

namespace deskflow {

EiClipboard::EiClipboard(ClipboardID id) : m_id(id)
{
  open(0);
  empty();
  close();
}

bool EiClipboard::empty()
{
  std::scoped_lock lock{m_mutex};
  if (!m_open) {
    LOG_WARN("cannot empty clipboard, not open");
    return false;
  }

  // Clear all data
  ++m_revision;
  for (int32_t index = 0; index < static_cast<int>(Format::TotalFormats); ++index) {
    m_data[index] = "";
    m_added[index] = false;
  }

  // Save time
  m_timeOwned = m_time;

  // We're the owner now
  m_owner = true;

  return true;
}

void EiClipboard::add(Format format, const std::string &data)
{
  std::scoped_lock lock{m_mutex};
  if (!m_open) {
    LOG_WARN("cannot add to clipboard, not open");
    return;
  }

  if (!m_owner) {
    LOG_WARN("cannot add to clipboard, no owner");
    return;
  }

  const auto formatID = static_cast<int>(format);
  m_data[formatID] = data;
  m_added[formatID] = true;
}

bool EiClipboard::open(Time time) const
{
  m_mutex.lock();
  ++m_openDepth;
  m_open = true;
  m_time = time;

  return true;
}

void EiClipboard::close() const
{
  m_open = --m_openDepth != 0;
  m_mutex.unlock();
}

EiClipboard::Time EiClipboard::getTime() const
{
  std::scoped_lock lock{m_mutex};
  return m_timeOwned;
}

uint64_t EiClipboard::revision() const
{
  std::scoped_lock lock{m_mutex};
  return m_revision;
}

bool EiClipboard::assign(const IClipboard *source)
{
  std::array<std::string, static_cast<int>(Format::TotalFormats)> incoming;
  std::array<bool, static_cast<int>(Format::TotalFormats)> formats;
  const auto timestamp = source->getTime();
  if (!source->open(timestamp))
    return false;
  for (size_t index = 0; index < incoming.size(); ++index) {
    const auto format = static_cast<Format>(index);
    formats[index] = source->has(format);
    if (formats[index])
      incoming[index] = source->get(format);
  }
  source->close();
  std::scoped_lock lock{m_mutex};
  ++m_revision;
  m_timeOwned = timestamp;
  m_owner = true;
  for (size_t index = 0; index < incoming.size(); ++index) {
    m_added[index] = formats[index];
    m_data[index] = std::move(incoming[index]);
  }
  return true;
}

bool EiClipboard::replaceIfCurrent(uint64_t revision, Format format, const std::string &data)
{
  std::scoped_lock lock{m_mutex};
  if (revision != m_revision)
    return false;
  for (int index = 0; index < static_cast<int>(Format::TotalFormats); ++index) {
    m_added[index] = index == static_cast<int>(format);
    m_data[index] = m_added[index] ? data : std::string();
  }
  return true;
}

bool EiClipboard::has(Format format) const
{
  std::scoped_lock lock{m_mutex};
  if (!m_open) {
    LOG_WARN("cannot check for clipboard format, not open");
    return false;
  }
  return m_added[static_cast<int>(format)];
}

std::string EiClipboard::get(Format format) const
{
  std::scoped_lock lock{m_mutex};
  if (!m_open) {
    LOG_WARN("cannot get clipboard format, not open");
    return "";
  }
  return m_data[static_cast<int>(format)];
}

} // namespace deskflow
