# Networking: the MzzPlorkClient <-> MzzPlork Server protocol

This is the protocol between the native client (`runtime/src/{TcpServerConnection,ProtocolClient}.*`)
and the MzzPlork Server (`server/src/protocol.js`, `server/src/tcpServer.js`),
entirely independent of GTA V. It has nothing to do with the bootstrapper's
own update-manifest fetching (see docs/architecture.md's "Two protocols").

## Transport

Plain TCP, newline-delimited JSON (JSON Lines) -- one message object per
line: `{"type": "<message-type>", "payload": {...}}\n`. Chosen for the
same reason as the bootstrapper<->client stdout protocol: simple, human-
debuggable, no schema/codegen tooling needed. See docs/decisions.md.

## Client architecture

Two layers, split deliberately so the sequential parts of the protocol
(the state machine needs to *wait* for a reply before moving on) don't
leak into the transport:

- **`TcpServerConnection`** (`IServerConnection`): raw Winsock socket +
  framing. Runs a background thread that reads the socket and hands
  complete messages to whatever handler is registered. Nothing here knows
  about auth, manifests, or the client state machine.
- **`ProtocolClient`**: implements the actual protocol against the
  *abstract* `IServerConnection` interface only (never touches a socket
  directly) -- works identically against `TcpServerConnection` or a test
  fake. Exposes blocking request/response helpers (`SendHello`,
  `SendAuth`, `SendHeartbeat`, `RequestResourceManifest`) backed by a
  condition variable, plus callback-style delivery for the two message
  types that don't fit request/response: `event` (server-pushed) and
  `kick` (server-initiated disconnect).

## Message types

Client -> Server:

| Type | Payload | When |
|---|---|---|
| `hello` | `{protocolVersion}` | Immediately after connecting |
| `auth` | `{clientId, displayName}` | After a successful `hello_ack` |
| `heartbeat` | `{}` | Every 10s while Ready and connected |
| `resource_manifest_request` | `{}` | After a successful `auth_result` |
| `resources_ready` | `{loadedCount, failedCount}` | After `ResourceRegistry::LoadAll()` finishes (docs/resources.md's "Notify server" step) |
| `player_state` | `{position:{x,y,z}, rotation:{pitch,yaw,roll}, basicState, timestamp}` | Fire-and-forget, sent by `Replication::Tick()` whenever the local player's state changed since the last send, throttled to `replicationIntervalMs` (docs/synchronization.md) |
| `disconnect` | `{reason}` | On shutdown |

Server -> Client:

| Type | Payload | Meaning |
|---|---|---|
| `hello_ack` | `{ok, protocolVersion, reason?}` | `ok:false` on a major-version mismatch; server closes the connection either way after replying |
| `auth_result` | `{ok, playerId, sessionId, displayName, reason?}` | See docs/decisions.md re: this not being a real account system |
| `heartbeat_ack` | `{serverTimeMs}` | |
| `resource_manifest` | `{manifestVersion, httpPort, resources:[{id,path,size,hash,version,dependencies}]}` | `httpPort` tells the client where to download the actual file bytes from; `dependencies` drives load ordering (docs/resources.md) |
| `resources_ready_ack` | `{ok}` | Acknowledges `resources_ready`; the server also broadcasts a `player_ready` event (below) to other authenticated sessions |
| `event` | `{eventType, data}` | Routed to `EventSystem::Publish()` client-side. `eventType` values the server currently emits: `player_ready` (`{playerId, displayName}`, when another client's resources become ready), `player_joined` (`{playerId, displayName}`, broadcast when a session authenticates, and backfilled to a newcomer for every already-authenticated session), `player_left` (`{playerId}`, broadcast on both graceful and abrupt disconnect -- see docs/synchronization.md), and `player_state` (`{playerId, position, rotation, basicState, timestamp}`, relayed from another session's `player_state` message, excluding the sender). |
| `kick` | `{reason}` | Sent for *every* disconnect, including one the client itself requested -- `ProtocolClient` distinguishes "we asked to disconnect" from "the server kicked us unprompted" (see docs/decisions.md) rather than logging both as failures |

## Protocol version compatibility

`isProtocolCompatible()` (`server/src/protocol.js`) compares major
versions only (`"1.x"` accepts any `"1.y"`). A mismatch gets `hello_ack:{ok:false}`
and the server closes the connection.

## Heartbeat and timeouts

The client sends `heartbeat` every 10s once Ready (`Runtime.cpp`'s
heartbeat thread). The server (`tcpServer.js`) tracks `lastHeartbeatAt`
per session and disconnects any session silent for more than 45s
(configurable: `heartbeatIntervalMs`/`heartbeatTimeoutMs` in `server/config.json`).

## Graceful shutdown

`TcpServerConnection::Disconnect()` half-closes the send side
(`shutdown(socket, SD_SEND)`) and waits for its own reader thread to
notice and exit *before* calling `closesocket()` -- closing the socket
handle while another thread might still be inside `recv()` on it is what
produces a spurious `ECONNRESET` on the peer's side instead of a clean
close. This was a real bug caught during this phase's end-to-end testing,
not a hypothetical; see docs/decisions.md.

## Testing

`server/tests/protocol.test.js` and `server/tests/server.test.js` cover
message framing, the full handshake, incompatible-version rejection,
pre-auth manifest-request rejection, and heartbeat timeout, all against a
real in-process server on ephemeral ports. `runtime/tests/E2ETests.cpp`
proves the real client and real server actually interoperate.
`runtime/tests/SyncE2ETests.cpp` extends that to two real clients,
proving `player_state` actually replicates between them -- see
docs/synchronization.md.

