"use strict";
const path = require("path");
const { Logger } = require("./log");
const { computeManifest } = require("./resourceManifest");
const { createTcpServer } = require("./tcpServer");
const { createHttpResourceServer } = require("./httpResourceServer");
const { EventBus } = require("./eventBus");
const { startAdminConsole } = require("./adminConsole");

// Wires the whole server together. Exported (not just run as a script)
// so tests can start/stop a real server instance in-process against
// ephemeral ports.
async function startServer(configOverrides = {}) {
  const config = { ...require("../config.json"), ...configOverrides };
  const resourcesDir = path.isAbsolute(config.resourcesDir)
    ? config.resourcesDir
    : path.join(__dirname, "..", config.resourcesDir);

  const logger = new Logger(config.logFilePath || path.join(__dirname, "..", "logs", "server.log"));

  let currentManifest = computeManifest(resourcesDir);
  const sessions = new Map();
  const eventBus = new EventBus(sessions);
  const tcpServer = createTcpServer({
    config,
    logger,
    eventBus,
    sessions,
    getManifest: () => currentManifest
  });

  eventBus.on("sessionConnected", (session) => logger.info(`session connected: ${session.id}`));
  eventBus.on("sessionAuthenticated", (session) => logger.info(`session authenticated: ${session.id} as player ${session.playerId}`));
  eventBus.on("sessionDisconnected", (session, reason) => logger.info(`session disconnected: ${session.id} (${reason})`));

  const httpServer = createHttpResourceServer(resourcesDir, logger);

  const tcpPort = await tcpServer.listen();
  await new Promise((resolve) => httpServer.listen(config.httpPort, resolve));
  const httpPort = httpServer.address().port;
  // Reflect the real bound ports back into config -- callers may have
  // requested an ephemeral port (0), and anything reading config.tcpPort
  // / config.httpPort afterwards (e.g. the resource_manifest response)
  // needs the port that was actually assigned, not what was requested.
  config.tcpPort = tcpPort;
  config.httpPort = httpPort;

  logger.info(`MzzPlork Server listening: tcp=${tcpPort} http=${httpPort} protocol=${config.protocolVersion}`);

  let shuttingDown = false;
  async function shutdown() {
    if (shuttingDown) return;
    shuttingDown = true;
    logger.info("shutting down");
    await tcpServer.shutdown();
    await new Promise((resolve) => httpServer.close(resolve));
    logger.close();
  }

  return { config, tcpServer, httpServer, eventBus, logger, tcpPort, httpPort, shutdown, refreshManifest: () => { currentManifest = computeManifest(resourcesDir); } };
}

if (require.main === module) {
  startServer().then((instance) => {
    startAdminConsole({
      tcpServer: instance.tcpServer,
      eventBus: instance.eventBus,
      logger: instance.logger,
      onShutdown: async () => {
        await instance.shutdown();
        process.exit(0);
      }
    });
    process.on("SIGINT", async () => {
      await instance.shutdown();
      process.exit(0);
    });
    process.on("SIGTERM", async () => {
      await instance.shutdown();
      process.exit(0);
    });
  });
}

module.exports = { startServer };


