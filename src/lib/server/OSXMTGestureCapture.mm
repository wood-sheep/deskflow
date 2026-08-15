/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "server/OSXMTGestureCapture.h"

#include "base/Log.h"
#include "common/Settings.h"

#include <CoreFoundation/CoreFoundation.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <mutex>

// ---------------------------------------------------------------------------
// Minimal declarations for the private MultitouchSupport.framework API.
// The framework ships with macOS but has no public headers, so the ABI is
// declared here (same shape used by open-source utilities such as FingerMgmt
// and BetterTouchTool-style integrations).
// ---------------------------------------------------------------------------
typedef struct __MTDevice *MTDeviceRef;

typedef struct
{
  int32_t frame;
  double timestamp;
  int32_t identifier;
  int32_t state; // 1=touching 4=moved 7=lifted
  int32_t unknown1;
  int32_t unknown2;
  float normalizedX;
  float normalizedY;
  float total;
  float pressure;
} MTContact;

typedef void (*MTContactCallbackFunction)(
    int device, MTContact *contacts, int numContacts, double timestamp, void *frame
);

extern "C" {
extern MTDeviceRef MTDeviceCreateDefault(void);
extern void MTRegisterContactFrameCallback(MTDeviceRef, MTContactCallbackFunction);
extern void MTDeviceStart(MTDeviceRef, int);
extern void MTDeviceStop(MTDeviceRef, int);
extern void MTDeviceRelease(MTDeviceRef);
}

namespace
{
//! Normalized-distance threshold for one full swipe gesture.
constexpr double kSwipeThreshold = 0.10;
//! Minimum number of valid contacts before tracking a swipe.
constexpr int kMinFingers = 3;
//! Cool-down between emitted gestures (seconds).
constexpr double kCoolDown = 0.25;
//! Throttle between sustained horizontal Update events (seconds). This is the
//! rate at which the Alt+Tab switcher advances while the fingers keep sliding.
constexpr double kUpdateInterval = 0.18;
//! Minimum centroid travel required before another Update is emitted.
constexpr double kUpdateThreshold = 0.03;

bool isValidContact(const MTContact &c)
{
  // MTContact layout is not documented; garbage contacts leak through with
  // out-of-range coordinates. Only trust contacts inside the normalized plane.
  return c.normalizedX > 0.0f && c.normalizedX < 1.0f && c.normalizedY > 0.0f && c.normalizedY < 1.0f;
}

double nowSeconds()
{
  return CFAbsoluteTimeGetCurrent();
}

//! C callback trampoline. The MultitouchSupport framework passes an internal
//! frame pointer (not application data) as the last argument, so the capture
//! instance is reached via a process-wide singleton instead.
OSXMTGestureCapture *g_mtGestureCaptureInstance = nullptr;

void contactCallback(int, MTContact *contacts, int numContacts, double, void *)
{
  if (g_mtGestureCaptureInstance != nullptr) {
    g_mtGestureCaptureInstance->onContacts(contacts, numContacts);
  }
}
} // namespace

OSXMTGestureCapture::OSXMTGestureCapture(GestureHandler handler) : m_handler(std::move(handler))
{
}

OSXMTGestureCapture::~OSXMTGestureCapture()
{
  stop();
}

bool OSXMTGestureCapture::start()
{
  if (m_device != nullptr) {
    return true;
  }

  MTDeviceRef device = MTDeviceCreateDefault();
  if (device == nullptr) {
    LOG_WARN("mt gesture: no trackpad device available");
    return false;
  }

  // Callbacks arrive on a private framework thread; all shared state is
  // guarded by m_mutex. The frame pointer is owned by the framework, so the
  // instance is reached through the process-wide pointer instead.
  g_mtGestureCaptureInstance = this;
  MTRegisterContactFrameCallback(device, contactCallback);

  m_device = device;
  MTDeviceStart(device, 0);
  LOG_INFO("mt gesture: trackpad capture started (backend: MultitouchSupport)");
  return true;
}

void OSXMTGestureCapture::stop()
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_device != nullptr) {
    MTDeviceStop(static_cast<MTDeviceRef>(m_device), 0);
    MTDeviceRelease(static_cast<MTDeviceRef>(m_device));
    m_device = nullptr;
  }
  g_mtGestureCaptureInstance = nullptr;
  m_tracking = false;
}

