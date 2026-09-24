# Decisions

A log of the non-obvious engineering choices made across this project,
and why -- so a future reader doesn't have to reverse-engineer the
reasoning from the code, and so "why is it built this way" has one place
to look instead of being scattered across commit history.

## Application data lives in `%LOCALAPPDATA%`, not next to `MzzPlork.exe`

The original target tree put `MzzPlork Application Data` as a sibling
folder of the installed exe. If `MzzPlork.exe` is installed under
`Program Files`, a non-elevated process can't write there. Per-user,
non-roaming, potentially large (resources, cache) data belongs in
`%LOCALAPPDATA%\MzzPlork`.

## Two different protocols, both JSON Lines over a stream

Bootstrapper<->client (stdout) and client<->server (TCP) both use
newline-delimited JSON rather than a binary format or something requiring
codegen (protobuf, etc.). Simple, human-debuggable without extra tooling,
and consistent between the two places it's used.

## A hand-rolled, minimal JSON library instead of a third-party one

`runtime/src/Json.h/.cpp`. No package manager is available in this build
environment to pull in a library, and a full JSON library is more than
this project needs. It started scoped to flat objects (config files only)
and grew to a real recursive value type (objects, arrays, strings,
numbers, bools, null) once the client<->server protocol's
`resource_manifest` message needed an array of objects -- extending the
existing library rather than writing a second, parallel parser.

## SHA-256 via Windows CNG (BCrypt), not hand-rolled or a pulled-in library

`runtime/src/Sha256.h/.cpp`. A vetted, OS-provided crypto primitive beats
both hand-rolled crypto code (an easy place to get subtly wrong) and
pulling in OpenSSL with no package manager to manage it.

## WinHTTP for the native resource downloader

`runtime/src/HttpDownloader.h/.cpp`. Standard Windows API, no external
dependency. Deliberately does not support resuming an interrupted
download, unlike the bootstrapper's `downloader.js` -- a disclosed scope
reduction (the goal was a correct, verified, end-to-end pipeline first),
not an oversight. Resumable transfer can follow the same design
`downloader.js` already proved.

## `ServiceStatus::NotImplemented` as a third outcome, distinct from `Failed`

`runtime/src/ErrorHandling.h`. The boot sequence needs to tell the
difference between "this doesn't work" (fatal, e.g. a malformed config
file) and "this doesn't exist yet" (not fatal, e.g. GTA V integration).
Collapsing both into a single `Failed` status would force a choice between
crashing on every run (nothing works yet) or silently treating real
failures as fine -- neither is honest.

## `IGameIntegration`: dependency inversion around the one thing legally/technically blocked

See docs/game-integration.md for the full reasoning. The short version:
everything else in the runtime (networking, resources, auth, the state
machine) was built to not depend on how GTA V integration eventually gets
resolved, so that question can be decided separately without triggering a
rewrite of everything else.

## Auth is session identity, not a real account system

`server/src/auth.js`, driven from `ProtocolClient::SendAuth`. There is no
password, no persistent user database, and no verification of who a
client claims to be -- it issues a session/player id so the rest of the
pipeline (session tracking, resource delivery, events) has something real
to plug into. Documented explicitly so it's never mistaken for real
security by a future reader (or a future feature built on top of it
without re-examining this assumption).

## Two separate "resource manifest" systems -- not one shared mechanism

Documented in full in docs/resources.md. Summary: the bootstrapper's
manifest updates the *installed client binary*, before it even starts;
the server's manifest delivers *per-server content* to an already-running,
authenticated client. Different lifecycles, different trust boundaries
(the bootstrapper trusts a configured update server; the running client
trusts whatever server the player chose to connect to) -- conflating them
into one system would have made both weaker.

## Why the MzzPlork Server is Node.js, not C++

