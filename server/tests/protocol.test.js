const test = require("node:test");
const assert = require("node:assert/strict");
const net = require("node:net");

const { MessageFramer, PROTOCOL_VERSION, isProtocolCompatible, encodeMessage } = require("../src/protocol");

test("isProtocolCompatible: same major version is compatible", () => {
  assert.equal(isProtocolCompatible(PROTOCOL_VERSION), true);
  assert.equal(isProtocolCompatible("1.9"), true);
});

test("isProtocolCompatible: different major version is not compatible", () => {
  assert.equal(isProtocolCompatible("2.0"), false);
  assert.equal(isProtocolCompatible("0.1"), false);
});


test("MessageFramer: parses whole and split-across-chunks messages over a real loopback socket", async () => {
  const server = net.createServer();
  await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
  const port = server.address().port;

  const received = [];
  const serverConnectionPromise = new Promise((resolve) => {
    server.once("connection", (socket) => {
      const framer = new MessageFramer(socket);
      framer.on("message", (type, payload) => received.push({ type, payload }));
      resolve(framer);
    });
  });

  const client = net.connect(port, "127.0.0.1");
  await new Promise((resolve) => client.once("connect", resolve));
  await serverConnectionPromise;

  // Write one full message, then a message split across two writes.
  client.write(encodeMessage("hello", { protocolVersion: "1.0" }));
  const second = encodeMessage("heartbeat", { n: 1 });
  client.write(second.slice(0, 5));
  await new Promise((resolve) => setTimeout(resolve, 20));
  client.write(second.slice(5));

  await new Promise((resolve) => setTimeout(resolve, 50));

  assert.equal(received.length, 2);
  assert.equal(received[0].type, "hello");
  assert.equal(received[1].type, "heartbeat");

  client.end();
  server.close();
});

test("MessageFramer: malformed JSON emits protocolError instead of throwing", async () => {
  const server = net.createServer();
  await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
  const port = server.address().port;

  const errors = [];
  server.once("connection", (socket) => {
    const framer = new MessageFramer(socket);
    framer.on("protocolError", (err) => errors.push(err));
  });

  const client = net.connect(port, "127.0.0.1");
  await new Promise((resolve) => client.once("connect", resolve));
  client.write("{not valid json}\n");
  await new Promise((resolve) => setTimeout(resolve, 50));

  assert.equal(errors.length, 1);

  client.end();
  server.close();
});

