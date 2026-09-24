"use strict";
const net = require("net");
const { MessageFramer, PROTOCOL_VERSION, isProtocolCompatible } = require("./protocol");
const { Session } = require("./session");
const { authenticate } = require("./auth");
const { ConnectionState } = require("./connectionState");

// The TCP control channel: connections, hello/version handshake, auth,
// heartbeat (with a timeout watchdog that disconnects silent clients),
// resource manifest requests, and graceful per-session and whole-server
// shutdown.
function createTcpServer({ config, logger, eventBus, getManifest, sessions = new Map() }) {
  let heartbeatWatchdog = null;

  function disconnectSession(session, reason) {
    if (!sessions.has(session.id)) return;
    if (!session.transitionTo(ConnectionState.DISCONNECTING)) {
      logger.warn(`session ${session.id} rejected transition to disconnecting from ${session.state}`);
    }
    logger.info(`session ${session.id} disconnecting: ${reason}`);
    try {
      session.send("kick", { reason });
    } catch {
      // socket may already be closing; best-effort notification only
    }
    session.framer.socket.end();
    sessions.delete(session.id);
    if (session.authenticated) {
      eventBus.broadcastExcept(session.id, "player_left", { playerId: session.playerId });
    }
    session.transitionTo(ConnectionState.DISCONNECTED);
    eventBus.emit("sessionDisconnected", session, reason);
  }

  const server = net.createServer((socket) => {
    const framer = new MessageFramer(socket);
    const session = new Session(framer, socket.remoteAddress);
    sessions.set(session.id, session);
    logger.info(`connection from ${session.remoteAddress}, session ${session.id}`);
    eventBus.emit("sessionConnected", session);

    framer.on("protocolError", (err) => {
      logger.warn(`session ${session.id} protocol error: ${err.message}`);
    });

    framer.on("message", (type, payload) => {
      switch (type) {
        case "hello": {
          session.helloReceived = true;
          session.clientProtocolVersion = payload.protocolVersion;
          const compatible = isProtocolCompatible(payload.protocolVersion);
          session.send("hello_ack", {
            ok: compatible,
            protocolVersion: PROTOCOL_VERSION,
            reason: compatible ? undefined : `protocol version ${payload.protocolVersion} is incompatible with server ${PROTOCOL_VERSION}`
          });
          if (compatible) {
            session.transitionTo(ConnectionState.AUTHENTICATING);
          } else {
            disconnectSession(session, "incompatible protocol version");
          }
          break;
        }
        case "auth": {
          if (!session.helloReceived) {
            disconnectSession(session, "auth before hello");
            return;
          }
          const result = authenticate(payload.clientId, payload.displayName);
          session.authenticated = result.ok;
          session.clientId = payload.clientId || null;
          session.playerId = result.playerId;
          session.displayName = result.displayName;
          session.send("auth_result", result);
          if (result.ok) {
            session.transitionTo(ConnectionState.AUTHENTICATED);
            // Tell the newcomer about everyone already present (docs/synchronization.md),
            // then tell everyone else the newcomer arrived.
            for (const other of sessions.values()) {
              if (other.id !== session.id && other.authenticated) {
                session.send("event", { eventType: "player_joined", data: { playerId: other.playerId, displayName: other.displayName } });
                if (other.lastPlayerState) {
                  session.send("event", { eventType: "player_state", data: { playerId: other.playerId, ...other.lastPlayerState } });
                }
              }
            }
            eventBus.broadcastExcept(session.id, "player_joined", { playerId: session.playerId, displayName: session.displayName });
            eventBus.emit("sessionAuthenticated", session);
          }
          break;
        }
        case "heartbeat": {
          session.touchHeartbeat();
          session.send("heartbeat_ack", { serverTimeMs: Date.now() });
          break;
        }
        case "resource_manifest_request": {
          if (!session.authenticated) {
            disconnectSession(session, "resource manifest requested before authentication");
            return;
          }
          session.send("resource_manifest", { ...getManifest(), httpPort: config.httpPort });
          break;
        }
        case "player_state": {
          if (!session.authenticated) {
            disconnectSession(session, "player_state before authentication");
            return;
          }
          session.lastPlayerState = { position: payload.position, rotation: payload.rotation, basicState: payload.basicState, timestamp: payload.timestamp };
          eventBus.broadcastExcept(session.id, "player_state", { playerId: session.playerId, ...session.lastPlayerState });
          break;
        }
        case "resources_ready": {
          if (!session.authenticated) {
            disconnectSession(session, "resources_ready before authentication");
            return;
          }
          session.resourcesReady = true;
          logger.info(`session ${session.id} resources ready: ${payload.loadedCount} loaded, ${payload.failedCount} failed`);
          session.send("resources_ready_ack", { ok: true });
          eventBus.broadcast("player_ready", { playerId: session.playerId, displayName: session.displayName });
          break;
        }
        case "disconnect": {
          disconnectSession(session, payload.reason || "client requested disconnect");
          break;
        }
        default:
          logger.warn(`session ${session.id} sent unknown message type "${type}"`);
      }
    });

    socket.on("close", () => {
      // Covers the raw-close path (crash, force-kill, network drop) that
      // never went through disconnectSession() above -- e.g. a client
      // that simply vanishes without sending "disconnect" and before the
      // heartbeat watchdog would have caught it. disconnectSession()
      // guards on `sessions.has(id)`, so calling it again here after an
      // already-graceful disconnect is a safe no-op, not a double-fire.
      disconnectSession(session, "connection closed");
    });
    socket.on("error", (err) => {
      logger.warn(`session ${session.id} socket error: ${err.message}`);
    });
  });

  function startHeartbeatWatchdog() {
    heartbeatWatchdog = setInterval(() => {
      for (const session of Array.from(sessions.values())) {
        if (session.isHeartbeatExpired(config.heartbeatTimeoutMs)) {
          disconnectSession(session, "heartbeat timeout");
        }
      }
    }, config.heartbeatIntervalMs);
  }

  function listen() {
    return new Promise((resolve) => {
      server.listen(config.tcpPort, () => {
        startHeartbeatWatchdog();
        resolve(server.address().port);
      });
    });
  }

  function shutdown() {
    return new Promise((resolve) => {
      clearInterval(heartbeatWatchdog);
      for (const session of Array.from(sessions.values())) {
        disconnectSession(session, "server shutting down");
      }
      server.close(() => resolve());
    });
  }

  return { server, sessions, listen, shutdown };
}

module.exports = { createTcpServer };





