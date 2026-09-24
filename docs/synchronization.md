# Multiplayer synchronization

This is real network replication of *abstract* entity state -- position,
rotation, and a small generic "basic state" bag -- between the native
client and the MzzPlork Server, and from there to every other connected
client. It is deliberately **not** bound to GTA V in any way: there is no
game process to read state from or apply state to yet (see
docs/game-integration.md), so nothing here pretends otherwise. What's
replicated today is exactly what a local development tool
([Local development test environment](#local-development-test-environment)
below) can drive by hand.

## Scope of this phase

Built now, for one entity type (the player):

1. Local player identity
2. Remote player connection (`player_joined`)
3. Remote player position
4. Rotation
5. Basic state (a generic `json::Object` bag -- health, flags, whatever a
   future consumer needs, not yet given specific meaning)
6. Disconnect handling (`player_left`), including abrupt disconnects
7. Entity ownership (`Entity::IsLocal()`)
8. Entity creation/destruction (`EntityManager`)
9. Server events and client events (`EventSystem`, already general-purpose)
10. Synchronization frequency (`replicationIntervalMs`, throttled per-entity)
11. Interpolation (`InterpolatedEntity`)

Explicitly deferred, per the phased plan this was built against: Peds,
Vehicles, Objects, vehicle occupants, weapons, animations, and
prediction. `EntityManager`/`Entity` are already generic enough (keyed by
id, typed by `EntityType`) that adding those entity types later reuses
this machinery rather than needing a parallel system -- see
[What's still out of scope](#whats-still-out-of-scope).

## Module breakdown

Per the explicit requirement not to implement this as one module, each
concern is its own class:

| Module | File | Responsibility |
|---|---|---|
| `Entity` | `Entity.h/.cpp` | Base state every synchronized thing has: id, `EntityType`, `IsLocal()`, position, rotation, basic state. Thread-safe (see below). |
| `Player` | `Player.h/.cpp` | `Entity` + a display name. The only concrete entity type so far. |
| `EntityManager` | `EntityManager.h/.cpp` | Owns every entity, keyed by id. Create/destroy/lookup, plus creation/destruction callbacks for observability. Thread-safe. |
| `NetworkState` | `NetworkState.h/.cpp` | The wire-format snapshot (position, rotation, basic state, timestamp) -- distinct from `Entity` so serialization doesn't leak into the entity model, and so `Interpolation` can work with plain snapshots. |
| `Interpolation` (`InterpolatedEntity`) | `Interpolation.h/.cpp` | Smooths a remote entity's movement between received snapshots -- pure math, no network or game dependency, directly unit-testable. |
| `Replication` | `Replication.h/.cpp` | Ties the above together with `EntityManager`, `ProtocolClient`, and `EventSystem`: subscribes to server events, applies them, periodically sends the local player's state. |
| `EventSystem` | (pre-existing) | Already general-purpose typed pub/sub; `player_joined`/`player_left`/`player_state` needed zero new plumbing beyond `events_.Subscribe(...)`. |

## Client flow

```
Authenticate (playerId assigned)
  -> Replication constructed, subscribes to player_joined/player_left/player_state
  -> Replication::Start() creates the local Player entity
  -> Runtime.cpp starts a replication tick thread (Tick() every 20ms)
  -> SetLocalPosition()/SetLocalRotation() (driven by local dev tool today,
     GTA V integration once that exists) mark local state dirty
  -> Tick() sends player_state when dirty and replicationIntervalMs has elapsed
  -> Incoming player_joined/player_left/player_state (network thread) ->
     EntityManager create/destroy, InterpolatedEntity::SetTarget()
```

## Server side

`server/src/tcpServer.js` / `eventBus.js` / `session.js`:

- On successful auth, the newcomer is told about every already-
  authenticated session (`player_joined` + that session's last known
  `player_state`, if any) via `eventBus`, and every existing session is
  told about the newcomer (`broadcastExcept`).
- A `player_state` message is relayed to every *other* authenticated
  session (never echoed back to the sender) and remembered as
  `session.lastPlayerState`, so a later-joining client can be caught up
  immediately rather than waiting for the sender's next tick.
- `disconnectSession()` broadcasts `player_left` before removing the
  session, and the raw socket `"close"` handler routes through
  `disconnectSession()` too -- so an abrupt disconnect (crash, force-kill,
  network drop, no `"disconnect"` message ever sent) still notifies every
  other client, not just a graceful one. This was a real gap found and
  fixed during this phase, not a hypothetical -- see docs/decisions.md.

See docs/networking.md for the exact message shapes.

## Clock domain

`Replication::OnPlayerState()` anchors interpolation to its own local
`NowMs()` (`steady_clock`-based), never to the remote player_state's
embedded `timestamp`. Using the sender's timestamp as a local
interpolation anchor would silently assume synchronized clocks, which
nothing in this system establishes. Caught during design review before
any code was written against the wrong assumption -- see docs/decisions.md.

## Thread safety

The wired runtime has (at least) two threads touching this state
concurrently: the network reader thread (applies incoming
`player_joined`/`player_left`/`player_state`) and the replication tick
thread (reads/sends the local player's state, throttled by
`replicationIntervalMs`). A third -- the control-command thread reading
stdin -- writes the local player's position/rotation via
`SetLocalPosition()`/`SetLocalRotation()`. `EntityManager` and `Entity`
are internally locked for exactly this reason; `Replication`'s own
dirty-flag/last-sent-timestamp are `std::atomic`. This was not correct on
the first pass -- see the "Bugs found" entries in docs/decisions.md for
how it was actually found (a real crash in `SyncE2ETests.cpp`, not a code
review guess).

## Local development test environment

There is no real GTA V binding to source a local player's position from
yet (see docs/game-integration.md), so `Runtime.cpp`'s control-input loop
(the same stdin channel the bootstrapper uses to signal shutdown) accepts
two commands for manual/interactive testing:

```
setpos <x> <y> <z>
setrot <pitch> <yaw> <roll>
```

Each parses into a `Vector3`/`Rotation3` and calls
`Replication::SetLocalPosition()`/`SetLocalRotation()`, exactly the same
entry point a real game-integration consumer would eventually call.
Malformed or unrecognized lines are ignored (`HandleControlCommand()`),
consistent with the control channel's existing "reserved for future
commands" tolerance.

To drive this by hand: run the MzzPlork Server, run two
`MzzPlorkClient.exe --appdata <dir>` instances pointed at it (distinct
`--appdata` directories so each gets its own generated `clientId`/
`playerId`), and type `setpos ...`/`setrot ...` into one client's stdin.
The other client's log (`<appdata>\logs\client.log`) will show
`player_state received for <id> pos=(...) rot=(...)` -- `Runtime.cpp`
logs both entity lifecycle (`entity created`/`entity destroyed`) and
every received `player_state` with its actual values specifically so
this is observable without a debugger. `runtime/tests/SyncE2ETests.cpp`
automates exactly this scenario.

## What's still out of scope

Peds, Vehicles, Objects, vehicle occupants, weapons, animations,
prediction -- explicitly deferred per the phased plan this was built
against. `EntityType`, `EntityManager`, and `NetworkState` are already
generic enough that adding a new entity type means a new `Entity`
subclass and a new `EntityType` enumerator, not a parallel system.
Binding any of this to actual GTA V state remains blocked on
docs/game-integration.md's legal/technical constraint, independent of how
much replication machinery exists on the abstract side.

## Testing

- `EntityTests.cpp`, `EntityManagerTests.cpp`: entity/manager behavior in
  isolation (construction, position/rotation/basic-state accessors,
  create/destroy, lookup, lifecycle callbacks, local-vs-remote).
- `NetworkStateTests.cpp`: `ToJson`/`FromJson` round-tripping, including
  through an actual `Stringify`/`Parse` cycle (the real wire path), and
  the `timestamp` wire field name.
- `InterpolationTests.cpp`: snap-on-first-target, halfway/at-duration/
  past-duration blending, shortest-path angle wraparound (350 -> 10
  degrees goes through 0, not through 180), retargeting mid-transition
  blends from the current visual position rather than snapping back.
- `ReplicationTests.cpp`: local entity creation on `Start()`, the dirty/
  throttle send logic (including the initial-state-owed-on-construction
  case), `player_joined`/`player_left`/`player_state` event handling
  (including the local-player-echo-is-ignored and
  defensive-entity-creation-for-an-unseen-player cases), and
  `GetInterpolatedState()`.
- `SyncE2ETests.cpp`: the real end-to-end proof -- a real MzzPlork Server
  and **two** real `MzzPlorkClient.exe` processes, driving one client's
  local player through the control channel above and confirming the
  *other* client's `Replication` actually received and applied the exact
  position/rotation values, via its log file.
- Server side: `server/tests/server.test.js` covers the join-notification
  backfill, `player_state` relay (and non-echo-to-sender), catching up a
  later-joining client on already-moving players, and both graceful and
  abrupt disconnect producing `player_left`.
