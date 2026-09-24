"use strict";
const crypto = require("crypto");
const { ConnectionState, isValidTransition } = require("./connectionState");

// One per connected TCP client. authenticated/playerId are set once
// auth.js issues a session; lastHeartbeatAt drives the timeout watchdog
// in tcpServer.js.
//
// `state` is the formal connection lifecycle (connectionState.js),
// tracked alongside (not instead of) the existing helloReceived/
// authenticated booleans deliberately: those booleans are what every
// protocol guard in tcpServer.js already checks and is tested against,
// and their exact meaning ("has hello ever succeeded," "is auth currently
// valid") isn't quite reconstructible from `state` alone once a session
// has moved on to Disconnecting/Disconnected. `state` adds a real,
// validated, named lifecycle on top -- observable (see `list` in
// adminConsole.js) and structurally guarded (transitionTo() rejects an
// invalid jump) -- without changing any existing guard's behavior.
class Session {
  constructor(framer, remoteAddress) {
    this.id = crypto.randomUUID();
    this.framer = framer;
    this.remoteAddress = remoteAddress;
    this.connectedAt = Date.now();
    this.lastHeartbeatAt = Date.now();
    this.clientProtocolVersion = null;
    this.state = ConnectionState.CONNECTING;
    this.helloReceived = false;
    this.authenticated = false;
    this.clientId = null;
    this.playerId = null;
    this.displayName = null;
    this.resourcesReady = false;
    this.lastPlayerState = null;  // latest {position, rotation, basicState, timestamp} -- see docs/synchronization.md
  }

  send(type, payload) {
    this.framer.send(type, payload);
  }

  touchHeartbeat() {
    this.lastHeartbeatAt = Date.now();
  }

  isHeartbeatExpired(timeoutMs) {
    return Date.now() - this.lastHeartbeatAt > timeoutMs;
  }

  // Returns true and applies the change if `to` is a legal transition
  // from the current state; returns false (state unchanged) otherwise --
  // callers that care can log the rejection, but this never throws, since
  // a rejected transition is a bug to notice, not a reason to crash a
  // live connection's message handling.
  transitionTo(to) {
    if (!isValidTransition(this.state, to)) return false;
    this.state = to;
    return true;
  }
}

module.exports = { Session };
