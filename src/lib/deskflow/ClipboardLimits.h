#pragma once

#include "deskflow/IClipboard.h"

#include <cstddef>

namespace deskflow::ClipboardLimits {

inline constexpr size_t maximumBytes = 16 * 1024 * 1024;
inline constexpr size_t maximumTextBytes = 256 * 1024;

inline size_t forFormat(IClipboard::Format format)
{
  return format == IClipboard::Format::Bitmap ? maximumBytes : maximumTextBytes;
}

inline bool allows(const IClipboard *clipboard)
{
  if (!clipboard || !clipboard->open(0))
    return false;

  size_t totalBytes = 4;
  bool allowed = true;
  for (int formatIndex = 0; formatIndex < static_cast<int>(IClipboard::Format::TotalFormats); ++formatIndex) {
    const auto format = static_cast<IClipboard::Format>(formatIndex);
    if (!clipboard->has(format))
      continue;
    const auto size = clipboard->getSize(format);
    if (size > forFormat(format) || totalBytes > maximumBytes - 8 || size > maximumBytes - totalBytes - 8) {
      allowed = false;
      break;
    }
    totalBytes += size + 8;
  }
  clipboard->close();
  return allowed;
}

} // namespace deskflow::ClipboardLimits
