const https = require("https");
const http = require("http");

function fetchManifest(url) {
  return new Promise((resolve, reject) => {
    const client = url.startsWith("https:") ? https : http;
    const req = client.get(url, { timeout: 5000 }, (res) => {
      if (res.statusCode !== 200) {
        res.resume();
        reject(new Error(`manifest fetch failed: HTTP ${res.statusCode}`));
        return;
      }
      let body = "";
      res.on("data", (chunk) => { body += chunk; });
      res.on("end", () => {
        try {
          resolve(JSON.parse(body));
        } catch (err) {
          reject(new Error(`manifest is not valid JSON: ${err.message}`));
        }
      });
    });
    req.on("timeout", () => req.destroy(new Error("manifest fetch timed out")));
    req.on("error", reject);
  });
}

function isValidComponent(component) {
  if (!component || typeof component !== "object") return false;
  if (typeof component.version !== "string") return false;
  if (!Array.isArray(component.files)) return false;
  for (const file of component.files) {
    if (typeof file.path !== "string") return false;
    if (typeof file.size !== "number") return false;
    if (typeof file.version !== "string") return false;
    if (!/^[a-f0-9]{64}$/.test(file.hash || "")) return false;
    if (file.url !== undefined && typeof file.url !== "string") return false;
  }
  return true;
}

// Validates against schemas/version-manifest.schema.json: a top-level
// `version` string plus `runtime` and `resources` components, each with
// its own version and a files[] list of {path,size,hash,version,url?}.
function isValidManifest(manifest) {
  if (!manifest || typeof manifest !== "object") return false;
  if (typeof manifest.version !== "string") return false;
  if (!isValidComponent(manifest.runtime)) return false;
  if (!isValidComponent(manifest.resources)) return false;
  return true;
}

module.exports = { fetchManifest, isValidManifest };
