const test = require("node:test");
const assert = require("node:assert/strict");
const net = require("node:net");
const http = require("node:http");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const crypto = require("node:crypto");

const { startServer } = require("../src/main");
const { PROTOCOL_VERSION, encodeMessage } = require("../src/protocol");

function tmpResourcesDir() {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "mzzplork-server-it-"));
  fs.writeFileSync(path.join(dir, "test.dat"), "integration-test-payload");
  return dir;
}

function connectRawClient(port) {
  return new Promise((resolve) => {
    const socket = net.connect(port, "127.0.0.1", () => resolve(socket));
  });
}

function collectMessages(socket) {
  const messages = [];
  let buffer = "";
  socket.setEncoding("utf8");
  socket.on("data", (chunk) => {
    buffer += chunk;
    let idx;
    while ((idx = buffer.indexOf("\n")) !== -1) {
      const line = buffer.slice(0, idx);
      buffer = buffer.slice(idx + 1);
      if (line.trim()) messages.push(JSON.parse(line));
    }
  });
  return messages;
}

function waitFor(fn, timeoutMs = 2000) {
  return new Promise((resolve, reject) => {
    const start = Date.now();
    const tick = () => {
      if (fn()) return resolve();
      if (Date.now() - start > timeoutMs) return reject(new Error("waitFor timed out"));
      setTimeout(tick, 10);
    };
    tick();
  });
}

function httpGet(port, urlPath) {
  return new Promise((resolve, reject) => {
    http.get({ host: "127.0.0.1", port, path: urlPath }, (res) => {
      const chunks = [];
      res.on("data", (c) => chunks.push(c));
      res.on("end", () => resolve({ statusCode: res.statusCode, body: Buffer.concat(chunks) }));
    }).on("error", reject);
  });
}

test("full handshake: hello -> auth -> heartbeat -> resource manifest -> HTTP download -> verify", async () => {
  const resourcesDir = tmpResourcesDir();
  const instance = await startServer({ tcpPort: 0, httpPort: 0, resourcesDir, logFilePath: path.join(resourcesDir, "server.log") });
  try {
    const socket = await connectRawClient(instance.tcpPort);
    const messages = collectMessages(socket);

    socket.write(encodeMessage("hello", { protocolVersion: PROTOCOL_VERSION }));
    await waitFor(() => messages.some((m) => m.type === "hello_ack"));
    const helloAck = messages.find((m) => m.type === "hello_ack");
    assert.equal(helloAck.payload.ok, true);

    socket.write(encodeMessage("auth", { clientId: "test-client-1", displayName: "Tester" }));
    await waitFor(() => messages.some((m) => m.type === "auth_result"));
    const authResult = messages.find((m) => m.type === "auth_result");
    assert.equal(authResult.payload.ok, true);
    assert.ok(authResult.payload.playerId);

    socket.write(encodeMessage("heartbeat", {}));
    await waitFor(() => messages.some((m) => m.type === "heartbeat_ack"));

    socket.write(encodeMessage("resource_manifest_request", {}));
    await waitFor(() => messages.some((m) => m.type === "resource_manifest"));
    const manifestMsg = messages.find((m) => m.type === "resource_manifest");
    assert.equal(manifestMsg.payload.resources.length, 1);
    assert.equal(manifestMsg.payload.httpPort, instance.httpPort);
    const entry = manifestMsg.payload.resources[0];
    assert.equal(entry.id, "test.dat");

    const download = await httpGet(instance.httpPort, `/resources/${entry.id}`);
    assert.equal(download.statusCode, 200);
    assert.equal(download.body.length, entry.size);
    const actualHash = crypto.createHash("sha256").update(download.body).digest("hex");
    assert.equal(actualHash, entry.hash);

    socket.write(encodeMessage("disconnect", { reason: "test complete" }));
    await waitFor(() => messages.some((m) => m.type === "kick"));
  } finally {
    await instance.shutdown();
  }
});

test("incompatible protocol version is rejected and the connection is closed", async () => {
  const resourcesDir = tmpResourcesDir();
  const instance = await startServer({ tcpPort: 0, httpPort: 0, resourcesDir, logFilePath: path.join(resourcesDir, "server.log") });
  try {
    const socket = await connectRawClient(instance.tcpPort);
    const messages = collectMessages(socket);
    let closed = false;
    socket.on("close", () => { closed = true; });

    socket.write(encodeMessage("hello", { protocolVersion: "99.0" }));
    await waitFor(() => messages.some((m) => m.type === "hello_ack"));
    assert.equal(messages[0].payload.ok, false);
    await waitFor(() => closed);
  } finally {
    await instance.shutdown();
  }
});

