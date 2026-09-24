const { app, BrowserWindow } = require("electron");
const path = require("path");
const { runBootstrapper } = require("./bootstrap");
const { loadConfig } = require("./config");

let mainWindow;

app.whenReady().then(() => {
  mainWindow = new BrowserWindow({
    width: 720,
    height: 480,
    webPreferences: {
      preload: path.join(__dirname, "preload.js"),
      contextIsolation: true,
      nodeIntegration: false
    }
  });

  mainWindow.loadFile(path.join(__dirname, "..", "dist-renderer", "index.html"));
  mainWindow.webContents.once("did-finish-load", () => {
    const config = loadConfig();
    runBootstrapper({
      manifestUrl: config.updateServerUrl,
      downloadBaseUrl: config.downloadBaseUrl,
      onProgress: (payload) => {
        if (mainWindow) {
          mainWindow.webContents.send("mzzplork:state", payload);
        }
      }
    });
  });
});

app.on("window-all-closed", () => {
  if (process.platform !== "darwin") app.quit();
});
