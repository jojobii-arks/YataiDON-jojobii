const { contextBridge } = require("electron")

contextBridge.exposeInMainWorld("progressCompanion", {
  platform: process.platform,
  shell: "electron",
})