test("resource manifest request before authentication is refused", async () => {
  const resourcesDir = tmpResourcesDir();
  const instance = await startServer({ tcpPort: 0, httpPort: 0, resourcesDir, logFilePath: path.join(resourcesDir, "server.log") });
  try {
    const socket = await connectRawClient(instance.tcpPort);
    const messages = collectMessages(socket);
    let closed = false;
    socket.on("close", () => { closed = true; });

    socket.write(encodeMessage("hello", { protocolVersion: PROTOCOL_VERSION }));
    await waitFor(() => messages.some((m) => m.type === "hello_ack"));
    socket.write(encodeMessage("resource_manifest_request", {}));
    await waitFor(() => closed);

    assert.equal(messages.some((m) => m.type === "resource_manifest"), false);
  } finally {
    await instance.shutdown();
  }
});

test("a silent client is disconnected once the heartbeat timeout elapses", async () => {
  const resourcesDir = tmpResourcesDir();
  const instance = await startServer({
    tcpPort: 0,
    httpPort: 0,
    resourcesDir,
    logFilePath: path.join(resourcesDir, "server.log"),
    heartbeatIntervalMs: 50,
    heartbeatTimeoutMs: 120
  });
  try {
    const socket = await connectRawClient(instance.tcpPort);
    const messages = collectMessages(socket);
    socket.write(encodeMessage("hello", { protocolVersion: PROTOCOL_VERSION }));
    // Deliberately never sends a heartbeat -- the watchdog should kick it.
    await waitFor(() => messages.some((m) => m.type === "kick"), 3000);
    const kick = messages.find((m) => m.type === "kick");
    assert.equal(kick.payload.reason, "heartbeat timeout");
  } finally {
    await instance.shutdown();
  }
});




test("resources_ready is acknowledged and broadcast to other authenticated sessions", async () => {
  const resourcesDir = tmpResourcesDir();
  const instance = await startServer({ tcpPort: 0, httpPort: 0, resourcesDir, logFilePath: path.join(resourcesDir, "server.log") });
  try {
    const socketA = await connectRawClient(instance.tcpPort);
    const messagesA = collectMessages(socketA);
    socketA.write(encodeMessage("hello", { protocolVersion: PROTOCOL_VERSION }));
    await waitFor(() => messagesA.some((m) => m.type === "hello_ack"));
    socketA.write(encodeMessage("auth", { clientId: "client-a", displayName: "A" }));
    await waitFor(() => messagesA.some((m) => m.type === "auth_result"));

    const socketB = await connectRawClient(instance.tcpPort);
    const messagesB = collectMessages(socketB);
    socketB.write(encodeMessage("hello", { protocolVersion: PROTOCOL_VERSION }));
    await waitFor(() => messagesB.some((m) => m.type === "hello_ack"));
    socketB.write(encodeMessage("auth", { clientId: "client-b", displayName: "B" }));
    await waitFor(() => messagesB.some((m) => m.type === "auth_result"));

    socketA.write(encodeMessage("resources_ready", { loadedCount: 3, failedCount: 0 }));
    await waitFor(() => messagesA.some((m) => m.type === "resources_ready_ack"));
    const ack = messagesA.find((m) => m.type === "resources_ready_ack");
    assert.equal(ack.payload.ok, true);

    // B should observe A's readiness as a broadcast event.
    await waitFor(() => messagesB.some((m) => m.type === "event" && m.payload.eventType === "player_ready"));
    const event = messagesB.find((m) => m.type === "event" && m.payload.eventType === "player_ready");
    assert.ok(event.payload.data.playerId);

  } finally {
    await instance.shutdown();
  }
});

test("resources_ready before authentication is refused", async () => {
  const resourcesDir = tmpResourcesDir();
  const instance = await startServer({ tcpPort: 0, httpPort: 0, resourcesDir, logFilePath: path.join(resourcesDir, "server.log") });
  try {
    const socket = await connectRawClient(instance.tcpPort);
    const messages = collectMessages(socket);
    let closed = false;
    socket.on("close", () => { closed = true; });

    socket.write(encodeMessage("hello", { protocolVersion: PROTOCOL_VERSION }));
    await waitFor(() => messages.some((m) => m.type === "hello_ack"));
    socket.write(encodeMessage("resources_ready", { loadedCount: 0, failedCount: 0 }));
    await waitFor(() => closed);

    assert.equal(messages.some((m) => m.type === "resources_ready_ack"), false);
  } finally {
    await instance.shutdown();
  }
});




// --- Player presence / state relay (docs/synchronization.md) ---

async function authenticatedClient(port, clientId, displayName) {
  const socket = await connectRawClient(port);
  const messages = collectMessages(socket);
  socket.write(encodeMessage("hello", { protocolVersion: PROTOCOL_VERSION }));
  await waitFor(() => messages.some((m) => m.type === "hello_ack"));
  socket.write(encodeMessage("auth", { clientId, displayName }));
  await waitFor(() => messages.some((m) => m.type === "auth_result"));
  const playerId = messages.find((m) => m.type === "auth_result").payload.playerId;
  return { socket, messages, playerId };
}

