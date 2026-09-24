const test = require("node:test");
const assert = require("node:assert/strict");
const { PassThrough } = require("stream");

const { startAdminConsole } = require("../src/adminConsole");

function fakeSession(id, playerId, authenticated) {
  return {
    id,
    playerId,
    authenticated,
    remoteAddress: "127.0.0.1",
    sent: [],
    send(type, payload) { this.sent.push({ type, payload }); },
    framer: { socket: { ended: false, end() { this.ended = true; } } }
  };
}

function makeHarness(sessions) {
  const input = new PassThrough();
  let output = "";
  const fakeOutput = { write: (chunk) => { output += chunk; } };
  const tcpServer = { sessions };
  const eventBus = { broadcast: (eventType, data) => eventBus.calls.push({ eventType, data }), calls: [] };
  const logger = { info: () => {} };
  let shutdownCalled = false;
  const rl = startAdminConsole({
    tcpServer, eventBus, logger,
    onShutdown: () => { shutdownCalled = true; },
    input, output: fakeOutput
  });
  return {
    sendLine: (line) => new Promise((resolve) => { input.write(line + "\n"); setImmediate(resolve); }),
    getOutput: () => output,
    eventBus,
    wasShutdownCalled: () => shutdownCalled,
    close: () => rl.close()
  };
}

test("adminConsole: list prints one line per session with id/player/auth/address", async () => {
  const sessions = new Map([
    ["s1", fakeSession("s1", "p1", true)],
    ["s2", fakeSession("s2", null, false)]
  ]);
  const h = makeHarness(sessions);

  await h.sendLine("list");

  const output = h.getOutput();
  assert.match(output, /s1\s+player=p1\s+auth=true\s+from=127\.0\.0\.1/);
  assert.match(output, /s2\s+player=-\s+auth=false\s+from=127\.0\.0\.1/);
  h.close();
});

test("adminConsole: kick <sessionId> sends a kick message and ends the socket", async () => {
  const target = fakeSession("s1", "p1", true);
  const sessions = new Map([["s1", target]]);
  const h = makeHarness(sessions);

  await h.sendLine("kick s1");

  assert.equal(target.sent.length, 1);
  assert.deepEqual(target.sent[0], { type: "kick", payload: { reason: "kicked by admin" } });
  assert.equal(target.framer.socket.ended, true);
  h.close();
});

test("adminConsole: kick <playerId> also resolves by player id, not just session id", async () => {
  const target = fakeSession("s1", "the-player-id", true);
  const sessions = new Map([["s1", target]]);
  const h = makeHarness(sessions);

  await h.sendLine("kick the-player-id");

  assert.equal(target.sent.length, 1);
  h.close();
});

test("adminConsole: kick with no matching session reports an error, not a crash", async () => {
  const sessions = new Map();
  const h = makeHarness(sessions);

  await h.sendLine("kick nobody");

  assert.match(h.getOutput(), /no session matching "nobody"/);
  h.close();
});

test("adminConsole: broadcast <message> pushes a server_message event with the joined text", async () => {
  const h = makeHarness(new Map());

  await h.sendLine("broadcast hello everyone");

  assert.equal(h.eventBus.calls.length, 1);
  assert.deepEqual(h.eventBus.calls[0], { eventType: "server_message", data: { message: "hello everyone" } });
  h.close();
});

test("adminConsole: shutdown invokes the onShutdown callback", async () => {
  const h = makeHarness(new Map());

  await h.sendLine("shutdown");

  assert.equal(h.wasShutdownCalled(), true);
  h.close();
});

test("adminConsole: an unknown command reports usage instead of doing nothing silently", async () => {
  const h = makeHarness(new Map());

  await h.sendLine("frobnicate");

  assert.match(h.getOutput(), /unknown command "frobnicate"/);
  h.close();
});

test("adminConsole: a blank line is a no-op (no error output)", async () => {
  const h = makeHarness(new Map());

  await h.sendLine("");

  assert.equal(h.getOutput(), "");
  h.close();
});
