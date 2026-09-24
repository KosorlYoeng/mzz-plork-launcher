const fs = require("fs");
const path = require("path");

// Whether MzzPlork Application Data represents a fresh install (no client
// runtime present yet) or an existing one. version.json alone is not
// trustworthy -- it could claim a version while the exe it describes was
// deleted -- so this checks the actual file too.
function determineInstallState(appDataRoot, version) {
  const clientExePath = path.join(appDataRoot, "runtime", "MzzPlorkClient.exe");
  const hasClientExe = fs.existsSync(clientExePath);
  const runtimeVersion = version && version.runtime && version.runtime.version;
  const hasRuntimeVersion = Boolean(runtimeVersion) && runtimeVersion !== "0.0.0";
  return hasClientExe && hasRuntimeVersion ? "existing" : "first-install";
}

module.exports = { determineInstallState };
