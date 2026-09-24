# GTA V Integration

**Status: interface only. No GTA V process manipulation, memory hooking, or
reverse-engineered native-function access is implemented, per explicit
instruction. This is a deliberate architectural boundary, not a gap to be
filled casually -- the legitimate integration path is a separate decision
(see "The blocker" below), not something this document or this phase
resolves.**

## The interface

```cpp
class IGameIntegration {
public:
    virtual ServiceResult Initialize() = 0;
    virtual ServiceResult DetectGame() = 0;
    virtual ServiceResult LaunchGame() = 0;
    virtual ServiceResult Connect() = 0;
    virtual void Disconnect() = 0;
    virtual GameState GetGameState() const = 0;
    virtual void Shutdown() = 0;
};
```

(`runtime/src/GameIntegration.h`.) The rest of the runtime depends only on
this interface -- dependency inversion, per the architectural requirement:

```
MzzPlork Runtime
      |
      v
IGameIntegration
      |
      v
Implementation selected later
```

`Runtime.h`'s `RuntimeDependencies::game` is a `unique_ptr<IGameIntegration>`,
injected at construction (`MakeDefaultDependencies()` for production,
fakes for tests -- see `GameIntegrationTests.cpp`). Nothing in `Runtime.cpp`
knows or cares which concrete implementation it holds.

## What's real vs. what isn't, and why the split is exactly here

| Method | Real? | Why |
|---|---|---|
| `Initialize()` | Real | Trivial setup |
| `DetectGame()` | Real | Locating an installed game is a standard, legitimate OS operation -- filesystem/registry reads. No game process is touched. |
| `LaunchGame()` | Real | Starting a process via `CreateProcess` is a standard, legitimate OS operation. The game's own normal launch/DRM/auth flow is unaffected -- this doesn't run before or bypass any of it, it just starts the same exe a user double-clicking it would. |
| `Connect()` | **Honestly `NotImplemented`** | Establishing an in-process integration with a *running* game has no legitimate mechanism identified -- see "The blocker." This is the one method a fake/mock implementation must never silently succeed at. |
| `Disconnect()` | Real (no-op) | Nothing to tear down, since `Connect()` has never succeeded, by construction |
| `GetGameState()` | Real for what's observable; `integrated` is hardcoded `false` | Process presence (`GTA5.exe` running, via `CreateToolhelp32Snapshot`) is a real, independently-observable fact. Whether MzzPlork has actually integrated with it is not -- and is always reported as `false`, since nothing has ever made that true. |
| `Shutdown()` | Real | Closes the handle from `LaunchGame()`, if any |

This mirrors the same reasoning applied throughout `runtime/`: Cache,
Network, and now `DetectGame()`/`LaunchGame()` are real because they don't
depend on anything unbuilt; `Connect()` is honestly unimplemented because
the thing it would depend on doesn't exist.

### `DetectGame()`'s two mechanisms -- one verified, one not

```cpp
std::unique_ptr<IGameIntegration> MakeGameIntegration(std::string pathOverride);
```

- **`pathOverride` non-empty** (`config.json`'s `gtaInstallPathOverride`):
  checks exactly that path. This is the only mechanism actually verified
  in this environment -- tested in `GameIntegrationTests.cpp` against a
  real (fixture) file on disk.
- **`pathOverride` empty**: falls back to a Rockstar Games Launcher
  registry lookup (`HKLM\SOFTWARE\WOW6432Node\Rockstar Games\Grand Theft
  Auto V\InstallFolder`) based on community-documented conventions.
  **This has never been run against a real installation** -- there is no
  real GTA V or Rockstar Games Launcher install available in this
  development environment to test against. Per this phase's explicit
  instruction not to pretend something works before it's verified: treat
  this path as unverified until someone runs it against a real install
  and confirms or corrects the registry key.

## The blocker

As of September 2026, there is no legitimate path for an independent,
non-FiveM multiplayer client/server platform for GTA V. This is the
central, current, actively-enforced finding from this project's research
phase, unchanged since then:

- Rockstar's September 2026 Mod Guidelines restrict multiplayer modding to
  their own licensed Creator Platform (FiveM) or direct licensing.
- Take-Two acquired Cfx.re (FiveM's maker) in 2023 and has since forced
  the shutdown of every independent alternative: alt:V (Feb 2026) and
  RAGE:MP (Aug 2026), the latter's own statement naming FiveM "the only
  authorized platform for GTAV multiplayer modding" per their Platform
  License Agreement.
- The only technical mechanism that has ever existed for third-party code
  to run inside/alongside GTA V -- DLL injection + reverse-engineered
  native-function hooking -- is exactly what RAGE:MP and alt:V used, and
  is exactly what was just shut down.

Full research, sources, and the layered technical design (what
start/coordinate, identify-process, and IPC-with-a-future-in-game-module
would look like *if* this were resolved) live in the research writeup
produced for this project; the finding hasn't changed and isn't repeated
in full here. The short version: this is a legal/business decision, not
an engineering one, and it is not resolved by writing more code.

## What this means going forward

- Do not implement `Connect()`. Do not give it a body that pretends to
  attach to the game, even partially, even behind a feature flag.
- Do not remove or bypass this interface -- `IGameIntegration` is the
  permanent seam where a legitimate implementation (if one is decided on)
  plugs in later, without the rest of the runtime changing.
- `PLAYING` (in `ClientState.h`) exists in the enum for the same reason:
  it's the documented future state, reachable only once `Connect()` is
  real. Nothing transitions to it today.
- If a licensing path, a different integration mechanism, or a scope
  change (e.g. building as Creator Platform content instead of an
  independent server) gets decided on separately, it plugs in as a new
  `IGameIntegration` implementation -- everything else in `runtime/`
  (state machine, networking, resources, auth) was built without
  depending on how this resolves.
