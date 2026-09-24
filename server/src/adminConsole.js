"use strict";
const readline = require("readline");

// Minimal real admin capability: stdin commands against the live server.
// Not a full admin panel -- list/kick/broadcast/shutdown, which is what
// the brief actually asked for ("administration" + "graceful shutdown").
function startAdminConsole({ tcpServer, eventBus, logger, onShutdown, input = process.stdin, output = process.stdout }) {
  const rl = readline.createInterface({ input, output, terminal: false });

  rl.on("line", (line) => {
    const [command, ...args] = line.trim().split(/\s+/);
    switch (command) {
      case "list": {
        for (const session of tcpServer.sessions.values()) {
          output.write(`${session.id}  player=${session.playerId || "-"}  auth=${session.authenticated}  from=${session.remoteAddress}\n`);
        }
        break;
      }
      case "kick": {
        const target = args[0];
        const session = [...tcpServer.sessions.values()].find((s) => s.id === target || s.playerId === target);
        if (session) {
          session.send("kick", { reason: "kicked by admin" });
          session.framer.socket.end();
        } else {
          output.write(`no session matching "${target}"\n`);
        }
        break;
      }
      case "broadcast": {
        eventBus.broadcast("server_message", { message: args.join(" ") });
        break;
      }
      case "shutdown": {
        logger.info("admin requested shutdown");
        onShutdown();
        break;
      }
      case "": break;
      default:
        output.write(`unknown command "${command}" (list | kick <id> | broadcast <message> | shutdown)\n`);
    }
  });

  return rl;
}

module.exports = { startAdminConsole };
