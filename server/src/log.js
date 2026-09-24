"use strict";
const fs = require("fs");
const path = require("path");

// Simple leveled logger writing to both console and a log file --
// mirrors the shape of runtime/src/Log.h/.cpp on the client side, kept
// deliberately separate rather than shared, since this is a different
// language/runtime.
const LEVELS = { debug: 0, info: 1, warn: 2, error: 3 };

class Logger {
  constructor(logFilePath, level = "info") {
    this.level = LEVELS[level] ?? LEVELS.info;
    fs.mkdirSync(path.dirname(logFilePath), { recursive: true });
    this.stream = fs.createWriteStream(logFilePath, { flags: "a" });
  }

  _write(levelName, message) {
    if (LEVELS[levelName] < this.level) return;
    const line = `[${new Date().toISOString()}] ${levelName.toUpperCase()} ${message}`;
    console.log(line);
    this.stream.write(line + "\n");
  }

  debug(message) { this._write("debug", message); }
  info(message) { this._write("info", message); }
  warn(message) { this._write("warn", message); }
  error(message) { this._write("error", message); }

  close() {
    this.stream.end();
  }
}

module.exports = { Logger };
