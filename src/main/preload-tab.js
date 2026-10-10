'use strict';

/**
 * Minimal preload for tab contents (error pages only). The single exposed
 * method lets the certificate error page request "continue anyway"; the
 * main process validates sender, tab state and URL before trusting it.
 */

var { contextBridge, ipcRenderer } = require('electron');

contextBridge.exposeInMainWorld('web98cert', {
  proceed: function (url) {
    if (typeof url !== 'string') return;
    ipcRenderer.send('cert:proceed', url.slice(0, 8192));
  }
});


// Narrow bridge for the bundled start page; the main process verifies its URL.
contextBridge.exposeInMainWorld('web98home', {
  action: function (action, query) {
    if (typeof action !== 'string' || action.length > 20) {
      return Promise.reject(new Error('Invalid action'));
    }
    return ipcRenderer.invoke('startpage:action', {
      action: action,
      query: typeof query === 'string' ? query.slice(0, 500) : ''
    });
  }
});
