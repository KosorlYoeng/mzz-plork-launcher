"use strict";
// Wire protocol between MzzPlorkClient and MzzPlork Server: newline-
// delimited JSON (one message object per line) over a plain TCP socket.
// Consistent in style with the bootstrapper<->client stdout JSONL
// protocol (ClientRuntimeInterface.h) -- same reasoning: simple, human-
// debuggable, no schema/codegen tooling required.
//
// Envelope: {"type": "<message-type>", "payload": {...}}
//
// Client -> Server: hello, auth, heartbeat, resource_manifest_request, disconnect
// Server -> Client: hello_ack, auth_result, heartbeat_ack, resource_manifest, event, kick

const { EventEmitter } = require("events");

const PROTOCOL_VERSION = "1.0";

function protocolMajor(version) {
  return String(version).split(".")[0];
}

function isProtocolCompatible(clientVersion) {
  return protocolMajor(clientVersion) === protocolMajor(PROTOCOL_VERSION);
}

function encodeMessage(type, payload) {
  return JSON.stringify({ type, payload: payload || {} }) + "\n";
}

// Wraps a net.Socket, buffering partial reads and emitting one "message"
// event per complete JSON line. Malformed lines emit "protocolError"
// rather than crashing the connection.
class MessageFramer extends EventEmitter {
  constructor(socket) {
    super();
    this.socket = socket;
    this.buffer = "";
    socket.setEncoding("utf8");
    socket.on("data", (chunk) => this._onData(chunk));
  }

  _onData(chunk) {
    this.buffer += chunk;
    let newlineIndex;
    while ((newlineIndex = this.buffer.indexOf("\n")) !== -1) {
      const line = this.buffer.slice(0, newlineIndex);
      this.buffer = this.buffer.slice(newlineIndex + 1);
      if (!line.trim()) continue;
      try {
        const message = JSON.parse(line);
        if (!message || typeof message.type !== "string") {
          this.emit("protocolError", new Error("message missing string 'type'"));
          continue;
        }
        this.emit("message", message.type, message.payload || {});
      } catch (err) {
        this.emit("protocolError", new Error(`malformed JSON line: ${err.message}`));
      }
    }
  }

  send(type, payload) {
    if (this.socket.writable) {
      this.socket.write(encodeMessage(type, payload));
    }
  }
}

module.exports = { PROTOCOL_VERSION, isProtocolCompatible, encodeMessage, MessageFramer };