function findEvent(messages, eventType) {
  return messages.find((m) => m.type === "event" && m.payload.eventType === eventType);
}

test("a newly authenticated client learns about already-connected players", async () => {
  const resourcesDir = tmpResourcesDir();
  const instance = await startServer({ tcpPort: 0, httpPort: 0, resourcesDir, logFilePath: path.join(resourcesDir, "server.log") });
  try {
    const a = await authenticatedClient(instance.tcpPort, "client-a", "Alice");
    const b = await authenticatedClient(instance.tcpPort, "client-b", "Bob");

    // B (joined second) should have been told about A.
    await waitFor(() => findEvent(b.messages, "player_joined") !== undefined);
    assert.equal(findEvent(b.messages, "player_joined").payload.data.playerId, a.playerId);

    // A (joined first) should have been told B arrived, via the broadcast.
    await waitFor(() => findEvent(a.messages, "player_joined") !== undefined);
    assert.equal(findEvent(a.messages, "player_joined").payload.data.playerId, b.playerId);
  } finally {
    await instance.shutdown();
  }
});

test("player_state is relayed to other sessions but not echoed back to the sender", async () => {
  const resourcesDir = tmpResourcesDir();
  const instance = await startServer({ tcpPort: 0, httpPort: 0, resourcesDir, logFilePath: path.join(resourcesDir, "server.log") });
  try {
    const a = await authenticatedClient(instance.tcpPort, "client-a", "Alice");
    const b = await authenticatedClient(instance.tcpPort, "client-b", "Bob");

    const state = { position: { x: 1, y: 2, z: 3 }, rotation: { pitch: 0, yaw: 90, roll: 0 }, basicState: {}, timestamp: Date.now() };
    a.socket.write(encodeMessage("player_state", state));

    await waitFor(() => findEvent(b.messages, "player_state") !== undefined);
    const received = findEvent(b.messages, "player_state");
    assert.equal(received.payload.data.playerId, a.playerId);
    assert.deepEqual(received.payload.data.position, state.position);

    await new Promise((resolve) => setTimeout(resolve, 50));
    assert.equal(findEvent(a.messages, "player_state"), undefined);  // never echoed to the sender
  } finally {
    await instance.shutdown();
  }
});

test("a newly joined client is told the current state of already-moving players", async () => {
  const resourcesDir = tmpResourcesDir();
  const instance = await startServer({ tcpPort: 0, httpPort: 0, resourcesDir, logFilePath: path.join(resourcesDir, "server.log") });
  try {
    const a = await authenticatedClient(instance.tcpPort, "client-a", "Alice");
    const state = { position: { x: 5, y: 5, z: 5 }, rotation: { pitch: 0, yaw: 0, roll: 0 }, basicState: {}, timestamp: Date.now() };
    a.socket.write(encodeMessage("player_state", state));
    await new Promise((resolve) => setTimeout(resolve, 50));  // let the server record it

    const b = await authenticatedClient(instance.tcpPort, "client-b", "Bob");
    await waitFor(() => findEvent(b.messages, "player_state") !== undefined);
    assert.equal(findEvent(b.messages, "player_state").payload.data.playerId, a.playerId);
  } finally {
    await instance.shutdown();
  }
});

test("a graceful disconnect notifies other sessions via player_left", async () => {
  const resourcesDir = tmpResourcesDir();
  const instance = await startServer({ tcpPort: 0, httpPort: 0, resourcesDir, logFilePath: path.join(resourcesDir, "server.log") });
  try {
    const a = await authenticatedClient(instance.tcpPort, "client-a", "Alice");
    const b = await authenticatedClient(instance.tcpPort, "client-b", "Bob");

    a.socket.write(encodeMessage("disconnect", { reason: "test" }));
    await waitFor(() => findEvent(b.messages, "player_left") !== undefined);
    assert.equal(findEvent(b.messages, "player_left").payload.data.playerId, a.playerId);
  } finally {
    await instance.shutdown();
  }
});

test("an abrupt connection loss (no disconnect message) still notifies other sessions via player_left", async () => {
  const resourcesDir = tmpResourcesDir();
  const instance = await startServer({ tcpPort: 0, httpPort: 0, resourcesDir, logFilePath: path.join(resourcesDir, "server.log") });
  try {
    const a = await authenticatedClient(instance.tcpPort, "client-a", "Alice");
    const b = await authenticatedClient(instance.tcpPort, "client-b", "Bob");

    a.socket.destroy();  // simulates a crash/force-kill -- no "disconnect" message ever sent
    await waitFor(() => findEvent(b.messages, "player_left") !== undefined);
    assert.equal(findEvent(b.messages, "player_left").payload.data.playerId, a.playerId);
  } finally {
    await instance.shutdown();
  }
});
