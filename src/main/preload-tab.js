'use strict';

/**
 * Minimal preload for tab contents (error pages only). The single exposed
 * method lets the certificate error page request "continue anyway"; the
 * main process validates sender, tab state and URL before trusting it.
 */

var { contextBridge, ipcRenderer } = require('electron');

contextBridge.exposeInMainWorld('web98cert', {
  proceed: function (url) {
    ipcRenderer.send('cert:proceed', url);
  }
});
