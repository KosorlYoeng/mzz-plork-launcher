# Resources

There are **two separate resource systems** in this project. Conflating
them is an easy mistake, so this document exists to keep them straight.

## 1. The bootstrapper's update manifest (installing/updating the client)

**What it's for:** getting a verified `MzzPlorkClient.exe` (and any base
resources) onto disk *before the client process even starts*.

**Where:** `bootstrapper/electron/{manifest,updateManager,downloader}.js`,
schema at `schemas/version-manifest.schema.json`.

**Summary:** the bootstrapper fetches a manifest from a configured update
server (`MZZPLORK_UPDATE_SERVER_URL`/`MZZPLORK_DOWNLOAD_BASE_URL`, no
production URL hardcoded), diffs it file-by-file against
`%LOCALAPPDATA%\MzzPlork\version.json`, downloads only changed files
(resumable via HTTP `Range`, retried once on a hash mismatch), stages them
in `downloads\`, then moves them into `runtime\` or `resources\` only once
the whole set for a component is verified. Full detail: this file's
history lives in earlier phases' documentation, superseded by this doc and
docs/architecture.md.

## 2. The client's resource manifest (per-server content)

**What it's for:** once `MzzPlorkClient.exe` is running and has
authenticated to a specific MzzPlork Server, downloading whatever content
*that server* wants connected clients to have. Analogous in spirit to how
FiveM servers deliver resources to connecting clients, implemented
independently.

**Where:** client side, `runtime/src/{ResourceManager,HttpDownloader}.*`,
driven from `Runtime.cpp`'s `DOWNLOADING_RESOURCES` state; server side,
`server/src/resourceManifest.js`, `server/src/httpResourceServer.js`.

### How it works

1. `ResourceManager::Initialize(appDataRoot)` creates `resources\` if
   missing.
2. After authentication, the client sends `resource_manifest_request`
   (docs/networking.md) and gets back `{manifestVersion, httpPort,
   resources:[{id, path, size, hash, version}]}`.
3. `ResourceManager::ApplyManifest()` resets the lifecycle registry from
   the manifest (every entry starts `Pending`), then processes each entry
   **independently**:
   - If a local file already exists at `resources\<path>` with matching
     size **and** re-hashed content, it's marked `Cached` directly (the
     `Pending -> Cached` shortcut) -- never redownloaded.
   - Otherwise: `Pending -> Downloading`, `HttpDownloader::DownloadAndVerify()`
     fetches it (with full resume support -- see below) from
     `http://<serverHost>:<httpPort>/resources/<id>` (a *different* port
     than the TCP control channel -- the manifest tells the client which
     one), `-> Verifying -> Cached` on success, or `-> Failed` (with the
     error recorded) on any failure.
4. One resource failing does **not** stop the others, and does not fail
   `ApplyManifest()` overall -- this is a deliberate change from an
   earlier phase, where a single failure aborted the whole manifest. Now
   that failures are tracked per-resource by the lifecycle registry (and
   independently testable -- "missing resource," "changed resource,"
   "corrupted resource," "failed download" are all about *one* resource's
   outcome, not the batch), an all-or-nothing gate would just throw that
   granularity away. `ApplyManifest()`'s own result reports whether
   manifest processing ran at all (e.g. a missing `httpPort` still fails
   outright); per-resource outcomes are read via
   `GetResourceState(id)`/`GetAllResources()`.

### Resume support (`HttpDownloader`)

Now at parity with the bootstrapper's `downloader.js` -- same proven
behavior (resume via HTTP `Range`, safe fallback when a server doesn't
honor it, flush-before-reporting on a dropped connection so a partial file
is always consistent), reimplemented against WinHTTP with one deliberate
mechanical difference explained below.

