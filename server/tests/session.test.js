const test = require("node:test");
const assert = require("node:assert/strict");

const { Session } = require("../src/session");
const { ConnectionState } = require("../src/connectionState");

function fakeFramer() {
  const sent = [];
  return { sent, send: (type, payload) => sent.push({ type, payload }) };
}

test("Session: constructor sets sane defaults and starts Connecting", () => {
  const framer = fakeFramer();
  const session = new Session(framer, "127.0.0.1");

  assert.equal(typeof session.id, "string");
  assert.equal(session.remoteAddress, "127.0.0.1");
  assert.equal(session.state, ConnectionState.CONNECTING);
  assert.equal(session.helloReceived, false);
  assert.equal(session.authenticated, false);
  assert.equal(session.playerId, null);
  assert.equal(session.resourcesReady, false);
  assert.equal(session.lastPlayerState, null);
});

test("Session: send() delegates to the framer", () => {
  const framer = fakeFramer();
  const session = new Session(framer, "127.0.0.1");

  session.send("heartbeat_ack", { serverTimeMs: 123 });

  assert.equal(framer.sent.length, 1);
  assert.deepEqual(framer.sent[0], { type: "heartbeat_ack", payload: { serverTimeMs: 123 } });
});

test("Session: touchHeartbeat resets the expiry clock", async () => {
  const session = new Session(fakeFramer(), "127.0.0.1");
  session.lastHeartbeatAt = Date.now() - 10_000;

  assert.equal(session.isHeartbeatExpired(5_000), true);
  session.touchHeartbeat();
  assert.equal(session.isHeartbeatExpired(5_000), false);
});

test("Session: isHeartbeatExpired compares against the configured timeout", () => {
  const session = new Session(fakeFramer(), "127.0.0.1");
  session.lastHeartbeatAt = Date.now() - 100;

  assert.equal(session.isHeartbeatExpired(50), true);
  assert.equal(session.isHeartbeatExpired(500), false);
});

test("Session: transitionTo applies a legal transition and returns true", () => {
  const session = new Session(fakeFramer(), "127.0.0.1");

  const applied = session.transitionTo(ConnectionState.AUTHENTICATING);

  assert.equal(applied, true);
  assert.equal(session.state, ConnectionState.AUTHENTICATING);
});

test("Session: transitionTo rejects an illegal jump and leaves state unchanged", () => {
  const session = new Session(fakeFramer(), "127.0.0.1");

  const applied = session.transitionTo(ConnectionState.AUTHENTICATED);

  assert.equal(applied, false);
  assert.equal(session.state, ConnectionState.CONNECTING);
});

test("Session: the full real lifecycle (connecting -> authenticating -> authenticated -> disconnecting -> disconnected) applies in order", () => {
  const session = new Session(fakeFramer(), "127.0.0.1");

  assert.equal(session.transitionTo(ConnectionState.AUTHENTICATING), true);
  assert.equal(session.transitionTo(ConnectionState.AUTHENTICATED), true);
  assert.equal(session.transitionTo(ConnectionState.DISCONNECTING), true);
  assert.equal(session.transitionTo(ConnectionState.DISCONNECTED), true);
  assert.equal(session.state, ConnectionState.DISCONNECTED);

  // Terminal: nothing transitions out of it, including trying to disconnect again.
  assert.equal(session.transitionTo(ConnectionState.DISCONNECTING), false);
  assert.equal(session.state, ConnectionState.DISCONNECTED);
});
