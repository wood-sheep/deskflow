/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "server/ClientProxy1_9.h"

#include "base/Log.h"
#include "deskflow/ProtocolTypes.h"
#include "deskflow/ProtocolUtil.h"

ClientProxy1_9::ClientProxy1_9(
    const std::string &name, deskflow::IStream *adoptedStream, Server *server, IEventQueue *events
)
    : ClientProxy1_8(name, adoptedStream, server, events)
{
}

void ClientProxy1_9::gesture(const GestureEvent &event)
{
  LOG_VERBOSE(
      "send gesture to \"%s\" type=%d phase=%d fingers=%d delta=%d,%d sequence=%u", getName().c_str(),
      static_cast<int>(event.type), static_cast<int>(event.phase), event.fingers, event.deltaX, event.deltaY,
      event.sequence
  );
  ProtocolUtil::writef(
      getStream(), kMsgDGesture, static_cast<int32_t>(event.type), static_cast<int32_t>(event.phase), event.fingers,
      event.deltaX, event.deltaY, event.sequence
  );
}
