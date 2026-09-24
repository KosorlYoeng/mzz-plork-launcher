const test = require("node:test");
const assert = require("node:assert/strict");

const { runBootstrapper } = require("../electron/bootstrap");

function collectProgress() {
  const events = [];
  return { onProgress: (e) => events.push(e), events };
}

const FIRST_INSTALL_VERSION = { runtime: { version: "0.0.0", files: [] }, resources: { version: "0.0.0", files: [] } };
const EXISTING_VERSION = { runtime: { version: "1.0.0", files: [] }, resources: { version: "1.0.0", files: [] } };
const MANIFEST = { version: "1.0.0", runtime: { version: "1.0.0", files: [] }, resources: { version: "1.0.0", files: [] } };

function okResult(component, overrides = {}) {
  return { component, status: "ok", targetVersion: "1.0.0", files: [], downloaded: [], removed: [], unchanged: [], ...overrides };
}

function baseDeps(overrides = {}) {
  return {
    ensureAppDataFn: () => "C:\\fake\\appdata",
    readVersionFn: () => EXISTING_VERSION,
    writeVersionFn: () => {},
    fetchManifestFn: async () => MANIFEST,
    isValidManifestFn: () => true,
    determineInstallStateFn: () => "existing",
    planUpdateFn: () => [],
    applyUpdateFn: async () => ({ ok: true, results: [] }),
    verifyComponentFilesFn: async () => ({ ok: true, missing: [], corrupt: [] }),
    launchFn: (appDataRoot, onEvent) => {
      onEvent({ event: "state", state: "ready" });
      return { pid: 1234 };
    },
    ...overrides
  };
}

test("first installation: fetches manifest, installs, verifies, and launches the client", async () => {
  const { onProgress, events } = collectProgress();
  let appliedPlan = null;
  const result = await runBootstrapper(
    baseDeps({
      readVersionFn: () => FIRST_INSTALL_VERSION,
      determineInstallStateFn: () => "first-install",
      planUpdateFn: () => [{ component: "runtime", targetVersion: "1.0.0", files: [] }],
      applyUpdateFn: async (root, url, plan) => {
        appliedPlan = plan;
        return { ok: true, results: [okResult("runtime")] };
      },
      onProgress
    })
  );
  assert.equal(result.ok, true);
  assert.equal(result.installState, "first-install");
  assert.ok(appliedPlan && appliedPlan.length === 1, "should have applied the install plan");
  assert.ok(events.some((e) => e.stage === "client" && e.status === "ok"));
});

test("existing installation with no update just launches the client", async () => {
  const { onProgress, events } = collectProgress();
  const result = await runBootstrapper(baseDeps({ onProgress }));
  assert.equal(result.ok, true);
  assert.equal(result.installState, "existing");
  assert.ok(events.some((e) => e.stage === "update" && e.message === "Up to date"));
});

test("update available: downloads and applies before launching", async () => {
  const { onProgress } = collectProgress();
  let applyCalled = false;
  await runBootstrapper(
    baseDeps({
      planUpdateFn: () => [{ component: "runtime", targetVersion: "1.1.0", files: [] }],
      applyUpdateFn: async () => {
        applyCalled = true;
        return { ok: true, results: [okResult("runtime", { targetVersion: "1.1.0" })] };
      },
      onProgress
    })
  );
  assert.equal(applyCalled, true);
});

test("no update available: does not call applyUpdate", async () => {
  let applyCalled = false;
  await runBootstrapper(
    baseDeps({
      planUpdateFn: () => [],
      applyUpdateFn: async () => {
        applyCalled = true;
        return { ok: true, results: [] };
      }
    })
  );
  assert.equal(applyCalled, false);
});

test("failed runtime update on first install is a hard failure (nothing to launch)", async () => {
  const { onProgress, events } = collectProgress();
  const result = await runBootstrapper(
    baseDeps({
      readVersionFn: () => FIRST_INSTALL_VERSION,
      determineInstallStateFn: () => "first-install",
      planUpdateFn: () => [{ component: "runtime", targetVersion: "1.0.0", files: [] }],
      applyUpdateFn: async () => ({ ok: false, results: [{ component: "runtime", status: "failed", error: "network down" }] }),
      onProgress
    })
  );
  assert.equal(result.ok, false);
  assert.equal(result.stage, "update");
  assert.ok(events.some((e) => e.stage === "update" && e.status === "error"));
});

