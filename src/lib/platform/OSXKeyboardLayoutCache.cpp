/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/OSXKeyboardLayoutCache.h"

#include "base/Log.h"
#include "platform/OSXAutoTypes.h"

#include <dispatch/dispatch.h>
#include <pthread.h>

#include <cstring>

namespace {

void inputSourceChanged(CFNotificationCenterRef, void *, CFStringRef, const void *, CFDictionaryRef)
{
  // Distributed notifications may be delivered on any thread, but the refresh
  // itself must run on the main thread.
  dispatch_async(dispatch_get_main_queue(), ^{ OSXKeyboardLayoutCache::instance().refresh(); });
}

CFStringEncoding encodingForSource(TISInputSourceRef source)
{
  if (source == nullptr) {
    return kCFStringEncodingInvalidId;
  }

  const auto languages = (CFArrayRef)TISGetInputSourceProperty(source, kTISPropertyInputSourceLanguages);
  if (languages == nullptr || CFArrayGetCount(languages) == 0) {
    return kCFStringEncodingInvalidId;
  }

  const auto language = (CFStringRef)CFArrayGetValueAtIndex(languages, 0);
  if (language == nullptr) {
    return kCFStringEncodingInvalidId;
  }

  return CFStringConvertIANACharSetNameToEncoding(language);
}

} // namespace

std::string cfStringToStdString(CFStringRef string)
{
  if (string == nullptr) {
    return {};
  }

  const auto length = CFStringGetMaximumSizeForEncoding(CFStringGetLength(string), kCFStringEncodingUTF8);
  std::string result(static_cast<size_t>(length) + 1, '\0');
  if (!CFStringGetCString(string, result.data(), static_cast<CFIndex>(result.size()), kCFStringEncodingUTF8)) {
    return {};
  }

  result.resize(std::strlen(result.c_str()));
  return result;
}

OSXKeyboardLayoutCache::Snapshot::~Snapshot()
{
  if (currentLayoutData != nullptr) {
    CFRelease(currentLayoutData);
  }
  for (auto &group : groups) {
    if (group.layoutData != nullptr) {
      CFRelease(group.layoutData);
    }
    if (group.source != nullptr) {
      CFRelease(group.source);
    }
  }
}

OSXKeyboardLayoutCache &OSXKeyboardLayoutCache::instance()
{
  static OSXKeyboardLayoutCache cache;
  return cache;
}

void OSXKeyboardLayoutCache::runOnMainThread(const std::function<void()> &fn)
{
  if (pthread_main_np()) {
    fn();
    return;
  }
  dispatch_sync(dispatch_get_main_queue(), ^{ fn(); });
}

void OSXKeyboardLayoutCache::start()
{
  {
    std::scoped_lock lock{m_mutex};
    if (m_started) {
      return;
    }
    m_started = true;
  }

  // The observers have to be registered from the main thread so the refresh
  // and the system's own TIS work are serialized with everything else.
  runOnMainThread([this] {
    CFNotificationCenterAddObserver(
        CFNotificationCenterGetDistributedCenter(), this, &inputSourceChanged,
        kTISNotifySelectedKeyboardInputSourceChanged, nullptr, CFNotificationSuspensionBehaviorDeliverImmediately
    );
    refresh();
  });
}

std::shared_ptr<const OSXKeyboardLayoutCache::Snapshot> OSXKeyboardLayoutCache::snapshot()
{
  {
    std::scoped_lock lock{m_mutex};
    if (m_refreshed) {
      return m_snapshot;
    }
  }

  // First call: take one now instead of handing out an empty snapshot.
  runOnMainThread([this] { refresh(); });

  std::scoped_lock lock{m_mutex};
  return m_snapshot;
}

