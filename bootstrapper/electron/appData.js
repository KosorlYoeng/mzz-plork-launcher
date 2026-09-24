const fs = require("fs");
const path = require("path");
const os = require("os");

const SUBDIRS = ["runtime", "resources", "cache", "config", "logs", "downloads"];

function resolveAppDataRoot() {
  const base = process.env.LOCALAPPDATA || path.join(os.homedir(), "AppData", "Local");
  return path.join(base, "MzzPlork");
}

function ensureAppData() {
  const root = resolveAppDataRoot();
  fs.mkdirSync(root, { recursive: true });
  for (const dir of SUBDIRS) {
    fs.mkdirSync(path.join(root, dir), { recursive: true });
  }
  return root;
}

function versionFilePath(root) {
  return path.join(root, "version.json");
}

// Mirrors schemas/version-manifest.schema.json so the local record and a
// freshly-fetched remote manifest can be diffed directly. "0.0.0" is the
// sentinel meaning "nothing installed for this component yet".
function defaultVersion() {
  return {
    version: "0.0.0",
    generatedAt: new Date().toISOString(),
    runtime: { version: "0.0.0", files: [] },
    resources: { version: "0.0.0", files: [] }
  };
}

function readVersion(root) {
  const file = versionFilePath(root);
  if (!fs.existsSync(file)) {
    const initial = defaultVersion();
    fs.writeFileSync(file, JSON.stringify(initial, null, 2), "utf8");
    return initial;
  }
  return JSON.parse(fs.readFileSync(file, "utf8"));
}

function writeVersion(root, data) {
  fs.writeFileSync(versionFilePath(root), JSON.stringify(data, null, 2), "utf8");
}

module.exports = { resolveAppDataRoot, ensureAppData, readVersion, writeVersion, defaultVersion, SUBDIRS };