Consistency with the bootstrapper's existing stack (already Node/Electron),
fast iteration, and Node's built-in `net`/`http`/`crypto` modules cover
everything the server needs without hand-rolling a second server
implementation in C++ on top of the substantial new C++ work the client
already required this phase.

## Bugs found (and fixed) during end-to-end testing of the client-server phase

Kept here rather than only in commit history, because each reflects a
reusable lesson:

- **`config.tcpPort`/`config.httpPort` not reflecting the real bound
  port.** Requesting an ephemeral port (`0`) and then reading `config.httpPort`
  later (e.g. to embed in the `resource_manifest` response) returned the
  originally-requested `0`, not the OS-assigned port, because nothing
  wrote the real value back into `config` after binding. Fixed in
  `server/src/main.js`. Lesson: a "port" isn't final until the listener
  actually reports it.
- **`ECONNRESET` instead of a graceful close.** `TcpServerConnection::Disconnect()`
  called `closesocket()` immediately, while its own background reader
  thread might still be inside `recv()` on that socket -- closing a
  socket another thread is actively using is what produces a reset
  instead of a clean FIN. Fixed by half-closing (`shutdown(socket, SD_SEND)`),
  joining the reader thread, and only then closing the handle. Lesson:
  "graceful shutdown" is a real, testable claim, not a description --
  this one only surfaced by actually running the full chain and reading
  the *other* side's log.
- **A voluntary disconnect logged as a failure.** The server acknowledges
  every disconnect (including client-initiated ones) with a `kick`
  message; the client's kick handler logged all of them as
  `server-connection: failed`, making a successful shutdown look like an
  error in the logs. Fixed by tracking `disconnectRequested_` and
  distinguishing "the server acknowledged our own request" from "the
  server kicked us unprompted." Lesson: a message name (`kick`) isn't the
  same as its meaning in every context it's sent.




## Replication interpolation is anchored to a local receive clock, never the sender's embedded timestamp

`runtime/src/Replication.cpp`'s `OnPlayerState()` computes its own
`NowMs()` (local `steady_clock`) as the interpolation anchor when a
`player_state` event arrives, and never uses that event's `timestamp`
field for anything except round-tripping it back out on the next send.
Using the sender's embedded timestamp as a local interpolation anchor
would silently assume the sender's and receiver's clocks are
synchronized -- nothing in this system establishes that (no NTP-style
sync, no server-relative clock exchange), so it would be an unfounded
assumption baked into timing-sensitive math. This was caught during
design review, before any test was written against it, specifically
because it is the kind of bug that "looks right" in a single-machine test
(where both clocks are, trivially, the same clock) and only misbehaves
once two genuinely separate machines are involved -- exactly the
scenario a same-process unit test can't catch. See docs/synchronization.md.

## Bugs found (and fixed) while adding HttpDownloader resume support

- **A "completed" transfer wasn't checked against Content-Length before
  being trusted.** `WinHttpQueryDataAvailable`/`WinHttpReadData` can end
  their read loop "normally" -- no explicit error -- even when the
  underlying connection was reset mid-stream; a plain TCP close looks the
  same as a clean end-of-body to that loop. The first version of the
  resume logic treated "the loop ended without erroring" as `Completed`,
  deferring all truncation detection to the final size/hash check. That
  check *did* catch the problem, but classified it as a permanent
  verification failure (deleting the resumable partial file) rather than
  a transient interruption (which should preserve it and allow a retry) --
  the wrong outcome for the wrong reason. Fixed by comparing bytes
  actually received against the response's `Content-Length` header
  immediately after the read loop, before deciding the attempt succeeded.
  Caught by `HttpDownloader_ResumesAfterInterruptionWithinOneCall` and
  `HttpDownloader_ConnectionInterruptionIsNeverReportedAsSuccess`
  failing -- both for the same underlying reason. Lesson: "the read loop
  didn't error" and "we received everything we were promised" are
  different claims; only the second one is safe to treat as success.
