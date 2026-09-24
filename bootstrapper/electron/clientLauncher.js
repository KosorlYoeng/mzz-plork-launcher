const { spawn } = require("child_process");
const fs = require("fs");
const path = require("path");
const readline = require("readline");

function clientExecutablePath(appDataRoot) {
  return path.join(appDataRoot, "runtime", "MzzPlorkClient.exe");
}

// Spawns MzzPlorkClient.exe and relays its lifecycle to onEvent(). In
// addition to the client's own stdout JSONL events (per
// runtime/src/ClientRuntimeInterface.h), this also reports:
//   {event:"error", message:"failed to start client process: ..."}  -- spawn itself failed (e.g. exe missing permissions)
//   {event:"exit", code, signal, crashed}                            -- crashed=true when the process exited non-cleanly
function launchClient(appDataRoot, onEvent) {
  const exePath = clientExecutablePath(appDataRoot);
  if (!fs.existsSync(exePath)) {
    onEvent({ event: "error", message: "client runtime not installed", path: exePath });
    return null;
  }

  const child = spawn(exePath, ["--appdata", appDataRoot], { windowsHide: true });

  child.on("error", (err) => {
    onEvent({ event: "error", message: `failed to start client process: ${err.message}` });
  });

  const rl = readline.createInterface({ input: child.stdout });
  rl.on("line", (line) => {
    try {
      onEvent(JSON.parse(line));
    } catch (err) {
      onEvent({ event: "log", message: line });
    }
  });

  child.stderr.on("data", (chunk) => {
    onEvent({ event: "error", message: chunk.toString() });
  });

  child.on("exit", (code, signal) => {
    onEvent({ event: "exit", code, signal, crashed: code !== 0 && code !== null });
  });

  return child;
}

module.exports = { clientExecutablePath, launchClient };
