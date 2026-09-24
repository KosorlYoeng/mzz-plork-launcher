const test = require("node:test");
const assert = require("node:assert/strict");
const http = require("node:http");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const crypto = require("node:crypto");

const { planUpdate, applyUpdate } = require("../electron/updateManager");

function tmpAppData() {
  return fs.mkdtempSync(path.join(os.tmpdir(), "mzzplork-update-"));
}

function fileEntry(buffer, relPath, extra = {}) {
  return {
    path: relPath,
    size: buffer.length,
    hash: crypto.createHash("sha256").update(buffer).digest("hex"),
    version: "1.0.0",
    ...extra
  };
}

function listen(server) {
  return new Promise((resolve) => server.listen(0, "127.0.0.1", () => resolve(server.address().port)));
}
function close(server) {
  return new Promise((resolve) => server.close(resolve));
}

test("planUpdate: update available when a file's hash differs from the local ledger", () => {
  const oldPayload = Buffer.from("old-bytes");
  const newPayload = Buffer.from("new-bytes-here");
  const manifest = { runtime: { version: "1.1.0", files: [fileEntry(newPayload, "MzzPlorkClient.exe")] } };
  const local = { runtime: { version: "1.0.0", files: [fileEntry(oldPayload, "MzzPlorkClient.exe")] } };
  const plan = planUpdate(manifest, local);
  assert.equal(plan.length, 1);
  assert.equal(plan[0].component, "runtime");
  assert.equal(plan[0].toDownload.length, 1);
  assert.equal(plan[0].toDownload[0].path, "MzzPlorkClient.exe");
  assert.equal(plan[0].unchanged.length, 0);
});

test("planUpdate: no update when every file's hash+size already matches", () => {
  const payload = Buffer.from("identical-bytes");
  const entry = fileEntry(payload, "MzzPlorkClient.exe");
  const manifest = { runtime: { version: "1.0.0", files: [entry] } };
  const local = { runtime: { version: "1.0.0", files: [entry] } };
  assert.deepEqual(planUpdate(manifest, local), []);
});

test("planUpdate: only the changed file is queued, unchanged files are left alone", () => {
  const unchangedPayload = Buffer.from("stays-the-same");
  const changedOld = Buffer.from("resource-v1");
  const changedNew = Buffer.from("resource-v2-longer");
  const unchangedEntry = fileEntry(unchangedPayload, "textures.pak");
  const manifest = {
    resources: { version: "2.0.0", files: [unchangedEntry, fileEntry(changedNew, "models.pak")] }
  };
  const local = {
    resources: { version: "1.0.0", files: [unchangedEntry, fileEntry(changedOld, "models.pak")] }
  };
  const plan = planUpdate(manifest, local);
  assert.equal(plan.length, 1);
  assert.deepEqual(plan[0].toDownload.map((f) => f.path), ["models.pak"]);
  assert.deepEqual(plan[0].unchanged.map((f) => f.path), ["textures.pak"]);
});

test("planUpdate: a file removed from the manifest is queued for safe removal", () => {
  const kept = fileEntry(Buffer.from("kept"), "kept.pak");
  const removed = fileEntry(Buffer.from("obsolete"), "obsolete.pak");
  const manifest = { resources: { version: "2.0.0", files: [kept] } };
  const local = { resources: { version: "1.0.0", files: [kept, removed] } };
  const plan = planUpdate(manifest, local);
  assert.equal(plan.length, 1);
  assert.deepEqual(plan[0].toRemove, ["obsolete.pak"]);
  assert.equal(plan[0].toDownload.length, 0);
});

test("applyUpdate: installs a new runtime file and does not redownload an unchanged one", async () => {
  const newPayload = Buffer.from("fake-client-binary-v2");
  const unchangedPayload = Buffer.from("support-file-unchanged");
  const newEntry = fileEntry(newPayload, "MzzPlorkClient.exe");
  const unchangedEntry = fileEntry(unchangedPayload, "support.dll");

  const requestedPaths = [];
  const server = http.createServer((req, res) => {
    requestedPaths.push(req.url);
    if (req.url.endsWith("MzzPlorkClient.exe")) {
      res.writeHead(200, { "Content-Length": newPayload.length });
      res.end(newPayload);
    } else {
      res.writeHead(404);
      res.end();
    }
  });
  const port = await listen(server);
  const appDataRoot = tmpAppData();

  // Pre-seed the "unchanged" file on disk, exactly as a prior install left it.
  fs.mkdirSync(path.join(appDataRoot, "runtime"), { recursive: true });
  fs.writeFileSync(path.join(appDataRoot, "runtime", "support.dll"), unchangedPayload);

  const plan = [
    {
      component: "runtime",
      targetDir: "runtime",
      targetVersion: "1.1.0",
      files: [newEntry, unchangedEntry],
      toDownload: [newEntry],
      toRemove: [],
      unchanged: [unchangedEntry]
    }
  ];
  try {
    const result = await applyUpdate(appDataRoot, `http://127.0.0.1:${port}`, plan);
    assert.equal(result.ok, true);
    assert.deepEqual(requestedPaths, ["/runtime/MzzPlorkClient.exe"], "the unchanged file must never be requested");
    assert.deepEqual(fs.readFileSync(path.join(appDataRoot, "runtime", "MzzPlorkClient.exe")), newPayload);
    assert.deepEqual(fs.readFileSync(path.join(appDataRoot, "runtime", "support.dll")), unchangedPayload);
  } finally {
    await close(server);
  }
});

