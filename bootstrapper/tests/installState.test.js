const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");

const { determineInstallState } = require("../electron/installState");

function tmpAppData() {
  return fs.mkdtempSync(path.join(os.tmpdir(), "mzzplork-appdata-"));
}

test("first installation: no client exe and no runtime version", () => {
  const root = tmpAppData();
  const version = { runtime: { version: "0.0.0", files: [] } };
  assert.equal(determineInstallState(root, version), "first-install");
});

test("first installation: version.json claims a version but the exe is missing", () => {
  const root = tmpAppData();
  const version = { runtime: { version: "1.2.3", files: [] } };
  assert.equal(determineInstallState(root, version), "first-install");
});

test("existing installation: exe present and a non-sentinel version recorded", () => {
  const root = tmpAppData();
  fs.mkdirSync(path.join(root, "runtime"), { recursive: true });
  fs.writeFileSync(path.join(root, "runtime", "MzzPlorkClient.exe"), "stub");
  const version = { runtime: { version: "1.2.3", files: [] } };
  assert.equal(determineInstallState(root, version), "existing");
});
