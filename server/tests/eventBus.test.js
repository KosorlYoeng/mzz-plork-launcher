const test = require("node:test");
const assert = require("node:assert/strict");

const { EventBus } = require("../src/eventBus");

function fakeSession(id, authenticated) {
  const sent = [];
  return { id, authenticated, sent, send: (type, payload) => sent.push({ type, payload }) };
}

test("EventBus: broadcast() sends to every authenticated session", () => {
  const a = fakeSession("a", true);
  const b = fakeSession("b", true);
  const sessions = new Map([["a", a], ["b", b]]);
  const bus = new EventBus(sessions);

  bus.broadcast("player_ready", { playerId: "a" });

  assert.equal(a.sent.length, 1);
  assert.deepEqual(a.sent[0], { type: "event", payload: { eventType: "player_ready", data: { playerId: "a" } } });
  assert.equal(b.sent.length, 1);
});

test("EventBus: broadcast() skips sessions that are not yet authenticated", () => {
  const a = fakeSession("a", true);
  const b = fakeSession("b", false);
  const sessions = new Map([["a", a], ["b", b]]);
  const bus = new EventBus(sessions);

  bus.broadcast("player_ready", { playerId: "a" });

  assert.equal(a.sent.length, 1);
  assert.equal(b.sent.length, 0);
});

test("EventBus: broadcastExcept() sends to every authenticated session except the given id", () => {
  const a = fakeSession("a", true);
  const b = fakeSession("b", true);
  const c = fakeSession("c", true);
  const sessions = new Map([["a", a], ["b", b], ["c", c]]);
  const bus = new EventBus(sessions);

  bus.broadcastExcept("b", "player_state", { playerId: "b", position: { x: 1, y: 2, z: 3 } });

  assert.equal(a.sent.length, 1);
  assert.equal(b.sent.length, 0);
  assert.equal(c.sent.length, 1);
});

test("EventBus: broadcastExcept() still skips unauthenticated sessions regardless of id", () => {
  const a = fakeSession("a", true);
  const b = fakeSession("b", false);
  const sessions = new Map([["a", a], ["b", b]]);
  const bus = new EventBus(sessions);

  bus.broadcastExcept("a", "player_state", {});

  assert.equal(a.sent.length, 0); // excluded
  assert.equal(b.sent.length, 0); // not authenticated
});

test("EventBus: is a real EventEmitter for internal pub/sub, independent of broadcast()", () => {
  const bus = new EventBus(new Map());
  let received = null;
  bus.on("sessionConnected", (session) => { received = session; });

  bus.emit("sessionConnected", { id: "x" });

  assert.deepEqual(received, { id: "x" });
});

test("EventBus: broadcast() with no sessions does not throw", () => {
  const bus = new EventBus(new Map());
  assert.doesNotThrow(() => bus.broadcast("player_ready", {}));
});
