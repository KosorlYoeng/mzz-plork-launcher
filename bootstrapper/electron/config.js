const path = require("path");

// Base values come from config.json (a placeholder until a real update
// server exists -- see docs/FLOWS.md). Either can be overridden per
// environment (dev/staging/prod) without touching code or the checked-in
// config file.
function loadConfig(env = process.env) {
  const base = require("./config.json");
  return {
    updateServerUrl: env.MZZPLORK_UPDATE_SERVER_URL || base.updateServerUrl,
    downloadBaseUrl: env.MZZPLORK_DOWNLOAD_BASE_URL || base.downloadBaseUrl
  };
}

module.exports = { loadConfig };
