"use strict";
// Standalone launcher used only by the native runtime's automated
// end-to-end test (runtime/tests/E2ETests.cpp): starts a real MzzPlork
// Server on ephemeral ports against a throwaway resources directory, and
// prints the assigned ports so the spawning test can connect to them.
const fs = require("fs");
const os = require("os");
const path = require("path");
const { startServer } = require("../src/main");

const resourcesDir = fs.mkdtempSync(path.join(os.tmpdir(), "mzzplork-e2e-resources-"));
fs.writeFileSync(path.join(resourcesDir, "e2e-test-resource.txt"), "native client end-to-end test fixture");

startServer({
  tcpPort: 0,
  httpPort: 0,
  resourcesDir,
  logFilePath: path.join(resourcesDir, "server.log")
}).then((instance) => {
  console.log("TCP_PORT=" + instance.tcpPort);
  console.log("HTTP_PORT=" + instance.httpPort);
});
