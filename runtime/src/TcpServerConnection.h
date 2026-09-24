#pragma once
#include <memory>
#include <string>

#include "ServerConnection.h"

namespace mzzplork {

// Real IServerConnection over a TCP socket (Winsock), speaking the
// newline-delimited JSON protocol in server/src/protocol.js. Runs a
// background thread that reads the socket and dispatches complete
// messages to whatever handler SetMessageHandler() registered --
// callers on another thread (see ProtocolClient) are responsible for any
// synchronization they need around that.
std::unique_ptr<IServerConnection> MakeTcpServerConnection();

}
