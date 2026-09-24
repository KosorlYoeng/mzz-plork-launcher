#pragma once
#include <memory>

#include "ServerConnection.h"

namespace mzzplork {

// A concrete IServerConnection that honestly does nothing: Connect()
// always returns false, IsConnected() always false, and the message
// handler is stored but never invoked, since no connection ever forms.
// Used when no real server is configured (e.g. no serverHost in config).
std::unique_ptr<IServerConnection> MakeNotImplementedServerConnection();

}
