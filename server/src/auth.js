"use strict";
const crypto = require("crypto");

// Minimal session/identity issuance -- NOT a real account system. There
// is no password, no persistent user database, and no verification of
// who the client claims to be. It exists to give the rest of the
// pipeline (session tracking, resource delivery, events) something real
// to plug into. A real account system is future work and is explicitly
// out of scope here; see docs/decisions.md.
function authenticate(clientId, displayName) {
  const playerId = clientId && /^[a-f0-9-]{8,}$/i.test(clientId)
    ? clientId
    : crypto.randomUUID();
  return {
    ok: true,
    playerId,
    sessionId: crypto.randomUUID(),
    displayName: displayName || `Player-${playerId.slice(0, 8)}`
  };
}

module.exports = { authenticate };
