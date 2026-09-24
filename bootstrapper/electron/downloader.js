const fs = require("fs");
const path = require("path");
const http = require("http");
const https = require("https");
const crypto = require("crypto");

class CorruptedDownloadError extends Error {
  constructor(message, filePath) {
    super(message);
    this.name = "CorruptedDownloadError";
    this.filePath = filePath;
  }
}

function sha256File(filePath) {
  return new Promise((resolve, reject) => {
    const hash = crypto.createHash("sha256");
    const stream = fs.createReadStream(filePath);
    stream.on("data", (chunk) => hash.update(chunk));
    stream.on("end", () => resolve(hash.digest("hex")));
    stream.on("error", reject);
  });
}

function clientFor(url) {
  return url.startsWith("https:") ? https : http;
}

// Downloads `url` into `destPath`, resuming from an existing partial file
// via HTTP Range if the server honors it, and verifying the finished
// file's size + sha256 against `expected` ({size, sha256}). On a mismatch
// the bad file is deleted and CorruptedDownloadError is thrown so the
// caller can decide whether to retry.
//
// If the connection drops mid-transfer, whatever was received is flushed
// and closed before this rejects, so the partial file on disk is always
// consistent and safe to resume from on a later attempt.
async function downloadFile(url, destPath, expected, { maxRedirects = 3 } = {}) {
  await fs.promises.mkdir(path.dirname(destPath), { recursive: true });

  let startOffset = 0;
  try {
    const stat = await fs.promises.stat(destPath);
    if (stat.size > 0 && stat.size < expected.size) {
      startOffset = stat.size;
    } else if (stat.size >= expected.size) {
      await fs.promises.unlink(destPath);
    }
  } catch {
    // no partial file present, starting fresh
  }

  await new Promise((resolve, reject) => {
    let settled = false;
    const settle = (err) => {
      if (settled) return;
      settled = true;
      if (err) reject(err);
      else resolve();
    };

    const attempt = (attemptUrl, redirectsLeft, offset) => {
      const headers = offset > 0 ? { Range: `bytes=${offset}-` } : {};
      const client = clientFor(attemptUrl);
      const req = client.get(attemptUrl, { headers, timeout: 15000 }, (res) => {
        if ([301, 302, 307, 308].includes(res.statusCode) && res.headers.location && redirectsLeft > 0) {
          res.resume();
          attempt(res.headers.location, redirectsLeft - 1, offset);
          return;
        }

        const resumed = res.statusCode === 206;
        if (offset > 0 && !resumed) {
          res.resume();
          fs.promises
            .unlink(destPath)
            .catch(() => {})
            .then(() => attempt(attemptUrl, redirectsLeft, 0));
          return;
        }
        if (res.statusCode !== 200 && res.statusCode !== 206) {
          res.resume();
          settle(new Error(`download failed: HTTP ${res.statusCode} for ${attemptUrl}`));
          return;
        }

        const out = fs.createWriteStream(destPath, { flags: resumed ? "a" : "w" });
        res.pipe(out);
        out.on("finish", () => settle());
        out.on("error", (err) => settle(err));
        // On a dropped connection, close the write stream first so any
        // buffered bytes are flushed to disk before we reject -- otherwise
        // a later resume attempt could find a truncated/inconsistent file.
        res.on("error", (err) => out.end(() => settle(err)));
      });
      req.on("timeout", () => req.destroy(new Error(`download timed out for ${attemptUrl}`)));
      req.on("error", (err) => settle(err));
    };
    attempt(url, maxRedirects, startOffset);
  });

  const stat = await fs.promises.stat(destPath);
  if (stat.size !== expected.size) {
    await fs.promises.unlink(destPath).catch(() => {});
    throw new CorruptedDownloadError(`size mismatch: expected ${expected.size}, got ${stat.size}`, destPath);
  }
  const actualHash = await sha256File(destPath);
  if (actualHash !== expected.hash) {
    await fs.promises.unlink(destPath).catch(() => {});
    throw new CorruptedDownloadError(`hash mismatch: expected ${expected.hash}, got ${actualHash}`, destPath);
  }

  return destPath;
}

module.exports = { downloadFile, sha256File, CorruptedDownloadError };

