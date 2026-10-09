'use strict';

var { contextBridge, ipcRenderer } = require('electron');

contextBridge.exposeInMainWorld('web98', {
  versions: process.versions,
  invoke: function (channel, data) {
    return ipcRenderer.invoke(channel, data);
  },
  send: function (channel, data) {
    ipcRenderer.send(channel, data);
  },
  on: function (channel, callback) {
    var listener = function (_event, payload) {
      callback(payload);
    };
    ipcRenderer.on(channel, listener);
    return function () {
      ipcRenderer.removeListener(channel, listener);
    };
  }
});