**Explicit download state** (`DownloadState.h`, following the same
`enum class` + `ToString()` pattern as `RuntimeState`/`ClientState` --
the project's existing convention, not a new one):

```
Idle -> Downloading -> [Interrupted -> Resuming ->]* Verifying -> Completed
                                                                 -> Failed
```

`DownloadAndVerify()` takes an optional `onStateChange` callback so a
caller (or a test) can observe these transitions directly, not just the
final result.

**Atomic completion.** Every download writes to `<destPath>.partial`, never
directly to `destPath`:

```
<destPath>.partial  ->  download/resume  ->  verify (size + hash)  ->  atomic rename  ->  <destPath>
```

`destPath` only ever becomes a fully-verified file, in one atomic
`std::filesystem::rename` -- a reader can never observe a partially
downloaded file at the final path. This is a stronger guarantee than
`downloader.js` currently makes (which writes directly to `destPath`
throughout); the `.partial`-suffix staging pattern mirrors the
bootstrapper's own stage-then-move convention in `updateManager.js`
(download to `downloads\`, move into place only once verified),
applied here at the single-file level.

**Determining the resume offset:** an existing `<destPath>.partial` file's
size becomes the requested `Range: bytes=<size>-` offset. A zero-byte
partial is treated as no partial at all. A partial file already >= the
expected size is discarded and restarted from zero (stale/foreign data,
not something to resume from).

**HTTP status handling**, mirroring `downloader.js`'s decision but adapted
to WinHTTP's request/response model:
- `206 Partial Content`: appended to the existing partial file.
- `200` in response to a Range request: the server doesn't support Range.
  `downloader.js` handles this by aborting and issuing a second, rangeless
  request; the native client instead writes the (already in-flight) 200
  response in truncate mode as the complete file -- same safety guarantee
  (never appended to stale partial data), one fewer round trip. See
  docs/decisions.md.
- `416 Range Not Satisfiable`: the resume offset is no longer valid (e.g.
  the resource changed server-side) -- the partial file is discarded and
  the download restarts from zero.
- `404`: permanent failure. Not retried; the partial file (if any) is
  deleted, since the resource is gone and there's nothing to resume
  toward.
- `5xx`: treated as transient -- retried (up to `DownloadRequest::maxAttempts`,
  default 2), partial file preserved.
- Connection reset / timeout mid-transfer: also transient. Detected two
  ways -- a `WinHttpReadData` error, or (since a plain TCP close can look
  like a clean end-of-body to that read loop) comparing bytes actually
  received against the response's `Content-Length` header. Either way,
  whatever was received is flushed before the interruption is reported, so
  the partial file is always consistent and resumable. This
  Content-Length cross-check is a real fix made during this phase's
  testing -- see docs/decisions.md.
- **Process interruption** (the whole client process dying mid-download,
  not just the connection) needs no special handling: the persistent
  `.partial` file is exactly what survives on disk either way, and the
  next call to `DownloadAndVerify()` -- on the next run of the process --
  resumes from it via the same offset-determination logic as any other
  partial file.

**Never treats an incomplete file as successful.** The final size and
hash are always checked against the manifest entry before the atomic
rename; a mismatch deletes `<destPath>.partial` and reports a failure.
`<destPath>` is never touched until both checks pass.

**Cleanup policy** (requirement: clean up only when *permanently*
invalid): a `404` or a hash/size verification failure deletes the partial
file, since neither is something a later retry could resolve. Exhausting
`maxAttempts` on repeated *transient* interruptions does **not** delete
the partial file -- that's not a permanent failure, just one this call
gave up retrying; a later call (the next process run, say) can still
resume from it.

### Testing

`runtime/tests/HttpDownloaderTests.cpp` (17 tests) exercises fresh
downloads, resume within one call and from a pre-existing partial file,
206/200-to-Range/416/404/5xx handling, connection interruption, timeout,
hash success/failure, zero-byte and oversized partial files, an
already-complete destination file, and the state-transition callback --
against `runtime/tests/MinimalHttpServer.h`, a small raw-socket test-only
HTTP server written for this (there is no HTTP server building block
anywhere in this codebase or the C++ standard library; WinHTTP is a
client-only API). `ResourceManagerTests.cpp` and `E2ETests.cpp` (the real
client against the real server) continue to pass unchanged, since
`DownloadAndVerify()'s signature stayed backward-compatible.

### Server side: how the manifest is computed

`server/src/resourceManifest.js`'s `computeManifest()` walks the
configured `resourcesDir` and hashes every file at startup -- the manifest
is always a live reflection of what's actually on disk, not a hand-maintained
fixture that can drift from reality. `resolveResourceFile()` rejects any
requested id containing a path separator or `..`, since the manifest only
ever advertises bare filenames from a directory listing.

**Declaring dependencies.** A directory listing can't tell you that one
resource depends on another, so that's declared separately, in
`server/resource-dependencies.json` (deliberately outside `resourcesDir`
-- anything inside it is advertised *and served* as a literal resource,
and a config file is not one):

```json
{ "dependent.txt": ["base.txt"] }
```

`computeManifest()` merges this into each resource's `dependencies` array
(defaulting to `[]`, and to `[]` again if the config file is missing or
malformed -- a bad config degrades to "no declared dependencies" rather
than crashing manifest computation).

## Resource lifecycle (client side)

Full client flow:

```
Connect -> Receive resource manifest -> Compare local cache
  -> Download missing/changed resources -> Verify hashes -> Store in cache
  -> Load resources -> Notify server -> Enter ready state
```

Each resource has its own deterministic state (`ResourceState.h`,
following the same `enum class` + `ToString()` convention as
`RuntimeState`/`ClientState`/`DownloadState`):

```
Pending -> Downloading -> Verifying -> Cached -> Loading -> Loaded -> Started
              |               |                     |
              v               v                     v
            Failed          Failed                Failed
Started <-> Stopped (Start()/Stop())
Started|Stopped -> Loading (Reload())
```

`ResourceRegistry::TransitionTo()` validates every change against this
table (`IsValidTransition()`); an invalid jump is rejected, not silently
allowed. **This is the actual enforcement behind "do not allow unverified
files to load"**: `Loading` is reachable only from `Cached`, so nothing
that skipped download/verification can ever reach it, structurally --
not by convention, by construction.

### Dependency ordering

`ResourceRegistry::LoadAll()` runs once `ApplyManifest()` has verified
whatever it could:

1. Only `Cached` resources are eligible -- anything still `Pending` (never
   verified) or already `Failed` (download error) is left alone.
2. Any eligible resource whose declared dependency is missing from the
   manifest, or already `Failed`, is itself marked `Failed` (with a
   specific reason: `"missing dependency: X"` or `"dependency failed: X"`)
   -- and this cascades transitively, so a whole broken subtree fails
   together while everything outside it still loads normally.
3. The remaining resources are topologically sorted (Kahn's algorithm) and
   loaded in dependency order. Anything left over after that -- a resource
   that's part of a circular dependency -- is marked `Failed` with
   `"circular dependency"`.
4. "Loading" a resource means confirming its bytes are still present and
   correctly sized on disk (catching e.g. external deletion between
   verification and load) and marking it available -- it does **not**
   parse or run the resource's content, since that requires knowing what
   *kind* of resource it is, which requires a real consumer (GTA V
   integration, docs/game-integration.md). Reaching `Loaded` is the
   honest, real endpoint of this phase's lifecycle; `Start()` is a
   separate, explicitly-invoked operation (see below), not something
   `LoadAll()` does automatically.

### Start / Stop / Reload

Explicit lifecycle operations, callable per resource once it's `Loaded`:
- **Start** (`Loaded|Stopped -> Started`): marks the resource active.
- **Stop** (`Started -> Stopped`): marks it inactive.
- **Reload** (`Started|Stopped -> Loading -> Loaded[-> Started]`):
  re-validates the on-disk file (without re-downloading -- refetching a
  changed file is `ApplyManifest()`'s job, a fresh manifest, not this) and
  restores whichever of `Loaded`/`Started` it was in before.

As with `Loaded`, none of these do anything GTA-V-specific -- they're real
state transitions and real file re-validation, available for whatever
future consumer needs them, not pretending to activate game content that
doesn't have a runtime to activate in yet.

### Notify server, enter ready state

Once `LoadAll()` finishes, the client sends `resources_ready`
(`{loadedCount, failedCount}`) and waits for `resources_ready_ack`
(docs/networking.md). The server logs it and broadcasts a `player_ready`
event to other authenticated sessions (`server/src/tcpServer.js`) -- a
real, minimal, useful thing to do with the notification, not a placeholder.
Only after this does `Runtime.cpp` transition to `READY`.

### Error reporting

Every resource's terminal `Failed` state carries a specific message
(`ResourceRecord::error`) -- "missing dependency: X," "circular
dependency," "hash mismatch: ...," "file missing during load: ..." --
surfaced through `IResourceManager::GetAllResources()` /
`GetResourceState(id)`, and logged individually by `Runtime.cpp` before
the aggregate `resource-loading` service event.

### What's still out of scope

Downloading, verifying, caching, ordering, and lifecycle-tracking
resources is real and tested end-to-end (`runtime/tests/E2ETests.cpp`
now exercises real dependent resources against a real server). What a
resource's content actually *does* -- parsing/running it against a game
-- is not: that requires a real consumer, which requires GTA V
integration (docs/game-integration.md). The plumbing is legitimate and
complete; the destination isn't built.

### Testing

`runtime/tests/ResourceStateTests.cpp` (the transition table) and
`runtime/tests/ResourceRegistryTests.cpp` (dependency ordering, missing/
circular/cascading-failure cases, start/stop/reload, "unverified files
never load") cover the lifecycle in isolation, without any network.
`runtime/tests/ResourceManagerTests.cpp` adds the network-backed cases
against `MinimalHttpServer`: missing resource, changed resource,
corrupted resource, failed download (isolated to that resource), cache
hit (proven by zero requests to the server), cache invalidation (a second
`ApplyManifest()` call with different content is not satisfied by the
first call's stale `Cached` state), and dependencies parsed end-to-end
from a real manifest. Server-side, `resourceManifest.test.js` covers the
dependency-config file (present, absent, malformed) and `server.test.js`
covers `resources_ready`'s acknowledgment and broadcast.





