const { contextBridge, ipcRenderer } = require("electron");

contextBridge.exposeInMainWorld("mzzplork", {
  onState: (callback) => {
    ipcRenderer.on("mzzplork:state", (_event, payload) => callback(payload));
  },
  onClientEvent: (callback) => {
    ipcRenderer.on("mzzplork:client-event", (_event, payload) => callback(payload));
  }
});
