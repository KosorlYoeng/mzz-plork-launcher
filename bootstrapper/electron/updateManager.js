const fs = require("fs");
const path = require("path");
const { downloadFile, sha256File, CorruptedDownloadError } = require("./downloader");

// Where each manifest component's files live once installed.
const COMPONENT_TARGET_DIR = {
  runtime: "runtime",
  resources: "resources"
};

// Guards against a manifest entry whose `path` tries to escape the
// component's target directory (e.g. "../../../Windows/System32/x").
// The manifest is remote, attacker-controllable data, so this is a real
// boundary, not defensive dead code.
function resolveSafePath(baseDir, relPath) {
  const resolved = path.resolve(baseDir, relPath);
  const normalizedBase = path.resolve(baseDir) + path.sep;
  if (!resolved.startsWith(normalizedBase)) {
    throw new Error(`unsafe file path in manifest: "${relPath}" escapes ${baseDir}`);
  }
  return resolved;
}

function statSizeIfExists(filePath) {
  try {
    return fs.statSync(filePath).size;
  } catch {
    return null;
  }
}

// Builds a per-component, per-file plan: which remote files are missing or
// changed (toDownload), which locally-recorded files no longer appear in
// the remote manifest (toRemove -- deleted only after a successful update,
// see applyUpdate), and which are already correct and left untouched
// (unchanged). Hash+size are authoritative for "did this file change" --
// the descriptive `version` field on a file is not used for that decision,
// since two files could share a version string without sharing content.
// A component is only included in the plan if there is something to do.
function planUpdate(manifest, localVersion) {
  const plan = [];
  for (const component of Object.keys(COMPONENT_TARGET_DIR)) {
    const remote = manifest[component];
    if (!remote) continue;
    const local = (localVersion && localVersion[component]) || { version: "0.0.0", files: [] };
    const targetDir = COMPONENT_TARGET_DIR[component];

    const localByPath = new Map((local.files || []).map((f) => [f.path, f]));
    const remotePaths = new Set(remote.files.map((f) => f.path));

    // This only compares the two manifests (remote vs. the local ledger),
    // which keeps planUpdate pure and easy to unit test. It does not touch
    // disk -- applyUpdate re-checks each "unchanged" file is still actually
    // present and correctly sized before it actually skips downloading it,
    // so a file deleted or truncated outside the bootstrapper still
    // self-heals on the next run instead of being silently trusted forever.
    const toDownload = [];
    const unchanged = [];
    for (const remoteFile of remote.files) {
      const localRecord = localByPath.get(remoteFile.path);
      const recordMatches = localRecord && localRecord.hash === remoteFile.hash && localRecord.size === remoteFile.size;
      if (recordMatches) {
        unchanged.push(remoteFile);
      } else {
        toDownload.push(remoteFile);
      }
    }

    const toRemove = (local.files || []).filter((f) => !remotePaths.has(f.path)).map((f) => f.path);

    const needsAction = toDownload.length > 0 || toRemove.length > 0 || remote.version !== local.version;
    if (needsAction) {
      plan.push({ component, targetDir, targetVersion: remote.version, files: remote.files, toDownload, toRemove, unchanged });
    }
  }
  return plan;
}

function isRetryableError(err) {
  return err instanceof CorruptedDownloadError || /ECONNRESET|ETIMEDOUT|socket hang up|timed out/i.test(err.message || "");
}

function resolveDownloadUrl(downloadBaseUrl, component, file) {
  return file.url || `${downloadBaseUrl}/${component}/${file.path}`;
}

async function stagedFileMatches(stagingPath, expected) {
  const size = statSizeIfExists(stagingPath);
  if (size !== expected.size) return false;
  const hash = await sha256File(stagingPath).catch(() => null);
  return hash === expected.hash;
}