- **Exhausting retries deleted an otherwise-resumable partial file.** The
  original loop-exhaustion path unconditionally deleted `<destPath>.partial`
  when `maxAttempts` ran out. But by construction, reaching that path only
  ever happens via repeated *transient* (`Interrupted`) outcomes --
  `HardFailure` and a failed verification both already return (and clean
  up) earlier. Deleting the partial file here contradicted "do not restart
  the entire download unnecessarily": a later call (the next run of the
  process, say) would be forced to start over instead of resuming. Fixed
  by leaving the partial file in place when attempts are exhausted due to
  transient interruptions. Lesson: "clean up when permanently invalid"
  and "clean up whenever we give up" are not the same rule.
- **Assertions inside the test HTTP server's request handler were
  unsafe.** Several `HttpDownloaderTests.cpp` cases initially asserted
  directly inside the handler passed to `MinimalHttpServer` (e.g. "the
  client must have sent a Range header"). That handler runs on the
  server's own accept thread; an assertion failure there throws across a
  thread boundary the test framework doesn't catch, which calls
  `std::terminate()` and takes down the entire test binary -- not just
  that one test, every test after it too. Fixed by having the handler
  record what it observed (into a `std::atomic<bool>`) and asserting on
  that from the main thread after the download call returns. Caught by
  code review before ever running (not by a failure), but worth recording:
  the same mistake would be easy to reintroduce in a future test.


## Bugs found (and fixed) while building the resource lifecycle system

- **Dependency pre-validation failures were not counted in `LoadSummary`.**
  `ResourceRegistry::LoadAll()` runs a pre-pass that fails any resource
  whose dependency is missing from the manifest or already failed, before
  the topological sort even runs -- but that pre-pass transitioned the
  resource to `Failed` without adding it to `summary.failed`/`summary.errors`.
  The resource's own state was correct; the *aggregate count* Runtime.cpp
  logs and reports to the server was wrong. Caught by three different
  tests (`ResourceRegistry_MissingDependencyFailsOnlyTheDependent`,
  `..._ADependencyThatFailedToDownloadCascadesToItsDependents`,
  `..._ResourcesOutsideAFailedSubgraphStillLoad`) all failing on the same
  assertion for the same underlying reason. Lesson: a summary that's
  built in two separate passes over the same data needs both passes to
  actually write to it -- easy to add a new early-exit path and forget it
  contributes to a total computed elsewhere.
- **A test's own `socket.end()` raced the server's shutdown-time write.**
  `resources_ready is acknowledged and broadcast...` called
  `socketA.end()`/`socketB.end()` before its `finally { instance.shutdown() }`
  block ran -- but `shutdown()` disconnects every session by writing a
  `kick` message first, and writing to a socket the *client* side already
  ended raised `ERR_STREAM_WRITE_AFTER_END` asynchronously, after the test
  had already reported success, which Node's test runner correctly
  flagged as an uncaught exception. Every other test in the file leaves
  socket cleanup entirely to `instance.shutdown()`; this one didn't need
  to be different. Fixed by removing the manual `.end()` calls. Lesson:
  when nine other tests in the same file share a cleanup convention and
  a tenth doesn't, that's usually the bug, not a case that needed special
  handling.

## The server's connection lifecycle is a validated state machine, tracked alongside (not instead of) the existing booleans

`server/src/connectionState.js`, wired through `Session.transitionTo()`.
Closes a real inconsistency with the project's own established
convention (`runtime/src/{RuntimeState,ClientState,ResourceState,
DownloadState}.h`: named `enum class` + a table that structurally
validates every transition) -- the server side previously tracked
connection progress purely through ad hoc booleans (`helloReceived`,
`authenticated`) set at each handler, with no formal state or guard
against an invalid jump. Deliberately additive rather than a replacement:
those booleans answer a slightly different question ("has this ever
succeeded") than `state` does once a session reaches `disconnecting`/
`disconnected`, and every existing protocol guard in `tcpServer.js` is
already tested against them -- reusing `state` as their sole source of
truth would have required re-deriving that exact same meaning from a
five-state enum, for no real benefit over just tracking both. See
docs/server.md.

## Bugs found (and fixed) while adding multiplayer synchronization

- **`EntityManager`'s map had no synchronization, despite being written
  from two threads.** The wired runtime has the network reader thread
  create/destroy remote entities (`Replication`'s `player_joined`/
  `player_left` handlers) while the replication tick thread concurrently
  reads the local player -- both against the same `std::map`. Nothing in
  the original unit tests exercised real threads (manual `Tick()` calls,
  single-threaded), so this was invisible until `SyncE2ETests.cpp` -- the
  first test to run two real clients against a real server -- crashed the
  whole test binary with an access violation. Fixed by giving
  `EntityManager` its own mutex around every map access, releasing it
  before invoking the create/destroy callbacks (so a callback that calls
  back into the manager can't deadlock on its own lock). `Entity`'s
  position/rotation/basicState and `Replication`'s dirty-flag/last-sent
  timestamp had the same class of bug for the same reason (local-player
  fields written from the control-command thread, read from the
  replication tick thread) and got the same treatment -- a lock in
  `Entity`, `std::atomic` for the two plain flags in `Replication`.
  Lesson: a data structure that's only ever driven single-threaded in its
  unit tests can still be genuinely unsafe the moment it's wired into a
  runtime with real background threads -- the unit tests proved the logic
  right, not the concurrency.
- **`ReadUntil`'s detached reader thread captured the caller's stack by
  reference.** `runtime/tests/SpawnHelper.h`'s `ReadUntil()` helper
  detaches its blocking-read thread on timeout (Windows anonymous pipes
  have no portable non-blocking read) but the thread's lambda captured
  `predicate`, `accumulated`, `done`, and `mutex` by reference to the
  *caller's* stack frame. Once `ReadUntil()` returns, all of that is
  destroyed; a detached thread that later wakes up with more data and
  calls the (now-dangling) predicate is a genuine use-after-return. This
  was latent in every prior use of `ReadUntil()` (all non-capturing
  lambdas, and every prior call happened to succeed well before its
  timeout in practice) -- `SyncE2ETests.cpp` was the first call likely to
  actually hit a timeout with the thread still parked in a blocking read,
  which is what exposed it as a real, reproducible crash rather than a
  theoretical one. Fixed by moving the shared state to a `std::shared_ptr`
  the detached thread keeps alive, and taking `predicate` by value instead
  of by reference. Lesson: "detach because it might still be blocked"
  requires everything that thread touches to actually outlive the
  detaching function, not just the pipe handle it was explicitly
  documented as depending on.
- **`Logger` opened its file exclusively, silently failing every
  concurrent reader.** `fopen_s(&file_, path, "a")` on Windows defaults to
  no sharing at all, so a *separate process* trying to open the same log
  file for reading while the client is still running fails outright --
  and `std::ifstream` fails that open silently (no exception by default),
  which looked exactly like "the line we're polling for hasn't been
  written yet" rather than "we can't even see the file." Every prior test
  that read a client's log file did so only after the owning `Logger` was
  destroyed (same-process) or the process was already terminated
  (`E2ETests.cpp`) -- `SyncE2ETests.cpp` was the first to poll a *live*
  process's log file from a separate process, which is what surfaced this.
  Fixed by opening with `_fsopen(..., _SH_DENYWR)` instead, which still
  reserves write access to this process but allows concurrent readers --
  a general production improvement (log tailing, monitoring agents), not
  just a test workaround. Lesson: a silent, no-exception failure mode
  (`ifstream` opening a file it can't actually access) can look identical
  to "the data isn't there yet," and timing-based debugging will never
  find it -- only actually reading the file from *outside* the test
  process, with something that reports failures loudly, did.
