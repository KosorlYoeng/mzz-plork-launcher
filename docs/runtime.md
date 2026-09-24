# MzzPlork Client Runtime

`MzzPlorkClient.exe` is a standalone native executable (C++17, `runtime/`),
entirely separate from the Electron bootstrapper UI and from the MzzPlork
Server. This document covers its internal architecture, its lifecycle,
and how `MzzPlork.exe` starts it.

## How MzzPlork.exe starts MzzPlorkClient.exe

1. The Electron main process (`bootstrapper/electron/bootstrap.js`) runs
   the install/update sequence (docs/architecture.md, docs/resources.md),
   ending with a verified `runtime\MzzPlorkClient.exe` under
   `%LOCALAPPDATA%\MzzPlork`.
2. `bootstrapper/electron/clientLauncher.js` calls
   `child_process.spawn(exePath, ["--appdata", appDataRoot], { windowsHide: true })`.
   `--appdata` is the only argument in the public contract -- see
   `runtime/src/ClientRuntimeInterface.h`.
3. The client's `stdin` stays open as an inherited pipe for as long as the
   bootstrapper is alive; it closing is what unblocks the client's idle
   loop and starts its shutdown. There is no other "shut down now" signal
   today.
4. The client writes one JSON object per line to `stdout` (see "Wire
   protocol" below); `clientLauncher.js` parses each line and forwards it
   to `bootstrap.js`, which relays it to the Vue UI over IPC.
5. `clientLauncher.js` also watches the child process object itself
   (`error`/`exit` events) to distinguish a spawn failure or a crash from
   a clean shutdown, independent of whatever the process managed to print.

## The modules

| Module | Files | Real, or interface-only? |
|---|---|---|
| Runtime | `Runtime.h/.cpp` | Real -- the orchestrator, owns every other service and drives the lifecycle |
| Configuration | `Config.h/.cpp`, `Json.h/.cpp` | Real -- loads `config\config.json` (a real, if minimal, JSON library: objects, arrays, strings, numbers, bools, null) |
| Logging | `Log.h/.cpp` | Real -- level-gated file logger |
| Client State | `ClientState.h/.cpp` | Real -- the session state machine (below) |
| Cache | `CacheService.h/.cpp` | Real -- `cache\`, key-sanitized, on disk |
| Network | `NetworkService.h/.cpp` | Real -- a Winsock availability check, distinct from... |
| Server Connection | `ServerConnection.h` (interface), `TcpServerConnection.h/.cpp` (real), `ServerConnectionStub.h/.cpp` (used when no server is configured) | Real -- an actual TCP socket speaking docs/networking.md's protocol |
| Authentication | `ProtocolClient::SendAuth`, driven by `Runtime.cpp`'s `LoadOrCreateClientId()` | Real session issuance against the server -- **not** a real account system; see docs/decisions.md |
| Resource Manager | `ResourceManager.h/.cpp`, `HttpDownloader.h/.cpp` | Real -- see docs/resources.md |
| Event System | `EventSystem.h/.cpp` | Real -- typed pub/sub for server-pushed events |
| Error Handling | `ErrorHandling.h/.cpp` | Real -- `ServiceStatus`/`ServiceResult` vocabulary + `CrashHandler` (writes `logs\crash-<timestamp>.log` before the process dies) |
| GTA V Integration | `GameIntegration.h/.cpp` | **Split**: `detectGame()`/`launchGame()` are real; `connect()` is honestly `NotImplemented`. See docs/game-integration.md. |

`Lifecycle.h/.cpp` (the coarse `Initializing`/`Ready`/`ShuttingDown`/`Stopped`
process state, used on the wire) and `ClientRuntimeInterface.h/.cpp` (the
bootstrapper-facing protocol) are used by Runtime rather than being
separate top-level modules.

## Client state machine

```
STARTING -> INITIALIZING -> CHECKING_UPDATE -> CONNECTING -> AUTHENTICATING
  -> DOWNLOADING_RESOURCES -> LOADING_RESOURCES -> READY -> (idle)
  -> DISCONNECTING -> STOPPED
```

`PLAYING` exists in the enum (`ClientState.h`) but **nothing in this
codebase ever transitions to it**. Reaching `Playing` would require
`GameIntegration::Connect()` to succeed, and it is structurally
`NotImplemented` -- there's no code path that could accidentally claim a
game session exists. `READY` is the last state a run of this client
actually reaches.

What each state does today:

- **STARTING**: initial value, before configuration is even loaded.
- **INITIALIZING**: config, logging, cache, network, and the GTA V
  integration boundary (`Initialize()` + `DetectGame()`, both real) come
  up. A `Failed` cache or config result aborts here with
  `ExitCode::FatalInitError`.
- **CHECKING_UPDATE**: a placeholder checkpoint today. Substantive
  version-compatibility checking happens during the `hello` handshake in
  CONNECTING; updating the *installed* client/resources is the
  bootstrapper's job (docs/resources.md), which already ran before this
  process started. This state exists for observability and as the seam
  for future client-side update logic -- a disclosed scope boundary, not
  an oversight.
- **CONNECTING**: if `serverHost` is configured, connects and runs the
  `hello`/`hello_ack` protocol-version handshake (docs/networking.md). If
  not configured, or if it fails, this is logged and the client proceeds
  -- not fatal.
- **AUTHENTICATING**: sends `auth` with a per-install persistent client ID
  (`config\client-id.txt`, generated once via `BCryptGenRandom`). See
  docs/decisions.md for why this is a session identity, not a real
  account system.
- **DOWNLOADING_RESOURCES**: requests the resource manifest and applies it
  (docs/resources.md). Skipped if not connected/authenticated.
- **LOADING_RESOURCES**: no consumer exists yet -- see docs/game-integration.md.
  Logged and passed through.
- **READY**: reached unconditionally (even with no server, even after an
  auth failure) -- this client is designed to always reach a stable idle
  state rather than crash or hang on a partial failure. A heartbeat
  thread starts here if connected (every 10s, comfortably under the
  server's 45s timeout) and stops on shutdown.
- **DISCONNECTING** / **STOPPED**: reached when the bootstrapper closes
  stdin. Sends `disconnect`, stops the heartbeat thread, closes the
  server connection, calls `GameIntegration::Shutdown()`.

## Wire protocol (bootstrapper <-> client, stdout JSON Lines)

```json
{"event":"state","state":"initializing"}
{"event":"service","service":"<name>","status":"ok"|"not_implemented"|"failed","message":"..."}
{"event":"log","message":"..."}
{"event":"error","message":"..."}
```

`service` events fire once per lifecycle step (`configuration`, `logging`,
`cache`, `network`, `game-integration`, `game-detection`,
`server-connection`, `authentication`, `resources`, `resource-loading`,
`client-state`) reporting real outcomes -- `not_implemented` for Network's
sibling concerns that remain unbuilt is expected, not an error. This is
additive to the Phase 1 contract; `clientLauncher.js` forwards anything it
doesn't specifically recognize.

Exit codes: `0` success, `1` `FatalInitError` (malformed config, cache
directory failure), `2` `InvalidArguments` (missing `--appdata`).

## Testing

`runtime/tests/TestFramework.h` is a small self-contained test framework
(no third-party dependency available in this build environment). Build
and run from `runtime\tests` (paths are relative to that directory):

```
runtime\tests\build.bat
runtime\tests\MzzPlorkClientTests.exe
```

50 tests as of this phase, including:
- Unit tests for every module above (Lifecycle, ClientState, Json, Sha256,
  Config, Cache, EventSystem, GameIntegration, ResourceManager, the
  NotImplemented server-connection stub, the crash handler via a hidden
  `--debug-crash` flag).
- `RuntimeTests.cpp`: `Runtime::Run()` end-to-end against a fake
  `IServerConnection` that simulates real protocol replies, covering the
  full state sequence, auth/connection failure fallback, and the
  voluntary-disconnect-is-not-a-kick fix (docs/decisions.md).
- `E2ETests.cpp`: the actual requested integration test -- spawns the
  **real** `node` MzzPlork Server and the **real** `MzzPlorkClient.exe` as
  actual subprocesses (not mocks) and verifies the full chain reaches
  `state=ready` with `server-connection`, `authentication`, and
  `resources` all reporting `ok`.
