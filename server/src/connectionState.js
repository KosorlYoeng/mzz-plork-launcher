"use strict";

// A session's connection lifecycle, following the same enum + validated-
// transition-table convention already used client-side (runtime/src/
// {RuntimeState,ClientState,ResourceState,DownloadState}.h) -- named
// states plus a table that structurally enforces which transitions are
// legal, rather than trusting call sites to only ever set the "right"
// combination of flags. JS has no real enum type, so each state is its
// own human-readable string constant (it doubles as its own ToString()).
const ConnectionState = Object.freeze({
  CONNECTING: "connecting",       // socket accepted, no hello yet
  AUTHENTICATING: "authenticating", // compatible hello received, not yet authenticated
  AUTHENTICATED: "authenticated",   // auth succeeded
  DISCONNECTING: "disconnecting",   // teardown in progress (disconnectSession())
  DISCONNECTED: "disconnected"      // terminal
});

const TRANSITIONS = {
  [ConnectionState.CONNECTING]: [ConnectionState.AUTHENTICATING, ConnectionState.DISCONNECTING],
  [ConnectionState.AUTHENTICATING]: [ConnectionState.AUTHENTICATED, ConnectionState.DISCONNECTING],
  [ConnectionState.AUTHENTICATED]: [ConnectionState.DISCONNECTING],
  [ConnectionState.DISCONNECTING]: [ConnectionState.DISCONNECTED],
  [ConnectionState.DISCONNECTED]: [] // terminal
};

function isValidTransition(from, to) {
  return (TRANSITIONS[from] || []).includes(to);
}

module.exports = { ConnectionState, isValidTransition };
