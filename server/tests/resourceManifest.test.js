const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const crypto = require("node:crypto");

const { computeManifest, resolveResourceFile } = require("../src/resourceManifest");

function tmpResourcesDir() {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "mzzplork-server-resources-"));
  return dir;
}

test("computeManifest: reflects real files and their real hashes on disk", () => {
  const dir = tmpResourcesDir();
  fs.writeFileSync(path.join(dir, "a.txt"), "hello");
  const manifest = computeManifest(dir, "9.9.9");

  assert.equal(manifest.manifestVersion, "9.9.9");
  assert.equal(manifest.resources.length, 1);
  assert.equal(manifest.resources[0].id, "a.txt");
  assert.equal(manifest.resources[0].size, 5);
  assert.equal(manifest.resources[0].hash, crypto.createHash("sha256").update("hello").digest("hex"));
});

test("computeManifest: empty directory yields an empty resource list, not an error", () => {
  const dir = tmpResourcesDir();
  const manifest = computeManifest(dir);
  assert.deepEqual(manifest.resources, []);
});

test("resolveResourceFile: rejects path traversal and separators", () => {
  const dir = tmpResourcesDir();
  fs.writeFileSync(path.join(dir, "safe.txt"), "x");
  assert.equal(resolveResourceFile(dir, "safe.txt") !== null, true);
  assert.equal(resolveResourceFile(dir, "../safe.txt"), null);
  assert.equal(resolveResourceFile(dir, "..\\safe.txt"), null);
  assert.equal(resolveResourceFile(dir, "sub/safe.txt"), null);
  assert.equal(resolveResourceFile(dir, "does-not-exist.txt"), null);
});


test("computeManifest: defaults to an empty dependency list when no config file exists", () => {
  const dir = tmpResourcesDir();
  fs.writeFileSync(path.join(dir, "a.txt"), "hello");
  const manifest = computeManifest(dir, "1.0.0", path.join(dir, "does-not-exist.json"));
  assert.deepEqual(manifest.resources[0].dependencies, []);
});

test("computeManifest: applies declared dependencies from the config file", () => {
  const dir = tmpResourcesDir();
  fs.writeFileSync(path.join(dir, "base.txt"), "base");
  fs.writeFileSync(path.join(dir, "dependent.txt"), "dependent");
  const configPath = path.join(dir, "deps.json");
  fs.writeFileSync(configPath, JSON.stringify({ "dependent.txt": ["base.txt"] }));

  const manifest = computeManifest(dir, "1.0.0", configPath);
  const byId = Object.fromEntries(manifest.resources.map((r) => [r.id, r]));

  assert.deepEqual(byId["dependent.txt"].dependencies, ["base.txt"]);
  assert.deepEqual(byId["base.txt"].dependencies, []);
});

test("computeManifest: a malformed dependency config is ignored rather than crashing manifest computation", () => {
  const dir = tmpResourcesDir();
  fs.writeFileSync(path.join(dir, "a.txt"), "hello");
  const configPath = path.join(dir, "deps.json");
  fs.writeFileSync(configPath, "{not valid json");

  const manifest = computeManifest(dir, "1.0.0", configPath);
  assert.deepEqual(manifest.resources[0].dependencies, []);
});
