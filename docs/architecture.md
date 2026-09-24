# MzzPlork Architecture

## Status

This document describes the architecture after the Phase 6/7 client-server
foundation: a real client<->server protocol, a real MzzPlork Server, and
GTA V integration deliberately isolated behind an interface with no
in-process integration implemented. See docs/game-integration.md for why,
and docs/decisions.md for the reasoning behind the major choices below.

## Repository layout

```
E:\mzz-plork-launcher\
+-- bootstrapper/   Electron + Vue UI -- becomes MzzPlork.exe
+-- runtime/        Native C++ -- becomes MzzPlorkClient.exe
+-- server/         Node.js -- the MzzPlork Server (independent of GTA V)
+-- schemas/         JSON Schemas for the bootstrapper's own update manifest
+-- docs/
```

Installed on a player's machine (produced by packaging, a later phase):

```
MzzPlork.exe                            (built from bootstrapper/)
%LOCALAPPDATA%\MzzPlork\                ("MzzPlork Application Data")
+-- runtime\MzzPlorkClient.exe          (built from runtime/)
+-- resources\                          (downloaded from the MzzPlork Server -- see docs/resources.md)
+-- cache\
+-- config\                             (config.json, a persistent client-id.txt)
+-- logs\
+-- downloads\                          (bootstrapper's own staging area -- see docs/resources.md)
+-- version.json
```

`%LOCALAPPDATA%` instead of next to `MzzPlork.exe`: see docs/decisions.md.

The MzzPlork Server (`server/`) is not installed alongside the client --
it's a separate program, run wherever you choose to host it. A single
`server/` checkout with `npm start` is enough for local development and
testing (see docs/server.md).

## Three programs, two protocols

There are three independently-built, independently-running programs, and
two different protocols connecting them:

```
MzzPlork.exe (Electron)
    | spawns, reads stdout JSON Lines (docs/runtime.md)
    v
MzzPlorkClient.exe (native C++)
    | TCP, JSON Lines protocol (docs/networking.md)
    v
MzzPlork Server (Node.js, server/)
```

Do not confuse the two "manifest" concepts that exist at different layers:
- The **bootstrapper's update manifest** (`schemas/version-manifest.schema.json`,
  fetched by `bootstrapper/electron/manifest.js`) governs updating the
  installed `MzzPlorkClient.exe` binary and any base resources, before the
  client even starts. See docs/resources.md's "Two resource systems" section.
- The **server's resource manifest** (`server/src/protocol.js`'s
  `resource_manifest` message) governs resources the *running client*
  downloads from whichever *server* it connects to, after authenticating.
  Also covered in docs/resources.md.

## Modules and responsibilities

| Area | Location | Docs |
|---|---|---|
| Bootstrapper UI + update system | `bootstrapper/` | (see module table below) |
| Native client runtime | `runtime/` | docs/runtime.md |
| Client<->server protocol | `runtime/src/{TcpServerConnection,ProtocolClient}.*`, `server/src/protocol.js` | docs/networking.md |
| Resource manifest/download/verify/cache (client-side) | `runtime/src/{ResourceManager,HttpDownloader,CacheService}.*` | docs/resources.md |
| MzzPlork Server | `server/` | docs/server.md |
| GTA V integration boundary | `runtime/src/GameIntegration.h/.cpp` | docs/game-integration.md |
| Multiplayer synchronization (entities, replication, interpolation) | `runtime/src/{Entity,Player,EntityManager,NetworkState,Interpolation,Replication}.*` | docs/synchronization.md |

| Bootstrapper module | Location | Responsibility |
|---|---|---|
| Bootstrapper UI | `bootstrapper/src` (Vue) | Renders install/update/launch progress |
| Bootstrapper orchestrator | `bootstrapper/electron/bootstrap.js` | Implements the install/update/launch flow diagram; unit-tested without Electron |
| App Data manager | `bootstrapper/electron/appData.js` | Resolves `%LOCALAPPDATA%\MzzPlork`, creates the subdirectory layout, reads/writes `version.json` |
| Install state detector | `bootstrapper/electron/installState.js` | First install vs. existing install |
| Config loader | `bootstrapper/electron/config.js` | `config.json` + `MZZPLORK_UPDATE_SERVER_URL`/`MZZPLORK_DOWNLOAD_BASE_URL` env overrides -- no production URL is hardcoded |
| Update manifest client + planner/applier + downloader | `bootstrapper/electron/{manifest,updateManager,downloader}.js` | Fetches, diffs (per file), downloads, verifies, and applies updates to the installed client/resources |
| Client launcher/monitor | `bootstrapper/electron/clientLauncher.js` | Spawns `MzzPlorkClient.exe`, relays its lifecycle, distinguishes a crash from a clean exit |

## Constraints carried forward from the project brief

- No proprietary FiveM/Cfx.re binaries or code anywhere in this repository.
- No authentication, licensing, or anti-cheat mechanism is bypassed.
- No GTA V process manipulation, memory hooking, or reverse-engineered
  native-function access is implemented -- see docs/game-integration.md
  for the full research and the current blocker.
