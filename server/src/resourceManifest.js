"use strict";
const fs = require("fs");
const path = require("path");
const crypto = require("crypto");

// Computes the resource manifest by actually walking `resourcesDir` and
// hashing each file -- this is real data derived from real files on
// disk, not a hardcoded fixture, so a resource actually being wrong or
// missing is reflected immediately in the manifest served to clients.
function sha256File(filePath) {
  const buffer = fs.readFileSync(filePath);
  return crypto.createHash("sha256").update(buffer).digest("hex");
}

// Dependencies can't be inferred from a directory listing, so they're
// declared separately: {"<resourceId>": ["<dependencyId>", ...]}. This
// file lives outside resourcesDir on purpose -- anything inside
// resourcesDir gets advertised (and served) as a literal resource, and a
// config file is not one.
function loadDependencyConfig(configPath) {
  if (!configPath || !fs.existsSync(configPath)) return {};
  try {
    return JSON.parse(fs.readFileSync(configPath, "utf8"));
  } catch {
    return {};
  }
}

function computeManifest(resourcesDir, manifestVersion = "1.0.0", dependenciesConfigPath = null) {
  const configPath = dependenciesConfigPath || path.join(path.dirname(resourcesDir), "resource-dependencies.json");
  const dependencyConfig = loadDependencyConfig(configPath);

  const resources = [];
  if (fs.existsSync(resourcesDir)) {
    for (const entry of fs.readdirSync(resourcesDir, { withFileTypes: true })) {
      if (!entry.isFile()) continue;
      const filePath = path.join(resourcesDir, entry.name);
      const stat = fs.statSync(filePath);
      resources.push({
        id: entry.name,
        path: entry.name,
        size: stat.size,
        hash: sha256File(filePath),
        version: manifestVersion,
        dependencies: Array.isArray(dependencyConfig[entry.name]) ? dependencyConfig[entry.name] : []
      });
    }
  }
  return { manifestVersion, resources };
}

function resolveResourceFile(resourcesDir, id) {
  // Reject anything that isn't a bare filename -- no path separators, no
  // "..". The manifest only ever advertises bare filenames from a
  // directory listing, so a client asking for anything else is not a
  // legitimate request.
  if (!id || id.includes("/") || id.includes("\\") || id.includes("..")) {
    return null;
  }
  const filePath = path.join(resourcesDir, id);
  return fs.existsSync(filePath) ? filePath : null;
}

module.exports = { computeManifest, resolveResourceFile, sha256File };