// Applies a plan built by planUpdate(). Each component is handled in two
// phases so a failure never leaves the target directory half-updated:
//   1. download + verify every changed file into downloads/<component>/
//      (skipping ones already staged correctly from a prior interrupted
//      attempt); abort the component here on any unrecoverable failure,
//      before anything on disk under targetDir/ has been touched.
//   2. only once every file for the component is staged and verified,
//      move them all into targetDir/ and remove obsolete files.
// Components are independent: one component failing does not affect
// another component's result.
async function applyUpdate(appDataRoot, downloadBaseUrl, plan, { maxAttemptsPerFile = 2 } = {}) {
  const results = [];
  for (const item of plan) {
    const targetDirPath = path.join(appDataRoot, item.targetDir);
    const stagedPaths = [];
    let failure = null;

    // planUpdate marked these "unchanged" based on the local ledger alone.
    // Re-check them against the actual file on disk here: if one was
    // deleted or truncated outside the bootstrapper since the ledger was
    // written, treat it as needing a download instead of trusting stale
    // bookkeeping.
    const effectiveUnchanged = [];
    const effectiveToDownload = [...item.toDownload];
    for (const file of item.unchanged) {
      const onDiskPath = path.join(targetDirPath, file.path);
      if (statSizeIfExists(onDiskPath) === file.size) {
        effectiveUnchanged.push(file);
      } else {
        effectiveToDownload.push(file);
      }
    }

    for (const file of effectiveToDownload) {
      let finalPath;
      let stagingPath;
      try {
        finalPath = resolveSafePath(targetDirPath, file.path);
        stagingPath = resolveSafePath(path.join(appDataRoot, "downloads", item.component), file.path);
      } catch (err) {
        failure = { file: file.path, error: err.message };
        break;
      }

      if (await stagedFileMatches(stagingPath, file)) {
        stagedPaths.push({ file, stagingPath, finalPath });
        continue;
      }

      const url = resolveDownloadUrl(downloadBaseUrl, item.component, file);
      let lastError;
      let succeeded = false;
      for (let attempt = 1; attempt <= maxAttemptsPerFile && !succeeded; attempt++) {
        try {
          await downloadFile(url, stagingPath, file);
          succeeded = true;
        } catch (err) {
          lastError = err;
          if (!isRetryableError(err)) break;
        }
      }
      if (!succeeded) {
        failure = { file: file.path, error: lastError && lastError.message };
        break;
      }
      stagedPaths.push({ file, stagingPath, finalPath });
    }

    if (failure) {
      results.push({ component: item.component, status: "failed", error: failure.error, file: failure.file });
      continue; // leave targetDir untouched for this component; try the next one
    }

    for (const { stagingPath, finalPath } of stagedPaths) {
      await fs.promises.mkdir(path.dirname(finalPath), { recursive: true });
      await fs.promises.rename(stagingPath, finalPath);
    }

    const removed = [];
    for (const relPath of item.toRemove) {
      try {
        const obsoletePath = resolveSafePath(targetDirPath, relPath);
        await fs.promises.unlink(obsoletePath);
        removed.push(relPath);
      } catch {
        // Already gone, or never existed -- removal is best-effort cleanup,
        // not a correctness requirement.
      }
    }

    results.push({
      component: item.component,
      status: "ok",
      targetVersion: item.targetVersion,
      files: item.files,
      downloaded: stagedPaths.map((s) => s.file.path),
      removed,
      unchanged: effectiveUnchanged.map((f) => f.path)
    });
  }
  return { ok: results.every((r) => r.status === "ok"), results };
}

// Re-verifies every file recorded for a component against its expected
// hash -- used as a final gate before trusting a fresh install (or as a
// manual "repair" check later), independent of the per-file verification
// that already happens inline during download.
async function verifyComponentFiles(appDataRoot, component, files) {
  const targetDir = path.join(appDataRoot, COMPONENT_TARGET_DIR[component]);
  const missing = [];
  const corrupt = [];
  for (const file of files) {
    const filePath = path.join(targetDir, file.path);
    if (!fs.existsSync(filePath)) {
      missing.push(file.path);
      continue;
    }
    const hash = await sha256File(filePath);
    if (hash !== file.hash || fs.statSync(filePath).size !== file.size) {
      corrupt.push(file.path);
    }
  }
  return { ok: missing.length === 0 && corrupt.length === 0, missing, corrupt };
}

module.exports = { planUpdate, applyUpdate, verifyComponentFiles, COMPONENT_TARGET_DIR, resolveSafePath };

