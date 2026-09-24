const test = require("node:test");
const assert = require("node:assert/strict");
const http = require("node:http");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const crypto = require("node:crypto");

const { downloadFile, CorruptedDownloadError } = require("../electron/downloader");

function tmpFile() {
  return path.join(fs.mkdtempSync(path.join(os.tmpdir(), "mzzplork-dl-")), "payload.bin");
}

function payloadOf(size, byte = 0x41) {
  return Buffer.alloc(size, byte);
}

function expectedOf(buffer) {
  return { size: buffer.length, hash: crypto.createHash("sha256").update(buffer).digest("hex") };
}

function listen(server) {
  return new Promise((resolve) => server.listen(0, "127.0.0.1", () => resolve(server.address().port)));
}

function close(server) {
  return new Promise((resolve) => server.close(resolve));
}

test("downloads a file and verifies it against the manifest hash", async () => {
  const payload = payloadOf(5000);
  const expected = expectedOf(payload);
  const server = http.createServer((req, res) => {
    res.writeHead(200, { "Content-Length": payload.length });
    res.end(payload);
  });
  const port = await listen(server);
  const dest = tmpFile();
  try {
    await downloadFile(`http://127.0.0.1:${port}/file`, dest, expected);
    assert.equal(fs.readFileSync(dest).length, payload.length);
  } finally {
    await close(server);
  }
});

test("rejects and deletes the file when the hash does not match (corrupted download)", async () => {
  const payload = payloadOf(2000);
  const wrongExpected = { size: payload.length, hash: "0".repeat(64) };
  const server = http.createServer((req, res) => {
    res.writeHead(200, { "Content-Length": payload.length });
    res.end(payload);
  });
  const port = await listen(server);
  const dest = tmpFile();
  try {
    await assert.rejects(() => downloadFile(`http://127.0.0.1:${port}/file`, dest, wrongExpected), CorruptedDownloadError);
    assert.equal(fs.existsSync(dest), false);
  } finally {
    await close(server);
  }
});

test("resumes an interrupted download when the server supports Range", async () => {
  const payload = payloadOf(20000, 0x42);
  const expected = expectedOf(payload);
  let requestCount = 0;

  const server = http.createServer((req, res) => {
    requestCount++;
    if (requestCount === 1) {
      // Simulate a connection drop after writing half the payload.
      res.writeHead(200, { "Content-Length": payload.length });
      res.write(payload.subarray(0, 10000), () => {
        req.socket.destroy();
      });
      return;
    }
    const range = req.headers.range;
    if (range) {
      const start = Number(range.match(/bytes=(\d+)-/)[1]);
      res.writeHead(206, {
        "Content-Range": `bytes ${start}-${payload.length - 1}/${payload.length}`,
        "Content-Length": payload.length - start
      });
      res.end(payload.subarray(start));
    } else {
      res.writeHead(200, { "Content-Length": payload.length });
      res.end(payload);
    }
  });
  const port = await listen(server);
  const dest = tmpFile();
  try {
    await assert.rejects(() => downloadFile(`http://127.0.0.1:${port}/file`, dest, expected));
    assert.ok(fs.existsSync(dest), "partial file should remain for resume");
    assert.ok(fs.statSync(dest).size > 0 && fs.statSync(dest).size < payload.length);

    // Retry: should resume from the partial file via Range and complete.
    await downloadFile(`http://127.0.0.1:${port}/file`, dest, expected);
    const final = fs.readFileSync(dest);
    assert.equal(final.length, payload.length);
    assert.deepEqual(final, payload);
    assert.equal(requestCount, 2);
  } finally {
    await close(server);
  }
});

test("restarts from scratch when the server ignores Range requests", async () => {
  const payload = payloadOf(3000, 0x43);
  const expected = expectedOf(payload);
  const server = http.createServer((req, res) => {
    // Always returns the full file, ignoring any Range header.
    res.writeHead(200, { "Content-Length": payload.length });
    res.end(payload);
  });
  const port = await listen(server);
  const dest = tmpFile();
  fs.writeFileSync(dest, payloadOf(1000, 0x99)); // stale/foreign partial content
  try {
    await downloadFile(`http://127.0.0.1:${port}/file`, dest, expected);
    const final = fs.readFileSync(dest);
    assert.equal(final.length, payload.length);
    assert.deepEqual(final, payload);
  } finally {
    await close(server);
  }
});

test("reports a clear error on HTTP failure (failed update source)", async () => {
  const server = http.createServer((req, res) => {
    res.writeHead(404);
    res.end();
  });
  const port = await listen(server);
  const dest = tmpFile();
  try {
    await assert.rejects(() => downloadFile(`http://127.0.0.1:${port}/missing`, dest, { size: 10, hash: "x" }), /HTTP 404/);
  } finally {
    await close(server);
  }
});


