const test = require("node:test");
const assert = require("node:assert/strict");

const { ConnectionState, isValidTransition } = require("../src/connectionState");

test("isValidTransition: the real session lifecycle path is all legal", () => {
  assert.equal(isValidTransition(ConnectionState.CONNECTING, ConnectionState.AUTHENTICATING), true);
  assert.equal(isValidTransition(ConnectionState.AUTHENTICATING, ConnectionState.AUTHENTICATED), true);
  assert.equal(isValidTransition(ConnectionState.AUTHENTICATED, ConnectionState.DISCONNECTING), true);
  assert.equal(isValidTransition(ConnectionState.DISCONNECTING, ConnectionState.DISCONNECTED), true);
});

test("isValidTransition: disconnecting is reachable from every non-terminal state", () => {
  assert.equal(isValidTransition(ConnectionState.CONNECTING, ConnectionState.DISCONNECTING), true);
  assert.equal(isValidTransition(ConnectionState.AUTHENTICATING, ConnectionState.DISCONNECTING), true);
  assert.equal(isValidTransition(ConnectionState.AUTHENTICATED, ConnectionState.DISCONNECTING), true);
});

test("isValidTransition: cannot skip straight to authenticated without authenticating", () => {
  assert.equal(isValidTransition(ConnectionState.CONNECTING, ConnectionState.AUTHENTICATED), false);
});

test("isValidTransition: cannot go backwards", () => {
  assert.equal(isValidTransition(ConnectionState.AUTHENTICATED, ConnectionState.AUTHENTICATING), false);
  assert.equal(isValidTransition(ConnectionState.AUTHENTICATING, ConnectionState.CONNECTING), false);
  assert.equal(isValidTransition(ConnectionState.DISCONNECTING, ConnectionState.AUTHENTICATED), false);
});

test("isValidTransition: disconnected is terminal", () => {
  assert.equal(isValidTransition(ConnectionState.DISCONNECTED, ConnectionState.CONNECTING), false);
  assert.equal(isValidTransition(ConnectionState.DISCONNECTED, ConnectionState.DISCONNECTING), false);
  assert.equal(isValidTransition(ConnectionState.DISCONNECTED, ConnectionState.DISCONNECTED), false);
});

test("isValidTransition: a state never transitions to itself", () => {
  for (const state of Object.values(ConnectionState)) {
    assert.equal(isValidTransition(state, state), false);
  }
});

test("ConnectionState values are frozen, human-readable strings", () => {
  ConnectionState.CONNECTING = "nope"; // frozen -- silently ignored outside strict mode, throws inside it
  assert.equal(ConnectionState.CONNECTING, "connecting");
  assert.equal(ConnectionState.DISCONNECTED, "disconnected");
});