void OSXMTGestureCapture::onContacts(void *contactsPtr, int numContacts)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_device == nullptr) {
    return; // stopped
  }

  // The framework reports the real finger count in `numContacts`, but with a
  // declared 48-byte MTContact the per-contact payload is misaligned (real
  // stride is smaller), so only a subset of slots carries usable coordinates.
  // Use the raw count as the finger indicator and the valid slots for the
  // movement centroid.
  auto *contacts = static_cast<MTContact *>(contactsPtr);
  int valid = 0;
  double cx = 0.0, cy = 0.0;
  for (int i = 0; i < numContacts; ++i) {
    const MTContact &c = contacts[i];
    if (isValidContact(c)) {
      cx += c.normalizedX;
      cy += c.normalizedY;
      ++valid;
    }
  }
  const bool threeFingers = numContacts >= kMinFingers;
  const double now = nowSeconds();

  const auto emit = [this](GestureType type, GesturePhase phase, int fingers, int16_t dx, int16_t dy) {
    const GestureEvent event{type, phase, static_cast<uint8_t>(fingers), dx, dy, ++m_sequence};
    LOGC(
        true, (CLOG_INFO "mt gesture: emit type=%d phase=%d delta=%d,%d sequence=%u", static_cast<int>(type),
              static_cast<int>(phase), event.deltaX, event.deltaY, event.sequence)
    );
    if (m_handler) {
      m_handler(event);
    }
  };

  if (threeFingers && valid >= 1) {
    cx /= valid;
    cy /= valid;

    if (!m_tracking) {
      // Start of a potential swipe.
      m_tracking = true;
      m_deltaX = 0.0;
      m_deltaY = 0.0;
      LOGC(
          Settings::value(Settings::Log::GestureDiagnostics).toBool(),
          (CLOG_INFO "mt gesture: tracking start raw=%d valid=%d centroid=%.3f,%.3f", numContacts, valid, cx, cy)
      );
    } else {
      // Accumulate centroid movement while fingers stay down.
      m_deltaX += cx - m_lastX;
      m_deltaY += cy - m_lastY;
    }
    m_lastX = cx;
    m_lastY = cy;

    if (m_tracking && now - m_lastEmitted > kCoolDown) {
      const double dx = m_deltaX;
      const double dy = m_deltaY;
      if (std::abs(dx) >= kSwipeThreshold || std::abs(dy) >= kSwipeThreshold) {
        if (std::abs(dx) >= std::abs(dy)) {
          // Horizontal swipe: sustained app-switcher gesture. First travel
          // opens the switcher (Begin), continued travel advances it
          // (Update), fingers lifting confirms (End, sent below).
          const auto type = dx < 0 ? GestureType::SwipeLeft : GestureType::SwipeRight;
          if (!m_swipeActive) {
            m_swipeActive = true;
            m_swipeType = type;
            m_lastUpdate = now;
            m_deltaX = 0.0;
            m_deltaY = 0.0;
            emit(type, GesturePhase::Begin, numContacts, static_cast<int16_t>(std::clamp(dx, -32768.0, 32767.0)),
                 static_cast<int16_t>(std::clamp(dy, -32768.0, 32767.0)));
          } else if (type == m_swipeType && now - m_lastUpdate >= kUpdateInterval &&
                     std::abs(dx) >= kUpdateThreshold) {
            // Same direction, throttled: advance the switcher one more step.
            m_lastUpdate = now;
            m_deltaX = 0.0;
            m_deltaY = 0.0;
            emit(type, GesturePhase::Update, numContacts, static_cast<int16_t>(std::clamp(dx, -32768.0, 32767.0)),
                 static_cast<int16_t>(std::clamp(dy, -32768.0, 32767.0)));
          }
          // Direction changed while active: ignore the travel; the switcher
          // stays open and awaits same-direction movement.
        } else {
          // Vertical swipe: one-shot action (task view / desktop).
          // Trackpad normalized Y grows toward the user; verified on device
          // that a swipe up (toward the display) increases centroid Y.
          const auto type = dy > 0 ? GestureType::SwipeUp : GestureType::SwipeDown;
          m_deltaX = 0.0;
          m_deltaY = 0.0;
          m_lastEmitted = now;
          emit(type, GesturePhase::End, numContacts, static_cast<int16_t>(std::clamp(dx, -32768.0, 32767.0)),
               static_cast<int16_t>(std::clamp(dy, -32768.0, 32767.0)));
        }
      }
    }
  } else {
    // Fingers lifted or fewer than three: close any open switcher, then reset.
    if (m_swipeActive) {
      m_swipeActive = false;
      emit(m_swipeType, GesturePhase::End, kMinFingers, 0, 0);
    }
    m_tracking = false;
    m_deltaX = 0.0;
    m_deltaY = 0.0;
  }
}
