# MzzPlork Server

`server/` is a standalone Node.js program: client connections, auth/session,
resource manifest and delivery, events, admin, graceful shutdown. It has no
dependency on GTA V, the bootstrapper, or the native client -- anything
speaking docs/networking.md's protocol over TCP can talk to it, which is
exactly what `runtime/tests/E2ETests.cpp` exercises.

## Running it

```
cd server
npm start          # uses server/config.json: tcp=7777, http=7778
```

`server/config.json`:

```json
{
  "tcpPort": 7777,
  "httpPort": 7778,
  "protocolVersion": "1.0",
  "resourcesDir": "resources",
  "heartbeatIntervalMs": 15000,
  "heartbeatTimeoutMs": 45000
}
```

Two listeners: the TCP control channel (protocol messages) and a plain
HTTP server (actual resource file bytes) -- see docs/networking.md and
docs/resources.md for why they're separate.

## Local test environment (client <-> server, no GTA V)

```
MzzPlorkClient
  <-- TCP + HTTP -->
MzzPlork Server
```

To run this manually: start the server (`npm start` in `server/`), then
run `MzzPlorkClient.exe --appdata <dir>` where `<dir>\config\config.json`
sets `serverHost`/`serverPort` to match. `runtime/tests/E2ETests.cpp`
automates exactly this (via `server/test-support/launch-ephemeral.js`,
which starts the server on ephemeral ports for the test to discover).

## Modules

| Module | File | Responsibility |
|---|---|---|
| Protocol framing | `src/protocol.js` | Message envelope, `MessageFramer` (newline-buffered JSON parsing over a socket), protocol version compatibility |
| Session | `src/session.js` | Per-connection state: id, auth status, player id, heartbeat timestamp, formal `state` (see "Connection lifecycle" below) |
| Connection lifecycle | `src/connectionState.js` | Named states (`connecting`/`authenticating`/`authenticated`/`disconnecting`/`disconnected`) + a validated transition table, same `enum` + guarded-transition convention as `runtime/src/{RuntimeState,ClientState,ResourceState,DownloadState}.h` on the client side |
| Auth | `src/auth.js` | Issues a session/player id -- **not a real account system**, see docs/decisions.md |
| TCP server | `src/tcpServer.js` | Connections, hello/auth/heartbeat/manifest-request handling, heartbeat timeout watchdog, graceful per-session and whole-server shutdown |
| Resource manifest | `src/resourceManifest.js` | Computes the manifest from real files on disk; rejects unsafe resource ids |
| HTTP resource server | `src/httpResourceServer.js` | Serves the actual file bytes |
| Event bus | `src/eventBus.js` | Internal pub/sub + `broadcast()` to push an `event` message to every authenticated session |
| Admin console | `src/adminConsole.js` | stdin commands: `list`, `kick <id>`, `broadcast <message>`, `shutdown` |
| Logging | `src/log.js` | Leveled logger, console + `logs/server.log` |
| Entry point | `src/main.js` | Wires everything together; exported as `startServer(configOverrides)` so tests can start/stop real instances in-process on ephemeral ports |

## Connection lifecycle

Each `Session` tracks a formal `state` (`connectionState.js`), validated
against a transition table rather than left to whatever a handler happens
to set:

```
connecting -> authenticating -> authenticated -> disconnecting -> disconnected
     |               |                |
     v               v                v
              disconnecting (from any non-terminal state)
```

`Session.transitionTo(to)` applies the change and returns `true` if `to`
is reachable from the current state, or returns `false` (state
unchanged) otherwise -- `tcpServer.js` logs a warning on a rejected
transition rather than ever throwing mid-handler, since a live
connection should not be torn down by a bookkeeping bug.

This is tracked *alongside*, not instead of, the pre-existing
`helloReceived`/`authenticated` booleans every protocol guard in
`tcpServer.js` already checks: those booleans answer "has this ever
succeeded," which is not always reconstructible from `state` alone once
a session has moved on to `disconnecting`/`disconnected`. `state` adds a
real, named, structurally-guarded lifecycle on top, without changing any
existing guard's behavior -- see docs/decisions.md.

## Administration

Minimal by design -- what the brief actually asked for, not a full admin
panel: `list` (connected sessions), `kick <sessionId|playerId>`,
`broadcast <message>` (pushes an `event` to every authenticated client),
`shutdown` (graceful).

## Graceful shutdown

`shutdown()` (in `main.js`) stops the heartbeat watchdog, sends every
connected session a `kick` and half-closes/closes its socket, closes the
HTTP server, and closes the log stream -- in that order, so nothing is
still trying to write to a closing resource.

## Testing

`server/tests/` (64 tests total):

- `protocol.test.js` -- message framing over a real loopback socket.
- `resourceManifest.test.js` -- manifest computation, path-traversal
  rejection.
- `server.test.js` -- the full handshake against a real in-process
  server: hello -> auth -> heartbeat -> manifest -> HTTP download -> hash
  verification -> disconnect; incompatible-version, pre-auth rejection,
  heartbeat-timeout, player presence/state relay, and both graceful and
  abrupt disconnect (see docs/synchronization.md).
- `connectionState.test.js` -- the transition table in isolation: the
  real lifecycle path, disconnecting reachable from every non-terminal
  state, no backwards transitions, `disconnected` is terminal.
- `session.test.js` -- `Session` in isolation: defaults, `send()`,
  heartbeat expiry, `transitionTo()` (including a rejected illegal jump
  leaving state unchanged).
- `auth.test.js` -- `authenticate()` in isolation: UUID-shaped `clientId`
  reuse vs. fresh generation, `sessionId` uniqueness, `displayName`
  defaulting.
- `eventBus.test.js` -- `broadcast()`/`broadcastExcept()` in isolation:
  authenticated-only delivery, exclusion by id, and the underlying
  `EventEmitter` behavior.
- `log.test.js` -- `Logger` in isolation: directory creation, level
  filtering (including an unrecognized level falling back to `info`),
  append-not-truncate across instances.
- `adminConsole.test.js` -- every command (`list`/`kick`/`broadcast`/
  `shutdown`/unknown/blank) against fake sessions and a real `stream.PassThrough`
  for stdin.

All of the above run against real instances of the thing under test (a
real server, a real `Session`, a real `Logger` writing real files) --
never mocks of the module being tested.