test("applyUpdate: removes an obsolete file only after the update succeeds", async () => {
  const kept = Buffer.from("kept-bytes");
  const keptEntry = fileEntry(kept, "kept.pak");
  const server = http.createServer((req, res) => {
    res.writeHead(200, { "Content-Length": kept.length });
    res.end(kept);
  });
  const port = await listen(server);
  const appDataRoot = tmpAppData();
  fs.mkdirSync(path.join(appDataRoot, "resources"), { recursive: true });
  fs.writeFileSync(path.join(appDataRoot, "resources", "obsolete.pak"), "old content");

  const plan = [
    {
      component: "resources",
      targetDir: "resources",
      targetVersion: "2.0.0",
      files: [keptEntry],
      toDownload: [keptEntry],
      toRemove: ["obsolete.pak"],
      unchanged: []
    }
  ];
  try {
    const result = await applyUpdate(appDataRoot, `http://127.0.0.1:${port}`, plan);
    assert.equal(result.ok, true);
    assert.equal(result.results[0].removed[0], "obsolete.pak");
    assert.equal(fs.existsSync(path.join(appDataRoot, "resources", "obsolete.pak")), false);
  } finally {
    await close(server);
  }
});

test("applyUpdate: a failed download leaves obsolete files untouched (clean error recovery)", async () => {
  const server = http.createServer((req, res) => {
    res.writeHead(404);
    res.end();
  });
  const port = await listen(server);
  const appDataRoot = tmpAppData();
  fs.mkdirSync(path.join(appDataRoot, "resources"), { recursive: true });
  fs.writeFileSync(path.join(appDataRoot, "resources", "still-here.pak"), "old content");

  const failing = fileEntry(Buffer.from("wont-arrive"), "new.pak");
  const plan = [
    {
      component: "resources",
      targetDir: "resources",
      targetVersion: "2.0.0",
      files: [failing],
      toDownload: [failing],
      toRemove: ["still-here.pak"],
      unchanged: []
    }
  ];
  try {
    const result = await applyUpdate(appDataRoot, `http://127.0.0.1:${port}`, plan);
    assert.equal(result.ok, false);
    assert.equal(result.results[0].status, "failed");
    assert.equal(fs.existsSync(path.join(appDataRoot, "resources", "still-here.pak")), true, "must not remove obsolete files when the update failed");
  } finally {
    await close(server);
  }
});

test("applyUpdate: corrupted download fails cleanly and installs nothing", async () => {
  const payload = Buffer.from("real-bytes");
  const wrongEntry = { path: "MzzPlorkClient.exe", size: payload.length, hash: "0".repeat(64), version: "1.0.0" };
  const server = http.createServer((req, res) => {
    res.writeHead(200, { "Content-Length": payload.length });
    res.end(payload);
  });
  const port = await listen(server);
  const appDataRoot = tmpAppData();
  const plan = [
    { component: "runtime", targetDir: "runtime", targetVersion: "1.0.0", files: [wrongEntry], toDownload: [wrongEntry], toRemove: [], unchanged: [] }
  ];
  try {
    const result = await applyUpdate(appDataRoot, `http://127.0.0.1:${port}`, plan);
    assert.equal(result.ok, false);
    assert.equal(fs.existsSync(path.join(appDataRoot, "runtime", "MzzPlorkClient.exe")), false);
  } finally {
    await close(server);
  }
});

test("applyUpdate: uses a file's explicit url instead of the derived download path", async () => {
  const payload = Buffer.from("cdn-hosted-file");
  const entry = fileEntry(payload, "asset.pak");
  const server = http.createServer((req, res) => {
    if (req.url === "/cdn/asset.pak") {
      res.writeHead(200, { "Content-Length": payload.length });
      res.end(payload);
    } else {
      res.writeHead(404);
      res.end();
    }
  });
  const port = await listen(server);
  entry.url = `http://127.0.0.1:${port}/cdn/asset.pak`;
  const appDataRoot = tmpAppData();
  const plan = [
    { component: "resources", targetDir: "resources", targetVersion: "1.0.0", files: [entry], toDownload: [entry], toRemove: [], unchanged: [] }
  ];
  try {
    const result = await applyUpdate(appDataRoot, "http://127.0.0.1:1/unused", plan);
    assert.equal(result.ok, true);
    assert.deepEqual(fs.readFileSync(path.join(appDataRoot, "resources", "asset.pak")), payload);
  } finally {
    await close(server);
  }
});

test("applyUpdate: rejects a manifest file path that escapes the target directory", async () => {
  const malicious = fileEntry(Buffer.from("x"), "../../../evil.txt");
  const appDataRoot = tmpAppData();
  const plan = [
    { component: "runtime", targetDir: "runtime", targetVersion: "1.0.0", files: [malicious], toDownload: [malicious], toRemove: [], unchanged: [] }
  ];
  const result = await applyUpdate(appDataRoot, "http://127.0.0.1:1/unused", plan);
  assert.equal(result.ok, false);
  assert.match(result.results[0].error, /unsafe file path/);
  assert.equal(fs.existsSync(path.join(path.dirname(appDataRoot), "evil.txt")), false);
});