test("failed resources update on first install is not fatal (runtime is what matters to launch)", async () => {
  let launched = false;
  const result = await runBootstrapper(
    baseDeps({
      readVersionFn: () => FIRST_INSTALL_VERSION,
      determineInstallStateFn: () => "first-install",
      planUpdateFn: () => [
        { component: "runtime", targetVersion: "1.0.0", files: [] },
        { component: "resources", targetVersion: "1.0.0", files: [] }
      ],
      applyUpdateFn: async () => ({
        ok: false,
        results: [okResult("runtime"), { component: "resources", status: "failed", error: "404" }]
      }),
      launchFn: (root, onEvent) => {
        launched = true;
        onEvent({ event: "state", state: "ready" });
        return { pid: 1 };
      }
    })
  );
  assert.equal(launched, true);
  assert.equal(result.ok, true);
});

test("failed update on an existing install falls through and still launches the current client", async () => {
  let launched = false;
  const result = await runBootstrapper(
    baseDeps({
      planUpdateFn: () => [{ component: "runtime", targetVersion: "1.1.0", files: [] }],
      applyUpdateFn: async () => ({ ok: false, results: [{ component: "runtime", status: "failed", error: "network down" }] }),
      launchFn: (root, onEvent) => {
        launched = true;
        onEvent({ event: "state", state: "ready" });
        return { pid: 1 };
      }
    })
  );
  assert.equal(launched, true);
  assert.equal(result.ok, true);
});

test("post-install verification failure on first install is a hard failure", async () => {
  const result = await runBootstrapper(
    baseDeps({
      readVersionFn: () => FIRST_INSTALL_VERSION,
      determineInstallStateFn: () => "first-install",
      planUpdateFn: () => [{ component: "runtime", targetVersion: "1.0.0", files: [] }],
      applyUpdateFn: async () => ({ ok: true, results: [okResult("runtime")] }),
      verifyComponentFilesFn: async () => ({ ok: false, missing: ["MzzPlorkClient.exe"], corrupt: [] })
    })
  );
  assert.equal(result.ok, false);
  assert.equal(result.stage, "verify");
});

test("missing runtime: launch returns null, reported as a client-stage error", async () => {
  const { onProgress, events } = collectProgress();
  const result = await runBootstrapper(baseDeps({ launchFn: () => null, onProgress }));
  assert.equal(result.ok, false);
  assert.equal(result.stage, "client");
  assert.ok(events.some((e) => e.stage === "client" && e.status === "error" && /not installed/.test(e.message)));
});

test("client startup failure: spawn succeeds but the process reports a crash", async () => {
  const { onProgress, events } = collectProgress();
  const result = await runBootstrapper(
    baseDeps({
      launchFn: (root, onEvent) => {
        onEvent({ event: "exit", code: 1, signal: null, crashed: true });
        return { pid: 1 };
      },
      onProgress
    })
  );
  assert.equal(result.ok, true, "runBootstrapper resolves once the process is spawned; crash is reported via onProgress");
  assert.ok(events.some((e) => e.stage === "client" && e.status === "error" && /exited unexpectedly/.test(e.message)));
});

test("manifest fetch failure on first install cannot proceed (no files to install)", async () => {
  const result = await runBootstrapper(
    baseDeps({
      readVersionFn: () => FIRST_INSTALL_VERSION,
      determineInstallStateFn: () => "first-install",
      fetchManifestFn: async () => {
        throw new Error("network unreachable");
      }
    })
  );
  assert.equal(result.ok, false);
  assert.equal(result.stage, "manifest");
});

test("manifest fetch failure on an existing install still launches using local state", async () => {
  let launched = false;
  const result = await runBootstrapper(
    baseDeps({
      fetchManifestFn: async () => {
        throw new Error("network unreachable");
      },
      launchFn: (root, onEvent) => {
        launched = true;
        onEvent({ event: "state", state: "ready" });
        return { pid: 1 };
      }
    })
  );
  assert.equal(launched, true);
  assert.equal(result.ok, true);
});
