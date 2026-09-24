"use strict";
const http = require("http");
const fs = require("fs");
const { resolveResourceFile } = require("./resourceManifest");

// Serves the actual resource file bytes over plain HTTP, separate from
// the TCP control channel -- same split as the bootstrapper's own
// manifest-over-one-channel / files-over-HTTP approach.
function createHttpResourceServer(resourcesDir, logger) {
  return http.createServer((req, res) => {
    const id = decodeURIComponent(req.url.replace(/^\/resources\//, ""));
    const filePath = resolveResourceFile(resourcesDir, id);
    if (!req.url.startsWith("/resources/") || !filePath) {
      res.writeHead(404);
      res.end();
      return;
    }
    const stat = fs.statSync(filePath);
    res.writeHead(200, { "Content-Length": stat.size });
    fs.createReadStream(filePath).pipe(res);
    logger.debug(`served resource "${id}" (${stat.size} bytes) to ${req.socket.remoteAddress}`);
  });
}

module.exports = { createHttpResourceServer };
