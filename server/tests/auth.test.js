const test = require("node:test");
const assert = require("node:assert/strict");

const { authenticate } = require("../src/auth");

test("authenticate: always succeeds -- this is session identity, not a real account system (see docs/decisions.md)", () => {
  const result = authenticate("anything", "Anyone");
  assert.equal(result.ok, true);
});

test("authenticate: reuses a UUID-shaped clientId as the playerId", () => {
  const clientId = "550e8400-e29b-41d4-a716-446655440000";
  const result = authenticate(clientId, "Alice");
  assert.equal(result.playerId, clientId);
});

test("authenticate: a non-UUID clientId gets a freshly generated playerId, not reused verbatim", () => {
  const result = authenticate("not-a-uuid", "Alice");
  assert.notEqual(result.playerId, "not-a-uuid");
  assert.match(result.playerId, /^[0-9a-f-]{36}$/i);
});

test("authenticate: a missing/empty clientId gets a freshly generated playerId", () => {
  const result = authenticate(undefined, "Alice");
  assert.match(result.playerId, /^[0-9a-f-]{36}$/i);

  const result2 = authenticate("", "Alice");
  assert.match(result2.playerId, /^[0-9a-f-]{36}$/i);
});

test("authenticate: two calls with no clientId get two different playerIds", () => {
  const a = authenticate(undefined, "Alice");
  const b = authenticate(undefined, "Bob");
  assert.notEqual(a.playerId, b.playerId);
});

test("authenticate: every call gets a fresh, distinct sessionId even for the same clientId", () => {
  const clientId = "550e8400-e29b-41d4-a716-446655440000";
  const a = authenticate(clientId, "Alice");
  const b = authenticate(clientId, "Alice");
  assert.notEqual(a.sessionId, b.sessionId);
});

test("authenticate: an explicit displayName is used as-is", () => {
  const result = authenticate(undefined, "Alice");
  assert.equal(result.displayName, "Alice");
});

test("authenticate: a missing displayName defaults to Player-<first 8 chars of playerId>", () => {
  const result = authenticate(undefined, undefined);
  assert.equal(result.displayName, `Player-${result.playerId.slice(0, 8)}`);

  const result2 = authenticate(undefined, "");
  assert.equal(result2.displayName, `Player-${result2.playerId.slice(0, 8)}`);
});