void OSXKeyboardLayoutCache::refresh()
{
  auto snapshot = std::make_shared<Snapshot>();

  CFStringRef keys[] = {kTISPropertyInputSourceCategory};
  CFStringRef values[] = {kTISCategoryKeyboardInputSource};
  AutoCFDictionary dict(
      CFDictionaryCreate(nullptr, (const void **)keys, (const void **)values, 1, nullptr, nullptr), CFRelease
  );
  AutoCFArray sources(TISCreateInputSourceList(dict.get(), false), CFRelease);

  const auto count = sources != nullptr ? CFArrayGetCount(sources.get()) : 0;
  snapshot->groups.reserve(static_cast<size_t>(count));
  for (CFIndex i = 0; i < count; ++i) {
    const auto source = (TISInputSourceRef)CFArrayGetValueAtIndex(sources.get(), i);
    if (source == nullptr) {
      continue;
    }

    Group group;
    group.id = cfStringToStdString((CFStringRef)TISGetInputSourceProperty(source, kTISPropertyInputSourceID));
    const auto languages = (CFArrayRef)TISGetInputSourceProperty(source, kTISPropertyInputSourceLanguages);
    if (languages != nullptr && CFArrayGetCount(languages) > 0) {
      group.language = cfStringToStdString((CFStringRef)CFArrayGetValueAtIndex(languages, 0));
      group.encoding = CFStringConvertIANACharSetNameToEncoding(
          (CFStringRef)CFArrayGetValueAtIndex(languages, 0)
      );
    }
    group.layoutData = (CFDataRef)TISGetInputSourceProperty(source, kTISPropertyUnicodeKeyLayoutData);
    if (group.layoutData != nullptr) {
      CFRetain(group.layoutData);
    }
    group.source = source;
    CFRetain(group.source);
    const auto capable = (CFBooleanRef)TISGetInputSourceProperty(source, kTISPropertyInputSourceIsEnableCapable);
    group.enableCapable = capable != nullptr && CFBooleanGetValue(capable);
    snapshot->groups.push_back(std::move(group));
  }

  const auto current = TISCopyCurrentKeyboardLayoutInputSource();
  if (current != nullptr) {
    snapshot->hasCurrentLayout = true;
    snapshot->currentId =
        cfStringToStdString((CFStringRef)TISGetInputSourceProperty(current, kTISPropertyInputSourceID));
    const auto layoutData = (CFDataRef)TISGetInputSourceProperty(current, kTISPropertyUnicodeKeyLayoutData);
    if (layoutData != nullptr) {
      snapshot->currentLayoutData = (CFDataRef)CFRetain(layoutData);
    }
    snapshot->currentEncoding = encodingForSource(current);
    CFRelease(current);
  } else {
    LOG_DEBUG("no current keyboard layout input source");
  }

  const auto inputSource = TISCopyCurrentKeyboardInputSource();
  if (inputSource != nullptr) {
    const auto languages = (CFArrayRef)TISGetInputSourceProperty(inputSource, kTISPropertyInputSourceLanguages);
    if (languages != nullptr && CFArrayGetCount(languages) > 0) {
      const auto language = (CFStringRef)CFArrayGetValueAtIndex(languages, 0);
      snapshot->currentInputSourceLanguage = cfStringToStdString(language);
      snapshot->currentInputSourceEncoding =
          language != nullptr ? CFStringConvertIANACharSetNameToEncoding(language) : kCFStringEncodingInvalidId;
    }
    CFRelease(inputSource);
  }

  {
    std::scoped_lock lock{m_mutex};
    m_snapshot = std::move(snapshot);
    m_refreshed = true;
  }
}

bool OSXKeyboardLayoutCache::selectGroup(const std::shared_ptr<const Snapshot> &snapshot, size_t index) const
{
  if (snapshot == nullptr || index >= snapshot->groups.size()) {
    LOG_WARN("needed keyboard layout is null");
    return false;
  }

  const auto &group = snapshot->groups[index];
  if (group.source == nullptr || !group.enableCapable) {
    LOG_WARN("needed keyboard layout is disabled for programmatically selection");
    return false;
  }

  if (TISSelectInputSource(group.source) != noErr) {
    LOG_WARN("failed to set needed keyboard layout");
    return false;
  }

  return true;
}
