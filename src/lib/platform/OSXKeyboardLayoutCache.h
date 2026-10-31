/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <Carbon/Carbon.h>

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

//! Snapshot of the active keyboard layout and the installed layouts.
/*!
 * macOS aborts the process when TIS/TSM APIs are called from two threads at
 * once. Qt calls them on the main thread (menu shortcuts, "selected input
 * source changed" notifications), so deskflow must never call them from its
 * event tap or core thread. This cache reads TIS on the main thread only and
 * hands every other thread an immutable, retained snapshot instead.
 */
class OSXKeyboardLayoutCache
{
public:
  struct Group
  {
    //! `kTISPropertyInputSourceID`, also the key used to match the active layout.
    std::string id;
    //! First language code of the input source, e.g. "en".
    std::string language;
    //! Encoding derived from \c language, used to convert single characters.
    CFStringEncoding encoding = kCFStringEncodingInvalidId;
    //! Retained `kTISPropertyUnicodeKeyLayoutData`, null for non-uchr layouts.
    CFDataRef layoutData = nullptr;
    //! Retained input source, only used by \c selectGroup on the main thread.
    TISInputSourceRef source = nullptr;
    //! Whether macOS allows selecting this layout programmatically.
    bool enableCapable = false;
  };

  struct Snapshot
  {
    ~Snapshot();

    std::vector<Group> groups;
    //! True when TIS reported a current layout.
    bool hasCurrentLayout = false;
    //! Retained key layout data of the layout that is active right now.
    CFDataRef currentLayoutData = nullptr;
    //! Id of the active layout, empty when TIS reported none.
    std::string currentId;
    //! Encoding of the active keyboard layout.
    CFStringEncoding currentEncoding = kCFStringEncodingInvalidId;
    //! First language of the active input source, e.g. "en".
    std::string currentInputSourceLanguage;
    //! Encoding of the active input source (may differ for input methods).
    CFStringEncoding currentInputSourceEncoding = kCFStringEncodingInvalidId;
  };

  static OSXKeyboardLayoutCache &instance();

  //! Registers the TIS observers and takes the first snapshot. Any thread.
  void start();

  //! Returns the latest snapshot, taking the first one if necessary.
  std::shared_ptr<const Snapshot> snapshot();

  //! Re-reads every layout. Runs on the main thread.
  void refresh();

  //! Selects a layout by its index in \p snapshot. Runs on the main thread.
  bool selectGroup(const std::shared_ptr<const Snapshot> &snapshot, size_t index) const;

  //! Runs \p fn on the main thread, directly when already there.
  static void runOnMainThread(const std::function<void()> &fn);

private:
  OSXKeyboardLayoutCache() = default;

  mutable std::mutex m_mutex;
  std::shared_ptr<const Snapshot> m_snapshot;
  bool m_started = false;
  bool m_refreshed = false;
};

//! Converts a CoreFoundation string to UTF-8, empty when null.
std::string cfStringToStdString(CFStringRef string);
