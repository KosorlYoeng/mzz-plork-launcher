const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("fs");
const os = require("os");
const path = require("path");

const { Logger } = require("../src/log");

function tempLogPath() {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), "mzzplork-log-test-"));
  return path.join(dir, "nested", "server.log");
}

function readLog(logPath) {
  return fs.readFileSync(logPath, "utf8");
}

test("Logger: creates the log file's directory if missing (nested path)", async () => {
  const logPath = tempLogPath();
  assert.equal(fs.existsSync(path.dirname(logPath)), false);

  const logger = new Logger(logPath);
  logger.info("hello");
  logger.close();
  await new Promise((resolve) => setTimeout(resolve, 20));

  assert.equal(fs.existsSync(logPath), true);
});

test("Logger: writes level, timestamp, and message to the file", async () => {
  const logPath = tempLogPath();
  const logger = new Logger(logPath);
  logger.info("something happened");
  logger.close();
  await new Promise((resolve) => setTimeout(resolve, 20));

  const content = readLog(logPath);
  assert.match(content, /^\[.+\] INFO something happened/m);
});

test("Logger: debug/warn/error all write with their own level label", async () => {
  const logPath = tempLogPath();
  const logger = new Logger(logPath, "debug");
  logger.debug("d");
  logger.warn("w");
  logger.error("e");
  logger.close();
  await new Promise((resolve) => setTimeout(resolve, 20));

  const content = readLog(logPath);
  assert.match(content, /DEBUG d/);
  assert.match(content, /WARN w/);
  assert.match(content, /ERROR e/);
});

test("Logger: default level (info) filters out debug messages", async () => {
  const logPath = tempLogPath();
  const logger = new Logger(logPath); // default "info"
  logger.debug("should not appear");
  logger.info("should appear");
  logger.close();
  await new Promise((resolve) => setTimeout(resolve, 20));

  const content = readLog(logPath);
  assert.equal(content.includes("should not appear"), false);
  assert.match(content, /should appear/);
});

test("Logger: an explicit level filters out everything below it", async () => {
  const logPath = tempLogPath();
  const logger = new Logger(logPath, "error");
  logger.info("filtered");
  logger.warn("filtered too");
  logger.error("kept");
  logger.close();
  await new Promise((resolve) => setTimeout(resolve, 20));

  const content = readLog(logPath);
  assert.equal(content.includes("filtered"), false);
  assert.match(content, /ERROR kept/);
});

test("Logger: an unrecognized level string falls back to info", async () => {
  const logPath = tempLogPath();
  const logger = new Logger(logPath, "not-a-real-level");
  logger.debug("filtered (fell back to info)");
  logger.info("kept");
  logger.close();
  await new Promise((resolve) => setTimeout(resolve, 20));

  const content = readLog(logPath);
  assert.equal(content.includes("filtered (fell back to info)"), false);
  assert.match(content, /kept/);
});

test("Logger: appends across multiple instances rather than truncating", async () => {
  const logPath = tempLogPath();
  const first = new Logger(logPath);
  first.info("first line");
  first.close();
  await new Promise((resolve) => setTimeout(resolve, 20));

  const second = new Logger(logPath);
  second.info("second line");
  second.close();
  await new Promise((resolve) => setTimeout(resolve, 20));

  const content = readLog(logPath);
  assert.match(content, /first line/);
  assert.match(content, /second line/);
});
