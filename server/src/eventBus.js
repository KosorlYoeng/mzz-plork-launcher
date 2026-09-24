"use strict";
const { EventEmitter } = require("events");

// Thin wrapper over Node's EventEmitter for the server's internal pub/sub
// (e.g. "session connected", "session authenticated"), plus broadcast()
// to push a protocol "event" message out to every authenticated session.
// This is what "client/server events" concretely means on the server
// side today -- a real, working dispatch mechanism, not a placeholder.
class EventBus extends EventEmitter {
  constructor(sessions) {
    super();
    this.sessions = sessions;
  }

  broadcast(eventType, data) {
    for (const session of this.sessions.values()) {
      if (session.authenticated) {
        session.send("event", { eventType, data });
      }
    }
  }

  // Same as broadcast(), but skips one session -- used for relaying a
  // player's own state/presence to everyone *else*, not back to them.
  broadcastExcept(excludeSessionId, eventType, data) {
    for (const session of this.sessions.values()) {
      if (session.authenticated && session.id !== excludeSessionId) {
        session.send("event", { eventType, data });
      }
    }
  }
}

module.exports = { EventBus };


