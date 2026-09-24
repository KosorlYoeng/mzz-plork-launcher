const { ensureAppData, readVersion, writeVersion } = require("./appData");
const { fetchManifest, isValidManifest } = require("./manifest");
const { determineInstallState } = require("./installState");
const { planUpdate, applyUpdate, verifyComponentFiles } = require("./updateManager");
const { launchClient } = require("./clientLauncher");

// Implements the bootstrapper flow:
//   Initialize -> Check Application Data -> (missing? install : check updates)
//   -> download/update -> verify -> start MzzPlorkClient.exe
//
// Every real dependency (filesystem, network, process spawn) is injectable
// so this can run against a local test HTTP server / temp directory
// without Electron. Defaults wire it to the real modules for production
// use from electron/main.js.
async function runBootstrapper(options) {
  const {
    manifestUrl,
    downloadBaseUrl,
    onProgress = () => {},
    ensureAppDataFn = ensureAppData,
    readVersionFn = readVersion,
    writeVersionFn = writeVersion,
    fetchManifestFn = fetchManifest,
    isValidManifestFn = isValidManifest,
    determineInstallStateFn = determineInstallState,
    planUpdateFn = planUpdate,
    applyUpdateFn = applyUpdate,
    verifyComponentFilesFn = verifyComponentFiles,
    launchFn = launchClient
  } = options;

  onProgress({ stage: "appdata", status: "pending", message: "Checking MzzPlork Application Data..." });
  const appDataRoot = ensureAppDataFn();
  const localVersion = readVersionFn(appDataRoot);
  const installState = determineInstallStateFn(appDataRoot, localVersion);
  onProgress({
    stage: "appdata",
    status: "ok",
    message: `${installState === "first-install" ? "First install" : "Existing install"} at ${appDataRoot}`
  });

  onProgress({ stage: "manifest", status: "pending", message: "Checking for updates..." });
  let manifest = null;
  try {
    manifest = await fetchManifestFn(manifestUrl);
    if (!isValidManifestFn(manifest)) {
      throw new Error("manifest failed schema validation");
    }
    onProgress({ stage: "manifest", status: "ok", message: "Manifest retrieved" });
  } catch (err) {
    if (installState === "first-install") {
      onProgress({ stage: "manifest", status: "error", message: `Cannot install: ${err.message}` });
      return { ok: false, stage: "manifest", error: err.message };
    }
    onProgress({ stage: "manifest", status: "error", message: `Update check unavailable: ${err.message}` });
  }

  if (manifest) {
    const plan = planUpdateFn(manifest, localVersion);
    if (plan.length === 0) {
      onProgress({ stage: "update", status: "ok", message: "Up to date" });
    } else {
      onProgress({ stage: "update", status: "pending", message: `Updating: ${plan.map((p) => p.component).join(", ")}` });
      const { results } = await applyUpdateFn(appDataRoot, downloadBaseUrl, plan);

      for (const result of results) {
        if (result.status === "ok") {
          localVersion[result.component] = { version: result.targetVersion, files: result.files };
          onProgress({
            stage: "update",
            status: "ok",
            message: `${result.component}: downloaded ${result.downloaded.length}, reused ${result.unchanged.length}, removed ${result.removed.length}`
          });
        } else {
          onProgress({ stage: "update", status: "error", message: `${result.component} update failed: ${result.error}` });
          // The runtime component is required to have anything to launch at
          // all; resources are content the client loads later (Phase 10/14),
          // so a first-install failure there is not fatal to bootstrapping.
          if (installState === "first-install" && result.component === "runtime") {
            return { ok: false, stage: "update", error: result.error, results };
          }
        }
      }
      writeVersionFn(appDataRoot, localVersion);

      const runtimeResult = results.find((r) => r.component === "runtime" && r.status === "ok");
      if (runtimeResult) {
        const verification = await verifyComponentFilesFn(appDataRoot, "runtime", runtimeResult.files);
        if (!verification.ok) {
          const problems = [...verification.missing, ...verification.corrupt].join(", ");
          onProgress({ stage: "update", status: "error", message: `Installed runtime files failed verification: ${problems}` });
          if (installState === "first-install") {
            return { ok: false, stage: "verify", error: "post-install verification failed", verification };
          }
        } else {
          onProgress({ stage: "update", status: "ok", message: "Installation verified" });
        }
      }
    }
  }

  onProgress({ stage: "client", status: "pending", message: "Starting MzzPlorkClient..." });
  const child = launchFn(appDataRoot, (event) => {
    onProgress({ stage: "client-event", status: event.event === "error" ? "error" : "ok", message: JSON.stringify(event), event });
    if (event.event === "error") {
      onProgress({ stage: "client", status: "error", message: event.message });
    } else if (event.event === "state" && event.state === "ready") {
      onProgress({ stage: "client", status: "ok", message: "Client runtime ready" });
    } else if (event.event === "exit" && event.crashed) {
      onProgress({ stage: "client", status: "error", message: `client exited unexpectedly (code ${event.code})` });
    }
  });

  if (!child) {
    onProgress({ stage: "client", status: "error", message: "client runtime not installed" });
    return { ok: false, stage: "client", error: "client runtime not installed" };
  }

  return { ok: true, appDataRoot, installState, child };
}

module.exports = { runBootstrapper };
